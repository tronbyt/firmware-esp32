#include "syslog.h"

#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <sys/socket.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "nvs_settings.h"
#include "raii_utils.hpp"

namespace {

constexpr int SYSLOG_FACILITY = 16;  // local0
constexpr size_t MAX_SYSLOG_MSG_LEN = 512;

int s_sock = -1;
struct sockaddr_in s_dest_addr;
bool s_enabled = false;
vprintf_like_t s_prev_logger = nullptr;
SemaphoreHandle_t s_log_mutex = nullptr;

char s_host[128] = {0};
uint16_t s_port = 514;
char s_hostname[33] = CONFIG_LWIP_LOCAL_HOSTNAME;

char s_log_buffer[MAX_SYSLOG_MSG_LEN];
size_t s_log_len = 0;
char s_packet_buf[MAX_SYSLOG_MSG_LEN + 128];

int syslog_vprintf(const char* fmt, va_list args) {
  va_list args_copy;
  va_copy(args_copy, args);

  int ret = 0;
  if (s_prev_logger) {
    ret = s_prev_logger(fmt, args);
  } else {
    ret = vprintf(fmt, args);
  }

  if (!s_enabled || s_sock < 0) {
    va_end(args_copy);
    return ret;
  }

  if (xPortInIsrContext()) {
    va_end(args_copy);
    return ret;
  }

  raii::MutexGuard lock(s_log_mutex, 10 / portTICK_PERIOD_MS);
  if (lock) {
    int n = vsnprintf(s_log_buffer + s_log_len,
                      sizeof(s_log_buffer) - s_log_len, fmt, args_copy);

    if (n > 0) {
      size_t written = static_cast<size_t>(n);
      if (s_log_len + written >= sizeof(s_log_buffer)) {
        written = sizeof(s_log_buffer) - s_log_len - 1;
      }
      s_log_len += written;
      s_log_buffer[s_log_len] = '\0';

      if (s_log_buffer[s_log_len - 1] == '\n') {
        s_log_buffer[--s_log_len] = '\0';

        int severity = 6;
        if (s_log_len > 0) {
          char* p = s_log_buffer;
          while (*p && (*p == ' ' || *p == '\t' || *p == '\r' ||
                        *p == '\n')) {
            p++;
          }
          switch (*p) {
            case 'E': severity = 3; break;
            case 'W': severity = 4; break;
            case 'I': severity = 6; break;
            case 'D': severity = 7; break;
            case 'V': severity = 7; break;
          }
        }
        int pri = (SYSLOG_FACILITY * 8) + severity;

        char time_str[32] = "-";
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2016 - 1900)) {
          strftime(time_str, sizeof(time_str),
                   "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);
        }

        snprintf(s_hostname, sizeof(s_hostname), "%s",
                 config_get().hostname);
        if (strlen(s_hostname) == 0) strcpy(s_hostname, "-");

        int pkt_len =
            snprintf(s_packet_buf, sizeof(s_packet_buf),
                     "<%d>1 %s %s tronbyt - - - %s", pri, time_str,
                     s_hostname, s_log_buffer);

        if (s_sock >= 0 && pkt_len > 0) {
          sendto(s_sock, s_packet_buf, pkt_len, 0,
                 reinterpret_cast<struct sockaddr*>(&s_dest_addr),
                 sizeof(s_dest_addr));
        }

        s_log_len = 0;
      }
    }
  }

  va_end(args_copy);
  return ret;
}

}  // namespace

esp_err_t syslog_init(const char* addr) {
  if (!addr || strlen(addr) == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_log_mutex) {
    s_log_mutex = xSemaphoreCreateMutex();
  }

  if (s_enabled) {
    syslog_deinit();
  }

  char host_buf[128];
  strncpy(host_buf, addr, sizeof(host_buf) - 1);
  host_buf[sizeof(host_buf) - 1] = '\0';

  char* port_sep = strrchr(host_buf, ':');
  if (port_sep) {
    *port_sep = '\0';
    s_port = static_cast<uint16_t>(atoi(port_sep + 1));
  } else {
    s_port = 514;
  }
  strncpy(s_host, host_buf, sizeof(s_host) - 1);
  s_host[sizeof(s_host) - 1] = '\0';

  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo* res;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", s_port);

  int err = getaddrinfo(s_host, port_str, &hints, &res);
  if (err != 0 || !res) {
    printf("syslog: DNS lookup failed for %s: %d\n", s_host, err);
    return ESP_FAIL;
  }

  memcpy(&s_dest_addr, res->ai_addr, sizeof(s_dest_addr));
  freeaddrinfo(res);

  s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s_sock < 0) {
    printf("syslog: Unable to create socket: errno %d\n", errno);
    return ESP_FAIL;
  }

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  if (setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    printf("syslog: Failed to set SO_SNDTIMEO: errno %d\n", errno);
  }

  s_enabled = true;

  if (!s_prev_logger) {
    s_prev_logger = esp_log_set_vprintf(syslog_vprintf);
  }

  printf("syslog: Initialized. Sending to %s:%d\n", s_host, s_port);
  return ESP_OK;
}

void syslog_deinit(void) {
  s_enabled = false;
  if (s_sock >= 0) {
    close(s_sock);
    s_sock = -1;
  }
}

void syslog_update_config(const char* addr) { syslog_init(addr); }

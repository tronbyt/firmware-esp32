#include "syslog.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_settings.h"

static int s_sock = -1;
static struct sockaddr_in s_dest_addr;
static bool s_enabled = false;
static vprintf_like_t s_prev_logger = NULL;
static SemaphoreHandle_t s_log_mutex = NULL;

static char s_host[128] = {0};
static uint16_t s_port = 514;
static char s_hostname[33] = CONFIG_LWIP_LOCAL_HOSTNAME;

#define SYSLOG_FACILITY 16  // local0
#define MAX_SYSLOG_MSG_LEN 512

static char s_log_buffer[MAX_SYSLOG_MSG_LEN];
static size_t s_log_len = 0;
static char s_packet_buf[MAX_SYSLOG_MSG_LEN + 128];

static int syslog_vprintf(const char* fmt, va_list args) {
  // Copy args for our use later because vprintf consumes it
  va_list args_copy;
  va_copy(args_copy, args);

  // Call the previous logger if it exists (usually outputs to UART)
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

  // Do not attempt to send over network if in ISR context
  if (xPortInIsrContext()) {
    va_end(args_copy);
    return ret;
  }

  // Protect buffers with mutex
  if (s_log_mutex &&
      xSemaphoreTake(s_log_mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    // Format and append directly into static buffer
    int n = vsnprintf(s_log_buffer + s_log_len,
                      sizeof(s_log_buffer) - s_log_len, fmt, args_copy);

    if (n > 0) {
      size_t written = (size_t)n;
      if (s_log_len + written >= sizeof(s_log_buffer)) {
        written = sizeof(s_log_buffer) - s_log_len - 1;
      }
      s_log_len += written;
      s_log_buffer[s_log_len] = '\0';

      // Check for newline (indicates end of log message)
      if (s_log_buffer[s_log_len - 1] == '\n') {
        // Remove trailing newline
        s_log_buffer[--s_log_len] = '\0';

        // Determine log level (simplified)
        int severity = 6;
        if (s_log_len > 0) {
          char* p = s_log_buffer;
          while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
          }
          switch (*p) {
            case 'E':
              severity = 3;
              break;
            case 'W':
              severity = 4;
              break;
            case 'I':
              severity = 6;
              break;
            case 'D':
              severity = 7;
              break;
            case 'V':
              severity = 7;
              break;
          }
        }
        int pri = (SYSLOG_FACILITY * 8) + severity;

        // Timestamp
        char time_str[32] = "-";
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2016 - 1900)) {
          strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000Z",
                   &timeinfo);
        }

        // Hostname
        nvs_get_hostname(s_hostname, sizeof(s_hostname));
        if (strlen(s_hostname) == 0) strcpy(s_hostname, "-");

        // Construct final Syslog packet in static packet buffer
        int pkt_len = snprintf(s_packet_buf, sizeof(s_packet_buf),
                               "<%d>1 %s %s tronbyt - - - %s", pri, time_str,
                               s_hostname, s_log_buffer);

        if (s_sock >= 0 && pkt_len > 0) {
          sendto(s_sock, s_packet_buf, pkt_len, 0,
                 (struct sockaddr*)&s_dest_addr, sizeof(s_dest_addr));
        }

        // Reset buffer for next line
        s_log_len = 0;
      }
    }
    xSemaphoreGive(s_log_mutex);
  }

  va_end(args_copy);
  return ret;
}

esp_err_t syslog_init(const char* addr) {
  if (addr == NULL || strlen(addr) == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_log_mutex == NULL) {
    s_log_mutex = xSemaphoreCreateMutex();
  }

  if (s_enabled) {
    syslog_deinit();
  }

  // Parse addr (host:port)
  char host_buf[128];
  strncpy(host_buf, addr, sizeof(host_buf) - 1);
  host_buf[sizeof(host_buf) - 1] = 0;

  char* port_sep = strrchr(host_buf, ':');
  if (port_sep) {
    *port_sep = 0;
    s_port = atoi(port_sep + 1);
  } else {
    s_port = 514;
  }
  strncpy(s_host, host_buf, sizeof(s_host) - 1);
  s_host[sizeof(s_host) - 1] = '\0';

  // Resolve hostname
  struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_DGRAM,
  };
  struct addrinfo* res;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", s_port);

  int err = getaddrinfo(s_host, port_str, &hints, &res);
  if (err != 0 || res == NULL) {
    // Use standard printf as ESP_LOG might be hooked or recursion might occur
    printf("syslog: DNS lookup failed for %s: %d\n", s_host, err);
    return ESP_FAIL;
  }

  // Configure destination
  memcpy(&s_dest_addr, res->ai_addr, sizeof(s_dest_addr));
  freeaddrinfo(res);

  // Create UDP socket
  s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s_sock < 0) {
    printf("syslog: Unable to create socket: errno %d\n", errno);
    return ESP_FAIL;
  }

  // Set send timeout to avoid blocking if network buffer is full
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;  // 100ms
  if (setsockopt(s_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
    printf("syslog: Failed to set SO_SNDTIMEO: errno %d\n", errno);
  }

  // Hook logger
  s_enabled = true;

  // Only register if not already registered
  if (s_prev_logger == NULL) {
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

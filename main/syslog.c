#include "syslog.h"
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "syslog";

static int s_sock = -1;
static struct sockaddr_in s_dest_addr;
static bool s_enabled = false;
static vprintf_like_t s_prev_logger = NULL;

static char s_host[128] = {0};
static uint16_t s_port = 514;
static char s_hostname[33] = CONFIG_LWIP_LOCAL_HOSTNAME;

#define SYSLOG_FACILITY 16 // local0
#define MAX_SYSLOG_MSG_LEN 512

static void init_sntp(void) {
    if (esp_sntp_enabled()) return;
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    char server[MAX_SNTP_SERVER_LEN + 1] = "pool.ntp.org";
    nvs_get_sntp_server(server, sizeof(server));
    if (strlen(server) == 0) strcpy(server, "pool.ntp.org");

    esp_sntp_setservername(0, server);
    esp_sntp_init();
}

static int syslog_vprintf(const char *fmt, va_list args) {
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

    // Determine log level
    // With CONFIG_LOG_COLORS=n, the format string starts with the level char.
    int severity = 6; // Default INFO
    if (fmt && *fmt) {
        switch (*fmt) {
            case 'E': severity = 3; break;
            case 'W': severity = 4; break;
            case 'I': severity = 6; break;
            case 'D': severity = 7; break;
            case 'V': severity = 7; break;
        }
    }

    char syslog_msg[MAX_SYSLOG_MSG_LEN];
    int offset = 0;

    int pri = (SYSLOG_FACILITY * 8) + severity;

    // 2. Timestamp
    // Use ISO8601 if SNTP is synced, otherwise "-"
    char time_str[32] = "-";
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year > (2016 - 1900)) {
        // Valid time
        strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo); // Simplified, no ms or TZ offset calculation here
    }

    // 3. Hostname
    nvs_get_hostname(s_hostname, sizeof(s_hostname));
    if (strlen(s_hostname) == 0) strcpy(s_hostname, "-");

    // Header construction
    offset += snprintf(syslog_msg + offset, MAX_SYSLOG_MSG_LEN - offset,
                       "<%d>1 %s %s tronbyt - - - ",
                       pri, time_str, s_hostname);

    // 4. MSG
    // Format the user message
    if (offset < MAX_SYSLOG_MSG_LEN) {
        // We perform the printf into the remaining buffer
        vsnprintf(syslog_msg + offset, MAX_SYSLOG_MSG_LEN - offset, fmt, args_copy);
    }

    va_end(args_copy);

    // Send over UDP
    if (s_sock >= 0) {
        sendto(s_sock, syslog_msg, strlen(syslog_msg), 0, (struct sockaddr *)&s_dest_addr, sizeof(s_dest_addr));
    }

    return ret;
}

esp_err_t syslog_init(const char *addr) {
    if (addr == NULL || strlen(addr) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_enabled) {
        syslog_deinit();
    }

    // Parse addr (host:port)
    char host_buf[128];
    strncpy(host_buf, addr, sizeof(host_buf)-1);
    host_buf[sizeof(host_buf)-1] = 0;

    char *port_sep = strrchr(host_buf, ':');
    if (port_sep) {
        *port_sep = 0;
        s_port = atoi(port_sep + 1);
    } else {
        s_port = 514;
    }
    strncpy(s_host, host_buf, sizeof(s_host) - 1);

    // Resolve hostname
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res;
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

    // Init SNTP
    init_sntp();

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

void syslog_update_config(const char *addr) {
    syslog_init(addr);
}

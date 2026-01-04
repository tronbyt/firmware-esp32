#include "ota.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <http_parser.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>

static const char *TAG = "OTA";

static bool is_ip_private(const struct sockaddr *addr) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        return (ip >> 24 == 10) ||               // 10.0.0.0/8
               ((ip >> 20) == 0xAC1) ||          // 172.16.0.0/12
               ((ip >> 16) == 0xC0A8) ||         // 192.168.0.0/16
               (ip >> 24 == 127);                // 127.0.0.0/8
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
        if ((sin6->sin6_addr.s6_addr[0] & 0xFE) == 0xFC) return true;
        if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) return true;
        if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) return true;
    }
    return false;
}

static bool validate_and_rewrite_url(const char *url, char *out_url, size_t out_len) {
    struct http_parser_url u;
    http_parser_url_init(&u);

    size_t url_len = strlen(url);
    if (http_parser_parse_url(url, url_len, 0, &u) != 0) {
        ESP_LOGE(TAG, "Failed to parse OTA URL");
        return false;
    }

    if (!(u.field_set & (1 << UF_SCHEMA))) {
        ESP_LOGE(TAG, "URL schema missing");
        return false;
    }

    const char *schema = url + u.field_data[UF_SCHEMA].off;
    size_t schema_len = u.field_data[UF_SCHEMA].len;

    if (schema_len == 5 && strncasecmp(schema, "https", 5) == 0) {
        if (url_len >= out_len) return false;
        memcpy(out_url, url, url_len + 1);
        return true;
    }

    if (schema_len != 4 || strncasecmp(schema, "http", 4) != 0) {
        ESP_LOGE(TAG, "Unsupported protocol: %.*s", (int)schema_len, schema);
        return false;
    }

    if (!(u.field_set & (1 << UF_HOST))) return false;
    char host[256];
    size_t host_len = u.field_data[UF_HOST].len;
    if (host_len >= sizeof(host)) return false;
    memcpy(host, url + u.field_data[UF_HOST].off, host_len);
    host[host_len] = '\0';

    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return false;

    bool private_ip = false;
    char ip_str[INET6_ADDRSTRLEN];
    bool is_ipv6 = false;

    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        if (is_ip_private(p->ai_addr)) {
            void *addr_ptr = (p->ai_family == AF_INET) ? (void*)&((struct sockaddr_in *)p->ai_addr)->sin_addr : (void*)&((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
            is_ipv6 = (p->ai_family == AF_INET6);
            inet_ntop(p->ai_family, addr_ptr, ip_str, sizeof(ip_str));
            private_ip = true;
            break;
        }
    }
    freeaddrinfo(res);

    if (!private_ip) {
        ESP_LOGE(TAG, "Security violation: OTA via HTTP allowed only for private IPs.");
        return false;
    }

    int written = snprintf(out_url, out_len, "http://");
    if (u.field_set & (1 << UF_USERINFO)) written += snprintf(out_url + written, out_len - written, "%.*s@", (int)u.field_data[UF_USERINFO].len, url + u.field_data[UF_USERINFO].off);
    written += snprintf(out_url + written, out_len - written, is_ipv6 ? "[%s]" : "%s", ip_str);
    if (u.field_set & (1 << UF_PORT)) written += snprintf(out_url + written, out_len - written, ":%.*s", (int)u.field_data[UF_PORT].len, url + u.field_data[UF_PORT].off);
    if (u.field_set & (1 << UF_PATH)) written += snprintf(out_url + written, out_len - written, "%.*s", (int)u.field_data[UF_PATH].len, url + u.field_data[UF_PATH].off);
    if (u.field_set & (1 << UF_QUERY)) written += snprintf(out_url + written, out_len - written, "?%.*s", (int)u.field_data[UF_QUERY].len, url + u.field_data[UF_QUERY].off);
    if (u.field_set & (1 << UF_FRAGMENT)) written += snprintf(out_url + written, out_len - written, "#%.*s", (int)u.field_data[UF_FRAGMENT].len, url + u.field_data[UF_FRAGMENT].off);
    
    ESP_LOGI(TAG, "Rewritten OTA URL: %s", out_url);
    return true;
}

void run_ota(const char* url) {
    char final_url[512] = {0};
    if (!validate_and_rewrite_url(url, final_url, sizeof(final_url))) {
        return;
    }

    ESP_LOGI(TAG, "Starting OTA update from URL: %s", final_url);

    esp_http_client_config_t http_config = {
        .url = final_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
        .save_client_session = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = true,
        // .partition.staging defaults to NULL, which correctly finds the 
        // next partition based on the currently running one (esp_ota_get_running_partition).
    };

    // We proceed without explicitly attempting to "sync" otadata (via set_boot_partition),
    // because doing so on a running partition that fails strict validation would abort
    // the update process. esp_https_ota will correctly identify the next slot.

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Update successful. Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Update failed: %s", esp_err_to_name(ret));
    }
}

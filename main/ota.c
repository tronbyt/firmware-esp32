#include "ota.h"
#include "display.h"
#include "gfx.h"
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
        // fc00::/7 (Unique Local Addresses)
        if ((sin6->sin6_addr.s6_addr[0] & 0xFE) == 0xFC) return true;
        // fe80::/10 (Link-local)
        if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) return true;
        // ::1 (Loopback)
        if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) return true;
    }
    return false;
}

static bool check_url_protocol(const char *url, size_t url_len, const struct http_parser_url *u, char *out_url, size_t out_len) {
    if (u->field_set & (1 << UF_SCHEMA)) {
        const char *schema = url + u->field_data[UF_SCHEMA].off;
        size_t schema_len = u->field_data[UF_SCHEMA].len;

        if (schema_len == 5 && strncasecmp(schema, "https", 5) == 0) {
            if (url_len >= out_len) {
                ESP_LOGE(TAG, "HTTPS URL is too long for buffer");
                return false;
            }
            memcpy(out_url, url, url_len + 1);
            return true; // HTTPS is allowed
        }
        if (schema_len != 4 || strncasecmp(schema, "http", 4) != 0) {
            ESP_LOGE(TAG, "Unsupported protocol: %.*s", (int)schema_len, schema);
            return false;
        }
    } else {
        ESP_LOGE(TAG, "URL schema missing");
        return false;
    }
    return true; // Is HTTP
}

static bool resolve_and_validate_host(const char *url, const struct http_parser_url *u, char *ip_str, size_t ip_str_len, bool *is_ipv6) {
    if (!(u->field_set & (1 << UF_HOST))) {
        ESP_LOGE(TAG, "URL host missing");
        return false;
    }

    char host[256];
    size_t host_len = u->field_data[UF_HOST].len;
    if (host_len >= sizeof(host)) {
        ESP_LOGE(TAG, "URL host is too long");
        return false;
    }
    memcpy(host, url + u->field_data[UF_HOST].off, host_len);
    host[host_len] = '\0';

    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        ESP_LOGE(TAG, "DNS resolution failed for %s", host);
        return false;
    }

    bool private_ip = false;
    *is_ipv6 = false;

    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        if (is_ip_private(p->ai_addr)) {
            void *addr_ptr;
            if (p->ai_family == AF_INET) {
                addr_ptr = &((struct sockaddr_in *)p->ai_addr)->sin_addr;
                *is_ipv6 = false;
            } else {
                addr_ptr = &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr;
                *is_ipv6 = true;
            }
            if (inet_ntop(p->ai_family, addr_ptr, ip_str, ip_str_len) != NULL) {
                private_ip = true;
                break;
            }
        }
    }
    freeaddrinfo(res);

    if (!private_ip) {
        ESP_LOGE(TAG, "Security violation: OTA via HTTP allowed only for private IPs. Host: %s", host);
        return false;
    }
    return true;
}

static bool reconstruct_url(const char *url, const struct http_parser_url *u, const char *ip_str, bool is_ipv6, char *out_url, size_t out_len) {
    int written = 0;

#define APPEND_URL_PART(format, ...) do { \
    size_t remaining = (written < out_len) ? out_len - written : 0; \
    int ret = snprintf(out_url + written, remaining, format, ##__VA_ARGS__); \
    if (ret < 0) { \
        ESP_LOGE(TAG, "URL reconstruction failed"); \
        return false; \
    } \
    written += ret; \
} while (0)

    APPEND_URL_PART("http://");

    if (u->field_set & (1 << UF_USERINFO)) {
        APPEND_URL_PART("%.*s@", (int)u->field_data[UF_USERINFO].len, url + u->field_data[UF_USERINFO].off);
    }

    if (is_ipv6) {
        APPEND_URL_PART("[%s]", ip_str);
    } else {
        APPEND_URL_PART("%s", ip_str);
    }

    if (u->field_set & (1 << UF_PORT)) {
        APPEND_URL_PART(":%.*s", (int)u->field_data[UF_PORT].len, url + u->field_data[UF_PORT].off);
    }

    if (u->field_set & (1 << UF_PATH)) {
        APPEND_URL_PART("%.*s", (int)u->field_data[UF_PATH].len, url + u->field_data[UF_PATH].off);
    }

    if (u->field_set & (1 << UF_QUERY)) {
        APPEND_URL_PART("?%.*s", (int)u->field_data[UF_QUERY].len, url + u->field_data[UF_QUERY].off);
    }

    if (u->field_set & (1 << UF_FRAGMENT)) {
        APPEND_URL_PART("#%.*s", (int)u->field_data[UF_FRAGMENT].len, url + u->field_data[UF_FRAGMENT].off);
    }

#undef APPEND_URL_PART

    if (written >= out_len) {
        ESP_LOGE(TAG, "Rewritten URL is too long for buffer");
        return false;
    }
    return true;
}

static bool validate_and_rewrite_url(const char *url, char *out_url, size_t out_len) {
    struct http_parser_url u;
    http_parser_url_init(&u);

    size_t url_len = strlen(url);
    if (http_parser_parse_url(url, url_len, 0, &u) != 0) {
        ESP_LOGE(TAG, "Failed to parse OTA URL");
        return false;
    }

    // Check protocol and handle HTTPS
    if (!check_url_protocol(url, url_len, &u, out_url, out_len)) {
        return false;
    }

    // If check_url_protocol returns true and out_url is filled (HTTPS case), we are done.
    // However, the function returns true for HTTP as well, but doesn't fill out_url.
    // We need to check if it was HTTPS.
    if (strncmp(out_url, "https", 5) == 0) {
        return true;
    }

    // It is HTTP. Resolve and validate host.
    char ip_str[INET6_ADDRSTRLEN];
    bool is_ipv6;
    if (!resolve_and_validate_host(url, &u, ip_str, sizeof(ip_str), &is_ipv6)) {
        return false;
    }

    // Reconstruct URL with resolved IP
    if (!reconstruct_url(url, &u, ip_str, is_ipv6, out_url, out_len)) {
        return false;
    }

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
        .save_client_session = true, // Enable TLS session resumption
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = true,
    };

    // Stop animation and graphics loop
    gfx_stop();
    vTaskDelay(pdMS_TO_TICKS(100)); // Short grace period

    // Initialize display for OTA progress
    // We need to initialize BOTH buffers to ensure no "bleed through" of previous content
    // when flipping buffers during the OTA process.

    // Frame 1 (Back Buffer -> Front)
    display_clear();
    display_text("OTA Update", 2, 10, 0, 0, 255, 1);
    display_flip();

    // Frame 2 (Current Back Buffer)
    display_clear();
    display_text("OTA Update", 2, 10, 0, 0, 255, 1);
    // Don't flip here, we want to draw the progress bar on top of this background in the loop

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed: %s", esp_err_to_name(err));
        display_clear();
        display_text("OTA Fail", 2, 10, 255, 0, 0, 1);
        display_flip();
        vTaskDelay(pdMS_TO_TICKS(2000));
        gfx_start();
        return;
    }

    int bar_x = 2;
    int bar_y = 20;
    int bar_w = 60;
    int bar_h = 4;
    int last_progress_width = -1;

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int cur_len = esp_https_ota_get_image_len_read(https_ota_handle);
        int total_len = esp_https_ota_get_image_size(https_ota_handle);

        if (total_len > 0) {
            int progress_width = (cur_len * bar_w) / total_len;

            if (progress_width != last_progress_width) {
                // Draw background (dim)
                display_fill_rect(bar_x, bar_y, bar_w, bar_h, 10, 10, 10);
                // Draw progress (green)
                if (progress_width > 0) {
                    display_fill_rect(bar_x, bar_y, progress_width, bar_h, 0, 255, 0);
                }
                display_flip();
                last_progress_width = progress_width;
            }
        }

        // Yield to keep watchdog happy
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA Update failed: %s", esp_err_to_name(err));
        esp_https_ota_finish(https_ota_handle);
        display_clear();
        display_text("OTA Fail", 2, 10, 255, 0, 0, 1);
        display_flip();
        vTaskDelay(pdMS_TO_TICKS(2000));
        gfx_start();
    } else {
        err = esp_https_ota_finish(https_ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA Update successful. Rebooting...");
            display_clear();
            display_text("Rebooting", 2, 10, 0, 255, 0, 1);
            display_flip();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA Finish failed: %s", esp_err_to_name(err));
            display_clear();
            display_text("OTA Fail", 2, 10, 255, 0, 0, 1);
            display_flip();
            vTaskDelay(pdMS_TO_TICKS(2000));
            gfx_start();
        }
    }
}

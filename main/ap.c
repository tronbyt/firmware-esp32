#include "ap.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "nvs_settings.h"
#include "wifi.h"

#define TAG "AP"

// Default AP configuration
#define DEFAULT_AP_SSID "TRON-CONFIG"
#define DEFAULT_AP_PASSWORD ""

// DNS server task handle
static TaskHandle_t s_dns_task_handle = NULL;
static httpd_handle_t s_server = NULL;
static TimerHandle_t s_ap_shutdown_timer = NULL;

// HTML Parts for chunked response
static const char *s_html_part1 =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>Tronbyt WiFi Setup</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }"
    "h1 { color: #333; }"
    ".form-container { max-width: 400px; margin: 0 auto; }"
    ".form-group { margin-bottom: 15px; }"
    "label { display: block; margin-bottom: 5px; font-weight: bold; }"
    "input[type='text'], input[type='password'] { width: 100%; padding: 8px; "
    "box-sizing: border-box; }"
    "button { background-color: #4CAF50; color: white; padding: 10px 15px; "
    "border: none; cursor: pointer; }"
    "button:hover { background-color: #45a049; }"
    ".networks { margin-top: 20px; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='form-container'>"
    "<h1>Tronbyt WiFi Setup</h1>"
    "<form action='/save' method='post' "
    "enctype='application/x-www-form-urlencoded'>"
    "<div class='form-group'>"
    "<label for='ssid'>WiFi Network Name (2.4Ghz Only) :</label>"
    "<input type='text' id='ssid' name='ssid' maxlength='32'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='password'>WiFi Password:</label>"
    "<input type='password' id='password' name='password' maxlength='64'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='image_url'>Image URL:</label>"
    "<input type='text' id='image_url' name='image_url' maxlength='128' "
    "value='";

static const char *s_html_part2 =
    "'>"
    "</div>";

#if CONFIG_BOARD_TIDBYT_GEN1 || CONFIG_BOARD_MATRIXPORTAL_S3
static const char *s_html_part3_start =
    "<div class='form-group'>"
    "<label>"
    "<input type='checkbox' id='swap_colors' name='swap_colors' value='1' ";

static const char *s_html_part3_end =
    ">"
    " Swap Colors (Gen1/S3 only - requires reboot)"
    "</label>"
    "</div>";
#endif

static const char *s_html_part4 =
    "<button type='submit'>Save and Connect</button>"
    "</form>"
    "<hr>"
    "<h3>Firmware Update</h3>"
    "<div class='form-group'>"
    "<input type='file' id='fw_file' accept='.bin'>"
    "</div>"
    "<button id='upd_btn' onclick='uploadFirmware()'>Update Firmware</button>"
    "<div id='progress' style='margin-top: 10px;'></div>"
    "<script>"
    "function uploadFirmware() {"
    "var f=document.getElementById('fw_file').files[0];"
    "if(!f){alert('Select file');return;}"
    "var "
    "b=document.getElementById('upd_btn');b.disabled=true;b.innerText='"
    "Uploading...';"
    "var x=new XMLHttpRequest();x.open('POST','/update',true);"
    "x.upload.onprogress=function(e){if(e.lengthComputable){document."
    "getElementById('progress').innerText='Upload: "
    "'+((e.loaded/e.total)*100).toFixed(0)+'%%';}};"
    "x.onload=function(){if(x.status==200){document.getElementById('progress')."
    "innerText='Success! "
    "Rebooting...';}else{document.getElementById('progress').innerText='Failed:"
    " '+x.statusText;b.disabled=false;b.innerText='Update Firmware';}};"
    "x.onerror=function(){document.getElementById('progress').innerText='Error'"
    ";b.disabled=false;b.innerText='Update Firmware';};"
    "x.send(f);"
    "}"
    "</script>"
    "</div>"
    "</body>"
    "</html>";

// Success page HTML
static const char *s_success_html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>WiFi Configuration Saved</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; "
    "text-align: center; }"
    "h1 { color: #4CAF50; }"
    "p { margin-bottom: 20px; }"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Configuration Saved!</h1>"
    "<p>WiFi credentials and image URL have been saved.</p>"
    "<p>The device will now reboot and attempt to connect to the WiFi "
    "network.</p>"
    "<p>You can close this page.</p>"
    "</body>"
    "</html>";

// Forward declarations
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t save_handler(httpd_req_t *req);
static esp_err_t update_handler(httpd_req_t *req);
static esp_err_t captive_portal_handler(httpd_req_t *req);
static void url_decode(char *str);
static void start_dns_server(void);
static void stop_dns_server(void);

// DNS server implementation
#define DNS_PORT 53
#define DNS_MAX_LEN 512

typedef struct __attribute__((packed)) {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} dns_header_t;

static void dns_server_task(void *pvParameters) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create DNS socket");
    vTaskDelete(NULL);
    return;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(DNS_PORT);

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind DNS socket");
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "DNS server started on port 53");

  char rx_buffer[DNS_MAX_LEN];
  char tx_buffer[DNS_MAX_LEN];
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  while (1) {
    int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                       (struct sockaddr *)&client_addr, &client_addr_len);

    if (len < 0) {
      ESP_LOGE(TAG, "DNS recvfrom failed");
      break;
    }

    if (len < sizeof(dns_header_t)) {
      continue;  // Invalid DNS packet
    }

    dns_header_t *header = (dns_header_t *)rx_buffer;

    if ((ntohs(header->flags) & 0x8000) != 0) {
      continue;
    }

    memcpy(tx_buffer, rx_buffer, len);
    dns_header_t *resp_header = (dns_header_t *)tx_buffer;

    resp_header->flags = htons(0x8400);

    int response_len = len;
    int answers_added = 0;

    uint16_t num_questions = ntohs(header->qdcount);
    if (num_questions > 0 && response_len + 16 < DNS_MAX_LEN) {
      uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                          0x00, 0x3C, 0x00, 0x04, 10,   10,   0,    1};

      memcpy(tx_buffer + response_len, answer, sizeof(answer));
      response_len += sizeof(answer);
      answers_added = 1;
    }

    resp_header->ancount = htons(answers_added);

    sendto(sock, tx_buffer, response_len, 0, (struct sockaddr *)&client_addr,
           client_addr_len);
  }

  close(sock);
  vTaskDelete(NULL);
}

static void start_dns_server(void) {
  if (s_dns_task_handle != NULL) {
    ESP_LOGW(TAG, "DNS server already running");
    return;
  }
  xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle);
}

static void stop_dns_server(void) {
  if (s_dns_task_handle != NULL) {
    vTaskDelete(s_dns_task_handle);
    s_dns_task_handle = NULL;
    ESP_LOGI(TAG, "DNS server stopped");
  }
}

esp_err_t ap_start(void) {
  if (s_server != NULL) {
    ESP_LOGI(TAG, "Web server already started");
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 10;
  config.max_resp_headers = 16;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;

  ESP_LOGI(TAG, "Starting web server on 10.10.0.1:%d", config.server_port);

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start web server");
    return ESP_FAIL;
  }

  httpd_uri_t root_uri = {.uri = "/",
                          .method = HTTP_GET,
                          .handler = root_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &root_uri);

  httpd_uri_t save_uri = {.uri = "/save",
                          .method = HTTP_POST,
                          .handler = save_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &save_uri);

  httpd_uri_t update_uri = {.uri = "/update",
                            .method = HTTP_POST,
                            .handler = update_handler,
                            .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &update_uri);

  httpd_uri_t hotspot_detect_uri = {.uri = "/hotspot-detect.html",
                                    .method = HTTP_GET,
                                    .handler = captive_portal_handler,
                                    .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &hotspot_detect_uri);

  httpd_uri_t generate_204_uri = {.uri = "/generate_204",
                                  .method = HTTP_GET,
                                  .handler = captive_portal_handler,
                                  .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &generate_204_uri);

  httpd_uri_t ncsi_uri = {.uri = "/ncsi.txt",
                          .method = HTTP_GET,
                          .handler = captive_portal_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &ncsi_uri);

  httpd_uri_t wildcard_uri = {.uri = "/*",
                              .method = HTTP_GET,
                              .handler = captive_portal_handler,
                              .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &wildcard_uri);

  start_dns_server();

  return ESP_OK;
}

httpd_handle_t ap_get_server(void) { return s_server; }

void ap_reregister_wildcard(void) {
  if (!s_server) {
    return;
  }
  httpd_unregister_uri_handler(s_server, "/*", HTTP_GET);
  httpd_uri_t wildcard_uri = {.uri = "/*",
                              .method = HTTP_GET,
                              .handler = captive_portal_handler,
                              .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &wildcard_uri);
}

esp_err_t ap_stop(void) {
  if (s_server == NULL) {
    return ESP_OK;
  }
  stop_dns_server();
  esp_err_t err = httpd_stop(s_server);
  s_server = NULL;
  return err;
}

static void ap_shutdown_timer_callback(TimerHandle_t xTimer) {
  ESP_LOGI(TAG, "Shutting down config portal");
  ap_stop();
  esp_wifi_set_mode(WIFI_MODE_STA);
}

static esp_err_t root_handler(httpd_req_t *req) {
  const char *image_url = nvs_get_image_url();
  ESP_LOGI(TAG, "Serving root page (chunked)");

  httpd_resp_set_type(req, "text/html");

  esp_err_t ret = ESP_OK;

  do {
    // Send Part 1
    if ((ret = httpd_resp_send_chunk(req, s_html_part1,
                                     HTTPD_RESP_USE_STRLEN)) != ESP_OK)
      break;

    // Send Image URL (Dynamic)
    if (image_url && image_url[0]) {
      if ((ret = httpd_resp_send_chunk(req, image_url,
                                       HTTPD_RESP_USE_STRLEN)) != ESP_OK)
        break;
    }

    // Send Part 2
    if ((ret = httpd_resp_send_chunk(req, s_html_part2,
                                     HTTPD_RESP_USE_STRLEN)) != ESP_OK)
      break;

#if CONFIG_BOARD_TIDBYT_GEN1 || CONFIG_BOARD_MATRIXPORTAL_S3
    // Send Swap Colors Checkbox (Conditional)
    if ((ret = httpd_resp_send_chunk(req, s_html_part3_start,
                                     HTTPD_RESP_USE_STRLEN)) != ESP_OK)
      break;
    if (nvs_get_swap_colors()) {
      if ((ret = httpd_resp_send_chunk(req, "checked",
                                       HTTPD_RESP_USE_STRLEN)) != ESP_OK)
        break;
    }
    if ((ret = httpd_resp_send_chunk(req, s_html_part3_end,
                                     HTTPD_RESP_USE_STRLEN)) != ESP_OK)
      break;
#endif

    // Send Part 4 (End)
    if ((ret = httpd_resp_send_chunk(req, s_html_part4,
                                     HTTPD_RESP_USE_STRLEN)) != ESP_OK)
      break;

    // Finish response
    ret = httpd_resp_send_chunk(req, NULL, 0);
  } while (0);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send response chunk: %s", esp_err_to_name(ret));
    // On failure, response is likely broken and connection will be closed by
    // httpd.
  }

  return ret;
}

static void url_decode(char *str) {
  char *src = str;
  char *dst = str;

  while (*src) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = {src[1], src[2], 0};
      *dst = (char)strtol(hex, NULL, 16);
      src += 3;
    } else if (*src == '+') {
      *dst = ' ';
      src++;
    } else {
      *dst = *src;
      src++;
    }
    dst++;
  }
  *dst = '\0';
}

static esp_err_t save_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "Processing form submission");

  char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for form data");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server Error");
    return ESP_FAIL;
  }

  int ret, remaining = req->content_len;
  int received = 0;

  if (remaining > 4095) {
    ESP_LOGE(TAG, "Form data too large: %d bytes", remaining);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form data too large");
    free(buf);
    return ESP_FAIL;
  }

  while (remaining > 0) {
    ret = httpd_req_recv(req, buf + received, remaining);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "Failed to receive form data");
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                          "Failed to receive form data");
      free(buf);
      return ESP_FAIL;
    }
    received += ret;
    remaining -= ret;
  }

  buf[received] = '\0';
  ESP_LOGI(TAG, "Received form data (%d bytes)", received);

  // Buffers need to be large enough to hold URL-encoded data (roughly 3x size)
  char ssid[100] = {0};
  char password[200] = {0};
  char image_url[400] = {0};
  char swap_val[2] = {0};
  bool swap_colors = false;

  // Use httpd_query_key_value to parse form data
  if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
    ESP_LOGD(TAG, "SSID param missing");
  }

  if (httpd_query_key_value(buf, "password", password, sizeof(password)) !=
      ESP_OK) {
    ESP_LOGD(TAG, "Password param missing");
  }

  if (httpd_query_key_value(buf, "image_url", image_url, sizeof(image_url)) !=
      ESP_OK) {
    ESP_LOGD(TAG, "Image URL param missing");
  }

  if (httpd_query_key_value(buf, "swap_colors", swap_val, sizeof(swap_val)) ==
      ESP_OK) {
    swap_colors = (strcmp(swap_val, "1") == 0);
  }

  url_decode(ssid);
  url_decode(password);
  url_decode(image_url);

  ESP_LOGI(TAG, "Received SSID: %s, Image URL: %s, Swap Colors: %s", ssid,
           image_url, swap_colors ? "true" : "false");

  nvs_set_ssid(ssid);
  nvs_set_password(password);
  nvs_set_image_url(strlen(image_url) < 6 ? NULL : image_url);
  nvs_set_swap_colors(swap_colors);
  nvs_save_settings();

  free(buf);

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, s_success_html, strlen(s_success_html));

  // Delay to allow response to be sent, then reboot
  ESP_LOGI(TAG, "Configuration saved - rebooting in 1 second...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}

#define OTA_BUFFER_SIZE 1024

static esp_err_t update_handler(httpd_req_t *req) {
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;
  char *buf = heap_caps_malloc(OTA_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (buf == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Alloc failed");
    return ESP_FAIL;
  }

  update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG, "No OTA partition found");
    free(buf);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
           update_partition->subtype, update_partition->address);

  esp_err_t err =
      esp_ota_begin(update_partition, req->content_len, &update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
    free(buf);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "OTA begin failed");
    return ESP_FAIL;
  }

  int received;
  int remaining = req->content_len;

  while (remaining > 0) {
    received = httpd_req_recv(
        req, buf, (remaining < OTA_BUFFER_SIZE ? remaining : OTA_BUFFER_SIZE));
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "File receive failed");
      esp_ota_end(update_handle);
      free(buf);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Receive failed");
      return ESP_FAIL;
    }

    err = esp_ota_write(update_handle, buf, received);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
      esp_ota_end(update_handle);
      free(buf);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
      return ESP_FAIL;
    }

    remaining -= received;
  }

  free(buf);

  err = esp_ota_end(update_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)",
             esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Set boot failed");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "OTA Success! Rebooting...");
  httpd_resp_send(req, "OK", 2);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}

static esp_err_t captive_portal_handler(httpd_req_t *req) {
  char *host_buf = NULL;
  bool serve_directly = false;

  size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
  if (host_len > 0) {
    host_buf = malloc(host_len + 1);
    if (host_buf == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory for Host header");
    } else {
      if (httpd_req_get_hdr_value_str(req, "Host", host_buf, host_len + 1) !=
          ESP_OK) {
        free(host_buf);
        host_buf = NULL;
      }
    }
  }

  if (host_buf != NULL && (strcmp(host_buf, "10.10.0.1") == 0 ||
                           strstr(host_buf, "10.10.0.1") != NULL)) {
    serve_directly = true;
  }

  if (host_buf != NULL) {
    free(host_buf);
    host_buf = NULL;
  }

  if (serve_directly) {
    return root_handler(req);
  }

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://10.10.0.1/");
  httpd_resp_send(req, NULL, 0);

  return ESP_OK;
}

void ap_init_netif(void) {
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

  // Configure AP IP address to 10.10.0.1
  esp_netif_ip_info_t ip_info;
  IP4_ADDR(&ip_info.ip, 10, 10, 0, 1);
  IP4_ADDR(&ip_info.gw, 10, 10, 0, 1);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

  // Stop DHCP server before changing IP
  esp_netif_dhcps_stop(ap_netif);

  // Set the new IP address
  esp_err_t ap_err = esp_netif_set_ip_info(ap_netif, &ip_info);
  if (ap_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AP IP info: %s", esp_err_to_name(ap_err));
  } else {
    ESP_LOGI(TAG, "AP IP address set to 10.10.0.1");
  }

  // Start DHCP server with new configuration
  esp_netif_dhcps_start(ap_netif);
}

void ap_configure(void) {
  // Set WiFi mode to AP+STA
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  // Configure AP with explicit settings
  wifi_config_t ap_config = {0};
  strcpy((char *)ap_config.ap.ssid, DEFAULT_AP_SSID);
  ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);

  // Pick a random WiFi channel (1-11 for 2.4GHz)
  uint8_t random_channel = (esp_random() % 11) + 1;
  ap_config.ap.channel = random_channel;

  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.beacon_interval = 100;

  ESP_LOGI(TAG, "Setting AP SSID: %s on channel %d", DEFAULT_AP_SSID,
           random_channel);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
}

void ap_start_shutdown_timer(void) {
  if (s_ap_shutdown_timer != NULL) {
    xTimerDelete(s_ap_shutdown_timer, 0);
    s_ap_shutdown_timer = NULL;
  }

  s_ap_shutdown_timer =
      xTimerCreate("ap_shutdown_timer",
                   pdMS_TO_TICKS(2 * 60 * 1000),  // 2 minutes in milliseconds
                   pdFALSE,                       // One-shot timer
                   NULL,                          // No timer ID
                   ap_shutdown_timer_callback     // Callback function
      );

  if (s_ap_shutdown_timer != NULL) {
    if (xTimerStart(s_ap_shutdown_timer, 0) == pdPASS) {
      ESP_LOGI(TAG, "AP will automatically shut down in 2 minutes");
    } else {
      ESP_LOGE(TAG, "Failed to start AP shutdown timer");
      xTimerDelete(s_ap_shutdown_timer, 0);
      s_ap_shutdown_timer = NULL;
    }
  } else {
    ESP_LOGE(TAG, "Failed to create AP shutdown timer");
  }
}

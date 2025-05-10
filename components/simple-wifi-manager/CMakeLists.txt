idf_component_register(
    SRCS "simple_wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_server esp_wifi
)

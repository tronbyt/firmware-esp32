idf_component_register(
    SRCS "WifiManagerEsp32.cpp" "httpServer.cpp" "jsonDecoder.cpp" "SPIFFSControler.cpp" "dnsRedirector.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_http_server" "nvs_flash" "json" "esp_wifi" "esp_event" "spiffs"
)

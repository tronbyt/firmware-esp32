if(IDF_VERSION_MAJOR GREATER_EQUAL 4)
    idf_component_register(SRC_DIRS src
        REQUIRES log nvs_flash mdns wpa_supplicant lwip esp_http_server json esp_wifi
        INCLUDE_DIRS src)
    
    # Add binary data
    target_add_binary_data(${COMPONENT_TARGET} "src/code.js" TEXT)
    target_add_binary_data(${COMPONENT_TARGET} "src/index.html" TEXT)
    target_add_binary_data(${COMPONENT_TARGET} "src/style.css" TEXT)
else()
    set(COMPONENT_SRCDIRS src)
    set(COMPONENT_ADD_INCLUDEDIRS src)
    set(COMPONENT_REQUIRES log nvs_flash mdns wpa_supplicant lwip esp_http_server)
    register_component()
endif()

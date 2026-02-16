# Include secrets
include(${CMAKE_SOURCE_DIR}/secrets.cmake OPTIONAL)

# Define defaults if not set in secrets.cmake
if(NOT DEFINED VAL_WIFI_SSID)
    set(VAL_WIFI_SSID "XplaceholderWIFISSID____________")
endif()
if(NOT DEFINED VAL_WIFI_PASSWORD)
    set(VAL_WIFI_PASSWORD "XplaceholderWIFIPASSWORD________________________________________")
endif()
if(NOT DEFINED VAL_REMOTE_URL)
    set(VAL_REMOTE_URL "XplaceholderREMOTEURL___________________________________________________________________________________________________________")
endif()

idf_component_register(SRCS "main.c"
                            "display.cpp"
                            "gfx.c"
                            "ota.c" 
                            "remote.c" 
                            "wifi.c"
                            "ap.c"
                            "nvs_settings.c"
                            "dns_wrapper.c"
                            "syslog.c"
                            "sntp.c"
                            "heap_monitor.c"
                       INCLUDE_DIRS ".")

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_getaddrinfo" APPEND)

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    WIFI_SSID="${VAL_WIFI_SSID}"
    WIFI_PASSWORD="${VAL_WIFI_PASSWORD}"
    REMOTE_URL="${VAL_REMOTE_URL}"
)

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

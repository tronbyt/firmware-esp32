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
if(NOT DEFINED VAL_REFRESH_INTERVAL_SECONDS)
    set(VAL_REFRESH_INTERVAL_SECONDS 60)
endif()
if(NOT DEFINED VAL_DEFAULT_BRIGHTNESS)
    set(VAL_DEFAULT_BRIGHTNESS 50)
endif()
if(NOT DEFINED VAL_ENABLE_AP_MODE)
    set(VAL_ENABLE_AP_MODE 1)
endif()
if(NOT DEFINED VAL_AP_MODE_DEFAULT)
    set(VAL_AP_MODE_DEFAULT 1)
endif()
if(NOT DEFINED VAL_WIFI_POWER_SAVE_MODE)
    set(VAL_WIFI_POWER_SAVE_MODE 1)
endif()
if(NOT DEFINED VAL_SKIP_DISPLAY_VERSION)
    set(VAL_SKIP_DISPLAY_VERSION 0)
endif()
if(NOT DEFINED VAL_PREFER_IPV6)
    set(VAL_PREFER_IPV6 0)
endif()

idf_component_register(SRCS "main.c" 
                            "display.cpp" 
                            "flash.c" 
                            "gfx.c" 
                            "ota.c" 
                            "remote.c" 
                            "wifi.c"
                            "ap.c"
                            "nvs_settings.c"
                       INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    WIFI_SSID="${VAL_WIFI_SSID}"
    WIFI_PASSWORD="${VAL_WIFI_PASSWORD}"
    REMOTE_URL="${VAL_REMOTE_URL}"
    REFRESH_INTERVAL_SECONDS=${VAL_REFRESH_INTERVAL_SECONDS}
    DEFAULT_BRIGHTNESS=${VAL_DEFAULT_BRIGHTNESS}
    ENABLE_AP_MODE=${VAL_ENABLE_AP_MODE}
    AP_MODE_DEFAULT=${VAL_AP_MODE_DEFAULT}
    WIFI_POWER_SAVE_MODE=${VAL_WIFI_POWER_SAVE_MODE}
    SKIP_DISPLAY_VERSION=${VAL_SKIP_DISPLAY_VERSION}
    PREFER_IPV6=${VAL_PREFER_IPV6}
)

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

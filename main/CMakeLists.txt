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

# Conditionally include touch_control.c only for Tidbyt Gen2
set(SRCS "main.c"
         "display.cpp"
         "flash.c"
         "gfx.c"
         "ota.c"
         "remote.c"
         "wifi.c"
         "ap.c"
         "nvs_settings.c"
         "dns_wrapper.c"
         "syslog.c"
         "sntp.c")

if(CONFIG_BOARD_TIDBYT_GEN2)
    list(APPEND SRCS "touch_control.c" "beep.c")
endif()

if(CONFIG_ENABLE_WIREGUARD)
    list(APPEND SRCS "wg.c")
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS ".")

if(CONFIG_ENABLE_WIREGUARD)
    # Optional build-time default: wireguard.conf in the project root is
    # embedded as is (an empty file if there is none).
    set(_wg_src "${CMAKE_SOURCE_DIR}/wireguard.conf")
    set(_wg_gen "${CMAKE_CURRENT_BINARY_DIR}/gen/wg_default.conf")
    set(_wg_text "")
    if(EXISTS "${_wg_src}")
        file(SIZE "${_wg_src}" _wg_size)
        if(_wg_size GREATER 1023)
            message(FATAL_ERROR "wireguard.conf is larger than 1023 bytes")
        endif()
        file(READ "${_wg_src}" _wg_text)
        message(STATUS "Embedding wireguard.conf as default WireGuard configuration")
    endif()
    set(_wg_old "\n")
    if(EXISTS "${_wg_gen}")
        file(READ "${_wg_gen}" _wg_old)
    endif()
    if(NOT "${_wg_old}" STREQUAL "${_wg_text}")
        file(WRITE "${_wg_gen}" "${_wg_text}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wg_src}")
    target_add_binary_data(${COMPONENT_LIB} "${_wg_gen}" TEXT)

    # The WireGuard library has no Kconfig; its limits are plain defines.
    # Besides the device's own address the peer needs room for the
    # AllowedIPs networks.
    idf_build_set_property(COMPILE_DEFINITIONS "-DCONFIG_WIREGUARD_MAX_SRC_IPS=4" APPEND)
endif()

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_getaddrinfo" APPEND)

# Emit secrets as a generated header instead of compile definitions: CMake
# silently drops any -D value containing '#' (e.g. in a WiFi password).
foreach(_secret VAL_WIFI_SSID VAL_WIFI_PASSWORD VAL_REMOTE_URL)
    string(REPLACE "\\" "\\\\" ${_secret} "${${_secret}}")
    string(REPLACE "\"" "\\\"" ${_secret} "${${_secret}}")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen/secrets_gen.h"
"// Auto-generated from secrets.cmake - DO NOT EDIT
#pragma once
#define WIFI_SSID \"${VAL_WIFI_SSID}\"
#define WIFI_PASSWORD \"${VAL_WIFI_PASSWORD}\"
#define REMOTE_URL \"${VAL_REMOTE_URL}\"
")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

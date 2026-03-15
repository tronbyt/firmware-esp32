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

# Pendulum settings
if(NOT DEFINED VAL_PENDULUM_SPEED)
    set(VAL_PENDULUM_SPEED "0.01")
endif()
if(NOT DEFINED VAL_PENDULUM_ARM1_LENGTH)
    set(VAL_PENDULUM_ARM1_LENGTH "12.0")
endif()
if(NOT DEFINED VAL_PENDULUM_ARM2_LENGTH)
    set(VAL_PENDULUM_ARM2_LENGTH "10.0")
endif()
if(NOT DEFINED VAL_PENDULUM_MASS1)
    set(VAL_PENDULUM_MASS1 "1.0")
endif()
if(NOT DEFINED VAL_PENDULUM_MASS2)
    set(VAL_PENDULUM_MASS2 "1.0")
endif()

# Conditionally include touch_control.c only for Tidbyt Gen2
set(SRCS "double_pendulum.cpp" "main.c"
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
    list(APPEND SRCS "touch_control.c")
endif()

idf_component_register(SRCS ${SRCS}
                        INCLUDE_DIRS "."
                        "${CMAKE_SOURCE_DIR}/managed_components/esp32-hub75-matrixpanel-dma/src")

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_getaddrinfo" APPEND)

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    WIFI_SSID="${VAL_WIFI_SSID}"
    WIFI_PASSWORD="${VAL_WIFI_PASSWORD}"
    REMOTE_URL="${VAL_REMOTE_URL}"
    PENDULUM_SPEED=${VAL_PENDULUM_SPEED}
    PENDULUM_ARM1_LENGTH=${VAL_PENDULUM_ARM1_LENGTH}
    PENDULUM_ARM2_LENGTH=${VAL_PENDULUM_ARM2_LENGTH}
    PENDULUM_MASS1=${VAL_PENDULUM_MASS1}
    PENDULUM_MASS2=${VAL_PENDULUM_MASS2}
)

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()
if(NOT DEFINED VAL_WIFI_PASSWORD)
    set(VAL_WIFI_PASSWORD "XplaceholderWIFIPASSWORD________________________________________")
endif()
if(NOT DEFINED VAL_REMOTE_URL)
    set(VAL_REMOTE_URL "XplaceholderREMOTEURL___________________________________________________________________________________________________________")
endif()

# Pendulum settings
if(NOT DEFINED VAL_PENDULUM_SPEED)
    set(VAL_PENDULUM_SPEED "0.01")
endif()
if(NOT DEFINED VAL_PENDULUM_ARM1_LENGTH)
    set(VAL_PENDULUM_ARM1_LENGTH "12.0")
endif()
if(NOT DEFINED VAL_PENDULUM_ARM2_LENGTH)
    set(VAL_PENDULUM_ARM2_LENGTH "10.0")
endif()
if(NOT DEFINED VAL_PENDULUM_MASS1)
    set(VAL_PENDULUM_MASS1 "1.0")
endif()
if(NOT DEFINED VAL_PENDULUM_MASS2)
    set(VAL_PENDULUM_MASS2 "1.0")
endif()

# Conditionally include touch_control.c only for Tidbyt Gen2
set(SRCS "double_pendulum.cpp" "main.c"
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
    list(APPEND SRCS "touch_control.c")
endif()

idf_component_register(SRCS ${SRCS}
                        INCLUDE_DIRS "."
                        "${CMAKE_SOURCE_DIR}/managed_components/esp32-hub75-matrixpanel-dma/src")

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_getaddrinfo" APPEND)

target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    WIFI_SSID="${VAL_WIFI_SSID}"
    WIFI_PASSWORD="${VAL_WIFI_PASSWORD}"
    REMOTE_URL="${VAL_REMOTE_URL}"
    PENDULUM_SPEED="${VAL_PENDULUM_SPEED}"
    PENDULUM_ARM1_LENGTH="${VAL_PENDULUM_ARM1_LENGTH}"
    PENDULUM_ARM2_LENGTH="${VAL_PENDULUM_ARM2_LENGTH}"
    PENDULUM_MASS1="${VAL_PENDULUM_MASS1}"
    PENDULUM_MASS2="${VAL_PENDULUM_MASS2}"
)

if(CONFIG_NO_FAST_FUNCTIONS)
    idf_build_set_property(COMPILE_DEFINITIONS "-DNO_FAST_FUNCTIONS" APPEND)
endif()

cmake_minimum_required(VERSION 3.16.0)

# Generate secrets.cmake from secrets.json if it exists
execute_process(
    COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/generate_secrets_cmake.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)

# Apply or revert the ESP32-HUB75-MatrixPanel-DMA I2S divider patch based on
# CONFIG_PATCH_I2S_DIVIDER, so switching between patched/unpatched configs
# without a full clean still leaves managed_components/ in the right state.
if(CONFIG_PATCH_I2S_DIVIDER)
    set(_patch_i2s_mode "--apply")
else()
    set(_patch_i2s_mode "--revert")
endif()
execute_process(
    COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_i2s_divider.py ${_patch_i2s_mode}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE _patch_i2s_result
)
if(NOT _patch_i2s_result EQUAL 0)
    message(WARNING "patch_i2s_divider.py ${_patch_i2s_mode} exited non-zero (${_patch_i2s_result}); see output above")
endif()

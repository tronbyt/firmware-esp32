cmake_minimum_required(VERSION 3.16.0)

# PYTHON is set by project.cmake, but secrets run before that include. Resolve interpreter here.
if(DEFINED ENV{PYTHON} AND NOT "$ENV{PYTHON}" STREQUAL "")
    set(_idf_python "$ENV{PYTHON}")
elseif(DEFINED ENV{IDF_PYTHON_ENV_PATH} AND NOT "$ENV{IDF_PYTHON_ENV_PATH}" STREQUAL "")
    if(CMAKE_HOST_WIN32)
        set(_idf_python "$ENV{IDF_PYTHON_ENV_PATH}/Scripts/python.exe")
    else()
        set(_idf_python "$ENV{IDF_PYTHON_ENV_PATH}/bin/python")
    endif()
else()
    set(_idf_python "python3")
endif()

# Generate secrets.cmake from secrets.json if it exists
execute_process(
    COMMAND ${_idf_python} ${CMAKE_CURRENT_SOURCE_DIR}/generate_secrets_cmake.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE _generate_secrets_result
)
if(NOT _generate_secrets_result EQUAL 0)
    message(FATAL_ERROR "generate_secrets_cmake.py failed (exit ${_generate_secrets_result}); see output above")
endif()

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
    COMMAND ${_idf_python} ${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_i2s_divider.py ${_patch_i2s_mode}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE _patch_i2s_result
)
if(NOT _patch_i2s_result EQUAL 0)
    message(WARNING "patch_i2s_divider.py ${_patch_i2s_mode} exited non-zero (${_patch_i2s_result}); see output above")
endif()

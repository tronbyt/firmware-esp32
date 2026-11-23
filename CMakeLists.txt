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

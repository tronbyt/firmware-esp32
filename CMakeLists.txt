cmake_minimum_required(VERSION 3.16.0)

# Check and initialize submodules if needed
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libwebp/src/webp/decode.h")
    message(STATUS "Submodules not found. Attempting to initialize...")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT
        )
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    else()
        message(FATAL_ERROR "git not found, please install git and checkout submodules")
    endif()
endif()

# Generate secrets.cmake from secrets.json if it exists
execute_process(
    COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/generate_secrets_cmake.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)

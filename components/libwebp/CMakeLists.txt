# Wrapper component for libwebp submodule in external/libwebp

file(GLOB_RECURSE SOURCES "../../external/libwebp/src/*.c")

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "../../external/libwebp" "../../external/libwebp/src"
                       )

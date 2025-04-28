#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int gfx_initialize(const void* webp, size_t len);
void gfx_update(const void* webp, size_t len);
void gfx_shutdown();

#ifdef __cplusplus
}
#endif

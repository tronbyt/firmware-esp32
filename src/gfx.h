#pragma once

#include <stddef.h>



int gfx_initialize(const void* webp, size_t len);
void gfx_update(const void* webp, size_t len, int32_t dwell_secs);
void gfx_shutdown();

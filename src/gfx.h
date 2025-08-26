#pragma once

#include <stddef.h>



int gfx_initialize(const void* webp, size_t len);
int gfx_update(void* webp, size_t len, int32_t dwell_secs);
void gfx_shutdown();

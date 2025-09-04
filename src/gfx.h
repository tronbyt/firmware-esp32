#pragma once

#include <stddef.h>



int gfx_initialize();
int gfx_update(void* webp, size_t len, int32_t dwell_secs);
int gfx_display_asset(const char* asset_type);
void gfx_shutdown();

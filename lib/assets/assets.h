#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint8_t ASSET_BOOT_WEBP[];
extern const size_t ASSET_BOOT_WEBP_LEN;

#ifdef BOOT_WEBP_WINDYTRON
#include "lib/assets/windytron_c"
#else
#include "lib/assets/tronbyt_c"
#endif

extern const uint8_t ASSET_CONFIG_WEBP[];
extern const size_t ASSET_CONFIG_WEBP_LEN;

#include "lib/assets/config_c"


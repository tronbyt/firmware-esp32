#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint8_t ASSET_BOOT_WEBP[];
extern const size_t ASSET_BOOT_WEBP_LEN;

#ifdef BOOT_WEBP_PARROT
#include "lib/assets/parrot_c"
#else
#include "lib/assets/tronbyt_c"
#endif

extern const uint8_t ASSET_CONFIG_WEBP[];
extern const size_t ASSET_CONFIG_WEBP_LEN;

#include "lib/assets/config_c"

extern const uint8_t ASSET_404_WEBP[];
extern const size_t ASSET_404_WEBP_LEN;

#include "lib/assets/404_c"


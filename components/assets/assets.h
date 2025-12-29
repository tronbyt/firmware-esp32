#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint8_t ASSET_BOOT_WEBP[];
extern const size_t ASSET_BOOT_WEBP_LEN;

#if CONFIG_BOOT_WEBP_WINDYTRON
#include "windytron_c"
#elif CONFIG_BOOT_WEBP_PARROT
#include "parrot_c"
#else
#include "tronbyt_c"
#endif

extern const uint8_t ASSET_CONFIG_WEBP[];
extern const size_t ASSET_CONFIG_WEBP_LEN;

#include "config_c"

extern const uint8_t ASSET_404_WEBP[];
extern const size_t ASSET_404_WEBP_LEN;

#include "404_c"

extern const uint8_t ASSET_OVERSIZE_WEBP[];
extern const size_t ASSET_OVERSIZE_WEBP_LEN;

#include "oversize_c"

extern const uint8_t ASSET_NOCONNECT_WEBP[];
extern const size_t ASSET_NOCONNECT_WEBP_LEN;

#include "no_connect_c"

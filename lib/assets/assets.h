#pragma once

#include <stddef.h>
#include <stdint.h>

extern const uint8_t ASSET_BOOT_WEBP[];
extern const size_t ASSET_BOOT_WEBP_LEN;

#ifdef BOOT_WEBP_PARROT
#include "lib/assets/parrot_c"
#elif defined(BOOT_WEBP_SINGLE)
#include "lib/assets/single"
#else
#include "lib/assets/tronbyt_c"
#endif
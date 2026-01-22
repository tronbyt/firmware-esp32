#pragma once

#include <stdint.h>

// Retrieves url via HTTP GET. Caller is responsible for freeing buf
// and ota_url (if not NULL) on success.
int remote_get(const char* url, uint8_t** buf, size_t* len,
               uint8_t* brightness_pct, int32_t* dwell_secs, int* return_code,
               char** ota_url);
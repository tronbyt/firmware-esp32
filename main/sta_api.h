#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start a lightweight HTTP server on port 80 for local status queries. */
esp_err_t sta_api_start(void);

/** Stop the local status API server. */
esp_err_t sta_api_stop(void);

#ifdef __cplusplus
}
#endif

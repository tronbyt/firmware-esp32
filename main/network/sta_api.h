#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <stdbool.h>

/** Start a lightweight HTTP server on port 80 for local status queries. */
esp_err_t sta_api_start(void);

/** Stop the local status API server. */
esp_err_t sta_api_stop(void);

/** Return true if the STA API is using the given server handle. */
bool sta_api_owns_server(httpd_handle_t server);

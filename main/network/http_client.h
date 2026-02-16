#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Blocking HTTP polling loop — never returns.
 *
 * Fetches WebP images from @p url, queues them for display,
 * handles OTA headers, error codes, and wifi health checks.
 */
void http_client_run_loop(const char* url);

#ifdef __cplusplus
}
#endif

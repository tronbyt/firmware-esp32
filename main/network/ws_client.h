#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the WebSocket client.
 *
 * @param url  WebSocket URL (ws:// or wss://)
 * @return ESP_OK on success
 */
esp_err_t ws_client_start(const char* url);

/**
 * @brief Stop and destroy the WebSocket client.
 */
void ws_client_stop(void);

/**
 * @brief Blocking reconnect loop — never returns.
 *
 * Monitors the connection, sends client_info on connect,
 * and re-establishes the link when it drops.
 */
void ws_client_run_loop(void);

#ifdef __cplusplus
}
#endif

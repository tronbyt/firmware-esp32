#pragma once

#include <esp_err.h>
#include <esp_websocket_client.h>

/// Initialize the messages module with the active WS client handle.
void msg_init(esp_websocket_client_handle_t client);

/// Send device/client info JSON to the server.
esp_err_t msg_send_client_info();

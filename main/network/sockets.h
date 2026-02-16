#pragma once

#include <esp_websocket_client.h>

/// Initialize the sockets module (event-driven WebSocket client).
/// Sets up timers, registers WiFi/IP event handlers, and begins
/// connecting once the network is available.
void sockets_init(const char* url);

/// Deinitialize: stop client, delete timers, free resources.
void sockets_deinit();

/// True when the WebSocket connection is established.
bool sockets_is_connected();

/// Get the underlying WS client handle (for gfx module).
esp_websocket_client_handle_t sockets_get_client();

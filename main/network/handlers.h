#pragma once

#include <esp_websocket_client.h>

/// Handle an inbound text (JSON) message from the server.
void handle_text_message(esp_websocket_event_data_t* data);

/// Handle inbound binary (WebP) message chunks, with reassembly.
void handle_binary_message(esp_websocket_event_data_t* data);

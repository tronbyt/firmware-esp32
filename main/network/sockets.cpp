// Sockets — Event-driven WebSocket client with FSM and timer-based reconnect.
// Modeled on matrx-fw's sockets module, adapted for our JSON protocol.

#include "sockets.h"
#include "handlers.h"
#include "messages.h"

#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "display.h"
#include "scheduler.h"
#include "webp_player.h"
#include "wifi.h"

namespace {

const char* TAG = "sockets";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr int64_t RECONNECT_DELAY_US = 5000 * 1000;  // 5 seconds
constexpr int64_t HEALTH_CHECK_INTERVAL_US = 30000 * 1000;  // 30 seconds

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

enum class State : uint8_t {
  Disconnected,  // No connection attempt (waiting for network or URL)
  Ready,         // Network up, ready to connect
  Connected,     // WebSocket connected
};

struct SocketContext {
  esp_websocket_client_handle_t client = nullptr;
  State state = State::Disconnected;
  char* url = nullptr;
  bool sent_client_info = false;
};

SocketContext ctx;

// Timers
esp_timer_handle_t reconnect_timer = nullptr;
esp_timer_handle_t health_timer = nullptr;

// Forward declarations
esp_err_t start_client();
void schedule_reconnect();

// ---------------------------------------------------------------------------
// Health check timer — replaces the old blocking wifi_health_check loop
// ---------------------------------------------------------------------------

void health_timer_callback(void*) {
  wifi_health_check();
}

// ---------------------------------------------------------------------------
// Reconnection (timer-based, no blocking loop)
// ---------------------------------------------------------------------------

void reconnect_timer_callback(void*) {
  ESP_LOGI(TAG, "Reconnect timer fired");

  if (ctx.client) {
    esp_websocket_client_stop(ctx.client);
    esp_websocket_client_destroy(ctx.client);
    ctx.client = nullptr;
  }

  if (ctx.state == State::Ready || ctx.state == State::Disconnected) {
    // Check if network is still up
    if (wifi_is_connected()) {
      ctx.state = State::Ready;
      start_client();
    } else {
      ctx.state = State::Disconnected;
      ESP_LOGW(TAG, "Network not available, will retry when IP acquired");
    }
  }
}

void schedule_reconnect() {
  if (reconnect_timer) {
    esp_timer_stop(reconnect_timer);  // Stop any pending reconnect
    esp_timer_start_once(reconnect_timer, RECONNECT_DELAY_US);
    ESP_LOGI(TAG, "Scheduled reconnect in %lld ms",
             RECONNECT_DELAY_US / 1000);
  }
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------

void ws_event_handler(void*, esp_event_base_t, int32_t event_id,
                      void* event_data) {
  auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected");
      ctx.state = State::Connected;
      ctx.sent_client_info = false;
      msg_send_client_info();
      ctx.sent_client_info = true;
      scheduler_on_ws_connect();
      break;

    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected");
      draw_error_indicator_pixel();
      if (ctx.state == State::Connected) {
        ctx.state = State::Ready;
        scheduler_on_ws_disconnect();
        schedule_reconnect();
      }
      break;

    case WEBSOCKET_EVENT_DATA:
      if (data->op_code == 1 && data->data_len > 0) {
        handle_text_message(data);
      } else if (data->op_code == 2 || data->op_code == 0) {
        handle_binary_message(data);
      }
      break;

    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGE(TAG, "WebSocket error");
      draw_error_indicator_pixel();
      if (ctx.state == State::Connected) {
        ctx.state = State::Ready;
        scheduler_on_ws_disconnect();
        schedule_reconnect();
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Client lifecycle
// ---------------------------------------------------------------------------

esp_err_t start_client() {
  if (ctx.client) {
    ESP_LOGW(TAG, "Client already exists, destroying first");
    esp_websocket_client_destroy(ctx.client);
    ctx.client = nullptr;
  }

  if (!ctx.url) {
    ESP_LOGE(TAG, "No URL configured");
    return ESP_ERR_INVALID_STATE;
  }

  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = ctx.url;
  ws_cfg.task_stack = 8192;
  ws_cfg.buffer_size = 8192;
  ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
  ws_cfg.reconnect_timeout_ms = 10000;
  ws_cfg.network_timeout_ms = 10000;

  ctx.client = esp_websocket_client_init(&ws_cfg);
  if (!ctx.client) {
    ESP_LOGE(TAG, "Failed to init WS client");
    return ESP_FAIL;
  }

  esp_websocket_register_events(ctx.client, WEBSOCKET_EVENT_ANY,
                                ws_event_handler, nullptr);

  gfx_set_websocket_handle(ctx.client);
  msg_init(ctx.client);

  esp_err_t err = esp_websocket_client_start(ctx.client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WS client: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(ctx.client);
    ctx.client = nullptr;
    schedule_reconnect();
    return err;
  }

  ESP_LOGI(TAG, "Client started, connecting to %s", ctx.url);
  return ESP_OK;
}

// ---------------------------------------------------------------------------
// WiFi/IP event handlers (event-driven network awareness)
// ---------------------------------------------------------------------------

void wifi_event_handler(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (ctx.state != State::Disconnected) {
      ESP_LOGW(TAG, "WiFi disconnected");
      ctx.state = State::Disconnected;
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP, state=%d", static_cast<int>(ctx.state));
    if (ctx.state == State::Disconnected) {
      ctx.state = State::Ready;
      start_client();
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void sockets_init(const char* url) {
  ctx.url = strdup(url);

  // Create reconnect timer
  esp_timer_create_args_t reconnect_args = {};
  reconnect_args.callback = reconnect_timer_callback;
  reconnect_args.name = "sock_reconn";
  reconnect_args.skip_unhandled_events = true;
  esp_timer_create(&reconnect_args, &reconnect_timer);

  // Create health check timer (replaces blocking wifi_health_check loop)
  esp_timer_create_args_t health_args = {};
  health_args.callback = health_timer_callback;
  health_args.name = "sock_health";
  health_args.skip_unhandled_events = true;
  esp_timer_create(&health_args, &health_timer);
  esp_timer_start_periodic(health_timer, HEALTH_CHECK_INTERVAL_US);

  // Register WiFi/IP event handlers for event-driven reconnection
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                             wifi_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                             wifi_event_handler, nullptr);

  // If already connected, start immediately
  if (wifi_is_connected()) {
    ctx.state = State::Ready;
    start_client();
  } else {
    ctx.state = State::Disconnected;
    ESP_LOGI(TAG, "Waiting for network...");
  }
}

void sockets_deinit() {
  // Stop timers
  if (reconnect_timer) {
    esp_timer_stop(reconnect_timer);
    esp_timer_delete(reconnect_timer);
    reconnect_timer = nullptr;
  }
  if (health_timer) {
    esp_timer_stop(health_timer);
    esp_timer_delete(health_timer);
    health_timer = nullptr;
  }

  // Unregister event handlers
  esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                               wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               wifi_event_handler);

  // Destroy client
  if (ctx.client) {
    esp_websocket_client_stop(ctx.client);
    esp_websocket_client_destroy(ctx.client);
    ctx.client = nullptr;
  }

  // Free URL
  if (ctx.url) {
    free(ctx.url);
    ctx.url = nullptr;
  }

  ctx.state = State::Disconnected;
}

bool sockets_is_connected() {
  return ctx.client && esp_websocket_client_is_connected(ctx.client);
}

esp_websocket_client_handle_t sockets_get_client() {
  return ctx.client;
}

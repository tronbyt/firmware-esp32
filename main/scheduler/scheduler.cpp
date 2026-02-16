// Scheduler — FSM-based playback orchestrator.
// Modeled on matrx-fw's scheduler, adapted for our push (WS) + poll (HTTP)
// model.
//
// WS mode:  passive — reacts to content pushed by the server.
// HTTP mode: active — prefetches next image before dwell expires.

#include "scheduler.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "display.h"
#include "ota.h"
#include "remote.h"
#include "sdkconfig.h"
#include "webp_player.h"
#include "wifi.h"

static const char* TAG = "scheduler";

namespace {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr int64_t PREFETCH_BEFORE_US = 2 * 1000 * 1000;  // 2 s before dwell
constexpr int64_t RETRY_DELAY_US = 5 * 1000 * 1000;      // 5 s on error

// ---------------------------------------------------------------------------
// Mode & State
// ---------------------------------------------------------------------------

enum class Mode : uint8_t { NONE, WEBSOCKET, HTTP };

enum class State : uint8_t {
  IDLE,              // Nothing playing, waiting for content
  PLAYING,           // Content is being displayed
  HTTP_FETCHING,     // HTTP mode: fetch in progress (no content yet)
  HTTP_PREFETCHING,  // HTTP mode: playing + background fetch running
};

const char* state_name(State s) {
  switch (s) {
    case State::IDLE:
      return "IDLE";
    case State::PLAYING:
      return "PLAYING";
    case State::HTTP_FETCHING:
      return "HTTP_FETCHING";
    case State::HTTP_PREFETCHING:
      return "HTTP_PREFETCHING";
  }
  return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// HTTP prefetch result
// ---------------------------------------------------------------------------

struct PrefetchResult {
  uint8_t* webp = nullptr;
  size_t len = 0;
  uint8_t brightness_pct = 0;
  int32_t dwell_secs = 0;
  int status_code = 0;
  char* ota_url = nullptr;
  bool failed = false;
  std::atomic<bool> ready{false};

  void clear() {
    if (webp) {
      free(webp);
      webp = nullptr;
    }
    if (ota_url) {
      free(ota_url);
      ota_url = nullptr;
    }
    len = 0;
    brightness_pct = 0;
    dwell_secs = 0;
    status_code = 0;
    failed = false;
    ready.store(false);
  }
};

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

struct Context {
  Mode mode = Mode::NONE;
  State state = State::IDLE;
  bool ws_connected = false;
  char* http_url = nullptr;

  // Timers
  esp_timer_handle_t prefetch_timer = nullptr;
  esp_timer_handle_t retry_timer = nullptr;

  // HTTP prefetch
  PrefetchResult prefetch;
  TaskHandle_t fetch_task = nullptr;

  // Default brightness
  uint8_t brightness_pct = (CONFIG_HUB75_BRIGHTNESS * 100) / 255;
};

Context ctx;

// Forward declarations
void transition_to(State new_state);
void http_trigger_fetch();
void http_apply_prefetch();
void ota_task_entry(void* param);

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void transition_to(State new_state) {
  if (ctx.state != new_state) {
    ESP_LOGI(TAG, "State: %s -> %s", state_name(ctx.state),
             state_name(new_state));
    ctx.state = new_state;
  }
}

// ---------------------------------------------------------------------------
// Timer management
// ---------------------------------------------------------------------------

void stop_timers() {
  if (ctx.prefetch_timer) esp_timer_stop(ctx.prefetch_timer);
  if (ctx.retry_timer) esp_timer_stop(ctx.retry_timer);
}

void start_prefetch_timer(int32_t dwell_secs) {
  if (!ctx.prefetch_timer || dwell_secs <= 2) return;

  esp_timer_stop(ctx.prefetch_timer);
  int64_t delay_us =
      static_cast<int64_t>(dwell_secs - 2) * 1000 * 1000;
  esp_err_t err = esp_timer_start_once(ctx.prefetch_timer, delay_us);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start prefetch timer: %s",
             esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "Prefetch timer: %lld ms", delay_us / 1000);
  }
}

void start_retry_timer() {
  if (!ctx.retry_timer) return;
  esp_timer_stop(ctx.retry_timer);
  esp_timer_start_once(ctx.retry_timer, RETRY_DELAY_US);
}

// ---------------------------------------------------------------------------
// OTA helper
// ---------------------------------------------------------------------------

void ota_task_entry(void* param) {
  auto* url = static_cast<char*>(param);
  run_ota(url);
  free(url);
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// HTTP fetch task
// ---------------------------------------------------------------------------

void http_fetch_task(void* param) {
  (void)param;

  ctx.prefetch.clear();

  uint8_t* webp = nullptr;
  size_t len = 0;
  uint8_t brightness_pct = ctx.brightness_pct;
  int32_t dwell_secs = 0;
  int status_code = 0;
  char* ota_url = nullptr;

  ESP_LOGI(TAG, "HTTP fetch: %s", ctx.http_url);

  bool ok = wifi_is_connected() &&
            !remote_get(ctx.http_url, &webp, &len, &brightness_pct,
                        &dwell_secs, &status_code, &ota_url);

  ctx.prefetch.webp = webp;
  ctx.prefetch.len = len;
  ctx.prefetch.brightness_pct = brightness_pct;
  ctx.prefetch.dwell_secs = dwell_secs;
  ctx.prefetch.status_code = status_code;
  ctx.prefetch.ota_url = ota_url;
  ctx.prefetch.failed = !ok;
  ctx.prefetch.ready.store(true);

  // If player is idle (dwell expired while we were fetching), apply now
  if (ctx.state == State::HTTP_FETCHING ||
      ctx.state == State::HTTP_PREFETCHING) {
    http_apply_prefetch();
  }

  ctx.fetch_task = nullptr;
  vTaskDelete(nullptr);
}

void http_trigger_fetch() {
  if (ctx.fetch_task) {
    ESP_LOGW(TAG, "Fetch already in progress");
    return;
  }

  xTaskCreatePinnedToCore(http_fetch_task, "http_fetch", 8192, nullptr, 3,
                          &ctx.fetch_task, 0);
}

void http_apply_prefetch() {
  if (!ctx.prefetch.ready.load()) return;

  // Handle OTA
  if (ctx.prefetch.ota_url) {
    char* ota_url = ctx.prefetch.ota_url;
    ctx.prefetch.ota_url = nullptr;
    ESP_LOGI(TAG, "OTA URL received via HTTP: %s", ota_url);
    xTaskCreate(ota_task_entry, "ota_task", 8192, ota_url, 5, nullptr);
  }

  if (ctx.prefetch.failed) {
    ESP_LOGE(TAG, "HTTP fetch failed (status %d)", ctx.prefetch.status_code);
    draw_error_indicator_pixel();

    int sc = ctx.prefetch.status_code;
    if (sc == 404 || sc == 400) {
      gfx_play_embedded("error_404", false);
    } else if (sc == 413) {
      gfx_play_embedded("oversize", false);
    }

    ctx.prefetch.clear();
    start_retry_timer();
    transition_to(State::IDLE);
    return;
  }

  // Apply brightness and queue image
  display_set_brightness(ctx.prefetch.brightness_pct);
  ctx.brightness_pct = ctx.prefetch.brightness_pct;

  int32_t dwell = ctx.prefetch.dwell_secs;
  int counter = gfx_update(ctx.prefetch.webp, ctx.prefetch.len, dwell);
  if (counter < 0) {
    ESP_LOGE(TAG, "Failed to queue HTTP-fetched WebP");
    free(ctx.prefetch.webp);
    ctx.prefetch.webp = nullptr;
    ctx.prefetch.clear();
    start_retry_timer();
    transition_to(State::IDLE);
    return;
  }
  // Ownership transferred
  ctx.prefetch.webp = nullptr;
  ctx.prefetch.clear();

  transition_to(State::PLAYING);

  // Start prefetch timer for next image
  if (dwell > 0) {
    start_prefetch_timer(dwell);
  }
}

// ---------------------------------------------------------------------------
// Timer callbacks
// ---------------------------------------------------------------------------

void prefetch_timer_callback(void*) {
  ESP_LOGD(TAG, "Prefetch timer fired");

  if (ctx.mode == Mode::HTTP && ctx.state == State::PLAYING) {
    transition_to(State::HTTP_PREFETCHING);
    http_trigger_fetch();
  }
}

void retry_timer_callback(void*) {
  ESP_LOGI(TAG, "Retry timer fired");

  if (ctx.mode == Mode::HTTP) {
    transition_to(State::HTTP_FETCHING);
    http_trigger_fetch();
  }
}

// ---------------------------------------------------------------------------
// GFX Player event handler
// ---------------------------------------------------------------------------

void on_player_playing(const gfx_playing_evt_t* evt) {
  if (!evt) return;

  ESP_LOGI(TAG, "Player: PLAYING (source=%d, frames=%" PRIu32
                ", duration=%" PRIu32 "ms)",
           evt->source_type, evt->frame_count, evt->duration_ms);

  if (evt->source_type == GFX_SOURCE_RAM) {
    transition_to(State::PLAYING);
  }
}

void on_player_stopped() {
  ESP_LOGD(TAG, "Player: STOPPED");

  switch (ctx.mode) {
    case Mode::WEBSOCKET:
      // WS mode: server pushes next content, just go idle
      transition_to(State::IDLE);
      break;

    case Mode::HTTP:
      // Check if prefetch is ready
      if (ctx.prefetch.ready.load()) {
        http_apply_prefetch();
      } else if (ctx.state == State::HTTP_PREFETCHING) {
        // Still fetching — stay in fetching state, will apply when done
        transition_to(State::HTTP_FETCHING);
      } else {
        // No prefetch was started (short dwell?). Fetch now.
        transition_to(State::HTTP_FETCHING);
        http_trigger_fetch();
      }
      break;

    default:
      transition_to(State::IDLE);
      break;
  }
}

void on_player_error(const gfx_error_evt_t* evt) {
  ESP_LOGW(TAG, "Player: ERROR (code=%d)", evt ? evt->error_code : -1);

  switch (ctx.mode) {
    case Mode::WEBSOCKET:
      draw_error_indicator_pixel();
      transition_to(State::IDLE);
      break;

    case Mode::HTTP:
      draw_error_indicator_pixel();
      start_retry_timer();
      transition_to(State::IDLE);
      break;

    default:
      transition_to(State::IDLE);
      break;
  }
}

void player_event_handler(void*, esp_event_base_t, int32_t event_id,
                          void* event_data) {
  switch (event_id) {
    case GFX_PLAYER_EVT_PLAYING:
      on_player_playing(
          static_cast<const gfx_playing_evt_t*>(event_data));
      break;
    case GFX_PLAYER_EVT_STOPPED:
      on_player_stopped();
      break;
    case GFX_PLAYER_EVT_ERROR:
      on_player_error(
          static_cast<const gfx_error_evt_t*>(event_data));
      break;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void scheduler_init() {
  // Create prefetch timer (HTTP mode)
  esp_timer_create_args_t prefetch_args = {};
  prefetch_args.callback = prefetch_timer_callback;
  prefetch_args.name = "sched_pref";
  prefetch_args.skip_unhandled_events = true;
  esp_timer_create(&prefetch_args, &ctx.prefetch_timer);

  // Create retry timer
  esp_timer_create_args_t retry_args = {};
  retry_args.callback = retry_timer_callback;
  retry_args.name = "sched_retry";
  retry_args.skip_unhandled_events = true;
  esp_timer_create(&retry_args, &ctx.retry_timer);

  // Register for player events
  esp_event_handler_register(GFX_PLAYER_EVENTS, ESP_EVENT_ANY_ID,
                             player_event_handler, nullptr);

  ESP_LOGI(TAG, "Scheduler initialized");
}

void scheduler_start_ws() {
  ctx.mode = Mode::WEBSOCKET;
  transition_to(State::IDLE);
  ESP_LOGI(TAG, "Started in WebSocket mode");
}

void scheduler_start_http(const char* url) {
  ctx.mode = Mode::HTTP;
  ctx.http_url = strdup(url);

  // Trigger initial fetch
  transition_to(State::HTTP_FETCHING);
  http_trigger_fetch();

  ESP_LOGI(TAG, "Started in HTTP mode: %s", url);
}

void scheduler_stop() {
  stop_timers();

  ctx.prefetch.clear();
  ctx.mode = Mode::NONE;
  transition_to(State::IDLE);

  if (ctx.http_url) {
    free(ctx.http_url);
    ctx.http_url = nullptr;
  }

  ESP_LOGI(TAG, "Scheduler stopped");
}

void scheduler_on_ws_connect() {
  ctx.ws_connected = true;
  transition_to(State::IDLE);
  ESP_LOGI(TAG, "WS connected — awaiting content");
}

void scheduler_on_ws_disconnect() {
  ctx.ws_connected = false;
  stop_timers();
  gfx_play_embedded("no_connect", true);
  transition_to(State::IDLE);
  ESP_LOGI(TAG, "WS disconnected — showing no_connect sprite");
}

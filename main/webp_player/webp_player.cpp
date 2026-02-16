// WebP Player - Event-driven animated WebP playback task
// Modeled after matrx-fw/main/webp_player/
// State machine: IDLE <-> PLAYING
#include "webp_player.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <http_parser.h>
#include <webp/demux.h>

#include "assets.h"
#include "display.h"
#include "nvs_settings.h"
#include "raii_utils.hpp"
#include "version.h"

static const char* TAG = "webp_player";

ESP_EVENT_DEFINE_BASE(GFX_PLAYER_EVENTS);

namespace {

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

constexpr uint32_t TASK_STACK_SIZE = 4096;
constexpr int TASK_PRIORITY = 2;
constexpr int TASK_CORE = 1;
constexpr int DECODE_RETRY_COUNT = 3;
constexpr int DECODE_RETRY_DELAY_MS = 200;

constexpr EventBits_t BIT_IDLE = BIT0;

//------------------------------------------------------------------------------
// Player State
//------------------------------------------------------------------------------

enum class State : uint8_t { IDLE, PLAYING };

//------------------------------------------------------------------------------
// Pending Command (written by API, read by task)
//------------------------------------------------------------------------------

struct PendingCmd {
  std::atomic<bool> valid{false};
  void* buf = nullptr;
  size_t len = 0;
  int32_t dwell_secs = 0;
  int counter = 0;
};

//------------------------------------------------------------------------------
// Player Context
//------------------------------------------------------------------------------

struct PlayerContext {
  TaskHandle_t task = nullptr;
  SemaphoreHandle_t mutex = nullptr;
  EventGroupHandle_t event_group = nullptr;
  esp_websocket_client_handle_t ws_handle = nullptr;

  std::atomic<State> state{State::IDLE};
  std::atomic<bool> paused{false};
  PendingCmd pending;
  int counter = 0;
  int loaded_counter = 0;

  // Current playback data (task-local)
  void* webp_buf = nullptr;
  size_t webp_len = 0;
  int32_t dwell_secs = 0;
  int active_counter = -1;

  // Decoder
  WebPAnimDecoder* decoder = nullptr;
  WebPData webp_data = {nullptr, 0};
  WebPAnimInfo anim_info = {};

  // Timing
  TickType_t next_frame_tick = 0;
  int64_t playback_start_us = 0;
  int last_timestamp = 0;

  // Error tracking
  int decode_error_count = 0;
  bool initialized = false;
};

PlayerContext ctx;

//------------------------------------------------------------------------------
// Static Asset Detection
//------------------------------------------------------------------------------

bool is_static_asset(const void* ptr) {
  return ptr == ASSET_BOOT_WEBP || ptr == ASSET_CONFIG_WEBP ||
         ptr == ASSET_404_WEBP || ptr == ASSET_OVERSIZE_WEBP ||
         ptr == ASSET_NOCONNECT_WEBP;
}

//------------------------------------------------------------------------------
// Decoder Management
//------------------------------------------------------------------------------

void destroy_decoder() {
  if (ctx.decoder) {
    WebPAnimDecoderDelete(ctx.decoder);
    ctx.decoder = nullptr;
  }
  ctx.webp_data = {nullptr, 0};
  ctx.last_timestamp = 0;
}

bool create_decoder() {
  destroy_decoder();

  if (!ctx.webp_buf || ctx.webp_len == 0) {
    ESP_LOGE(TAG, "No WebP data");
    return false;
  }

  ctx.webp_data.bytes = static_cast<const uint8_t*>(ctx.webp_buf);
  ctx.webp_data.size = ctx.webp_len;

  WebPAnimDecoderOptions opts;
  WebPAnimDecoderOptionsInit(&opts);
  opts.color_mode = MODE_RGBA;

  ctx.decoder = WebPAnimDecoderNew(&ctx.webp_data, &opts);
  if (!ctx.decoder) {
    ESP_LOGE(TAG, "Failed to create decoder");
    return false;
  }

  if (!WebPAnimDecoderGetInfo(ctx.decoder, &ctx.anim_info)) {
    WebPAnimDecoderDelete(ctx.decoder);
    ctx.decoder = nullptr;
    ESP_LOGE(TAG, "Failed to get anim info");
    return false;
  }

  ESP_LOGI(TAG, "Decoder created: %u frames, %ux%u",
           ctx.anim_info.frame_count, ctx.anim_info.canvas_width,
           ctx.anim_info.canvas_height);
  return true;
}

//------------------------------------------------------------------------------
// Buffer Management
//------------------------------------------------------------------------------

void free_buffer() {
  if (ctx.webp_buf && !is_static_asset(ctx.webp_buf)) {
    free(ctx.webp_buf);
  }
  ctx.webp_buf = nullptr;
  ctx.webp_len = 0;
}

//------------------------------------------------------------------------------
// Event Emission
//------------------------------------------------------------------------------

void emit_playing_event() {
  esp_event_post(GFX_PLAYER_EVENTS, GFX_PLAYER_EVT_PLAYING,
                 nullptr, 0, 0);
}

void emit_error_event() {
  esp_event_post(GFX_PLAYER_EVENTS, GFX_PLAYER_EVT_ERROR,
                 nullptr, 0, 0);
}

void emit_stopped_event() {
  esp_event_post(GFX_PLAYER_EVENTS, GFX_PLAYER_EVT_STOPPED,
                 nullptr, 0, 0);
}

//------------------------------------------------------------------------------
// WebSocket Notifications
//------------------------------------------------------------------------------

void send_displaying_notification(int counter) {
  if (!ctx.ws_handle ||
      !esp_websocket_client_is_connected(ctx.ws_handle)) {
    return;
  }
  char message[64];
  int len =
      snprintf(message, sizeof(message), "{\"displaying\":%d}", counter);
  if (len > 0 && static_cast<size_t>(len) < sizeof(message)) {
    esp_websocket_client_send_text(ctx.ws_handle, message, len,
                                   portMAX_DELAY);
    ESP_LOGD(TAG, "Sent displaying notification: %s", message);
  }
}

void send_queued_notification(int counter) {
  if (!ctx.ws_handle ||
      !esp_websocket_client_is_connected(ctx.ws_handle)) {
    return;
  }
  char message[64];
  int len =
      snprintf(message, sizeof(message), "{\"queued\":%d}", counter);
  if (len > 0 && static_cast<size_t>(len) < sizeof(message)) {
    esp_websocket_client_send_text(ctx.ws_handle, message, len,
                                   portMAX_DELAY);
    ESP_LOGD(TAG, "Sent queued notification: %s", message);
  }
}

//------------------------------------------------------------------------------
// State Transitions
//------------------------------------------------------------------------------

void goto_idle() {
  destroy_decoder();
  ctx.state.store(State::IDLE);
  xEventGroupSetBits(ctx.event_group, BIT_IDLE);
}

bool start_playback() {
  ctx.decode_error_count = 0;

  if (!create_decoder()) {
    return false;
  }

  ctx.playback_start_us = esp_timer_get_time();
  ctx.next_frame_tick = xTaskGetTickCount();
  ctx.last_timestamp = 0;
  ctx.state.store(State::PLAYING);
  xEventGroupClearBits(ctx.event_group, BIT_IDLE);

  send_displaying_notification(ctx.active_counter);
  emit_playing_event();
  ESP_LOGI(TAG, "Playback started: counter=%d, dwell=%ld",
           ctx.active_counter, static_cast<long>(ctx.dwell_secs));
  return true;
}

//------------------------------------------------------------------------------
// Decode Error Handling
//------------------------------------------------------------------------------

void handle_decode_error() {
  ctx.decode_error_count++;
  ESP_LOGW(TAG, "Decode error %d/%d", ctx.decode_error_count,
           DECODE_RETRY_COUNT);

  if (ctx.decode_error_count >= DECODE_RETRY_COUNT) {
    ESP_LOGE(TAG, "Max retries reached");
    draw_error_indicator_pixel();
    emit_error_event();
    free_buffer();
    goto_idle();
    return;
  }

  // Retry: recreate decoder after delay
  vTaskDelay(pdMS_TO_TICKS(DECODE_RETRY_DELAY_MS));
  if (!create_decoder()) {
    ctx.decode_error_count = DECODE_RETRY_COUNT;
    handle_decode_error();
  }
}

//------------------------------------------------------------------------------
// Command Handling
//------------------------------------------------------------------------------

void handle_pending_command() {
  if (!ctx.pending.valid.load(std::memory_order_acquire)) {
    // No valid pending = stop command (from gfx_interrupt)
    if (ctx.state.load() == State::PLAYING) {
      goto_idle();
      emit_stopped_event();
      ESP_LOGI(TAG, "Stopped by interrupt");
    }
    return;
  }

  // Play command — accept pending content
  {
    raii::MutexGuard lock(ctx.mutex);
    if (!lock) return;

    destroy_decoder();
    free_buffer();

    ctx.webp_buf = ctx.pending.buf;
    ctx.webp_len = ctx.pending.len;
    ctx.dwell_secs = ctx.pending.dwell_secs;
    ctx.active_counter = ctx.pending.counter;
    ctx.loaded_counter = ctx.pending.counter;

    ctx.pending.buf = nullptr;
    ctx.pending.len = 0;
    ctx.pending.valid.store(false, std::memory_order_release);
  }

  // Stop current if playing
  if (ctx.state.load() == State::PLAYING) {
    destroy_decoder();
  }

  // Start new playback
  if (!start_playback()) {
    ESP_LOGE(TAG, "start_playback failed");
    emit_error_event();
    free_buffer();
    goto_idle();
  }
}

//------------------------------------------------------------------------------
// Frame Decode and Render
// Returns frame delay in ms, or -1 on error
//------------------------------------------------------------------------------

int decode_and_render_frame() {
  if (!ctx.decoder) return -1;

  // Check for loop completion
  if (!WebPAnimDecoderHasMoreFrames(ctx.decoder)) {
    WebPAnimDecoderReset(ctx.decoder);
    ctx.last_timestamp = 0;
  }

  // Get next frame
  uint8_t* pix = nullptr;
  int timestamp = 0;
  if (!WebPAnimDecoderGetNext(ctx.decoder, &pix, &timestamp)) {
    return -1;
  }

  // Reset error count on successful decode
  ctx.decode_error_count = 0;

  // Render frame
#ifdef CONFIG_DISPLAY_FRAME_SYNC
  display_draw_buffer(pix, ctx.anim_info.canvas_width,
                      ctx.anim_info.canvas_height);
  display_wait_frame(50);
  display_flip();
#else
  display_draw(pix, ctx.anim_info.canvas_width,
               ctx.anim_info.canvas_height);
#endif

  // Calculate delay
  int delay_ms = timestamp - ctx.last_timestamp;
  ctx.last_timestamp = timestamp;

  // Static image: hold for remaining dwell time
  if (ctx.anim_info.frame_count == 1) {
    int64_t dwell_us = (ctx.dwell_secs > 0)
                           ? static_cast<int64_t>(ctx.dwell_secs) * 1000000
                           : 1000000;
    int64_t elapsed_us = esp_timer_get_time() - ctx.playback_start_us;
    int64_t remaining_us = dwell_us - elapsed_us;
    if (remaining_us > 0) {
      uint32_t remaining_ms = static_cast<uint32_t>(remaining_us / 1000);
      delay_ms = static_cast<int>(
          remaining_ms > 60000 ? 60000 : remaining_ms);
    } else {
      // Dwell expired, reset for next loop
      ctx.playback_start_us = esp_timer_get_time();
      delay_ms = 0;
    }
  }

  return (delay_ms > 0) ? delay_ms : 1;
}

//------------------------------------------------------------------------------
// Calculate Wait Ticks (drift-free timing)
//------------------------------------------------------------------------------

TickType_t calculate_wait_ticks(int delay_ms) {
  if (delay_ms <= 0) return 0;

  TickType_t target = ctx.next_frame_tick + pdMS_TO_TICKS(delay_ms);
  TickType_t now = xTaskGetTickCount();

  if (now >= target) {
    ctx.next_frame_tick = now;
    return 0;
  }

  ctx.next_frame_tick = target;
  return target - now;
}

//------------------------------------------------------------------------------
// Version Info Display (boot screen)
//------------------------------------------------------------------------------

void display_version_info(const char* img_url) {
  display_clear();
  char version_text[32];
  snprintf(version_text, sizeof(version_text), "v%s", FIRMWARE_VERSION);

  if (img_url && strlen(img_url) > 0) {
    ESP_LOGI(TAG, "Full URL: %s", img_url);
    char host_only[64] = {0};
    char last_two[32] = {0};

    struct http_parser_url u;
    http_parser_url_init(&u);

    if (http_parser_parse_url(img_url, strlen(img_url), 0, &u) == 0) {
      if (u.field_set & (1 << UF_HOST)) {
        size_t host_len = u.field_data[UF_HOST].len;
        if (host_len >= sizeof(host_only)) host_len = sizeof(host_only) - 1;
        memcpy(host_only, img_url + u.field_data[UF_HOST].off, host_len);
        host_only[host_len] = '\0';
      }

      if (u.field_set & (1 << UF_PATH)) {
        const char* path = img_url + u.field_data[UF_PATH].off;
        size_t path_len = u.field_data[UF_PATH].len;
        const char* last_slash = nullptr;
        const char* second_last_slash = nullptr;

        for (size_t i = 0; i < path_len; i++) {
          if (path[i] == '/') {
            second_last_slash = last_slash;
            last_slash = path + i;
          }
        }

        const char* src = second_last_slash ? second_last_slash : path;
        size_t len = static_cast<size_t>((path + path_len) - src);
        if (len >= sizeof(last_two)) len = sizeof(last_two) - 1;
        memcpy(last_two, src, len);
        last_two[len] = '\0';
      }
    }

    if (strlen(host_only) > 0) {
      ESP_LOGI(TAG, "Displaying host: '%s' at y=0", host_only);
      display_text(host_only, 0, 0, 255, 255, 255, 1);
    }

    if (strlen(last_two) > 0) {
      const char* disp = last_two;
      size_t plen = strlen(last_two);
      if (plen > 11) disp = last_two + (plen - 11);
      ESP_LOGI(TAG, "Displaying path: '%s' at y=10", disp);
      display_text(disp, 0, 10, 255, 255, 255, 1);
    }
  }

  int text_width = static_cast<int>(strlen(version_text)) * 6;
  int x = (64 - text_width) / 2;
  display_text(version_text, x, 24, 255, 255, 255, 1);
  display_flip();
  vTaskDelay(pdMS_TO_TICKS(2000));
}

//------------------------------------------------------------------------------
// Player Task
//------------------------------------------------------------------------------

void player_task(void*) {
  ESP_LOGI(TAG, "Player task started on core %d", xPortGetCoreID());

  while (true) {
    State state = ctx.state.load();

    // --- IDLE: block until command ---
    if (state == State::IDLE) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (ctx.paused.load()) continue;
      handle_pending_command();
      continue;
    }

    // --- PLAYING ---

    // Handle pause
    if (ctx.paused.load()) {
      goto_idle();
      emit_stopped_event();
      ESP_LOGI(TAG, "Paused");
      continue;
    }

    // Decode and render one frame
    int delay_ms = decode_and_render_frame();
    if (delay_ms < 0) {
      handle_decode_error();
      continue;
    }

    // Wait for frame delay OR notification
    TickType_t wait_ticks = calculate_wait_ticks(delay_ms);
    uint32_t notified = ulTaskNotifyTake(pdTRUE, wait_ticks);

    if (notified) {
      handle_pending_command();
    }
  }
}

}  // namespace

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

int gfx_initialize(const char* img_url) {
  if (ctx.initialized) {
    ESP_LOGE(TAG, "Already initialized");
    return 1;
  }

  ESP_LOGI(TAG, "Largest heap block: %d",
           heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  // Boot animation — use static asset directly
  ctx.webp_buf =
      const_cast<void*>(static_cast<const void*>(ASSET_BOOT_WEBP));
  ctx.webp_len = ASSET_BOOT_WEBP_LEN;
  ctx.dwell_secs = 0;
  ctx.active_counter = 0;

  ctx.mutex = xSemaphoreCreateMutex();
  if (!ctx.mutex) {
    ESP_LOGE(TAG, "Could not create mutex");
    return 1;
  }

  ctx.event_group = xEventGroupCreate();
  if (!ctx.event_group) {
    ESP_LOGE(TAG, "Could not create event group");
    return 1;
  }

  ctx.initialized = true;

  if (display_initialize()) return 1;

  if (!nvs_get_skip_display_version()) {
    display_version_info(img_url);
  }

  // Pre-initialize decoder so task starts in PLAYING state
  if (create_decoder()) {
    ctx.playback_start_us = esp_timer_get_time();
    ctx.next_frame_tick = xTaskGetTickCount();
    ctx.state.store(State::PLAYING);
  }

  BaseType_t ret = xTaskCreatePinnedToCore(
      player_task, "webp_player", TASK_STACK_SIZE, nullptr,
      TASK_PRIORITY, &ctx.task, TASK_CORE);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Could not create player task");
    return 1;
  }

  ESP_LOGI(TAG, "WebP player initialized");
  return 0;
}

void gfx_set_websocket_handle(esp_websocket_client_handle_t ws_handle) {
  ctx.ws_handle = ws_handle;
  ESP_LOGI(TAG, "Websocket handle set");
}

int gfx_update(void* webp, size_t len, int32_t dwell_secs) {
  raii::MutexGuard lock(ctx.mutex);
  if (!lock) {
    ESP_LOGE(TAG, "Could not take mutex");
    return -1;
  }

  // Free unconsumed pending buffer (frame-dropping)
  if (ctx.pending.valid.load() && ctx.pending.buf &&
      !is_static_asset(ctx.pending.buf)) {
    ESP_LOGW(TAG, "Dropping queued image (counter %d)", ctx.counter);
    free(ctx.pending.buf);
  }

  ctx.counter++;
  int counter = ctx.counter;

  ctx.pending.buf = webp;
  ctx.pending.len = len;
  ctx.pending.dwell_secs = dwell_secs;
  ctx.pending.counter = counter;
  ctx.pending.valid.store(true, std::memory_order_release);

  ESP_LOGI(TAG, "Queued image counter=%d size=%zu dwell=%ld",
           counter, len, static_cast<long>(dwell_secs));

  lock.release();

  xTaskNotifyGive(ctx.task);
  send_queued_notification(counter);
  return counter;
}

int gfx_get_loaded_counter(void) {
  if (!ctx.initialized) return -1;
  raii::MutexGuard lock(ctx.mutex);
  if (!lock) return -1;
  return ctx.loaded_counter;
}

int gfx_display_asset(const char* asset_type) {
  const uint8_t* asset_data = nullptr;
  size_t asset_len = 0;

  if (strcmp(asset_type, "config") == 0) {
    asset_data = ASSET_CONFIG_WEBP;
    asset_len = ASSET_CONFIG_WEBP_LEN;
  } else if (strcmp(asset_type, "error_404") == 0) {
    asset_data = ASSET_404_WEBP;
    asset_len = ASSET_404_WEBP_LEN;
  } else if (strcmp(asset_type, "no_connect") == 0) {
    asset_data = ASSET_NOCONNECT_WEBP;
    asset_len = ASSET_NOCONNECT_WEBP_LEN;
  } else if (strcmp(asset_type, "oversize") == 0) {
    ESP_LOGI(TAG, "DISPLAYING OVERSIZE GRAPHIC");
    asset_data = ASSET_OVERSIZE_WEBP;
    asset_len = ASSET_OVERSIZE_WEBP_LEN;
  } else {
    ESP_LOGE(TAG, "Unknown asset type: %s", asset_type);
    return 1;
  }

  gfx_interrupt();

  int result = gfx_update(
      const_cast<void*>(static_cast<const void*>(asset_data)),
      asset_len, 5);
  if (result < 0) {
    ESP_LOGE(TAG, "Failed to display %s asset", asset_type);
    return 1;
  }
  return 0;
}

void gfx_display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                      uint8_t b, int scale) {
  display_text(text, x, y, r, g, b, scale);
}

void gfx_stop(void) {
  ctx.paused.store(true);
  if (ctx.task) xTaskNotifyGive(ctx.task);
  ESP_LOGI(TAG, "Paused");
}

void gfx_start(void) {
  ctx.paused.store(false);
  if (ctx.task) xTaskNotifyGive(ctx.task);
  ESP_LOGI(TAG, "Resumed");
}

void gfx_shutdown(void) { display_shutdown(); }

void gfx_interrupt(void) {
  // Clear pending valid (signals stop to handle_pending_command)
  ctx.pending.valid.store(false, std::memory_order_release);
  if (ctx.task) xTaskNotifyGive(ctx.task);
}

void gfx_wait_idle(void) {
  if (!ctx.event_group) return;
  xEventGroupWaitBits(ctx.event_group, BIT_IDLE, pdFALSE, pdTRUE,
                      portMAX_DELAY);
}

bool gfx_is_animating(void) {
  if (!ctx.event_group) return false;
  return (xEventGroupGetBits(ctx.event_group) & BIT_IDLE) == 0;
}

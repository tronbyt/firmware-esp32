# C++ Migration & Architecture Overhaul Plan

Migration of the firmware-esp32 codebase from C to modern embedded C++, adopting architectural patterns from [koiosdigital/matrx-fw](https://github.com/koiosdigital/matrx-fw) while retaining our hardware-specific features (VSYNC, 2x scaling, HTTP polling mode).

## Guiding Principles

- **Incremental migration** — rename `.c` → `.cpp` one module at a time, keep building at every step
- **C public APIs** — all `.h` files keep `extern "C"` guards; no classes in public headers
- **C++ internals** — anonymous namespaces, `enum class`, `constexpr`, `std::atomic`, RAII
- **No STL containers** — use fixed arrays, raw pointers, `heap_caps_malloc` for SPIRAM
- **No exceptions / RTTI** — ESP-IDF disables these by default, keep it that way
- **One commit per phase** — each phase is independently buildable and testable

---

## Phase 0: Build System & Scaffolding

### 0.1 Switch to GLOB_RECURSE for source discovery

**File:** `main/CMakeLists.txt`

Replace the manual SRCS list with automatic discovery and add subdirectory include paths:

```cmake
file(GLOB_RECURSE MAIN_SRCS **.cpp **.c)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "." "display" "gfx" "network" "config" "system"
    REQUIRES ...)
```

This allows adding/moving files without touching CMakeLists.txt.

### 0.2 Create directory structure (empty, files move later)

```
main/
  main.cpp                    # Entry point (Phase 6)
  raii_utils.hpp              # Shared RAII wrappers (Phase 0)
  display/
    display.cpp               # Existing display.cpp (move in Phase 1)
    display.h
  gfx/
    gfx.cpp                   # WebP player (Phase 2)
    gfx.h
  network/
    wifi.cpp                  # WiFi STA management (Phase 3)
    wifi.h
    ws_client.cpp             # WebSocket client (Phase 6)
    ws_client.h
    http_client.cpp           # HTTP polling loop (Phase 6)
    http_client.h
    remote.cpp                # HTTP content fetch (Phase 3)
    remote.h
    sta_api.cpp               # Local REST API (Phase 3)
    sta_api.h
  config/
    nvs_settings.cpp          # NVS settings (Phase 1)
    nvs_settings.h
    ap.cpp                    # Captive portal (Phase 4)
    ap.h
  system/
    ota.cpp                   # OTA updates (Phase 3)
    ota.h
    heap_monitor.cpp          # Heap diagnostics (Phase 1)
    heap_monitor.h
    syslog.cpp                # Network logging (Phase 3)
    syslog.h
    sntp.cpp                  # NTP config (Phase 3)
    sntp.h
```

### 0.3 Add raii_utils.hpp

Based on matrx-fw's pattern, create shared RAII wrappers:

```cpp
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace raii {

class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t m, TickType_t timeout = portMAX_DELAY)
        : mutex_(m), acquired_(xSemaphoreTake(m, timeout) == pdTRUE) {}
    ~MutexGuard() { if (acquired_) xSemaphoreGive(mutex_); }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

    explicit operator bool() const { return acquired_; }
    void release() { if (acquired_) { xSemaphoreGive(mutex_); acquired_ = false; } }

private:
    SemaphoreHandle_t mutex_;
    bool acquired_;
};

}  // namespace raii
```

**Commit:** `build: restructure directories and add RAII utilities`

---

## Phase 1: Low-Risk Module Conversions

Modules with minimal state and no complex threading. Pure rename + C++ cleanup.

### 1.1 nvs_settings.c → config/nvs_settings.cpp

- Rename file, move to `config/`
- Replace `static` globals with anonymous namespace
- Use `constexpr` for `MAX_*` constants
- Replace C-style casts with `static_cast`
- Keep all public API as `extern "C"` free functions (header unchanged)

### 1.2 heap_monitor.c → system/heap_monitor.cpp

- Rename + move
- Anonymous namespace for statics
- `constexpr` for thresholds (-4096, -65536)

### 1.3 display.cpp → display/display.cpp

- Already C++, just move to subdirectory
- No code changes needed

### 1.4 sta_api.c → network/sta_api.cpp

- Rename + move
- Anonymous namespace for server handle and handlers

**Commit:** `refactor: convert low-risk modules to C++ and move to subdirectories`

---

## Phase 2: WebP Player Overhaul (gfx.c → gfx/gfx.cpp)

This is the largest and most impactful change. Adopts matrx-fw's player architecture while keeping our VSYNC and scaling features.

### 2.1 Rename and restructure state

Replace the `struct gfx_state` + `_state` pointer with an anonymous namespace context:

```cpp
namespace {

constexpr uint32_t NOTIFY_PLAY = (1 << 0);
constexpr uint32_t NOTIFY_STOP = (1 << 1);

constexpr int DECODE_RETRY_COUNT = 3;
constexpr int DECODE_RETRY_DELAY_MS = 200;

enum class State : uint8_t { IDLE, PLAYING };

struct PendingUpdate {
    std::atomic<bool> valid{false};
    void* buf = nullptr;
    size_t len = 0;
    int32_t dwell_secs = 0;
};

struct PlayerContext {
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t decoder_mutex = nullptr;
    EventGroupHandle_t event_group = nullptr;
    esp_websocket_client_handle_t ws_handle = nullptr;
    std::atomic<State> state{State::IDLE};
    PendingUpdate pending;
    int loaded_counter = 0;
    // Decoder state
    WebPAnimDecoder* decoder = nullptr;
    WebPData webp_data = {};
    WebPAnimInfo anim_info = {};
    TickType_t next_frame_tick = 0;
    TickType_t playback_start = 0;
    int error_count = 0;
};

PlayerContext ctx;

}  // namespace
```

### 2.2 One frame per iteration (from matrx-fw)

Replace the tight `draw_webp()` inner loop with a `decode_and_render_frame()` function that decodes one frame and returns the delay:

```cpp
// Returns frame delay in ms, 0 on loop reset, -1 on error
int decode_and_render_frame() {
    raii::MutexGuard lock(ctx.decoder_mutex, pdMS_TO_TICKS(50));
    if (!lock) return 0;  // Busy, try next iteration

    if (!WebPAnimDecoderHasMoreFrames(ctx.decoder)) {
        WebPAnimDecoderReset(ctx.decoder);
        return 0;
    }

    uint8_t* frame_buf;
    int timestamp;
    if (!WebPAnimDecoderGetNext(ctx.decoder, &frame_buf, &timestamp)) {
        return -1;
    }

    display_draw_buffer(frame_buf, ctx.anim_info.canvas_width,
                        ctx.anim_info.canvas_height);
    display_flip();

    int delay = timestamp - ctx.last_timestamp;
    ctx.last_timestamp = timestamp;
    return delay;
}
```

### 2.3 Task notification-based waiting (from matrx-fw)

Replace polling with `ulTaskNotifyTake`:

```cpp
void player_task(void*) {
    for (;;) {
        // Block until notified (zero CPU when idle)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Check for pending play command
        if (ctx.pending.valid.load()) {
            start_playback();
        }

        // Frame loop
        while (ctx.state.load() == State::PLAYING) {
            int delay = decode_and_render_frame();

            if (delay < 0) {
                handle_decode_error();
                continue;
            }

            if (check_duration_expired()) {
                goto_idle();
                break;
            }

            // Wait for frame delay OR early wake from notification
            TickType_t wait = calculate_wait_ticks(delay);
            if (wait > 0) {
                uint32_t notified = ulTaskNotifyTake(pdTRUE, wait);
                if (notified) {
                    // Woken early — new command pending
                    handle_pending_command();
                }
            }
        }
    }
}
```

### 2.4 Running tick accumulation (from matrx-fw)

Replace `xTaskDelayUntil` + `lastWakeTime` reset with drift-free timing:

```cpp
TickType_t calculate_wait_ticks(int delay_ms) {
    if (delay_ms <= 0) return 0;
    TickType_t target = ctx.next_frame_tick + pdMS_TO_TICKS(delay_ms);
    TickType_t now = xTaskGetTickCount();
    if (now >= target) {
        ctx.next_frame_tick = now;  // Behind schedule
        return 0;
    }
    ctx.next_frame_tick = target;
    return target - now;
}
```

### 2.5 Decode error retries (from matrx-fw)

```cpp
void handle_decode_error() {
    ctx.error_count++;
    if (ctx.error_count >= DECODE_RETRY_COUNT) {
        ESP_LOGE(TAG, "Decode failed after %d retries", DECODE_RETRY_COUNT);
        goto_idle();
        return;
    }
    ESP_LOGW(TAG, "Decode error, retry %d/%d", ctx.error_count, DECODE_RETRY_COUNT);
    vTaskDelay(pdMS_TO_TICKS(DECODE_RETRY_DELAY_MS));
    destroy_decoder();
    create_decoder();
}
```

### 2.6 Remove unnecessary vTaskDelay(1)

The current `vTaskDelay(pdMS_TO_TICKS(1))` after every frame render adds ~10ms latency for no benefit, especially with VSYNC. Remove it entirely — `display_wait_frame()` or `calculate_wait_ticks()` handles synchronization.

### 2.7 Use RAII MutexGuard

Replace manual `xSemaphoreTake`/`xSemaphoreGive` pairs with `raii::MutexGuard`.

**Commit:** `feat(gfx): rewrite WebP player with task notifications and one-frame-per-iteration`

---

## Phase 3: Remaining Module Conversions

Straightforward rename + C++ cleanup for each module. One commit per group.

### 3.1 network/ modules

- `wifi.c` → `network/wifi.cpp` — anonymous namespace, `enum class` for WiFi states
- `remote.c` → `network/remote.cpp` — anonymous namespace for callback state
- `syslog.c` → `system/syslog.cpp` — anonymous namespace, MutexGuard for log mutex
- `sntp.c` → `system/sntp.cpp` — trivial rename

**Commit:** `refactor: convert network and system modules to C++`

### 3.2 ota.c → system/ota.cpp

- Anonymous namespace
- `static_cast` for address casts
- MutexGuard if any added

**Commit:** `refactor: convert OTA module to C++`

---

## Phase 4: AP Module Conversion

### 4.1 ap.c → config/ap.cpp

Largest C file (709 lines). Convert incrementally:

- Anonymous namespace for all statics and HTML strings
- `constexpr` for string constants where possible
- Replace C-style casts
- Consider extracting HTML generation into a helper (optional)

### 4.2 dns_wrapper.c → config/dns_wrapper.cpp

Trivial rename + move.

**Commit:** `refactor: convert captive portal to C++`

---

## Phase 5: Extract WebSocket and HTTP Client from main.c

This breaks up the 743-line monolith.

### 5.1 Extract ws_client.cpp

Move from main.c to `network/ws_client.cpp`:

- `websocket_event_handler()` dispatch
- `ws_handle_text_message()` — JSON command parsing
- `ws_handle_binary_message()` — WebP reassembly
- `send_client_info()`
- WebSocket connection state (ws_handle, event group, reconnection loop)
- `ota_task_entry()` wrapper

Public API:

```cpp
// network/ws_client.h
esp_err_t ws_client_start(const char* url);
void ws_client_stop(void);
void ws_client_run_loop(void);  // Blocking reconnect loop
```

### 5.2 Extract http_client.cpp

Move the HTTP polling `for(;;)` loop from main.c to `network/http_client.cpp`:

```cpp
// network/http_client.h
void http_client_run_loop(const char* url);  // Blocking fetch loop
```

### 5.3 Slim down main.cpp

After extraction, main.cpp should contain only:

- `app_main()` — initialization sequence + dispatch to ws or http loop
- Button check
- Boot mode detection

Target: < 150 lines.

**Commit:** `refactor: extract WebSocket and HTTP client from main into separate modules`

---

## Phase 6: Final Rename and Cleanup

### 6.1 main.c → main.cpp

- Rename to `.cpp`
- Anonymous namespace for remaining statics
- `extern "C" void app_main()` entry point
- Remove any leftover C-isms

### 6.2 Remove flash.c / flash.h

Already removed from build in earlier work, delete the files entirely.

### 6.3 Update includes across all files

- Replace `<string.h>` → `<cstring>`
- Replace `<stdlib.h>` → `<cstdlib>`
- Replace `<stdint.h>` → `<cstdint>`
- etc. (only in `.cpp` files, `.h` files keep C headers for compatibility)

**Commit:** `refactor: complete C++ migration of main entry point`

---

## Phase 7: Advanced Improvements (Post-Migration)

These changes build on the C++ foundation but are optional enhancements.

### 7.1 ESP Event System for player state

Add `WEBP_PLAYER_EVENTS` event base with `PLAYING`, `ERROR`, `STOPPED` events (from matrx-fw). Decouple gfx from main.c / ws_client — components subscribe to events instead of polling `gfx_is_animating()`.

### 7.2 Separate decoder mutex

Add a dedicated `decoder_mutex` to the player context (50ms timeout on hot path) separate from the buffer ownership mutex. Reduces contention when `gfx_update()` is called during playback.

### 7.3 Static image sleep optimization

For single-frame images, calculate exact remaining display time and use `ulTaskNotifyTake(pdTRUE, remaining_ticks)` instead of polling every 100ms. Saves CPU and responds instantly to new content.

### 7.4 Prefetch timer (from matrx-fw scheduler)

In HTTP polling mode, start fetching the next image 2 seconds before the current dwell expires. Eliminates the visible gap between images:

```cpp
constexpr int64_t PREFETCH_BEFORE_US = 2 * 1000 * 1000;
```

### 7.5 mDNS service registration

Register `_tronbyt._tcp` mDNS service on WiFi connect. Makes device discoverable for local tooling and home automation.

---

## Migration Order Summary

| Phase | Scope | Risk | Files Changed |
|-------|-------|------|---------------|
| 0 | Build system + scaffolding | None | CMakeLists.txt, raii_utils.hpp |
| 1 | Low-risk modules (nvs, heap, display, sta_api) | Low | 4 files renamed + moved |
| 2 | **WebP player rewrite** | **Medium** | gfx.c → gfx.cpp (major rewrite) |
| 3 | Network + system modules | Low | 5 files renamed + moved |
| 4 | AP captive portal | Low | 2 files renamed + moved |
| 5 | **main.c split** | **Medium** | main.c → main.cpp + 2 new files |
| 6 | Final rename + cleanup | Low | main.c → main.cpp, delete flash.* |
| 7 | Advanced improvements | Optional | Various |

**Critical path:** Phase 0 → Phase 2 (gfx rewrite) → Phase 5 (main.c split)

The other phases can happen in any order after Phase 0 since they're independent renames.

---

## Testing Strategy

Each phase must pass:

1. **Build succeeds** for all board targets (tidbyt-gen1, tronbyt-s3, matrixportal-s3, etc.)
2. **Boot to animation** — device boots, connects to WiFi, displays content
3. **WebSocket mode** — receives images, settings updates, OTA commands
4. **HTTP polling mode** — fetches and displays images on interval
5. **AP mode** — captive portal works for WiFi configuration
6. **OTA** — firmware update completes successfully

No unit test framework exists currently. Consider adding `unity` (ESP-IDF's built-in test framework) for the gfx player state machine after Phase 2.

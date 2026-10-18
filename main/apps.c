#include "apps.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_api.h"
#include "display.h"
#include "gfx.h"

static const char *TAG = "apps";

#define NVS_NS "apps"
#define NVS_KEY "config"
#define FRAME_MS 40
#define MAX_SHOW_MS 60000  // cap for long marquees / notifications
#define TASK_STACK 12288  // TLS handshakes + JSON parsing run in this task

extern volatile int32_t isAnimating;  // -1 = touch "skip" (main.c)

static const app_t *const APPS[] = {&app_clock, &app_weather, &app_departures,
                                    &app_spotify, &app_message};
#define APP_COUNT (sizeof(APPS) / sizeof(APPS[0]))

static const char *DEFAULT_CONFIG =
    "{\"timezone\":\"CET-1CEST,M3.5.0,M10.5.0/3\","
    "\"brightness\":{\"day\":60,\"night\":10,\"night_start\":22,\"night_end\":7},"
    "\"rotation\":["
    "{\"app\":\"clock\",\"dwell\":10,\"enabled\":true,\"config\":{\"24h\":true}},"
    "{\"app\":\"weather\",\"dwell\":10,\"enabled\":false,"
    "\"config\":{\"latitude\":null,\"longitude\":null}},"
    "{\"app\":\"spotify\",\"dwell\":10,\"enabled\":true,\"config\":{}}"
    "]}";

typedef struct {
  char title[32];
  char text[256];
  char color[10];
  int secs;
} notification_t;

static SemaphoreHandle_t s_lock;
static char *s_config_json;  // stored config, guarded by s_lock
static volatile bool s_config_changed;
static QueueHandle_t s_notifications;
static volatile bool s_skip;
static volatile bool s_power = true;
static const char *volatile s_current = "";
static TaskHandle_t s_task;
static int s_brightness = -1;

// --- helpers shared with the apps (app_api.h) ---------------------------------

int cfg_int(const cJSON *cfg, const char *key, int def) {
  const cJSON *v = cJSON_GetObjectItem(cfg, key);
  return cJSON_IsNumber(v) ? v->valueint : def;
}

double cfg_double(const cJSON *cfg, const char *key, double def) {
  const cJSON *v = cJSON_GetObjectItem(cfg, key);
  return cJSON_IsNumber(v) ? v->valuedouble : def;
}

bool cfg_bool(const cJSON *cfg, const char *key, bool def) {
  const cJSON *v = cJSON_GetObjectItem(cfg, key);
  return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

const char *cfg_str(const cJSON *cfg, const char *key, const char *def) {
  const cJSON *v = cJSON_GetObjectItem(cfg, key);
  return cJSON_IsString(v) ? v->valuestring : def;
}

bool cfg_has_number(const cJSON *cfg, const char *key) {
  return cJSON_IsNumber(cJSON_GetObjectItem(cfg, key));
}

rgb_t parse_color(const char *hex, rgb_t def) {
  if (!hex) return def;
  if (hex[0] == '#') hex++;
  if (strlen(hex) != 6) return def;
  char *end;
  unsigned long v = strtoul(hex, &end, 16);
  if (*end) return def;
  return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
static int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int yoe = (int)(y - era * 400);
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

time_t parse_iso8601_utc(const char *s) {
  int y, mo, d, h, mi, sec;
  if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) != 6)
    return 0;
  return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
}

bool time_is_valid(void) { return time(NULL) > 1700000000; }

// --- config -------------------------------------------------------------------

static void *psram_malloc(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  return p ? p : malloc(size);
}

static const app_t *find_app(const char *name) {
  for (size_t i = 0; i < APP_COUNT; i++) {
    if (name && strcmp(APPS[i]->name, name) == 0) return APPS[i];
  }
  return NULL;
}

static esp_err_t validate(const cJSON *root, const char **error) {
  if (!cJSON_IsObject(root)) return *error = "config must be a JSON object", ESP_ERR_INVALID_ARG;
  const cJSON *rotation = cJSON_GetObjectItem(root, "rotation");
  if (!cJSON_IsArray(rotation)) return *error = "rotation must be an array", ESP_ERR_INVALID_ARG;
  const cJSON *item;
  cJSON_ArrayForEach(item, rotation) {
    if (!find_app(cfg_str(item, "app", NULL)))
      return *error = "rotation item with unknown or missing app", ESP_ERR_INVALID_ARG;
    int dwell = cfg_int(item, "dwell", 10);
    if (dwell < 1 || dwell > 3600) return *error = "dwell must be 1-3600", ESP_ERR_INVALID_ARG;
    const cJSON *c = cJSON_GetObjectItem(item, "config");
    if (c && !cJSON_IsObject(c)) return *error = "item config must be an object", ESP_ERR_INVALID_ARG;
  }
  const cJSON *tz = cJSON_GetObjectItem(root, "timezone");
  if (tz && !cJSON_IsString(tz)) return *error = "timezone must be a string", ESP_ERR_INVALID_ARG;
  return ESP_OK;
}

static char *load_stored_config(void) {
  nvs_handle_t h;
  char *json = NULL;
  size_t len = 0;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    if (nvs_get_blob(h, NVS_KEY, NULL, &len) == ESP_OK && len > 0) {
      json = psram_malloc(len + 1);
      if (json && nvs_get_blob(h, NVS_KEY, json, &len) == ESP_OK) {
        json[len] = '\0';
      } else {
        free(json);
        json = NULL;
      }
    }
    nvs_close(h);
  }
  if (json) {
    const char *error;
    cJSON *root = cJSON_Parse(json);
    bool ok = validate(root, &error) == ESP_OK;
    cJSON_Delete(root);
    if (ok) return json;
    ESP_LOGW(TAG, "stored config invalid (%s), using defaults", error);
    free(json);
  }
  return strdup(DEFAULT_CONFIG);
}

void apps_init(void) {
  if (s_lock) return;
  // Big API responses (departures, Spotify) parse into thousands of small
  // cJSON nodes; keep them out of the scarce internal RAM.
  cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = free};
  cJSON_InitHooks(&hooks);
  s_config_json = load_stored_config();
  s_lock = xSemaphoreCreateMutex();
}

char *apps_get_config_json(void) {
  if (!s_lock) return strdup(DEFAULT_CONFIG);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  char *copy = strdup(s_config_json ? s_config_json : DEFAULT_CONFIG);
  xSemaphoreGive(s_lock);
  return copy;
}

esp_err_t apps_set_config_json(const char *json, const char **error) {
  if (!s_lock) {
    *error = "not initialised";
    return ESP_ERR_INVALID_STATE;
  }
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    *error = "invalid JSON";
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = validate(root, error);
  if (err != ESP_OK) {
    cJSON_Delete(root);
    return err;
  }
  char *compact = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!compact) return ESP_ERR_NO_MEM;

  nvs_handle_t h;
  err = nvs_open(NVS_NS, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_set_blob(h, NVS_KEY, compact, strlen(compact));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) {
    *error = "could not save to flash";
    free(compact);
    return err;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  free(s_config_json);
  s_config_json = compact;
  xSemaphoreGive(s_lock);
  s_config_changed = true;  // the task picks it up at the next frame
  ESP_LOGI(TAG, "config updated");
  return ESP_OK;
}

static cJSON *take_config(void) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  cJSON *root = cJSON_Parse(s_config_json);
  s_config_changed = false;
  xSemaphoreGive(s_lock);
  const char *tz = cfg_str(root, "timezone", "CET-1CEST,M3.5.0,M10.5.0/3");
  setenv("TZ", tz, 1);
  tzset();
  return root;
}

// --- runtime ------------------------------------------------------------------

static void update_brightness(const cJSON *cfg, bool force) {
  if (!s_power) return;
  const cJSON *b = cJSON_GetObjectItem(cfg, "brightness");
  if (!b) return;
  int target = cfg_int(b, "day", 60);
  if (time_is_valid()) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int start = cfg_int(b, "night_start", 22), end = cfg_int(b, "night_end", 7);
    bool night = start > end ? (tm.tm_hour >= start || tm.tm_hour < end)
                             : (tm.tm_hour >= start && tm.tm_hour < end);
    if (night) target = cfg_int(b, "night", 10);
  }
  if (target < 0) target = 0;
  if (target > DISPLAY_MAX_BRIGHTNESS) target = DISPLAY_MAX_BRIGHTNESS;
  if (force || target != s_brightness) {
    display_set_brightness((uint8_t)target);
    s_brightness = target;
  }
}

// Should the app on screen give way? Consumes skip requests.
static bool interrupted(void) {
  if (s_skip) {
    s_skip = false;
    return true;
  }
  if (isAnimating == -1) {  // touch TAP in main.c
    isAnimating = 0;
    return true;
  }
  return s_config_changed || !s_power || uxQueueMessagesWaiting(s_notifications) > 0;
}

typedef uint32_t (*frame_fn)(const void *ctx, uint32_t t_ms);

// Draw frames until min_ms (or longer if the content needs it) has passed.
static void run_frames(frame_fn draw, const void *ctx, uint32_t min_ms,
                       bool interruptible, const cJSON *cfg) {
  int64_t start = esp_timer_get_time();
  uint32_t need = 0;
  int64_t last_brightness = start;
  for (;;) {
    uint32_t t = (uint32_t)((esp_timer_get_time() - start) / 1000);
    uint32_t until = min_ms > need ? min_ms : need;
    if (until > MAX_SHOW_MS) until = MAX_SHOW_MS;
    if (t >= until) break;
    if (interruptible && interrupted()) break;
    canvas_clear(RGB(0, 0, 0));
    need = draw(ctx, t);
    canvas_present();
    if (esp_timer_get_time() - last_brightness > 30 * 1000000LL) {
      update_brightness(cfg, false);
      last_brightness = esp_timer_get_time();
    }
    vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
  }
}

typedef struct {
  const app_t *app;
  const cJSON *cfg;
} app_ctx_t;

static uint32_t draw_app(const void *ctx, uint32_t t_ms) {
  const app_ctx_t *c = ctx;
  return c->app->draw(c->cfg, t_ms);
}

static uint32_t draw_notification(const void *ctx, uint32_t t_ms) {
  const notification_t *n = ctx;
  return message_draw(n->title, n->text, parse_color(n->color, RGB(255, 255, 255)),
                      t_ms);
}

static void apps_task(void *arg) {
  // The WebP renderer stays paused while the apps own the display.
  gfx_stop();
  vTaskDelay(pdMS_TO_TICKS(200));
  isAnimating = 0;

  cJSON *cfg = take_config();
  update_brightness(cfg, true);
  int index = 0, skipped = 0;

  for (;;) {
    if (s_config_changed) {
      cJSON_Delete(cfg);
      cfg = take_config();
      update_brightness(cfg, true);
    }
    if (!s_power) {
      s_current = "";
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    notification_t n;
    if (xQueueReceive(s_notifications, &n, 0) == pdTRUE) {
      s_current = "notification";
      run_frames(draw_notification, &n, n.secs * 1000, false, cfg);
      continue;
    }

    const cJSON *rotation = cJSON_GetObjectItem(cfg, "rotation");
    int count = cJSON_GetArraySize(rotation);
    if (count == 0 || skipped >= count) {
      // Nothing to show right now (all disabled or all skipping): show the
      // clock rather than a frozen frame.
      app_ctx_t c = {&app_clock, NULL};
      s_current = "clock";
      run_frames(draw_app, &c, 5000, true, cfg);
      skipped = 0;
      continue;
    }

    const cJSON *item = cJSON_GetArrayItem(rotation, index++ % count);
    const app_t *app = find_app(cfg_str(item, "app", NULL));
    const cJSON *app_cfg = cJSON_GetObjectItem(item, "config");
    if (!app || !cfg_bool(item, "enabled", true) || !app->prepare(app_cfg)) {
      skipped++;
      continue;
    }
    skipped = 0;
    s_current = app->name;
    app_ctx_t c = {app, app_cfg};
    run_frames(draw_app, &c, cfg_int(item, "dwell", 10) * 1000, true, cfg);
  }
}

// --- public API -----------------------------------------------------------------

bool apps_local_mode(const char *image_url) {
  return image_url == NULL || image_url[0] == '\0' || strcmp(image_url, "local") == 0;
}

void apps_start(void) {
  if (s_task) return;
  apps_init();
  s_notifications = xQueueCreate(8, sizeof(notification_t));
  xTaskCreatePinnedToCore(apps_task, "apps", TASK_STACK, NULL, 2, &s_task, 1);
  ESP_LOGI(TAG, "local apps started");
}

esp_err_t apps_notify(const char *title, const char *text, const char *color,
                      int secs) {
  if (!s_notifications) return ESP_ERR_INVALID_STATE;
  notification_t n = {.secs = secs > 0 ? secs : 15};
  if (n.secs > 600) n.secs = 600;
  snprintf(n.title, sizeof(n.title), "%s", title ? title : "");
  snprintf(n.text, sizeof(n.text), "%s", text ? text : "");
  snprintf(n.color, sizeof(n.color), "%s", color ? color : "");
  return xQueueSend(s_notifications, &n, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

void apps_next(void) { s_skip = true; }

void apps_set_display_power(bool on) {
  s_power = on;
  if (on) s_brightness = -1;  // reapply the schedule on the next frame
}

const char *apps_current(void) { return s_current; }

char *apps_list_json(void) {
  cJSON *arr = cJSON_CreateArray();
  for (size_t i = 0; i < APP_COUNT; i++)
    cJSON_AddItemToArray(arr, cJSON_CreateString(APPS[i]->name));
  char *out = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  return out;
}

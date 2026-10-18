// Spotify "now playing": album art, scrolling title/artist, progress bar.
//
// Uses a refresh token from the PKCE flow (tools/spotify_auth.py), stored in
// NVS together with the app's client ID. Skipped while nothing is playing
// unless cfg "show_paused" is true.

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_api.h"
#include "http_util.h"
#include "jpeg_decoder.h"

static const char *TAG = "app_spotify";

#define NVS_NS "apps"
#define POLL_US (5 * 1000000LL)
#define ART 32

static struct {
  char client_id[64];
  char refresh_token[256];
  char access_token[512];
  int64_t token_expires_us;

  bool have_track;
  bool playing;
  char track_id[64];
  char title[128];
  char artist[128];
  int progress_ms, duration_ms;
  int64_t polled_us;

  char art_url[160];
  uint8_t *art;  // ART*ART*3 RGB888 in PSRAM
  bool art_valid;
} s;

static void load_credentials(void) {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  size_t len = sizeof(s.client_id);
  nvs_get_str(h, "sp_cid", s.client_id, &len);
  len = sizeof(s.refresh_token);
  nvs_get_str(h, "sp_rt", s.refresh_token, &len);
  nvs_close(h);
}

static void save_refresh_token(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "sp_cid", s.client_id);
  nvs_set_str(h, "sp_rt", s.refresh_token);
  nvs_commit(h);
  nvs_close(h);
}

void spotify_set_credentials(const char *client_id, const char *refresh_token) {
  load_credentials();
  snprintf(s.client_id, sizeof(s.client_id), "%s", client_id ? client_id : "");
  snprintf(s.refresh_token, sizeof(s.refresh_token), "%s",
           refresh_token ? refresh_token : "");
  s.access_token[0] = '\0';
  s.token_expires_us = 0;
  s.have_track = false;
  save_refresh_token();
}

bool spotify_is_configured(void) {
  load_credentials();
  return s.client_id[0] && s.refresh_token[0];
}

static bool refresh_access_token(void) {
  char body[512];
  snprintf(body, sizeof(body),
           "grant_type=refresh_token&refresh_token=%s&client_id=%s",
           s.refresh_token, s.client_id);
  const char *headers[] = {"Content-Type", "application/x-www-form-urlencoded",
                           NULL};
  http_response_t resp;
  if (http_request("POST", "https://accounts.spotify.com/api/token", headers,
                   body, 8 * 1024, &resp) != ESP_OK ||
      resp.status != 200) {
    ESP_LOGW(TAG, "token refresh failed (status %d): %.200s", resp.status,
             resp.body ? resp.body : "");
    http_response_free(&resp);
    return false;
  }
  cJSON *root = cJSON_Parse(resp.body);
  http_response_free(&resp);
  const char *token = cfg_str(root, "access_token", "");
  int expires_in = cfg_int(root, "expires_in", 3600);
  snprintf(s.access_token, sizeof(s.access_token), "%s", token);
  s.token_expires_us = esp_timer_get_time() + (expires_in - 60) * 1000000LL;
  // PKCE refresh tokens may rotate; keep the newest one.
  const char *rotated = cfg_str(root, "refresh_token", NULL);
  if (rotated && strcmp(rotated, s.refresh_token) != 0) {
    snprintf(s.refresh_token, sizeof(s.refresh_token), "%s", rotated);
    save_refresh_token();
  }
  cJSON_Delete(root);
  return s.access_token[0] != '\0';
}

// Decode a JPEG cover into the 32x32 art buffer (nearest-neighbour resample
// after TJpgDec's power-of-two downscale).
static bool decode_art(const uint8_t *jpg, size_t len) {
  esp_jpeg_image_cfg_t cfg = {.indata = (uint8_t *)jpg, .indata_size = len};
  esp_jpeg_image_output_t info;
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) return false;

  esp_jpeg_image_scale_t scale = JPEG_IMAGE_SCALE_0;
  int w = info.width, h = info.height;
  if (w >= ART * 8 && h >= ART * 8) {
    scale = JPEG_IMAGE_SCALE_1_8, w /= 8, h /= 8;
  } else if (w >= ART * 4 && h >= ART * 4) {
    scale = JPEG_IMAGE_SCALE_1_4, w /= 4, h /= 4;
  } else if (w >= ART * 2 && h >= ART * 2) {
    scale = JPEG_IMAGE_SCALE_1_2, w /= 2, h /= 2;
  }
  // TJpgDec rounds odd sizes up; allocate a little extra.
  size_t out_size = (size_t)(w + 8) * (h + 8) * 3;
  uint8_t *out = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM);
  if (!out) return false;
  cfg.outbuf = out;
  cfg.outbuf_size = out_size;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
  cfg.out_scale = scale;
  esp_jpeg_image_output_t img;
  esp_err_t err = esp_jpeg_decode(&cfg, &img);
  if (err == ESP_OK) {
    if (!s.art) s.art = heap_caps_malloc(ART * ART * 3, MALLOC_CAP_SPIRAM);
    if (s.art) {
      for (int y = 0; y < ART; y++) {
        for (int x = 0; x < ART; x++) {
          const uint8_t *p = out + ((y * img.height / ART) * img.width + x * img.width / ART) * 3;
          memcpy(s.art + (y * ART + x) * 3, p, 3);
        }
      }
    }
  } else {
    ESP_LOGW(TAG, "cover decode failed: %s", esp_err_to_name(err));
  }
  free(out);
  return err == ESP_OK && s.art;
}

static void fetch_art(const char *url) {
  if (strcmp(url, s.art_url) == 0) return;
  snprintf(s.art_url, sizeof(s.art_url), "%s", url);
  s.art_valid = false;
  if (!url[0]) return;
  http_response_t resp;
  if (http_request("GET", url, NULL, NULL, 128 * 1024, &resp) == ESP_OK &&
      resp.status == 200) {
    s.art_valid = decode_art((const uint8_t *)resp.body, resp.len);
  }
  http_response_free(&resp);
}

// Smallest cover that is still at least ART pixels wide.
static const char *pick_image(const cJSON *images) {
  const char *best = "";
  int best_w = 1 << 30;
  const cJSON *img;
  cJSON_ArrayForEach(img, images) {
    int w = cfg_int(img, "width", 0);
    if (w >= ART && w < best_w) {
      best_w = w;
      best = cfg_str(img, "url", "");
    }
  }
  return best;
}

static void poll(void) {
  if (esp_timer_get_time() - s.polled_us < POLL_US) return;
  if ((!s.access_token[0] || esp_timer_get_time() > s.token_expires_us) &&
      !refresh_access_token())
    return;

  char auth[600];
  snprintf(auth, sizeof(auth), "Bearer %s", s.access_token);
  const char *headers[] = {"Authorization", auth, NULL};
  http_response_t resp;
  esp_err_t err = http_request(
      "GET",
      "https://api.spotify.com/v1/me/player/currently-playing?additional_types=episode",
      headers, NULL, 64 * 1024, &resp);
  if (err != ESP_OK) {
    http_response_free(&resp);
    return;
  }
  s.polled_us = esp_timer_get_time();
  if (resp.status == 401) {
    s.access_token[0] = '\0';  // expired early; refresh on the next turn
    s.polled_us = 0;
  } else if (resp.status == 204 || resp.len == 0) {
    s.have_track = false;  // nothing playing
  } else if (resp.status == 200) {
    cJSON *root = cJSON_Parse(resp.body);
    const cJSON *item = cJSON_GetObjectItem(root, "item");
    if (cJSON_IsObject(item)) {
      s.have_track = true;
      s.playing = cfg_bool(root, "is_playing", false);
      s.progress_ms = cfg_int(root, "progress_ms", 0);
      s.duration_ms = cfg_int(item, "duration_ms", 0);
      snprintf(s.track_id, sizeof(s.track_id), "%s", cfg_str(item, "id", ""));
      snprintf(s.title, sizeof(s.title), "%s", cfg_str(item, "name", ""));

      // Track: join artist names. Podcast episode: the show's name.
      s.artist[0] = '\0';
      const cJSON *artist;
      cJSON_ArrayForEach(artist, cJSON_GetObjectItem(item, "artists")) {
        size_t used = strlen(s.artist);
        snprintf(s.artist + used, sizeof(s.artist) - used, "%s%s",
                 used ? ", " : "", cfg_str(artist, "name", ""));
      }
      const cJSON *show = cJSON_GetObjectItem(item, "show");
      if (!s.artist[0] && show) {
        snprintf(s.artist, sizeof(s.artist), "%s", cfg_str(show, "name", ""));
      }
      const cJSON *images =
          cJSON_GetObjectItem(cJSON_GetObjectItem(item, "album"), "images");
      if (!images) images = cJSON_GetObjectItem(item, "images");
      char url[160];
      snprintf(url, sizeof(url), "%s", pick_image(images));
      cJSON_Delete(root);
      http_response_free(&resp);
      fetch_art(url);
      return;
    }
    s.have_track = false;  // e.g. an ad
    cJSON_Delete(root);
  } else {
    ESP_LOGW(TAG, "currently-playing returned %d", resp.status);
  }
  http_response_free(&resp);
}

static bool spotify_prepare(const cJSON *cfg) {
  if (!spotify_is_configured() || !time_is_valid()) return false;
  poll();
  if (!s.have_track) return false;
  return s.playing || cfg_bool(cfg, "show_paused", false);
}

static void draw_note_icon(void) {
  canvas_rect(0, 0, ART - 1, ART - 1, RGB(30, 30, 30));
  rgb_t green = RGB(30, 215, 96);
  canvas_rect(18, 7, 19, 21, green);
  canvas_line(19, 7, 24, 10, green);
  canvas_ellipse(12, 19, 19, 25, green);
}

static uint32_t spotify_draw(const cJSON *cfg, uint32_t t_ms) {
  if (s.art_valid) {
    canvas_blit_rgb(0, 0, ART, ART, s.art);
  } else {
    draw_note_icon();
  }
  uint32_t need = canvas_marquee(&font_tb8, 35, 63, 3, s.title,
                                 RGB(255, 255, 255), t_ms);
  uint32_t need_artist = canvas_marquee(&font_tb8, 35, 63, 13, s.artist,
                                        RGB(170, 170, 170), t_ms);
  if (need_artist > need) need = need_artist;

  int progress = s.progress_ms;
  if (s.playing) progress += (int)((esp_timer_get_time() - s.polled_us) / 1000);
  if (progress > s.duration_ms) progress = s.duration_ms;
  canvas_rect(35, 27, 63, 28, RGB(50, 50, 50));
  if (s.duration_ms > 0) {
    int fill = 29 * progress / s.duration_ms;
    if (fill > 0) canvas_rect(35, 27, 35 + fill - 1, 28, RGB(30, 215, 96));
  }
  if (!s.playing) {  // pause symbol
    canvas_rect(36, 21, 37, 24, RGB(170, 170, 170));
    canvas_rect(40, 21, 41, 24, RGB(170, 170, 170));
  }
  return need;
}

const app_t app_spotify = {"spotify", spotify_prepare, spotify_draw};

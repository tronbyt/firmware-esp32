// WireGuard client for the Tronbyt firmware.
//
// The configuration is a standard WireGuard .conf (as exported by many routers
// for a single device). Only IPv4 is used. Traffic for the tunnel's subnet
// (the "Address" line) goes through the tunnel; everything else, including
// the WireGuard packets themselves, keeps using WiFi.

#include "wg.h"

#include <ctype.h>
#include <errno.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_wireguard.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/ip4_addr.h>
#include <lwip/netdb.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>

#include "nvs_settings.h"
#include "wifi.h"

static const char* TAG = "wg";

// The Curve25519 handshake runs in the lwIP thread and overflows its default
// stack (see sdkconfig.defaults.wireguard).
_Static_assert(CONFIG_LWIP_TCPIP_TASK_STACK_SIZE >= 6144,
               "WireGuard needs CONFIG_LWIP_TCPIP_TASK_STACK_SIZE >= 6144");

#define WG_KEY_STR_LEN 44  // base64 of 32 bytes
#define WG_MAX_ALLOWED 3   // + the device's own address = 4 source IPs
#define WG_HOST_LEN 96

typedef struct {
  char private_key[WG_KEY_STR_LEN + 4];
  char public_key[WG_KEY_STR_LEN + 4];
  char preshared_key[WG_KEY_STR_LEN + 4];
  char address[16];
  char netmask[16];
  char endpoint[WG_HOST_LEN];
  int port;
  int keepalive;
  char allowed_ip[WG_MAX_ALLOWED][16];
  char allowed_mask[WG_MAX_ALLOWED][16];
  int allowed_count;
  int allowed_ignored;  // IPv4 AllowedIPs entries beyond WG_MAX_ALLOWED
} wg_conf_t;

typedef enum {
  WG_STATE_UNDECIDED = 0,
  WG_STATE_DIRECT,  // server reachable without tunnel
  WG_STATE_UP,      // tunnel established
  WG_STATE_DOWN,    // tunnel wanted but not (yet) established
} wg_state_t;

static wg_conf_t s_conf;
static wireguard_config_t s_wg_config = ESP_WIREGUARD_CONFIG_DEFAULT();
static wireguard_ctx_t s_ctx = ESP_WIREGUARD_CONTEXT_DEFAULT();
static volatile wg_state_t s_state = WG_STATE_UNDECIDED;
static bool s_initialized = false;
static uint8_t s_mode = WG_MODE_OFF;
static char s_probe_host[WG_HOST_LEN];
static char s_probe_port[8];

// ---------------------------------------------------------------- parsing

static char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;
  char* end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) end--;
  *end = '\0';
  return s;
}

static void prefix_to_mask(int prefix, char* out, size_t out_len) {
  if (prefix < 0) prefix = 0;
  if (prefix > 32) prefix = 32;
  uint32_t mask = prefix == 0 ? 0 : 0xffffffffu << (32 - prefix);
  snprintf(out, out_len, "%u.%u.%u.%u", (unsigned)(mask >> 24),
           (unsigned)((mask >> 16) & 0xff), (unsigned)((mask >> 8) & 0xff),
           (unsigned)(mask & 0xff));
}

// Splits "a.b.c.d/nn" into ip and prefix. Returns false for anything that is
// not a plain IPv4 address (e.g. IPv6 entries, which are ignored).
static bool parse_cidr(char* item, char* ip, size_t ip_len, int* prefix) {
  item = trim(item);
  if (*item == '\0' || strchr(item, ':') != NULL) return false;
  *prefix = 32;
  char* slash = strchr(item, '/');
  if (slash) {
    *slash = '\0';
    *prefix = atoi(slash + 1);
  }
  ip4_addr_t tmp;
  if (!ip4addr_aton(item, &tmp)) return false;
  strlcpy(ip, item, ip_len);
  return true;
}

static bool parse_conf(const char* text, wg_conf_t* c) {
  memset(c, 0, sizeof(*c));
  c->port = 51820;
  c->keepalive = 25;
  int address_prefix = 32;

  char* copy = strdup(text);
  if (!copy) return false;

  char* save_line = NULL;
  for (char* line = strtok_r(copy, "\r\n", &save_line); line;
       line = strtok_r(NULL, "\r\n", &save_line)) {
    line = trim(line);
    if (*line == '\0' || *line == '#' || *line == ';' || *line == '[') continue;
    char* eq =
        strchr(line, '=');  // base64 values end in '=', so split at the first
    if (!eq) continue;
    *eq = '\0';
    char* key = trim(line);
    char* val = trim(eq + 1);

    if (strcasecmp(key, "PrivateKey") == 0) {
      strlcpy(c->private_key, val, sizeof(c->private_key));
    } else if (strcasecmp(key, "PublicKey") == 0) {
      strlcpy(c->public_key, val, sizeof(c->public_key));
    } else if (strcasecmp(key, "PresharedKey") == 0) {
      strlcpy(c->preshared_key, val, sizeof(c->preshared_key));
    } else if (strcasecmp(key, "PersistentKeepalive") == 0) {
      c->keepalive = atoi(val);
    } else if (strcasecmp(key, "Address") == 0) {
      char* save = NULL;
      for (char* item = strtok_r(val, ",", &save); item;
           item = strtok_r(NULL, ",", &save)) {
        if (c->address[0] == '\0' &&
            parse_cidr(item, c->address, sizeof(c->address), &address_prefix)) {
          break;
        }
      }
    } else if (strcasecmp(key, "AllowedIPs") == 0) {
      char* save = NULL;
      for (char* item = strtok_r(val, ",", &save); item;
           item = strtok_r(NULL, ",", &save)) {
        int prefix;
        char ip[16];
        if (!parse_cidr(item, ip, sizeof(ip), &prefix)) continue;  // IPv6
        if (c->allowed_count >= WG_MAX_ALLOWED) {
          c->allowed_ignored++;
          continue;
        }
        strlcpy(c->allowed_ip[c->allowed_count], ip, sizeof(c->allowed_ip[0]));
        prefix_to_mask(prefix, c->allowed_mask[c->allowed_count],
                       sizeof(c->allowed_mask[0]));
        c->allowed_count++;
      }
    } else if (strcasecmp(key, "Endpoint") == 0) {
      char* colon = strrchr(val, ':');
      if (colon && strchr(colon, ']') == NULL) {
        *colon = '\0';
        c->port = atoi(colon + 1);
      }
      if (*val == '[') {  // [v6-literal]
        val++;
        char* close = strchr(val, ']');
        if (close) *close = '\0';
      }
      strlcpy(c->endpoint, val, sizeof(c->endpoint));
    }
  }
  free(copy);

  // The interface netmask decides what is routed into the tunnel. If the
  // address is a single host (/32), use the allowed network that contains it.
  if (address_prefix == 32 && c->address[0]) {
    ip4_addr_t addr, net, mask;
    ip4addr_aton(c->address, &addr);
    for (int i = 0; i < c->allowed_count; i++) {
      ip4addr_aton(c->allowed_ip[i], &net);
      ip4addr_aton(c->allowed_mask[i], &mask);
      if (mask.addr != 0 && (addr.addr & mask.addr) == (net.addr & mask.addr)) {
        strlcpy(c->netmask, c->allowed_mask[i], sizeof(c->netmask));
        break;
      }
    }
  }
  if (c->netmask[0] == '\0') {
    prefix_to_mask(address_prefix, c->netmask, sizeof(c->netmask));
  }

  if (strlen(c->private_key) != WG_KEY_STR_LEN ||
      strlen(c->public_key) != WG_KEY_STR_LEN || c->address[0] == '\0' ||
      c->endpoint[0] == '\0' || c->port <= 0 || c->port > 65535) {
    return false;
  }
  if (c->preshared_key[0] && strlen(c->preshared_key) != WG_KEY_STR_LEN) {
    return false;
  }
  return true;
}

// Extracts host and port from http://, https://, ws:// or wss:// URLs.
static bool parse_url_host(const char* url, char* host, size_t host_len,
                           char* port, size_t port_len) {
  const char* p = strstr(url, "://");
  if (!p) return false;
  bool tls = strncmp(url, "https", 5) == 0 || strncmp(url, "wss", 3) == 0;
  p += 3;
  const char* end = p + strcspn(p, "/?#");
  const char* at = memchr(p, '@', end - p);
  if (at) p = at + 1;

  const char* host_end = end;
  const char* port_start = NULL;
  if (*p == '[') {  // IPv6 literal
    p++;
    host_end = memchr(p, ']', end - p);
    if (!host_end) return false;
    if (host_end + 1 < end && host_end[1] == ':') port_start = host_end + 2;
  } else {
    const char* colon = memchr(p, ':', end - p);
    if (colon) {
      host_end = colon;
      port_start = colon + 1;
    }
  }
  size_t n = host_end - p;
  if (n == 0 || n >= host_len) return false;
  memcpy(host, p, n);
  host[n] = '\0';
  if (port_start && port_start < end && (size_t)(end - port_start) < port_len) {
    memcpy(port, port_start, end - port_start);
    port[end - port_start] = '\0';
  } else {
    strlcpy(port, tls ? "443" : "80", port_len);
  }
  return true;
}

// ------------------------------------------------------------- reachability

// true if a TCP connection to the server can be opened within timeout_ms
static bool probe_direct(int timeout_ms) {
  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
  struct addrinfo* res = NULL;
  if (getaddrinfo(s_probe_host, s_probe_port, &hints, &res) != 0 || !res) {
    return false;
  }
  bool ok = false;
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd >= 0) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    if (rc == 0) {
      ok = true;
    } else if (errno == EINPROGRESS) {
      fd_set wfds;
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      struct timeval tv = {.tv_sec = timeout_ms / 1000,
                           .tv_usec = (timeout_ms % 1000) * 1000};
      if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0) {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        ok = (so_error == 0);
      }
    }
    close(fd);
  }
  freeaddrinfo(res);
  return ok;
}

// ------------------------------------------- lwIP calls (run in tcpip thread)

static esp_err_t op_init(void* arg) {
  return esp_wireguard_init(&s_wg_config, &s_ctx);
}

static esp_err_t op_connect(void* arg) { return esp_wireguard_connect(&s_ctx); }

static esp_err_t op_allow(void* arg) {
  for (int i = 0; i < s_conf.allowed_count; i++) {
    esp_err_t err = esp_wireguard_add_allowed_ip(&s_ctx, s_conf.allowed_ip[i],
                                                 s_conf.allowed_mask[i]);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Could not add allowed IP %s/%s", s_conf.allowed_ip[i],
               s_conf.allowed_mask[i]);
    }
  }
  return ESP_OK;
}

static esp_err_t op_is_up(void* arg) {
  return esp_wireguard_peer_is_up(&s_ctx);
}

static esp_err_t op_disconnect(void* arg) {
  if (s_ctx.netif == NULL) return ESP_OK;
  return esp_wireguard_disconnect(&s_ctx);
}

// -------------------------------------------------------------------- task

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// WireGuard handshakes carry a timestamp that must increase from one
// handshake to the next, so the clock has to be set before connecting.
static void wait_for_clock(int timeout_ms) {
  if (!esp_sntp_enabled()) esp_sntp_init();
  for (int waited = 0; waited < timeout_ms; waited += 250) {
    if (time(NULL) > 1700000000) return;
    delay_ms(250);
  }
  ESP_LOGW(TAG, "Clock not set yet - the handshake may be rejected");
}

// Brings the tunnel up. Returns true once the peer has answered.
static bool tunnel_connect(void) {
  wait_for_clock(15000);

  if (!s_initialized) {
    esp_err_t err = esp_netif_tcpip_exec(op_init, NULL);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
      return false;
    }
    s_initialized = true;
  }

  // connect returns "retry" while the endpoint's name is still being resolved
  esp_err_t err = ESP_FAIL;
  for (int i = 0; i < 20; i++) {
    err = esp_netif_tcpip_exec(op_connect, NULL);
    if (err == ESP_OK) break;
    delay_ms(500);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not start connection to %s:%d (0x%x)", s_conf.endpoint,
             s_conf.port, err);
    esp_netif_tcpip_exec(op_disconnect, NULL);
    return false;
  }
  esp_netif_tcpip_exec(op_allow, NULL);

  for (int i = 0; i < 60; i++) {  // up to 15 s for the handshake
    if (esp_netif_tcpip_exec(op_is_up, NULL) == ESP_OK) {
      ESP_LOGI(TAG, "Tunnel up: %s/%s via %s:%d", s_conf.address,
               s_conf.netmask, s_conf.endpoint, s_conf.port);
      return true;
    }
    delay_ms(250);
  }
  ESP_LOGW(TAG, "No handshake from %s:%d", s_conf.endpoint, s_conf.port);
  esp_netif_tcpip_exec(op_disconnect, NULL);
  return false;
}

// Auto mode without tunnel: how often the direct path is checked again, and
// how many checks in a row have to fail before the tunnel is tried.
#define DIRECT_CHECK_MS 60000
#define DIRECT_MAX_FAILS 3

static void wg_task(void* arg) {
  for (;;) {
    if (!wifi_is_connected()) {
      delay_ms(2000);
      continue;
    }

    if (s_mode == WG_MODE_AUTO && (probe_direct(3000) || probe_direct(3000))) {
      ESP_LOGI(TAG, "Server %s:%s reachable directly - tunnel not needed",
               s_probe_host, s_probe_port);
      s_state = WG_STATE_DIRECT;

      // Keep checking: the network behind the WiFi can change without the
      // device restarting (e.g. a travel router switching its uplink).
      int fails = 0;
      while (fails < DIRECT_MAX_FAILS) {
        delay_ms(DIRECT_CHECK_MS);
        if (!wifi_is_connected()) continue;
        fails = probe_direct(3000) ? 0 : fails + 1;
      }
      ESP_LOGW(TAG,
               "Server %s:%s no longer reachable directly - trying the "
               "tunnel",
               s_probe_host, s_probe_port);
      s_state = WG_STATE_DOWN;
    }

    if (!tunnel_connect()) {
      s_state = WG_STATE_DOWN;
      delay_ms(20000);
      continue;  // probe again, then retry
    }
    s_state = WG_STATE_UP;

    // Keep an eye on the tunnel. The library re-keys on its own; if the peer
    // stays silent (e.g. the endpoint got a new IP address), start over so the
    // endpoint name is resolved again.
    int down_count = 0;
    while (down_count < 4) {
      delay_ms(15000);
      if (esp_netif_tcpip_exec(op_is_up, NULL) == ESP_OK) {
        down_count = 0;
      } else {
        down_count++;
      }
    }
    ESP_LOGW(TAG, "Tunnel lost - reconnecting");
    s_state = WG_STATE_DOWN;
    esp_netif_tcpip_exec(op_disconnect, NULL);
  }
}

// --------------------------------------------------------------------- API

bool wg_is_up(void) { return s_state == WG_STATE_UP; }

bool wg_conf_is_valid(const char* text) {
  if (text == NULL) return false;
  wg_conf_t* c = malloc(sizeof(wg_conf_t));
  if (c == NULL) return false;
  bool ok = parse_conf(text, c);
  free(c);
  return ok;
}

void wg_start(const char* image_url, int wait_ms) {
  static bool started = false;
  if (started) return;

  s_mode = nvs_get_wg_mode();
  const char* text = nvs_get_wg_conf();
  if (s_mode == WG_MODE_OFF || text == NULL || text[0] == '\0') {
    ESP_LOGI(TAG, "WireGuard not configured or switched off");
    return;
  }
  if (!parse_conf(text, &s_conf)) {
    ESP_LOGE(TAG, "WireGuard configuration is incomplete or invalid");
    return;
  }
  if (s_conf.allowed_ignored > 0) {
    ESP_LOGW(TAG,
             "Only the first %d IPv4 AllowedIPs entries are used, %d "
             "ignored",
             WG_MAX_ALLOWED, s_conf.allowed_ignored);
  }
  if (s_mode == WG_MODE_AUTO &&
      !parse_url_host(image_url, s_probe_host, sizeof(s_probe_host),
                      s_probe_port, sizeof(s_probe_port))) {
    ESP_LOGW(TAG, "Cannot test reachability of %s - using the tunnel",
             image_url);
    s_mode = WG_MODE_ALWAYS;
  }

  s_wg_config.private_key = s_conf.private_key;
  s_wg_config.public_key = s_conf.public_key;
  s_wg_config.preshared_key =
      s_conf.preshared_key[0] ? s_conf.preshared_key : NULL;
  s_wg_config.address = s_conf.address;
  s_wg_config.netmask = s_conf.netmask;
  s_wg_config.endpoint = s_conf.endpoint;
  s_wg_config.port = (uint16_t)s_conf.port;
  s_wg_config.persistent_keepalive = (uint16_t)s_conf.keepalive;

  ESP_LOGI(TAG, "WireGuard mode %s, endpoint %s:%d, address %s/%s",
           s_mode == WG_MODE_AUTO ? "auto" : "always", s_conf.endpoint,
           s_conf.port, s_conf.address, s_conf.netmask);

  if (xTaskCreate(wg_task, "wg", 6144, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Could not create WireGuard task");
    return;
  }
  started = true;

  for (int waited = 0; waited < wait_ms; waited += 100) {
    if (s_state == WG_STATE_DIRECT || s_state == WG_STATE_UP) break;
    delay_ms(100);
  }
}

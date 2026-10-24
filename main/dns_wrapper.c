#include <arpa/inet.h>
#include <esp_log.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "lwip/netdb.h"

static const char *TAG = "DNS_WRAPPER";

// Declare the real function (which will be esp_getaddrinfo)
int __real_esp_getaddrinfo(const char *nodename, const char *servname,
                           const struct addrinfo *hints, struct addrinfo **res);

int __wrap_esp_getaddrinfo(const char *nodename, const char *servname,
                           const struct addrinfo *hints,
                           struct addrinfo **res) {
  if (nodename != NULL) {
    struct in_addr ipv4;
    struct in6_addr ipv6;
    if (inet_pton(AF_INET, nodename, &ipv4) == 1 ||
        inet_pton(AF_INET6, nodename, &ipv6) == 1) {
      struct addrinfo numeric_hints = {.ai_family = AF_UNSPEC};
      if (hints != NULL) {
        numeric_hints = *hints;
      }
      numeric_hints.ai_flags |= AI_NUMERICHOST;
      return lwip_getaddrinfo(nodename, servname, &numeric_hints, res);
    }

    const char *mdns_suffix = ".local";
    const size_t mdns_suffix_len = strlen(mdns_suffix);
    size_t len = strlen(nodename);
    // Check for .local suffix for mDNS
    if (len >= mdns_suffix_len &&
        strcasecmp(nodename + len - mdns_suffix_len, mdns_suffix) == 0) {
      ESP_LOGD(TAG, "Redirecting mDNS query for %s to lwip_getaddrinfo",
               nodename);
      return lwip_getaddrinfo(nodename, servname, hints, res);
    }
  }
  // Fallback to the standard ESP implementation (which handles IPv6/AF_UNSPEC
  // better)
  return __real_esp_getaddrinfo(nodename, servname, hints, res);
}

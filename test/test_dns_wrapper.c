#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
static int esp_calls;
static int lwip_calls;
static struct addrinfo received_hints;
int lwip_getaddrinfo(const char *node, const char *service,
                     const struct addrinfo *hints, struct addrinfo **res) {
  ++lwip_calls;
  received_hints = hints ? *hints : (struct addrinfo){0};
  if (node && (strcasecmp(node, "panel.local") == 0)) return EAI_NONAME;
  assert(hints != NULL);
  assert((hints->ai_flags & AI_NUMERICHOST) != 0);
  return getaddrinfo(node, service, hints, res);
}
int __real_esp_getaddrinfo(const char *node, const char *service,
                           const struct addrinfo *hints,
                           struct addrinfo **res) {
  (void)node;
  (void)service;
  (void)hints;
  (void)res;
  ++esp_calls;
  return EAI_AGAIN;
}
#include "../main/dns_wrapper.c"
int main(void) {
  struct addrinfo hints = {.ai_flags = AI_NUMERICSERV,
                           .ai_family = AF_UNSPEC,
                           .ai_socktype = SOCK_STREAM,
                           .ai_protocol = IPPROTO_TCP};
  unsigned char original_hints[sizeof(hints)];
  memcpy(original_hints, &hints, sizeof(hints));
  struct addrinfo *res = NULL;
  assert(__wrap_esp_getaddrinfo("192.0.2.1", "8080", &hints, &res) == 0);
  assert(esp_calls == 0 && lwip_calls == 1);
  assert(memcmp(original_hints, &hints, sizeof(hints)) == 0);
  assert(received_hints.ai_family == hints.ai_family);
  assert(received_hints.ai_socktype == hints.ai_socktype);
  assert(received_hints.ai_protocol == hints.ai_protocol);
  assert(received_hints.ai_flags == (AI_NUMERICSERV | AI_NUMERICHOST));
  assert(res->ai_family == AF_INET);
  struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
  assert(ntohs(addr->sin_port) == 8080);
  assert(ntohl(addr->sin_addr.s_addr) == 0xc0000201);
  freeaddrinfo(res);
  hints.ai_family = AF_INET;
  assert(__wrap_esp_getaddrinfo("192.0.2.1", NULL, &hints, &res) == 0);
  freeaddrinfo(res);
  hints.ai_family = AF_UNSPEC;
  assert(__wrap_esp_getaddrinfo("::1", "443", &hints, &res) == 0);
  assert(res->ai_family == AF_INET6);
  freeaddrinfo(res);
  assert(__wrap_esp_getaddrinfo("192.0.2.1", NULL, NULL, &res) == 0);
  assert(received_hints.ai_family == AF_UNSPEC);
  assert(received_hints.ai_flags == AI_NUMERICHOST);
  assert(received_hints.ai_socktype == 0);
  freeaddrinfo(res);
  hints.ai_family = AF_INET6;
  assert(__wrap_esp_getaddrinfo("192.0.2.1", NULL, &hints, &res) != 0);
  assert(received_hints.ai_family == AF_INET6);
  assert(hints.ai_flags == AI_NUMERICSERV);
  hints.ai_family = AF_INET;
  assert(__wrap_esp_getaddrinfo("::1", NULL, &hints, &res) != 0);
  assert(received_hints.ai_family == AF_INET);
  assert(hints.ai_flags == AI_NUMERICSERV);
  hints.ai_family = AF_INET6;
  assert(__wrap_esp_getaddrinfo("::1", NULL, &hints, &res) == 0);
  assert(res->ai_family == AF_INET6);
  freeaddrinfo(res);
  assert(esp_calls == 0);
  assert(__wrap_esp_getaddrinfo("panel.LoCaL", NULL, &hints, &res) ==
         EAI_NONAME);
  assert(esp_calls == 0 && lwip_calls == 8);
  assert(received_hints.ai_flags == AI_NUMERICSERV);
  assert(__wrap_esp_getaddrinfo("example.com", "443", &hints, &res) ==
         EAI_AGAIN);
  assert(__wrap_esp_getaddrinfo("192.0.2.1.example.com", NULL, &hints, &res) ==
         EAI_AGAIN);
  assert(__wrap_esp_getaddrinfo(NULL, NULL, NULL, &res) == EAI_AGAIN);
  assert(esp_calls == 3 && lwip_calls == 8);
  puts(
      "PASS: numeric IPv4/IPv6, numeric-host flags, preserved hints, "
      "family mismatches, mDNS and normal DNS routing");
}

#pragma once

#include <esp_err.h>
#include <stddef.h>

typedef struct {
  int status;  // HTTP status code, 0 if the request never completed
  char *body;  // NUL-terminated, in PSRAM; NULL if empty
  size_t len;
} http_response_t;

// Perform an HTTP(S) request and collect the whole response body (up to
// max_len bytes) in PSRAM. `headers` is a NULL-terminated list of
// "Name", "Value" pairs and may be NULL. `body` may be NULL for GET.
esp_err_t http_request(const char *method, const char *url,
                       const char *const *headers, const char *body,
                       size_t max_len, http_response_t *out);

void http_response_free(http_response_t *resp);

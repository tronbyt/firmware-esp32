#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

/**
 * @brief Function type for registering HTTP URI handlers on a server.
 *
 * Registrar functions are stored and re-invoked whenever the server
 * is (re)started, so that all routes are always present.
 */
typedef void (*http_handler_registrar_fn)(httpd_handle_t server);

/**
 * @brief Register WiFi event handlers so the HTTP server auto-starts
 *        when a STA IP is obtained.
 *
 * Call once during startup, before any WiFi connection is established.
 */
void http_server_init(void);

/**
 * @brief Start the HTTP server (no-op if already running).
 *
 * All previously registered handler registrars are invoked so that
 * their routes are present on the new server instance.
 */
void http_server_start(void);

/**
 * @brief Stop the HTTP server.
 */
void http_server_stop(void);

/**
 * @brief Get the current httpd handle (NULL if not running).
 */
httpd_handle_t http_server_handle(void);

/**
 * @brief Store a registrar callback.
 *
 * The callback is invoked immediately if the server is already running,
 * and again on every future (re)start.
 */
void http_server_register_handlers(http_handler_registrar_fn registrar);

#pragma once

#include <esp_err.h>
#include <stdbool.h>

/**
 * @brief Initialize Syslog logging
 *
 * Sets up the UDP socket and registers the logging redirect.
 *
 * @param addr Address of the syslog server (format "host:port" or "host")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t syslog_init(const char *addr);

/**
 * @brief Deinitialize Syslog logging
 *
 * Restores original logging and closes socket.
 */
void syslog_deinit(void);

/**
 * @brief Update Syslog configuration
 *
 * @param addr New address
 */
void syslog_update_config(const char *addr);

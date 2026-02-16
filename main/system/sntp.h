#pragma once

/**
 * @brief Configure SNTP (Pre-Network)
 *
 * Configures SNTP server modes (NVS, DHCP, Fallback).
 * Must be called before WiFi/DHCP start to ensure DHCP options are handled.
 */
void app_sntp_config(void);

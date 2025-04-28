#pragma once

int wifi_initialize(const char *ssid, const char *password);

void wifi_shutdown();

int wifi_get_mac(uint8_t mac[6]);

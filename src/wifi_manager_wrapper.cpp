#include "wifi_manager_wrapper.h"
#include "wifi.h"
#include <stdio.h>

// Flag to indicate if WiFi is connected
static bool wifi_connected = false;

void wifi_manager_init(void) {
    // Use hardcoded credentials for now
    // In a real application, these would be stored in NVS or provided by the user
    const char* ssid = "your_wifi_ssid";
    const char* password = "your_wifi_password";

    printf("Initializing WiFi with SSID: %s\n", ssid);

    // Initialize WiFi
    int result = wifi_initialize(ssid, password);
    if (result == 0) {
        wifi_connected = true;
        printf("WiFi connected successfully\n");
    } else {
        wifi_connected = false;
        printf("WiFi connection failed\n");
    }
}

bool wifi_manager_is_connected(void) {
    return wifi_connected;
}

void wifi_manager_get_mac(uint8_t* mac) {
    wifi_get_mac(mac);
}

void wifi_manager_start_ap(void) {
    // This is a simplified version that doesn't actually start an AP
    // In a real application, this would start an AP and a web server
    printf("AP mode not implemented in this simplified version\n");
}

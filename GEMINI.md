# Project Overview

This repository contains community-supported firmware for the Tidbyt hardware, designed to be built with PlatformIO and the ESP-IDF framework. The firmware enables the device to display WebP images fetched from a URL or received via a WebSocket connection. It supports different Tidbyt hardware generations (Gen1, Gen2, S3) and other ESP32-based matrix displays.

The firmware can be configured via a `secrets.json` file to connect to a WiFi network and a specified URL for image data. It also provides a WiFi configuration portal for easy setup.

# Building and Running

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)

## Configuration

1.  Copy `secrets.json.example` to `secrets.json`.
2.  Edit `secrets.json` with your WiFi credentials and the URL for the image data.

```json
{
    "WIFI_SSID": "myssiD",
    "WIFI_PASSWORD": "<PASSWORD>",
    "REMOTE_URL": "http://homeServer.local:8000/admin/tronbyt_1/next",
    "REFRESH_INTERVAL_SECONDS": 10,
    "DEFAULT_BRIGHTNESS" : 30
}
```

## Building

To build the firmware, use the `pio run` command with the appropriate environment for your hardware.

-   **Tidbyt Gen1:**
    ```bash
    pio run --environment tidbyt-gen1
    ```
-   **Tidbyt Gen2:**
    ```bash
    pio run --environment tidbyt-gen2
    ```

## Flashing

To flash the firmware to the device, use the `--target upload` flag.

-   **Tidbyt Gen1:**
    ```bash
    pio run --environment tidbyt-gen1 --target erase --target upload
    ```
-   **Tidbyt Gen2:**
    ```bash
    pio run --environment tidbyt-gen2 --target erase --target upload
    ```

## Monitoring

To monitor the device's logs, use the `pio device monitor` command:

```bash
pio device monitor
```

# Development Conventions

-   The project uses PlatformIO for building, flashing, and monitoring.
-   Dependencies are managed in `platformio.ini` and are fetched from GitHub.
-   The firmware is written in C/C++ and uses the ESP-IDF framework.
-   Configuration is managed through a `secrets.json` file.
-   Different hardware configurations are managed through PlatformIO environments.

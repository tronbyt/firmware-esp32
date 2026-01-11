# Project Overview

This repository contains community-supported firmware for the Tidbyt hardware, designed to be built with native ESP-IDF. The firmware enables the device to display WebP images fetched from a URL or received via a WebSocket connection. It supports different Tidbyt hardware generations (Gen1, Gen2, S3) and other ESP32-based matrix displays.

The firmware can be configured via a `secrets.json` file or via Kconfig (`idf.py menuconfig`). It also provides a WiFi configuration portal for easy setup.

# Building and Running

## Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) (Native)
- [Python 3](https://www.python.org/)

## Configuration

1.  **Clone the repository**:
    ```bash
    git clone <repo-url>
    ```

2.  **Secrets**:
    Copy `secrets.json.example` to `secrets.json`. Edit it with your WiFi credentials and the URL for the image data. Values in `secrets.json` will automatically override Kconfig settings at build time.

```json
{
    "WIFI_SSID": "myssiD",
    "WIFI_PASSWORD": "<PASSWORD>",
    "REMOTE_URL": "http://homeServer.local:8000/admin/tronbyt_1/next",
}
```

3.  **Kconfig**:
    Use `idf.py menuconfig` to configure hardware types, boot animations, and other system settings under the "Tronbyt Configuration" menu.

## Building for Specific Hardware

To build for a specific board, use the provided `sdkconfig.defaults.<board>` files or the convenience Makefile:

-   **Using Makefile**:
    ```bash
    make tidbyt-gen1
    make tidbyt-gen2
    make tronbyt-s3
    ```

-   **Using idf.py directly**:
    ```bash
    idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tidbyt-gen1" build
    ```

## Flashing and Monitoring

```bash
idf.py flash monitor
```

# Development Conventions

-   The project uses native ESP-IDF for building, flashing, and monitoring.
-   Dependencies are managed via the `idf_component.yml` file.
-   Configuration is managed through `secrets.json` (overrides) and Kconfig.
-   Different hardware configurations are managed through board-specific `sdkconfig.defaults` files.

# Configuration Strategy

The project employs a two-tiered configuration system:

## 1. Build-Time Configuration (Kconfig / `sdkconfig`)
Used for:
- **System Settings:** Stack sizes, compiler optimization, log levels.
- **Hardware Definition:** Pin assignments (`CONFIG_BUTTON_PIN`), board-specific hardware flags.
- **Feature Flags:** Enabling/disabling core modules (e.g., `ENABLE_AP_MODE` logic via preprocessor defines).
- **Network Defaults:** LwIP tuning (IPv6 support, buffer sizes).

These settings are defined in `Kconfig.projbuild` and `sdkconfig.defaults` (per-environment). They compile into the firmware binary.

## 2. Secrets & Runtime Configuration (`secrets.json`)
Used for:
- **Credentials:** WiFi SSID and Password.
- **Deployment Settings:** Target Image URL (`REMOTE_URL`).
- **Application Defaults:** Default brightness, refresh interval.

**Mechanism:**
- `secrets.json` is parsed by `generate_secrets_cmake.py` during the build process.
- Values are injected as compiler definitions (macros) via `secrets.cmake`.
- **Runtime Override:** The firmware also checks Non-Volatile Storage (NVS) for these values (`ssid`, `password`, `image_url`). If the device is configured via the WiFi Captive Portal, the NVS values take precedence over the hardcoded `secrets.json` defaults.

**Best Practice:**
- Use **Kconfig** for structural changes that affect the binary code or memory layout.
- Use **secrets.json** for parameters that vary between deployments or need to be user-configurable without recompiling (via NVS fallback).
- The firmware is written in C/C++.
- The code is formatted using the rules in .clang-fornat.

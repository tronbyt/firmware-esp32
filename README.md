# Tronbyt Firmware

[![Discord Server](https://img.shields.io/discord/928484660785336380?style=flat-square)](https://discord.gg/nKDErHGmU7)

This repository contains a community supported firmware for the Tidbyt hardware 🤓.

## Warning

⚠️ Warning! Flashing your Tidbyt with this firmware or derivatives could fatally
damage your device. As such, flashing your Tidbyt with this firmware or
derivatives voids your warranty and comes without support.

## Getting Started

Follow the setup instructions for [tronbyt-server][3] unless you want to build yourself with ESP-IDF.

## Building yourself with ESP-IDF

Only follow these instructions if you want to build the firmware yourself. Otherwise let the [tronbyt-server][3] generate the firmware file for you.
This project uses the native [ESP-IDF][2] framework to build, flash, and monitor firmware.

Additionally, this firmware is designed to work with https://github.com/tronbyt/server or
you can point this firmware at any URL that hosts a WebP image that is optimized for the Tidbyt display.

### Configuration

To flash the custom firmware on your device, follow these steps:

1. Copy `secrets.json.example` to `secrets.json`.
2. Edit `secrets.json` with your information. If using tronbyt_manager in Docker, use the Docker host's IP address.

Example `secrets.json`:
```json
{
    "WIFI_SSID": "myssid",
    "WIFI_PASSWORD": "<PASSWORD>",
    "REMOTE_URL": "http://homeServer.local:8000/tronbyt_1/next",
}
```

### Build and Flash

Use the provided `Makefile` for convenience to build for specific hardware:

```bash
# For Tidbyt Gen 1
make tidbyt-gen1

# For Tidbyt Gen 2
make tidbyt-gen2

# For Tronbyt S3
make tronbyt-s3
```

To flash the built firmware to your device:

```bash
idf.py flash
```

## Monitoring Logs

To check the output of your running firmware, run the following:

```bash
idf.py monitor
```

## Advanced Settings

The firmware supports several advanced settings stored in Non-Volatile Storage (NVS). These can be configured via the WebSocket connection or by using `idf.py menuconfig` (which sets the build-time defaults).

| Setting | NVS Key | Description |
| :--- | :--- | :--- |
| **Hostname** | `hostname` | The network hostname of the device. Defaults to `tronbyt-<mac>`. |
| **Syslog Address** | `syslog_addr` | Remote Syslog (RFC 5424) server in `host:port` format (e.g., `192.168.1.10:1517`). |
| **SNTP Server** | `sntp_server` | Custom NTP server for time synchronization. Defaults to DHCP provided servers or `pool.ntp.org`. |
| **Swap Colors** | `swap_colors` | Boolean (0/1) to swap RGB color order. Useful for specific panel variants. |
| **AP Mode** | `ap_mode` | Boolean (0/1) to enable/disable the fallback WiFi configuration portal. |
| **WiFi Power Save**| `wifi_ps` | WiFi power management mode (0: None, 1: Min, 2: Max). |
| **Prefer IPv6** | `prefer_ipv6` | Boolean (0/1) to prefer IPv6 connectivity over IPv4. |

## Back to Normal

### Using Web Flasher (Recommended)

The easiest way to restore your Tidbyt to factory firmware is using the web flasher with the pre-built merged binary files:

1. Download the appropriate merged binary file:
   - **Gen 1**: [gen1_merged.bin](https://github.com/tronbyt/firmware-esp32/raw/main/reset/gen1_merged.bin)
   - **Gen 2**: [gen2_merged.bin](https://github.com/tronbyt/firmware-esp32/raw/main/reset/gen2_merged.bin)
2. Visit [https://espressif.github.io/esptool-js/](https://espressif.github.io/esptool-js/) (requires Chrome or Edge browser)
3. Connect your Tidbyt via USB
4. Use the following settings:
   - **Flash Address**: `0x0`
   - **File**: Select the downloaded merged binary file

![Web Flasher Settings](docs/assets/web_flasher_settings.png)

4. Click "Program" to flash the factory firmware

### Using the WiFi config portal

The firmware has a rudimentary wifi config portal page that can be accessed by joining the TRONBYT-CONFIG network and navigating to http://10.10.0.1. 

[WiFi Config Portal How-To Video](https://www.youtube.com/watch?v=OAWUCG-HRDs)

## Troubleshooting

### OTA Update Fails with "Validation Failed" or "Checksum Error"

If you are seeing errors like `ESP_ERR_OTA_VALIDATE_FAILED` or checksum mismatches in the logs, especially on Gen 1 devices previously used with ESPHome or stock firmware:

1.  **Partition Table Mismatch:** You likely have an old or incompatible partition table on your device. This happens if you flashed only the `firmware.bin` instead of the full `merged.bin` during the initial install.

2.  **Solution:** You must perform a **clean install**.

    *   Use the **Web Flasher** method described in "Back to Normal.
    *   Ensure you select the **merged binary** (`gen1_merged.bin`).
    *   Ideally, use the "Erase Flash" option in the flasher tool before programming to ensure a clean slate.


[1]: https://github.com/tidbyt/pixlet
[2]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html
[3]: https://github.com/tronbyt/server

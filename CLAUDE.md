# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Tronbyt is a community-supported firmware for Tidbyt hardware (LED matrix displays). This ESP32-based firmware fetches and displays WebP images from a remote server over WiFi. The firmware supports multiple hardware configurations including Tidbyt Gen1, Gen2, and custom boards like Tronbyt-S3 and Pixoticker.

## Build System

This project uses **PlatformIO** with the ESP-IDF framework.

### Build Commands

```bash
# Important !!!! When building/testing and monitoring build with the the following command and read the serial output immediately.
platformio run --target upload --target monitor --environment tidbyt-gen1

# Build for specific hardware
pio run --environment tidbyt-gen1 # use this target as the default testing target
pio run --environment tidbyt-gen2
pio run --environment tronbyt-s3
pio run --environment pixoticker

# Flash firmware (erases device first, then uploads)
pio run --environment tidbyt-gen1 --target erase --target upload

# Monitor serial output
pio device monitor
```

### Available Environments

- `tidbyt-gen1` / `tidbyt-gen2` - Original Tidbyt hardware (Gen 1 & 2)
- `tidbyt-gen1_swap` - Gen1 with swapped colors for different display variants
- `tidbyt-gen1-patched` / `tidbyt-gen2-patched` - Use 8Hz instead of 10Hz (legacy clock division for displays with color artifacts)
- `tronbyt-s3` - Custom Tronbyt board with ESP32-S3
- `tronbyt-s3-wide` - S3 board with wider display configuration
- `pixoticker` - 4MB flash variant, no PSRAM
- `matrixportal-s3` - Adafruit Matrix Portal S3
- `matrixportal-s3-waveshare` - Matrix Portal S3 with Waveshare displays (swapped colors)

## Configuration & Secrets

The firmware requires WiFi credentials and a remote server URL. Configuration uses `secrets.json`:

1. Copy `secrets.json.example` to `secrets.json`
2. Edit with your WiFi SSID, password, and server URL
3. Build and flash

**Important**: If you modify `secrets.json` after initial flash, you MUST erase the device first:
```bash
pio run -e <your-env> -t erase
```

The build system (in `extra_scripts/pre.py`) handles three scenarios:
- `secrets.json` exists → uses it for deployment
- Only `secrets.json.injected` exists → uses placeholders (manual WiFi config needed)
- Neither exists → uses placeholders from `secrets_place.json`

### WiFi Configuration Portal

The firmware provides a captive portal for WiFi configuration:
1. Connect to `TRON-CONFIG` network
2. Navigate to `http://10.10.0.1`
3. Enter WiFi credentials and image URL

## Code Architecture

### Core Components

**Main Loop** (`src/main.c`)
- Entry point with FreeRTOS task management
- Handles WebSocket and HTTP image fetching modes
- Manages display refresh timing and brightness
- Sends client info (firmware version, MAC address) to server via WebSocket or HTTP headers

**Network Layer**
- `src/wifi.c` - WiFi connection management, captive portal web server, NVS storage
- `src/remote.c` - HTTP client for fetching WebP images from remote server
  - Handles custom headers: `Tronbyt-Brightness`, `Tronbyt-Dwell-Secs`, `X-Tronbyt-Client-Info`
  - Implements dynamic buffer allocation with configurable max sizes
  - Detects oversized content and displays error graphic

**Display System**
- `src/display.cpp` - Hardware abstraction for HUB75 LED matrix panels (uses ESP32-HUB75-MatrixPanel-DMA library)
- `src/gfx.c` - Graphics rendering, WebP decoding, and embedded asset management
- Pin configurations defined per-hardware in `display.cpp` using conditional compilation

**Assets** (`lib/assets/`)
- Boot images and error graphics stored as embedded `.webp.h` files
- Converted using `extra_scripts/webp_to_c.h.py`

### Build Flags & Hardware Variants

Key preprocessor flags control hardware-specific behavior:
- `TIDBYT_GEN2` - Tidbyt Gen 2 hardware
- `TRONBYT_S3` / `TRONBYT_S3_WIDE` - Custom S3 boards
- `PIXOTICKER` - Smaller memory variant
- `SWAP_COLORS` - Swap R/B channels for certain displays
- `NO_INVERT_CLOCK_PHASE` - Clock phase control for S3 variants
- `HTTP_BUFFER_SIZE_MAX` / `HTTP_BUFFER_SIZE_DEFAULT` - Memory limits (varies by board)

### WebSocket Protocol

When `use_websocket` is enabled:
- Client sends JSON with firmware version and MAC address on connect
- Server can send WebP images directly via binary messages
- Server sends dwell time and brightness as text messages in format: `{"dwell_secs": N, "brightness_pct": N}`
- Oversized messages (> buffer max) trigger error display

### Extra Scripts

- `pre.py` - Loads secrets, sets build flags, deletes stale `sdkconfig`
- `post.py` - Post-build processing (firmware tagging, version injection)
- `patch_i2s_divider.py` - Patches HUB75 library for 8Hz display variants
- `reset.py` - Factory firmware restore functionality

## Development Workflow

1. **Making changes**: Edit source files in `src/`
2. **Testing**: Build for your target environment and monitor serial output
3. **Display issues**: Try `-patched` variants for 8Hz if seeing color artifacts
4. **New assets**: Convert WebP to C header with `extra_scripts/webp_to_c.h.py`

## Hardware Pin Mappings

Pin configurations are in `src/display.cpp` with conditional compilation blocks for each board variant. Each defines R1/G1/BL1, R2/G2/BL2 (RGB channels for upper/lower half), CH_A through CH_E (address lines), LAT (latch), OE (output enable), and CLK (clock).

## Git Workflow

- Main branch: `main`
- Current working branch: `send_client_info`
- Recent work involves WebSocket bidirectional communication and client info transmission

# Migration from PlatformIO to Native ESP-IDF

This project has been migrated to use native ESP-IDF.

## Workflow Mapping

| Task | PlatformIO Command | Native ESP-IDF Command |
| :--- | :--- | :--- |
| **Build** | `pio run` | `idf.py build` |
| **Flash** | `pio run -t upload` | `idf.py flash` |
| **Monitor** | `pio run -t monitor` | `idf.py monitor` |
| **Config** | (Edit `platformio.ini` / `secrets.json`) | `idf.py menuconfig` |
| **Clean** | `pio run -t clean` | `idf.py fullclean` |

## Getting Started

1.  **Clone with submodules**:
    ```bash
    git clone --recursive <repo-url>
    # OR if already cloned:
    git submodule update --init --recursive
    ```

2.  **Build for a specific board**:
    To mimic PlatformIO environments, use the provided `sdkconfig.defaults.<board>` files or the convenience Makefile:
    ```bash
    # Using Makefile
    make tidbyt-gen1
    
    # OR using idf.py directly
    idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.tidbyt-gen1" build
    ```

## Configuration & Secrets

### Kconfig
Project configuration is now handled via Kconfig. Use `idf.py menuconfig` to configure:
*   **Tronbyt Configuration**: WiFi credentials, Remote URL, Refresh Interval, Brightness, etc.
*   **Hardware Configuration**: Board Type, Boot Animation, optimization flags.

### secrets.json
The `secrets.json` injection is still supported. If `secrets.json` exists in the project root, its values will **automatically override** any values set in Kconfig/sdkconfig during the build process. This is handled by `generate_secrets_cmake.py` which is called automatically by CMake.

## Directory Structure

*   `main/`: Main application source code.
*   `components/`: Local components:
    *   `assets`: Built-in WebP images.
    *   `libwebp`: Wrapper for external libwebp submodule.
    *   `ESP32-HUB75-MatrixPanel-DMA`: HUB75 submodule.
*   `external/`: Git submodules.
*   `sdkconfig.defaults.*`: Default configuration fragments for different hardware.

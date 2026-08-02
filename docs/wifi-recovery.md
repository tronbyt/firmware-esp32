# Wi-Fi and HTTP recovery

Firmware logs the reset reason, whether saved credentials exist, disconnect reason/category, retry attempt and capped delay, portal-entry reason, connection recovery, HTTP status, sanitized frame hash/size, and queue state. It never logs passwords, API keys, SSIDs, authenticated Image URLs, OTA URLs, or private replacement URLs.

Saved Wi-Fi retries continue while the setup portal remains available. Backoff grows from 1 to 30 seconds and resets after an IP is acquired. The portal distinguishes no credentials, manual entry, and startup timeout; disconnect diagnostics distinguish router unavailability from authentication rejection. A successful saved-network connection exits recovery automatically without requiring the credentials to be entered again.

HTTP 401/403 is reported as authentication rejection, not generic Wi-Fi failure. Physical firmware must use a device-scoped key in its complete authenticated Image URL. If that key changes, update the board's saved URL.

## Build and physical verification

MatrixPortal S3 verification is pinned to ESP-IDF 5.5.2. The dedicated
`Verify MatrixPortal S3` workflow configures `esp32s3` with
`sdkconfig.defaults` plus `sdkconfig.defaults.matrixportal-s3`, builds the ELF
and binaries, records the partition table, size report, checksums, complete
compiler log, and uploads them as a 14-day artifact. It is the canonical
reproducible build when ESP-IDF is not installed locally.

For MatrixPortal S3, with ESP-IDF 5.5.2 installed:

```sh
make matrixportal-s3
```

The target is `esp32s3`; board defaults are `sdkconfig.defaults.matrixportal-s3`. The current managed components are `esp_websocket_client` 1.6.0, `esp32-hub75-matrixpanel-dma` 3.0.14, and the Tronbyt `libwebp` fork. Equivalent explicit commands after sourcing the ESP-IDF export script are:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.matrixportal-s3" set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemPORT flash
idf.py -p /dev/cu.usbmodemPORT monitor
```

The same compile can be reproduced without installing ESP-IDF when Docker is
available:

```sh
docker run --rm \
  -v "$PWD:/project" \
  -w /project \
  espressif/idf:v5.5.2 \
  bash -lc '. "$IDF_PATH/export.sh" && idf.py -B build-matrixportal-s3 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.matrixportal-s3" set-target esp32s3 && idf.py -B build-matrixportal-s3 build'
```

Replace the serial port with the board's actual `/dev/cu.*` device. `make flash` and `make monitor` wrap the final two commands after a MatrixPortal S3 build has configured the target.

USB validation must be done on the physical board; it was not performed by the code-only test pass. Back up the current firmware/configuration, flash over USB, and monitor serial output. Verify:

1. Normal boot, frame polling, and 15-second cadence.
2. Correct authenticated URL without printing it.
3. Power on the board before the router; confirm the portal stays reachable and the saved network connects automatically when the router returns.
4. Test an intentionally wrong password and distinguish `authentication_rejected` from `router_unavailable`.
5. Confirm HTTP 401 is distinct from transport loss.
6. Confirm successful frame hashes change with content and no credentials appear in serial output.

Rollback by reflashing the backed-up firmware image and restoring the prior NVS configuration. Do not rotate keys as part of rollback; if a key was independently changed, restore the board's matching authenticated Image URL.

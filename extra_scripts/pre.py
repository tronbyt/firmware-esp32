#!/usr/bin/env python3

import os
import json

Import("env")


def load_secrets_config():
    """
    Load secrets configuration based on available files.
    Handles three scenarios:
    1. secrets.json exists - use it (fresh deployment)
    2. secrets.json.injected exists but secrets.json doesn't - inform user
    3. Neither exists - use placeholder values
    """
    secrets_file = "secrets.json"
    injected_file = "secrets.json.injected"
    placeholders_file = "secrets_place.json"

    secrets_exist = os.path.exists(secrets_file)
    injected_exist = os.path.exists(injected_file)

    if secrets_exist:
        # Scenario 1: secrets.json exists - use it for fresh deployment
        print("=" * 60)
        print("📄 Found secrets.json - Loading configuration for deployment")
        if injected_exist:
            print("⚠️  WARNING: Both secrets.json and secrets.json.injected exist!")
            print("   This suggests you may be re-deploying with new secrets.")
            print("   If you modified secrets, you MUST erase the device first:")
            print("   pio run -e <your-env> -t erase")
        print("=" * 60)

        try:
            with open(secrets_file, "r") as f:
                json_config = json.load(f)
                return {
                    "wifi_ssid": json_config.get("WIFI_SSID", ""),
                    "wifi_password": json_config.get("WIFI_PASSWORD", ""),
                    "remote_url": json_config.get("REMOTE_URL", ""),
                    "refresh_interval_seconds": json_config.get("REFRESH_INTERVAL_SECONDS", 10),
                    "default_brightness": json_config.get("DEFAULT_BRIGHTNESS", 10),
                    "source": "secrets.json"
                }
        except (json.JSONDecodeError, IOError) as e:
            print(f"❌ Error reading secrets.json: {e}")
            print("   Build failed - cannot proceed without valid secrets configuration.")
            exit(1)

    elif injected_exist:
        # Scenario 2: Only secrets.json.injected exists - use placeholders
        print("=" * 60)
        print("📋 Found secrets.json.injected but no secrets.json")
        print("   The injected file indicates secrets were previously deployed.")
        print("")
        print("   To deploy with NEW secrets:")
        print("   1. Copy: cp secrets.json.injected secrets.json")
        print("   2. Edit secrets.json and fix any syntax errors")
        print("   3. If you modified secrets, run: pio run -e <your-env> -t erase")
        print("   4. Run: pio run -e <your-env> --target upload")
        print("=" * 60)

    else:
        # Scenario 3: Neither file exists - first time setup
        print("=" * 60)
        print("🆕 No secrets files found - First time setup")
        print("   Using PLACEHOLDER values for firmware compilation.")
        print("")
        print("   To deploy with real secrets:")
        print("   1. Copy: cp secrets.json.example secrets.json")
        print("   2. Edit secrets.json with your actual values")
        print("   3. Run: pio run -e <your-env> --target upload")
        print("=" * 60)

        # Return placeholder values for scenarios 2 and 3
        try:
            with open(placeholders_file, "r") as f:
                json_config = json.load(f)
                return {
                    "wifi_ssid": json_config.get("WIFI_SSID", ""),
                    "wifi_password": json_config.get("WIFI_PASSWORD", ""),
                    "remote_url": json_config.get("REMOTE_URL", ""),
                    "refresh_interval_seconds": json_config.get(
                        "REFRESH_INTERVAL_SECONDS", 10
                    ),
                    "default_brightness": json_config.get("DEFAULT_BRIGHTNESS", 10),
                    "source": "secrets.json",
                }
        except (json.JSONDecodeError, IOError) as e:
            print(f"❌ Error reading secrets_place.json: {e}")
            print(
                "   Build failed - cannot proceed without valid secrets configuration."
            )
            exit(1)


def main() -> None:
    # copy libwebp's library.json to the lib directory
    env.Execute(Copy("$PROJECT_LIBDEPS_DIR/$PIOENV/libwebp/library.json", "$PROJECT_DIR/lib/webp/library.json"))

    sdkconfig_path = os.path.join(env["PROJECT_DIR"], "sdkconfig")
    if os.path.exists(sdkconfig_path):
        print(f"Deleting existing {sdkconfig_path} to force regeneration...")
        os.remove(sdkconfig_path)

    # Load secrets configuration based on available files
    config = load_secrets_config()

    # Apply configuration to build flags
    env.Append(
        CCFLAGS=[
            f"-DWIFI_SSID={env.StringifyMacro(config['wifi_ssid'])}",
            f"-DWIFI_PASSWORD={env.StringifyMacro(config['wifi_password'])}",
            f"-DREMOTE_URL={env.StringifyMacro(config['remote_url'])}",
            f"-DREFRESH_INTERVAL_SECONDS={config['refresh_interval_seconds']}",
            f"-DDEFAULT_BRIGHTNESS={config['default_brightness']}",
        ],
    )

    # Print final configuration summary
    print(f"🔧 Build configuration loaded from: {config['source']}")
    if config['source'] != 'placeholder':
        print(f"   SSID: {config['wifi_ssid']}")
        print(f"   URL: {config['remote_url']}")
        print(f"   Refresh: {config['refresh_interval_seconds']}s")
        print(f"   Brightness: {config['default_brightness']}")
        if config['source'] == 'secrets.json.injected':
            print("   ℹ️  Using previously injected configuration")
    else:
        print("   Using placeholder values - firmware will need manual configuration")


main()

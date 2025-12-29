import json
import os
import sys


def generate_cmake_secrets(output_path):
    secrets_file = os.path.join(os.getcwd(), "secrets.json")
    placeholders_file = os.path.join(os.getcwd(), "secrets_place.json")

    config = {}

    if os.path.exists(secrets_file):
        try:
            with open(secrets_file, "r") as f:
                config = json.load(f)
        except (json.JSONDecodeError, FileNotFoundError) as e:
            print(f"Warning: Could not load or parse secrets.json: {e}")
    elif os.path.exists(placeholders_file):
        try:
            with open(placeholders_file, "r") as f:
                config = json.load(f)
            print(f"Note: Using fallback secrets from {placeholders_file}")
        except (json.JSONDecodeError, FileNotFoundError) as e:
            print(f"Warning: Could not load or parse {placeholders_file}: {e}")

    cmake_content = "# Generated secrets overrides\n"

    # We use the variable names expected by main/CMakeLists.txt
    if "WIFI_SSID" in config:
        cmake_content += f'set(VAL_WIFI_SSID "{str(config["WIFI_SSID"]).replace("\\", "\\\\").replace('"', '\\"')}")\n'
    if "WIFI_PASSWORD" in config:
        cmake_content += f'set(VAL_WIFI_PASSWORD "{str(config["WIFI_PASSWORD"]).replace("\\", "\\\\").replace('"', '\\"')}")\n'
    if "REMOTE_URL" in config:
        cmake_content += f'set(VAL_REMOTE_URL "{str(config["REMOTE_URL"]).replace("\\", "\\\\").replace('"', '\\"')}")\n'
    if "REFRESH_INTERVAL_SECONDS" in config:
        cmake_content += (
            f"set(VAL_REFRESH_INTERVAL_SECONDS {config['REFRESH_INTERVAL_SECONDS']})\n"
        )
    if "DEFAULT_BRIGHTNESS" in config:
        cmake_content += f"set(VAL_DEFAULT_BRIGHTNESS {config['DEFAULT_BRIGHTNESS']})\n"
    if "ENABLE_AP_MODE" in config:
        val = 1 if config["ENABLE_AP_MODE"] else 0
        cmake_content += f"set(VAL_ENABLE_AP_MODE {val})\n"
    if "ENABLE_WIFI_POWER_SAVE" in config:
        val = 1 if config["ENABLE_WIFI_POWER_SAVE"] else 0
        cmake_content += f"set(VAL_WIFI_POWER_SAVE_MODE {val})\n"
    if "SKIP_DISPLAY_VERSION" in config:
        val = 1 if config["SKIP_DISPLAY_VERSION"] else 0
        cmake_content += f"set(VAL_SKIP_DISPLAY_VERSION {val})\n"
    if "PREFER_IPV6" in config:
        val = 1 if config["PREFER_IPV6"] else 0
        cmake_content += f"set(VAL_PREFER_IPV6 {val})\n"

    with open(output_path, "w") as f:
        f.write(cmake_content)


if __name__ == "__main__":
    output_path = "secrets.cmake"
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    generate_cmake_secrets(output_path)

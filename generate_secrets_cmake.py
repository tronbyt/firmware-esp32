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

    with open(output_path, "w") as f:
        f.write(cmake_content)


if __name__ == "__main__":
    output_path = "secrets.cmake"
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    generate_cmake_secrets(output_path)

import base64
import json
import os
import sys


def validate_and_normalize_ca(ca_raw):
    if not ca_raw:
        return ""

    ca_str = ""
    if isinstance(ca_raw, list):
        ca_str = "\n".join(ca_raw)
    else:
        ca_str = str(ca_raw).strip()

    # Try Base64 decoding if it doesn't start with PEM header
    if not "BEGIN CERTIFICATE" in ca_str:
        try:
            decoded = base64.b64decode(ca_str).decode("utf-8", errors="ignore")
            if "BEGIN CERTIFICATE" in decoded:
                ca_str = decoded
        except Exception:
            pass

    # A doubled backslash in secrets.json (e.g. "\\n") decodes to a literal
    # two-character "\n" rather than a real newline. If there's no real line
    # break yet but this literal escape is present, treat it as the intended
    # line break instead of silently emitting a single-line, invalid PEM.
    if "\n" not in ca_str and "\\n" in ca_str:
        ca_str = ca_str.replace("\\r\\n", "\n").replace("\\n", "\n")

    # Normalize line endings
    ca_str = ca_str.replace("\r\n", "\n").strip()

    # Check for PEM headers
    if "BEGIN CERTIFICATE" not in ca_str or "END CERTIFICATE" not in ca_str:
        print("ERROR: CUSTOM_CA is not a valid PEM certificate (missing BEGIN/END CERTIFICATE header).")
        sys.exit(1)

    # A real PEM certificate has the header, base64 body, and footer on
    # separate lines. Reject anything that still looks like a single-line
    # blob (stray backslashes, too few lines) rather than writing invalid
    # PEM out to custom_ca.pem.
    body_lines = [line for line in ca_str.split("\n") if line]
    if len(body_lines) < 3 or "\\" in ca_str:
        print("ERROR: CUSTOM_CA does not look like a properly line-broken PEM certificate.")
        sys.exit(1)

    # Ensure trailing newline for MbedTLS parser
    if not ca_str.endswith("\n"):
        ca_str += "\n"

    return ca_str


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

    def escape_cmake(value):
        return str(value).replace("\\", "\\\\").replace('"', '\\"')

    # We use the variable names expected by main/CMakeLists.txt
    if "WIFI_SSID" in config:
        cmake_content += f'set(VAL_WIFI_SSID "{escape_cmake(config["WIFI_SSID"])}")\n'
    if "WIFI_PASSWORD" in config:
        cmake_content += f'set(VAL_WIFI_PASSWORD "{escape_cmake(config["WIFI_PASSWORD"])}")\n'
    if "REMOTE_URL" in config:
        cmake_content += f'set(VAL_REMOTE_URL "{escape_cmake(config["REMOTE_URL"])}")\n'
    # Merged into the mbedtls certificate bundle at build time via
    # CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH=main/certs, so custom and
    # default-trusted hosts both verify through the same bundle.
    certs_dir = os.path.join(os.getcwd(), "main", "certs")
    custom_ca_path = os.path.join(certs_dir, "custom_ca.pem")
    os.makedirs(certs_dir, exist_ok=True)

    if "CUSTOM_CA" in config and config["CUSTOM_CA"]:
        ca_pem = validate_and_normalize_ca(config["CUSTOM_CA"])
        with open(custom_ca_path, "w") as f:
            f.write(ca_pem)
    elif os.path.exists(custom_ca_path):
        os.remove(custom_ca_path)

    with open(output_path, "w") as f:
        f.write(cmake_content)


if __name__ == "__main__":
    output_path = "secrets.cmake"
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    generate_cmake_secrets(output_path)

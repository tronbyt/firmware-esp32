#!/usr/bin/env python3

import os
import json

Import("env")


def main() -> None:
    # copy libwebp's library.json to the lib directory
    env.Execute(Copy("$PROJECT_LIBDEPS_DIR/$PIOENV/libwebp/library.json", "$PROJECT_DIR/lib/webp/library.json"))

    sdkconfig_path = os.path.join(env["PROJECT_DIR"], "sdkconfig")
    if os.path.exists(sdkconfig_path):
        print(f"Deleting existing {sdkconfig_path} to force regeneration...")
        os.remove(sdkconfig_path)

    # if secrets.h file exists
    if os.path.exists("secrets.json"):
        # read secrets.h file
        with open("secrets.json", "r") as f:
            json_config = json.load(f)

            wifi_ssid = json_config.get("WIFI_SSID", "")
            wifi_password = json_config.get("WIFI_PASSWORD", "")
            remote_url = json_config.get("REMOTE_URL", "")
            refresh_interval_seconds = json_config.get(
                "REFRESH_INTERVAL_SECONDS", 10
            )
            default_brightness = json_config.get("DEFAULT_BRIGHTNESS", 10)

    else:  # use environment variables
        print(
            "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\nWARNING : edit secrets.json.example and save as secrets.json\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        )
        print("Using Xplaceholder values for direct firmware.bin modification.")
        wifi_ssid = "XplaceholderWIFISSID____________"
        wifi_password = "XplaceholderWIFIPASSWORD________________________________________"
        remote_url = "XplaceholderREMOTEURL___________________________________________________________________________________________________________"
        refresh_interval_seconds = (
            10  # int(os.environ.get("REFRESH_INTERVAL_SECONDS"))
        )
        default_brightness = (
            30  # int(os.environ.get("DEFAULT_BRIGHTNESS"))
        )

    env.Append(
        CCFLAGS=[
            f"-DWIFI_SSID={env.StringifyMacro(wifi_ssid)}",
            f"-DWIFI_PASSWORD={env.StringifyMacro(wifi_password)}",
            f"-DREMOTE_URL={env.StringifyMacro(remote_url)}",
            f"-DREFRESH_INTERVAL_SECONDS={refresh_interval_seconds}",
            f"-DDEFAULT_BRIGHTNESS={default_brightness}",
        ],
    )


main()

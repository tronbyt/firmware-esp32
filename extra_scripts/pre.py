#!/usr/bin/env python3

import os,json

Import("env")

def main() -> None:
    # if secrets.h file exists
    if os.path.exists("secrets.json"):
        # read secrets.h file
        with open("secrets.json", "r") as f:
            json_config = json.load(f)

            tidbyt_wifi_ssid = json_config.get("TIDBYT_WIFI_SSID","")
            tidbyt_wifi_password= json_config.get("TIDBYT_WIFI_PASSWORD","")
            tidbyt_remote_url = json_config.get("TIDBYT_REMOTE_URL", "")
            tidbyt_refresh_interval_seconds = json_config.get("TIDBYT_REFRESH_INTERVAL_SECONDS",10)
            tidbyt_default_brightness = json_config.get("TIDBYT_DEFAULT_BRIGHTNESS",10)

    else: # use environment variables
        tidbyt_wifi_ssid = os.environ.get("TIDBYT_WIFI_SSID")
        tidbyt_wifi_password = os.environ.get("TIDBYT_WIFI_PASSWORD")
        tidbyt_remote_url = os.environ.get("TIDBYT_REMOTE_URL")
        tidbyt_refresh_interval_seconds = int(os.environ.get("TIDBYT_REFRESH_INTERVAL_SECONDS"))
        tidbyt_default_brightness = int(os.environ.get("TIDBYT_DEFAULT_BRIGHTNESS"))

    env.Append(
        CCFLAGS=[
            f"-DTIDBYT_WIFI_SSID='\"{tidbyt_wifi_ssid}\"'",
            f"-DTIDBYT_WIFI_PASSWORD='\"{tidbyt_wifi_password}\"'",
            f"-DTIDBYT_REMOTE_URL='\"{tidbyt_remote_url}\"'",
            f"-DTIDBYT_REFRESH_INTERVAL_SECONDS={tidbyt_refresh_interval_seconds}",
            f"-DTIDBYT_DEFAULT_BRIGHTNESS={tidbyt_default_brightness}",
        ],
        CPPDEFINES=[
            "-DNO_GFX",
            "-DNO_FAST_FUNCTIONS",
        ],
    )

main()
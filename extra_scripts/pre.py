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
        print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\nWARNING : edit secrets.json.example and save as secrets.json\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        print("Using Xplaceholder values for direct firmware.bin modification.")
        tidbyt_wifi_ssid = "XplaceholderWIFISSID________________________________"
        tidbyt_wifi_password = "XplaceholderWIFIPASSWORD____________________________"
        tidbyt_remote_url = "XplaceholderREMOTEURL_________________________________________________________________________________________"
        tidbyt_refresh_interval_seconds = 10 #int(os.environ.get("TIDBYT_REFRESH_INTERVAL_SECONDS"))
        tidbyt_default_brightness = 30 # int(os.environ.get("TIDBYT_DEFAULT_BRIGHTNESS"))

    env.Append(
        CCFLAGS=[
            f"-DTIDBYT_WIFI_SSID={env.StringifyMacro(tidbyt_wifi_ssid)}",
            f"-DTIDBYT_WIFI_PASSWORD={env.StringifyMacro(tidbyt_wifi_password)}",
            f"-DTIDBYT_REMOTE_URL={env.StringifyMacro(tidbyt_remote_url)}",
            f"-DTIDBYT_REFRESH_INTERVAL_SECONDS={tidbyt_refresh_interval_seconds}",
            f"-DTIDBYT_DEFAULT_BRIGHTNESS={tidbyt_default_brightness}",
        ],
        CPPDEFINES=[
            "-DNO_GFX",
            "-DNO_FAST_FUNCTIONS",
        ],
    )

main()

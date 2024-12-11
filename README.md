# Hardware SDK
[![Discord Server](https://img.shields.io/discord/928484660785336380?style=flat-square)](https://discord.gg/r45MXG4kZc)

This repository contains a community supported firmware for the Tidbyt hardware 🤓. 

## Alert
As of December 2024 this firmeware is only functional for Tidbyt Gen1.

## Warning

⚠️ Warning! Flashing your Tidbyt with this firmware or derivatives could fatally 
damage your device. As such, flashing your Tidbyt with this firmware or
derivatives voids your warranty and comes without support.

## Setup
This project uses PlatformIO to build, flash, and monitor firmware on the Tidbyt.
To get started, you will need to download [PlatformIO Core][2] on your computer.

Additionally, this firmware is designed to work with [Tidbyt Manager](https://github.com/tavdog/tidbyt-manager).
You can point this firmware at any URL that hosts a WebP image that is optimized for the Tidbyt display.

## Getting Started
To flash the custom firmware on your device, run the following after replacing
the variables in secrets.json.example with your desired own information and renaming it to `secrets.json`
If using tidbyt_manager in docker replace the ip address to the docker host's ip address.
```
{
    "TIDBYT_WIFI_SSID": "myssiD",
    "TIDBYT_WIFI_PASSWORD": "<PASSWORD>",
    "TIDBYT_REMOTE_URL=": "http://192.168.1.10:8000/admin/skidbyt_1/next",
    "TIDBYT_REFRESH_INTERVAL_SECONDS": 10,
    "TIDBYT_DEFAULT_BRIGHTNESS" : 30
}
```
Then run the following command :
```pio run --environment tidbyt --target upload```

## Monitoring Logs
To check the output of your running firmware, run the following:
```
pio device monitor
```

## Back to Normal
To get your Tidbyt back to normal, you can run the following to flash the
production firmware onto your Tidbyt:
```
pio run --target reset
```

[1]: https://github.com/tidbyt/pixlet
[2]: https://docs.platformio.org/en/latest/core/installation/index.html

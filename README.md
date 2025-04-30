# Tronbyt Firmware

[![Discord Server](https://img.shields.io/discord/928484660785336380?style=flat-square)](https://discord.gg/r45MXG4kZc)

This repository contains a community supported firmware for the Tidbyt hardware 🤓.

## Warning

⚠️ Warning! Flashing your Tidbyt with this firmware or derivatives could fatally
damage your device. As such, flashing your Tidbyt with this firmware or
derivatives voids your warranty and comes without support.

## Getting Started

Follow the setup instructions for [tronbyt-server][3] unless you want to build yourself with platformio.

## Building yourself with PlatformIO

Only follow these instructions if you want to build the firmware yourself. Otherwise let the [tronbyt-server][3] generate the firmware file for you.
This project uses PlatformIO to build, flash, and monitor firmware on the Tidbyt.
To get started, you will need to download [PlatformIO Core][2] on your computer.

Additionally, this firmware is designed to work with https://github.com/tronbyt/server or
you can point this firmware at any URL that hosts a WebP image that is optimized for the Tidbyt display.

To flash the custom firmware on your device, run the following after replacing
the variables in secrets.json.example with your desired own information and renaming it to `secrets.json`
If using tronbyt_manager in docker replace the ip address to the docker host's ip address.

```
{
    "WIFI_SSID": "myssiD",
    "WIFI_PASSWORD": "<PASSWORD>",
    "REMOTE_URL=": "http://homeServer.local:8000/admin/tronbyt_1/next",
    "REFRESH_INTERVAL_SECONDS": 10,
    "DEFAULT_BRIGHTNESS" : 30
}
```

Then run the following command:

```
pio run --environment tidbyt-gen1 --target upload
```

If you're flashing to a Tidbyt Gen2, just change to the above to use
the `--environment tidbyt-gen2` flag.

## Monitoring Logs

To check the output of your running firmware, run the following:

```
pio device monitor
```

## Back to Normal

To get your Tidbyt back to normal, you can run the following to flash the
production firmware onto your Tidbyt:

```
pio run --target reset --environment tidbyt-gen1
```

And if you're working with a Tidbyt Gen 2:

```
pio run --target reset --environment tidbyt-gen2
```

[1]: https://github.com/tidbyt/pixlet
[2]: https://docs.platformio.org/en/latest/core/installation/index.html
[3]: https://github.com/tronbyt/server

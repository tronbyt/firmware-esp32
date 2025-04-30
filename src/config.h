#pragma once

// These constants are defined by the build system in the pre.py script
// They are passed as compiler flags from the secrets.json file
// We're defining them here for the IDE to recognize them
#ifndef TIDBYT_WIFI_SSID
#define TIDBYT_WIFI_SSID "default_ssid"
#endif

#ifndef TIDBYT_WIFI_PASSWORD
#define TIDBYT_WIFI_PASSWORD "default_password"
#endif

#ifndef TIDBYT_REMOTE_URL
#define TIDBYT_REMOTE_URL "http://default.url"
#endif

#ifndef TIDBYT_REFRESH_INTERVAL_SECONDS
#define TIDBYT_REFRESH_INTERVAL_SECONDS 10
#endif

#ifndef TIDBYT_DEFAULT_BRIGHTNESS
#define TIDBYT_DEFAULT_BRIGHTNESS 30
#endif

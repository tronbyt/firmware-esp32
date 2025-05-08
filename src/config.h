#pragma once

// These constants are defined by the build system in the pre.py script
// They are passed as compiler flags from the secrets.json file
// We're defining them here for the IDE to recognize them
#ifndef WIFI_SSID
#define WIFI_SSID "default_ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "default_password"
#endif

#ifndef REMOTE_URL
#define REMOTE_URL "http://default.url"
#endif

#ifndef REFRESH_INTERVAL_SECONDS
#define REFRESH_INTERVAL_SECONDS 10
#endif

#ifndef DEFAULT_BRIGHTNESS
#define DEFAULT_BRIGHTNESS 30
#endif

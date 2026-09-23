// The device's identity: one name used for both the WiFi SoftAP SSID and the
// BLE device name, plus the AP's password. Stored in NVS so it survives a
// reboot, and changed from the web UI (which then restarts the device).
//
// NVS rather than a file on LittleFS: the storage partition is formatted when it
// fails to mount and the whole image is rewritten whenever the theme or the web
// UI is uploaded, so a config file there would not survive normal use.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Longest device name accepted — a hard limit, not a taste call. The legacy BLE
// advertising PDU is 31 bytes and the name shares it with the flags and the
// service UUID: 3 (flags) + 4 (one 16-bit UUID) + 2 + len <= 31.
#define DEV_NAME_MAX 22
// WPA2-PSK passphrase rules, which esp_wifi_set_config enforces by failing.
#define DEV_PASS_MIN  8
#define DEV_PASS_MAX 63

// Loads the stored identity, falling back to the factory defaults for anything
// missing or invalid. Call once at boot, before anything reads the name.
void settingsLoad(void);

const char* settingsName(void);
const char* settingsPass(void);

// Validates both and stores them. An empty name or password means "leave that
// one alone". On failure nothing is written and `err` (if given) receives a
// short reason for the web UI to show.
bool settingsSave(const char* name, const char* pass, const char** err);

// Stores the factory defaults.
bool settingsReset(const char** err);

// True when the given name/password would be accepted.
bool settingsNameValid(const char* name);
bool settingsPassValid(const char* pass);

#ifdef __cplusplus
}
#endif

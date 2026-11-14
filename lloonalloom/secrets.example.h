#pragma once
// Public template: copy to secrets.h and fill your own settings.
// Keep secrets.h local; only this example belongs in Git.
#define SECRET_SSID "wifi-name"
#define SECRET_PASS "wifi-password"
#define SECRET_DEVICE_NAME "lloona"
#define SECRET_LATITUDE 41.4
#define SECRET_LONGITUDE 2.17
// Optional bearer token; configure the same value in Home Assistant.
#define SECRET_API_TOKEN ""
// OTA is enabled by default. Fill the password before compiling.
// Set false only when deliberately choosing USB-only updates.
#define SECRET_ENABLE_OTA true
#define SECRET_OTA_PASSWORD ""

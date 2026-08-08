#pragma once

#include <stdint.h>

#include "events.h" // PIN_LENGTH

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define MQTT_HOST_MAX_LEN 64
#define MQTT_USERNAME_MAX_LEN 32
#define MQTT_PASSWORD_MAX_LEN 64

// Local-only device provisioning state: WiFi/MQTT credentials and the admin
// PIN gating the Settings screen. Deliberately separate from AccessConfig
// (access_control.h) — that one is synced FROM MQTT, but WiFi/MQTT
// credentials have to exist before MQTT is reachable at all, so they can't
// live in the same synced file.
struct DeviceConfig {
    char wifiSsid[WIFI_SSID_MAX_LEN + 1];
    char wifiPassword[WIFI_PASSWORD_MAX_LEN + 1];
    char mqttHost[MQTT_HOST_MAX_LEN + 1];
    char mqttUsername[MQTT_USERNAME_MAX_LEN + 1];
    char mqttPassword[MQTT_PASSWORD_MAX_LEN + 1];
    char adminPin[PIN_LENGTH + 1];
    bool provisioned;
};

// Loads from LittleFS. Returns false (leaving `out` untouched) if no file
// exists yet or parsing fails — callers should treat that as "unprovisioned".
bool loadDeviceConfig(DeviceConfig &out);

// Persists to LittleFS. Returns false on write failure.
bool saveDeviceConfig(const DeviceConfig &config);

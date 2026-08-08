#pragma once

#include <lvgl.h>

// Builds the SETTINGS screen inside `parent` (the shared content area,
// already inset below the box's bezel): WiFi SSID/password, MQTT host, and
// admin PIN fields with an on-screen keyboard, plus a Save button. Used both
// for first-boot provisioning (unprovisioned devices land here automatically)
// and for later edits (reached via the hidden admin-PIN gesture on HOME).
// Saving writes DeviceConfig to LittleFS and restarts the device — simplest
// correct way to apply new WiFi/MQTT credentials.
void buildSettingsScreen(lv_obj_t *parent);

#pragma once

// Live MQTT connection state, updated once per configSyncTask loop iteration
// (see configSyncTaskFn). uiTask (Core 1) polls this to drive the status
// icon in the top-right corner — a plain bool read is safe cross-core on
// this hardware (single-byte, aligned), same as the existing WiFi.status()
// cross-core reads.
extern volatile bool mqttConnected;

// Task entry point (Core 0). Connects to Wi-Fi/MQTT, subscribes to the
// retained config topic, and on a successfully-parsed payload writes it to
// LittleFS and notifies logicTask to reload (CONFIG_UPDATED on logicQueue).
// Runs entirely independently of logicTask's own LittleFS-first boot path —
// this task's job is to eventually sync a *newer* config, not to block boot.
// Spawn via xTaskCreatePinnedToCore, never call directly.
void configSyncTaskFn(void *pvParameters);

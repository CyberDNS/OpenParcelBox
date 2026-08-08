#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Fixed PIN length for the keypad UI and access_control validation.
#define PIN_LENGTH 6

// Shared with PinEntry.id (access_control.h) — whoever/whatever matched a
// grant (a courier name, "dev", "BLE", etc.), reported out over MQTT so Home
// Assistant can show/notify on who opened the box.
#define ACCESS_ID_MAX_LEN 16

enum class AppEvent {
    PIN_SUBMITTED,             // UI -> logic: user entered a full PIN
    BLE_PROXIMITY_DETECTED,    // BLE -> logic: allow-listed phone is "near"
    REMOTE_OPEN_REQUESTED,     // configSync -> logic: HA button press (msg.scope set by which button)
    ACCESS_PARCELBOX_GRANTED,  // logic -> UI + servo: unlock
    ACCESS_PARCELBOX_DENIED,   // logic -> UI: bad PIN / not recognized
    CONFIG_UPDATED,            // configSync -> logic: reload config from LittleFS
};

// Which compartment(s) a grant opens — same servo, different angle:
// PARCEL_ONLY (e.g. courier drop-off) vs FULL (letters + parcel).
enum class AccessScope : uint8_t {
    PARCEL_ONLY,
    FULL,
};

struct AppMessage {
    AppEvent event;
    char pin[PIN_LENGTH + 1];             // only populated for PIN_SUBMITTED
    AccessScope scope;                    // only meaningful for ACCESS_PARCELBOX_GRANTED/DENIED
    char accessId[ACCESS_ID_MAX_LEN + 1]; // only populated for ACCESS_PARCELBOX_GRANTED
};

// Kept short — this is one already-formatted log line ("<timestamp> <text>",
// see log.cpp), not a structured payload. Sized comfortably above what
// log.cpp's line buffers format into, without truncating.
#define LOG_LINE_MAX_LEN 128

// UI/BLE -> logic
extern QueueHandle_t logicQueue;
// logic -> UI
extern QueueHandle_t uiQueue;
// logic -> servo
extern QueueHandle_t servoQueue;
// logic -> configSync: publish an MQTT event/state update (ACCESS_PARCELBOX_
// GRANTED/DENIED, for the Home Assistant "who opened it" entities)
extern QueueHandle_t mqttQueue;
// log.cpp -> configSync: depth-1 "this file changed" signals (see
// xQueueOverwrite in log.cpp). On seeing one, configSyncTask re-reads the
// matching .log file from LittleFS in full and republishes it (retained) to
// openparcelbox/log/error, .../access, or .../denied — so those topics
// always hold the log's complete current content, not just the newest
// line, and a client subscribing at any time gets the full history so far.
extern QueueHandle_t errorLogDirtyQueue;
extern QueueHandle_t accessLogDirtyQueue;
extern QueueHandle_t deniedLogDirtyQueue;

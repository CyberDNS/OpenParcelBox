#pragma once

#include <stddef.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// "What's been put in the box since it was last emptied": every letter-flap
// opening and every parcel-only grant is appended; a FULL grant (the
// resident emptying the box) clears it. Persisted to LittleFS so it survives
// a reboot, and published (retained) to MQTT as one JSON document:
//
//   {"count":3,"letters":2,"parcels":1,"since":"2026-11-14T07:12:03Z",
//    "items":[{"time":"2026-11-14T09:30:11Z","type":"letter"},
//             {"time":"2026-11-14T11:02:45Z","type":"parcel","by":"Courier DHL"},
//             ...]}
//
// "count"/"letters"/"parcels" keep counting past INBOX_MAX_ITEMS; only the
// newest INBOX_MAX_ITEMS entries are kept in "items".

#define INBOX_MAX_ITEMS 20
// Comfortably above INBOX_MAX_ITEMS entries at their longest (~80 bytes each).
#define INBOX_JSON_MAX_LEN 2048

enum class InboxItemType : uint8_t {
    LETTER,
    PARCEL,
};

// depth-1 "inbox changed" signal (xQueueOverwrite), drained by configSyncTask
// which then republishes the whole inbox document.
extern QueueHandle_t inboxDirtyQueue;

// Call once from setup(), after LittleFS is mounted and before tasks start.
void inboxInit();

// by: who opened it (parcel grants); nullptr/empty for letters.
void inboxAdd(InboxItemType type, const char *by);
void inboxClear();

// Writes the JSON document above into buf; returns its length.
size_t inboxSerialize(char *buf, size_t bufLen);

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "events.h"

// Local paths of the three append-only logs (see log.cpp for the
// trim-on-size-cap policy). Exposed so configSyncTask can read/republish
// the same files whose dirty-queues it drains — kept as the single source
// of truth rather than duplicating the path strings there.
extern const char *const kErrorLogPath;
extern const char *const kAccessLogPath;
extern const char *const kDeniedLogPath;

// Creates the file-write mutex and the three dirty-queues above. Call once
// from setup(), after Serial.begin() and before any task is spawned
// (logError/logAccess assume it already exists) — deliberately before the
// LittleFS.begin() check, so a mount failure itself can be reported via
// logError().
void logInit();

// The mutex logAccess()/logError() hold while appending/trimming a log file.
// configSyncTask must hold this too while it reads a file back out to stream
// to MQTT (republishLogFile) — otherwise a trim's truncate-and-rewrite can
// land mid-read, handing the reader a shorter/different file than the size
// it already promised in the MQTT packet's header, desyncing that
// connection's framing until the next reconnect.
SemaphoreHandle_t logFileMutex();

// "%Y-%m-%dT%H:%M:%SZ" (UTC) once NTP has synced, "T+<uptime>ms" before
// that. 24 bytes is enough for either.
void formatTimestamp(char *buf, size_t bufLen);

// Appends a timestamped line to /error.log (trimmed back once it grows past
// a size cap — see log.cpp) and marks errorLogDirtyQueue so configSyncTask
// republishes the file's full current content to MQTT. Safe to call from
// any task/core.
void logError(const char *fmt, ...);

// Appends a timestamped line recording an access attempt and its scope —
// to /access.log if granted, /denied.log if not — and marks the matching
// dirty-queue (accessLogDirtyQueue/deniedLogDirtyQueue) the same way.
// Separate from the structured MQTT event configSyncTask publishes off
// mqttQueue for Home Assistant — this is only the local, durable copy plus
// its raw-file MQTT mirror.
void logAccess(const char *accessId, AccessScope scope, bool granted);

#include "log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

const char *const kErrorLogPath = "/error.log";
const char *const kAccessLogPath = "/access.log";  // grants only
const char *const kDeniedLogPath = "/denied.log";  // denials only

// Each log is trimmed back to roughly its newest half once it crosses this
// size, so a chatty error condition can't slowly fill (or wear out) the
// LittleFS partition. 16KB holds several hundred lines each — plenty for
// on-device diagnosis; anything older is expected to already be wherever
// the MQTT topics get aggregated (e.g. Home Assistant's logbook).
static const size_t kMaxLogBytes = 16 * 1024;

// Guards LittleFS open/write/close for these two files specifically — both
// logAccess() (called from logicTask) and logError() (called from any task,
// including uiTask/configSyncTask on the other core) can fire at any time,
// and interleaved writes from two tasks would otherwise corrupt a line.
static SemaphoreHandle_t s_fileMutex = nullptr;

QueueHandle_t errorLogDirtyQueue = nullptr;
QueueHandle_t accessLogDirtyQueue = nullptr;
QueueHandle_t deniedLogDirtyQueue = nullptr;

void logInit() {
    s_fileMutex = xSemaphoreCreateMutex();
    // Depth 1 + xQueueOverwrite: several appends before configSyncTask next
    // checks in collapse into a single pending "go republish the whole
    // file" signal, rather than queuing up one entry per line.
    errorLogDirtyQueue = xQueueCreate(1, sizeof(uint8_t));
    accessLogDirtyQueue = xQueueCreate(1, sizeof(uint8_t));
    deniedLogDirtyQueue = xQueueCreate(1, sizeof(uint8_t));
}

SemaphoreHandle_t logFileMutex() {
    return s_fileMutex;
}

static void markDirty(QueueHandle_t queue) {
    uint8_t dummy = 1;
    xQueueOverwrite(queue, &dummy);
}

// "%Y-%m-%dT%H:%M:%SZ" once NTP has synced (see ensureWifiConnected's
// configTime() call); before that, or if it never does, time(nullptr) just
// counts up from the epoch from boot, which would print a confusing
// "1970-01-01" — so fall back to millis() uptime instead, which is at
// least honestly labeled as relative.
static void formatTimestamp(char *buf, size_t bufLen) {
    time_t now = time(nullptr);
    if (now > 1700000000) { // sometime in 2023 — well past any un-synced clock's value
        struct tm tmInfo;
        gmtime_r(&now, &tmInfo);
        strftime(buf, bufLen, "%Y-%m-%dT%H:%M:%SZ", &tmInfo);
    } else {
        snprintf(buf, bufLen, "T+%lums", millis());
    }
}

// Keeps only the newest ~half of the file once it crosses kMaxLogBytes,
// snapped forward to the next line boundary so no entry is left truncated
// mid-line. Called with s_fileMutex already held.
static void trimLogIfNeeded(const char *path) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        return;
    }
    size_t size = f.size();
    if (size <= kMaxLogBytes) {
        f.close();
        return;
    }

    f.seek(size - kMaxLogBytes / 2);
    int c;
    while ((c = f.read()) != -1 && c != '\n') {
        // Skip the rest of the (possibly partial) line we seeked into.
    }
    String tail = f.readString();
    f.close();

    File out = LittleFS.open(path, "w");
    if (!out) {
        return;
    }
    out.print(tail);
    out.close();
}

static void appendLine(const char *path, const char *line) {
    // A stuck/corrupt filesystem should cost us one dropped log line, not a
    // hung task — every caller here (logicTask, uiTask, configSyncTask) has
    // other work that must keep running regardless.
    if (xSemaphoreTake(s_fileMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    File f = LittleFS.open(path, "a");
    if (f) {
        f.println(line);
        f.close();
        trimLogIfNeeded(path);
    }

    xSemaphoreGive(s_fileMutex);
}

void logError(const char *fmt, ...) {
    char msg[LOG_LINE_MAX_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Serial.printf("ERROR: %s\n", msg);

    char ts[24];
    formatTimestamp(ts, sizeof(ts));
    char line[LOG_LINE_MAX_LEN];
    snprintf(line, sizeof(line), "%s %s", ts, msg);
    appendLine(kErrorLogPath, line);
    markDirty(errorLogDirtyQueue);
}

void logAccess(const char *accessId, AccessScope scope, bool granted) {
    char ts[24];
    formatTimestamp(ts, sizeof(ts));
    char line[LOG_LINE_MAX_LEN];
    // Which file/topic it lands in already says granted vs. denied, so the
    // line itself doesn't need to repeat that.
    snprintf(line, sizeof(line), "%s id=%s scope=%s", ts,
             (accessId && accessId[0]) ? accessId : "-",
             scope == AccessScope::FULL ? "full" : "parcel");

    const char *path = granted ? kAccessLogPath : kDeniedLogPath;
    QueueHandle_t dirtyQueue = granted ? accessLogDirtyQueue : deniedLogDirtyQueue;
    appendLine(path, line);
    markDirty(dirtyQueue);
}

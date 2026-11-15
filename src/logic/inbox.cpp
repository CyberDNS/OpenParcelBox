#include "inbox.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#include <freertos/semphr.h>

#include "events.h"
#include "../logging/log.h"

static const char *kInboxPath = "/inbox.json";

struct InboxItem {
    char time[24];
    InboxItemType type;
    char by[ACCESS_ID_MAX_LEN + 1];
};

static InboxItem s_items[INBOX_MAX_ITEMS];
static size_t s_itemCount = 0;
static uint32_t s_letters = 0;
static uint32_t s_parcels = 0;
static char s_since[24] = "";

// logicTask mutates, configSyncTask serializes for MQTT — different tasks.
static SemaphoreHandle_t s_mutex = nullptr;

QueueHandle_t inboxDirtyQueue = nullptr;

// Called with s_mutex held.
static size_t serializeLocked(char *buf, size_t bufLen) {
    JsonDocument doc;
    doc["count"] = s_letters + s_parcels;
    doc["letters"] = s_letters;
    doc["parcels"] = s_parcels;
    doc["since"] = s_since;
    JsonArray items = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < s_itemCount; i++) {
        JsonObject item = items.add<JsonObject>();
        item["time"] = s_items[i].time;
        item["type"] = s_items[i].type == InboxItemType::PARCEL ? "parcel" : "letter";
        if (s_items[i].by[0]) {
            item["by"] = s_items[i].by;
        }
    }
    return serializeJson(doc, buf, bufLen);
}

// Called with s_mutex held. The persisted file is the same document that
// goes to MQTT, so there's only one format to keep in sync.
static void persistAndNotifyLocked() {
    static char buf[INBOX_JSON_MAX_LEN];
    size_t n = serializeLocked(buf, sizeof(buf));
    File f = LittleFS.open(kInboxPath, "w");
    if (f) {
        f.write(reinterpret_cast<const uint8_t *>(buf), n);
        f.close();
    } else {
        logError("inbox: failed to write %s", kInboxPath);
    }
    uint8_t dummy = 1;
    xQueueOverwrite(inboxDirtyQueue, &dummy);
}

static void loadFromLittleFS() {
    // Checked first: open() on a missing file makes the VFS layer log an
    // error, and a missing file here is normal (nothing delivered yet).
    if (!LittleFS.exists(kInboxPath)) {
        return;
    }
    File f = LittleFS.open(kInboxPath, "r");
    if (!f) {
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        logError("inbox: ignoring unparseable %s", kInboxPath);
        return;
    }

    s_letters = doc["letters"] | 0;
    s_parcels = doc["parcels"] | 0;
    strlcpy(s_since, doc["since"] | "", sizeof(s_since));
    s_itemCount = 0;
    for (JsonObject item : doc["items"].as<JsonArray>()) {
        if (s_itemCount >= INBOX_MAX_ITEMS) {
            break;
        }
        InboxItem &dst = s_items[s_itemCount++];
        strlcpy(dst.time, item["time"] | "", sizeof(dst.time));
        dst.type = strcmp(item["type"] | "", "parcel") == 0 ? InboxItemType::PARCEL
                                                           : InboxItemType::LETTER;
        strlcpy(dst.by, item["by"] | "", sizeof(dst.by));
    }
}

void inboxInit() {
    s_mutex = xSemaphoreCreateMutex();
    inboxDirtyQueue = xQueueCreate(1, sizeof(uint8_t));
    loadFromLittleFS();
}

void inboxAdd(InboxItemType type, const char *by) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Full: drop the oldest entry; the counters still include it.
    if (s_itemCount == INBOX_MAX_ITEMS) {
        memmove(&s_items[0], &s_items[1], sizeof(InboxItem) * (INBOX_MAX_ITEMS - 1));
        s_itemCount--;
    }
    InboxItem &item = s_items[s_itemCount++];
    formatTimestamp(item.time, sizeof(item.time));
    item.type = type;
    strlcpy(item.by, by ? by : "", sizeof(item.by));

    if (type == InboxItemType::PARCEL) {
        s_parcels++;
    } else {
        s_letters++;
    }

    persistAndNotifyLocked();
    xSemaphoreGive(s_mutex);
}

void inboxClear() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_itemCount = 0;
    s_letters = 0;
    s_parcels = 0;
    formatTimestamp(s_since, sizeof(s_since));
    persistAndNotifyLocked();
    xSemaphoreGive(s_mutex);
}

size_t inboxSerialize(char *buf, size_t bufLen) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t n = serializeLocked(buf, bufLen);
    xSemaphoreGive(s_mutex);
    return n;
}

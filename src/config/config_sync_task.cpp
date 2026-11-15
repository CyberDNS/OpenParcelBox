#include "config_sync_task.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "events.h"
#include "device_config.h"
#include "../logging/log.h"
#include "../logic/access_control.h"
#include "../logic/inbox.h"

// UTC, no DST — this is only used for log timestamps (see log.cpp), not
// anything user-facing, so there's no reason to plumb through a configurable
// timezone for it.
static const char *kNtpServer = "pool.ntp.org";

// Raw log-file mirrors: each topic always holds the complete, current
// content of the matching LittleFS file (/error.log, /access.log,
// /denied.log), retained, republished in full whenever it changes — plain
// text, no JSON/HA discovery involved. A client subscribing at any moment
// gets the whole log so far, not just whatever line happened to be newest.
static const char *kMqttErrorLogTopic = "openparcelbox/log/error";
static const char *kMqttAccessLogTopic = "openparcelbox/log/access";
static const char *kMqttDeniedLogTopic = "openparcelbox/log/denied";

// MQTT broker port/topic/client-id aren't collected on the Settings screen —
// only host + username/password are, for now. Revisit if a real deployment
// needs these configurable too.
static const uint16_t kMqttPort = 1883;
static const char *kMqttConfigTopic = "openparcelbox/config";
static const char *kMqttOpenCommandTopic = "openparcelbox/command/open";
static const char *kMqttClientId = "parcelbox";

// Home Assistant MQTT Discovery. One "device" grouping two entities:
//   - an Event entity (openparcelbox/access/event, NOT retained) — fires live
//     on every grant, carries who/scope as attributes. This is what an HA
//     automation triggers a push notification from.
//   - a Sensor entity (openparcelbox/access/last, retained) — persists
//     "last opened by" across HA restarts. HA timestamps every entity state
//     change automatically (last_changed), so "when" needs no explicit
//     field from the device itself.
// ASSUMPTION: single-box deployment — "openparcelbox" is a fixed device
// identifier. A multi-box setup would need a unique ID per device (e.g.
// derived from the MAC address) to avoid HA merging them into one device.
static const char *kMqttEventTopic = "openparcelbox/access/event";
static const char *kMqttLastTopic = "openparcelbox/access/last";
static const char *kHaEventDiscoveryTopic = "homeassistant/event/openparcelbox/access/config";
static const char *kHaSensorDiscoveryTopic = "homeassistant/sensor/openparcelbox/last_access/config";

// Letter flap reed contact: "open"/"closed", retained (HA binary_sensor).
static const char *kMqttLetterFlapTopic = "openparcelbox/letter_flap";
static const char *kHaLetterFlapDiscoveryTopic = "homeassistant/binary_sensor/openparcelbox/letter_flap/config";
// What's been put in since the last full opening — see inbox.h for the
// JSON format. Retained (HA sensor: state = count, list in attributes).
static const char *kMqttInboxTopic = "openparcelbox/inbox";
static const char *kHaInboxDiscoveryTopic = "homeassistant/sensor/openparcelbox/inbox/config";

// Last flap state seen from logicTask, republished on every (re)connect.
// -1 = not reported yet.
static int8_t s_letterFlapState = -1;

static WiFiClient s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);
static uint32_t s_activeConfigVersion = 0;

volatile bool mqttConnected = false;

static void handleConfigTopic(uint8_t *payload, unsigned int length) {
    AccessConfig parsed;
    if (!applyAndPersistConfig(reinterpret_cast<const char *>(payload), length, parsed)) {
        logError("configSyncTaskFn: rejected unparseable config payload");
        return;
    }

    if (parsed.version == s_activeConfigVersion) {
        return;
    }
    s_activeConfigVersion = parsed.version;

    AppMessage msg{};
    msg.event = AppEvent::CONFIG_UPDATED;
    xQueueSend(logicQueue, &msg, 0);
}

// HA button payloads are just "full" or "parcel" (see publishHaButtonDiscovery's
// payload_press) — no PIN, no further check: reaching this topic at all
// already implies MQTT broker access/authorization.
static void handleOpenCommandTopic(uint8_t *payload, unsigned int length) {
    char payloadStr[16] = {0};
    size_t n = length < sizeof(payloadStr) - 1 ? length : sizeof(payloadStr) - 1;
    memcpy(payloadStr, payload, n);
    payloadStr[n] = '\0';

    AppMessage msg{};
    msg.event = AppEvent::REMOTE_OPEN_REQUESTED;
    msg.scope = (strcmp(payloadStr, "full") == 0) ? AccessScope::FULL : AccessScope::PARCEL_ONLY;
    xQueueSend(logicQueue, &msg, 0);
}

static void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
    if (strcmp(topic, kMqttConfigTopic) == 0) {
        handleConfigTopic(payload, length);
    } else if (strcmp(topic, kMqttOpenCommandTopic) == 0) {
        handleOpenCommandTopic(payload, length);
    }
}

// Sets the device block identically on both entities so Home Assistant
// groups them under one "ParcelBox" device instead of two separate ones.
static void addHaDevice(JsonObject &doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray ids = device["identifiers"].to<JsonArray>();
    ids.add("openparcelbox");
    device["name"] = "ParcelBox";
    device["manufacturer"] = "OpenParcelBox";
}

static void publishHaButtonDiscovery(const char *discoveryTopic, const char *name,
                                      const char *uniqueId, const char *payloadPress) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["name"] = name;
    root["unique_id"] = uniqueId;
    root["command_topic"] = kMqttOpenCommandTopic;
    root["payload_press"] = payloadPress;
    addHaDevice(root);

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    s_mqtt.publish(discoveryTopic, (const uint8_t *)buf, n, true);
}

// Retained discovery configs, republished on every (re)connect — cheap, and
// exactly how HA expects to pick up entities after it (or the broker)
// restarts and loses whatever it had cached.
static void publishHaDiscovery() {
    {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["name"] = "Access";
        root["unique_id"] = "openparcelbox_access_event";
        root["state_topic"] = kMqttEventTopic;
        JsonArray types = root["event_types"].to<JsonArray>();
        types.add("opened");
        types.add("denied");
        root["value_template"] = "{{ value_json.event_type }}";
        root["json_attributes_topic"] = kMqttEventTopic;
        addHaDevice(root);

        char buf[512];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        s_mqtt.publish(kHaEventDiscoveryTopic, (const uint8_t *)buf, n, true);
    }
    {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["name"] = "Last Opened By";
        root["unique_id"] = "openparcelbox_last_access";
        root["state_topic"] = kMqttLastTopic;
        root["value_template"] = "{{ value_json.id }}";
        root["json_attributes_topic"] = kMqttLastTopic;
        addHaDevice(root);

        char buf[512];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        s_mqtt.publish(kHaSensorDiscoveryTopic, (const uint8_t *)buf, n, true);
    }
    {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["name"] = "Letter Flap";
        root["unique_id"] = "openparcelbox_letter_flap";
        root["device_class"] = "opening";
        root["state_topic"] = kMqttLetterFlapTopic;
        root["payload_on"] = "open";
        root["payload_off"] = "closed";
        addHaDevice(root);

        char buf[512];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        s_mqtt.publish(kHaLetterFlapDiscoveryTopic, (const uint8_t *)buf, n, true);
    }
    {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["name"] = "Inbox";
        root["unique_id"] = "openparcelbox_inbox";
        root["icon"] = "mdi:mailbox";
        root["unit_of_measurement"] = "items";
        root["state_topic"] = kMqttInboxTopic;
        root["value_template"] = "{{ value_json.count }}";
        root["json_attributes_topic"] = kMqttInboxTopic;
        addHaDevice(root);

        char buf[512];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        s_mqtt.publish(kHaInboxDiscoveryTopic, (const uint8_t *)buf, n, true);
    }
    publishHaButtonDiscovery("homeassistant/button/openparcelbox/open_full/config",
                              "Open Full", "openparcelbox_open_full", "full");
    publishHaButtonDiscovery("homeassistant/button/openparcelbox/open_parcel/config",
                              "Open Parcel", "openparcelbox_open_parcel", "parcel");
}

// Publishes to the live Event topic (not retained — a stale replay on
// reconnect would otherwise look like a fresh notification). Grants also go
// to the persistent Sensor topic (retained, so "last opened by" survives
// restarts) — denials deliberately don't touch it, since "last opened by"
// should only ever reflect an actual grant.
// Returns false if any required publish failed (e.g. connection dropped
// right before/during the call) so the caller can retry instead of silently
// losing the event — this is the one message type we can't afford to drop,
// since it's the only record of who opened (or tried to open) the box.
static bool publishAccessEvent(const AppMessage &msg) {
    bool granted = msg.event == AppEvent::ACCESS_PARCELBOX_GRANTED;

    JsonDocument doc;
    doc["event_type"] = granted ? "opened" : "denied";
    doc["id"] = msg.accessId;
    doc["scope"] = (msg.scope == AccessScope::FULL) ? "full" : "parcel";

    char buf[192];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    bool ok = s_mqtt.publish(kMqttEventTopic, (const uint8_t *)buf, n, false);
    if (granted) {
        ok = s_mqtt.publish(kMqttLastTopic, (const uint8_t *)buf, n, true) && ok;
    }
    return ok;
}

static bool publishLetterFlapState() {
    if (s_letterFlapState < 0) {
        return true; // nothing to report yet
    }
    return s_mqtt.publish(kMqttLetterFlapTopic, s_letterFlapState ? "open" : "closed", true);
}

// Streamed via beginPublish() rather than publish(): the document can be
// larger than PubSubClient's 1024-byte buffer, which publish() can't exceed.
static void republishInbox() {
    static char buf[INBOX_JSON_MAX_LEN];
    size_t n = inboxSerialize(buf, sizeof(buf));
    if (s_mqtt.beginPublish(kMqttInboxTopic, n, true)) {
        s_mqtt.write(reinterpret_cast<const uint8_t *>(buf), n);
        s_mqtt.endPublish();
    }
}

// Kicks off (or re-kicks) the STA connection and returns immediately —
// callers poll WiFi.status() themselves rather than blocking here, since a
// dropped connection needs to be retried from the main loop, not just once
// at boot.
static void beginWifi(const DeviceConfig &cfg) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
}

// Kicks off the SNTP background sync exactly once (the system clock persists
// across WiFi drops/MQTT reconnects as long as the board stays powered, so
// there's no need to redo this on every reconnect). log.cpp's
// formatTimestamp() falls back to an uptime string on its own until this has
// actually completed, so nothing here needs to block waiting for it.
static void startNtpSyncOnce() {
    static bool s_started = false;
    if (s_started) {
        return;
    }
    s_started = true;
    configTime(0, 0, kNtpServer);
}

// Polls WiFi.status() every call (cheap) and re-issues WiFi.begin() on a
// backoff once the link has been down for a while. This exists because
// ESP32's own auto-reconnect isn't reliable for every disconnect reason
// (e.g. beacon timeout/auth expiry from a router reboot or brief outage can
// leave the radio simply not retrying) — logged here previously as a
// dropped access event never reaching MQTT because the box was silently
// stuck offline until the next manual reboot.
static void ensureWifiConnected(const DeviceConfig &cfg) {
    static bool s_wasConnected = true; // assume connected until proven otherwise
    static uint32_t s_downSinceMs = 0;
    static uint32_t s_lastRetryMs = 0;
    static const uint32_t kRetryIntervalMs = 10000;

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        if (!s_wasConnected) {
            logError("configSyncTaskFn: WiFi reconnected");
        }
        s_wasConnected = true;
        s_downSinceMs = 0;
        startNtpSyncOnce();
        return;
    }

    if (s_wasConnected) {
        // Logged once at the transition (not on every retry below) so a
        // long outage leaves one error-log entry, not hundreds.
        logError("configSyncTaskFn: WiFi link lost, will retry");
        s_wasConnected = false;
        s_downSinceMs = millis();
        s_lastRetryMs = 0; // retry immediately on the first drop
    }

    if (millis() - s_lastRetryMs >= kRetryIntervalMs) {
        Serial.printf("configSyncTaskFn: WiFi down %lus, retrying\n",
                       (millis() - s_downSinceMs) / 1000);
        s_lastRetryMs = millis();
        beginWifi(cfg);
    }
}

// Streams the whole file straight from LittleFS into the MQTT payload via
// beginPublish()/write()/endPublish() rather than reading it into a RAM
// buffer first — these files are allowed to grow up to 16KB (see log.cpp's
// trim policy) which would otherwise mean a 16KB heap allocation here on
// every single access/denial/error. Retained, so a client that subscribes
// at any later point immediately gets the file's current full content
// instead of waiting for the next change.
static void republishLogFile(const char *path, const char *topic) {
    // Must hold the same mutex appendLine()/trimLogIfNeeded() use (see
    // log.cpp) for the whole read — otherwise a trim's truncate-and-rewrite
    // landing mid-stream changes the file out from under us after we've
    // already told MQTT how many bytes to expect via beginPublish(),
    // desyncing that connection's packet framing.
    if (xSemaphoreTake(logFileMutex(), pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        xSemaphoreGive(logFileMutex());
        return; // nothing written yet
    }

    if (s_mqtt.beginPublish(topic, f.size(), true)) {
        uint8_t buf[256];
        int n;
        while ((n = f.read(buf, sizeof(buf))) > 0) {
            s_mqtt.write(buf, n);
        }
        s_mqtt.endPublish();
    }
    f.close();
    xSemaphoreGive(logFileMutex());
}

// Single non-blocking connect attempt — never loops/delays itself, so it
// can be called every iteration of the main loop without ever stalling
// s_mqtt.loop() or WiFi reconnection behind a long retry sleep.
static void tryConnectMqttOnce(const DeviceConfig &cfg) {
    static uint32_t s_lastAttemptMs = 0;
    static const uint32_t kRetryIntervalMs = 2000;
    // Only the first failure after a connected state is logError'd (persisted
    // + queued for MQTT) — every retry after that is still printed to Serial
    // for live debugging, but not written to /error.log, or a long outage
    // would fill it with a few hundred identical "connect failed" lines.
    static bool s_wasConnected = true;

    if (millis() - s_lastAttemptMs < kRetryIntervalMs) {
        return;
    }
    s_lastAttemptMs = millis();

    // A failed low-level TCP connect doesn't get cleaned up by PubSubClient
    // itself (it sets state MQTT_CONNECT_FAILED without calling stop()) —
    // on ESP32's small fixed LWIP socket pool, retrying every couple of
    // seconds without explicitly releasing the socket first leaks one per
    // attempt until every socket is exhausted and every future connect()
    // fails immediately, which is indistinguishable from this rc=-2 loop
    // persisting for hours/days despite Wi-Fi never dropping.
    s_wifiClient.stop();

    if (s_mqtt.connect(kMqttClientId, cfg.mqttUsername, cfg.mqttPassword)) {
        Serial.println("configSyncTaskFn: MQTT connected");
        s_mqtt.subscribe(kMqttConfigTopic);
        s_mqtt.subscribe(kMqttOpenCommandTopic);
        publishHaDiscovery();
        // Retained publishes only reach subscribers once the broker has
        // actually seen one — republish all three unconditionally here so a
        // client connecting right after boot (before any new access/error
        // event) still gets the logs' current full content immediately,
        // not just from whatever change happens to come next.
        republishLogFile(kErrorLogPath, kMqttErrorLogTopic);
        republishLogFile(kAccessLogPath, kMqttAccessLogTopic);
        republishLogFile(kDeniedLogPath, kMqttDeniedLogTopic);
        republishInbox();
        publishLetterFlapState();
        s_wasConnected = true;
    } else {
        if (s_wasConnected) {
            logError("configSyncTaskFn: MQTT connect failed, rc=%d", s_mqtt.state());
        } else {
            Serial.printf("configSyncTaskFn: MQTT connect failed, rc=%d\n", s_mqtt.state());
        }
        s_wasConnected = false;
    }
}

// Best-effort: a failed/incomplete publish here is simply dropped — the
// durable copy already landed in the matching .log file by the time
// log.cpp marked it dirty, so this is a live mirror, not the record of
// truth, and the next change will trigger another attempt anyway.
static void publishDirtyLogFiles() {
    if (!s_mqtt.connected()) {
        return;
    }
    uint8_t dummy;
    if (xQueueReceive(errorLogDirtyQueue, &dummy, 0) == pdTRUE) {
        republishLogFile(kErrorLogPath, kMqttErrorLogTopic);
    }
    if (xQueueReceive(accessLogDirtyQueue, &dummy, 0) == pdTRUE) {
        republishLogFile(kAccessLogPath, kMqttAccessLogTopic);
    }
    if (xQueueReceive(deniedLogDirtyQueue, &dummy, 0) == pdTRUE) {
        republishLogFile(kDeniedLogPath, kMqttDeniedLogTopic);
    }
    // Same best-effort reasoning: the inbox is persisted on-device, and is
    // republished in full on every reconnect anyway.
    if (xQueueReceive(inboxDirtyQueue, &dummy, 0) == pdTRUE) {
        republishInbox();
    }
}

void configSyncTaskFn(void *pvParameters) {
    DeviceConfig devCfg{};
    if (!loadDeviceConfig(devCfg) || !devCfg.provisioned) {
        // Nothing to connect to yet — the Settings screen writes this file
        // and reboots once WiFi/MQTT credentials are entered, so this task
        // just has nothing to do until then rather than looping/retrying.
        Serial.println("configSyncTaskFn: device not provisioned yet, idling");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    // Deliberately does not block logicTask's own LittleFS-first load — this
    // task connects independently and only ever pushes a *newer* config.
    //
    // Goes through ensureWifiConnected() (same retry/backoff as the main
    // loop below) rather than a bare beginWifi() + wait — a single attempt
    // here previously just hung forever if it didn't succeed, with nothing
    // ever calling WiFi.begin() again. That's exactly what an abrupt
    // watchdog-triggered reboot can trigger: no clean WiFi.disconnect()
    // happened first, so the router may still be expiring the old
    // association for a bit and flub/ignore the very next join attempt.
    while (WiFi.status() != WL_CONNECTED) {
        ensureWifiConnected(devCfg);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    startNtpSyncOnce();

    s_mqtt.setServer(devCfg.mqttHost, kMqttPort);
    s_mqtt.setCallback(mqttCallback);
    s_mqtt.setBufferSize(1024); // payloads carry multiple PINs/BLE MACs
    // PubSubClient::connect() busy-waits for the broker's CONNACK with no
    // delay/yield in the loop (see PubSubClient.cpp), bounded only by this
    // timeout — the library default (15s) is long enough to starve Core 0's
    // idle task past ESP-IDF's ~5s task watchdog and reboot the device (seen
    // live: a connection-reset right before a reconnect attempt led to
    // exactly this crash). Shortening it only affects this wait and a couple
    // of other socket read timeouts, not the normal keep-alive ping interval,
    // so there's no real downside on a local network.
    s_mqtt.setSocketTimeout(3);

    AppMessage msg;
    bool haveMsg = false;
    for (;;) {
        // WiFi is checked/repaired unconditionally, every iteration — not
        // just when MQTT looks disconnected — because a dropped link needs
        // its own retry loop independent of whatever MQTT is doing.
        ensureWifiConnected(devCfg);

        if (WiFi.status() == WL_CONNECTED) {
            if (!s_mqtt.connected()) {
                tryConnectMqttOnce(devCfg);
            }
            s_mqtt.loop();
            publishDirtyLogFiles();
        }
        // Read once per iteration, right after the connect attempt/loop()
        // above, so it reflects broker-initiated drops too, not just our
        // own connect attempts.
        mqttConnected = s_mqtt.connected();

        // Only pull a new message off the queue once any previous one has
        // actually been published — otherwise a publish failure right after
        // a reconnect (or a connection that drops mid-publish) would drop
        // the event on the floor instead of retrying it, which is exactly
        // how a real access event previously went unreported to MQTT.
        if (!haveMsg) {
            haveMsg = xQueueReceive(mqttQueue, &msg, 0) == pdTRUE;
        }

        if (haveMsg) {
            bool done;
            switch (msg.event) {
                case AppEvent::ACCESS_PARCELBOX_GRANTED:
                case AppEvent::ACCESS_PARCELBOX_DENIED:
                    done = s_mqtt.connected() && publishAccessEvent(msg);
                    break;
                case AppEvent::LETTER_FLAP_CHANGED:
                    // Recorded even while offline, so the reconnect path
                    // above publishes the latest state.
                    s_letterFlapState = msg.flapOpen ? 1 : 0;
                    done = s_mqtt.connected() && publishLetterFlapState();
                    break;
                default:
                    done = true; // not an event type this task publishes
                    break;
            }
            // !done: leave haveMsg set, retry next loop once (re)connected.
            if (done) {
                haveMsg = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

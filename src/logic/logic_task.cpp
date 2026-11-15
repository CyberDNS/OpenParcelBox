#include "logic_task.h"

#include <Arduino.h>
#include <string.h>

#include "access_control.h"
#include "events.h"
#include "inbox.h"
#include "letter_flap_config.h"
#include "../logging/log.h"

static AccessConfig s_config{};

// Unknown until letterFlapTask reports its startup state, so a flap that's
// already open at boot isn't counted as a delivery.
static bool s_flapKnown = false;
static bool s_flapOpen = false;
static bool s_haveFlapActivity = false;
static uint32_t s_lastFlapActivityMs = 0;

// A parcel-only grant means something was (probably) dropped off; a full
// grant means the resident is emptying the box, so the list starts over.
static void updateInboxForGrant(const AppMessage &grant) {
    if (grant.scope == AccessScope::FULL) {
        inboxClear();
    } else {
        inboxAdd(InboxItemType::PARCEL, grant.accessId);
    }
}

static void handleLetterFlap(const AppMessage &msg) {
    bool wasClosed = s_flapKnown && !s_flapOpen;
    s_flapKnown = true;
    s_flapOpen = msg.flapOpen;

    uint32_t now = millis();
    if (msg.flapOpen && wasClosed) {
        // Measured from the last change in either direction, not the last
        // opening: a flap held open for a while and then bouncing open
        // again as it shuts must still count as one delivery.
        if (!s_haveFlapActivity || now - s_lastFlapActivityMs >= LETTER_FLAP_COALESCE_MS) {
            inboxAdd(InboxItemType::LETTER, nullptr);
        }
    }
    s_haveFlapActivity = true;
    s_lastFlapActivityMs = now;

    xQueueSend(mqttQueue, &msg, 0);
}

static void handleMessage(const AppMessage &msg) {
    switch (msg.event) {
        case AppEvent::PIN_SUBMITTED: {
            // TODO: rate-limit/backoff repeated failed attempts.
            AccessScope scope = AccessScope::PARCEL_ONLY;
            char accessId[ACCESS_ID_MAX_LEN + 1] = {0};
            bool ok = validatePin(s_config, msg.pin, scope, accessId);
            AppMessage out{};
            out.event = ok ? AppEvent::ACCESS_PARCELBOX_GRANTED
                           : AppEvent::ACCESS_PARCELBOX_DENIED;
            out.scope = scope;
            strlcpy(out.accessId, accessId, sizeof(out.accessId));
            xQueueSend(uiQueue, &out, 0);
            logAccess(accessId, scope, ok);
            if (ok) {
                xQueueSend(servoQueue, &out, 0);
                updateInboxForGrant(out);
            }
            xQueueSend(mqttQueue, &out, 0);
            break;
        }
        case AppEvent::BLE_PROXIMITY_DETECTED: {
            // TODO: BLE unlock is a best-effort convenience feature only —
            // confirm allow-listed MAC before granting (msg carries no MAC
            // field yet; extend AppMessage if bleTask needs to pass one).
            // FULL scope: BLE proximity is tied to the resident's own phone,
            // not a courier's, so it should behave like the resident's PIN.
            // TODO: "BLE" is a placeholder id — once the allow-list actually
            // checks a specific MAC, report which registered phone matched.
            AppMessage out{};
            out.event = AppEvent::ACCESS_PARCELBOX_GRANTED;
            out.scope = AccessScope::FULL;
            strlcpy(out.accessId, "BLE", sizeof(out.accessId));
            xQueueSend(uiQueue, &out, 0);
            xQueueSend(servoQueue, &out, 0);
            xQueueSend(mqttQueue, &out, 0);
            logAccess(out.accessId, out.scope, true);
            updateInboxForGrant(out);
            break;
        }
        case AppEvent::REMOTE_OPEN_REQUESTED: {
            // Trusted source — reaching this topic at all already implies
            // MQTT broker access/authorization, so no PIN check here, just
            // relay the requested scope as a grant through the same pipeline
            // as PIN/BLE (same UI feedback, same "who opened it" reporting).
            AppMessage out{};
            out.event = AppEvent::ACCESS_PARCELBOX_GRANTED;
            out.scope = msg.scope;
            strlcpy(out.accessId, "Home Assistant", sizeof(out.accessId));
            xQueueSend(uiQueue, &out, 0);
            xQueueSend(servoQueue, &out, 0);
            xQueueSend(mqttQueue, &out, 0);
            logAccess(out.accessId, out.scope, true);
            updateInboxForGrant(out);
            break;
        }
        case AppEvent::CONFIG_UPDATED: {
            AccessConfig reloaded;
            if (loadConfigFromLittleFS(reloaded)) {
                s_config = reloaded;
            }
            AppMessage out{};
            out.event = AppEvent::CONFIG_UPDATED;
            xQueueSend(uiQueue, &out, 0);
            break;
        }
        case AppEvent::LETTER_FLAP_CHANGED:
            handleLetterFlap(msg);
            break;
        default:
            break;
    }
}

void logicTaskFn(void *pvParameters) {
    // Load whatever config is on disk (if any) before anything else, so the
    // box works offline immediately. LittleFS itself is mounted in main's
    // setup(), before tasks are spawned.
    if (!loadConfigFromLittleFS(s_config)) {
        logError("logicTaskFn: no valid config on LittleFS yet, using dev default PIN 123456");
        s_config.unlockDurationMs = 5000;
        // TODO: remove once configSyncTask/provisioning can actually populate
        // a real config — this only exists so the PIN flow is testable on a
        // freshly flashed board with no config.json yet.
        s_config.pinCount = 1;
        strlcpy(s_config.pins[0].id, "dev", sizeof(s_config.pins[0].id));
        strlcpy(s_config.pins[0].pin, "123456", sizeof(s_config.pins[0].pin));
        s_config.pins[0].scope = AccessScope::FULL;
    }

    AppMessage msg;
    for (;;) {
        if (xQueueReceive(logicQueue, &msg, portMAX_DELAY) == pdTRUE) {
            handleMessage(msg);
        }
    }
}

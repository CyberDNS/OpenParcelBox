#include "access_control.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

static const char *kConfigPath = "/config.json";

static bool parseConfigJson(const char *json, size_t len, AccessConfig &out) {
    // TODO: size this to the actual expected payload; MAX_PINS/MAX_BLE_ALLOWED
    // worth of entries plus JSON overhead.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        return false;
    }

    AccessConfig parsed{};
    parsed.version = doc["version"] | 0;
    parsed.unlockDurationMs = doc["unlock_duration_ms"] | 5000;

    JsonArrayConst pins = doc["pins"];
    parsed.pinCount = 0;
    for (JsonObjectConst p : pins) {
        if (parsed.pinCount >= MAX_PINS) break;
        PinEntry &entry = parsed.pins[parsed.pinCount++];
        strlcpy(entry.id, p["id"] | "", sizeof(entry.id));
        strlcpy(entry.pin, p["pin"] | "", sizeof(entry.pin));
        // Unrecognized/missing scope defaults to the least-privileged option.
        const char *scopeStr = p["scope"] | "parcel";
        entry.scope = (strcmp(scopeStr, "full") == 0) ? AccessScope::FULL : AccessScope::PARCEL_ONLY;
    }

    JsonArrayConst bleAllowed = doc["ble_allowed"];
    parsed.bleAllowedCount = 0;
    for (JsonVariantConst mac : bleAllowed) {
        if (parsed.bleAllowedCount >= MAX_BLE_ALLOWED) break;
        strlcpy(parsed.bleAllowed[parsed.bleAllowedCount++], mac.as<const char *>(),
                BLE_MAC_LEN + 1);
    }

    out = parsed;
    return true;
}

bool loadConfigFromLittleFS(AccessConfig &out) {
    if (!LittleFS.exists(kConfigPath)) {
        return false;
    }

    File f = LittleFS.open(kConfigPath, "r");
    if (!f) {
        return false;
    }

    String contents = f.readString();
    f.close();

    return parseConfigJson(contents.c_str(), contents.length(), out);
}

bool applyAndPersistConfig(const char *json, size_t len, AccessConfig &out) {
    AccessConfig parsed;
    if (!parseConfigJson(json, len, parsed)) {
        return false;
    }

    File f = LittleFS.open(kConfigPath, "w");
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const uint8_t *>(json), len);
    f.close();

    out = parsed;
    return true;
}

bool validatePin(const AccessConfig &config, const char *pin, AccessScope &scopeOut, char *idOut) {
    for (uint8_t i = 0; i < config.pinCount; i++) {
        if (strncmp(config.pins[i].pin, pin, PIN_LENGTH) == 0) {
            scopeOut = config.pins[i].scope;
            strlcpy(idOut, config.pins[i].id, ACCESS_ID_MAX_LEN + 1);
            return true;
        }
    }
    return false;
}

bool isBleMacAllowed(const AccessConfig &config, const char *mac) {
    for (uint8_t i = 0; i < config.bleAllowedCount; i++) {
        if (strncmp(config.bleAllowed[i], mac, BLE_MAC_LEN) == 0) {
            return true;
        }
    }
    return false;
}

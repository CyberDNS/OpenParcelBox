#include "device_config.h"

#include <string.h>

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "secure_store.h"

static const char *kDeviceConfigPath = "/device_config.json";

static_assert(WIFI_PASSWORD_MAX_LEN <= 64 && MQTT_PASSWORD_MAX_LEN <= 64,
              "secure_store.cpp's kSecretPlainMaxLen assumes both password fields fit in 64 bytes");

bool loadDeviceConfig(DeviceConfig &out) {
    if (!LittleFS.exists(kDeviceConfigPath)) {
        return false;
    }

    File f = LittleFS.open(kDeviceConfigPath, "r");
    if (!f) {
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        return false;
    }

    DeviceConfig parsed{};
    strlcpy(parsed.wifiSsid, doc["wifi_ssid"] | "", sizeof(parsed.wifiSsid));
    strlcpy(parsed.mqttHost, doc["mqtt_host"] | "", sizeof(parsed.mqttHost));
    strlcpy(parsed.mqttUsername, doc["mqtt_username"] | "", sizeof(parsed.mqttUsername));
    strlcpy(parsed.adminPin, doc["admin_pin"] | "", sizeof(parsed.adminPin));
    parsed.provisioned = doc["provisioned"] | false;

    // wifi_password/mqtt_password are stored encrypted (see secure_store.h) —
    // a decrypt failure most likely means this file was copied onto a
    // different board (the key is derived from this chip's eFuse MAC), which
    // is indistinguishable from corruption from here; either way there's no
    // password we can recover, so the field is left blank rather than
    // failing the whole config load.
    if (!decryptSecret(doc["wifi_password"] | "", parsed.wifiPassword, sizeof(parsed.wifiPassword))) {
        parsed.wifiPassword[0] = '\0';
    }
    if (!decryptSecret(doc["mqtt_password"] | "", parsed.mqttPassword, sizeof(parsed.mqttPassword))) {
        parsed.mqttPassword[0] = '\0';
    }

    out = parsed;
    return true;
}

bool saveDeviceConfig(const DeviceConfig &config) {
    // encryptSecret() returning "" for a non-empty input means it failed
    // (see secure_store.h) — must not persist that as silent password loss.
    String wifiPasswordEnc = encryptSecret(config.wifiPassword);
    String mqttPasswordEnc = encryptSecret(config.mqttPassword);
    if ((config.wifiPassword[0] && wifiPasswordEnc.length() == 0) ||
        (config.mqttPassword[0] && mqttPasswordEnc.length() == 0)) {
        return false;
    }

    JsonDocument doc;
    doc["wifi_ssid"] = config.wifiSsid;
    doc["wifi_password"] = wifiPasswordEnc;
    doc["mqtt_host"] = config.mqttHost;
    doc["mqtt_username"] = config.mqttUsername;
    doc["mqtt_password"] = mqttPasswordEnc;
    doc["admin_pin"] = config.adminPin;
    doc["provisioned"] = config.provisioned;

    File f = LittleFS.open(kDeviceConfigPath, "w");
    if (!f) {
        return false;
    }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

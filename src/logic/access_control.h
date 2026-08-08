#pragma once

#include <stdint.h>

#include "events.h"

#define MAX_PINS 16
#define MAX_BLE_ALLOWED 8
#define BLE_MAC_LEN 17 // "AA:BB:CC:DD:EE:FF" + '\0' below

struct PinEntry {
    char id[ACCESS_ID_MAX_LEN + 1];
    char pin[PIN_LENGTH + 1];
    AccessScope scope;
};

struct AccessConfig {
    uint32_t version;
    PinEntry pins[MAX_PINS];
    uint8_t pinCount;
    char bleAllowed[MAX_BLE_ALLOWED][BLE_MAC_LEN + 1];
    uint8_t bleAllowedCount;
    uint32_t unlockDurationMs;
};

// Loads config from LittleFS into `out`. Returns false (and leaves `out`
// untouched) if no config file exists yet or it fails to parse — callers
// should fall back to safe defaults in that case.
bool loadConfigFromLittleFS(AccessConfig &out);

// Parses `json`, and only on successful parse, writes it to LittleFS and
// populates `out`. Does not touch the existing file on parse failure.
bool applyAndPersistConfig(const char *json, size_t len, AccessConfig &out);

// True if `pin` matches any entry in `config.pins`, and on match writes that
// entry's scope to `scopeOut` and id (e.g. a courier name) to `idOut` — idOut
// must be at least ACCESS_ID_MAX_LEN + 1 bytes.
bool validatePin(const AccessConfig &config, const char *pin, AccessScope &scopeOut, char *idOut);

// True if `mac` is present in `config.bleAllowed`.
bool isBleMacAllowed(const AccessConfig &config, const char *mac);

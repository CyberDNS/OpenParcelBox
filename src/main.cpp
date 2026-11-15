#include <Arduino.h>
#include <LittleFS.h>

#include "events.h"
#include "version.h"

#include "ble/ble_task.h"
#include "config/config_sync_task.h"
#include "logging/log.h"
#include "logic/inbox.h"
#include "logic/logic_task.h"
#include "sensor/letter_flap_task.h"
#include "servo/servo_task.h"
#include "ui/ui_task.h"

QueueHandle_t logicQueue = nullptr;
QueueHandle_t uiQueue = nullptr;
QueueHandle_t servoQueue = nullptr;
QueueHandle_t mqttQueue = nullptr;

static const uint8_t kQueueDepth = 8;

void setup() {
    Serial.begin(115200);
    Serial.printf("OpenParcelBox firmware %s\n", FIRMWARE_VERSION);

    // Before the LittleFS.begin() check below, so a mount failure itself
    // can be reported through logError() rather than only Serial.
    logInit();

    // Mounted here, before any task starts, so logicTask's LittleFS-first
    // config load never races the filesystem's own init.
    if (!LittleFS.begin(true)) {
        logError("setup: LittleFS mount failed");
    }
    inboxInit();

    logicQueue = xQueueCreate(kQueueDepth, sizeof(AppMessage));
    uiQueue = xQueueCreate(kQueueDepth, sizeof(AppMessage));
    servoQueue = xQueueCreate(kQueueDepth, sizeof(AppMessage));
    mqttQueue = xQueueCreate(kQueueDepth, sizeof(AppMessage));

    // Core 1: UI/LVGL, higher priority — must never be starved.
    xTaskCreatePinnedToCore(uiTaskFn, "uiTask", 8192, nullptr, 3, nullptr, 1);

    // Core 0: networking/BLE/logic.
    xTaskCreatePinnedToCore(logicTaskFn, "logicTask", 4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(servoTaskFn, "servoTask", 2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(letterFlapTaskFn, "letterFlapTask", 2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(bleTaskFn, "bleTask", 4096, nullptr, 1, nullptr, 0);
    // 4096 wasn't enough here: WiFi.begin()/connect's underlying esp-idf/
    // lwIP call stack overflowed it (Guru Meditation: Stack canary watchpoint
    // triggered). WiFi-connecting tasks routinely need more than the default.
    xTaskCreatePinnedToCore(configSyncTaskFn, "configSyncTask", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
    vTaskDelete(NULL);
}

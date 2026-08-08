#pragma once

// Task entry point (Core 0). Central decision-maker: owns the active
// AccessConfig, validates PIN/BLE events from logicQueue, and publishes
// grant/deny events to uiQueue and servoQueue. Spawn via
// xTaskCreatePinnedToCore, never call directly.
void logicTaskFn(void *pvParameters);

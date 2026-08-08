#pragma once

// Task entry point (Core 0). Runs a continuous NimBLE scan, tracks a rolling
// RSSI window for a known phone address, and sends BLE_PROXIMITY_DETECTED to
// logicQueue when it's judged "near". Convenience-only, not security-critical.
// Spawn via xTaskCreatePinnedToCore, never call directly.
void bleTaskFn(void *pvParameters);

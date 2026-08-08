#pragma once

// Task entry point (Core 0). Owns the lock servo; knows nothing about PINs,
// BLE, or the UI — only reacts to ACCESS_PARCELBOX_GRANTED on servoQueue.
// Spawn via xTaskCreatePinnedToCore, never call directly.
void servoTaskFn(void *pvParameters);

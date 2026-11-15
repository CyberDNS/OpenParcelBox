#pragma once

// Task entry point (Core 0). Polls the letter flap reed contact, debounces
// it, and sends LETTER_FLAP_CHANGED to logicQueue on every stable change
// (plus once at startup with the initial state). Spawn via
// xTaskCreatePinnedToCore, never call directly.
void letterFlapTaskFn(void *pvParameters);

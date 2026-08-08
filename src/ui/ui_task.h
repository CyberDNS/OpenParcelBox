#pragma once

#include <lvgl.h>

enum class UiScreen {
    HOME,
    PIN_ENTRY,
    UNLOCKING,
    ERROR,
    SETTINGS,
};

// Tears down the current screen (lv_obj_clean on lv_scr_act()) and builds
// the requested one via its buildXScreen() function. Only ever call this
// from uiTask's own thread of execution (i.e. from an LVGL event callback
// or from uiTaskFn itself) — LVGL objects are not thread-safe.
void switchScreen(UiScreen screen);

// Task entry point (Core 1, high priority). Owns all LVGL objects; runs
// lv_timer_handler() in a tight non-blocking loop and drains uiQueue with
// a zero timeout each iteration. Spawn via xTaskCreatePinnedToCore, never
// call directly.
void uiTaskFn(void *pvParameters);

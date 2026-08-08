#pragma once

#include <lvgl.h>

// Feedback screen shown after logicTask sends ACCESS_PARCELBOX_GRANTED, built
// inside `parent` (the shared content area, already inset below the bezel).
void buildUnlockingScreen(lv_obj_t *parent);

#pragma once

#include <lvgl.h>

// Feedback screen shown after logicTask sends ACCESS_PARCELBOX_DENIED, built
// inside `parent` (the shared content area, already inset below the bezel).
void buildErrorScreen(lv_obj_t *parent);

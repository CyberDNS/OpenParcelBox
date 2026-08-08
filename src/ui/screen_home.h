#pragma once

#include <lvgl.h>

// Builds the HOME screen inside `parent` (the shared content area, already
// inset below the box's bezel — see ui_layout_config.h): background
// illustration, name label, "tap to unlock" hint. The whole surface is
// clickable and transitions to PIN_ENTRY on tap.
void buildHomeScreen(lv_obj_t *parent);

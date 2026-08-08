#pragma once

#include <lvgl.h>

// Board bring-up (SPI display + I2C touch + IO expander reset), ported from
// the vendor demo: Arduino/examples/11_lvgl_arduino_v8/11_lvgl_arduino_v8.ino
//
// Call displayDriverInit() once from uiTask before the first lv_timer_handler()
// call. It initializes Wire, the TCA9554 (for LCD reset), the ST7796 panel,
// the FT6336 touch controller, and registers both with LVGL.
void displayDriverInit();

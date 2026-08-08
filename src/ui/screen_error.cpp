#include "screen_error.h"

#include <lvgl.h>

void buildErrorScreen(lv_obj_t *parent) {
    lv_obj_t *scr = parent;

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, LV_SYMBOL_CLOSE " Access denied");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE04444), 0);
    lv_obj_center(label);
}

#include "screen_unlocking.h"

#include <lvgl.h>

void buildUnlockingScreen(lv_obj_t *parent) {
    lv_obj_t *scr = parent;

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, LV_SYMBOL_OK " Unlocking...");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
}

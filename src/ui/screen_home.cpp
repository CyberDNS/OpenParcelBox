#include "screen_home.h"

#include <Arduino.h>
#include <lvgl.h>

#include "version.h"
#include "building_config.h"
#include "screen_pin_entry.h"
#include "ui_task.h"

// Hidden admin-settings entry point: long-press the version tag in the
// bottom-right corner (not an obvious button — delivery couriers shouldn't
// be tempted to poke it) to reach the admin PIN screen instead of the
// normal unlock flow.
static const uint32_t kAdminGestureHoldMs = 3000;
static const lv_coord_t kAdminCornerSize = 60;
static uint32_t s_cornerPressStartMs = 0;

// TODO(splash): temporary placeholder splash screen for delivery couriers in
// Germany — all languages shown at once rather than cycling. Plain ASCII
// only — the compiled-in font (see lv_font_montserrat_16.c's generation
// command) has no diacritics, so phrasing avoids them (e.g. German
// "antippen" rather than using u-umlaut).
static const char *const kTapScreenPhrases =
    "Bildschirm antippen\n" // German
    "Tap screen\n"          // English
    "Ekrana dokunun\n"      // Turkish
    "Dotknij ekranu\n"      // Polish
    "Atingeti ecranul";     // Romanian

static void homeClickedCb(lv_event_t *e) {
    setPinEntryMode(PinEntryMode::UNLOCK);
    switchScreen(UiScreen::PIN_ENTRY);
}

static void cornerPressedCb(lv_event_t *e) {
    s_cornerPressStartMs = millis();
}

static void cornerReleasedCb(lv_event_t *e) {
    if (s_cornerPressStartMs != 0 &&
        millis() - s_cornerPressStartMs >= kAdminGestureHoldMs) {
        setPinEntryMode(PinEntryMode::ADMIN);
        switchScreen(UiScreen::PIN_ENTRY);
    }
    s_cornerPressStartMs = 0;
}

// Light gray — dim enough to read as secondary chrome rather than competing
// with the tap-to-open message, but still clearly legible against the
// 0x101418 background (the earlier 0x6B7280 was too close to the
// background to read well on the actual display).
static const uint32_t kMutedTextColor = 0xB8C0CA;

void buildHomeScreen(lv_obj_t *parent) {
    lv_obj_t *scr = parent;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "OpenParcelBox");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text_fmt(label, LV_SYMBOL_ENVELOPE "\n%s", kTapScreenPhrases);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, homeClickedCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *versionLabel = lv_label_create(scr);
    lv_label_set_text(versionLabel, FIRMWARE_VERSION);
    lv_obj_set_style_text_font(versionLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(versionLabel, lv_color_hex(kMutedTextColor), 0);
    lv_obj_align(versionLabel, LV_ALIGN_BOTTOM_RIGHT, -4, -4);

    lv_obj_t *creditsLabel = lv_label_create(scr);
    lv_label_set_text(creditsLabel, BUILDING_RESIDENTS);
    lv_obj_set_style_text_font(creditsLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(creditsLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(creditsLabel, lv_color_hex(kMutedTextColor), 0);
    lv_obj_align(creditsLabel, LV_ALIGN_BOTTOM_LEFT, 4, -4);

    // Transparent, on top (created last), and clickable — catches taps over
    // the version tag before they reach homeClickedCb, so a quick tap does
    // nothing there; only a hold past kAdminGestureHoldMs triggers admin entry.
    lv_obj_t *corner = lv_obj_create(scr);
    lv_obj_set_size(corner, kAdminCornerSize, kAdminCornerSize);
    lv_obj_align(corner, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(corner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(corner, 0, 0);
    lv_obj_clear_flag(corner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(corner, cornerPressedCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(corner, cornerReleasedCb, LV_EVENT_RELEASED, nullptr);
}

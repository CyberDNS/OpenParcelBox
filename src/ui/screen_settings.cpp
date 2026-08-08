#include "screen_settings.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "../config/device_config.h"

static const lv_coord_t kHeaderHeight = 40;
static const lv_coord_t kKeyboardHeight = 160;

static lv_obj_t *s_ssidTa = nullptr;
static lv_obj_t *s_wifiPasswordTa = nullptr;
static lv_obj_t *s_mqttHostTa = nullptr;
static lv_obj_t *s_mqttUsernameTa = nullptr;
static lv_obj_t *s_mqttPasswordTa = nullptr;
static lv_obj_t *s_adminPinTa = nullptr;
static lv_obj_t *s_keyboard = nullptr;

static void textareaFocusedCb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void textareaDefocusedCb(lv_event_t *e) {
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void saveClickedCb(lv_event_t *e) {
    DeviceConfig cfg{};
    strlcpy(cfg.wifiSsid, lv_textarea_get_text(s_ssidTa), sizeof(cfg.wifiSsid));
    strlcpy(cfg.wifiPassword, lv_textarea_get_text(s_wifiPasswordTa), sizeof(cfg.wifiPassword));
    strlcpy(cfg.mqttHost, lv_textarea_get_text(s_mqttHostTa), sizeof(cfg.mqttHost));
    strlcpy(cfg.mqttUsername, lv_textarea_get_text(s_mqttUsernameTa), sizeof(cfg.mqttUsername));
    strlcpy(cfg.mqttPassword, lv_textarea_get_text(s_mqttPasswordTa), sizeof(cfg.mqttPassword));
    strlcpy(cfg.adminPin, lv_textarea_get_text(s_adminPinTa), sizeof(cfg.adminPin));
    cfg.provisioned = true;

    if (saveDeviceConfig(cfg)) {
        // Simplest correct way to apply new WiFi/MQTT credentials — avoids
        // hot-reconfiguring a live WiFiClient/PubSubClient mid-run.
        ESP.restart();
    }
    // TODO: surface a save-failure message on screen instead of silently
    // staying put — LittleFS write failures shouldn't be invisible.
}

static lv_obj_t *buildField(lv_obj_t *parent, const char *label, bool password, uint32_t maxLen) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_max_length(ta, maxLen);
    lv_obj_set_width(ta, LV_PCT(95));
    lv_obj_add_event_cb(ta, textareaFocusedCb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, textareaDefocusedCb, LV_EVENT_DEFOCUSED, nullptr);
    return ta;
}

void buildSettingsScreen(lv_obj_t *parent) {
    lv_obj_t *scr = parent;

    // Fields fill the full content-area height below the header and scroll
    // natively (this is a plain lv_obj with flex column layout, which
    // scrolls by default when content overflows). A blank spacer the height
    // of the keyboard sits at the very end so the last fields can still be
    // scrolled clear of the keyboard when it's shown, instead of being
    // permanently hidden behind it.
    lv_obj_t *form = lv_obj_create(scr);
    lv_obj_set_size(form, LV_PCT(100), lv_obj_get_height(scr) - kHeaderHeight);
    lv_obj_align(form, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);

    s_ssidTa = buildField(form, "WiFi SSID", false, WIFI_SSID_MAX_LEN);
    s_wifiPasswordTa = buildField(form, "WiFi Password", true, WIFI_PASSWORD_MAX_LEN);
    s_mqttHostTa = buildField(form, "MQTT Host", false, MQTT_HOST_MAX_LEN);
    s_mqttUsernameTa = buildField(form, "MQTT Username", false, MQTT_USERNAME_MAX_LEN);
    s_mqttPasswordTa = buildField(form, "MQTT Password", true, MQTT_PASSWORD_MAX_LEN);
    s_adminPinTa = buildField(form, "Admin PIN", true, PIN_LENGTH);

    lv_obj_t *spacer = lv_obj_create(form);
    lv_obj_set_size(spacer, 1, kKeyboardHeight);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    // Pre-fill with existing values so re-entering Settings to change one
    // field doesn't blank out the others.
    DeviceConfig existing{};
    if (loadDeviceConfig(existing)) {
        lv_textarea_set_text(s_ssidTa, existing.wifiSsid);
        lv_textarea_set_text(s_wifiPasswordTa, existing.wifiPassword);
        lv_textarea_set_text(s_mqttHostTa, existing.mqttHost);
        lv_textarea_set_text(s_mqttUsernameTa, existing.mqttUsername);
        lv_textarea_set_text(s_mqttPasswordTa, existing.mqttPassword);
        lv_textarea_set_text(s_adminPinTa, existing.adminPin);
    }

    // Header is created AFTER the form so it always draws on top of it —
    // it previously drew first, and the form (created after, at an
    // overlapping y-position) painted over the bottom of the Save button.
    // Solid background so nothing can show through as the form scrolls
    // beneath it.
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "ParcelBox Setup");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 5, 0);

    lv_obj_t *saveBtn = lv_btn_create(header);
    lv_obj_align(saveBtn, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_add_event_cb(saveBtn, saveClickedCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "Save");

    // Keyboard is created last (topmost of all) so it correctly overlays the
    // fields when shown rather than being drawn under them.
    s_keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(s_keyboard, LV_PCT(100), kKeyboardHeight);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

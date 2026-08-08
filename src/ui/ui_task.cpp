#include "ui_task.h"

#include <Arduino.h>
#include <WiFi.h>

#include "events.h"
#include "ui_layout_config.h"
#include "ui_timing_config.h"
#include "../config/config_sync_task.h"
#include "../config/device_config.h"
#include "../display/display_driver.h"
#include "screen_error.h"
#include "screen_home.h"
#include "screen_pin_entry.h"
#include "screen_settings.h"
#include "screen_unlocking.h"

static UiScreen s_currentScreen = UiScreen::HOME;
static uint32_t s_screenEnteredMs = 0;

// WiFi/MQTT status icons, top-right corner of every screen — rebuilt in
// switchScreen() like everything else, so these pointers only stay valid
// until the next switch; updateStatusIcons() is only ever called from
// uiTaskFn's own loop, same thread that does the rebuilding, so there's no
// stale-pointer race.
static lv_obj_t *s_wifiStatusIcon = nullptr;
static lv_obj_t *s_mqttStatusIcon = nullptr;
static const lv_coord_t kStatusIconGap = 6;
static const uint32_t kStatusConnectedColor = 0x4CAF50;
static const uint32_t kStatusDisconnectedColor = 0xE04444;

static void updateStatusIcons() {
    bool wifiUp = WiFi.status() == WL_CONNECTED;
    lv_obj_set_style_text_color(s_wifiStatusIcon,
                                 lv_color_hex(wifiUp ? kStatusConnectedColor : kStatusDisconnectedColor), 0);
    lv_obj_set_style_text_color(s_mqttStatusIcon,
                                 lv_color_hex(mqttConnected ? kStatusConnectedColor : kStatusDisconnectedColor), 0);
}

// No built-in MQTT glyph in LVGL's symbol font (see lv_symbol_def.h) —
// UPLOAD (a broadcast/publish arrow) reads reasonably as "talking to the
// broker" paired right next to the WiFi glyph.
static void buildStatusIcons(lv_obj_t *content) {
    s_mqttStatusIcon = lv_label_create(content);
    lv_label_set_text(s_mqttStatusIcon, LV_SYMBOL_UPLOAD);
    lv_obj_align(s_mqttStatusIcon, LV_ALIGN_TOP_RIGHT, -4, 4);

    s_wifiStatusIcon = lv_label_create(content);
    lv_label_set_text(s_wifiStatusIcon, LV_SYMBOL_WIFI);
    lv_obj_align_to(s_wifiStatusIcon, s_mqttStatusIcon, LV_ALIGN_OUT_LEFT_MID, -kStatusIconGap, 0);

    updateStatusIcons(); // paint the right colors immediately, not after up to a full poll interval
}

void switchScreen(UiScreen screen) {
    // A fresh screen object per switch, rather than reusing/partially-
    // resetting lv_scr_act() forever. We were bitten by this twice already:
    // first a leaked background color (fixed by resetting styles), then a
    // leaked LV_OBJ_FLAG_CLICKABLE + click handler from HOME that kept
    // firing on every later screen (any tap anywhere would jump to
    // PIN_ENTRY, cutting ERROR/UNLOCKING short). Styles, flags, and event
    // callbacks all live on the object itself — deleting the old object
    // and building on a blank one is the only way to be sure nothing from
    // a previous screen carries forward, instead of chasing each leaked
    // property one at a time.
    lv_obj_t *oldScr = lv_scr_act();
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(scr);

    // Content area inset below the box's bezel (measured with a calibration
    // ruler — see ui_layout_config.h) — every screen builds inside this
    // instead of directly on `scr`, so nothing ever renders in the hidden
    // strip regardless of which screen it is.
    // lv_disp_get_ver_res() queries the display driver's configured
    // resolution directly — unlike lv_obj_get_height(scr) on a screen we
    // *just* created, which isn't reliably resolved yet without a layout
    // pass (see lv_obj_update_layout() call below).
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), lv_disp_get_ver_res(nullptr) - UI_TOP_HIDDEN_PX);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    // Every screen manages its own precise absolute layout — none of them
    // should ever actually scroll at this outer level (a screen with its own
    // deliberately scrollable inner container, like Settings' form, keeps
    // that independently; this only affects `content` itself). Without this,
    // even a rounding-error pixel of overflow from a child silently turns on
    // both scrollbars.
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(content, lv_color_white(), 0);
    // Screens are built synchronously right after this, with no
    // lv_timer_handler() call in between — and mixing a percentage width
    // with a literal height doesn't commit to readable coordinates until an
    // actual layout pass runs. Force that now so buildXScreen() functions
    // (several of which read content's height, e.g. to size a keypad/form)
    // see the real value instead of a stale/zero one.
    lv_obj_update_layout(content);

    // Built before the per-screen switch below so every screen gets it for
    // free — on Settings specifically, its own header (created later, so it
    // draws on top) happens to cover this corner, which is fine: no PIN/HOME
    // screen needs the space back, and Settings has no use for a live status
    // glance while you're mid-edit of the very credentials it reflects.
    buildStatusIcons(content);

    s_currentScreen = screen;
    s_screenEnteredMs = millis();

    switch (screen) {
        case UiScreen::HOME:
            buildHomeScreen(content);
            break;
        case UiScreen::PIN_ENTRY:
            buildPinEntryScreen(content);
            break;
        case UiScreen::UNLOCKING:
            buildUnlockingScreen(content);
            break;
        case UiScreen::ERROR:
            buildErrorScreen(content);
            break;
        case UiScreen::SETTINGS:
            buildSettingsScreen(content);
            break;
    }

    lv_obj_del(oldScr);
}

static void handleLogicMessage(const AppMessage &msg) {
    switch (msg.event) {
        case AppEvent::ACCESS_PARCELBOX_GRANTED:
            switchScreen(UiScreen::UNLOCKING);
            break;
        case AppEvent::ACCESS_PARCELBOX_DENIED:
            switchScreen(UiScreen::ERROR);
            break;
        case AppEvent::CONFIG_UPDATED:
            // TODO: reflect new config (e.g. unlock duration) in the UI if
            // a relevant screen is active.
            break;
        default:
            break;
    }
}

void uiTaskFn(void *pvParameters) {
    displayDriverInit();

    // Unprovisioned devices (fresh flash, no device_config.json yet) skip
    // HOME entirely and land straight in Settings — no admin PIN exists yet
    // to gate it, and there's nothing else useful the box can do offline.
    DeviceConfig devCfg{};
    bool provisioned = loadDeviceConfig(devCfg) && devCfg.provisioned;
    switchScreen(provisioned ? UiScreen::HOME : UiScreen::SETTINGS);

    AppMessage msg;
    static const uint32_t kStatusPollIntervalMs = 500;
    uint32_t lastStatusPollMs = 0;
    for (;;) {
        lv_timer_handler();

        // Never block here — this loop must keep servicing lv_timer_handler().
        if (xQueueReceive(uiQueue, &msg, 0) == pdTRUE) {
            handleLogicMessage(msg);
        }

        if (millis() - lastStatusPollMs >= kStatusPollIntervalMs) {
            lastStatusPollMs = millis();
            updateStatusIcons();
        }

        if (s_currentScreen == UiScreen::PIN_ENTRY &&
            pinEntryIdleMs() > UI_PIN_ENTRY_IDLE_TIMEOUT_MS) {
            switchScreen(UiScreen::HOME);
        } else if (s_currentScreen == UiScreen::ERROR &&
                   millis() - s_screenEnteredMs > UI_ERROR_SCREEN_TIMEOUT_MS) {
            switchScreen(UiScreen::HOME);
        } else if (s_currentScreen == UiScreen::UNLOCKING &&
                   millis() - s_screenEnteredMs > UI_UNLOCKING_SCREEN_TIMEOUT_MS) {
            switchScreen(UiScreen::HOME);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

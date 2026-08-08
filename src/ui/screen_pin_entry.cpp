#include "screen_pin_entry.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "events.h"
#include "ui_task.h"
#include "../config/device_config.h"

// Classic 3x3 phone-keypad digit ordering (1/4/7 etc. aligned in columns,
// matching everyone's muscle memory) plus a full-width 0 row. A 4-columns
// layout was tried first to make buttons bigger, but that broke exactly this
// vertical alignment and read as unintuitive. No OK/submit button: PIN entry
// already auto-validates at PIN_LENGTH digits. Clear lives in its own large
// standalone button beside this grid (see buildPinEntryScreen) rather than
// competing for a grid cell — landscape width goes to that instead.
static const char *kDigitKeypadMap[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "0", "",
};

static const lv_coord_t kDotDiameter = 24;
static const lv_coord_t kDotSpacing = 14;

static lv_obj_t *s_titleLabel = nullptr;
static lv_obj_t *s_pinDots[PIN_LENGTH];
static char s_pinBuf[PIN_LENGTH + 1] = {0};
static uint8_t s_pinLen = 0;
static uint32_t s_lastInputMs = 0;

static PinEntryMode s_pendingMode = PinEntryMode::UNLOCK;
static PinEntryMode s_activeMode = PinEntryMode::UNLOCK;

void setPinEntryMode(PinEntryMode mode) {
    s_pendingMode = mode;
}

// Solid circle either way, just recolored — deliberately not relying on
// border-rendering defaults or bg_opa toggling after lv_obj_remove_style_all,
// which turned out to be exactly the kind of assumption that kept being wrong.
static const uint32_t kDotEmptyColor = 0x3A3F45;
static const uint32_t kDotFilledColor = 0xFFFFFF;

static void updatePinDots() {
    for (uint8_t i = 0; i < PIN_LENGTH; i++) {
        bool filled = i < s_pinLen;
        lv_obj_set_style_bg_color(s_pinDots[i], lv_color_hex(filled ? kDotFilledColor : kDotEmptyColor), 0);
    }
}

// Does NOT touch the dots — buildPinEntryScreen() calls this before the dots
// exist yet, so callers that need the visible reset must call
// updatePinDots() themselves afterward (same as every other call site here).
static void resetPinState() {
    s_pinLen = 0;
    s_pinBuf[0] = '\0';
    s_lastInputMs = millis();
}

static void submitAdminPin() {
    DeviceConfig cfg{};
    bool ok = loadDeviceConfig(cfg) && strncmp(cfg.adminPin, s_pinBuf, PIN_LENGTH) == 0;

    resetPinState();
    updatePinDots();

    // Admin-gate is a purely local/UI concern (which screen you're allowed to
    // see), not access control to the box itself — checked directly against
    // DeviceConfig rather than round-tripping through logicTask.
    switchScreen(ok ? UiScreen::SETTINGS : UiScreen::ERROR);
}

static void submitUnlockPin() {
    AppMessage msg{};
    msg.event = AppEvent::PIN_SUBMITTED;
    strncpy(msg.pin, s_pinBuf, PIN_LENGTH);
    msg.pin[PIN_LENGTH] = '\0';

    // Non-blocking send: logicTask should always be draining this queue, but
    // never let a slow/blocked consumer stall the UI thread.
    xQueueSend(logicQueue, &msg, 0);

    resetPinState();
    updatePinDots();
}

static void submitPin() {
    if (s_activeMode == PinEntryMode::ADMIN) {
        submitAdminPin();
    } else {
        submitUnlockPin();
    }
}

static void clearBtnClickedCb(lv_event_t *e) {
    s_lastInputMs = millis();
    resetPinState();
    updatePinDots();
}

static void keypadEventCb(lv_event_t *e) {
    lv_obj_t *btnm = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(btnm);
    const char *txt = lv_btnmatrix_get_btn_text(btnm, id);

    s_lastInputMs = millis();

    if (s_pinLen < PIN_LENGTH) {
        s_pinBuf[s_pinLen++] = txt[0];
        s_pinBuf[s_pinLen] = '\0';
        updatePinDots();
    }

    if (s_pinLen == PIN_LENGTH) {
        submitPin();
    }
}

void buildPinEntryScreen(lv_obj_t *parent) {
    lv_obj_t *scr = parent;
    resetPinState();

    // Consume the pending mode — reverts to UNLOCK by default so a later,
    // unrelated switchScreen(PIN_ENTRY) call never inherits a stale ADMIN mode.
    s_activeMode = s_pendingMode;
    s_pendingMode = PinEntryMode::UNLOCK;

    static const lv_coord_t kDotsRowY = 20;
    static const lv_coord_t kGapBeforeKeypad = 8;
    // Derived from the actual title/dots geometry below, not guessed
    // separately — this is exactly where the dots row ends, so it can't
    // drift out of sync with it again.
    static const lv_coord_t kTopAreaHeight = kDotsRowY + kDotDiameter + kGapBeforeKeypad;
    static const lv_coord_t kKeypadBottomMargin = 5;

    s_titleLabel = lv_label_create(scr);
    lv_label_set_text(s_titleLabel, s_activeMode == PinEntryMode::ADMIN ? "Admin PIN" : "Enter PIN");
    lv_obj_align(s_titleLabel, LV_ALIGN_TOP_MID, 0, 2);

    // PIN_LENGTH circles, each positioned with an explicit absolute (x,y) —
    // deliberately not a flex-layout row (LV_SIZE_CONTENT auto-sizing is
    // itself layout-resolution-dependent, the same category of bug as the
    // content-height issue fixed earlier in ui_task.cpp) and deliberately
    // direct children of `scr`, not a wrapper container, to remove any
    // dependency on a container's own size being resolved correctly first.
    lv_coord_t totalDotsWidth = PIN_LENGTH * kDotDiameter + (PIN_LENGTH - 1) * kDotSpacing;
    lv_coord_t dotsStartX = (lv_obj_get_width(scr) - totalDotsWidth) / 2;

    for (uint8_t i = 0; i < PIN_LENGTH; i++) {
        lv_obj_t *dot = lv_obj_create(scr);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, kDotDiameter, kDotDiameter);
        lv_obj_set_pos(dot, dotsStartX + i * (kDotDiameter + kDotSpacing), kDotsRowY);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_pinDots[i] = dot;
    }
    updatePinDots();

    lv_coord_t keypadHeight = lv_obj_get_height(scr) - kTopAreaHeight - kKeypadBottomMargin;
    lv_coord_t keypadY = lv_obj_get_height(scr) - keypadHeight - kKeypadBottomMargin;

    // Digit grid gets most of the width; Clear gets a large standalone button
    // beside it rather than a cramped grid cell — a small gap separates them
    // so it reads as a clearly distinct action, not a stray extra digit.
    static const lv_coord_t kGapBeforeClear = 12;
    static const lv_coord_t kClearBtnWidth = 100;
    lv_coord_t digitGridWidth = lv_obj_get_width(scr) - kClearBtnWidth - kGapBeforeClear;

    lv_obj_t *digitGrid = lv_btnmatrix_create(scr);
    lv_btnmatrix_set_map(digitGrid, kDigitKeypadMap);
    lv_obj_set_size(digitGrid, digitGridWidth, keypadHeight);
    // Absolute position, not lv_obj_align(BOTTOM_...) — bottom alignment
    // needs the child's OWN height to place its top edge, which hasn't
    // committed at align-time in this LVGL setup (confirmed by instrumenting
    // this exact screen earlier: every freshly set position/size read back
    // as 0 until an explicit lv_obj_update_layout() forced it). Top/left-
    // anchored placement doesn't have this problem since it doesn't depend
    // on the child's own size, so computing absolute coordinates directly
    // sidesteps it entirely rather than fighting alignment timing again.
    lv_obj_set_pos(digitGrid, 0, keypadY);
    lv_obj_add_event_cb(digitGrid, keypadEventCb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *clearBtn = lv_btn_create(scr);
    lv_obj_set_size(clearBtn, kClearBtnWidth, keypadHeight);
    lv_obj_set_pos(clearBtn, digitGridWidth + kGapBeforeClear, keypadY);
    lv_obj_add_event_cb(clearBtn, clearBtnClickedCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *clearLbl = lv_label_create(clearBtn);
    lv_label_set_text(clearLbl, "C");
    lv_obj_center(clearLbl);

    // Every lv_obj_set_pos/set_size call above only marks its object dirty —
    // none of it actually commits to paintable coordinates until a layout
    // pass runs. Force that resolution now, for this whole screen's subtree,
    // instead of leaving it to whatever LVGL's own refresh cycle would
    // otherwise do before the first paint.
    lv_obj_update_layout(scr);
}

// Only meaningful while PIN_ENTRY is the active screen — the caller (uiTaskFn)
// is responsible for only checking this while that's the case.
uint32_t pinEntryIdleMs() {
    return millis() - s_lastInputMs;
}

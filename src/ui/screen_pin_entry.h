#pragma once

#include <stdint.h>
#include <lvgl.h>

enum class PinEntryMode {
    UNLOCK, // normal delivery flow: submits to logicQueue for validation
    ADMIN,  // hidden gesture flow: checked locally against DeviceConfig's
            // admin PIN, gates entry to the SETTINGS screen
};

// Selects which mode the next buildPinEntryScreen() call operates in.
// Call this before switchScreen(UiScreen::PIN_ENTRY) — defaults to UNLOCK.
void setPinEntryMode(PinEntryMode mode);

// Builds the PIN_ENTRY screen inside `parent` (the shared content area,
// already inset below the box's bezel): numeric lv_btnmatrix keypad, masked
// PIN display, and a "C" clear button. Auto-submits once PIN_LENGTH digits
// have been entered — either to logicQueue (UNLOCK mode) or checked locally
// against the admin PIN (ADMIN mode).
void buildPinEntryScreen(lv_obj_t *parent);

// Milliseconds since the last digit was entered on this screen. Used by
// uiTaskFn to implement the idle-timeout-back-to-HOME behavior. Returns 0
// if the PIN_ENTRY screen isn't currently active.
uint32_t pinEntryIdleMs();

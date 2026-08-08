#pragma once

// How long the PIN_ENTRY screen waits for input before returning to HOME.
#define UI_PIN_ENTRY_IDLE_TIMEOUT_MS 15000

// How long the ERROR ("Access denied") and UNLOCKING screens stay up before
// returning to HOME. These are separate values, currently different — if the
// two feel inconsistent to read, set them equal.
#define UI_ERROR_SCREEN_TIMEOUT_MS 2000
#define UI_UNLOCKING_SCREEN_TIMEOUT_MS 3000 // TODO: use AccessConfig.unlockDurationMs instead

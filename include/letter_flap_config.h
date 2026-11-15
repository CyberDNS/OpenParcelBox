#pragma once

// Magnetic reed contact on the letter flap. Wire it between LETTER_FLAP_PIN
// and GND; the internal pull-up is used, so no external resistor is needed.
//
// GPIO17 = header pin 16 (GND on header pin 4 or 30). It's otherwise only
// used by the optional camera connector (VSYNC), which this build doesn't
// use. Not GPIO9/10/11: those are likely shared with the TF card slot.
#define LETTER_FLAP_PIN 17

// Pin level that means "flap open". The fitted contact closes the circuit
// while the magnet is near (flap shut) and opens it when the magnet moves
// away, so the pin reads LOW when shut and HIGH when open. A contact that
// works the other way round would need LOW here.
#define LETTER_FLAP_OPEN_LEVEL HIGH

// The level must stay unchanged this long before a change is accepted —
// filters reed contact bounce.
#define LETTER_FLAP_DEBOUNCE_MS 100
#define LETTER_FLAP_POLL_MS 10

// A single delivery can swing the flap open several times (several letters,
// the flap bouncing as it shuts). An opening less than this long after the
// flap's previous change (open or close) counts as the same delivery in the
// inbox list.
#define LETTER_FLAP_COALESCE_MS 5000

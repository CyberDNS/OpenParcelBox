#pragma once

// Pixels at the top of the screen physically covered by the box's bezel
// once mounted (measured with the calibration ruler in ui_task.cpp's
// switchScreen() history). No UI content should render above this line —
// every screen is built inside a content area already inset by this much,
// so individual screens don't need to account for it themselves.
#define UI_TOP_HIDDEN_PX 50

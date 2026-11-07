// Test-rig serial console (T-Display-S3 only). Lets us fill the graphs with days of
// data in a few seconds, which is otherwise impossible without waiting days.
#pragma once
#ifdef BOARD_TDISPLAY_S3
#include <stdint.h>

namespace testrig {

// Reads one line of serial input if there is one. Called from the UI loop.
void poll();

// Time dilation: maps a real tap time onto a compressed clock, so a minute of tapping
// lands across days of history. Returns `real` unchanged when dilation is off.
int64_t tap_time(int64_t real);

}  // namespace testrig
#endif

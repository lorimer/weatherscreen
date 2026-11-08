// Test-rig serial console (bring-up boards only; see docs/TEST_RIG.md).
//
// Compiled only into env:tdisplay. Nothing in the production firmware calls it, refers
// to it, or is built differently because it exists: it reaches the rest of the firmware
// only through public APIs that production already has.
#pragma once
#ifdef BOARD_TDISPLAY_S3

namespace testrig {

// Reads one line of serial input if there is one. Called from the rig's UI loop.
void poll();

}  // namespace testrig
#endif

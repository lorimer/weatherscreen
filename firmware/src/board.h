// Board bring-up: I2C, IO expander, RGB LCD, GT911 touch, backlight, LVGL glue.
#pragma once
#include <stdint.h>

namespace board {

// Initialises I2C, the IO expander, touch, LCD and LVGL. Returns false on a fatal error.
bool begin();

// Backlight 0..100 %. Never fully off (minimum 3 %).
void set_backlight(uint8_t percent);
uint8_t backlight();

// Turns the picture and touch 180 degrees, for a unit standing the other way up.
void set_flipped(bool on);

// False on a bring-up board whose SD pins are wired to something else, so storage
// knows to go straight to internal flash instead of driving them.
bool has_sd_slot();

// Raises the SD card's chip-select line through the expander (needed before SD_MMC).
void sd_cs_high();

// LVGL is not thread-safe: every LVGL call outside the UI loop must hold this lock.
bool lvgl_lock(uint32_t timeout_ms = 1000);
void lvgl_unlock();

// Run LVGL timers; call from loop().
void lvgl_tick();

// Touch diagnostics for the web status page.
uint32_t touches_accepted();
uint32_t touches_filtered();

}  // namespace board

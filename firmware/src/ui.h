// Screens. All functions run on the UI thread (Arduino loop) with the LVGL lock held,
// except ui::post_* which may be called from any task.
#pragma once
#include <Arduino.h>

namespace ui {

void begin();         // builds the main screen; starts setup if not configured
void loop();          // refresh data, clock and dimming (call every loop)

void show_main();
void show_setup_first_time();
void show_maintenance();

// Thread-safe notices from other tasks.
void post_updating(bool on);   // OTA in progress overlay

// Internal (shared between ui_main.cpp and ui_setup.cpp)
namespace detail {
void build_main();
void refresh_main(bool force);
void tick_clock();
bool main_active();
}  // namespace detail

}  // namespace ui

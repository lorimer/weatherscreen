// Networking task (core 0): Wi-Fi, time, weather, sensor sampling, analysis,
// web management interface, OTA and hotspot fallback.
#pragma once
#include <Arduino.h>

namespace net {

void begin();                  // starts the background task

bool connected();
String ip();
int rssi();
bool ap_active();
String ap_name();
uint32_t offline_seconds();    // 0 when connected

// Setup screens drive Wi-Fi directly; this pauses the task's own reconnect logic.
void set_setup_active(bool on);

// Ask the task to fetch weather again soon (e.g. after the location changed).
void request_weather();
void request_reconnect();

bool weather_ok();
String last_weather_error();

}  // namespace net

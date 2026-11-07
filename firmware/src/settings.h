// Persistent settings (NVS). Changed through the setup wizard or the web UI.
#pragma once
#include <Arduino.h>

#include "pain.h"

struct Settings {
  // Wi-Fi
  String ssid;
  String wifi_pass;
  // Location (from Open-Meteo geocoding)
  String place;          // "Grand Junction, Colorado"
  float lat = NAN, lon = NAN;
  float elevation_m = NAN;
  int32_t utc_offset_s = 0;  // last value reported by Open-Meteo for this location
  // Identity / access
  String device_name;    // also the network hostname
  String admin_pass;     // web UI, OTA and hotspot password
  // Display
  uint8_t day_brightness = 100;
  uint8_t night_brightness = 35;
  uint16_t fade_minutes = 30;
  bool flip_display = false;  // turn the picture and touch 180 degrees
  // Pain analysis parameters
  pain::Params pain;

  bool has_wifi() const { return ssid.length() > 0; }
  bool has_location() const { return !isnan(lat) && !isnan(lon); }
  bool setup_done() const { return has_wifi() && has_location() && admin_pass.length() >= 8; }
};

namespace settings {
void load();
void save();
Settings& get();               // live copy; call save() after changing
String default_device_name();  // weatherstation-xxxx from the MAC address
String hostname();             // sanitised device name
void factory_reset();
}  // namespace settings

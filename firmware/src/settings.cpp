#include "settings.h"

#include <Preferences.h>
#include <WiFi.h>

namespace settings {
namespace {
Settings s;
Preferences prefs;
}  // namespace

String default_device_name() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof buf, "weatherstation-%02x%02x", mac[4], mac[5]);
  return buf;
}

String hostname() {
  String in = s.device_name.length() ? s.device_name : default_device_name();
  String out;
  for (char c : in) {
    if (isalnum((unsigned char)c)) out += (char)tolower(c);
    else if ((c == '-' || c == ' ' || c == '_') && out.length() && !out.endsWith("-")) out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  if (out.isEmpty()) out = default_device_name();
  return out.substring(0, 32);
}

void load() {
  prefs.begin("ws", true);
  s.ssid = prefs.getString("ssid", "");
  s.wifi_pass = prefs.getString("wpass", "");
  s.place = prefs.getString("place", "");
  s.lat = prefs.getFloat("lat", NAN);
  s.lon = prefs.getFloat("lon", NAN);
  s.elevation_m = prefs.getFloat("elev", NAN);
  s.utc_offset_s = prefs.getInt("utcoff", 0);
  s.device_name = prefs.getString("name", "");
  s.admin_pass = prefs.getString("admin", "");
  s.day_brightness = prefs.getUChar("bday", 100);
  s.night_brightness = prefs.getUChar("bnight", 35);
  s.fade_minutes = prefs.getUShort("fade", 30);
  s.pain.bandwidth_h = prefs.getFloat("p_bw", s.pain.bandwidth_h);
  s.pain.gap_h = prefs.getFloat("p_gap", s.pain.gap_h);
  s.pain.density_window_h = prefs.getFloat("p_dw", s.pain.density_window_h);
  s.pain.support_full = prefs.getFloat("p_sup", s.pain.support_full);
  s.pain.cutoff_floor = prefs.getFloat("p_cf", s.pain.cutoff_floor);
  prefs.end();
  if (s.device_name.isEmpty()) s.device_name = default_device_name();
}

void save() {
  prefs.begin("ws", false);
  prefs.putString("ssid", s.ssid);
  prefs.putString("wpass", s.wifi_pass);
  prefs.putString("place", s.place);
  prefs.putFloat("lat", s.lat);
  prefs.putFloat("lon", s.lon);
  prefs.putFloat("elev", s.elevation_m);
  prefs.putInt("utcoff", s.utc_offset_s);
  prefs.putString("name", s.device_name);
  prefs.putString("admin", s.admin_pass);
  prefs.putUChar("bday", s.day_brightness);
  prefs.putUChar("bnight", s.night_brightness);
  prefs.putUShort("fade", s.fade_minutes);
  prefs.putFloat("p_bw", s.pain.bandwidth_h);
  prefs.putFloat("p_gap", s.pain.gap_h);
  prefs.putFloat("p_dw", s.pain.density_window_h);
  prefs.putFloat("p_sup", s.pain.support_full);
  prefs.putFloat("p_cf", s.pain.cutoff_floor);
  prefs.end();
}

Settings& get() { return s; }

void factory_reset() {
  prefs.begin("ws", false);
  prefs.clear();
  prefs.end();
  s = Settings();
  s.device_name = default_device_name();
}

}  // namespace settings

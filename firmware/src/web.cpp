#include "web.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "board.h"
#include "history.h"
#include "net.h"
#include "sensor.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"
#include "weather.h"
#include "web_page.h"

namespace web {
namespace {

WebServer server(80);
bool started = false;
bool upload_ok = false;
bool upload_authed = false;
String upload_err;

bool auth() {
  const auto& p = settings::get().admin_pass;
  if (p.length() >= 8 && server.authenticate("admin", p.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Weather & Pain Station");
  return false;
}

String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String jnum(float v, int d) { return isnan(v) ? String("null") : String(v, d); }

void send_json(const String& j) { server.send(200, "application/json", j); }

void h_index() {
  if (!auth()) return;
  server.send_P(200, "text/html", WEB_PAGE);
}

void h_status() {
  if (!auth()) return;
  const auto& s = settings::get();
  String h = history::status_json();  // {...}
  h.remove(h.length() - 1);           // drop closing brace to append device fields
  char storage_s[80];
  snprintf(storage_s, sizeof storage_s, "%s, %llu of %llu MB used", storage::medium(),
           (unsigned long long)(storage::used_bytes() >> 20), (unsigned long long)(storage::total_bytes() >> 20));
  h += ",\"device_name\":\"" + esc(s.device_name) + "\"";
  h += ",\"hostname\":\"" + settings::hostname() + "\"";
  h += ",\"place\":\"" + esc(s.place) + "\"";
  h += ",\"firmware\":\"" FW_VERSION "\"";
  h += ",\"uptime_s\":" + String(millis() / 1000);
  h += ",\"ip\":\"" + net::ip() + "\"";
  h += ",\"wifi\":\"" + esc(net::connected() ? s.ssid + " (" + String(net::rssi()) + " dBm)" : String("offline")) +
       (net::ap_active() ? ", hotspot on" : "") + "\"";
  h += ",\"weather_ok\":" + String(net::weather_ok() ? "true" : "false");
  h += ",\"weather_error\":\"" + esc(net::last_weather_error()) + "\"";
  h += ",\"sensor\":\"" + String(sensor::chip_name()) + "\"";
  h += ",\"storage\":\"" + String(storage_s) + "\"";
  h += ",\"backlight\":" + String(board::backlight());
  h += ",\"touch_ok\":" + String(board::touches_accepted());
  h += ",\"touch_filtered\":" + String(board::touches_filtered());
  h += ",\"heap\":" + String(ESP.getFreeHeap());
  h += ",\"psram\":" + String(ESP.getFreePsram());
  h += "}";
  send_json(h);
}

// ?from=&to= (unix seconds) or ?hours=N ending now.
void range_args(int64_t* from, int64_t* to) {
  *to = server.hasArg("to") ? atoll(server.arg("to").c_str()) : 0;
  *from = server.hasArg("from") ? atoll(server.arg("from").c_str()) : 0;
  if (!*from && server.hasArg("hours")) *from = (int64_t)time(nullptr) - (int64_t)server.arg("hours").toInt() * 3600;
}

void h_range() {
  if (!auth()) return;
  send_json(history::range_json());
}

void h_series() {
  if (!auth()) return;
  int64_t f, t;
  range_args(&f, &t);
  send_json(history::series_json(f, t));
}

void h_daily() {
  if (!auth()) return;
  int64_t f, t;
  range_args(&f, &t);
  send_json(history::daily_json(f, t));
}

void h_profile() {
  if (!auth()) return;
  int64_t f, t;
  range_args(&f, &t);
  send_json(history::profile_json(f, t));
}

void h_correlation() {
  if (!auth()) return;
  int64_t f, t;
  range_args(&f, &t);
  send_json(history::correlation_json(f, t));
}

void h_settings_get() {
  if (!auth()) return;
  const auto& s = settings::get();
  String j = "{";
  j += "\"device_name\":\"" + esc(s.device_name) + "\"";
  j += ",\"place\":\"" + esc(s.place) + "\"";
  j += ",\"lat\":" + jnum(s.lat, 4) + ",\"lon\":" + jnum(s.lon, 4) + ",\"elevation_m\":" + jnum(s.elevation_m, 0);
  j += ",\"ssid\":\"" + esc(s.ssid) + "\"";
  j += ",\"day_brightness\":" + String(s.day_brightness);
  j += ",\"night_brightness\":" + String(s.night_brightness);
  j += ",\"fade_minutes\":" + String(s.fade_minutes);
  j += ",\"bandwidth_h\":" + String(s.pain.bandwidth_h, 2);
  j += ",\"gap_h\":" + String(s.pain.gap_h, 2);
  j += ",\"density_window_h\":" + String(s.pain.density_window_h, 2);
  j += ",\"support_full\":" + String(s.pain.support_full, 2);
  j += ",\"cutoff_floor\":" + String(s.pain.cutoff_floor, 2);
  j += "}";
  send_json(j);
}

void h_settings_post() {
  if (!auth()) return;
  auto& s = settings::get();
  bool wifi = false, loc = false, pass = false;
  auto has = [](const char* k) { return server.hasArg(k) && server.arg(k).length() > 0; };
  auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };
  if (has("device_name")) { s.device_name = server.arg("device_name"); pass = true; }
  if (has("lat") && has("lon")) {
    s.lat = server.arg("lat").toFloat();
    s.lon = server.arg("lon").toFloat();
    s.place = server.arg("place");
    s.elevation_m = has("elevation_m") ? server.arg("elevation_m").toFloat() : NAN;
    loc = true;
  }
  if (has("ssid")) { if (server.arg("ssid") != s.ssid) wifi = true; s.ssid = server.arg("ssid"); }
  if (has("wifi_pass")) { s.wifi_pass = server.arg("wifi_pass"); wifi = true; }
  if (has("admin_pass")) {
    if (server.arg("admin_pass").length() < 8) {
      server.send(400, "text/plain", "Password must be at least 8 characters");
      return;
    }
    s.admin_pass = server.arg("admin_pass");
    pass = true;
  }
  if (has("day_brightness")) s.day_brightness = constrain(server.arg("day_brightness").toInt(), 10, 100);
  if (has("night_brightness")) s.night_brightness = constrain(server.arg("night_brightness").toInt(), 3, 100);
  if (has("fade_minutes")) s.fade_minutes = constrain(server.arg("fade_minutes").toInt(), 0, 180);
  if (has("bandwidth_h")) s.pain.bandwidth_h = clampf(server.arg("bandwidth_h").toFloat(), 2, 72);
  if (has("gap_h")) s.pain.gap_h = clampf(server.arg("gap_h").toFloat(), 4, 96);
  if (has("density_window_h")) s.pain.density_window_h = clampf(server.arg("density_window_h").toFloat(), 0.1f, 12);
  if (has("support_full")) s.pain.support_full = clampf(server.arg("support_full").toFloat(), 1, 20);
  if (has("cutoff_floor")) s.pain.cutoff_floor = clampf(server.arg("cutoff_floor").toFloat(), 1, 4);
  if (s.pain.gap_h < s.pain.bandwidth_h) s.pain.gap_h = s.pain.bandwidth_h;
  settings::save();
  server.send(200, "text/plain", "OK");
  if (loc) net::request_weather();
  if (wifi || pass) net::request_reconnect();  // also restarts mDNS/OTA with new name/password
  history::recompute();
}

void h_geocode() {
  if (!auth()) return;
  std::vector<weather::Place> res;
  String err;
  weather::geocode(server.arg("q"), &res, &err);
  String j = "[";
  for (size_t i = 0; i < res.size(); ++i) {
    if (i) j += ',';
    j += "{\"name\":\"" + esc(res[i].name) + "\",\"region\":\"" + esc(res[i].region) + "\",\"lat\":" +
         String(res[i].lat, 4) + ",\"lon\":" + String(res[i].lon, 4) + ",\"elevation\":" + jnum(res[i].elevation, 0) + "}";
  }
  send_json(j + "]");
}

void stream_file(const char* path, const char* name) {
  if (!auth()) return;
  File f = storage::fs().open(path, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "No data yet");
    return;
  }
  server.sendHeader("Content-Disposition", String("attachment; filename=") + name);
  server.streamFile(f, "text/csv");
  f.close();
}

class StringPrint : public Print {
 public:
  String s;
  size_t write(uint8_t c) override { s += (char)c; return 1; }
};

void h_daily_csv() {
  if (!auth()) return;
  int64_t f, t;
  range_args(&f, &t);
  if (!f) f = 1;  // whole history by default
  StringPrint p;
  history::write_daily_csv(p, f, t);
  server.sendHeader("Content-Disposition", "attachment; filename=daily.csv");
  server.send(200, "text/csv", p.s);
}

void h_restart() {
  if (!auth()) return;
  server.send(200, "text/plain", "Restarting");
  delay(500);
  ESP.restart();
}

void h_update_done() {
  if (!upload_authed) {
    auth();
    return;
  }
  if (upload_ok) {
    server.send(200, "text/plain", "Installed. Restarting now; the page will be back in about 30 seconds.");
    delay(800);
    ESP.restart();
  } else {
    ui::post_updating(false);
    server.send(500, "text/plain", "Update failed: " + upload_err);
  }
}

void h_update_upload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    const auto& p = settings::get().admin_pass;
    upload_authed = p.length() >= 8 && server.authenticate("admin", p.c_str());
    upload_ok = false;
    upload_err = "";
    if (!upload_authed) return;
    ui::post_updating(true);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) upload_err = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upload_authed && upload_err.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
      upload_err = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (upload_authed && upload_err.isEmpty()) {
      if (Update.end(true)) upload_ok = true;
      else upload_err = Update.errorString();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    upload_err = "aborted";
  }
}

}  // namespace

void begin() {
  if (started) return;
  server.on("/", HTTP_GET, h_index);
  server.on("/api/status", HTTP_GET, h_status);
  server.on("/api/series", HTTP_GET, h_series);
  server.on("/api/range", HTTP_GET, h_range);
  server.on("/api/daily", HTTP_GET, h_daily);
  server.on("/api/profile", HTTP_GET, h_profile);
  server.on("/api/correlation", HTTP_GET, h_correlation);
  server.on("/api/settings", HTTP_GET, h_settings_get);
  server.on("/api/settings", HTTP_POST, h_settings_post);
  server.on("/api/geocode", HTTP_GET, h_geocode);
  server.on("/api/restart", HTTP_POST, h_restart);
  server.on("/export/pain.csv", HTTP_GET, [] { stream_file("/pain.csv", "pain.csv"); });
  server.on("/export/pressure.csv", HTTP_GET, [] { stream_file("/pressure.csv", "pressure.csv"); });
  server.on("/export/daily.csv", HTTP_GET, h_daily_csv);
  server.on("/update", HTTP_POST, h_update_done, h_update_upload);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
  started = true;
}

void handle() {
  if (started) server.handleClient();
}

}  // namespace web

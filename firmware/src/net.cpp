#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <atomic>

#include "config.h"
#include "history.h"
#include "sensor.h"
#include "settings.h"
#include "ui.h"
#include "weather.h"
#include "web.h"

namespace net {
namespace {

std::atomic<bool> setup_active{false};
std::atomic<bool> want_weather{true};
std::atomic<bool> want_reconnect{false};
std::atomic<bool> wx_ok{false};
std::atomic<bool> ap_on{false};
std::atomic<uint32_t> offline_since{0};
String wx_err = "not fetched yet";
SemaphoreHandle_t err_mtx = xSemaphoreCreateMutex();

bool services_up = false;
bool time_started = false;
uint32_t last_wx_try = 0, last_wx_ok = 0, last_sensor = 0, last_recompute = 0, last_reconnect = 0;
uint32_t connected_since = 0;
int64_t last_logged_slot = 0;

void set_err(const String& e) {
  xSemaphoreTake(err_mtx, portMAX_DELAY);
  wx_err = e;
  xSemaphoreGive(err_mtx);
}

void start_ota() {
  const auto& s = settings::get();
  if (s.admin_pass.length() < 8) return;
  ArduinoOTA.end();
  ArduinoOTA.setHostname(settings::hostname().c_str());
  ArduinoOTA.setPassword(s.admin_pass.c_str());
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.onStart([] { ui::post_updating(true); });
  ArduinoOTA.onError([](ota_error_t) { ui::post_updating(false); });
  ArduinoOTA.begin();
}

void start_services() {
  if (services_up) return;
  MDNS.end();
  if (MDNS.begin(settings::hostname().c_str())) MDNS.addService("http", "tcp", 80);
  start_ota();
  web::begin();
  services_up = true;
}

void begin_wifi() {
  const auto& s = settings::get();
  WiFi.persistent(false);
  WiFi.setHostname(settings::hostname().c_str());
  WiFi.mode(ap_on ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  if (s.has_wifi()) WiFi.begin(s.ssid.c_str(), s.wifi_pass.c_str());
}

void manage_wifi() {
  if (setup_active) return;
  const auto& s = settings::get();
  uint32_t now = millis();

  if (want_reconnect.exchange(false)) {
    WiFi.disconnect();
    services_up = false;
    begin_wifi();
    last_reconnect = now;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!connected_since) connected_since = now;
    offline_since = 0;
    if (!time_started) {
      configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
      time_started = true;
    }
    start_services();
    // Leave hotspot mode after 5 minutes of stable Wi-Fi.
    if (ap_on && now - connected_since > 5UL * 60 * 1000) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      ap_on = false;
    }
    return;
  }

  connected_since = 0;
  if (!offline_since) offline_since = now ? now : 1;
  if (!s.has_wifi()) return;
  if (now - last_reconnect > 60000) {
    last_reconnect = now;
    WiFi.reconnect();
  }
  // Fallback hotspot so a maintainer nearby can reach the web page.
  if (!ap_on && now - offline_since > timing::AP_FALLBACK_AFTER_MS && s.admin_pass.length() >= 8) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_name().c_str(), s.admin_pass.c_str());
    ap_on = true;
    services_up = false;
    start_services();
  }
}

void do_weather() {
  const auto& s = settings::get();
  uint32_t now = millis();
  if (!s.has_location() || WiFi.status() != WL_CONNECTED) return;
  bool due = want_weather || (wx_ok && now - last_wx_ok > timing::WEATHER_REFRESH_MS) ||
             (!wx_ok && now - last_wx_try > timing::WEATHER_RETRY_MS) || !last_wx_try;
  if (!due) return;
  if (time(nullptr) < 1700000000 && now - last_wx_try < 5000) return;  // give NTP a moment
  want_weather = false;
  last_wx_try = now;
  weather::Data d;
  String err;
  esp_task_wdt_reset();
  if (weather::fetch(s.lat, s.lon, &d, &err)) {
    wx_ok = true;
    last_wx_ok = now;
    set_err("");
    history::set_weather(d);
    auto& sm = settings::get();
    bool dirty = false;
    if (sm.utc_offset_s != d.utc_offset_s) { sm.utc_offset_s = d.utc_offset_s; dirty = true; }
    if (isnan(sm.elevation_m) && !isnan(d.elevation_m)) { sm.elevation_m = d.elevation_m; dirty = true; }
    if (dirty) settings::save();
    history::recompute();
    last_recompute = now;
  } else {
    // Keep showing the last good forecast; the screen notes when it is stale.
    if (now - last_wx_ok > timing::WEATHER_REFRESH_MS) wx_ok = false;
    set_err(err);
    log_w("Weather fetch failed: %s", err.c_str());
  }
}

void do_sensor() {
  uint32_t now = millis();
  if (last_sensor && now - last_sensor < timing::SENSOR_READ_MS) return;
  last_sensor = now;
  int64_t t = time(nullptr);
  if (t < 1700000000) return;  // need a real clock to timestamp
  auto r = sensor::read();
  if (r.ok) {
    int64_t slot = t / (timing::SENSOR_LOG_MS / 1000);
    bool log_it = slot != last_logged_slot;
    if (log_it) last_logged_slot = slot;
    history::add_sensor_reading(t, r.station_hpa, r.temp_c, r.humidity, log_it);
  } else if (!sensor::present()) {
    history::log_api_pressure_if_no_sensor(t);
  }
}

void task(void*) {
  esp_task_wdt_add(nullptr);
  begin_wifi();
  for (;;) {
    esp_task_wdt_reset();
    manage_wifi();
    do_weather();
    do_sensor();
    uint32_t now = millis();
    if (history::needs_recompute() || now - last_recompute > 60000) {
      history::recompute();
      last_recompute = now;
    }
    if (services_up) {
      web::handle();
      ArduinoOTA.handle();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

void begin() { xTaskCreatePinnedToCore(task, "net", 16384, nullptr, 1, nullptr, 0); }

bool connected() { return WiFi.status() == WL_CONNECTED; }
String ip() { return connected() ? WiFi.localIP().toString() : (ap_on ? WiFi.softAPIP().toString() : String("-")); }
int rssi() { return connected() ? WiFi.RSSI() : 0; }
bool ap_active() { return ap_on; }
String ap_name() { return settings::hostname(); }
uint32_t offline_seconds() { return offline_since ? (millis() - offline_since) / 1000 : 0; }
void set_setup_active(bool on) {
  bool was = setup_active.exchange(on);
  if (was && !on) want_reconnect = true;  // re-apply saved credentials after the wizard
}
void request_weather() { want_weather = true; }
void request_reconnect() { want_reconnect = true; }
bool weather_ok() { return wx_ok; }
String last_weather_error() {
  xSemaphoreTake(err_mtx, portMAX_DELAY);
  String e = wx_err;
  xSemaphoreGive(err_mtx);
  return e;
}

}  // namespace net

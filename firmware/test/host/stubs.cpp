// Host-only stand-ins for the hardware and network modules, with sample data that
// matches the approved mockup.
#include <chrono>
#include "WiFi.h"
#include "../../src/board.h"
#include "../../src/history.h"
#include "../../src/net.h"
#include "../../src/sensor.h"
#include "../../src/settings.h"
#include "../../src/storage.h"
#include "../../src/weather.h"

WiFiStub WiFi;
EspStub ESP;
extern "C" uint32_t millis() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

namespace board {
void set_backlight(uint8_t) {}
uint8_t backlight() { return 100; }
}
namespace net {
bool connected() { return true; }
String ip() { return "192.168.1.50"; }
int rssi() { return -52; }
bool ap_active() { return false; }
String ap_name() { return "weatherstation-a1b2"; }
void set_setup_active(bool) {}
void request_weather() {}
void request_reconnect() {}
bool weather_ok() { return true; }
String last_weather_error() { return ""; }
}
namespace sensor { const char* chip_name() { return "BME280"; } }
namespace storage {
const char* medium() { return "microSD"; }
uint64_t total_bytes() { return 15500ull << 20; }
uint64_t used_bytes() { return 2ull << 20; }
}
namespace settings {
Settings s;
Settings& get() { return s; }
void save() {}
String hostname() { return "weatherstation-a1b2"; }
}
namespace history {
int taps_added = 0;
void add_tap(int) { ++taps_added; }
size_t tap_count() { return 214; }
bool snapshot_if_newer(uint32_t have, Snapshot* out) {
  if (have == 7) return false;
  Snapshot s;
  s.version = 7;
  s.wx_valid = true;
  auto& w = s.wx;
  w.valid = true; w.fetched = time(nullptr);
  w.temp_f = 72; w.feels_f = 70; w.wind_mph = 8; w.code = 0; w.is_day = true;
  w.today.hi = 78; w.today.lo = 51;
  int64_t mid = (time(nullptr) / 86400) * 86400;
  int codes[3] = {0, 2, 61}, hi[3] = {75, 68, 60}, lo[3] = {48, 45, 41}, pp[3] = {5, 20, 70};
  for (int k = 0; k < 3; ++k) { w.next[k].date = mid + 86400LL * (k + 1); w.next[k].code = codes[k]; w.next[k].hi = hi[k]; w.next[k].lo = lo[k]; w.next[k].precip_pct = pp[k]; }
  s.utc_offset_s = -7 * 3600;
  s.pressure_inhg = 29.84f;
  s.trend = Trend::Falling;
  for (int i = 0; i < SERIES_POINTS; ++i) {
    float x = i / float(SERIES_POINTS - 1);
    s.pressure_series[i] = x < 0.45f ? 30.04f + 0.02f * sinf(i * 0.4f) : 30.04f - (x - 0.45f) * 0.36f;
    s.pain_series[i] = (i > 27 && i < 42) ? NAN : 2.0f + 2.3f * powf(x, 1.6f);
  }
  *out = s;
  return true;
}
}
namespace weather {
const char* describe(int c, bool day) {
  switch (c) { case 0: return day ? "Sunny" : "Clear"; case 2: return "Partly cloudy"; case 61: return "Rain"; default: return "Cloudy"; }
}
const char* describe_forecast(int c, int pp) {
  if (c == 61) return pp >= 60 ? "Rain likely" : "Chance of rain";
  return describe(c, true);
}
bool geocode(const String&, std::vector<Place>*, String*) { return false; }
}

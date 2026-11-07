#include "history.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "config.h"
#include "sensor.h"
#include "settings.h"
#include "storage.h"

namespace history {
namespace {

constexpr float HPA_TO_INHG = 0.0295300f;
constexpr int64_t KEEP_PRESSURE_S = 400LL * 86400;

struct PSample {
  int64_t t;
  float hpa;  // sea level
};

SemaphoreHandle_t mtx = xSemaphoreCreateRecursiveMutex();
struct Lock {
  Lock() { xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mtx); }
};

std::vector<pain::Tap> taps;
std::vector<PSample> psamples;        // logged local (or API fallback) sea-level pressure
weather::Data wx;
PSample latest_local{0, NAN};         // most recent sensor reading (every minute)
float latest_temp = NAN, latest_hum = NAN;
bool dirty_taps = false;
Snapshot snap;
uint32_t version = 1;

int64_t now_s() { return (int64_t)time(nullptr); }
bool clock_ok() { return now_s() > 1700000000; }

// API hourly pressure, linearly interpolated.
float api_at(int64_t t) {
  const auto& h = wx.hourly_pressure;
  if (h.empty() || t < h.front().first || t > h.back().first) return NAN;
  auto it = std::lower_bound(h.begin(), h.end(), t,
                             [](const std::pair<int64_t, float>& a, int64_t v) { return a.first < v; });
  if (it == h.begin()) return it->second;
  auto prev = it - 1;
  float f = float(t - prev->first) / float(it->first - prev->first);
  return prev->second + f * (it->second - prev->second);
}

// Mean of logged samples within +-win seconds of t.
float local_at(int64_t t, int64_t win) {
  auto it = std::lower_bound(psamples.begin(), psamples.end(), t - win,
                             [](const PSample& a, int64_t v) { return a.t < v; });
  double s = 0;
  int n = 0;
  for (; it != psamples.end() && it->t <= t + win; ++it) { s += it->hpa; ++n; }
  if (latest_local.t && std::llabs(latest_local.t - t) <= win && !std::isnan(latest_local.hpa)) {
    s += latest_local.hpa; ++n;
  }
  return n ? float(s / n) : NAN;
}

// Sensor-vs-API calibration offset (median over the last 48 h) so the two sources join
// without a step when one fills gaps in the other.
float api_offset() {
  std::vector<float> d;
  int64_t from = now_s() - 48 * 3600;
  for (const auto& p : psamples) {
    if (p.t < from) continue;
    float a = api_at(p.t);
    if (!std::isnan(a)) d.push_back(p.hpa - a);
  }
  if (d.size() < 6) return 0;
  std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
  return d[d.size() / 2];
}

float merged_at(int64_t t, float off) {
  float v = local_at(t, 20 * 60);
  if (!std::isnan(v)) return v;
  float a = api_at(t);
  return std::isnan(a) ? NAN : a + off;
}

Trend classify(float d3h_hpa) {
  if (std::isnan(d3h_hpa)) return Trend::Unknown;
  // NWS-style tendency: ~0.02 inHg (0.7 hPa) per 3 h is noise; 0.06 inHg (2 hPa) is fast.
  if (d3h_hpa <= -2.0f) return Trend::FallingFast;
  if (d3h_hpa <= -0.7f) return Trend::Falling;
  if (d3h_hpa >= 2.0f) return Trend::RisingFast;
  if (d3h_hpa >= 0.7f) return Trend::Rising;
  return Trend::Steady;
}

const char* trend_name(Trend t) {
  switch (t) {
    case Trend::FallingFast: return "falling fast";
    case Trend::Falling: return "falling";
    case Trend::Steady: return "steady";
    case Trend::Rising: return "rising";
    case Trend::RisingFast: return "rising fast";
    default: return "unknown";
  }
}

String num(float v, int dec = 2) { return std::isnan(v) ? String("null") : String(v, dec); }

// Hourly series used by the analysis endpoints.
struct Hourly {
  int64_t start;
  std::vector<float> pain, pressure;
};

Hourly hourly(int days, const pain::Result& r) {
  Hourly h;
  int64_t end = now_s() - now_s() % 3600;
  h.start = end - (int64_t)days * 86400;
  auto tr = pain::trend_series(r, h.start, end, 3600);
  for (auto& p : tr) h.pain.push_back(p.ok ? p.v : NAN);
  for (int64_t t = h.start; t <= end; t += 3600) h.pressure.push_back(local_at(t, 30 * 60));
  return h;
}

}  // namespace

void begin() {
  Lock l;
  storage::read_pain([](int64_t t, int v) { taps.push_back({t, (float)v}); });
  int64_t cutoff = now_s() - KEEP_PRESSURE_S;
  storage::read_pressure([&](int64_t t, float hpa) {
    if (!clock_ok() || t >= cutoff) psamples.push_back({t, hpa});
  });
  std::sort(psamples.begin(), psamples.end(), [](const PSample& a, const PSample& b) { return a.t < b.t; });
  log_i("Loaded %u taps, %u pressure samples", (unsigned)taps.size(), (unsigned)psamples.size());
  dirty_taps = true;
}

void add_tap(int score) {
  int64_t t = now_s();
  if (!clock_ok()) t = 0;  // no clock yet: still record, flagged by t=0 (ignored on reload)
  storage::append_pain(t, score);
  Lock l;
  if (t) taps.push_back({t, (float)score});
  dirty_taps = true;
}

void add_sensor_reading(int64_t t, float station, float temp_c, float hum, bool log_it) {
  const auto& s = settings::get();
  float outdoor_c = std::isnan(wx.temp_f) ? NAN : (wx.temp_f - 32) / 1.8f;
  float elev = !std::isnan(s.elevation_m) ? s.elevation_m : wx.elevation_m;
  float sea = sensor::to_sea_level(station, elev, outdoor_c);
  {
    Lock l;
    latest_local = {t, sea};
    latest_temp = temp_c;
    latest_hum = hum;
    if (log_it) psamples.push_back({t, sea});
  }
  if (log_it) storage::append_pressure(t, station, sea, temp_c, hum);
}

void log_api_pressure_if_no_sensor(int64_t t) {
  // Without a sensor, keep an hourly record of the internet pressure so long-term
  // correlation still works.
  Lock l;
  if (latest_local.t && t - latest_local.t < 3600) return;
  if (std::isnan(wx.pressure_hpa)) return;
  if (!psamples.empty() && t - psamples.back().t < 3300) return;
  psamples.push_back({t, wx.pressure_hpa});
  storage::append_pressure(t, NAN, wx.pressure_hpa, NAN, NAN);
}

void set_weather(const weather::Data& d) {
  Lock l;
  wx = d;
}

bool needs_recompute() {
  Lock l;
  return dirty_taps;
}

void recompute() {
  std::vector<pain::Tap> copy;
  pain::Params params = settings::get().pain;
  {
    Lock l;
    copy = taps;
    dirty_taps = false;
  }
  int64_t now = now_s();
  int64_t end = now - now % SERIES_STEP_S;
  int64_t start = end - (SERIES_POINTS - 1) * SERIES_STEP_S;
  // Only the last ~3 days of taps affect the 48 h graph.
  std::vector<pain::Tap> recent;
  for (auto& t : copy)
    if (t.t >= start - 2 * 86400) recent.push_back(t);
  auto r = pain::analyze(recent, params);
  auto tr = pain::trend_series(r, start, end, SERIES_STEP_S);

  Lock l;
  Snapshot s;
  s.wx = wx;
  s.wx.hourly_pressure.clear();
  s.wx_valid = wx.valid;
  s.series_end = end;
  s.utc_offset_s = wx.valid ? wx.utc_offset_s : settings::get().utc_offset_s;
  float off = api_offset();
  for (int i = 0; i < SERIES_POINTS; ++i) {
    float p = merged_at(start + i * SERIES_STEP_S, off);
    s.pressure_series[i] = std::isnan(p) ? NAN : p * HPA_TO_INHG;
    s.pain_series[i] = tr[i].ok ? tr[i].v : NAN;
  }
  float cur = (latest_local.t && now - latest_local.t < 20 * 60) ? latest_local.hpa
                                                                 : (std::isnan(wx.pressure_hpa) ? NAN : wx.pressure_hpa + off);
  s.pressure_inhg = std::isnan(cur) ? NAN : cur * HPA_TO_INHG;
  float past = merged_at(now - 3 * 3600, off);
  s.trend = classify((std::isnan(cur) || std::isnan(past)) ? NAN : cur - past);
  s.version = ++version;
  snap = s;
}

bool snapshot_if_newer(uint32_t have, Snapshot* out) {
  Lock l;
  if (snap.version == have) return false;
  *out = snap;
  return true;
}

String status_json() {
  Lock l;
  String j = "{";
  j += "\"pressure_inhg\":" + num(snap.pressure_inhg, 2);
  j += ",\"trend\":\"" + String(trend_name(snap.trend)) + "\"";
  j += ",\"taps\":" + String(taps.size());
  j += ",\"pressure_samples\":" + String(psamples.size());
  j += ",\"sensor_temp_c\":" + num(latest_temp, 1);
  j += ",\"sensor_humidity\":" + num(latest_hum, 0);
  j += ",\"last_sensor\":" + String((long long)latest_local.t);
  j += ",\"weather_fetched\":" + String((long long)wx.fetched);
  j += ",\"temp_f\":" + num(wx.temp_f, 0);
  j += ",\"condition\":\"" + String(weather::describe(wx.code, wx.is_day)) + "\"";
  j += "}";
  return j;
}

String series_json(int hours) {
  hours = constrain(hours, 6, 24 * 60);
  std::vector<pain::Tap> copy;
  {
    Lock l;
    copy = taps;
  }
  int64_t now = now_s();
  int64_t step = hours <= 72 ? 1800 : 3600;
  int64_t end = now - now % step, start = end - (int64_t)hours * 3600;
  std::vector<pain::Tap> recent;
  for (auto& t : copy)
    if (t.t >= start - 2 * 86400) recent.push_back(t);
  auto r = pain::analyze(recent, settings::get().pain);
  auto tr = pain::trend_series(r, start, end, step);

  Lock l;
  float off = api_offset();
  String j = "{\"start\":" + String((long long)start) + ",\"step\":" + String((long long)step);
  j += ",\"pressure_inhg\":[";
  for (int64_t t = start, i = 0; t <= end; t += step, ++i) {
    float p = merged_at(t, off);
    if (i) j += ',';
    j += num(std::isnan(p) ? NAN : p * HPA_TO_INHG, 3);
  }
  j += "],\"pain\":[";
  for (size_t i = 0; i < tr.size(); ++i) {
    if (i) j += ',';
    j += tr[i].ok ? String(tr[i].v, 2) : String("null");
  }
  j += "],\"taps\":[";
  bool first = true;
  for (size_t i = 0; i < r.taps.size(); ++i) {
    if (r.taps[i].t < start) continue;
    if (!first) j += ',';
    first = false;
    j += "[" + String((long long)r.taps[i].t) + "," + String((int)r.taps[i].v) + "," + String(r.robust_w[i], 2) + "]";
  }
  j += "]}";
  return j;
}

String analysis_json(int days) {
  days = constrain(days, 7, 365);
  std::vector<pain::Tap> copy;
  {
    Lock l;
    copy = taps;
  }
  auto r = pain::analyze(copy, settings::get().pain);
  int32_t off_s = wx.valid ? wx.utc_offset_s : settings::get().utc_offset_s;
  auto daily = pain::daily_values(r, off_s);

  Lock l;
  Hourly h = hourly(days, r);
  String j = "{\"days\":" + String(days) + ",\"daily\":[";
  int64_t since = now_s() - (int64_t)days * 86400;
  bool first = true;
  for (auto& d : daily) {
    if (d.day_start < since) continue;
    // Daily mean pressure and the largest 24 h fall ending that day.
    double ps = 0;
    int pn = 0;
    float maxdrop = NAN;
    for (int64_t t = d.day_start; t < d.day_start + 86400; t += 3600) {
      float p = local_at(t, 30 * 60), p24 = local_at(t - 86400, 30 * 60);
      if (!std::isnan(p)) { ps += p; ++pn; }
      if (!std::isnan(p) && !std::isnan(p24)) {
        float drop = p24 - p;
        if (std::isnan(maxdrop) || drop > maxdrop) maxdrop = drop;
      }
    }
    if (!first) j += ',';
    first = false;
    j += "{\"day\":" + String((long long)(d.day_start + off_s)) + ",\"pain\":" + String(d.v, 2) +
         ",\"taps\":" + String(d.taps) + ",\"low_conf\":" + (d.low_confidence ? "true" : "false") +
         ",\"pressure_inhg\":" + num(pn ? float(ps / pn) * HPA_TO_INHG : NAN, 2) +
         ",\"max_24h_drop_inhg\":" + num(std::isnan(maxdrop) ? NAN : maxdrop * HPA_TO_INHG, 3) + "}";
  }
  j += "],\"correlation\":[";
  // Pain at hour t+lag against the pressure change over the window ending at hour t.
  const int windows[] = {3, 12, 24};
  first = true;
  for (int w : windows) {
    std::vector<float> dp(h.pressure.size(), NAN);
    for (size_t i = w; i < h.pressure.size(); ++i)
      if (!std::isnan(h.pressure[i]) && !std::isnan(h.pressure[i - w])) dp[i] = h.pressure[i] - h.pressure[i - w];
    for (int lag = 0; lag <= 48; lag += 3) {
      std::vector<float> a, b;
      for (size_t i = 0; i + lag < h.pain.size(); ++i) {
        a.push_back(dp[i]);
        b.push_back(h.pain[i + lag]);
      }
      int n = 0;
      float c = pain::pearson(a, b, 48, &n);
      if (!first) j += ',';
      first = false;
      j += "{\"window_h\":" + String(w) + ",\"lag_h\":" + String(lag) + ",\"r\":" + num(c, 3) + ",\"n\":" + String(n) + "}";
    }
  }
  j += "]}";
  return j;
}

void write_daily_csv(Print& out) {
  std::vector<pain::Tap> copy;
  {
    Lock l;
    copy = taps;
  }
  auto r = pain::analyze(copy, settings::get().pain);
  int32_t off_s = wx.valid ? wx.utc_offset_s : settings::get().utc_offset_s;
  out.print("date,pain_trend_mean,taps,low_confidence,mean_pressure_inhg\n");
  for (auto& d : pain::daily_values(r, off_s)) {
    time_t lt = d.day_start + off_s;
    struct tm tmv;
    gmtime_r(&lt, &tmv);
    char date[16];
    strftime(date, sizeof date, "%Y-%m-%d", &tmv);
    double ps = 0;
    int pn = 0;
    {
      Lock l;
      for (int64_t t = d.day_start; t < d.day_start + 86400; t += 3600) {
        float p = local_at(t, 30 * 60);
        if (!std::isnan(p)) { ps += p; ++pn; }
      }
    }
    out.printf("%s,%.2f,%d,%d,%s\n", date, d.v, d.taps, d.low_confidence ? 1 : 0,
               pn ? String(float(ps / pn) * HPA_TO_INHG, 3).c_str() : "");
  }
}

size_t tap_count() { Lock l; return taps.size(); }
size_t pressure_count() { Lock l; return psamples.size(); }
int64_t last_sensor_time() { Lock l; return latest_local.t; }
float last_sensor_temp_c() { Lock l; return latest_temp; }
float last_sensor_humidity() { Lock l; return latest_hum; }

}  // namespace history

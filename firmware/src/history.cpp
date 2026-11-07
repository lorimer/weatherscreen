#include "history.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "config.h"
#include "sensor.h"
#include "settings.h"
#include "storage.h"
#include "testrig.h"
#include "trends.h"

namespace history {
namespace {

constexpr float HPA_TO_INHG = 0.0295300f;
constexpr int64_t KEEP_PRESSURE_S = 400LL * 86400;

using PSample = trends::PSample;  // sea-level hPa

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

// Copies of the raw data, taken under the lock, so slow analysis runs unlocked.
struct Copy {
  std::vector<pain::Tap> taps;
  std::vector<PSample> pres;
  int32_t off;
};

Copy copy_all() {
  Lock l;
  Copy c;
  c.taps = taps;
  c.pres = psamples;
  c.off = wx.valid ? wx.utc_offset_s : settings::get().utc_offset_s;
  return c;
}

int64_t clamp_range(int64_t* from, int64_t* to) {
  int64_t now = now_s();
  if (*to <= 0 || *to > now) *to = now;
  if (*from <= 0 || *from >= *to) *from = *to - 2 * 86400;
  if (*to - *from > 3700LL * 86400) *from = *to - 3700LL * 86400;  // ~10 years
  return *to - *from;
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
#ifdef BOARD_TDISPLAY_S3
  if (t) t = testrig::tap_time(t);  // test rig: optional time dilation
#endif
  storage::append_pain(t, score);
  Lock l;
  if (t) taps.push_back({t, (float)score});
  dirty_taps = true;
}

#ifdef BOARD_TDISPLAY_S3
void add_tap_at(int64_t t, int score) {
  storage::append_pain(t, score);
  Lock l;
  taps.push_back({t, (float)score});  // analyze() sorts, so order does not matter
  dirty_taps = true;
}

void clear_taps() {
  Lock l;
  taps.clear();
  dirty_taps = true;
}
#endif

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
  // Taps older than the window still shape the trend at its left edge (the kernel
  // reaches +-12 h), so keep two extra days either side of it.
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

String range_json() {
  Lock l;
  int64_t first = 0;
  if (!taps.empty()) first = taps.front().t;
  if (!psamples.empty() && (!first || psamples.front().t < first)) first = psamples.front().t;
  return "{\"first\":" + String((long long)first) + ",\"now\":" + String((long long)now_s()) + "}";
}

String series_json(int64_t from, int64_t to) {
  int64_t span = clamp_range(&from, &to);
  int64_t step = trends::pick_step(span, 600);
  from -= from % step;
  Copy c = copy_all();
  // Taps from two days before the window still shape its start.
  std::vector<pain::Tap> sel;
  for (auto& t : c.taps)
    if (t.t >= from - 2 * 86400 && t.t <= to + 2 * 86400) sel.push_back(t);
  auto r = pain::analyze(sel, settings::get().pain);
  auto tr = pain::trend_series(r, from, to, step);

  String j;
  j.reserve(16 * tr.size() + 64);
  j = "{\"start\":" + String((long long)from) + ",\"step\":" + String((long long)step) + ",\"pressure_inhg\":[";
  {
    Lock l;
    float off = api_offset();
    for (size_t i = 0; i < tr.size(); ++i) {
      int64_t t = from + (int64_t)i * step;
      // Average over the step for long ranges; nearest 20 min for short ones.
      float p = step > 1800 ? trends::pressure_near(psamples, t, step / 2) : merged_at(t, off);
      if (std::isnan(p) && step > 1800) p = merged_at(t, off);
      if (i) j += ',';
      j += num(std::isnan(p) ? NAN : p * HPA_TO_INHG, 3);
    }
  }
  j += "],\"pain\":[";
  for (size_t i = 0; i < tr.size(); ++i) {
    if (i) j += ',';
    j += tr[i].ok ? String(tr[i].v, 2) : String("null");
  }
  j += "],\"taps\":[";
  // Individual taps only for ranges where they can be seen (<= 31 days).
  bool first = true;
  if (span <= 31LL * 86400) {
    for (size_t i = 0; i < r.taps.size(); ++i) {
      if (r.taps[i].t < from || r.taps[i].t > to) continue;
      if (!first) j += ',';
      first = false;
      j += "[" + String((long long)r.taps[i].t) + "," + String((int)r.taps[i].v) + "," + String(r.robust_w[i], 2) + "]";
    }
  }
  j += "]}";
  return j;
}

String daily_json(int64_t from, int64_t to) {
  clamp_range(&from, &to);
  Copy c = copy_all();
  auto r = pain::analyze(c.taps, settings::get().pain);
  auto days = trends::daily(r, c.pres, c.off, from, to);
  String j;
  j.reserve(days.size() * 120 + 32);
  j = "{\"utc_offset\":" + String(c.off) + ",\"days\":[";
  for (size_t i = 0; i < days.size(); ++i) {
    const auto& d = days[i];
    if (i) j += ',';
    j += "{\"day\":" + String((long long)(d.day_start + c.off));
    j += ",\"pain\":" + (d.taps ? String(d.pain, 2) : String("null"));
    j += ",\"taps\":" + String(d.taps);
    j += ",\"low_conf\":" + String(d.low_conf ? "true" : "false");
    j += ",\"p_mean\":" + num(d.p_n ? d.p_mean * HPA_TO_INHG : NAN, 3);
    j += ",\"p_min\":" + num(d.p_n ? d.p_min * HPA_TO_INHG : NAN, 3);
    j += ",\"p_max\":" + num(d.p_n ? d.p_max * HPA_TO_INHG : NAN, 3);
    j += ",\"fall24\":" + num(d.has_change ? d.fall24 * HPA_TO_INHG : NAN, 3);
    j += ",\"rise24\":" + num(d.has_change ? d.rise24 * HPA_TO_INHG : NAN, 3);
    j += "}";
  }
  j += "]}";
  return j;
}

String profile_json(int64_t from, int64_t to) {
  clamp_range(&from, &to);
  Copy c = copy_all();
  auto r = pain::analyze(c.taps, settings::get().pain);
  auto hp = trends::hour_profile(r, c.off, from, to);
  String j = "{\"pain\":[";
  for (int h = 0; h < 24; ++h) { if (h) j += ','; j += num(hp.pain[h], 2); }
  j += "],\"taps\":[";
  for (int h = 0; h < 24; ++h) { if (h) j += ','; j += String(hp.taps[h]); }
  return j + "]}";
}

String correlation_json(int64_t from, int64_t to) {
  clamp_range(&from, &to);
  Copy c = copy_all();
  auto r = pain::analyze(c.taps, settings::get().pain);
  int64_t start = from - from % 3600;
  auto tr = pain::trend_series(r, start, to, 3600);
  std::vector<float> pain_h, pres_h;
  for (size_t i = 0; i < tr.size(); ++i) {
    pain_h.push_back(tr[i].ok ? tr[i].v : NAN);
    pres_h.push_back(trends::pressure_near(c.pres, start + (int64_t)i * 3600, 1800));
  }
  auto lc = trends::lag_correlation(pain_h, pres_h, {3, 12, 24}, 48, 3);
  String j = "{\"lags\":[";
  for (size_t i = 0; i < lc.size(); ++i) {
    if (i) j += ',';
    j += "{\"window_h\":" + String(lc[i].window_h) + ",\"lag_h\":" + String(lc[i].lag_h) + ",\"r\":" +
         num(lc[i].r, 3) + ",\"n\":" + String(lc[i].n) + "}";
  }
  return j + "]}";
}

void write_daily_csv(Print& out, int64_t from, int64_t to) {
  clamp_range(&from, &to);
  Copy c = copy_all();
  auto r = pain::analyze(c.taps, settings::get().pain);
  out.print("date,pain_trend_mean,taps,low_confidence,pressure_mean_inhg,pressure_min_inhg,"
            "pressure_max_inhg,largest_24h_fall_inhg,largest_24h_rise_inhg\n");
  for (const auto& d : trends::daily(r, c.pres, c.off, from, to)) {
    time_t lt = d.day_start + c.off;
    struct tm tmv;
    gmtime_r(&lt, &tmv);
    char date[16];
    strftime(date, sizeof date, "%Y-%m-%d", &tmv);
    auto f = [](bool ok, float v, int dec) { return ok ? String(v, dec) : String(""); };
    out.printf("%s,%s,%d,%d,%s,%s,%s,%s,%s\n", date, f(d.taps > 0, d.pain, 2).c_str(), d.taps,
               d.low_conf ? 1 : 0, f(d.p_n > 0, d.p_mean * HPA_TO_INHG, 3).c_str(),
               f(d.p_n > 0, d.p_min * HPA_TO_INHG, 3).c_str(), f(d.p_n > 0, d.p_max * HPA_TO_INHG, 3).c_str(),
               f(d.has_change, d.fall24 * HPA_TO_INHG, 3).c_str(), f(d.has_change, d.rise24 * HPA_TO_INHG, 3).c_str());
  }
}

size_t tap_count() { Lock l; return taps.size(); }
size_t pressure_count() { Lock l; return psamples.size(); }
int64_t last_sensor_time() { Lock l; return latest_local.t; }
float last_sensor_temp_c() { Lock l; return latest_temp; }
float last_sensor_humidity() { Lock l; return latest_hum; }

}  // namespace history

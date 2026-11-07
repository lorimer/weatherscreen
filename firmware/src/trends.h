// Long-term aggregates for the web "Trends" views. Plain C++ (no Arduino), so it is
// unit-tested on a PC alongside pain.cpp (see test/test_trends.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "pain.h"

namespace trends {

struct PSample {
  int64_t t;   // unix seconds
  float hpa;   // sea-level pressure
};

// Mean of samples within +-win seconds of t (samples sorted by t). NAN if none.
float pressure_near(const std::vector<PSample>& s, int64_t t, int64_t win);

struct DayStats {
  int64_t day_start = 0;   // local midnight as unix seconds
  float pain = 0;          // mean of the pain trend over the day (valid if taps > 0)
  int taps = 0;
  bool low_conf = true;    // fewer than 2 taps
  float p_mean = 0, p_min = 0, p_max = 0;  // hPa (valid if p_n > 0)
  int p_n = 0;
  float fall24 = 0;        // largest drop over any 24 h window ending this day (hPa, >= 0)
  float rise24 = 0;        // largest rise likewise
  bool has_change = false; // fall24/rise24 valid
};

// One entry per local day in [from, to) that has taps or pressure data.
std::vector<DayStats> daily(const pain::Result& r, const std::vector<PSample>& p, int32_t utc_offset_s,
                            int64_t from, int64_t to);

struct HourProfile {
  float pain[24];   // weighted mean tap score by local hour (mis-taps count less); NAN if none
  int taps[24];
};
HourProfile hour_profile(const pain::Result& r, int32_t utc_offset_s, int64_t from, int64_t to);

struct LagPoint {
  int window_h, lag_h;
  float r;  // NAN when not enough data
  int n;
};
// Pain at hour i+lag against the pressure change over `window_h` ending at hour i.
// Both series are hourly and the same length; NAN = missing.
std::vector<LagPoint> lag_correlation(const std::vector<float>& pain_hourly,
                                      const std::vector<float>& pressure_hourly,
                                      const std::vector<int>& windows_h, int max_lag_h, int lag_step_h,
                                      int min_n = 48);

// Chooses a sampling step (multiple of 30 min) giving at most max_points over a span.
int64_t pick_step(int64_t span_s, int max_points);

}  // namespace trends

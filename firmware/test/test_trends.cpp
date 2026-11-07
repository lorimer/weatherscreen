// Host-side tests for the long-term trend aggregates.
//   g++ -std=c++17 -O2 -I../src test_trends.cpp ../src/trends.cpp ../src/pain.cpp -o test_trends && ./test_trends
#include <cmath>
#include <cstdio>

#include "trends.h"

using namespace trends;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

const int64_t D = 86400, H = 3600;
const int64_t T0 = 1'800'000'000LL - 1'800'000'000LL % D;  // a UTC midnight

int main() {
  // Pressure: steady 1015 for day 0, falls 12 hPa linearly through day 1, steady day 2.
  std::vector<PSample> p;
  for (int64_t t = T0; t < T0 + 3 * D; t += 600) {
    float v = t < T0 + D ? 1015 : t < T0 + 2 * D ? 1015 - 12.0f * (t - T0 - D) / D : 1003;
    p.push_back({t, v});
  }
  // Pain: 2 on day 0 (three taps), a single 4 on day 2.
  auto r = pain::analyze({{T0 + 9 * H, 2}, {T0 + 13 * H, 2}, {T0 + 19 * H, 2}, {T0 + 2 * D + 10 * H, 4}});

  auto d = daily(r, p, 0, T0, T0 + 3 * D);
  CHECK(d.size() == 3, "expected 3 days, got %zu", d.size());
  if (d.size() == 3) {
    CHECK(d[0].taps == 3 && std::fabs(d[0].pain - 2) < 0.01f, "day0 pain %.2f taps %d", d[0].pain, d[0].taps);
    CHECK(d[1].taps == 0, "day1 has no taps");
    CHECK(d[2].taps == 1 && d[2].low_conf && std::fabs(d[2].pain - 4) < 0.01f, "day2 pain %.2f", d[2].pain);
    CHECK(std::fabs(d[0].p_mean - 1015) < 0.01f && std::fabs(d[0].p_max - d[0].p_min) < 0.01f, "day0 steady");
    CHECK(d[1].p_max - d[1].p_min > 11.5f, "day1 range %.2f", d[1].p_max - d[1].p_min);
    CHECK(d[1].has_change && d[1].fall24 > 11.0f && d[1].rise24 < 0.01f, "day1 fall %.2f rise %.2f", d[1].fall24, d[1].rise24);
    CHECK(d[2].has_change && d[2].fall24 > 0.5f, "day2 still shows the fall from yesterday: %.2f", d[2].fall24);
    CHECK(!d[0].has_change, "day0 has no 24 h history");
    std::printf("daily     -> %zu days, day1 fall %.1f hPa, day2 pain %.1f (1 tap)\n", d.size(), d[1].fall24, d[2].pain);
  }

  // Local-time bucketing: UTC-7 shifts the 09:00 UTC tap to 02:00 local.
  auto hp = hour_profile(r, -7 * 3600, T0, T0 + 3 * D);
  CHECK(hp.taps[2] == 1 && hp.taps[9] == 0, "hour bucketing (%d at 2h, %d at 9h)", hp.taps[2], hp.taps[9]);
  CHECK(std::isnan(hp.pain[5]) && std::fabs(hp.pain[2] - 2) < 0.01f, "hour means");

  // Lag correlation: pain follows pressure falls by 6 h.
  std::vector<float> pres, pain_h;
  for (int i = 0; i < 400; ++i) pres.push_back(1013 + 6 * std::sin(i / 15.0f));
  for (int i = 0; i < 400; ++i) {
    int j = i - 6;
    float dp = (j >= 3) ? pres[j] - pres[j - 3] : 0;
    pain_h.push_back(3 - dp);  // more pain after a fall
  }
  auto lc = lag_correlation(pain_h, pres, {3}, 12, 3);
  float best = 0;
  int best_lag = -1;
  for (auto& x : lc)
    if (!std::isnan(x.r) && std::fabs(x.r) > std::fabs(best)) { best = x.r; best_lag = x.lag_h; }
  CHECK(best_lag == 6 && best < -0.95f, "best lag %d r %.2f", best_lag, best);
  std::printf("lag       -> strongest r %.2f at %d h (expected 6 h, negative)\n", best, best_lag);

  CHECK(pick_step(2 * D, 600) == 1800, "2 days -> 30 min");
  CHECK(pick_step(365 * D, 600) % 1800 == 0 && 365 * D / pick_step(365 * D, 600) <= 600, "1 year step");

  std::printf(failures ? "\n%d FAILED\n" : "\nall tests passed\n", failures);
  return failures ? 1 : 0;
}

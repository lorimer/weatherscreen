// Host-side tests for the pain analysis. Build and run on a PC:
//   g++ -std=c++17 -O2 -I../src test_pain.cpp ../src/pain.cpp -o test_pain && ./test_pain
#include <cmath>
#include <cstdio>
#include <vector>

#include "pain.h"

using namespace pain;

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      ++failures;                                         \
      std::printf("FAIL %s:%d  ", __FILE__, __LINE__);    \
      std::printf(__VA_ARGS__);                           \
      std::printf("\n");                                  \
    }                                                     \
  } while (0)

const int64_t H = 3600, T0 = 1'800'000'000;  // arbitrary epoch

float at(const Result& r, int64_t t) {
  float v = NAN;
  trend_at(r, t, &v);
  return v;
}

void two_twos_and_a_five() {
  // The example from the requirements: 2, 2, 5 close together -> the 5 is a mis-tap.
  auto r = analyze({{T0, 2}, {T0 + 60, 2}, {T0 + 120, 5}});
  float v = at(r, T0 + 60);
  CHECK(v < 2.4f, "2,2,5 trend should be ~2, got %.2f", v);
  CHECK(r.robust_w[2] < 0.2f, "the 5 should be near-zero weight, got %.2f", r.robust_w[2]);
  CHECK(r.robust_w[0] > 0.8f, "the 2s should keep weight, got %.2f", r.robust_w[0]);
  std::printf("2,2,5     -> trend %.2f, weight of 5 = %.2f\n", v, r.robust_w[2]);
}

void three_fives_are_real() {
  auto r = analyze({{T0, 5}, {T0 + 60, 5}, {T0 + 120, 5}, {T0 - 4 * H, 2}});
  float v = at(r, T0 + 60);
  // A quick burst counts as one episode, so the trend blends it with the earlier 2,
  // but none of the 5s is treated as a mis-tap.
  for (size_t i = 1; i < r.robust_w.size(); ++i)
    CHECK(r.robust_w[i] > 0.8f, "repeated 5s must keep full weight, got %.2f", r.robust_w[i]);
  CHECK(v > 3.5f, "5,5,5 should pull the trend up, got %.2f", v);
  std::printf("5,5,5     -> trend %.2f, weights kept\n", v);
}

void sparse_use_not_penalised() {
  // One tap every two days: each stands at full weight, and the line breaks between.
  auto r = analyze({{T0, 4}, {T0 + 48 * H, 2}, {T0 + 96 * H, 5}});
  for (size_t i = 0; i < r.robust_w.size(); ++i)
    CHECK(r.robust_w[i] > 0.99f, "sparse tap %zu should keep full weight, got %.2f", i, r.robust_w[i]);
  CHECK(std::fabs(at(r, T0) - 4) < 0.01f, "single tap value should be the trend");
  CHECK(std::fabs(at(r, T0 + 96 * H) - 5) < 0.01f, "single 5 should not be discounted");
  float mid;
  CHECK(!trend_at(r, T0 + 24 * H, &mid), "48 h gap should break the line");
  std::printf("sparse    -> weights all 1, gap breaks line\n");
}

void lone_mistap_in_regular_use() {
  // Taps of 2 every 2 h for two days, one stray 5 in the middle.
  std::vector<Tap> t;
  for (int h = 0; h <= 48; h += 2) t.push_back({T0 + h * H, 2});
  t.push_back({T0 + 25 * H, 5});
  auto r = analyze(t);
  size_t idx = 0;
  for (size_t i = 0; i < r.taps.size(); ++i) if (r.taps[i].v == 5) idx = i;
  float v = at(r, T0 + 25 * H);
  CHECK(r.robust_w[idx] < 0.3f, "stray 5 should be discounted, got %.2f", r.robust_w[idx]);
  CHECK(v < 2.3f, "trend at stray tap should stay ~2, got %.2f", v);
  std::printf("stray 5   -> weight %.2f, trend %.2f\n", r.robust_w[idx], v);
}

void genuine_rise_followed() {
  // Pain rising steadily from 1 to 5 over 24 h, tapped hourly.
  std::vector<Tap> t;
  for (int h = 0; h <= 24; ++h) t.push_back({T0 + h * H, std::round(1 + 4.0f * h / 24)});
  auto r = analyze(t);
  float minw = 1;
  for (float w : r.robust_w) minw = std::min(minw, w);
  float start = at(r, T0 + 2 * H), end = at(r, T0 + 22 * H);
  CHECK(minw > 0.6f, "a genuine trend should not be treated as outliers (min weight %.2f)", minw);
  CHECK(end - start > 2.5f, "trend should rise, %.2f -> %.2f", start, end);
  std::printf("rise      -> %.2f to %.2f, min weight %.2f\n", start, end, minw);
}

void density_balanced_days() {
  // Day 1: 57 taps of 3. Day 3: a single 5. Each day gets its own value.
  const int64_t D0 = T0 - T0 % 86400;  // UTC midnight
  std::vector<Tap> t;
  for (int i = 0; i < 57; ++i) t.push_back({D0 + 8 * H + i * 600, 3});
  t.push_back({D0 + 48 * H + 12 * H, 5});
  auto r = analyze(t);
  auto days = daily_values(r, 0);
  CHECK(days.size() == 2, "expected 2 days, got %zu", days.size());
  if (days.size() == 2) {
    CHECK(std::fabs(days[0].v - 3) < 0.05f, "busy day should be 3, got %.2f", days[0].v);
    CHECK(std::fabs(days[1].v - 5) < 0.05f, "single-tap day should be 5, got %.2f", days[1].v);
    CHECK(days[1].low_confidence && !days[0].low_confidence, "confidence flags");
    std::printf("days      -> busy %.2f (%d taps), sparse %.2f (%d tap, low conf)\n",
                days[0].v, days[0].taps, days[1].v, days[1].taps);
  }
}

void burst_does_not_dominate() {
  // 20 rapid taps of 4 between single 2s three hours either side.
  std::vector<Tap> t{{T0 - 3 * H, 2}, {T0 + 3 * H, 2}};
  for (int i = 0; i < 20; ++i) t.push_back({T0 + i * 30, 4});
  auto r = analyze(t);
  float v = at(r, T0);
  CHECK(v < 3.4f, "a burst should count about once, got %.2f", v);
  std::printf("burst     -> trend %.2f (unbalanced would be ~3.8)\n", v);
}

void series_and_pearson() {
  auto r = analyze({{T0, 3}, {T0 + 3 * H, 3}});
  auto s = trend_series(r, T0 - 30 * H, T0 + 3 * H, 1800);
  CHECK(!s.front().ok && s.back().ok, "series gap flags");
  int n;
  float c = pearson({1, 2, 3, 4, NAN}, {2, 4, 6, 8, 1}, 3, &n);
  CHECK(std::fabs(c - 1) < 1e-4 && n == 4, "pearson %.3f n=%d", c, n);
}

int main() {
  two_twos_and_a_five();
  three_fives_are_real();
  sparse_use_not_penalised();
  lone_mistap_in_regular_use();
  genuine_rise_followed();
  density_balanced_days();
  burst_does_not_dominate();
  series_and_pearson();
  std::printf(failures ? "\n%d FAILED\n" : "\nall tests passed\n", failures);
  return failures ? 1 : 0;
}

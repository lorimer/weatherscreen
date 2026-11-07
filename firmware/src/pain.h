// Pain-score analysis: turns noisy, irregular 1-5 taps into a smooth trend.
//
// Plain C++ with no Arduino dependencies so it can be unit-tested on a PC
// (see test/test_pain.cpp) and mirrored exactly in tools/analyze.py.
//
// Method (see the spec, "Pain analysis"):
//  1. Density balancing: each tap's base weight is 1 / (taps within +-density_window),
//     so a burst of 57 taps carries about as much weight as one tap on its own.
//  2. Robust local trend: locally weighted regression (tricube kernel, +-bandwidth).
//     The slope term is damped by how far off-centre the nearby taps sit, so the fit
//     slides from local-linear in the middle of the data to the local mean at the edges
//     (and at "now") instead of switching between the two. The kernel also widens
//     smoothly across a gap. Both keep steps out of the drawn line.
//  3. Outlier down-weighting that adapts to usage: a tap far from the trend loses
//     weight (bisquare on robust residual scale), but only in proportion to how much
//     nearby evidence exists to contradict it. A lone tap keeps full weight.
//  4. Everything is recomputed from raw taps each time (retroactive).
//  5. Daily value = mean of the trend over the day, so each day counts once.
#pragma once
#include <cstdint>
#include <vector>

namespace pain {

struct Tap {
  int64_t t;  // unix seconds (UTC)
  float v;    // score 1..5
};

struct Params {
  float bandwidth_h = 12.0f;        // trend kernel half-width
  float gap_h = 18.0f;              // no tap within this distance -> no trend (line breaks)
  float density_window_h = 1.5f;    // +-window for density balancing
  float support_full = 2.0f;        // other taps within bandwidth needed for full outlier power
  float cutoff_floor = 2.5f;        // residuals below this many points are never treated as mis-taps
  float cutoff_mad_mult = 4.0f;     // cutoff = max(floor, mult * median |residual|)
  int robust_iters = 4;
};

struct Result {
  std::vector<Tap> taps;            // sorted copy of the input
  std::vector<float> base_w;        // density weights
  std::vector<float> robust_w;      // 0..1 outlier weights (1 = fully trusted)
  Params p;
};

struct TrendPoint {
  int64_t t;
  float v;     // valid only if ok
  bool ok;
};

struct DayValue {
  int64_t day_start;  // local midnight, unix seconds
  float v;            // mean of trend over the day
  int taps;           // raw taps that day
  bool low_confidence;
};

// Fit weights from raw taps (any order).
Result analyze(const std::vector<Tap>& taps, const Params& p = Params());

// Trend value at time t. Returns false if no data close enough (gap).
bool trend_at(const Result& r, int64_t t, float* out);

// Trend sampled every step_s seconds over [from, to].
std::vector<TrendPoint> trend_series(const Result& r, int64_t from, int64_t to, int64_t step_s);

// One value per local day for days that have at least one tap.
// utc_offset_s converts UTC to local time for day boundaries.
std::vector<DayValue> daily_values(const Result& r, int32_t utc_offset_s, int64_t step_s = 1800);

// Pearson correlation of two equally long series, skipping pairs where either is NaN.
// Returns NaN when fewer than min_n pairs.
float pearson(const std::vector<float>& a, const std::vector<float>& b, int min_n, int* n_out);

}  // namespace pain

#include "pain.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace pain {
namespace {

constexpr double kHour = 3600.0;

inline float tricube(double u) {
  u = std::fabs(u);
  if (u >= 1.0) return 0.0f;
  double a = 1.0 - u * u * u;
  return static_cast<float>(a * a * a);
}

inline float bisquare(double r, double c) {
  double u = r / c;
  if (std::fabs(u) >= 1.0) return 0.0f;
  double a = 1.0 - u * u;
  return static_cast<float>(a * a);
}

// Index of first tap with t >= x.
size_t lower(const std::vector<Tap>& taps, int64_t x) {
  return std::lower_bound(taps.begin(), taps.end(), x,
                          [](const Tap& a, int64_t v) { return a.t < v; }) -
         taps.begin();
}

// Locally weighted fit at time t with half-width h_s seconds.
// Uses local-linear when there is data on both sides, otherwise local-constant
// (avoids wild extrapolation at the "now" edge of the graph).
bool local_fit(const std::vector<Tap>& taps, const std::vector<float>& w, int64_t t,
               double h_s, float* out) {
  size_t i0 = lower(taps, t - static_cast<int64_t>(h_s));
  double sw = 0, sx = 0, sy = 0;
  for (size_t i = i0; i < taps.size() && taps[i].t <= t + h_s; ++i) {
    double dx = static_cast<double>(taps[i].t - t);
    double k = tricube(dx / h_s) * w[i];
    if (k <= 0) continue;
    double xh = dx / kHour;
    sw += k;
    sx += k * xh;
    sy += k * taps[i].v;
  }
  if (sw < 1e-9) return false;
  double xm = sx / sw, ym = sy / sw;
  double val = ym;
  // The slope term carries the fit from the weighted mean at xm across to time t, so
  // the further the nearby taps sit to one side, the more it is extrapolation and the
  // less it can be trusted. Damp it by how far off-centre they are, which goes to zero
  // at the edges of the data (and at "now") and to one in the middle.
  //
  // This used to be an on/off test for taps on both sides. That made the estimator
  // switch form abruptly as t swept past a tap, which put visible steps in the line.
  const double h_h = h_s / kHour;
  const double trust = std::max(0.0, 1.0 - 2.0 * std::fabs(xm) / h_h);
  if (trust > 0) {
    double sxx = 0, sxy = 0;
    for (size_t i = i0; i < taps.size() && taps[i].t <= t + h_s; ++i) {
      double dx = static_cast<double>(taps[i].t - t);
      double k = tricube(dx / h_s) * w[i];
      if (k <= 0) continue;
      double xh = dx / kHour - xm;
      sxx += k * xh * xh;
      sxy += k * xh * (taps[i].v - ym);
    }
    // sxx is the weighted spread in hours^2; when the taps are nearly coincident the
    // slope is meaningless, so require a real spread rather than just non-zero.
    if (sxx / sw > 0.25) val = ym - trust * (sxy / sxx) * xm;  // value at x = 0 (time t)
  }
  *out = static_cast<float>(std::min(5.0, std::max(1.0, val)));
  return true;
}

float nearest_gap_h(const std::vector<Tap>& taps, int64_t t) {
  if (taps.empty()) return 1e9f;
  size_t i = lower(taps, t);
  double best = 1e18;
  if (i < taps.size()) best = std::min(best, static_cast<double>(taps[i].t - t));
  if (i > 0) best = std::min(best, static_cast<double>(t - taps[i - 1].t));
  return static_cast<float>(best / kHour);
}

}  // namespace

Result analyze(const std::vector<Tap>& in, const Params& p) {
  Result r;
  r.p = p;
  r.taps = in;
  std::sort(r.taps.begin(), r.taps.end(), [](const Tap& a, const Tap& b) { return a.t < b.t; });
  const auto& taps = r.taps;
  const size_t n = taps.size();
  r.base_w.assign(n, 1.0f);
  r.robust_w.assign(n, 1.0f);
  if (n == 0) return r;

  // 1. Density balancing and 3a. how much nearby evidence each tap has.
  const int64_t dw = static_cast<int64_t>(p.density_window_h * kHour);
  const int64_t bw = static_cast<int64_t>(p.bandwidth_h * kHour);
  std::vector<float> conf(n, 0.0f);
  for (size_t i = 0; i < n; ++i) {
    size_t a = lower(taps, taps[i].t - dw), b = lower(taps, taps[i].t + dw + 1);
    r.base_w[i] = 1.0f / static_cast<float>(b - a);
    size_t c = lower(taps, taps[i].t - bw), d = lower(taps, taps[i].t + bw + 1);
    float others = static_cast<float>(d - c - 1);
    conf[i] = std::min(1.0f, others / p.support_full);
  }

  // 2-3. Robust iterations.
  std::vector<float> w(n), resid(n), absr;
  for (int it = 0; it < p.robust_iters; ++it) {
    for (size_t i = 0; i < n; ++i) w[i] = r.base_w[i] * r.robust_w[i];
    absr.clear();
    for (size_t i = 0; i < n; ++i) {
      float f;
      if (!local_fit(taps, w, taps[i].t, bw, &f)) f = taps[i].v;
      resid[i] = taps[i].v - f;
      absr.push_back(std::fabs(resid[i]));
    }
    std::nth_element(absr.begin(), absr.begin() + absr.size() / 2, absr.end());
    float mad = absr[absr.size() / 2];
    float cutoff = std::max(p.cutoff_floor, p.cutoff_mad_mult * mad);
    for (size_t i = 0; i < n; ++i) {
      float b = bisquare(resid[i], cutoff);
      r.robust_w[i] = 1.0f - conf[i] * (1.0f - b);
    }
  }
  return r;
}

bool trend_at(const Result& r, int64_t t, float* out) {
  const auto& taps = r.taps;
  if (taps.empty()) return false;
  float gap = nearest_gap_h(taps, t);
  if (gap > r.p.gap_h) return false;
  double h = std::max(r.p.bandwidth_h, gap) * kHour;  // grows smoothly across a gap
  std::vector<float> w(taps.size());
  for (size_t i = 0; i < taps.size(); ++i) w[i] = r.base_w[i] * r.robust_w[i];
  if (local_fit(taps, w, t, h, out)) return true;
  return local_fit(taps, r.base_w, t, h, out);  // every nearby tap was an outlier
}

std::vector<TrendPoint> trend_series(const Result& r, int64_t from, int64_t to, int64_t step_s) {
  std::vector<TrendPoint> out;
  if (step_s <= 0 || to < from) return out;
  // Build the combined weights once.
  const auto& taps = r.taps;
  std::vector<float> w(taps.size()), wb(taps.size());
  for (size_t i = 0; i < taps.size(); ++i) {
    w[i] = r.base_w[i] * r.robust_w[i];
    wb[i] = r.base_w[i];
  }
  for (int64_t t = from; t <= to; t += step_s) {
    TrendPoint tp{t, 0.0f, false};
    float gap = nearest_gap_h(taps, t);
    if (gap <= r.p.gap_h) {
      double h = std::max(r.p.bandwidth_h, gap) * kHour;  // grows smoothly across a gap
      tp.ok = local_fit(taps, w, t, h, &tp.v) || local_fit(taps, wb, t, h, &tp.v);
    }
    out.push_back(tp);
  }
  return out;
}

std::vector<DayValue> daily_values(const Result& r, int32_t off, int64_t step_s) {
  std::map<int64_t, int> counts;
  for (const auto& t : r.taps) {
    int64_t local = t.t + off;
    int64_t key = (local >= 0 ? local / 86400 : (local - 86399) / 86400);
    counts[key]++;
  }
  std::vector<DayValue> out;
  for (const auto& kv : counts) {
    int64_t start = kv.first * 86400 - off;
    auto pts = trend_series(r, start, start + 86400 - step_s, step_s);
    double s = 0;
    int k = 0;
    for (const auto& p : pts)
      if (p.ok) { s += p.v; ++k; }
    if (k == 0) continue;
    out.push_back({start, static_cast<float>(s / k), kv.second, kv.second < 2});
  }
  return out;
}

float pearson(const std::vector<float>& a, const std::vector<float>& b, int min_n, int* n_out) {
  double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
  int n = 0;
  size_t m = std::min(a.size(), b.size());
  for (size_t i = 0; i < m; ++i) {
    if (std::isnan(a[i]) || std::isnan(b[i])) continue;
    sa += a[i]; sb += b[i];
    saa += a[i] * a[i]; sbb += b[i] * b[i]; sab += a[i] * b[i];
    ++n;
  }
  if (n_out) *n_out = n;
  if (n < min_n) return NAN;
  double cov = sab - sa * sb / n, va = saa - sa * sa / n, vb = sbb - sb * sb / n;
  if (va <= 1e-12 || vb <= 1e-12) return NAN;
  return static_cast<float>(cov / std::sqrt(va * vb));
}

}  // namespace pain

#include "trends.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace trends {
namespace {

int64_t local_day(int64_t t, int32_t off) {
  int64_t l = t + off;
  return l >= 0 ? l / 86400 : (l - 86399) / 86400;
}

}  // namespace

float pressure_near(const std::vector<PSample>& s, int64_t t, int64_t win) {
  auto it = std::lower_bound(s.begin(), s.end(), t - win, [](const PSample& a, int64_t v) { return a.t < v; });
  double sum = 0;
  int n = 0;
  for (; it != s.end() && it->t <= t + win; ++it) {
    sum += it->hpa;
    ++n;
  }
  return n ? static_cast<float>(sum / n) : NAN;
}

std::vector<DayStats> daily(const pain::Result& r, const std::vector<PSample>& p, int32_t off, int64_t from,
                            int64_t to) {
  std::map<int64_t, DayStats> days;
  int64_t d0 = local_day(from, off), d1 = local_day(to - 1, off);

  // Pain: reuse the exact daily values shown elsewhere.
  for (const auto& dv : pain::daily_values(r, off)) {
    int64_t k = local_day(dv.day_start, off);
    if (k < d0 || k > d1) continue;
    auto& d = days[k];
    d.day_start = dv.day_start;
    d.pain = dv.v;
    d.taps = dv.taps;
    d.low_conf = dv.low_confidence;
  }

  // Pressure: per-day mean/min/max from the samples.
  auto it = std::lower_bound(p.begin(), p.end(), from, [](const PSample& a, int64_t v) { return a.t < v; });
  for (; it != p.end() && it->t < to; ++it) {
    int64_t k = local_day(it->t, off);
    auto& d = days[k];
    d.day_start = k * 86400 - off;
    if (d.p_n == 0) {
      d.p_min = d.p_max = it->hpa;
      d.p_mean = 0;
    }
    d.p_mean += it->hpa;
    d.p_min = std::min(d.p_min, it->hpa);
    d.p_max = std::max(d.p_max, it->hpa);
    d.p_n++;
  }

  std::vector<DayStats> out;
  for (auto& kv : days) {
    DayStats d = kv.second;
    if (d.p_n) d.p_mean /= d.p_n;
    // Largest 24 h change ending at any hour of this day.
    float fall = -1e9f, rise = -1e9f;
    for (int64_t t = d.day_start; t < d.day_start + 86400; t += 3600) {
      float a = pressure_near(p, t - 86400, 1800), b = pressure_near(p, t, 1800);
      if (std::isnan(a) || std::isnan(b)) continue;
      fall = std::max(fall, a - b);
      rise = std::max(rise, b - a);
      d.has_change = true;
    }
    if (d.has_change) {
      d.fall24 = std::max(0.0f, fall);
      d.rise24 = std::max(0.0f, rise);
    }
    out.push_back(d);
  }
  return out;
}

HourProfile hour_profile(const pain::Result& r, int32_t off, int64_t from, int64_t to) {
  HourProfile hp;
  double sw[24] = {0}, sv[24] = {0};
  for (int h = 0; h < 24; ++h) hp.taps[h] = 0;
  for (size_t i = 0; i < r.taps.size(); ++i) {
    int64_t t = r.taps[i].t;
    if (t < from || t >= to) continue;
    int64_t l = t + off;
    int h = static_cast<int>(((l % 86400) + 86400) % 86400 / 3600);
    double w = r.robust_w[i];
    sw[h] += w;
    sv[h] += w * r.taps[i].v;
    hp.taps[h]++;
  }
  for (int h = 0; h < 24; ++h) hp.pain[h] = sw[h] > 1e-6 ? static_cast<float>(sv[h] / sw[h]) : NAN;
  return hp;
}

std::vector<LagPoint> lag_correlation(const std::vector<float>& pain_h, const std::vector<float>& pres_h,
                                      const std::vector<int>& windows, int max_lag, int lag_step, int min_n) {
  std::vector<LagPoint> out;
  const size_t n = std::min(pain_h.size(), pres_h.size());
  for (int w : windows) {
    std::vector<float> dp(n, NAN);
    for (size_t i = w; i < n; ++i)
      if (!std::isnan(pres_h[i]) && !std::isnan(pres_h[i - w])) dp[i] = pres_h[i] - pres_h[i - w];
    for (int lag = 0; lag <= max_lag; lag += lag_step) {
      std::vector<float> a, b;
      for (size_t i = 0; i + lag < n; ++i) {
        a.push_back(dp[i]);
        b.push_back(pain_h[i + lag]);
      }
      int cnt = 0;
      float c = pain::pearson(a, b, min_n, &cnt);
      out.push_back({w, lag, c, cnt});
    }
  }
  return out;
}

int64_t pick_step(int64_t span, int max_points) {
  const int64_t half_hour = 1800;
  if (max_points < 2) max_points = 2;
  int64_t step = (span + max_points - 2) / (max_points - 1);
  step = ((step + half_hour - 1) / half_hour) * half_hour;
  return std::max(half_hour, step);
}

}  // namespace trends

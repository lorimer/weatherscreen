#!/usr/bin/env python3
"""
Re-runs the station's pain analysis on a computer, from the CSV files downloaded from
the web page (Data & updates -> pain.csv, pressure.csv).

It is a line-for-line port of firmware/src/pain.cpp, so results match the screen.
Change the parameters to explore; raw data is never modified.

Usage:
  python3 analyze.py pain.csv pressure.csv [--utc-offset -7] [--days 90] [--plot out.png]

Outputs: daily values (CSV to stdout or --daily file) and a pressure-change vs pain
correlation table (lags 0-48 h).
"""
import argparse
import bisect
import csv
import math
from datetime import datetime, timedelta, timezone

HOUR = 3600.0


class Params:
    bandwidth_h = 12.0
    gap_h = 18.0
    density_window_h = 1.5
    support_full = 2.0
    cutoff_floor = 2.5
    cutoff_mad_mult = 4.0
    robust_iters = 4


def tricube(u):
    u = abs(u)
    return 0.0 if u >= 1 else (1 - u ** 3) ** 3


def bisquare(r, c):
    u = r / c
    return 0.0 if abs(u) >= 1 else (1 - u * u) ** 2


def local_fit(ts, vs, w, t, h_s):
    i0 = bisect.bisect_left(ts, t - int(h_s))
    sw = sx = sy = 0.0
    idx = []
    for i in range(i0, len(ts)):
        if ts[i] > t + h_s:
            break
        dx = ts[i] - t
        k = tricube(dx / h_s) * w[i]
        if k <= 0:
            continue
        xh = dx / HOUR
        sw += k; sx += k * xh; sy += k * vs[i]
        idx.append((i, k, xh))
    if sw < 1e-9:
        return None
    xm, ym = sx / sw, sy / sw
    val = ym
    # The slope carries the fit from the weighted mean at xm across to time t, so the
    # further the nearby taps sit to one side, the more it is extrapolation. Damp it by
    # how far off-centre they are: zero at the edges of the data, one in the middle.
    h_h = h_s / HOUR
    trust = max(0.0, 1.0 - 2.0 * abs(xm) / h_h)
    if trust > 0:
        sxx = sum(k * (xh - xm) ** 2 for _, k, xh in idx)
        sxy = sum(k * (xh - xm) * (vs[i] - ym) for i, k, xh in idx)
        if sxx / sw > 0.25:
            val = ym - trust * (sxy / sxx) * xm
    return min(5.0, max(1.0, val))


class Result:
    def __init__(self, taps, p=Params()):
        taps = sorted(taps)
        self.ts = [t for t, _ in taps]
        self.vs = [float(v) for _, v in taps]
        self.p = p
        n = len(taps)
        dw, bw = int(p.density_window_h * HOUR), int(p.bandwidth_h * HOUR)
        self.base = [1.0] * n
        self.robust = [1.0] * n
        conf = [0.0] * n
        for i, t in enumerate(self.ts):
            a, b = bisect.bisect_left(self.ts, t - dw), bisect.bisect_left(self.ts, t + dw + 1)
            self.base[i] = 1.0 / (b - a)
            c, d = bisect.bisect_left(self.ts, t - bw), bisect.bisect_left(self.ts, t + bw + 1)
            conf[i] = min(1.0, (d - c - 1) / p.support_full)
        for _ in range(p.robust_iters if n else 0):
            w = [b * r for b, r in zip(self.base, self.robust)]
            resid = []
            for i, t in enumerate(self.ts):
                f = local_fit(self.ts, self.vs, w, t, bw)
                resid.append(self.vs[i] - (self.vs[i] if f is None else f))
            absr = sorted(abs(r) for r in resid)
            mad = absr[len(absr) // 2]
            cutoff = max(p.cutoff_floor, p.cutoff_mad_mult * mad)
            self.robust = [1 - conf[i] * (1 - bisquare(resid[i], cutoff)) for i in range(n)]

    def gap_h(self, t):
        if not self.ts:
            return 1e9
        i = bisect.bisect_left(self.ts, t)
        best = 1e18
        if i < len(self.ts):
            best = min(best, self.ts[i] - t)
        if i > 0:
            best = min(best, t - self.ts[i - 1])
        return best / HOUR

    def trend(self, t):
        g = self.gap_h(t)
        if g > self.p.gap_h:
            return None
        h = max(self.p.bandwidth_h, g) * HOUR  # grows smoothly across a gap
        w = [b * r for b, r in zip(self.base, self.robust)]
        v = local_fit(self.ts, self.vs, w, t, h)
        return v if v is not None else local_fit(self.ts, self.vs, self.base, t, h)

    def daily(self, off_s, step=1800):
        days = {}
        for t in self.ts:
            days.setdefault((t + off_s) // 86400, 0)
            days[(t + off_s) // 86400] += 1
        out = []
        for key, n in sorted(days.items()):
            start = key * 86400 - off_s
            pts = [self.trend(t) for t in range(start, start + 86400, step)]
            pts = [p for p in pts if p is not None]
            if pts:
                out.append((start, sum(pts) / len(pts), n, n < 2))
        return out


def pearson(a, b, min_n=48):
    pairs = [(x, y) for x, y in zip(a, b) if x is not None and y is not None]
    n = len(pairs)
    if n < min_n:
        return None, n
    ma = sum(x for x, _ in pairs) / n
    mb = sum(y for _, y in pairs) / n
    va = sum((x - ma) ** 2 for x, _ in pairs)
    vb = sum((y - mb) ** 2 for _, y in pairs)
    if va <= 1e-12 or vb <= 1e-12:
        return None, n
    return sum((x - ma) * (y - mb) for x, y in pairs) / math.sqrt(va * vb), n


def load_pain(path):
    with open(path) as f:
        return [(int(r[0]), int(r[1])) for r in csv.reader(f)
                if r and r[0].isdigit() and int(r[0]) > 1_600_000_000 and 1 <= int(r[1]) <= 5]


def load_pressure(path):
    out = []
    with open(path) as f:
        for r in csv.reader(f):
            if r and r[0].isdigit():
                try:
                    out.append((int(r[0]), float(r[2])))
                except ValueError:
                    pass
    return sorted(out)


def pressure_at(ps, t, win=1800):
    """Mean sea-level pressure (hPa) of samples within +-win seconds of t."""
    times = pressure_at.times if getattr(pressure_at, "src", None) is ps else None
    if times is None:
        pressure_at.src, pressure_at.times = ps, [x[0] for x in ps]
        times = pressure_at.times
    lo, hi = bisect.bisect_left(times, t - win), bisect.bisect_right(times, t + win)
    vals = [v for _, v in ps[lo:hi]]
    return sum(vals) / len(vals) if vals else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pain_csv")
    ap.add_argument("pressure_csv", nargs="?")
    ap.add_argument("--utc-offset", type=float, default=0, help="hours, e.g. -7 for MST")
    ap.add_argument("--days", type=int, default=90)
    ap.add_argument("--daily", help="write daily values to this CSV")
    ap.add_argument("--plot", help="save a chart (needs matplotlib)")
    for k in ("bandwidth_h", "gap_h", "density_window_h", "support_full", "cutoff_floor"):
        ap.add_argument("--" + k.replace("_", "-"), type=float, default=getattr(Params, k))
    a = ap.parse_args()
    p = Params()
    for k in ("bandwidth_h", "gap_h", "density_window_h", "support_full", "cutoff_floor"):
        setattr(p, k, getattr(a, k))

    taps = load_pain(a.pain_csv)
    r = Result(taps, p)
    off = int(a.utc_offset * 3600)
    daily = r.daily(off)
    lines = ["date,pain_trend_mean,taps,low_confidence"]
    for start, v, n, low in daily:
        d = datetime.fromtimestamp(start + off, timezone.utc).strftime("%Y-%m-%d")
        lines.append(f"{d},{v:.2f},{n},{int(low)}")
    if a.daily:
        open(a.daily, "w").write("\n".join(lines) + "\n")
    else:
        print("\n".join(lines))
    discounted = sum(1 for w in r.robust if w < 0.5)
    print(f"\n{len(taps)} taps, {discounted} judged likely mis-taps (weight < 0.5)")

    if a.pressure_csv:
        ps = load_pressure(a.pressure_csv)
        end = (max(r.ts[-1] if r.ts else 0, ps[-1][0] if ps else 0) // 3600) * 3600
        start = end - a.days * 86400
        hours = list(range(start, end + 1, 3600))
        pain_h = [r.trend(t) for t in hours]
        pres_h = [pressure_at(ps, t) for t in hours]
        print("\nPressure change vs pain (negative r: more pain after pressure falls)")
        print(f"{'window':>7} {'lag':>5} {'r':>7} {'n':>6}")
        for w in (3, 12, 24):
            dp = [None] * len(hours)
            for i in range(w, len(hours)):
                if pres_h[i] is not None and pres_h[i - w] is not None:
                    dp[i] = pres_h[i] - pres_h[i - w]
            best = None
            for lag in range(0, 49, 3):
                c, n = pearson(dp[:len(hours) - lag], pain_h[lag:])
                if c is not None and (best is None or abs(c) > abs(best[0])):
                    best = (c, lag, n)
                print(f"{w:>5} h {lag:>4}h {('%.3f' % c) if c is not None else '   --':>7} {n:>6}")
            if best:
                print(f"  -> {w} h window: strongest r = {best[0]:.2f} with pain {best[1]} h later")

        if a.plot:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(12, 6))
            tt = [datetime.fromtimestamp(t + off, timezone.utc) for t in hours]
            ax1.plot(tt, [None if v is None else v * 0.02953 for v in pres_h], color="#2B5F9E")
            ax1.set_ylabel("Pressure (inHg)")
            ax2.plot(tt, pain_h, color="#B3261E")
            sel = [(datetime.fromtimestamp(t + off, timezone.utc), v, w) for t, v, w in zip(r.ts, r.vs, r.robust) if t >= start]
            ax2.scatter([s[0] for s in sel], [s[1] for s in sel], c=[(0.7, 0.15, 0.12, 0.15 + 0.7 * s[2]) for s in sel], s=14)
            ax2.set_ylabel("Pain (1-5)"); ax2.set_ylim(0.8, 5.2)
            fig.tight_layout(); fig.savefig(a.plot)
            print(f"saved {a.plot}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Serves the station's web page (from src/web_page.h) with a mock API backed by 14 months
of synthetic data, for testing the UI in a browser without hardware.
    python3 mock_server.py [port]      then open http://localhost:8765/
"""
import json, math, random, re, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent.parent / "tools"))
import analyze as A  # same algorithm as the device

page = (HERE.parent.parent / "src" / "web_page.h").read_text()
PAGE = re.search(r'R"HTML\((.*)\)HTML"', page, re.S).group(1)
INHG = 0.02953
OFF = -7 * 3600
NOW = int(time.time()) // 600 * 600
START = NOW - 425 * 86400
random.seed(7)

# Pressure: slow random weather systems plus occasional sharp fronts.
pres, p, v = [], 1015.0, 0.0
for t in range(START, NOW + 1, 600):
    v += random.gauss(0, 0.02) - 0.0015 * (p - 1015) - 0.02 * v
    if random.random() < 0.0009: v -= random.uniform(0.25, 0.5)   # front arriving
    p += v
    pres.append((t, p))
ptimes = [t for t, _ in pres]

def p_at(t, win=1800):
    import bisect
    lo, hi = bisect.bisect_left(ptimes, t - win), bisect.bisect_right(ptimes, t + win)
    vals = [x for _, x in pres[lo:hi]]
    return sum(vals) / len(vals) if vals else None

# Pain: baseline + response to the 12 h pressure fall 8 h earlier; uneven logging.
taps = []
for d in range(425):
    day0 = START + d * 86400
    mode = random.choices(["none", "one", "few", "many"], [0.2, 0.25, 0.4, 0.15])[0]
    n = {"none": 0, "one": 1, "few": random.randint(2, 5), "many": random.randint(12, 45)}[mode]
    for _ in range(n):
        t = day0 + random.randint(14 * 3600, 28 * 3600)  # ~7am-9pm local
        a, b = p_at(t - 8 * 3600 - 12 * 3600), p_at(t - 8 * 3600)
        fall = (a - b) if a and b else 0
        lvl = 2.2 + 0.35 * math.sin(d / 40) + 0.25 * fall + random.gauss(0, 0.5)
        s = max(1, min(5, round(lvl)))
        if random.random() < 0.04: s = random.choice([1, 5])          # mis-tap
        taps.append((t, s))
        if mode == "many" and random.random() < 0.3:                   # quick double tap
            taps.append((t + random.randint(2, 40), s))
taps.sort()
print(f"{len(taps)} taps, {len(pres)} pressure samples")
R = A.Result(taps)

def series(fr, to):
    span = to - fr
    step = max(1800, -(-((span + 598) // 599) // 1800) * 1800)
    fr -= fr % step
    ts = list(range(fr, to + 1, step))
    pr = [p_at(t, max(1800, step // 2)) for t in ts]
    pn = [R.trend(t) for t in ts]
    tp = [[t, int(v), round(w, 2)] for t, v, w in zip(R.ts, R.vs, R.robust) if fr <= t <= to] if span <= 31 * 86400 else []
    return {"start": fr, "step": step, "pressure_inhg": [None if x is None else round(x * INHG, 3) for x in pr],
            "pain": [None if x is None else round(x, 2) for x in pn], "taps": tp}

DAILY = {s: (v, n, low) for s, v, n, low in R.daily(OFF)}
def daily(fr, to):
    out = []
    d0 = (fr + OFF) // 86400
    while d0 * 86400 - OFF < to:
        ds = d0 * 86400 - OFF
        samples = [x for t, x in pres if ds <= t < ds + 86400]
        falls = []
        for t in range(ds, ds + 86400, 3600):
            a, b = p_at(t - 86400), p_at(t)
            if a and b: falls.append((a - b, b - a))
        pv = DAILY.get(ds)
        if samples or pv:
            out.append({"day": ds + OFF, "pain": round(pv[0], 2) if pv else None, "taps": pv[1] if pv else 0,
                        "low_conf": pv[2] if pv else True,
                        "p_mean": round(sum(samples) / len(samples) * INHG, 3) if samples else None,
                        "p_min": round(min(samples) * INHG, 3) if samples else None,
                        "p_max": round(max(samples) * INHG, 3) if samples else None,
                        "fall24": round(max(0, max(f for f, _ in falls)) * INHG, 3) if falls else None,
                        "rise24": round(max(0, max(r for _, r in falls)) * INHG, 3) if falls else None})
        d0 += 1
    return {"utc_offset": OFF, "days": out}

def profile(fr, to):
    sw, sv, n = [0.0] * 24, [0.0] * 24, [0] * 24
    for t, v, w in zip(R.ts, R.vs, R.robust):
        if fr <= t < to:
            h = ((t + OFF) % 86400) // 3600
            sw[h] += w; sv[h] += w * v; n[h] += 1
    return {"pain": [round(sv[h] / sw[h], 2) if sw[h] > 1e-6 else None for h in range(24)], "taps": n}

def correlation(fr, to):
    hs = list(range(fr - fr % 3600, to + 1, 3600))
    pn = [R.trend(t) for t in hs]; pr = [p_at(t) for t in hs]
    out = []
    for w in (3, 12, 24):
        dp = [None] * len(hs)
        for i in range(w, len(hs)):
            if pr[i] is not None and pr[i - w] is not None: dp[i] = pr[i] - pr[i - w]
        for lag in range(0, 49, 3):
            c, n = A.pearson(dp[:len(hs) - lag], pn[lag:])
            out.append({"window_h": w, "lag_h": lag, "r": None if c is None else round(c, 3), "n": n})
    return {"lags": out}

STATUS = {"device_name": "weatherstation-a1b2", "hostname": "weatherstation-a1b2", "place": "Grand Junction, Colorado, United States",
          "firmware": "1.1.0", "uptime_s": 86400 * 3, "ip": "192.168.1.50", "wifi": "HomeNetwork (-52 dBm)", "weather_ok": True,
          "weather_error": "", "weather_fetched": NOW - 300, "sensor": "BME280", "last_sensor": NOW - 60, "sensor_humidity": 31,
          "storage": "microSD, 3 of 15193 MB used", "taps": len(taps), "pressure_samples": len(pres), "backlight": 100,
          "touch_ok": len(taps) + 40, "touch_filtered": 37, "heap": 180000, "psram": 6100000, "pressure_inhg": 29.84,
          "trend": "falling", "temp_f": 72, "condition": "Sunny"}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(200); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(b)))
        self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urlparse(self.path); q = {k: v[0] for k, v in parse_qs(u.query).items()}
        to = int(q.get("to", 0)) or NOW; fr = int(q.get("from", 0)) or to - 2 * 86400
        fr, to = max(fr, START - 86400), min(to, NOW)
        routes = {"/api/status": lambda: STATUS, "/api/range": lambda: {"first": taps[0][0], "now": NOW},
                  "/api/series": lambda: series(fr, to), "/api/daily": lambda: daily(fr, to),
                  "/api/profile": lambda: profile(fr, to), "/api/correlation": lambda: correlation(fr, to),
                  "/api/settings": lambda: {"device_name": "weatherstation-a1b2"}}
        if u.path == "/": return self.send(PAGE, "text/html")
        if u.path in routes: return self.send(json.dumps(routes[u.path]()))
        self.send_response(404); self.end_headers()

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()

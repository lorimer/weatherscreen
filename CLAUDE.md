# CLAUDE.md: project context for Claude Code

This file hands over the full context of the design conversation that produced this
repo. Read it before changing anything. The README is user-facing; this file is for
whoever (human or AI) continues development.

## What this is

A 7" ESP32-S3 tabletop weather station for an **elderly family member**. It shows the
internet forecast, logs barometric pressure, and lets him log pain (1-5) with one tap,
so pain can be correlated with pressure changes. Family members maintain it remotely
(web UI + OTA over VPN), so nobody has to visit.

Spec doc (Claude Docs artifact, may be out of date in small details):
https://claude.ai/artifact/SgFygNMMvvXpWQ6WgSL6Pc
Approved screen mockup (Claude Design artifact): https://claude.ai/artifact/RAsStMi68Cq138KQC3gu2n

## Status (as of v1.2.0, 2026-11-07)

- **Never run on the Waveshare 7B yet**, but no longer never run at all. The firmware
  was brought up on a LilyGO T-Display-S3 (a 320x170 panel) as a temporary rig, with
  the real 1024x600 UI rendered into a PSRAM canvas and shown scaled. That rig is
  removed again; see "What the hardware bring-up found" below for what it proved. The
  rig itself is in this branch's history, between v1.1.0 and v1.2.0, if it is ever
  wanted again.
- Verified off-device:
  - the firmware compiles (PlatformIO, about 39 % of the 6.25 MB app slot);
  - the analysis has host unit tests (`test_pain.cpp`, `test_trends.cpp`), and the
    Python port in `tools/analyze.py` matches the C++ exactly;
  - the screens were rendered from the real LVGL code on a PC (`firmware/test/host`);
  - the web page was tested in headless Chrome against a mock API (`firmware/test/web`);
  - the case was checked for collisions against Waveshare's STEP model.
- Next step on the user's side: buy parts, print, flash, and work through the
  first-power-on checklist in README §6.

### What the hardware bring-up found (2026-11-07)

Running on a real ESP32-S3 found three things that no amount of off-device checking had:

1. **The forecast never parsed.** Open-Meteo sends the forecast chunked; see the gotcha
   below. The screen would have been permanently empty on the 7B too.
2. **The pain trend was not smooth.** Three discontinuities in the estimator put jumps
   of over 2 points (on a 1-5 scale) between adjacent 30-minute samples. Fixed; see the
   algorithm notes.
3. **Press-and-hold and colour order** were rig-specific and went with the rig.

The lesson worth keeping: the off-device checks all passed while the product did not
work. Compiling, unit tests, host-rendered screens and a mocked web page between them
missed a dead network path and a visibly broken graph.

## Requirements the user gave (keep these; they were explicit)

**Hardware**
- ESP32-based, cheap, available on Amazon, a 7"+ display for older eyes.
- Chosen board: **Waveshare ESP32-S3-Touch-LCD-7B** (1024x600 IPS, GT911 touch, dimmable
  backlight). The user picked the 7B for its resolution. The plain "-7" (800x480) is NOT
  compatible.
- Sensor: BME280 (BMP280 works too). microSD for logs. USB-C power.

**Usability is the #1 priority**
- One screen only. No menus, modes or pop-ups the user can get lost in or stuck on.
- **A plain tap is the only gesture.** No long-press, double-tap or confirm. The user
  said these "are all going to cause frustration and anger".
- Accidental taps are fixed in the **statistics**, never by making the UI stricter.
  There is no undo button (one more thing to mis-tap).
- About 85 % of use is in daytime: prioritise day readability. Night = **dim only**; the
  colours never change.
- Clean, clear, attractive, large type. Light theme, Atkinson Hyperlegible font.
- No weather alerts (they "will just cause unneeded concern and worry").
- Imperial units. Sea-level-adjusted pressure.

**Layout (user-specified, approved in the mockup)**
- Left ~600 px: current weather + 3-day forecast.
- Middle ~300 px: pressure (value, trend, 72 h graph) with the pain **trend line** below
  it, stacked on a shared time axis, evenly spaced vertically. The user asked for 3 days
  on both, with day gridlines (2026-11-07).
- Right ~122 px: five always-visible pain buttons, 5 (top, "Worst") to 1 (bottom,
  "Least"), with numbers. Coloured **blue (1) to red (5)**: the user changed this from
  green-to-red on 2026-11-07. Red/green is the pair most often confused, and the ramp
  also steps in lightness so it reads without colour at all.
- The pain history is shown as a **line** of calculated values, never individual points.

**Pain analysis (user's statistical requirements)**
- He may tap many times a day, inconsistently, or rarely. **Adapt to the usage level.**
  Don't penalise single taps when use is sparse.
- A day with 57 taps must not drown out a day with 1: each day counts once.
- Don't simply "keep the last tap". Look at the data over time: in "2, 2, 5" the 5 is
  likely a mis-tap; in "5, 5, 5" all three are real. Judge anomalies against the trend
  over hours and days, not seconds or minutes.
- Raw taps are never modified; only the weights change.

**Maintenance**
- Web management UI + OTA, reachable over VPN. First-time setup on the device with an
  on-screen keyboard (Wi-Fi, location, maintenance password).
- Settings open with a **10-second hold on the date/time** (the user liked this; it's a
  maintainer-only gesture).
- Auto-dimming by sunrise and sunset.
- One unit now, but build for **more units in other locations** later (per-device name,
  hostname and location).
- Long-term trending in the web UI: longer periods, more detail (done in v1.1.0).

**Case**
- 3D printed, **tabletop**, bezel flush with the glass (no lip to catch a finger near the
  right-edge buttons), heavy and stable so taps don't tip or slide it, the BME280 away
  from the electronics' heat.

**User preferences for working with them**
- Short answers. Lead with a TL;DR. Use tables for comparisons. No filler or praise.
- Challenge their assumptions. Say so when you're uncertain. Ask when an ambiguity
  matters.

## Architecture

```
firmware/src/
  main.cpp        setup/loop; watchdog 30 s; marks the OTA image valid after 60 s (rollback)
  config.h        pins, panel timing, IO-expander bits, colours (match the mockup)
  board.*         I2C (Wire on 8/9), IO expander, RGB panel (esp_lcd), GT911 touch +
                  invisible touch filter, LVGL glue, backlight
  ui.*            routing, sunrise/sunset dimming, OTA overlay
  ui_boot.cpp     boot screen: the Old Man Weather mark, then the wordmark bar
  ui_main.cpp     main screen (the layout above); pain buttons fire on PRESSED
  ui_setup.cpp    first-time wizard, maintenance menu, custom on-screen keyboard
  ui_common.h     LVGL helpers
  net.*           core-0 task: Wi-Fi (+ AP fallback after 30 min offline), NTP, weather
                  schedule, sensor sampling, analysis refresh, mDNS, ArduinoOTA, web
  web.*           WebServer on :80, Basic auth user "admin"; API + firmware upload
  web_page.h      the single-page web UI (self-contained SVG charts, no CDN)
  weather.*       Open-Meteo forecast + geocoding (HTTPS, setInsecure on purpose)
  sensor.*        minimal BME280/BMP280 driver (0x76/0x77) + sea-level reduction
  storage.*       append-only CSVs on SD (SDMMC 1-bit), LittleFS fallback
  history.*       thread-safe data store; snapshot for the UI; JSON for the web API
  pain.*          pain analysis (plain C++, host-tested)
  trends.*        long-term aggregates (plain C++, host-tested)
  settings.*      NVS-backed settings
  fonts/ icons/   generated (tools/gen_fonts.sh, tools/gen_icons.py, tools/gen_logo.py)
firmware/test/    test_pain.cpp, test_trends.cpp, host/ (screen renderer), web/ (mock API + Playwright)
case/case.py      CadQuery: front_tray + stand; --check runs a clearance check vs the STEP
tools/analyze.py  Python port of the analysis (verified identical output)
```

Threads: the **LVGL/UI runs in the Arduino loop on core 1**; everything network,
sensor and analysis runs in the **net task on core 0**. Shared data goes through
`history` (recursive mutex) and `std::atomic` flags. Any LVGL call from outside the UI
loop must hold `board::lvgl_lock()`.

### Pain algorithm (pain.cpp, mirrored in tools/analyze.py)
1. Density weight = 1 / (taps within ±1.5 h): a burst counts as one episode.
2. Locally weighted regression trend (tricube, ±12 h). The slope term is damped by
   `trust = max(0, 1 - 2|xm|/h)`, where `xm` is the weighted mean offset of the nearby
   taps, so the fit slides continuously from local-linear in the middle of the data to
   the local mean at the edges and at "now" (which is what avoids wild extrapolation).
   The kernel widens smoothly to `max(bandwidth, gap)` rather than stepping to `gap_h`.
   **Both of these exist to keep steps out of the drawn line**: the earlier on/off test
   for "data on both sides" made the estimator change form as the sample time swept
   past a tap, which put jumps of over 2 points between adjacent 30-minute samples.
3. Robust reweighting: bisquare on the residual with cutoff = max(2.5, 4 × MAD).
   Outlier power scales with nearby evidence (other taps within ±12 h / 2), so sparse
   use isn't penalised. 4 iterations.
4. Line breaks where no tap is within 18 h. Daily value = mean of the trend over the day.
- Known trade-off (accepted by the user): a genuine one-off spike looks the same as a
  mis-tap.
- **Change pain.cpp and analyze.py together**, then re-run the parity check (see the
  commands below).

### Data files (on SD or LittleFS)
- `/pain.csv`: `unix_utc,score`
- `/pressure.csv`: `unix_utc,station_hpa,sealevel_hpa,temp_c,humidity_pct`. Rows with an
  empty or nan station value are hourly API pressure, logged only when no sensor is
  present.

### Web API (all Basic auth)
`/api/status`, `/api/settings` (GET/POST), `/api/geocode?q=`, `/api/restart` (POST),
`/api/range`, and `/api/series|daily|profile|correlation?from=&to=` (unix seconds;
`hours=` also accepted), `/export/pain.csv|pressure.csv|daily.csv?from=&to=`,
`/update` (POST multipart firmware).

## Hardware facts (from Waveshare's 7B repo: github.com/waveshareteam/ESP32-S3-Touch-LCD-7B)
- RGB pins, 30 MHz pclk, porches: see `config.h`. I2C SDA 8 / SCL 9. GT911 at 0x5D
  (reset with INT low). Touch INT on GPIO 4.
- **IO expander is a CH32V003 at 0x24** (not the CH422G of the older 7"):
  - registers: mode 0x02, output 0x03, input 0x04, PWM 0x05, ADC 0x06;
  - output bits: 1 = touch reset, 2 = backlight enable, 3 = LCD reset, 4 = SD CS,
    5 = USB/CAN select (we set 0 = USB);
  - **backlight PWM is inverted** (0 = full brightness), capped at 97 so it never goes
    fully dark.
- SD card: SDMMC 1-bit, CLK 12 / CMD 11 / D0 13; drive the expander's SD CS high first.
- Mechanical (from the STEP model; tray-local coords, X right, Y up, Z toward the
  viewer, glass front at Z = 0):
  - glass 192.96 × 110.76 mm, R7.88, 1.0 mm thick;
  - M3 frame tabs at X ∈ {−64.59, 61.61}, Y ∈ {−33.81, 31.85}, Z = −8.5;
  - the deepest part is at Z = −18.4;
  - USB-C ports on the board's right edge, opening toward +X (hence the 90° cable);
  - HY2.0 headers on the left; SD slot at the top edge of the board.
- "Up" was inferred from the product photo: the thinner black border (11.00 mm) is the
  top. **Unverified on hardware.** If the image or touch is upside down, fix it in
  `config.h`/`board.cpp`, and in `case.py` mirror `TABS` X.

## Build / test commands

```bash
# Firmware (pioarduino platform = Arduino-ESP32 3.3.x / IDF 5.5)
cd firmware && pio run                  # build
pio run -t upload && pio device monitor # flash over USB
pio run -e station_ota -t upload        # OTA (set upload_port + --auth in platformio.ini)

# Host tests
cd firmware/test
g++ -std=c++17 -O2 -I../src test_pain.cpp ../src/pain.cpp -o /tmp/tp && /tmp/tp
g++ -std=c++17 -O2 -I../src test_trends.cpp ../src/trends.cpp ../src/pain.cpp -o /tmp/tt && /tmp/tt

# Render the device screens to PNG (needs one `pio run` first, for LVGL sources)
firmware/test/host/build.sh             # writes firmware/test/host/out/*.png

# Web UI against synthetic data
python3 firmware/test/web/mock_server.py 8765   # open http://localhost:8765
python3 firmware/test/web/shoot.py http://127.0.0.1:8765/ /tmp/shots   # Playwright screenshots

# Case
cd case && pip install cadquery && python3 case.py [--check path/to/ESP32-S3-Touch-LCD-7B.stp]
python3 render_preview.py               # preview PNGs (trimesh + matplotlib)

# Analysis on exported CSVs
python3 tools/analyze.py pain.csv pressure.csv --utc-offset -7 --plot chart.png
```

## Gotchas already hit (don't repeat them)
- **Arduino defines `RISING`/`FALLING` as macros.** Don't use them as identifiers
  (colours are `FALL_C`/`RISE_C`).
- LVGL 8 has no `margin` styles. Use pad_row/pad_column or a spacer box.
- LVGL 8 `lv_coord_t` values above 8191 are special. The chart encodes pressure as
  (inHg − 28) × 1000.
- `lv_chart_get_point_pos_by_id` includes the chart padding, and so do child positions:
  subtract the padding once.
- Fonts are `const`. To add a symbol fallback, copy the font struct (see the keyboard
  font in ui_setup.cpp).
- `lv_spinner` needs `LV_USE_ARC 1`.
- Don't set `lib_ldf_mode = deep+`: the Network library then isn't found on
  Arduino 3.x.
- Duplicate HTML ids in web_page.h break tab switching (Overview's `#tr` vs the Trends
  section). The section id is `trends`.
- `git clone` of a bundle with no HEAD gives an empty checkout. Use `-b main`.
- In the host-renderer Arduino shim, min/max must return by value (not a reference).
- **Open-Meteo's forecast endpoint answers with `Transfer-Encoding: chunked`**, and
  HTTPClient's `getStream()` hands back the raw socket, so chunk-size lines reach
  ArduinoJson and it fails with `InvalidInput`. `http.useHTTP10(true)` drops chunked
  encoding and keeps the body streamable. The geocoding endpoint sends a Content-Length,
  so it always worked -- which is why location search succeeded while the dashboard
  stayed empty. Found on hardware 2026-11-07; it would have failed on the 7B too.
- The PlatformIO registry was blocked in the original sandbox, so libraries come from
  GitHub URLs in platformio.ini (LVGL v8.3.11, ArduinoJson v7.4.2). That works anywhere.

## Open items / ideas not done
- [ ] First hardware bring-up (README §6 checklist): orientation, touch mapping,
      backlight PWM direction, SD mount, BME280 address, the GT911 address sequence.
- [ ] Confirm which HY2.0 header is I2C on the real board; label it in the README.
- [ ] Maybe: the trend reacts slowly to a quick burst of high scores, because a burst
      counts as one vote by design. Revisit with real data (parameters are adjustable
      in the web Settings).
- [ ] Maybe: a snap-on rear cover for the stand; a heat-set-insert option in case.py.
- [ ] Maybe: a favicon (the browser shows a harmless 404).
- [ ] The web server is synchronous. Long `/api/correlation` ranges (a year) may block
      the net task for a second or two; fine, but watch the WDT if ranges grow.

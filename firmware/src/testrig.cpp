// Test-rig serial console. See testrig.h. Built only for env:tdisplay.
#ifdef BOARD_TDISPLAY_S3
#include "testrig.h"

#include <Arduino.h>

#include "history.h"
#include "storage.h"

namespace testrig {
namespace {

constexpr int64_t DEMO_SPAN_S = 3 * 86400;  // the window the screen shows

// Time dilation state.
int64_t dil_t0_real = 0, dil_t0_virt = 0;
float dil_factor = 0;  // 0 = off

char line[64];
uint8_t len = 0;

void help() {
  Serial.println();
  Serial.println("Test rig console (this build only):");
  Serial.println("  seed [days]     fill the graphs with plausible pain taps, shaped by the");
  Serial.println("                  real pressure history (pain follows pressure falls)");
  Serial.println("  dilate <min>    spread the next <min> minutes of your own tapping across");
  Serial.println("                  3 days, so the graph fills while you tap");
  Serial.println("  dilate off      back to real time");
  Serial.println("  clear           delete all pain taps");
  Serial.println("  status          what is on and how much data there is");
  Serial.println("  help            this list");
}

void status() {
  Serial.printf("taps %u, pressure samples %u\n", (unsigned)history::tap_count(),
                (unsigned)history::pressure_count());
  if (dil_factor > 0) {
    const int64_t virt = tap_time((int64_t)time(nullptr));
    Serial.printf("dilation ON x%.0f; a tap now lands %.1f h ago\n", dil_factor,
                  (double)((int64_t)time(nullptr) - virt) / 3600.0);
  } else {
    Serial.println("dilation off");
  }
}

// Pressure in inHg at time t, from the snapshot the screen is drawing, or NAN.
float pressure_at(const history::Snapshot& s, int64_t t) {
  const int64_t start = s.series_end - (int64_t)(history::SERIES_POINTS - 1) * history::SERIES_STEP_S;
  const int64_t i = (t - start) / history::SERIES_STEP_S;
  if (i < 0 || i >= history::SERIES_POINTS) return NAN;
  return s.pressure_series[i];
}

// Taps shaped by the real pressure history, so the two graphs visibly relate: pain
// rises as pressure falls, with the irregular use the analysis is built for.
void seed(int days) {
  const int64_t now = (int64_t)time(nullptr);
  if (now < 1700000000) {
    Serial.println("no clock yet; wait for NTP and try again");
    return;
  }
  if (days < 1) days = 1;
  if (days > 14) days = 14;

  history::Snapshot snap;
  history::snapshot_if_newer(0xFFFFFFFF, &snap);
  bool have_pressure = false;
  for (int i = 0; i < history::SERIES_POINTS; ++i)
    if (!isnan(snap.pressure_series[i])) { have_pressure = true; break; }

  int made = 0;
  for (int d = days - 1; d >= 0; --d) {
    const int64_t day_start = now - (int64_t)d * 86400;
    // Irregular use: some days a single tap, some days a burst, some days nothing.
    const int roll = (int)random(0, 10);
    if (roll == 0) continue;                       // a quiet day
    const int episodes = roll < 3 ? 1 : (int)random(2, 5);
    for (int e = 0; e < episodes; ++e) {
      // Waking hours, roughly.
      const int64_t t = day_start - (int64_t)random(0, 86400) + 0;
      if (t > now) continue;
      float level = 2.5f;
      if (have_pressure) {
        const float p = pressure_at(snap, t), p24 = pressure_at(snap, t - 24 * 3600);
        if (!isnan(p) && !isnan(p24)) level = 2.2f + (p24 - p) * 9.0f;  // falls hurt
      }
      level += (float)random(-60, 61) / 100.0f;
      int score = (int)lroundf(level);
      if (score < 1) score = 1;
      if (score > 5) score = 5;
      // A burst of repeated taps about one episode: the analysis counts it once.
      const int burst = (int)random(1, 4);
      for (int b = 0; b < burst; ++b) {
        history::add_tap_at(t + b * (int64_t)random(20, 300), score);
        ++made;
      }
    }
  }
  // One obvious mis-tap, so the outlier handling is visible on the graph.
  history::add_tap_at(now - (int64_t)random(3600, 2 * 86400), 5);
  ++made;

  history::recompute();
  Serial.printf("seeded %d taps over %d days%s\n", made, days,
                have_pressure ? ", shaped by the real pressure history"
                              : " (no pressure history yet, so levels are generic)");
}

void clear() {
  history::clear_taps();
  storage::fs().remove("/pain.csv");
  history::recompute();
  Serial.println("pain taps deleted");
}

void dilate(const char* arg) {
  if (!strcmp(arg, "off") || !strcmp(arg, "0")) {
    dil_factor = 0;
    Serial.println("dilation off");
    return;
  }
  const float minutes = atof(arg);
  if (minutes <= 0) {
    Serial.println("usage: dilate <minutes> | dilate off");
    return;
  }
  const int64_t now = (int64_t)time(nullptr);
  if (now < 1700000000) {
    Serial.println("no clock yet; wait for NTP and try again");
    return;
  }
  dil_factor = (float)DEMO_SPAN_S / (minutes * 60.0f);
  dil_t0_real = now;
  dil_t0_virt = now - DEMO_SPAN_S;
  Serial.printf("dilation ON x%.0f: tap for %.0f minutes to fill 3 days\n", dil_factor, minutes);
  Serial.println("the first taps land 3 days ago and catch up to now");
}

void run(char* s) {
  while (*s == ' ') ++s;
  char* arg = strchr(s, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') ++arg; } else arg = (char*)"";
  if (!strcmp(s, "seed")) seed(*arg ? atoi(arg) : 3);
  else if (!strcmp(s, "dilate")) dilate(arg);
  else if (!strcmp(s, "clear")) clear();
  else if (!strcmp(s, "status")) status();
  else if (!strcmp(s, "help") || !*s) help();
  else Serial.printf("unknown command \"%s\"; try help\n", s);
}

}  // namespace

void poll() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = 0;
      run(line);
      len = 0;
    } else if (len < sizeof line - 1) {
      line[len++] = c;
    } else {
      len = 0;  // overlong line: drop it
    }
  }
}

int64_t tap_time(int64_t real) {
  if (dil_factor <= 0) return real;
  int64_t v = dil_t0_virt + (int64_t)((double)(real - dil_t0_real) * dil_factor);
  if (v > real) v = real;  // never record a tap in the future
  return v;
}

}  // namespace testrig
#endif  // BOARD_TDISPLAY_S3

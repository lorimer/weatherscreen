// Test-rig serial console. See testrig.h. Built only for env:tdisplay.
//
// Deliberately written against the public storage:: and history:: APIs that production
// already exposes, so that no production file carries a hook, a branch or an #ifdef for
// its sake. Seeding writes rows to /pain.csv and restarts: history::begin() loads the
// file on boot, which is why no "insert a tap at time T" entry point is needed.
#ifdef BOARD_TDISPLAY_S3
#include "testrig.h"

#include <Arduino.h>

#include "history.h"
#include "storage.h"

namespace testrig {
namespace {

char line[64];
uint8_t len = 0;

void help() {
  Serial.println();
  Serial.println("Test rig console (this build only):");
  Serial.println("  seed [days]  write plausible pain taps over the last [days] days,");
  Serial.println("               shaped by the real pressure history, then restart");
  Serial.println("  clear        delete all pain taps, then restart");
  Serial.println("  status       how much data there is");
  Serial.println("  help         this list");
  Serial.println("Seeded taps are real rows in /pain.csv. Run clear before the unit is");
  Serial.println("used for real, or they become part of somebody's history.");
}

void status() {
  Serial.printf("taps %u, pressure samples %u, storage %s\n", (unsigned)history::tap_count(),
                (unsigned)history::pressure_count(), storage::medium());
}

// Pressure in inHg at time t, from the snapshot the screen is drawing, or NAN.
float pressure_at(const history::Snapshot& s, int64_t t) {
  const int64_t start =
      s.series_end - (int64_t)(history::SERIES_POINTS - 1) * history::SERIES_STEP_S;
  const int64_t i = (t - start) / history::SERIES_STEP_S;
  if (i < 0 || i >= history::SERIES_POINTS) return NAN;
  return s.pressure_series[i];
}

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
    // Irregular use: some days one tap, some a burst, some nothing at all.
    const int roll = (int)random(0, 10);
    if (roll == 0) continue;
    const int episodes = roll < 3 ? 1 : (int)random(2, 5);
    for (int e = 0; e < episodes; ++e) {
      const int64_t t = day_start - (int64_t)random(0, 86400);
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
      // A burst about one episode: the analysis should count it once.
      const int burst = (int)random(1, 4);
      for (int b = 0; b < burst; ++b) {
        storage::append_pain(t + b * (int64_t)random(20, 300), score);
        ++made;
      }
    }
  }
  storage::append_pain(now - (int64_t)random(3600, 2 * 86400), 5);  // one obvious mis-tap
  ++made;

  Serial.printf("wrote %d taps over %d days%s; restarting to load them\n", made, days,
                have_pressure ? ", shaped by the real pressure history" : "");
  Serial.flush();
  delay(200);
  ESP.restart();
}

void clear() {
  storage::fs().remove("/pain.csv");
  Serial.println("pain taps deleted; restarting");
  Serial.flush();
  delay(200);
  ESP.restart();
}

void run(char* s) {
  while (*s == ' ') ++s;
  char* arg = strchr(s, ' ');
  if (arg) { *arg++ = 0; while (*arg == ' ') ++arg; } else arg = (char*)"";
  if (!strcmp(s, "seed")) seed(*arg ? atoi(arg) : 3);
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

}  // namespace testrig
#endif  // BOARD_TDISPLAY_S3

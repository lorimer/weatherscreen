// Central data store shared by the UI (core 1) and the network task (core 0).
// Holds raw taps and pressure samples, the latest weather, and the derived series
// the screen draws. Thread-safe.
#pragma once
#include <Arduino.h>

#include "pain.h"
#include "weather.h"

namespace history {

constexpr int SERIES_POINTS = 97;          // 48 h at 30-minute steps, ending "now"
constexpr int64_t SERIES_STEP_S = 1800;

enum class Trend { Unknown, FallingFast, Falling, Steady, Rising, RisingFast };

struct Snapshot {
  uint32_t version = 0;
  weather::Data wx;                       // hourly_pressure left empty
  bool wx_valid = false;
  float pressure_inhg = NAN;              // current, sea-level
  Trend trend = Trend::Unknown;
  float pressure_series[SERIES_POINTS];   // inHg, NAN = no data
  float pain_series[SERIES_POINTS];       // 1..5, NAN = no data (line breaks)
  int64_t series_end = 0;
  int32_t utc_offset_s = 0;
  Snapshot() {
    for (int i = 0; i < SERIES_POINTS; ++i) pressure_series[i] = pain_series[i] = NAN;
  }
};

void begin();  // loads saved logs

// Called from the UI when a pain button is pressed.
void add_tap(int score);

// Called by the network task.
void add_sensor_reading(int64_t t, float station_hpa, float temp_c, float humidity, bool log_it);
void set_weather(const weather::Data& d);
void log_api_pressure_if_no_sensor(int64_t t);
void recompute();               // re-run analysis and rebuild series
bool needs_recompute();         // a new tap is waiting (debounced by caller)

// UI: copies the latest snapshot if its version differs from `have`.
bool snapshot_if_newer(uint32_t have, Snapshot* out);

// Web API helpers (JSON strings).
String status_json();
String series_json(int hours);
String analysis_json(int days);
void write_daily_csv(Print& out);

// Diagnostics
size_t tap_count();
size_t pressure_count();
int64_t last_sensor_time();
float last_sensor_temp_c();
float last_sensor_humidity();

}  // namespace history

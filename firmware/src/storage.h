// Append-only CSV logs on the microSD card, falling back to internal flash (LittleFS)
// when no card is present. Raw data is never edited or deleted by the firmware.
//
//   /pain.csv      unix_utc,score
//   /pressure.csv  unix_utc,station_hpa,sealevel_hpa,temp_c,humidity_pct
#pragma once
#include <Arduino.h>
#include <FS.h>

#include <functional>

namespace storage {

bool begin();                 // mount SD, else LittleFS
bool on_sd();
fs::FS& fs();
const char* medium();         // "microSD" or "internal flash"
uint64_t total_bytes();
uint64_t used_bytes();

bool append_pain(int64_t t, int score);
bool append_pressure(int64_t t, float station_hpa, float sea_hpa, float temp_c, float hum);

// Calls fn for each parsed line (oldest first).
void read_pain(const std::function<void(int64_t, int)>& fn);
void read_pressure(const std::function<void(int64_t, float sea_hpa)>& fn);

}  // namespace storage

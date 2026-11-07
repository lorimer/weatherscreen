// Minimal BME280 / BMP280 driver (I2C 0x76 or 0x77) in forced mode, plus sea-level
// correction. Uses the shared I2C bus on the board's I2C header (SDA 8, SCL 9).
#pragma once
#include <Arduino.h>

namespace sensor {

struct Reading {
  bool ok = false;
  float station_hpa = NAN;
  float temp_c = NAN;    // board-internal temperature (not shown; warmed by the electronics)
  float humidity = NAN;  // NAN on a BMP280
};

bool begin();             // detects the chip; safe to call again to re-detect
bool present();
const char* chip_name();  // "BME280", "BMP280" or "none"
Reading read();

// Sea-level pressure from station pressure, elevation (m) and outdoor temperature (C).
float to_sea_level(float station_hpa, float elevation_m, float outdoor_c);

}  // namespace sensor

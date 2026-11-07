// Open-Meteo client: forecast (free, no API key) and place search (geocoding).
#pragma once
#include <Arduino.h>

#include <vector>

namespace weather {

struct Day {
  int64_t date = 0;  // local midnight (unix)
  int code = -1;     // WMO weather code
  float hi = NAN, lo = NAN;
  int precip_pct = -1;
  int64_t sunrise = 0, sunset = 0;
};

struct Data {
  bool valid = false;
  int64_t fetched = 0;      // unix time of last success
  float temp_f = NAN, feels_f = NAN, wind_mph = NAN, pressure_hpa = NAN;
  int code = -1;
  bool is_day = true;
  int32_t utc_offset_s = 0;
  float elevation_m = NAN;
  Day today;
  Day next[3];
  Day tomorrow_sun;         // sunrise/sunset tomorrow (for dimming after midnight)
  std::vector<std::pair<int64_t, float>> hourly_pressure;  // past 2 days + forecast, hPa
};

// Blocking HTTPS fetch. Returns true and fills out on success.
bool fetch(float lat, float lon, Data* out, String* err = nullptr);

struct Place {
  String name;    // "Grand Junction"
  String region;  // "Colorado, United States"
  float lat, lon, elevation;
};
bool geocode(const String& query, std::vector<Place>* out, String* err = nullptr);

// Human words for a WMO code.
const char* describe(int code, bool is_day);
const char* describe_forecast(int code, int precip_pct);

}  // namespace weather

#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace weather {
namespace {

// Weather data is public and not sensitive, so certificate pinning is not used; this
// avoids devices breaking when Open-Meteo rotates certificates.
bool get_json(const String& url, JsonDocument& doc, const JsonDocument* filter, String* err) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("WeatherPainStation/" FW_VERSION);
  // The forecast endpoint answers with Transfer-Encoding: chunked, and getStream() hands
  // back the raw socket, so the chunk-size lines reach the parser and it fails with
  // InvalidInput. Asking for HTTP/1.0 drops chunked encoding and keeps the body
  // streamable, so we never have to hold it all in RAM. (The geocoding endpoint sends a
  // Content-Length and worked either way, which is why only the forecast was broken.)
  http.useHTTP10(true);
  if (!http.begin(client, url)) {
    if (err) *err = "connect failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    if (err) *err = "HTTP " + String(code);
    http.end();
    return false;
  }
  DeserializationError e = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                  : deserializeJson(doc, http.getStream());
  http.end();
  if (e) {
    if (err) *err = String("JSON: ") + e.c_str();
    return false;
  }
  return true;
}

String enc(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o += c;
    else if (c == ' ') o += "%20";
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

Day day_at(JsonObject daily, int i) {
  Day d;
  d.date = daily["time"][i] | 0LL;
  d.code = daily["weather_code"][i] | -1;
  d.hi = daily["temperature_2m_max"][i] | NAN;
  d.lo = daily["temperature_2m_min"][i] | NAN;
  d.precip_pct = daily["precipitation_probability_max"][i] | -1;
  d.sunrise = daily["sunrise"][i] | 0LL;
  d.sunset = daily["sunset"][i] | 0LL;
  return d;
}

}  // namespace

bool fetch(float lat, float lon, Data* out, String* err) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) +
               "&longitude=" + String(lon, 4) +
               "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,pressure_msl,is_day"
               "&hourly=pressure_msl"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max"
               "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&timeformat=unixtime"
               "&past_days=2&forecast_days=5";
  JsonDocument doc;
  if (!get_json(url, doc, nullptr, err)) return false;
  if (doc["error"] | false) {
    if (err) *err = doc["reason"] | "API error";
    return false;
  }
  Data d;
  d.utc_offset_s = doc["utc_offset_seconds"] | 0;
  d.elevation_m = doc["elevation"] | NAN;
  JsonObject cur = doc["current"];
  d.temp_f = cur["temperature_2m"] | NAN;
  d.feels_f = cur["apparent_temperature"] | NAN;
  d.wind_mph = cur["wind_speed_10m"] | NAN;
  d.pressure_hpa = cur["pressure_msl"] | NAN;
  d.code = cur["weather_code"] | -1;
  d.is_day = (cur["is_day"] | 1) == 1;
  int64_t now = cur["time"] | (int64_t)time(nullptr);

  JsonObject daily = doc["daily"];
  JsonArray days = daily["time"];
  int today = -1;
  for (size_t i = 0; i < days.size(); ++i) {
    int64_t t = days[i] | 0LL;
    if (t <= now && now < t + 86400) today = i;
  }
  if (today < 0 || today + 3 >= (int)days.size()) {
    if (err) *err = "unexpected daily data";
    return false;
  }
  d.today = day_at(daily, today);
  for (int k = 0; k < 3; ++k) d.next[k] = day_at(daily, today + 1 + k);
  d.tomorrow_sun = d.next[0];

  JsonArray ht = doc["hourly"]["time"], hp = doc["hourly"]["pressure_msl"];
  d.hourly_pressure.reserve(ht.size());
  for (size_t i = 0; i < ht.size() && i < hp.size(); ++i) {
    if (hp[i].isNull()) continue;
    d.hourly_pressure.emplace_back(ht[i].as<int64_t>(), hp[i].as<float>());
  }
  d.fetched = time(nullptr);
  d.valid = !isnan(d.temp_f);
  *out = std::move(d);
  return out->valid;
}

bool geocode(const String& q, std::vector<Place>* out, String* err) {
  out->clear();
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=" + enc(q);
  JsonDocument doc;
  if (!get_json(url, doc, nullptr, err)) return false;
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    Place p;
    p.name = r["name"] | "";
    String a1 = r["admin1"] | "", c = r["country"] | "";
    p.region = a1.length() ? a1 + ", " + c : c;
    p.lat = r["latitude"] | NAN;
    p.lon = r["longitude"] | NAN;
    p.elevation = r["elevation"] | NAN;
    if (p.name.length() && !isnan(p.lat)) out->push_back(p);
  }
  if (out->empty() && err) *err = "No places found";
  return !out->empty();
}

const char* describe(int c, bool day) {
  switch (c) {
    case 0: return day ? "Sunny" : "Clear";
    case 1: return day ? "Mostly sunny" : "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 77: return "Snow";
    case 75: return "Heavy snow";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorms";
    case 96: case 99: return "Storms, hail";
    default: return "";
  }
}

const char* describe_forecast(int c, int pp) {
  bool wet = (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95;
  bool snow = (c >= 71 && c <= 77) || c == 85 || c == 86;
  if (snow) return pp >= 60 ? "Snow likely" : "Chance of snow";
  if (wet) return pp >= 60 ? "Rain likely" : pp >= 0 && pp < 30 ? describe(c, true) : "Chance of rain";
  return describe(c, true);
}

}  // namespace weather

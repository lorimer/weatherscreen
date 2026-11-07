#include "sensor.h"

#include <Wire.h>

namespace sensor {
namespace {

uint8_t addr = 0;
uint8_t chip = 0;  // 0x60 BME280, 0x58 BMP280

struct {
  uint16_t T1; int16_t T2, T3;
  uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
  uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
} cal;

bool rd(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
  return true;
}

bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool probe(uint8_t a) {
  addr = a;
  uint8_t id = 0;
  if (!rd(0xD0, &id, 1)) return false;
  if (id != 0x60 && id != 0x58) return false;
  chip = id;
  return true;
}

bool read_calibration() {
  uint8_t b[26];
  if (!rd(0x88, b, 26)) return false;
  auto u16 = [&](int i) { return (uint16_t)(b[i] | (b[i + 1] << 8)); };
  auto s16 = [&](int i) { return (int16_t)u16(i); };
  cal.T1 = u16(0); cal.T2 = s16(2); cal.T3 = s16(4);
  cal.P1 = u16(6); cal.P2 = s16(8); cal.P3 = s16(10); cal.P4 = s16(12); cal.P5 = s16(14);
  cal.P6 = s16(16); cal.P7 = s16(18); cal.P8 = s16(20); cal.P9 = s16(22);
  cal.H1 = b[25];
  if (chip == 0x60) {
    uint8_t h[7];
    if (!rd(0xE1, h, 7)) return false;
    cal.H2 = (int16_t)(h[0] | (h[1] << 8));
    cal.H3 = h[2];
    cal.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
    cal.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
    cal.H6 = (int8_t)h[6];
  }
  return true;
}

}  // namespace

bool begin() {
  chip = 0;
  if (!probe(0x76) && !probe(0x77)) {
    addr = 0;
    return false;
  }
  wr(0xE0, 0xB6);  // soft reset
  delay(5);
  if (!read_calibration()) {
    chip = 0;
    return false;
  }
  wr(0xF5, 0x00);  // no IIR filter, standby irrelevant in forced mode
  return true;
}

bool present() { return chip != 0; }
const char* chip_name() { return chip == 0x60 ? "BME280" : chip == 0x58 ? "BMP280" : "none"; }

Reading read() {
  Reading r;
  if (!chip && !begin()) return r;
  if (chip == 0x60) wr(0xF2, 0x01);           // humidity x1
  if (!wr(0xF4, (1 << 5) | (3 << 2) | 0x01)) {  // temp x1, pressure x4, forced
    chip = 0;                                  // re-detect next time (unplugged?)
    return r;
  }
  uint32_t t0 = millis();
  uint8_t st = 0x08;
  while ((st & 0x08) && millis() - t0 < 100) {
    delay(5);
    rd(0xF3, &st, 1);
  }
  uint8_t d[8];
  if (!rd(0xF7, d, chip == 0x60 ? 8 : 6)) return r;
  int32_t adc_P = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
  int32_t adc_T = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);

  // Bosch datasheet floating-point compensation.
  double v1 = (adc_T / 16384.0 - cal.T1 / 1024.0) * cal.T2;
  double v2 = (adc_T / 131072.0 - cal.T1 / 8192.0);
  v2 = v2 * v2 * cal.T3;
  double t_fine = v1 + v2;
  r.temp_c = (float)(t_fine / 5120.0);

  v1 = t_fine / 2.0 - 64000.0;
  v2 = v1 * v1 * cal.P6 / 32768.0;
  v2 = v2 + v1 * cal.P5 * 2.0;
  v2 = v2 / 4.0 + cal.P4 * 65536.0;
  v1 = (cal.P3 * v1 * v1 / 524288.0 + cal.P2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * cal.P1;
  if (v1 == 0) return r;
  double p = 1048576.0 - adc_P;
  p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = cal.P9 * p * p / 2147483648.0;
  v2 = p * cal.P8 / 32768.0;
  p = p + (v1 + v2 + cal.P7) / 16.0;
  r.station_hpa = (float)(p / 100.0);

  if (chip == 0x60) {
    int32_t adc_H = (d[6] << 8) | d[7];
    double h = t_fine - 76800.0;
    h = (adc_H - (cal.H4 * 64.0 + cal.H5 / 16384.0 * h)) *
        (cal.H2 / 65536.0 * (1.0 + cal.H6 / 67108864.0 * h * (1.0 + cal.H3 / 67108864.0 * h)));
    h = h * (1.0 - cal.H1 * h / 524288.0);
    r.humidity = (float)constrain(h, 0.0, 100.0);
  }
  r.ok = r.station_hpa > 300 && r.station_hpa < 1100;
  return r;
}

float to_sea_level(float p, float h, float t_c) {
  if (isnan(h)) return p;
  if (isnan(t_c)) t_c = 15.0f;
  // Hypsometric reduction (as used by most consumer stations).
  return p * powf(1.0f - (0.0065f * h) / (t_c + 0.0065f * h + 273.15f), -5.257f);
}

}  // namespace sensor

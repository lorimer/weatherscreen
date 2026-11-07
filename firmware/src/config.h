// Hardware pins, timing and visual constants.
#pragma once
#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---- Waveshare ESP32-S3-Touch-LCD-7B ------------------------------------------------
// From Waveshare's 7B examples (github.com/waveshareteam/ESP32-S3-Touch-LCD-7B).
namespace pins {
constexpr int I2C_SDA = 8;
constexpr int I2C_SCL = 9;
constexpr int TOUCH_INT = 4;

constexpr int LCD_VSYNC = 3, LCD_HSYNC = 46, LCD_DE = 5, LCD_PCLK = 7;
// RGB565 data lines, bit 0 (B3) .. bit 15 (R7)
constexpr int LCD_DATA[16] = {14, 38, 18, 17, 10,       // B3..B7
                              39, 0, 45, 48, 47, 21,    // G2..G7
                              1, 2, 42, 41, 40};        // R3..R7

constexpr int SD_CLK = 12, SD_CMD = 11, SD_D0 = 13;     // SDMMC 1-bit
}  // namespace pins

namespace lcd {
constexpr int W = 1024, H = 600;
constexpr uint32_t PCLK_HZ = 30 * 1000 * 1000;
constexpr int HSYNC_PULSE = 162, HSYNC_BACK = 152, HSYNC_FRONT = 48;
constexpr int VSYNC_PULSE = 45, VSYNC_BACK = 13, VSYNC_FRONT = 3;
}  // namespace lcd

// IO expander (a CH32V003 running Waveshare's expander firmware) at I2C 0x24
namespace ioex {
constexpr uint8_t ADDR = 0x24;
constexpr uint8_t REG_MODE = 0x02, REG_OUT = 0x03, REG_IN = 0x04, REG_PWM = 0x05, REG_ADC = 0x06;
constexpr uint8_t PIN_TOUCH_RST = 1, PIN_BACKLIGHT = 2, PIN_LCD_RST = 3, PIN_SD_CS = 4,
                  PIN_USB_CAN_SEL = 5;  // 0 = USB, 1 = CAN
}  // namespace ioex

constexpr uint8_t GT911_ADDR = 0x5D;

// ---- Behaviour -----------------------------------------------------------------------
namespace timing {
constexpr uint32_t WEATHER_REFRESH_MS = 15UL * 60 * 1000;
constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000;
constexpr uint32_t SENSOR_READ_MS = 60UL * 1000;          // read every minute (display)
constexpr uint32_t SENSOR_LOG_MS = 10UL * 60 * 1000;      // log every 10 minutes
// Without a sensor, pressure comes from the forecast. Log it on the same 30-minute grid
// the history graph is drawn on. Storage is not the constraint: a row is about 36 bytes,
// so this is ~1.7 kB a day, under 650 kB a year, on a card with gigabytes spare.
constexpr uint32_t API_PRESSURE_LOG_S = 30UL * 60;
constexpr uint32_t ANALYSIS_DEBOUNCE_MS = 3000;
constexpr uint32_t ANALYSIS_PERIOD_MS = 10UL * 60 * 1000;
constexpr uint32_t AP_FALLBACK_AFTER_MS = 30UL * 60 * 1000;
constexpr uint32_t SETTINGS_IDLE_MS = 3UL * 60 * 1000;    // settings screens return to main
constexpr uint32_t MAINT_HOLD_MS = 10UL * 1000;           // hold on the date/time to open settings
constexpr uint32_t STALE_AFTER_S = 60 * 60;               // show "updated ... ago" after 1 h
constexpr uint32_t TOUCH_MIN_MS = 40;                     // shorter contacts are glitches
}  // namespace timing

// ---- Colours (match the approved mockup) ---------------------------------------------
namespace col {
constexpr uint32_t BG = 0xF6F3EC, TEXT = 0x1C1B19, TEXT2 = 0x3D3A35, MUTED = 0x5A564E;
constexpr uint32_t RULE = 0xDDD6C8, AXIS = 0xCFC7B6, MIDLINE = 0xE4DED1;
constexpr uint32_t PRESSURE = 0x2B5F9E, PAIN = 0xB3261E, FALL_C = 0xA84B0C, RISE_C = 0x2B5F9E;
constexpr uint32_t KEY = 0xFFFFFF, KEY_SHADOW = 0xCFC7B6, KEY_ALT = 0xE6E0D3, ACCENT = 0x2B5F9E;
// 1..5, blue (least) to red (worst). Lightness falls about 2.4 L* a step as well as
// the hue turning, so the scale is still readable if the colours are not: red/green is
// the pair most often confused, and these buttons are for an elderly user. White text
// clears 4.5:1 on all five (5.0 at the blue end, 7.2 at the red).
// The route is cool to warm through a desaturated middle rather than straight through
// magenta, which reads as a severity scale instead of a rainbow.
constexpr uint32_t PAIN_BTN[5] = {0x4270B1, 0x736191, 0x7D5872, 0x9B3D57, 0xAB1D19};
}  // namespace col

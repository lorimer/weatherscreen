// Board layer for the LilyGO T-Display-S3 Touch: a TEST RIG, not a shipping target.
//
// The station's real panel is 1024x600. This board has a 320x170 ST7789, eleven times
// fewer pixels, so the approved layout cannot be shown as designed. Rather than
// redesign the UI for the small screen (which would tell us nothing about the real
// one), LVGL still renders the real, unmodified 1024x600 screen into a canvas in
// PSRAM, and this file presents a view of that canvas on the panel:
//
//   FIT      (default) the whole screen scaled down about 3.5x. Text is unreadable by
//            design; this view is for judging layout, balance and colour.
//   ONE2ONE  a 320x170 window into the canvas at true pixel size. Drag to pan.
//
// Switching views: triple tap anywhere, or press KEY (GPIO14). BOOT (GPIO0) steps the
// 1:1 window through the six tiles that cover the screen.
//
// On the triple tap: in FIT view taps pass straight through to the UI, because the real
// board fires a pain button on PRESSED and that timing is worth keeping honest. So a
// triple tap on a pain button also logs three taps. Use KEY to switch views without
// touching the UI. On a test rig this is harmless, and the analysis counts a burst as
// one episode in any case.
//
// Deliberate differences from the real board:
//   - no IO expander: the backlight is LEDC on GPIO38 and there is no SD chip-select;
//   - CST816 touch reports one contact, so there is no palm to reject; the
//     short-contact glitch filter is kept so the touch counters still mean the same;
//   - no microSD and no BME280, so logs go to LittleFS and pressure comes from the
//     forecast API. Both are supported paths in the shipping firmware.
#ifdef BOARD_TDISPLAY_S3  // inert in the Waveshare build, whatever the src filter does
#include "board.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <lvgl.h>

#include "config.h"

namespace board {
namespace {

// ---- Pins (LilyGO schematic; the same values the flock-you port uses) ---------------
constexpr int PIN_LCD_PWR = 15;  // panel supply enable, must be driven high
constexpr int PIN_BL = 38;
constexpr int PIN_WR = 8, PIN_RD = 9, PIN_DC = 7, PIN_CS = 6, PIN_RST = 5;
constexpr int PIN_D[8] = {39, 40, 41, 42, 45, 46, 47, 48};
constexpr int PIN_TP_SDA = 18, PIN_TP_SCL = 17, PIN_TP_RST = 21;
constexpr int PIN_BTN_BOOT = 0, PIN_BTN_KEY = 14;
constexpr uint8_t CST816_ADDR = 0x15;

// The panel in its native portrait orientation: the visible 170 columns start at 35.
constexpr int PAN_W = 170, PAN_H = 320, PAN_GAP_X = 35;
constexpr uint32_t PAN_PCLK_HZ = 20 * 1000 * 1000;

// The screen as we use it: landscape. The rotation happens in the presenter below, so
// the panel keeps its native scan order and there is no MADCTL guesswork.
constexpr int SCR_W = PAN_H, SCR_H = PAN_W;  // 320 x 170

// ---- The two things to check on the first boot -------------------------------------
// If the picture comes up upside down, flip this. Touch follows it automatically: the
// touch mapping below is derived from the same constant.
constexpr bool ROT_FLIP = false;
// This panel is BGR (LovyanGFX calls it rgb_order = false). If the pressure line comes
// up red and the pain line blue, they are swapped: use ..._ORDER_RGB instead.
constexpr lcd_rgb_element_order_t PAN_RGB_ORDER = LCD_RGB_ELEMENT_ORDER_BGR;

// ---- State -------------------------------------------------------------------------
esp_lcd_panel_io_handle_t io = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t lv_mutex = nullptr;
uint8_t bl_percent = 100;

lv_color_t* canvas = nullptr;  // the real 1024x600 screen, in PSRAM
uint16_t* out = nullptr;       // one panel frame, native portrait, byte-swapped
volatile bool xfer_busy = false;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

// Bring-up results. This board talks over the ESP32-S3's native USB, so anything
// logged during setup() is gone before a host can open the port. Keep the findings and
// print them once, a few seconds in, from the LVGL thread (see rig_banner()).
uint8_t tp_chip = 0;
bool canvas_internal = false;
bool usb_was_up = false;

enum View { VIEW_FIT = 0, VIEW_1TO1 = 1 };
View view = VIEW_FIT;
int pan_x = 0, pan_y = 0;  // top-left of the 1:1 window, in canvas pixels

// The six 320x170 tiles that cover the 1024x600 screen.
constexpr int PRESET_N = 6;
constexpr int16_t preset_x[PRESET_N] = {0, 352, 704, 0, 352, 704};
constexpr int16_t preset_y[PRESET_N] = {0, 0, 0, 430, 430, 430};
int preset = 0;

// FIT view: the canvas fits SCR_W x SCR_H with its aspect ratio kept (fit by height).
constexpr int FIT_W = (int)((int64_t)lcd::W * SCR_H / lcd::H);  // 290
constexpr int FIT_X = (SCR_W - FIT_W) / 2;                      // 15
// The source span of each output column and row, precomputed to keep the inner loop
// free of division.
int16_t fx0[SCR_W], fx1[SCR_W], fy0[SCR_H], fy1[SCR_H];

// ---- Presenter ---------------------------------------------------------------------
// Landscape (lx, ly) -> an index into the native-portrait frame.
inline int pan_index(int lx, int ly) {
  return ROT_FLIP ? lx * PAN_W + (PAN_W - 1 - ly) : (PAN_H - 1 - lx) * PAN_W + ly;
}

bool on_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  xfer_busy = false;
  return false;
}

void present_fit() {
  for (int ly = 0; ly < SCR_H; ++ly) {
    const int sy0 = fy0[ly], sy1 = fy1[ly];
    for (int lx = 0; lx < SCR_W; ++lx) {
      uint16_t c16 = 0;  // letterbox columns stay black
      if (lx >= FIT_X && lx < FIT_X + FIT_W) {
        const int sx0 = fx0[lx - FIT_X], sx1 = fx1[lx - FIT_X];
        uint32_t r = 0, g = 0, b = 0, n = 0;
        for (int sy = sy0; sy < sy1; ++sy) {
          const lv_color_t* row = canvas + (size_t)sy * lcd::W;
          for (int sx = sx0; sx < sx1; ++sx) {
            const uint16_t c = row[sx].full;
            r += (c >> 11) & 0x1F;
            g += (c >> 5) & 0x3F;
            b += c & 0x1F;
            ++n;
          }
        }
        if (!n) n = 1;
        c16 = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
      }
      out[pan_index(lx, ly)] = __builtin_bswap16(c16);
    }
  }
}

void present_1to1() {
  for (int ly = 0; ly < SCR_H; ++ly) {
    const lv_color_t* row = canvas + (size_t)(pan_y + ly) * lcd::W + pan_x;
    for (int lx = 0; lx < SCR_W; ++lx) out[pan_index(lx, ly)] = __builtin_bswap16(row[lx].full);
  }
}

void present() {
  if (!canvas || !out || !panel) return;
  uint32_t waited = 0;
  while (xfer_busy && waited < 200) { delay(1); ++waited; }
  if (view == VIEW_FIT) present_fit();
  else present_1to1();
  xfer_busy = true;
  esp_lcd_panel_draw_bitmap(panel, 0, 0, PAN_W, PAN_H, out);
}

void set_pan(int x, int y) {
  const int max_x = lcd::W - SCR_W, max_y = lcd::H - SCR_H;
  x = x < 0 ? 0 : (x > max_x ? max_x : x);
  y = y < 0 ? 0 : (y > max_y ? max_y : y);
  if (x == pan_x && y == pan_y) return;
  pan_x = x;
  pan_y = y;
  present();
}

void set_view(View v) {
  if (v == view) return;
  view = v;
  log_i("view = %s", v == VIEW_FIT ? "fit (whole screen, scaled)" : "1:1 (drag to pan)");
  present();
}

void flush_cb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  const int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; ++y)
    memcpy(canvas + (size_t)y * lcd::W + a->x1, px + (size_t)(y - a->y1) * w,
           (size_t)w * sizeof(lv_color_t));
  if (lv_disp_flush_is_last(drv)) present();
  lv_disp_flush_ready(drv);
}

// ---- CST816 touch ------------------------------------------------------------------
bool cst_read(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)CST816_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
  return true;
}

bool cst_write(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// Touch state, the view gesture, and the deferred click used in 1:1 view.
constexpr int PAN_SLOP = 6;      // movement beyond this turns a 1:1 contact into a pan
constexpr int TAP_SLOP = 8;      // movement up to this still counts as a tap
constexpr int PRESS_DELAY = 120; // in 1:1, a still finger becomes a real press after this
constexpr int MULTI_GAP = 600, MULTI_DIST = 60;

struct {
  bool down = false;
  uint32_t down_ms = 0;
  int sx = 0, sy = 0;          // screen coords where this contact started
  int lx = 0, ly = 0;          // latest screen coords
  bool panning = false;
  int pan_x0 = 0, pan_y0 = 0;  // pan offset when this contact started
  bool held = false;           // 1:1: a still finger, passed through as a live press
  bool reported = false;       // currently reporting PRESSED to LVGL

  int taps = 0;                // consecutive quick taps, for the triple tap
  uint32_t last_tap_ms = 0;
  int tap_x = 0, tap_y = 0;

  int click_hold = 0;          // reads left to report PRESSED (1:1 deferred click)
  lv_point_t pt{0, 0};         // last point handed to LVGL, in canvas coords

  uint32_t accepted = 0, filtered = 0;
} t;

// Screen coords -> canvas coords for the current view.
void to_canvas(int lx, int ly, lv_point_t* p) {
  int cx, cy;
  if (view == VIEW_FIT) {
    cx = (int)((int64_t)(lx - FIT_X) * lcd::W / FIT_W);
    cy = (int)((int64_t)ly * lcd::H / SCR_H);
  } else {
    cx = pan_x + lx;
    cy = pan_y + ly;
  }
  p->x = (lv_coord_t)(cx < 0 ? 0 : (cx >= lcd::W ? lcd::W - 1 : cx));
  p->y = (lv_coord_t)(cy < 0 ? 0 : (cy >= lcd::H ? lcd::H - 1 : cy));
}

struct Btn {
  int pin;
  bool down = false;
  uint32_t changed = 0;
};
Btn b_boot{PIN_BTN_BOOT}, b_key{PIN_BTN_KEY};

bool btn_pressed(Btn& b, uint32_t now) {
  const bool d = digitalRead(b.pin) == LOW;
  if (d == b.down || now - b.changed < 40) return false;
  b.down = d;
  b.changed = now;
  return d;
}

// Printed every time a serial monitor attaches, so the rig's state is always one
// connect away rather than lost in the first second after a reset.
void rig_banner() {
  log_w("--- T-Display-S3 test rig ---");
  log_w("canvas %dx%d in PSRAM, panel %dx%d (%s), frame buffer in %s", lcd::W, lcd::H, SCR_W,
        SCR_H, ROT_FLIP ? "flipped" : "normal", canvas_internal ? "internal RAM" : "PSRAM");
  log_w("touch chip 0x%02X (expect 0xB5 CST816S / 0xB4 CST816T / 0xB6 CST820)", tp_chip);
  log_w("view: %s. Triple tap or KEY switches, BOOT steps the 1:1 window.",
        view == VIEW_FIT ? "scaled" : "1:1");
  log_w("free heap %u, free PSRAM %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (!tp_chip) log_e("touch did not answer on I2C 0x%02X: taps will not work", CST816_ADDR);
}

void touch_read_cb(lv_indev_drv_t*, lv_indev_data_t* data) {
  const uint32_t now = millis();
  const bool usb_up = (bool)Serial;  // true once a host opens the CDC port
  if (usb_up && !usb_was_up) rig_banner();
  usb_was_up = usb_up;

  // KEY switches view without the taps reaching the UI; BOOT walks the 1:1 window.
  if (btn_pressed(b_key, now)) set_view(view == VIEW_FIT ? VIEW_1TO1 : VIEW_FIT);
  if (btn_pressed(b_boot, now)) {
    preset = (preset + 1) % PRESET_N;
    view = VIEW_1TO1;
    pan_x = preset_x[preset];
    pan_y = preset_y[preset];
    present();
  }

  uint8_t b[6];
  bool pressed = false;
  int rx = 0, ry = 0;
  if (cst_read(0x01, b, sizeof b)) {
    if ((b[1] & 0x0F) >= 1) {
      pressed = true;
      rx = ((b[2] & 0x0F) << 8) | b[3];
      ry = ((b[4] & 0x0F) << 8) | b[5];
    }
  }
  // Raw portrait coords -> landscape screen coords (the inverse of pan_index).
  int lx, ly;
  if (ROT_FLIP) {
    lx = ry;
    ly = PAN_W - 1 - rx;
  } else {
    lx = PAN_H - 1 - ry;
    ly = rx;
  }
  if (lx < 0) lx = 0; else if (lx >= SCR_W) lx = SCR_W - 1;
  if (ly < 0) ly = 0; else if (ly >= SCR_H) ly = SCR_H - 1;

  if (pressed && !t.down) {  // contact starts
    t.down = true;
    t.down_ms = now;
    t.sx = lx;
    t.sy = ly;
    t.panning = false;
    t.pan_x0 = pan_x;
    t.pan_y0 = pan_y;
  }
  if (pressed) {
    t.lx = lx;
    t.ly = ly;
    if (view == VIEW_1TO1) {
      // A finger that moves soon pans; one that stays put becomes a real press, so
      // press-and-hold gestures (the 10-second hold on the date opens Maintenance)
      // work here and not only in the scaled view.
      const int dx = lx - t.sx, dy = ly - t.sy;
      if (!t.held) {
        if (!t.panning && (abs(dx) > PAN_SLOP || abs(dy) > PAN_SLOP)) t.panning = true;
        if (!t.panning && now - t.down_ms >= PRESS_DELAY) t.held = true;
      }
      if (t.panning) set_pan(t.pan_x0 - dx, t.pan_y0 - dy);
    }
  }
  if (!pressed && t.down) {  // contact ends
    t.down = false;
    const uint32_t dur = now - t.down_ms;
    if (dur < timing::TOUCH_MIN_MS) t.filtered++;  // a glitch, as on the real board
    const bool tap = !t.panning && dur >= timing::TOUCH_MIN_MS && dur < 500 &&
                     abs(t.lx - t.sx) <= TAP_SLOP && abs(t.ly - t.sy) <= TAP_SLOP;
    if (tap) {
      if (now - t.last_tap_ms < MULTI_GAP && abs(t.lx - t.tap_x) < MULTI_DIST &&
          abs(t.ly - t.tap_y) < MULTI_DIST) {
        t.taps++;
      } else {
        t.taps = 1;
        t.tap_x = t.lx;
        t.tap_y = t.ly;
      }
      t.last_tap_ms = now;
      if (t.taps >= 3) {
        t.taps = 0;
        set_view(view == VIEW_FIT ? VIEW_1TO1 : VIEW_FIT);
      } else if (view == VIEW_1TO1 && !t.held) {
        to_canvas(t.lx, t.ly, &t.pt);  // too quick to have become a hold: click it now
        t.click_hold = 2;
      }
    }
    t.held = false;
  }

  if (view == VIEW_FIT) {
    // Live pass-through, so a pain button still fires on PRESSED as it will on the
    // real board.
    const bool report = t.down && now - t.down_ms >= timing::TOUCH_MIN_MS;
    if (report) {
      to_canvas(t.lx, t.ly, &t.pt);
      if (!t.reported) t.accepted++;
    }
    t.reported = report;
  } else {
    // A still finger is passed through live, so holds and drags inside a widget work.
    // A tap too quick for that is replayed as a click once the release proves it was
    // not the start of a pan.
    if (t.held) {
      if (!t.reported) t.accepted++;
      t.reported = true;
      to_canvas(t.lx, t.ly, &t.pt);
    } else if (t.click_hold > 0) {
      if (!t.reported) t.accepted++;
      t.reported = true;
      t.click_hold--;
    } else {
      t.reported = false;
    }
  }

  data->point = t.pt;
  data->state = t.reported ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---- LCD ---------------------------------------------------------------------------
bool lcd_init() {
  pinMode(PIN_LCD_PWR, OUTPUT);
  digitalWrite(PIN_LCD_PWR, HIGH);
  pinMode(PIN_RD, OUTPUT);  // the i80 driver does not manage RD; park it high
  digitalWrite(PIN_RD, HIGH);

  esp_lcd_i80_bus_config_t bus = {};
  bus.clk_src = LCD_CLK_SRC_DEFAULT;
  bus.dc_gpio_num = PIN_DC;
  bus.wr_gpio_num = PIN_WR;
  bus.bus_width = 8;
  for (int i = 0; i < 8; ++i) bus.data_gpio_nums[i] = PIN_D[i];
  bus.max_transfer_bytes = (size_t)PAN_W * PAN_H * sizeof(uint16_t);
  bus.dma_burst_size = 64;
  esp_lcd_i80_bus_handle_t bus_h = nullptr;
  if (esp_lcd_new_i80_bus(&bus, &bus_h) != ESP_OK) return false;

  esp_lcd_panel_io_i80_config_t iocfg = {};
  iocfg.cs_gpio_num = PIN_CS;
  iocfg.pclk_hz = PAN_PCLK_HZ;
  iocfg.trans_queue_depth = 4;
  iocfg.on_color_trans_done = on_trans_done;
  iocfg.lcd_cmd_bits = 8;
  iocfg.lcd_param_bits = 8;
  iocfg.dc_levels.dc_idle_level = 0;
  iocfg.dc_levels.dc_cmd_level = 0;
  iocfg.dc_levels.dc_dummy_level = 0;
  iocfg.dc_levels.dc_data_level = 1;
  if (esp_lcd_new_panel_io_i80(bus_h, &iocfg, &io) != ESP_OK) return false;

  esp_lcd_panel_dev_config_t dev = {};
  dev.reset_gpio_num = PIN_RST;
  dev.rgb_ele_order = PAN_RGB_ORDER;
  dev.bits_per_pixel = 16;
  if (esp_lcd_new_panel_st7789(io, &dev, &panel) != ESP_OK) return false;
  if (esp_lcd_panel_reset(panel) != ESP_OK) return false;
  if (esp_lcd_panel_init(panel) != ESP_OK) return false;
  esp_lcd_panel_invert_color(panel, true);   // this panel is inverted
  esp_lcd_panel_set_gap(panel, PAN_GAP_X, 0);
  esp_lcd_panel_disp_on_off(panel, true);
  return true;
}

}  // namespace

bool begin() {
  // The only I2C on this board is the touch bus. A BME280 wired to GPIO 17/18 would be
  // found by sensor::begin() as usual; with nothing there it falls back to API pressure.
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  Wire.setTimeOut(50);

  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(20);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(80);
  cst_read(0xA7, &tp_chip, 1);  // 0xB5 CST816S, 0xB4 CST816T, 0xB6 CST820
  cst_write(0xFE, 0x01);        // disable auto-sleep, or polling goes quiet when idle

  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY, INPUT_PULLUP);

  if (!lcd_init()) {
    log_e("LCD init failed");
    return false;
  }
  ledcAttach(PIN_BL, 22000, 8);
  set_backlight(100);

  canvas = (lv_color_t*)heap_caps_calloc((size_t)lcd::W * lcd::H, sizeof(lv_color_t),
                                         MALLOC_CAP_SPIRAM);
  out = (uint16_t*)heap_caps_calloc((size_t)PAN_W * PAN_H, sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  canvas_internal = out != nullptr;
  if (!out) out = (uint16_t*)heap_caps_calloc((size_t)PAN_W * PAN_H, sizeof(uint16_t),
                                              MALLOC_CAP_SPIRAM);
  if (!canvas || !out) {
    log_e("No memory for the %dx%d canvas", lcd::W, lcd::H);
    return false;
  }
  for (int i = 0; i < FIT_W; ++i) {
    fx0[i] = (int16_t)((int64_t)i * lcd::W / FIT_W);
    fx1[i] = (int16_t)((int64_t)(i + 1) * lcd::W / FIT_W);
    if (fx1[i] <= fx0[i]) fx1[i] = fx0[i] + 1;
  }
  for (int i = 0; i < SCR_H; ++i) {
    fy0[i] = (int16_t)((int64_t)i * lcd::H / SCR_H);
    fy1[i] = (int16_t)((int64_t)(i + 1) * lcd::H / SCR_H);
    if (fy1[i] <= fy0[i]) fy1[i] = fy0[i] + 1;
  }

  lv_init();
  lv_mutex = xSemaphoreCreateRecursiveMutex();

  const size_t buf_px = (size_t)lcd::W * 60;
  auto* buf = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf) {
    log_e("No PSRAM for LVGL buffer");
    return false;
  }
  lv_disp_draw_buf_init(&draw_buf, buf, nullptr, buf_px);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = lcd::W;  // the UI still thinks it is on the real panel
  disp_drv.ver_res = lcd::H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&indev_drv);

  log_i("T-Display-S3 test rig: %dx%d canvas on a %dx%d panel", lcd::W, lcd::H, SCR_W, SCR_H);
  log_i("triple tap or KEY switches scaled/1:1, BOOT steps the 1:1 window");
  return true;
}

void set_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent < 3) percent = 3;  // never fully off: the buttons must stay visible
  bl_percent = percent;
  ledcWrite(PIN_BL, (uint32_t)percent * 255 / 100);
}

uint8_t backlight() { return bl_percent; }

void sd_cs_high() {}  // no IO expander and no SD card on this board

bool lvgl_lock(uint32_t timeout_ms) {
  return lv_mutex && xSemaphoreTakeRecursive(lv_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_unlock() { xSemaphoreGiveRecursive(lv_mutex); }

void lvgl_tick() {
  if (lvgl_lock(50)) {
    lv_timer_handler();
    lvgl_unlock();
  }
}

uint32_t touches_accepted() { return t.accepted; }
uint32_t touches_filtered() { return t.filtered; }

}  // namespace board

#endif  // BOARD_TDISPLAY_S3

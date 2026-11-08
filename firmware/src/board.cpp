#include "board.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include "config.h"

namespace board {
namespace {

esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t lv_mutex = nullptr;
uint8_t ioex_out = 0xFF;
uint8_t bl_percent = 100;
bool flipped = false;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

// ---------------------------------------------------------------- IO expander
bool ioex_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ioex::ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void ioex_pin(uint8_t pin, bool high) {
  if (high) ioex_out |= (1 << pin);
  else ioex_out &= ~(1 << pin);
  ioex_write(ioex::REG_OUT, ioex_out);
}

// ---------------------------------------------------------------- GT911 touch
bool gt_read(uint16_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)GT911_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
  return true;
}

void gt_clear() {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0x00);
  Wire.endTransmission();
}

void touch_reset() {
  // Reset with INT held low selects I2C address 0x5D (same sequence as Waveshare's driver).
  pinMode(pins::TOUCH_INT, OUTPUT);
  ioex_pin(ioex::PIN_TOUCH_RST, false);
  delay(100);
  digitalWrite(pins::TOUCH_INT, LOW);
  delay(100);
  ioex_pin(ioex::PIN_TOUCH_RST, true);
  delay(200);
  pinMode(pins::TOUCH_INT, INPUT);
}

// Touch filtering (invisible to the user):
//  - contacts shorter than TOUCH_MIN_MS are glitches and never reach the UI
//  - two or more simultaneous contacts (palm, gripping the case) suppress input
//    until every finger is lifted
struct {
  bool reported = false;   // currently reporting "pressed" to LVGL
  bool multi = false;      // multi-touch seen during this contact
  uint32_t start = 0;      // when the current single contact began
  uint32_t last_data = 0;  // last time the controller had fresh data
  int npts = 0;
  uint16_t x = 0, y = 0;
  uint32_t accepted = 0, filtered = 0;
} tch;

void touch_read_cb(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint32_t now = millis();
  uint8_t status = 0;
  if (gt_read(0x814E, &status, 1) && (status & 0x80)) {
    int n = status & 0x0F;
    if (n > 5) n = 0;
    if (n >= 1) {
      uint8_t p[8];
      if (gt_read(0x814F, p, 8)) {
        tch.x = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
        tch.y = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
      }
    }
    gt_clear();
    tch.npts = n;
    tch.last_data = now;
  } else if (now - tch.last_data > 120) {
    tch.npts = 0;  // controller stopped reporting: treat as released
  }

  if (tch.npts == 0) {
    if (tch.start && !tch.reported && !tch.multi) tch.filtered++;  // too short
    tch.reported = false;
    tch.multi = false;
    tch.start = 0;
  } else if (tch.npts >= 2 || tch.multi) {
    if (!tch.multi) tch.filtered++;
    tch.multi = true;
    tch.reported = false;
  } else {
    if (!tch.start) tch.start = now;
    if (!tch.reported && now - tch.start >= timing::TOUCH_MIN_MS) {
      tch.reported = true;
      tch.accepted++;
    }
  }

  uint16_t px = tch.x < lcd::W ? tch.x : lcd::W - 1;
  uint16_t py = tch.y < lcd::H ? tch.y : lcd::H - 1;
  if (flipped) {
    px = lcd::W - 1 - px;
    py = lcd::H - 1 - py;
  }
  data->point.x = px;
  data->point.y = py;
  data->state = tch.reported ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---------------------------------------------------------------- LCD
bool lcd_init() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = lcd::PCLK_HZ;
  cfg.timings.h_res = lcd::W;
  cfg.timings.v_res = lcd::H;
  cfg.timings.hsync_pulse_width = lcd::HSYNC_PULSE;
  cfg.timings.hsync_back_porch = lcd::HSYNC_BACK;
  cfg.timings.hsync_front_porch = lcd::HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = lcd::VSYNC_PULSE;
  cfg.timings.vsync_back_porch = lcd::VSYNC_BACK;
  cfg.timings.vsync_front_porch = lcd::VSYNC_FRONT;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = lcd::W * 10;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = pins::LCD_HSYNC;
  cfg.vsync_gpio_num = pins::LCD_VSYNC;
  cfg.de_gpio_num = pins::LCD_DE;
  cfg.pclk_gpio_num = pins::LCD_PCLK;
  cfg.disp_gpio_num = -1;
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = pins::LCD_DATA[i];
  cfg.flags.fb_in_psram = 1;
  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;
  if (esp_lcd_panel_reset(panel) != ESP_OK) return false;
  return esp_lcd_panel_init(panel) == ESP_OK;
}

void flush_cb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  // RGB panels copy into the frame buffer synchronously.
  if (flipped) {
    // A 180 degree turn of a rectangular block is just the pixels in reverse order,
    // because its rows are contiguous. Doing it here rather than through
    // esp_lcd_panel_mirror() keeps it working whether or not the RGB driver supports
    // mirroring, which it does not advertise.
    const int n = (a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
    for (int i = 0, j = n - 1; i < j; ++i, --j) {
      lv_color_t t = px[i];
      px[i] = px[j];
      px[j] = t;
    }
    esp_lcd_panel_draw_bitmap(panel, lcd::W - 1 - a->x2, lcd::H - 1 - a->y2,
                              lcd::W - a->x1, lcd::H - a->y1, px);
  } else {
    esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
  }
  lv_disp_flush_ready(drv);
}

}  // namespace

bool begin() {
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  Wire.setTimeOut(50);

  // All expander pins as outputs; USB (not CAN) on the shared port; backlight on.
  ioex_write(ioex::REG_MODE, 0xFF);
  ioex_out = 0xFF & ~(1 << ioex::PIN_USB_CAN_SEL);
  ioex_write(ioex::REG_OUT, ioex_out);

  touch_reset();

  if (!lcd_init()) {
    log_e("LCD init failed");
    return false;
  }
  ioex_pin(ioex::PIN_BACKLIGHT, true);
  set_backlight(100);

  lv_init();
  lv_mutex = xSemaphoreCreateRecursiveMutex();

  const size_t buf_px = lcd::W * 100;
  auto* buf = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!buf) {
    log_e("No PSRAM for LVGL buffer");
    return false;
  }
  lv_disp_draw_buf_init(&draw_buf, buf, nullptr, buf_px);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = lcd::W;
  disp_drv.ver_res = lcd::H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&indev_drv);
  return true;
}

void set_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent < 3) percent = 3;  // never fully off: the buttons must stay visible
  bl_percent = percent;
  // The expander's PWM is inverted: 0 = full brightness. Waveshare caps it at 97.
  uint8_t duty = 100 - percent;
  if (duty > 97) duty = 97;
  ioex_write(ioex::REG_PWM, (uint8_t)(duty * 255 / 100));
}

uint8_t backlight() { return bl_percent; }

void set_flipped(bool on) {
  if (on == flipped) return;
  flipped = on;
  lv_obj_invalidate(lv_scr_act());  // nothing cached is valid any more
}

bool has_sd_slot() { return true; }

void sd_cs_high() { ioex_pin(ioex::PIN_SD_CS, true); }

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

uint32_t touches_accepted() { return tch.accepted; }
uint32_t touches_filtered() { return tch.filtered; }

}  // namespace board

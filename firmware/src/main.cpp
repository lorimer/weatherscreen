// Weather & Pain Station
// Waveshare ESP32-S3-Touch-LCD-7B + BME280. See README.md.
//
// Core 1 (Arduino loop): LVGL screen and touch.
// Core 0 (net task):     Wi-Fi, weather, sensor, analysis, web UI, OTA.
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <lvgl.h>

#include "board.h"
#include "config.h"
#include "history.h"
#include "net.h"
#include "sensor.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"

// Keep a freshly installed firmware "on probation": if it crashes before it has run
// for a minute, the bootloader rolls back to the previous version.
bool verifyRollbackLater() { return true; }

namespace {
bool marked_valid = false;
}

void setup() {
  Serial.begin(115200);
  log_i("Weather & Pain Station %s", FW_VERSION);

  // Watchdog: reboot if the UI or network task hangs for 30 s.
  esp_task_wdt_config_t wdt = {.timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt);

  settings::load();
  if (!board::begin()) {
    log_e("Display failed to start; restarting in 5 s");
    delay(5000);
    ESP.restart();
  }
  storage::begin();
  sensor::begin();
  history::begin();

  board::lvgl_lock();
  ui::begin();
  board::lvgl_unlock();

  net::begin();
  esp_task_wdt_add(nullptr);
}

void loop() {
  if (board::lvgl_lock(100)) {
    ui::loop();
    lv_timer_handler();
    board::lvgl_unlock();
  }
  esp_task_wdt_reset();

  if (!marked_valid && millis() > 60000) {
    esp_ota_mark_app_valid_cancel_rollback();
    marked_valid = true;
  }
  delay(5);
}

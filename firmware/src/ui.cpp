// Screen routing, dimming and the OTA overlay.
#include "ui.h"

#include <atomic>

#include "board.h"
#include "history.h"
#include "settings.h"
#include "ui_common.h"

namespace ui {
namespace {

std::atomic<bool> updating{false};
lv_obj_t* overlay = nullptr;
uint32_t last_dim_check = 0;

// 0 at night, 1 in daytime, with a linear fade of `fade_s` centred on sunrise/sunset.
float daylight(int64_t t, int64_t rise, int64_t set, int64_t fade_s) {
  if (!rise || !set) return -1;
  float a = (float)(t - (rise - fade_s / 2)) / (float)fade_s;
  float b = (float)((set + fade_s / 2) - t) / (float)fade_s;
  return constrain(min(a, b), 0.0f, 1.0f);
}

void update_dimming() {
  if (millis() - last_dim_check < 20000 && last_dim_check) return;
  last_dim_check = millis();
  const auto& s = settings::get();
  history::Snapshot snap;
  history::snapshot_if_newer(0xFFFFFFFF, &snap);  // always copies (version never matches)
  int64_t now = time(nullptr);
  float f = -1;
  if (snap.wx_valid && now > 1700000000) {
    int64_t fade = (int64_t)s.fade_minutes * 60;
    f = max(daylight(now, snap.wx.today.sunrise, snap.wx.today.sunset, fade),
            daylight(now, snap.wx.tomorrow_sun.sunrise, snap.wx.tomorrow_sun.sunset, fade));
  }
  uint8_t target = f < 0 ? s.day_brightness
                         : (uint8_t)lroundf(s.night_brightness + (s.day_brightness - s.night_brightness) * f);
  // Setup screens are always full brightness so a maintainer can see them.
  if (!detail::main_active()) target = 100;
  if (target != board::backlight()) board::set_backlight(target);
}

void update_overlay() {
  bool on = updating.load();
  if (on && !overlay) {
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lcd::W, lcd::H);
    lv_obj_set_style_bg_color(overlay, C(col::BG), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);  // swallow taps
    lv_obj_t* l = label(overlay, &atk_b40, col::TEXT, "Updating software...");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -20);
    lv_obj_t* l2 = label(overlay, &atk_r28, col::MUTED, "This takes about a minute. Please leave it plugged in.");
    lv_obj_align(l2, LV_ALIGN_CENTER, 0, 36);
    board::set_backlight(100);
  } else if (!on && overlay) {
    lv_obj_del(overlay);
    overlay = nullptr;
  }
}

}  // namespace

namespace {
// Where the boot screen hands over once its reveal has finished.
void route() {
  if (!settings::get().setup_done()) show_setup_first_time();
  else show_main();
}
}  // namespace

void begin() {
  board::set_flipped(settings::get().flip_display);
  detail::build_main();
  detail::show_boot(route);
}

void loop() {
  if (detail::main_active()) {
    detail::refresh_main(false);
    detail::tick_clock();
  }
  update_dimming();
  update_overlay();
}

void post_updating(bool on) { updating.store(on); }

}  // namespace ui

// Boot screen: the Old Man Weather mark, then the wordmark on its bar.
//
// The artwork is the approved boot artboard (Claude Design canvas "Old Man Weather -
// Logo", project/Boot.dc.html): a 320 px mark centred above a 132 px black bar holding
// the wordmark. The artboard is a still; the reveal below is ours.
//
// It is deliberately short and calm -- it is the first thing the user sees every time
// the station is plugged in, and the point is to look settled, not to perform. Nothing
// here blocks start-up: the animation runs on LVGL timers while the network task gets
// on with Wi-Fi, so it costs no boot time at all.
#include "icons/logo.h"
#include "ui.h"
#include "ui_common.h"

namespace ui {
namespace detail {
namespace {

constexpr int BAR_H = 132;        // the artboard's wordmark bar
constexpr int MARK = 320;         // the artboard's mark size
constexpr uint32_t T_MARK = 650;  // the mark fades and settles
constexpr uint32_t T_BAR_IN = 350, T_BAR = 500;
constexpr uint32_t T_WORD_IN = 620, T_WORD = 380;
constexpr uint32_t T_HOLD = 1900;  // total, then the real screen takes over

lv_obj_t* boot_scr = nullptr;
void (*after)() = nullptr;

void set_zoom(void* o, int32_t v) { lv_img_set_zoom((lv_obj_t*)o, (lv_coord_t)v); }
void set_img_opa(void* o, int32_t v) { lv_obj_set_style_img_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
void set_opa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
void set_y(void* o, int32_t v) { lv_obj_set_y((lv_obj_t*)o, (lv_coord_t)v); }

void anim(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t delay,
          uint32_t time) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_time(&a, time);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

void finish_cb(lv_timer_t* t) {
  lv_timer_del(t);
  lv_obj_t* old = boot_scr;
  boot_scr = nullptr;
  if (after) after();            // loads the main screen or the setup wizard
  if (old) lv_obj_del_async(old);
}

}  // namespace

void show_boot(void (*done)()) {
  after = done;
  boot_scr = screen();
  lv_scr_load(boot_scr);

  // The mark, centred in the space above the bar.
  lv_obj_t* mark = lv_img_create(boot_scr);
  lv_img_set_src(mark, &ic_logo_320);
  lv_obj_set_style_transform_pivot_x(mark, MARK / 2, 0);
  lv_obj_set_style_transform_pivot_y(mark, MARK / 2, 0);
  lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, (lcd::H - BAR_H - MARK) / 2);
  lv_img_set_zoom(mark, 218);  // 85 %
  lv_obj_set_style_img_opa(mark, LV_OPA_TRANSP, 0);

  // The wordmark bar, which slides up from below the screen.
  lv_obj_t* bar = box(boot_scr);
  lv_obj_set_size(bar, lcd::W, BAR_H);
  lv_obj_set_style_bg_color(bar, C(col::TEXT), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_pos(bar, 0, lcd::H);

  lv_obj_t* word = label(bar, &atk_b64, col::BG, "OLD MAN WEATHER");
  lv_obj_set_style_text_letter_space(word, 12, 0);
  lv_obj_align(word, LV_ALIGN_CENTER, 6, 0);  // the tracking adds a gap on the right
  lv_obj_set_style_opa(word, LV_OPA_TRANSP, 0);

  anim(mark, set_img_opa, LV_OPA_TRANSP, LV_OPA_COVER, 0, T_MARK);
  anim(mark, set_zoom, 218, 256, 0, T_MARK);
  anim(bar, set_y, lcd::H, lcd::H - BAR_H, T_BAR_IN, T_BAR);
  anim(word, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, T_WORD_IN, T_WORD);
  lv_timer_create(finish_cb, T_HOLD, nullptr);
}

}  // namespace detail
}  // namespace ui

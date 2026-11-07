// Main screen: current weather (left), pressure and pain history (middle),
// pain buttons (right). Layout follows the approved mockup at 1024 x 600.
#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "history.h"
#include "icons/icons.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "ui_common.h"
#include "weather.h"

namespace ui {
namespace {

lv_obj_t* scr = nullptr;

// weather pane
lv_obj_t *header, *lbl_date, *lbl_time, *img_now, *lbl_temp, *lbl_cond, *lbl_hi, *lbl_lo, *lbl_feels;
struct FcCol { lv_obj_t *day, *img, *hi, *lo, *word; } fc[3];
// history pane
lv_obj_t *lbl_press, *img_trend, *lbl_trend, *lbl_stale;
lv_obj_t *chart_p, *chart_pain, *dot_p, *dot_pain;
lv_chart_series_t *ser_p, *ser_pain;
// pain pane
lv_obj_t* pain_btn[5];
lv_obj_t* pain_lbl[5];
lv_timer_t* pain_timer[5];

history::Snapshot snap;
uint32_t snap_version = 0;
int last_minute = -1;
uint32_t hold_start = 0;
bool hold_fired = false;

const lv_img_dsc_t* icon_for(int code, bool day, bool big) {
#define PICK(n) (big ? &ic_##n##_132 : &ic_##n##_64)
  switch (code) {
    case 0: return day ? PICK(clear_day) : PICK(clear_night);
    case 1: case 2: return day ? PICK(partly_day) : PICK(partly_night);
    case 3: return PICK(cloudy);
    case 45: case 48: return PICK(fog);
    case 51: case 53: case 55: case 56: case 57: return PICK(drizzle);
    case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: return PICK(rain);
    case 71: case 73: case 75: case 77: case 85: case 86: return PICK(snow);
    case 95: case 96: case 99: return PICK(thunder);
    default: return PICK(cloudy);
  }
#undef PICK
}

String deg(float f) { return isnan(f) ? String("--") : String((int)lroundf(f)) + "\xC2\xB0"; }

struct tm local_tm(int64_t t, int32_t off) {
  time_t lt = (time_t)(t + off);
  struct tm v;
  gmtime_r(&lt, &v);
  return v;
}

const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                         "August", "September", "October", "November", "December"};

// ---------------------------------------------------------------- pain buttons
void pain_restore_cb(lv_timer_t* t) {
  int i = (int)(intptr_t)t->user_data;
  lv_obj_set_style_text_font(pain_lbl[i], &atk_b48, 0);
  lv_label_set_text_fmt(pain_lbl[i], "%d", i + 1);
  pain_timer[i] = nullptr;
}

void pain_pressed_cb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  history::add_tap(i + 1);
  // Feedback: a check mark for one second, then the number again.
  lv_obj_set_style_text_font(pain_lbl[i], &lv_font_montserrat_48, 0);
  lv_label_set_text(pain_lbl[i], LV_SYMBOL_OK);
  if (pain_timer[i]) lv_timer_reset(pain_timer[i]);
  else {
    pain_timer[i] = lv_timer_create(pain_restore_cb, 1000, (void*)(intptr_t)i);
    lv_timer_set_repeat_count(pain_timer[i], 1);
  }
}

// ---------------------------------------------------------------- maintenance hold
// Holding the date/time for 10 s opens settings. The user never needs this.
void header_cb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_PRESSED) {
    hold_start = millis();
    hold_fired = false;
  } else if (c == LV_EVENT_PRESSING) {
    if (!hold_fired && millis() - hold_start >= timing::MAINT_HOLD_MS) {
      hold_fired = true;
      show_maintenance();
    }
  }
}

// ---------------------------------------------------------------- charts
lv_obj_t* make_chart(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, uint32_t color, lv_chart_series_t** ser,
                     lv_obj_t** dot) {
  lv_obj_t* ch = lv_chart_create(parent);
  lv_obj_remove_style_all(ch);
  lv_obj_set_size(ch, w, h);
  lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ch, LV_OBJ_FLAG_SCROLLABLE);
  lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(ch, history::SERIES_POINTS);
  lv_chart_set_div_line_count(ch, 0, 0);
  lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_pad_all(ch, 6, 0);
  lv_obj_set_style_line_width(ch, 4, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(ch, true, LV_PART_ITEMS);
  lv_obj_set_style_width(ch, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(ch, 0, LV_PART_INDICATOR);
  // Baseline and a midline marking "1 day ago".
  lv_obj_t* base = box(ch);
  lv_obj_add_flag(base, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(base, w - 12, 2);  // content width (chart pad is 6)
  lv_obj_set_style_bg_color(base, C(col::AXIS), 0);
  lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
  lv_obj_align(base, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t* mid = box(ch);
  lv_obj_add_flag(mid, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(mid, 2, h - 12);
  lv_obj_set_style_bg_color(mid, C(col::MIDLINE), 0);
  lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
  lv_obj_align(mid, LV_ALIGN_TOP_MID, 0, 0);
  *ser = lv_chart_add_series(ch, C(color), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(ch, *ser, LV_CHART_POINT_NONE);
  // "Now" dot on the last point.
  *dot = box(ch);
  lv_obj_add_flag(*dot, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(*dot, 12, 12);
  lv_obj_set_style_radius(*dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(*dot, C(color), 0);
  lv_obj_set_style_bg_opa(*dot, LV_OPA_COVER, 0);
  return ch;
}

void place_dot(lv_obj_t* ch, lv_chart_series_t* ser, lv_obj_t* dot) {
  uint16_t last = history::SERIES_POINTS - 1;
  if (ser->y_points[last] == LV_CHART_POINT_NONE) {
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_update_layout(ch);
  lv_point_t p;
  lv_chart_get_point_pos_by_id(ch, ser, last, &p);
  // Point coordinates include the chart's 6 px padding; child positions add it again.
  lv_obj_set_pos(dot, p.x - 6 - 6, p.y - 6 - 6);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

void fill_charts() {
  // Pressure: auto-scaled with a minimum span of 0.10 inHg so small wiggles stay small.
  float lo = 1e9, hi = -1e9;
  for (float v : snap.pressure_series)
    if (!isnan(v)) { lo = min(lo, v); hi = max(hi, v); }
  if (lo > hi) { lo = 29.8f; hi = 30.1f; }
  float span = max(0.10f, hi - lo), mid = (lo + hi) / 2;
  lo = mid - span * 0.65f;
  hi = mid + span * 0.65f;
  // LVGL 8 reserves coordinates above 8191, so plot thousandths of an inch above 28 inHg.
  auto enc = [](float inhg) { return (lv_coord_t)lroundf((inhg - 28.0f) * 1000); };
  lv_chart_set_range(chart_p, LV_CHART_AXIS_PRIMARY_Y, enc(lo), enc(hi));
  for (int i = 0; i < history::SERIES_POINTS; ++i) {
    float v = snap.pressure_series[i];
    ser_p->y_points[i] = isnan(v) ? LV_CHART_POINT_NONE : enc(v);
    float q = snap.pain_series[i];
    ser_pain->y_points[i] = isnan(q) ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(q * 100);
  }
  lv_chart_refresh(chart_p);
  lv_chart_refresh(chart_pain);
  place_dot(chart_p, ser_p, dot_p);
  place_dot(chart_pain, ser_pain, dot_pain);
}

void fill_weather() {
  const auto& w = snap.wx;
  if (!snap.wx_valid) {
    lv_img_set_src(img_now, &ic_cloudy_132);
    lv_label_set_text(lbl_temp, "--");
    lv_label_set_text(lbl_cond, net::connected() ? "Loading..." : "Offline");
    lv_label_set_text(lbl_hi, "");
    lv_label_set_text(lbl_lo, "");
    lv_label_set_text(lbl_feels, "");
    for (auto& c : fc) {
      lv_label_set_text(c.day, "");
      lv_label_set_text(c.hi, "");
      lv_label_set_text(c.lo, "");
      lv_label_set_text(c.word, "");
      lv_obj_add_flag(c.img, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  lv_img_set_src(img_now, icon_for(w.code, w.is_day, true));
  lv_label_set_text(lbl_temp, deg(w.temp_f).c_str());
  lv_label_set_text(lbl_cond, weather::describe(w.code, w.is_day));
  lv_label_set_text(lbl_hi, ("High " + deg(w.today.hi)).c_str());
  lv_label_set_text(lbl_lo, ("Low " + deg(w.today.lo)).c_str());
  String feels = "Feels like " + deg(w.feels_f) + "  \xC2\xB7  Wind " +
                 (isnan(w.wind_mph) ? String("--") : String((int)lroundf(w.wind_mph))) + " mph";
  lv_label_set_text(lbl_feels, feels.c_str());
  for (int k = 0; k < 3; ++k) {
    const auto& d = w.next[k];
    struct tm t = local_tm(d.date + 12 * 3600, 0);  // date is local midnight already
    lv_label_set_text(fc[k].day, kDays[t.tm_wday]);
    lv_img_set_src(fc[k].img, icon_for(d.code, true, false));
    lv_obj_clear_flag(fc[k].img, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(fc[k].hi, deg(d.hi).c_str());
    lv_label_set_text(fc[k].lo, (" / " + deg(d.lo)).c_str());
    lv_label_set_text(fc[k].word, weather::describe_forecast(d.code, d.precip_pct));
  }
}

void fill_pressure() {
  if (isnan(snap.pressure_inhg)) lv_label_set_text(lbl_press, "--");
  else lv_label_set_text(lbl_press, String(snap.pressure_inhg, 2).c_str());
  const char* word = "";
  const lv_img_dsc_t* img = nullptr;
  uint32_t color = col::MUTED;
  switch (snap.trend) {
    case history::Trend::FallingFast: word = "Falling fast"; img = &ic_arrow_down; color = col::FALL_C; break;
    case history::Trend::Falling: word = "Falling"; img = &ic_arrow_down; color = col::FALL_C; break;
    case history::Trend::Steady: word = "Steady"; img = &ic_arrow_right; break;
    case history::Trend::Rising: word = "Rising"; img = &ic_arrow_up; color = col::RISE_C; break;
    case history::Trend::RisingFast: word = "Rising fast"; img = &ic_arrow_up; color = col::RISE_C; break;
    default: break;
  }
  lv_label_set_text(lbl_trend, word);
  lv_obj_set_style_text_color(lbl_trend, C(color), 0);
  if (img) {
    lv_img_set_src(img_trend, img);
    lv_obj_clear_flag(img_trend, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(img_trend, LV_OBJ_FLAG_HIDDEN);
  }
}

void fill_stale() {
  int64_t now = time(nullptr);
  String s;
  if (snap.wx_valid && now - snap.wx.fetched > (int64_t)timing::STALE_AFTER_S) {
    int h = (int)((now - snap.wx.fetched) / 3600);
    s = h >= 48 ? "Offline " + String(h / 24) + " days" : "Offline " + String(h) + " h";
  } else if (!snap.wx_valid && !net::connected() && settings::get().has_wifi()) {
    s = "Offline";
  }
  lv_label_set_text(lbl_stale, s.c_str());
}

}  // namespace

namespace detail {

bool main_active() { return scr && lv_scr_act() == scr; }

void build_main() {
  scr = screen();
  lv_obj_t* root = row(scr);
  lv_obj_set_size(root, lcd::W, lcd::H);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // ---------------- Weather pane (600 px)
  lv_obj_t* wp = column(root);
  lv_obj_set_size(wp, 600, lcd::H);
  lv_obj_set_style_pad_left(wp, 32, 0);
  lv_obj_set_style_pad_right(wp, 32, 0);
  lv_obj_set_style_pad_top(wp, 22, 0);
  lv_obj_set_style_pad_bottom(wp, 24, 0);
  lv_obj_set_flex_align(wp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  header = row(wp);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(header, header_cb, LV_EVENT_ALL, nullptr);
  lbl_date = label(header, &atk_b28, col::TEXT, "");
  lbl_time = label(header, &atk_b28, col::TEXT, "");

  lv_obj_t* now = row(wp, 20);
  lv_obj_set_flex_align(now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  img_now = lv_img_create(now);
  lv_img_set_src(img_now, &ic_cloudy_132);
  lbl_temp = label(now, &atk_b168, col::TEXT, "--");
  lv_obj_set_style_text_letter_space(lbl_temp, -4, 0);
  lv_obj_t* hl = column(now, 6);
  lv_obj_set_style_pad_left(hl, 4, 0);
  lbl_cond = label(hl, &atk_b40, col::TEXT, "");
  lbl_hi = label(hl, &atk_r30, col::TEXT, "");
  lbl_lo = label(hl, &atk_r30, col::TEXT, "");

  lbl_feels = label(wp, &atk_r28, col::TEXT2, "");
  hrule(wp, 536);

  lv_obj_t* fcrow = row(wp, 16);
  lv_obj_set_width(fcrow, LV_PCT(100));
  for (int k = 0; k < 3; ++k) {
    lv_obj_t* c = column(fcrow, 4);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_width(c, 1);  // equal columns via flex-grow
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    fc[k].day = label(c, &atk_b30, col::TEXT, "");
    fc[k].img = lv_img_create(c);
    lv_obj_add_flag(fc[k].img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* t = row(c);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    fc[k].hi = label(t, &atk_b34, col::TEXT, "");
    fc[k].lo = label(t, &atk_r34, col::MUTED, "");
    fc[k].word = label(c, &atk_r24, col::TEXT2, "");
  }

  // Divider
  lv_obj_t* d1 = box(root);
  lv_obj_set_size(d1, 2, lcd::H - 40);
  lv_obj_set_style_bg_color(d1, C(col::RULE), 0);
  lv_obj_set_style_bg_opa(d1, LV_OPA_COVER, 0);

  // ---------------- History pane (298 px)
  lv_obj_t* hp = column(root);
  lv_obj_set_size(hp, 298, lcd::H);
  lv_obj_set_style_pad_left(hp, 22, 0);
  lv_obj_set_style_pad_right(hp, 20, 0);
  lv_obj_set_style_pad_top(hp, 22, 0);
  lv_obj_set_style_pad_bottom(hp, 24, 0);
  lv_obj_set_flex_align(hp, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* g1 = column(hp);
  lv_obj_t* cap1 = label(g1, &atk_b22, col::MUTED, "PRESSURE");
  lv_obj_set_style_text_letter_space(cap1, 1, 0);
  lv_obj_t* pv = row(g1, 8);
  lv_obj_set_flex_align(pv, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_top(pv, 4, 0);
  lbl_press = label(pv, &atk_b64, col::TEXT, "--");
  lv_obj_t* unit = label(pv, &atk_r24, col::MUTED, "inHg");
  lv_obj_set_style_pad_bottom(unit, 10, 0);
  lv_obj_t* tr = row(g1, 8);
  lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(tr, 4, 0);
  img_trend = lv_img_create(tr);
  lv_obj_add_flag(img_trend, LV_OBJ_FLAG_HIDDEN);
  lbl_trend = label(tr, &atk_b28, col::MUTED, "");

  lbl_stale = label(hp, &atk_b20, col::FALL_C, "");
  lv_obj_add_flag(lbl_stale, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
  lv_obj_align(lbl_stale, LV_ALIGN_TOP_RIGHT, 0, 2);

  chart_p = make_chart(hp, 256, 118, col::PRESSURE, &ser_p, &dot_p);

  lv_obj_t* g3 = column(hp, 8);
  lv_obj_t* cap2 = label(g3, &atk_b22, col::MUTED, "PAIN TREND");
  lv_obj_set_style_text_letter_space(cap2, 1, 0);
  chart_pain = make_chart(g3, 256, 104, col::PAIN, &ser_pain, &dot_pain);
  lv_chart_set_range(chart_pain, LV_CHART_AXIS_PRIMARY_Y, 80, 520);  // 1..5 with margin
  lv_obj_t* ax = row(g3);
  lv_obj_set_width(ax, 256);
  lv_obj_set_style_pad_top(ax, 0, 0);
  lv_obj_set_flex_align(ax, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(ax, &atk_r20, col::MUTED, "2 days ago");
  label(ax, &atk_r20, col::MUTED, "Now");

  lv_obj_t* d2 = box(root);
  lv_obj_set_size(d2, 2, lcd::H - 40);
  lv_obj_set_style_bg_color(d2, C(col::RULE), 0);
  lv_obj_set_style_bg_opa(d2, LV_OPA_COVER, 0);

  // ---------------- Pain buttons (122 px)
  lv_obj_t* pp = column(root, 8);
  lv_obj_set_size(pp, 122, lcd::H);
  lv_obj_set_style_pad_hor(pp, 10, 0);
  lv_obj_set_style_pad_ver(pp, 12, 0);
  lv_obj_set_flex_align(pp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(pp, &atk_b20, col::MUTED, "Worst");
  for (int s = 5; s >= 1; --s) {
    int i = s - 1;
    lv_obj_t* b = lv_btn_create(pp);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, C(col::PAIN_BTN[i]), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(C(col::PAIN_BTN[i]), LV_OPA_30), LV_STATE_PRESSED);
    // Fire on touch-down: a finger that slides off the button still counts.
    lv_obj_add_event_cb(b, pain_pressed_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
    pain_lbl[i] = label(b, &atk_b48, 0xFFFFFF, "");
    lv_label_set_text_fmt(pain_lbl[i], "%d", s);
    lv_obj_center(pain_lbl[i]);
    pain_btn[i] = b;
    pain_timer[i] = nullptr;
  }
  label(pp, &atk_b20, col::MUTED, "Least");

  lv_scr_load(scr);
}

void tick_clock() {
  int64_t now = time(nullptr);
  if (now < 1700000000) {
    lv_label_set_text(lbl_date, "");
    lv_label_set_text(lbl_time, "");
    return;
  }
  int32_t off = snap.wx_valid ? snap.utc_offset_s : settings::get().utc_offset_s;
  struct tm t = local_tm(now, off);
  if (t.tm_min == last_minute) return;
  last_minute = t.tm_min;
  lv_label_set_text_fmt(lbl_date, "%s, %s %d", kDays[t.tm_wday], kMonths[t.tm_mon], t.tm_mday);
  int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
  lv_label_set_text_fmt(lbl_time, "%d:%02d %s", h12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  fill_stale();
}

void refresh_main(bool force) {
  if (!scr) return;
  if (!history::snapshot_if_newer(force ? 0 : snap_version, &snap) && !force) return;
  snap_version = snap.version;
  fill_weather();
  fill_pressure();
  fill_charts();
  fill_stale();
  last_minute = -1;
  tick_clock();
}

}  // namespace detail

void show_main() {
  if (!scr) detail::build_main();
  lv_scr_load(scr);
  detail::refresh_main(true);
}

}  // namespace ui

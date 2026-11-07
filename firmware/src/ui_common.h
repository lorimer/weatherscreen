// Small LVGL helpers shared by the screens.
#pragma once
#include <lvgl.h>

#include "config.h"
#include "fonts/fonts.h"

namespace ui {

inline lv_color_t C(uint32_t rgb) { return lv_color_hex(rgb); }

// A plain container: no background, border, padding or scrolling.
inline lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  return o;
}

inline lv_obj_t* column(lv_obj_t* parent, int gap = 0) {
  lv_obj_t* o = box(parent);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}

inline lv_obj_t* row(lv_obj_t* parent, int gap = 0) {
  lv_obj_t* o = box(parent);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

inline lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_label_set_text(l, text);
  return l;
}

inline lv_obj_t* screen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, C(col::BG), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

inline lv_obj_t* hrule(lv_obj_t* parent, lv_coord_t w) {
  lv_obj_t* o = box(parent);
  lv_obj_set_size(o, w, 2);
  lv_obj_set_style_bg_color(o, C(col::RULE), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

// Large, flat button with a centred label.
inline lv_obj_t* button(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg,
                        const lv_font_t* font, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_bg_color(b, C(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_darken(C(bg), LV_OPA_20), LV_STATE_PRESSED);
  lv_obj_t* l = label(b, font, fg, text);
  lv_obj_center(l);
  return b;
}

}  // namespace ui

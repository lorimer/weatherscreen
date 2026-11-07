// First-time setup (Wi-Fi -> location -> maintenance password) and the maintenance
// menu opened by holding the date/time for 10 seconds.
//
// Only maintainers see these screens. Screens opened from the maintenance menu return
// to the main screen by themselves after a few minutes without a touch, so nobody can
// get stuck here. First-time setup has no timeout (there is nothing to go back to).
#include <WiFi.h>

#include <vector>

#include "board.h"
#include "history.h"
#include "net.h"
#include "sensor.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"
#include "ui_common.h"
#include "weather.h"

namespace ui {
namespace {

bool first_time = false;
lv_obj_t* setup_scr = nullptr;
lv_timer_t* poll = nullptr;
lv_timer_t* idle = nullptr;
lv_obj_t* ta = nullptr;
lv_obj_t* msg = nullptr;
String sel_ssid;

// Keyboard
lv_font_t kb_font;
const char* act_text = "Next";
void (*on_action)() = nullptr;
void (*on_back)() = nullptr;

// Background geocode
TaskHandle_t geo_task = nullptr;
volatile bool geo_done = false;
bool geo_ok = false;
String geo_query, geo_err;
std::vector<weather::Place> places;

void stop_poll() {
  if (poll) { lv_timer_del(poll); poll = nullptr; }
}

void show(lv_obj_t* s) {
  stop_poll();
  lv_obj_t* old = setup_scr;
  setup_scr = s;
  lv_scr_load(s);
  if (old) lv_obj_del_async(old);
  ta = nullptr;
}

void leave_to_main() {
  stop_poll();
  if (idle) { lv_timer_del(idle); idle = nullptr; }
  net::set_setup_active(false);
  show_main();
  if (setup_scr) { lv_obj_del_async(setup_scr); setup_scr = nullptr; }
}

void idle_cb(lv_timer_t*) {
  if (!first_time && lv_disp_get_inactive_time(nullptr) > timing::SETTINGS_IDLE_MS) leave_to_main();
}

// Page skeleton: title + step on the top line, returns the content column.
lv_obj_t* page(const char* title, const char* step) {
  lv_obj_t* s = screen();
  lv_obj_t* col = column(s, 14);
  lv_obj_set_size(col, lcd::W, lcd::H);
  lv_obj_set_style_pad_hor(col, 32, 0);
  lv_obj_set_style_pad_ver(col, 20, 0);
  lv_obj_t* hdr = row(col);
  lv_obj_set_width(hdr, LV_PCT(100));
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(hdr, &atk_b34, col::TEXT, title);
  label(hdr, &atk_r24, col::MUTED, step);
  show(s);
  return col;
}

// ---------------------------------------------------------------- keyboard
const char* map_lower[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                           "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                           "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                           "ABC", "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
                           "Back", "1#", " ", "Next", ""};
const char* map_upper[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                           "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                           "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                           "abc", "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
                           "Back", "1#", " ", "Next", ""};
const char* map_spec[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                          "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", "\n",
                          ".", ",", "?", "!", "'", "_", "#", "%", "+", "\n",
                          "abc", "=", "*", "<", ">", "[", "]", "~", LV_SYMBOL_BACKSPACE, "\n",
                          "Back", "ABC", " ", "Next", ""};
constexpr lv_btnmatrix_ctrl_t K = 4;
constexpr lv_btnmatrix_ctrl_t F = LV_KEYBOARD_CTRL_BTN_FLAGS;
const lv_btnmatrix_ctrl_t ctrl_alpha[] = {K, K, K, K, K, K, K, K, K, K,
                                          K, K, K, K, K, K, K, K, K, K,
                                          K, K, K, K, K, K, K, K, K,
                                          6 | F, K, K, K, K, K, K, K, 6 | LV_BTNMATRIX_CTRL_CHECKED,
                                          5 | F, 4 | F, 14, 8 | F};
const lv_btnmatrix_ctrl_t ctrl_spec[] = {K, K, K, K, K, K, K, K, K, K,
                                         K, K, K, K, K, K, K, K, K, K,
                                         K, K, K, K, K, K, K, K, K,
                                         6 | F, K, K, K, K, K, K, K, 6 | LV_BTNMATRIX_CTRL_CHECKED,
                                         5 | F, 4 | F, 14, 8 | F};

void kb_cb(lv_event_t* e) {
  lv_obj_t* kb = lv_event_get_target(e);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  if (id == LV_BTNMATRIX_BTN_NONE) return;
  const char* txt = lv_btnmatrix_get_btn_text(kb, id);
  if (!txt) return;
  if (strcmp(txt, "Back") == 0) {
    if (on_back) on_back();
  } else if (strcmp(txt, act_text) == 0) {
    if (on_action) on_action();
  } else {
    lv_keyboard_def_event_cb(e);
  }
}

// A label, a large text box and a full-width keyboard.
void text_entry(lv_obj_t* col, const char* prompt, const char* initial, const char* action,
                void (*act)(), void (*back)()) {
  act_text = action;
  on_action = act;
  on_back = back;
  map_lower[sizeof(map_lower) / sizeof(*map_lower) - 2] = action;
  map_upper[sizeof(map_upper) / sizeof(*map_upper) - 2] = action;
  map_spec[sizeof(map_spec) / sizeof(*map_spec) - 2] = action;

  lv_obj_t* r = row(col, 16);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* p = label(r, &atk_r24, col::TEXT2, prompt);
  lv_obj_set_width(p, 280);
  lv_label_set_long_mode(p, LV_LABEL_LONG_WRAP);
  ta = lv_textarea_create(r);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_text(ta, initial);
  lv_obj_set_flex_grow(ta, 1);
  lv_obj_set_height(ta, 60);
  lv_obj_set_style_text_font(ta, &atk_r30, 0);
  lv_obj_set_style_border_color(ta, C(col::ACCENT), 0);
  lv_obj_set_style_border_width(ta, 3, 0);
  lv_obj_set_style_radius(ta, 12, 0);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);

  msg = label(col, &atk_b22, col::FALL_C, "");

  lv_obj_t* kb = lv_keyboard_create(col);
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, map_lower, ctrl_alpha);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, map_upper, ctrl_alpha);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, map_spec, ctrl_spec);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_set_width(kb, LV_PCT(100));
  lv_obj_set_flex_grow(kb, 1);
  lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, 0, 0);
  lv_obj_set_style_pad_gap(kb, 8, 0);
  lv_obj_set_style_text_font(kb, &kb_font, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, C(col::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, C(col::KEY), LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 2, LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb, C(col::KEY_SHADOW), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, C(col::KEY_ALT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(kb, C(col::TEXT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, C(col::RULE), LV_PART_ITEMS | LV_STATE_PRESSED);
}

void set_msg(const char* m) {
  if (msg) lv_label_set_text(msg, m);
}

// Simple centred message with a spinner.
void busy(const char* title, const char* step, const String& text) {
  lv_obj_t* col = page(title, step);
  lv_obj_t* mid = column(col, 24);
  lv_obj_set_size(mid, LV_PCT(100), 420);
  lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* sp = lv_spinner_create(mid, 1000, 60);
  lv_obj_set_size(sp, 90, 90);
  lv_obj_set_style_arc_color(sp, C(col::ACCENT), LV_PART_INDICATOR);
  label(mid, &atk_r30, col::TEXT, text.c_str());
}

const char* step_label(int n) {
  static char buf[24];
  if (!first_time) return "Settings";
  snprintf(buf, sizeof buf, "Step %d of 3", n);
  return buf;
}

// ---------------------------------------------------------------- Wi-Fi
void wifi_list();
void wifi_pass();
void loc_search();
void admin_pass();
void done_screen();

void wifi_pick_cb(lv_event_t* e) {
  sel_ssid = (const char*)lv_event_get_user_data(e);
  wifi_pass();
}

void wifi_rescan_cb(lv_event_t*) { wifi_list(); }
void to_main_cb(lv_event_t*) { leave_to_main(); }

void scan_poll_cb(lv_timer_t*) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  stop_poll();
  lv_obj_t* col = page("Connect to Wi-Fi", step_label(1));
  label(col, &atk_r24, col::TEXT2, "Tap your network:");
  lv_obj_t* list = column(col, 10);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  // Unique names, strongest first.
  static std::vector<String> names;
  names.clear();
  std::vector<std::pair<int, String>> nets;
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (auto& x : nets) if (x.second == s) { dup = true; x.first = max(x.first, (int)WiFi.RSSI(i)); }
    if (!dup) nets.push_back({WiFi.RSSI(i), s});
  }
  WiFi.scanDelete();
  std::sort(nets.begin(), nets.end(), [](auto& a, auto& b) { return a.first > b.first; });
  names.reserve(nets.size());
  for (auto& x : nets) names.push_back(x.second);
  for (size_t i = 0; i < nets.size() && i < 12; ++i) {
    int rssi = nets[i].first;
    const char* q = rssi > -60 ? "Strong" : rssi > -72 ? "Good" : "Weak";
    lv_obj_t* b = button(list, "", col::KEY, col::TEXT, &atk_b28, LV_PCT(100), 64);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    lv_label_set_text(l, names[i].c_str());
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t* ql = label(b, &atk_r24, col::MUTED, q);
    lv_obj_align(ql, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(b, wifi_pick_cb, LV_EVENT_CLICKED, (void*)names[i].c_str());
  }
  if (nets.empty()) label(list, &atk_r28, col::TEXT, "No networks found.");
  lv_obj_t* btns = row(col, 16);
  lv_obj_set_width(btns, LV_PCT(100));
  lv_obj_t* rs = button(btns, "Scan again", col::KEY_ALT, col::TEXT, &atk_b28, 260, 64);
  lv_obj_add_event_cb(rs, wifi_rescan_cb, LV_EVENT_CLICKED, nullptr);
  if (!first_time) {
    lv_obj_t* c = button(btns, "Cancel", col::KEY_ALT, col::TEXT, &atk_b28, 220, 64);
    lv_obj_add_event_cb(c, to_main_cb, LV_EVENT_CLICKED, nullptr);
  }
}

void wifi_list() {
  net::set_setup_active(true);
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true);
  busy("Connect to Wi-Fi", step_label(1), "Looking for networks...");
  poll = lv_timer_create(scan_poll_cb, 300, nullptr);
}

uint32_t connect_start = 0;
String try_pass;

void connect_poll_cb(lv_timer_t*) {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    stop_poll();
    auto& s = settings::get();
    s.ssid = sel_ssid;
    s.wifi_pass = try_pass;
    settings::save();
    net::request_weather();
    if (first_time || !s.has_location()) loc_search();
    else leave_to_main();
    return;
  }
  if (millis() - connect_start > 20000 || st == WL_CONNECT_FAILED) {
    stop_poll();
    wifi_pass();
    set_msg("Could not connect. Check the password and try again.");
    WiFi.disconnect();
  }
}

void wifi_connect() {
  try_pass = lv_textarea_get_text(ta);
  busy("Connect to Wi-Fi", step_label(1), "Connecting to " + sel_ssid + "...");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(sel_ssid.c_str(), try_pass.c_str());
  connect_start = millis();
  poll = lv_timer_create(connect_poll_cb, 250, nullptr);
}

void wifi_pass() {
  lv_obj_t* col = page("Connect to Wi-Fi", step_label(1));
  String prompt = "Password for " + sel_ssid;
  text_entry(col, prompt.c_str(), sel_ssid == settings::get().ssid ? settings::get().wifi_pass.c_str() : "",
             "Connect", wifi_connect, wifi_list);
}

// ---------------------------------------------------------------- location
void geo_worker(void*) {
  std::vector<weather::Place> res;
  String err;
  bool ok = weather::geocode(geo_query, &res, &err);
  places = res;
  geo_err = err;
  geo_ok = ok;
  geo_done = true;
  geo_task = nullptr;
  vTaskDelete(nullptr);
}

void place_pick_cb(lv_event_t* e) {
  size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= places.size()) return;
  auto& s = settings::get();
  const auto& p = places[i];
  s.place = p.name + ", " + p.region;
  s.lat = p.lat;
  s.lon = p.lon;
  s.elevation_m = p.elevation;
  settings::save();
  net::request_weather();
  if (first_time) admin_pass();
  else leave_to_main();
}

void geo_poll_cb(lv_timer_t*) {
  if (!geo_done) return;
  stop_poll();
  if (!geo_ok) {
    loc_search();
    lv_textarea_set_text(ta, geo_query.c_str());
    set_msg(geo_err.length() ? geo_err.c_str() : "Nothing found. Try a city name or ZIP code.");
    return;
  }
  lv_obj_t* col = page("Location", step_label(2));
  label(col, &atk_r24, col::TEXT2, "Tap the right place:");
  lv_obj_t* list = column(col, 10);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  for (size_t i = 0; i < places.size(); ++i) {
    lv_obj_t* b = button(list, "", col::KEY, col::TEXT, &atk_b28, LV_PCT(100), 64);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    lv_label_set_text(l, places[i].name.c_str());
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t* r = label(b, &atk_r24, col::MUTED, places[i].region.c_str());
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(b, place_pick_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* again = button(col, "Search again", col::KEY_ALT, col::TEXT, &atk_b28, 280, 64);
  lv_obj_add_event_cb(again, [](lv_event_t*) { loc_search(); }, LV_EVENT_CLICKED, nullptr);
}

void loc_go() {
  geo_query = lv_textarea_get_text(ta);
  geo_query.trim();
  if (geo_query.length() < 2) {
    set_msg("Type a city name or ZIP code.");
    return;
  }
  if (!net::connected()) {
    set_msg("Not connected to Wi-Fi.");
    return;
  }
  busy("Location", step_label(2), "Searching for " + geo_query + "...");
  geo_done = false;
  xTaskCreatePinnedToCore(geo_worker, "geocode", 12288, nullptr, 1, &geo_task, 0);
  poll = lv_timer_create(geo_poll_cb, 250, nullptr);
}

void loc_back() {
  if (first_time) wifi_list();
  else leave_to_main();
}

void loc_search() {
  lv_obj_t* col = page("Location", step_label(2));
  text_entry(col, "City or ZIP code for the forecast", "", "Search", loc_go, loc_back);
}

// Signal strength in words first: dBm means nothing to most people reading this screen.
String signal_text() {
  if (!net::connected()) return "not connected";
  const int r = net::rssi();
  const char* q = r >= -55 ? "excellent" : r >= -65 ? "good" : r >= -75 ? "fair"
                                                             : r >= -85 ? "weak" : "very weak";
  return String(q) + "  (" + String(r) + " dBm)";
}

// The lines on the maintenance screen that go out of date while it is open.
lv_obj_t* mi_addr = nullptr;
lv_obj_t* mi_wifi = nullptr;
lv_obj_t* mi_signal = nullptr;
lv_obj_t* mi_weather = nullptr;
lv_obj_t* mi_taps = nullptr;
lv_obj_t* done_addr = nullptr;

void clear_live_labels() {
  mi_addr = mi_wifi = mi_signal = mi_weather = mi_taps = done_addr = nullptr;
}

String address_text() {
  const String ip = net::ip();
  if (ip == "-") return "waiting for Wi-Fi...";
  return net::ap_active() && !net::connected() ? ip + "  (hotspot)" : ip;
}

void live_info_cb(lv_timer_t*) {
  if (mi_addr) lv_label_set_text(mi_addr, address_text().c_str());
  if (mi_wifi) lv_label_set_text(mi_wifi, net::connected() ? settings::get().ssid.c_str()
                                                           : "not connected");
  if (mi_signal) lv_label_set_text(mi_signal, signal_text().c_str());
  if (mi_weather) lv_label_set_text(mi_weather, net::weather_ok() ? "OK"
                                                : net::last_weather_error().c_str());
  if (mi_taps) lv_label_set_text(mi_taps, String((unsigned)history::tap_count()).c_str());
  if (done_addr) lv_label_set_text(done_addr, ("or  http://" + address_text()).c_str());
}

// ---------------------------------------------------------------- maintenance password
void admin_go() {
  String p = lv_textarea_get_text(ta);
  if (p.length() < 8) {
    set_msg("Use at least 8 characters.");
    return;
  }
  settings::get().admin_pass = p;
  settings::save();
  net::request_reconnect();  // restarts the web server / OTA with the new password
  done_screen();
}

void admin_pass() {
  lv_obj_t* col = page("Maintenance password", step_label(3));
  text_entry(col, "For the web page and remote updates (8+ characters)",
             settings::get().admin_pass.c_str(), "Finish", admin_go, loc_search);
}

void done_screen() {
  lv_obj_t* col = page("All set", "");
  lv_obj_t* body = column(col, 14);
  lv_obj_set_style_pad_top(body, 20, 0);
  String host = settings::hostname();
  label(body, &atk_r30, col::TEXT, "The station is ready.");
  label(body, &atk_r28, col::TEXT2, ("Web page:  http://" + host + ".local").c_str());
  clear_live_labels();
  done_addr = label(body, &atk_r28, col::TEXT2, ("or  http://" + address_text()).c_str());
  label(body, &atk_r28, col::TEXT2, "User name: admin");
  label(body, &atk_r24, col::MUTED, "To reopen settings later, hold the date at the top for 10 seconds.");
  poll = lv_timer_create(live_info_cb, 1000, nullptr);  // cleared by the next show()
  lv_obj_t* gap = box(col);
  lv_obj_set_height(gap, 20);
  lv_obj_t* b = button(col, "Start", col::ACCENT, 0xFFFFFF, &atk_b30, 260, 72);
  lv_obj_add_event_cb(b, to_main_cb, LV_EVENT_CLICKED, nullptr);
  first_time = false;
}

// ---------------------------------------------------------------- maintenance menu
lv_obj_t* info_line(lv_obj_t* parent, const char* k, const String& v,
                    const lv_font_t* font = &atk_r24) {
  lv_obj_t* r = row(parent, 12);
  lv_obj_t* kl = label(r, &atk_b22, col::MUTED, k);
  lv_obj_set_width(kl, 200);
  return label(r, font, col::TEXT, v.c_str());
}


}  // namespace

void show_setup_first_time() {
  first_time = true;
  kb_font = atk_b30;
  kb_font.fallback = &lv_font_montserrat_28;
  wifi_list();
}

void show_maintenance() {
  first_time = false;
  kb_font = atk_b30;
  kb_font.fallback = &lv_font_montserrat_28;
  board::set_backlight(100);
  lv_obj_t* col = page("Maintenance", "Settings");
  auto& s = settings::get();
  lv_obj_t* info = column(col, 8);
  lv_obj_set_style_pad_top(info, 6, 0);
  clear_live_labels();
  info_line(info, "Device", s.device_name);
  mi_addr = info_line(info, "IP address", address_text(), &atk_b28);
  info_line(info, "Web page", "http://" + settings::hostname() + ".local");
  mi_wifi = info_line(info, "Wi-Fi", net::connected() ? s.ssid : String("not connected"));
  mi_signal = info_line(info, "Signal", signal_text());
  info_line(info, "Location", s.place.length() ? s.place : String("not set"));
  mi_weather = info_line(info, "Weather", net::weather_ok() ? String("OK") : net::last_weather_error());
  info_line(info, "Sensor", String(sensor::chip_name()));
  char st[64];
  snprintf(st, sizeof st, "%s, %llu of %llu MB used", storage::medium(),
           (unsigned long long)(storage::used_bytes() >> 20), (unsigned long long)(storage::total_bytes() >> 20));
  info_line(info, "Storage", st);
  mi_taps = info_line(info, "Pain taps", String((unsigned)history::tap_count()));
  info_line(info, "Firmware", FW_VERSION);
  poll = lv_timer_create(live_info_cb, 1000, nullptr);  // cleared by the next show()

  lv_obj_t* btns = row(col, 16);
  lv_obj_set_width(btns, LV_PCT(100));
  lv_obj_set_style_pad_top(btns, 16, 0);
  lv_obj_t* b1 = button(btns, "Change Wi-Fi", col::KEY_ALT, col::TEXT, &atk_b28, 220, 72);
  lv_obj_add_event_cb(b1, [](lv_event_t*) { wifi_list(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* b2 = button(btns, "Change location", col::KEY_ALT, col::TEXT, &atk_b28, 260, 72);
  lv_obj_add_event_cb(b2, [](lv_event_t*) { loc_search(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* b3 = button(btns, "Restart", col::KEY_ALT, col::TEXT, &atk_b28, 180, 72);
  lv_obj_add_event_cb(b3, [](lv_event_t*) { ESP.restart(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* b4 = button(btns, "Close", col::ACCENT, 0xFFFFFF, &atk_b28, 180, 72);
  lv_obj_add_event_cb(b4, to_main_cb, LV_EVENT_CLICKED, nullptr);

  if (!idle) idle = lv_timer_create(idle_cb, 1000, nullptr);
}

}  // namespace ui

// Renders the real UI code to PNG files on a PC so the layout can be checked
// against the mockup without hardware.  See build.sh.
#include <lvgl.h>
#include <cstdio>
#include <string>
#include <unistd.h>
#include "../../src/config.h"
#include "../../src/ui.h"

static lv_color_t fb[lcd::W * lcd::H];
static lv_color_t buf[lcd::W * 100];
static bool pressed = false;
static lv_point_t pt{0, 0};

static void flush(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* px) {
  for (int y = a->y1; y <= a->y2; ++y)
    for (int x = a->x1; x <= a->x2; ++x) fb[y * lcd::W + x] = *px++;
  lv_disp_flush_ready(d);
}
static void read(lv_indev_drv_t*, lv_indev_data_t* d) {
  d->point = pt;
  d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void run(int ms) {

  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)ms) { lv_timer_handler(); usleep(5000); }
}
static void tap(int x, int y, int hold_ms = 120) {
  pt = {(lv_coord_t)x, (lv_coord_t)y};
  pressed = true; run(hold_ms); pressed = false; run(150);
}
static void save(const char* name) {
  lv_refr_now(nullptr);
  std::string p = std::string("out/") + name + ".ppm";
  FILE* f = fopen(p.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", lcd::W, lcd::H);
  for (auto c : fb) {
    uint16_t v = c.full;
    unsigned char rgb[3] = {(unsigned char)(((v >> 11) & 0x1F) * 255 / 31), (unsigned char)(((v >> 5) & 0x3F) * 255 / 63),
                            (unsigned char)((v & 0x1F) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", p.c_str());
}

int main() {
  lv_init();
  static lv_disp_draw_buf_t db;
  lv_disp_draw_buf_init(&db, buf, nullptr, lcd::W * 100);
  static lv_disp_drv_t dd;
  lv_disp_drv_init(&dd);
  dd.hor_res = lcd::W; dd.ver_res = lcd::H; dd.flush_cb = flush; dd.draw_buf = &db;
  lv_disp_drv_register(&dd);
  static lv_indev_drv_t id;
  lv_indev_drv_init(&id);
  id.type = LV_INDEV_TYPE_POINTER; id.read_cb = read;
  lv_indev_drv_register(&id);

  ui::show_main();
  run(200);
  save("1_main");

  // Press "3" and capture the check-mark feedback.
  pt = {963, 300}; pressed = true; run(120); save("2_main_tap_feedback"); pressed = false; run(1300);
  save("3_main_after_feedback");

  ui::show_setup_first_time();
  run(600);
  save("4_setup_wifi_list");
  tap(400, 130);  // first network
  run(200);
  save("5_setup_wifi_password");

  ui::show_maintenance();
  run(200);
  save("6_maintenance");
  return 0;
}

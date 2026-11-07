#include "storage.h"

#include <LittleFS.h>
#include <SD_MMC.h>

#include "board.h"
#include "config.h"

namespace storage {
namespace {
bool sd_ok = false;
bool lfs_ok = false;
SemaphoreHandle_t mtx = xSemaphoreCreateMutex();

struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};

bool append_line(const char* path, const char* header, const char* line) {
  if (!sd_ok && !lfs_ok) return false;
  Lock l;
  bool exists = fs().exists(path);
  File f = fs().open(path, FILE_APPEND);
  if (!f) return false;
  if (!exists) f.print(header);
  size_t n = f.print(line);
  f.close();
  return n > 0;
}

void read_lines(const char* path, const std::function<void(char*)>& fn) {
  if (!sd_ok && !lfs_ok) return;
  Lock l;
  File f = fs().open(path, FILE_READ);
  if (!f) return;
  char buf[96];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', buf, sizeof buf - 1);
    buf[n] = 0;
    if (n && isdigit((unsigned char)buf[0])) fn(buf);  // skips the header
  }
  f.close();
}
}  // namespace

bool begin() {
#ifndef BOARD_TDISPLAY_S3  // the test rig has no SD slot; its pins are the LCD bus
  board::sd_cs_high();
  delay(10);
  if (SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0) &&
      SD_MMC.begin("/sdcard", /*mode1bit=*/true, /*format_if_mount_failed=*/false)) {
    sd_ok = true;
    log_i("Logging to microSD");
    return true;
  }
#endif
  lfs_ok = LittleFS.begin(/*formatOnFail=*/true);
  log_w("No SD card; logging to internal flash: %d", lfs_ok);
  return lfs_ok;
}

bool on_sd() { return sd_ok; }
fs::FS& fs() { return sd_ok ? (fs::FS&)SD_MMC : (fs::FS&)LittleFS; }
const char* medium() { return sd_ok ? "microSD" : (lfs_ok ? "internal flash" : "none"); }

uint64_t total_bytes() { return sd_ok ? SD_MMC.totalBytes() : (lfs_ok ? LittleFS.totalBytes() : 0); }
uint64_t used_bytes() { return sd_ok ? SD_MMC.usedBytes() : (lfs_ok ? LittleFS.usedBytes() : 0); }

bool append_pain(int64_t t, int score) {
  char line[40];
  snprintf(line, sizeof line, "%lld,%d\n", (long long)t, score);
  return append_line("/pain.csv", "unix_utc,score\n", line);
}

bool append_pressure(int64_t t, float st, float sea, float tc, float hum) {
  char line[80];
  snprintf(line, sizeof line, "%lld,%.2f,%.2f,%.1f,%.1f\n", (long long)t, st, sea, tc, hum);
  return append_line("/pressure.csv", "unix_utc,station_hpa,sealevel_hpa,temp_c,humidity_pct\n", line);
}

void read_pain(const std::function<void(int64_t, int)>& fn) {
  read_lines("/pain.csv", [&](char* s) {
    char* c = strchr(s, ',');
    if (!c) return;
    int64_t t = atoll(s);
    int v = atoi(c + 1);
    if (t > 1600000000 && v >= 1 && v <= 5) fn(t, v);
  });
}

void read_pressure(const std::function<void(int64_t, float)>& fn) {
  read_lines("/pressure.csv", [&](char* s) {
    char* c1 = strchr(s, ',');
    if (!c1) return;
    char* c2 = strchr(c1 + 1, ',');
    if (!c2) return;
    int64_t t = atoll(s);
    float sea = atof(c2 + 1);
    if (t > 1600000000 && sea > 850 && sea < 1100) fn(t, sea);
  });
}

}  // namespace storage

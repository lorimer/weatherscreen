// Host stub of the ESP32 WiFi API (just enough for the setup screens).
#pragma once
#include "Arduino.h"
typedef void* TaskHandle_t;
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, TaskHandle_t*, int) { return 1; }
inline void vTaskDelete(void*) {}
enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4 };
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1 };
#define WIFI_SCAN_RUNNING (-1)
struct WiFiStub {
  int scanComplete() { return 5; }
  String SSID(int i) { const char* n[] = {"HomeNetwork", "Neighbors 5G", "xfinitywifi", "DIRECT-roku", "HomeNetwork"}; return n[i]; }
  int RSSI(int i) { int r[] = {-48, -66, -75, -80, -52}; return r[i]; }
  void scanDelete() {}
  int scanNetworks(bool) { return -1; }
  wifi_mode_t getMode() { return WIFI_STA; }
  void mode(wifi_mode_t) {}
  void begin(const char*, const char*) {}
  wl_status_t status() { return WL_IDLE_STATUS; }
  void disconnect() {}
};
extern WiFiStub WiFi;
struct EspStub { void restart() {} };
extern EspStub ESP;

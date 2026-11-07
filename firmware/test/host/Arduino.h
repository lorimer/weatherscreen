// Minimal Arduino shim so the UI code can be rendered on a PC (test/host only).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <algorithm>
#include <type_traits>
using std::isnan;
extern "C" uint32_t millis();
template <class A, class B> typename std::common_type<A, B>::type min(A a, B b) { return a < b ? a : b; }
template <class A, class B> typename std::common_type<A, B>::type max(A a, B b) { return a > b ? a : b; }
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define log_i(...) do{}while(0)
#define log_w(...) do{}while(0)
#define log_e(...) do{}while(0)
#define FW_VERSION "1.0.0"
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(long long v) : s(std::to_string(v)) {}
  String(float v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s = b; }
  String(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s = b; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  bool isEmpty() const { return s.empty(); }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  void trim() {}
};
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
class Print { public: virtual size_t write(uint8_t) = 0; virtual ~Print() {} };

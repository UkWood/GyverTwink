// Заглушки Arduino / ESP8266 / FastLED, чтобы собрать реальный код прошивки (*.ino) на ПК.
// Это только то, что нужно parsing.ino, switcher.ino, power.ino и effects.ino.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <vector>

typedef uint8_t byte;
typedef bool boolean;

// ---- время и случайные числа ----
inline uint32_t& fakeMillis() {
  static uint32_t t = 100000;
  return t;
}
inline uint32_t millis() { return fakeMillis(); }
inline void delay(uint32_t ms) { fakeMillis() += ms; }   // delay() двигает время, как на железе
inline std::mt19937& fakeRng() {
  static std::mt19937 r(1);
  return r;
}
inline long random(long a, long b) { return a + (long)(fakeRng()() % (unsigned long)(b - a)); }

#define constrain(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
template <typename A, typename B> inline auto min(A a, B b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename A, typename B> inline auto max(A a, B b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// ---- сеть ----
struct IPAddress {
  uint8_t o[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : o{a, b, c, d} {}
  uint8_t operator[](int i) const { return o[i]; }
};

// входящие пакеты кладутся в in, всё отправленное попадает в out
struct WiFiUDP {
  std::deque<std::vector<uint8_t>> in;
  std::vector<std::vector<uint8_t>> out;
  std::vector<uint8_t> cur;

  int parsePacket() { return in.empty() ? 0 : (int)in.front().size(); }
  int read(uint8_t* buf, size_t len) {
    std::vector<uint8_t> p = in.front();
    in.pop_front();
    size_t n = std::min(len, p.size());   // остаток пакета отбрасывается, как в настоящем UDP
    memcpy(buf, p.data(), n);
    return (int)n;
  }
  IPAddress remoteIP() { return IPAddress(192, 168, 1, 10); }
  uint16_t remotePort() { return 4000; }
  int beginPacket(IPAddress, uint16_t) {
    cur.clear();
    return 1;
  }
  size_t write(const char* s) {
    cur.insert(cur.end(), s, s + strlen(s));
    return strlen(s);
  }
  size_t write(const uint8_t* d, size_t n) {
    cur.insert(cur.end(), d, d + n);
    return n;
  }
  int endPacket() {
    out.push_back(cur);
    return 1;
  }
};

// ---- EEPROM: считаем обращения ----
struct EEManager {
  int updates = 0, nows = 0;
  template <typename T> EEManager(T&) {}
  void update() { updates++; }
  void updateNow() { nows++; }
  bool tick() { return false; }
};

// ---- FastLED ----
struct CRGB {
  uint8_t r = 0, g = 0, b = 0;
  enum HTMLColorCode : uint32_t { Black = 0, White = 0xFFFFFF, Red = 0xFF0000 };
  CRGB() {}
  CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  CRGB(HTMLColorCode c) : r(c >> 16), g(c >> 8), b(c) {}
  bool operator==(const CRGB& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct CLEDController {
  CRGB* data = nullptr;
  int count = 0;
  std::vector<int> shown;   // яркость каждого вызова showLeds
  void setLeds(CRGB* l, int n) {
    data = l;
    count = n;
  }
  void clearLedData() {
    for (int i = 0; i < count; i++) data[i] = CRGB();
  }
  void showLeds(uint8_t bri = 255) { shown.push_back(bri); }
};

struct CRGBPalette16 {};
enum TBlendType { LINEARBLEND };
inline CRGB ColorFromPalette(const CRGBPalette16&, uint8_t idx, uint8_t, TBlendType) { return CRGB(idx, idx, idx); }
inline CRGB blend(const CRGB& a, const CRGB& b, uint8_t amt) {
  return CRGB(a.r + ((b.r - a.r) * amt) / 256, a.g + ((b.g - a.g) * amt) / 256, a.b + ((b.b - a.b) * amt) / 256);
}
inline uint8_t inoise8(uint16_t x, uint16_t y, uint16_t z) { return (uint8_t)(x * 31 + y * 17 + z * 7); }

// Модель мощности из FastLED 3.4.0 (power_mgt.cpp): R 80 мВт, G 55 мВт, B 75 мВт, 5 мВт на выключенный светодиод
inline uint32_t calculate_unscaled_power_mW(const CRGB* leds, uint16_t n) {
  uint32_t r = 0, g = 0, b = 0;
  for (uint16_t i = 0; i < n; i++) {
    r += leds[i].r;
    g += leds[i].g;
    b += leds[i].b;
  }
  r = (r * 80) >> 8;
  g = (g * 55) >> 8;
  b = (b * 75) >> 8;
  return r + g + b + 5u * n;
}
inline uint8_t calculate_max_brightness_for_power_vmA(const CRGB* leds, uint16_t n, uint8_t target, uint32_t volts, uint32_t mA) {
  uint32_t max_mW = volts * mA;
  uint32_t total = calculate_unscaled_power_mW(leds, n);
  uint32_t requested = (total * target) / 256;
  uint8_t res = target;
  if (requested > max_mW) res = (uint32_t)target * max_mW / requested;
  return res;
}

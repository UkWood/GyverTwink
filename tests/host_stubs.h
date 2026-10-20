// Заглушки Arduino / ESP8266 / FastLED, чтобы собрать реальный код прошивки (*.ino) на ПК.
// Это только то, что нужно parsing.ino, switcher.ino, power.ino и effects.ino.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <vector>
#include <arpa/inet.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

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

#ifdef HOST_SOCKET_UDP
// Вариант для стенда протокола: настоящий UDP-сокет на 127.0.0.1, ответы уходят отправителю
struct WiFiUDP {
  int fd = -1;
  sockaddr_in peer{};
  std::vector<uint8_t> pkt, cur;

  bool begin(uint16_t port) {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (fd < 0 || bind(fd, (sockaddr*)&a, sizeof(a)) < 0) return false;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
  }
  int parsePacket() {
    uint8_t buf[2048];
    socklen_t l = sizeof(peer);
    int n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr*)&peer, &l);
    if (n <= 0) return 0;
    pkt.assign(buf, buf + n);
    return n;
  }
  int read(uint8_t* b, size_t len) {
    size_t n = std::min(len, pkt.size());
    memcpy(b, pkt.data(), n);
    return (int)n;
  }
  IPAddress remoteIP() { return IPAddress(127, 0, 0, 1); }
  uint16_t remotePort() { return ntohs(peer.sin_port); }
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
  int endPacket() { return sendto(fd, cur.data(), cur.size(), 0, (sockaddr*)&peer, sizeof(peer)) >= 0; }
};
#else
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
#endif


// ---- Arduino String (минимум) ----
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  long toInt() const { return atol(c_str()); }
};
typedef const char* PGM_P;
#define PROGMEM
#define FW_VERSION_HOST "host"

// ---- ESP ----
struct EspClass {
  uint32_t getFreeHeap() { return 40000; }
};
inline EspClass ESP;

// ---- mDNS ----
struct MDNSResponder {
  std::string name;
  bool begin(const char* n) {
    name = n;
    return true;
  }
  void addService(const char*, const char*, uint16_t) {}
  void update() {}
};
inline MDNSResponder MDNS;

// ---- ESP8266WebServer ----
// Один класс для двух режимов: test() вызывает обработчик без сети (юнит-тесты),
// begin(port)+handleClient() принимает настоящие HTTP-запросы (хост-сервер для e2e-тестов).
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String filename, name, type;
  size_t totalSize = 0, currentSize = 0;
  uint8_t buf[1460];
};

class ESP8266WebServer {
public:
  typedef std::function<void()> THandlerFunction;
  struct Route {
    std::string uri;
    HTTPMethod method;
    THandlerFunction handler, upload;
  };
  struct Response {
    int code = 0;
    std::string type, body;
    std::map<std::string, std::string> headers;
  };

  ESP8266WebServer(int port = 80) : _port(port) {}

  void on(const char* uri, THandlerFunction h) { _routes.push_back({uri, HTTP_ANY, h, nullptr}); }
  void on(const char* uri, HTTPMethod m, THandlerFunction h) { _routes.push_back({uri, m, h, nullptr}); }
  void on(const char* uri, HTTPMethod m, THandlerFunction h, THandlerFunction up) { _routes.push_back({uri, m, h, up}); }
  void onNotFound(THandlerFunction h) { _notFound = h; }
  void collectHeaders(const char* keys[], size_t n) {
    _collect.clear();
    for (size_t i = 0; i < n; i++) _collect.push_back(lower(keys[i]));
  }

  String arg(const char* name) const {
    auto it = _args.find(name);
    return it == _args.end() ? String() : String(it->second);
  }
  bool hasArg(const char* name) const { return _args.count(name) > 0; }
  String header(const char* name) const {
    auto it = _headers.find(lower(name));
    return it == _headers.end() ? String() : String(it->second);
  }
  String uri() const { return _uri; }
  HTTPMethod method() const { return _method; }
  HTTPUpload& upload() { return _upload; }

  void sendHeader(const String& name, const String& value, bool = false) { _resp.headers[name] = value; }
  void send(int code, const char* type, const String& content) {
    _resp.code = code;
    _resp.type = type;
    _resp.body = content;
  }
  void send(int code, const char* type, const char* content) { send(code, type, String(content)); }
  void send_P(int code, PGM_P type, PGM_P content, size_t len) {
    _resp.code = code;
    _resp.type = type;
    _resp.body.assign(content, len);
  }

  // ---- режим без сети: выполнить запрос и вернуть ответ ----
  Response test(HTTPMethod m, const std::string& uri, const std::map<std::string, std::string>& args = {},
                const std::map<std::string, std::string>& headers = {}, const std::string& body = "") {
    _method = m;
    _uri = uri;
    _args = args;
    if (!body.empty()) _args["plain"] = body;
    _headers.clear();
    for (auto& h : headers) _headers[lower(h.first)] = h.second;
    return dispatch();
  }

  // ---- режим с сетью ----
  bool begin() { return begin(_port); }
  bool begin(int port) {
    if (port <= 0) return true;   // порт 0: без сети, запросы выполняет только test()
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    if (bind(_fd, (sockaddr*)&a, sizeof(a)) < 0 || listen(_fd, 16) < 0) {
      close(_fd);
      _fd = -1;
      return false;
    }
    fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
  }
  void handleClient() {
    if (_fd < 0) return;
    int c = accept(_fd, nullptr, nullptr);
    if (c < 0) return;
    serve(c);
    close(c);
  }

private:
  int _port, _fd = -1;
  std::vector<Route> _routes;
  THandlerFunction _notFound;
  std::vector<std::string> _collect;
  std::map<std::string, std::string> _args, _headers;
  std::string _uri;
  HTTPMethod _method = HTTP_GET;
  Response _resp;
  HTTPUpload _upload;

  static std::string lower(std::string s) {
    for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
  }
  Response dispatch() {
    _resp = Response();
    THandlerFunction h;
    for (auto& r : _routes) {
      if (r.uri == _uri && (r.method == HTTP_ANY || r.method == _method)) {
        h = r.handler;
        break;
      }
    }
    if (h) h();
    else if (_notFound) _notFound();
    else send(404, "text/plain", "Not found");
    return _resp;
  }
  static std::string urldecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
      if (s[i] == '+') o += ' ';
      else if (s[i] == '%' && i + 2 < s.size()) {
        o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
        i += 2;
      } else o += s[i];
    }
    return o;
  }
  void parseArgs(const std::string& q) {
    size_t p = 0;
    while (p < q.size()) {
      size_t e = q.find('&', p);
      if (e == std::string::npos) e = q.size();
      std::string kv = q.substr(p, e - p);
      size_t eq = kv.find('=');
      if (eq != std::string::npos) _args[urldecode(kv.substr(0, eq))] = urldecode(kv.substr(eq + 1));
      else if (!kv.empty()) _args[urldecode(kv)] = "";
      p = e + 1;
    }
  }
  void serve(int c) {
    // читаем запрос целиком (клиенты тестов локальные и быстрые)
    std::string req;
    char buf[4096];
    timeval tv{2, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    size_t hdrEnd = std::string::npos, need = 0;
    while (true) {
      ssize_t n = recv(c, buf, sizeof(buf), 0);
      if (n <= 0) break;
      req.append(buf, n);
      if (hdrEnd == std::string::npos) {
        hdrEnd = req.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) {
          std::string hl = lower(req.substr(0, hdrEnd));
          size_t cl = hl.find("content-length:");
          need = hdrEnd + 4 + (cl == std::string::npos ? 0 : (size_t)atol(hl.c_str() + cl + 15));
        }
      }
      if (hdrEnd != std::string::npos && req.size() >= need) break;
    }
    if (hdrEnd == std::string::npos) return;
    size_t l1 = req.find("\r\n");
    std::string line = req.substr(0, l1);
    size_t s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
    std::string m = line.substr(0, s1), target = line.substr(s1 + 1, s2 - s1 - 1);
    _method = m == "POST" ? HTTP_POST : m == "GET" ? HTTP_GET : m == "PUT" ? HTTP_PUT : m == "OPTIONS" ? HTTP_OPTIONS : HTTP_ANY;
    _args.clear();
    _headers.clear();
    size_t q = target.find('?');
    _uri = target.substr(0, q);
    if (q != std::string::npos) parseArgs(target.substr(q + 1));
    size_t p = l1 + 2;
    while (p < hdrEnd) {
      size_t e = req.find("\r\n", p);
      std::string h = req.substr(p, e - p);
      size_t col = h.find(':');
      if (col != std::string::npos) {
        std::string k = lower(h.substr(0, col)), v = h.substr(col + 1);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        if (std::find(_collect.begin(), _collect.end(), k) != _collect.end()) _headers[k] = v;
      }
      p = e + 2;
    }
    std::string body = req.substr(hdrEnd + 4);
    if (!body.empty()) {
      // как в настоящей библиотеке: urlencoded разбирается в аргументы, остальное - в "plain"
      std::string all = req.substr(0, hdrEnd);
      if (lower(all).find("application/x-www-form-urlencoded") != std::string::npos) parseArgs(body);
      else _args["plain"] = body;
    }
    Response r = dispatch();
    std::string out = "HTTP/1.1 " + std::to_string(r.code) + " X\r\nContent-Type: " + r.type + "\r\nContent-Length: " +
                      std::to_string(r.body.size()) + "\r\nConnection: close\r\n";
    for (auto& h : r.headers) out += h.first + ": " + h.second + "\r\n";
    out += "\r\n" + r.body;
    size_t sent = 0;
    while (sent < out.size()) {
      ssize_t n = ::send(c, out.data() + sent, out.size() - sent, 0);
      if (n <= 0) break;
      sent += n;
    }
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

// Модель мощности из FastLED (power_mgt.cpp, константы те же в 3.4.0 и 3.5.0): R 80 мВт, G 55 мВт, B 75 мВт, 5 мВт на выключенный светодиод
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

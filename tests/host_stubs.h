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
#include <memory>
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
inline long random(long b) { return random(0, b); }

#define constrain(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))
// возврат по значению: у decltype(a < b ? a : b) для одинаковых типов получается ссылка на параметр-копию (висячая ссылка)
template <typename A, typename B> inline std::common_type_t<A, B> min(A a, B b) { return a < b ? a : b; }
template <typename A, typename B> inline std::common_type_t<A, B> max(A a, B b) { return a > b ? a : b; }

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
  void reserve(size_t n) { std::string::reserve(n); }
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  long toInt() const { return atol(c_str()); }
};
typedef const char* PGM_P;
#define PROGMEM
#define FW_VERSION_HOST "host"

// ---- LittleFS: файлы в памяти; если задана переменная GT_FS_DIR - ещё и на диске (чтобы пережить перезапуск хост-сервера) ----
class File {
public:
  File() {}
  File(std::string* data, bool write, std::function<void()> commit) : _d(data), _w(write), _commit(commit) {}
  operator bool() const { return _d != nullptr; }
  size_t size() const { return _d ? _d->size() : 0; }
  size_t read(uint8_t* b, size_t n) {
    if (!_d || _w) return 0;
    size_t k = std::min(n, _d->size() - _pos);
    memcpy(b, _d->data() + _pos, k);
    _pos += k;
    return k;
  }
  size_t write(const uint8_t* b, size_t n) {
    if (!_d || !_w || *_fail) return 0;
    _d->append((const char*)b, n);
    return n;
  }
  void close() {
    if (_d && _w && _commit) _commit();
    _d = nullptr;
  }
  bool* _fail = &_never;
private:
  std::string* _d = nullptr;
  bool _w = false;
  size_t _pos = 0;
  std::function<void()> _commit;
  bool _never = false;
};

struct LittleFSClass {
  std::map<std::string, std::string> files;
  bool failWrites = false;   // тест: ошибка записи
  bool begun = false;
  std::string dir() const { return getenv("GT_FS_DIR") ? getenv("GT_FS_DIR") : ""; }
  static std::string disk(const std::string& d, const std::string& path) {
    std::string n = path;
    for (auto& c : n) if (c == '/') c = '_';
    return d + "/" + n;
  }
  bool begin() {
    begun = true;
    std::string d = dir();
    if (!d.empty()) {
      for (const char* p : {"/scenes.bin"}) {
        FILE* f = fopen(disk(d, p).c_str(), "rb");
        if (!f) continue;
        std::string s;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        fclose(f);
        files[p] = s;
      }
    }
    return true;
  }
  void sync(const std::string& path) {
    std::string d = dir();
    if (d.empty()) return;
    std::string n = disk(d, path);
    if (!files.count(path)) {
      ::remove(n.c_str());   // файл на диске (не метод класса)
      return;
    }
    FILE* f = fopen(n.c_str(), "wb");
    if (!f) return;
    fwrite(files[path].data(), 1, files[path].size(), f);
    fclose(f);
  }
  bool exists(const char* path) { return files.count(path) > 0; }
  File open(const char* path, const char* mode) {
    std::string p = path;
    if (mode[0] == 'r') {
      if (!files.count(p)) return File();
      return File(&files[p], false, nullptr);
    }
    // запись идёт во временную строку, в файл попадает при close() (как атомарная запись)
    auto buf = std::make_shared<std::string>();
    File f(buf.get(), true, [this, p, buf]() {
      files[p] = *buf;
      sync(p);
    });
    _keep.push_back(buf);
    f._fail = &failWrites;
    return f;
  }
  bool remove(const char* path) {
    bool had = files.erase(path) > 0;
    sync(path);
    return had;
  }
  bool rename(const char* from, const char* to) {
    if (!files.count(from)) return false;
    files[to] = files[from];
    files.erase(from);
    sync(from);
    sync(to);
    return true;
  }
private:
  std::vector<std::shared_ptr<std::string>> _keep;
};
inline LittleFSClass LittleFS;

// ---- ESP ----
struct EspClass {
  uint32_t getFreeHeap() { return 40000; }
  uint32_t freeSketchSpace = 1019904;   // как у d1_mini с разделом под OTA
  bool restarted = false;
  uint32_t getFreeSketchSpace() { return freeSketchSpace; }
  void restart() { restarted = true; }
};
inline EspClass ESP;

// ---- Update (запись прошивки): принимает образ в память, проверяет маркерный байт 0xE9, как настоящая библиотека ----
struct UpdateClass {
  std::vector<uint8_t> data;
  bool began = false, ended = false, error = false;
  uint32_t space = 0;
  bool failBegin = false;   // тест: не хватает места
  int failWriteAfter = -1;  // тест: сбой записи после стольких байт (-1 - нет)
  bool begin(uint32_t maxSpace) {
    data.clear();
    began = true;
    ended = false;
    error = false;
    space = maxSpace;
    if (failBegin || maxSpace == 0) error = true;
    return !error;
  }
  size_t write(const uint8_t* b, size_t n) {
    if (!began || error) return 0;
    if (data.empty() && n && b[0] != 0xE9) {
      error = true;   // UPDATE_ERROR_MAGIC_BYTE
      return 0;
    }
    if (data.size() + n > space) {
      error = true;   // UPDATE_ERROR_SPACE
      return 0;
    }
    if (failWriteAfter >= 0 && (int)(data.size() + n) > failWriteAfter) {
      error = true;
      return 0;
    }
    data.insert(data.end(), b, b + n);
    return n;
  }
  bool end(bool evenIfRemaining = false) {
    // размер образа заранее неизвестен (begin(maxSpace)), поэтому без evenIfRemaining недозаписанный образ - ошибка
    bool ok = began && !error && !data.empty() && evenIfRemaining;
    if (began && !ended) ended = true;
    if (!ok) error = true;
    began = false;
    return ok;
  }
  bool hasError() { return error; }
};
inline UpdateClass Update;

// ---- кнопка: сценарий нажатий задаётся тестом ----
struct EncButtonStub {
  int clicks = 0;   // сколько нажатий "выдать" в ближайшем tick
  bool stepF = false, relF = false;
  void tick() {}
  bool hasClicks(int n) {
    if (clicks == n) {
      clicks = 0;
      return true;
    }
    return false;
  }
  bool step() {
    bool r = stepF;
    stepF = false;
    return r;
  }
  bool releaseStep() {
    bool r = relF;
    relF = false;
    return r;
  }
};

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

// адрес «клиента» для юнит-тестов защиты (в режиме с сетью - 127.0.0.1)
inline uint32_t& fakeClientIp() { static uint32_t ip = 0x0A00000A; return ip; }
struct WebClientStub {
  struct Addr {
    uint32_t v;
    operator uint32_t() const { return v; }
  };
  Addr remoteIP() const { return Addr{fakeClientIp()}; }
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
  WebClientStub& client() { return _client; }

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
    for (auto& h : headers) {   // как в настоящей библиотеке: доступны только заголовки, заказанные через collectHeaders()
      std::string k = lower(h.first);
      if (std::find(_collect.begin(), _collect.end(), k) != _collect.end()) _headers[k] = h.second;
    }
    return dispatch();
  }

  // ---- загрузка файла без сети: куски вызывают обработчик загрузки, потом основной (как multipart в настоящей библиотеке) ----
  Response testUpload(const std::string& uri, const std::map<std::string, std::string>& headers, const std::string& filename,
                      const std::vector<std::string>& chunks, bool abortAfterFirst = false) {
    _method = HTTP_POST;
    _uri = uri;
    _args.clear();
    _headers.clear();
    for (auto& h : headers) {
      std::string k = lower(h.first);
      if (std::find(_collect.begin(), _collect.end(), k) != _collect.end()) _headers[k] = h.second;
    }
    return runUpload(filename, chunks, abortAfterFirst);
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
  WebClientStub _client;

  static std::string lower(std::string s) {
    for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
  }
  Response runUpload(const std::string& filename, const std::vector<std::string>& chunks, bool abortAfterFirst) {
    _resp = Response();
    Route* r = nullptr;
    for (auto& x : _routes) {
      if (x.uri == _uri && x.upload && (x.method == HTTP_ANY || x.method == _method)) r = &x;
    }
    if (!r) {
      send(404, "text/plain", "Not found");
      return _resp;
    }
    _upload = HTTPUpload();
    _upload.filename = filename;
    _upload.status = UPLOAD_FILE_START;
    r->upload();
    size_t total = 0;
    for (size_t k = 0; k < chunks.size(); k++) {
      for (size_t off = 0; off < chunks[k].size() || (off == 0 && chunks[k].empty()); off += sizeof(_upload.buf)) {
        size_t n = std::min(sizeof(_upload.buf), chunks[k].size() - off);
        memcpy(_upload.buf, chunks[k].data() + off, n);
        _upload.currentSize = n;
        total += n;
        _upload.totalSize = total;
        _upload.status = UPLOAD_FILE_WRITE;
        r->upload();
        if (chunks[k].empty()) break;
      }
      if (abortAfterFirst && k == 0) {
        _upload.status = UPLOAD_FILE_ABORTED;   // соединение оборвалось: основной обработчик не вызывается, ответа нет
        r->upload();
        return _resp;
      }
    }
    _upload.status = UPLOAD_FILE_END;
    _upload.totalSize = total;
    r->upload();
    r->handler();
    return _resp;
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
    std::string allHdr = lower(req.substr(0, hdrEnd));
    size_t mp = allHdr.find("multipart/form-data");
    if (mp != std::string::npos) {
      // граница и файл первой части
      size_t bpos = allHdr.find("boundary=", mp);
      size_t bend = allHdr.find("\r\n", bpos);
      std::string boundary = "--" + req.substr(hdrEnd - (allHdr.size() - bpos) + 9, bend == std::string::npos ? std::string::npos : bend - bpos - 9);
      size_t partStart = body.find(boundary);
      size_t dataStart = body.find("\r\n\r\n", partStart);
      size_t dataEnd = body.find("\r\n" + boundary, dataStart);
      std::string fname;
      size_t fn = body.find("filename=\"", partStart);
      if (fn != std::string::npos && fn < dataStart) fname = body.substr(fn + 10, body.find('"', fn + 10) - fn - 10);
      if (partStart == std::string::npos || dataStart == std::string::npos || dataEnd == std::string::npos) {
        send(400, "text/plain", "bad multipart");
        _resp.code = 400;
      } else {
        std::string file = body.substr(dataStart + 4, dataEnd - dataStart - 4);
        std::vector<std::string> chunks;
        for (size_t off = 0; off < file.size(); off += 1460) chunks.push_back(file.substr(off, 1460));
        if (chunks.empty()) chunks.push_back("");
        runUpload(fname, chunks, false);
      }
      Response r = _resp;
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
      return;
    }
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
  enum HTMLColorCode : uint32_t { Black = 0, White = 0xFFFFFF, Red = 0xFF0000, Cyan = 0x00FFFF, Blue = 0x0000FF, Green = 0x008000 };
  CRGB() {}
  CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  CRGB(HTMLColorCode c) : r(c >> 16), g(c >> 8), b(c) {}
  bool operator==(const CRGB& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct CLEDController {
  CRGB* data = nullptr;
  int count = 0;
  std::vector<int> shown;   // яркость каждого вызова showLeds
  // запись кадров (для тестов расписания): когда, с какой яркостью и какие светодиоды горели
  struct Frame {
    uint32_t t;
    int bri;
    std::vector<int> lit;
  };
  bool record = false;
  std::vector<Frame> frames;
  void setLeds(CRGB* l, int n) {
    data = l;
    count = n;
  }
  void clearLedData() {
    for (int i = 0; i < count; i++) data[i] = CRGB();
  }
  void showColor(const CRGB&, uint8_t bri) { shown.push_back(bri); }
  CRGB correction = CRGB(0xFF, 0xB0, 0xF0);   // последняя поправка цвета (setCorrection)
  void setCorrection(const CRGB& c) { correction = c; }
  int size() const { return count; }
  CRGB sent[4];   // первые светодиоды в том виде, в каком они ушли на ленту в последнем showLeds (после перестановки порядка цветов)
  void showLeds(uint8_t bri = 255) {
    shown.push_back(bri);
    for (int i = 0; i < 4 && i < count; i++) sent[i] = data[i];
    if (!record) return;
    Frame f{fakeMillis(), bri, {}};
    for (int i = 0; i < count; i++) {
      if (data[i].r || data[i].g || data[i].b) f.lit.push_back(i);
    }
    frames.push_back(f);
  }
};

struct CRGBPalette16 {};
enum TBlendType { LINEARBLEND };
inline CRGB ColorFromPalette(const CRGBPalette16&, uint8_t idx, uint8_t, TBlendType) { return CRGB(idx, idx, idx); }
inline CRGB blend(const CRGB& a, const CRGB& b, uint8_t amt) {
  return CRGB(a.r + ((b.r - a.r) * amt) / 256, a.g + ((b.g - a.g) * amt) / 256, a.b + ((b.b - a.b) * amt) / 256);
}
// хук для тестов: видит каждый вызов шума (какие координаты запрашивает эффект)
inline std::function<void(uint16_t, uint16_t, uint16_t)>& noiseHook() {
  static std::function<void(uint16_t, uint16_t, uint16_t)> h;
  return h;
}
inline uint8_t inoise8(uint16_t x, uint16_t y, uint16_t z) {
  if (noiseHook()) noiseHook()(x, y, z);
  return (uint8_t)(x * 31 + y * 17 + z * 7);
}

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

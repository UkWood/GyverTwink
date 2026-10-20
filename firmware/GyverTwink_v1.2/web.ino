// Веб-интерфейс: страница вшита в прошивку (web_page.h, gzip), управление - JSON API (docs/WEB_API.md).
// Использует встроенный ESP8266WebServer: без внешних библиотек, на ядрах 2.7.x и 3.x.
//
// Защита: все изменяющие запросы (POST) обязаны нести заголовок X-Requested-With: GyverTwink.
// Чужая страница в браузере не может отправить такой запрос (нужен CORS-предзапрос, устройство на него не отвечает),
// поэтому открытый в браузере сайт не сможет управлять гирляндой в вашей сети.

#define WEB_HDR "X-Requested-With"
#define WEB_HDR_VAL "GyverTwink"
#define MDNS_NAME "gyvertwink"

static void webJson(int code, const char* json) {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(code, "application/json", json);
}

static void webError(int code, const char* what) {
  char b[80];
  snprintf(b, sizeof(b), "{\"error\":\"%s\"}", what);
  webJson(code, b);
}

// только запросы нашей страницы
static bool webAllowed() {
  if (webServer.header(WEB_HDR) == WEB_HDR_VAL) return true;
  webError(403, "forbidden");
  return false;
}

// аргумент-число: 1 - принят, 0 - его нет в запросе, -1 - не число или вне диапазона
static int webArgInt(const char* name, long lo, long hi, long& v) {
  if (!webServer.hasArg(name)) return 0;
  String s = webServer.arg(name);
  if (s.length() == 0 || s.length() > 6) return -1;
  for (unsigned i = 0; i < s.length(); i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
  }
  v = s.toInt();
  return (v < lo || v > hi) ? -1 : 1;
}

static void webSendState() {
  char b[320];
  snprintf(b, sizeof(b),
           "{\"fw\":\"%s\",\"ledAm\":%u,\"ledMax\":%d,\"power\":%d,\"bright\":%u,\"auto\":%d,\"rnd\":%d,\"period\":%u,"
           "\"offOn\":%d,\"offMin\":%u,\"maxMa\":%u,\"cur\":%u,\"count\":%d,\"heap\":%u}",
           FW_VERSION, (unsigned)cfg.ledAm, LED_MAX, cfg.power ? 1 : 0, (unsigned)cfg.bright, cfg.autoCh ? 1 : 0,
           cfg.rndCh ? 1 : 0, (unsigned)cfg.prdCh, cfg.turnOff ? 1 : 0, (unsigned)cfg.offTmr, (unsigned)pwr.maxMa,
           (unsigned)curEff, ACTIVE_PALETTES * 2, (unsigned)ESP.getFreeHeap());
  webJson(200, b);
}

static void webSendEffect(byte n) {
  char b[100];
  snprintf(b, sizeof(b), "{\"n\":%u,\"fav\":%d,\"scale\":%u,\"speed\":%u}", (unsigned)n, effs[n].fav ? 1 : 0,
           (unsigned)effs[n].scale, (unsigned)effs[n].speed);
  webJson(200, b);
}

static void handleRoot() {
  webServer.sendHeader("Content-Encoding", "gzip");
  webServer.sendHeader("Cache-Control", "no-cache");
  webServer.send_P(200, "text/html", (PGM_P)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

static void handleState() {
  webSendState();
}

// POST /api/set: любые из power, bright, auto, rnd, period, offOn, offMin, maxMa, ledAm.
// Сначала проверяются все значения, и только потом применяются: неверный запрос ничего не меняет.
static void handleSet() {
  if (!webAllowed()) return;
  struct Item {
    const char* name;
    long lo, hi;
  };
  static const Item items[] = {
    {"ledAm", 1, LED_MAX}, {"power", 0, 1},    {"bright", 0, 255},  {"auto", 0, 1},   {"rnd", 0, 1},
    {"period", 1, 255},    {"offOn", 0, 1},    {"offMin", 1, 250},  {"maxMa", 0, 25500},
  };
  const int count = sizeof(items) / sizeof(items[0]);
  long val[sizeof(items) / sizeof(items[0])];
  bool has[sizeof(items) / sizeof(items[0])];
  bool any = false;
  for (int i = 0; i < count; i++) {
    int r = webArgInt(items[i].name, items[i].lo, items[i].hi, val[i]);
    if (r < 0) {
      webError(400, items[i].name);
      return;
    }
    has[i] = r > 0;
    any |= has[i];
  }
  if (any) settingsTouch();
  if (has[0]) setLedAmount(val[0]);
  if (has[1]) setPower(val[1]);
  if (has[2]) setBright(val[2]);
  if (has[3]) setAutoSwitch(val[3]);
  if (has[4]) setRandomSwitch(val[4]);
  if (has[5]) setPeriod(val[5]);
  if (has[6]) setOffTimer(val[6]);
  if (has[7]) setOffMinutes(val[7]);
  if (has[8]) setMaxMa(val[8]);
  webSendState();
}

static void handleNext() {
  if (!webAllowed()) return;
  settingsTouch();
  nextEffect();
  webSendState();
}

// эффект n из аргумента; false - ответ об ошибке уже отправлен
static bool webEffectArg(long& n) {
  if (webArgInt("n", 0, ACTIVE_PALETTES * 2 - 1, n) > 0) return true;
  webError(400, "n");
  return false;
}

static void handleEffectGet() {
  long n;
  if (webEffectArg(n)) webSendEffect(n);
}

// POST /api/effect: n и любые из fav, scale, speed; show=1 - показать этот эффект (как выбор в приложении)
static void handleEffectPost() {
  if (!webAllowed()) return;
  long n, fav, scale, speed, show;
  if (!webEffectArg(n)) return;
  int rf = webArgInt("fav", 0, 1, fav);
  int rs = webArgInt("scale", 0, 255, scale);
  int rp = webArgInt("speed", 0, 255, speed);
  int rw = webArgInt("show", 0, 1, show);
  if (rf < 0 || rs < 0 || rp < 0 || rw < 0) {
    webError(400, rf < 0 ? "fav" : rs < 0 ? "scale" : rp < 0 ? "speed" : "show");
    return;
  }
  if (rf > 0) effs[n].fav = fav;
  if (rs > 0) effs[n].scale = scale;
  if (rp > 0) effs[n].speed = speed;
  if (rf > 0 || rs > 0 || rp > 0) EEeff.update();
  if (rw > 0 && show) {
    forceEff = n;
    forceTmr.restart();
  }
  webSendEffect(n);
}

// ---------- сцены ----------
static void webSendScenes() {
  char b[700];
  int n = snprintf(b, sizeof(b), "{\"max\":%d,\"scenes\":[", SCENE_SLOTS);
  bool first = true;
  for (int i = 0; i < SCENE_SLOTS && n < (int)sizeof(b) - 60; i++) {
    if (scenes[i].used != 1) continue;
    n += snprintf(b + n, sizeof(b) - n, "%s{\"i\":%d,\"name\":\"%s\"}", first ? "" : ",", i, scenes[i].name);
    first = false;
  }
  snprintf(b + n, sizeof(b) - n, "]}");
  webJson(200, b);
}

static bool webSlotArg(long& slot) {
  if (webArgInt("slot", 0, SCENE_SLOTS - 1, slot) > 0) return true;
  webError(400, "slot");
  return false;
}

static void handleScenesGet() {
  webSendScenes();
}

// POST /api/scenes/save: slot и name - сохранить текущие настройки
static void handleSceneSave() {
  if (!webAllowed()) return;
  long slot;
  if (!webSlotArg(slot)) return;
  char name[SCENE_NAME_LEN + 1];
  if (!webServer.hasArg("name") || sceneCleanName(webServer.arg("name").c_str(), name) < 0) {
    webError(400, "name");
    return;
  }
  if (!sceneSave(slot, name)) {
    webError(500, "fs");
    return;
  }
  webSendScenes();
}

// POST /api/scenes/load: slot - применить сцену, в ответе новое состояние
static void handleSceneLoad() {
  if (!webAllowed()) return;
  long slot;
  if (!webSlotArg(slot)) return;
  if (!sceneLoad(slot)) {
    webError(404, "empty");
    return;
  }
  webSendState();
}

static void handleSceneDelete() {
  if (!webAllowed()) return;
  long slot;
  if (!webSlotArg(slot)) return;
  if (scenes[slot].used != 1) {
    webError(404, "empty");
    return;
  }
  if (!sceneDelete(slot)) {
    webError(500, "fs");
    return;
  }
  webSendScenes();
}

// ---------- координаты светодиодов (редактор) ----------
static const char HEX_DIGITS[] = "0123456789abcdef";

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// GET /api/xy: {"n":число,"w":..,"h":..,"xy":"hex"} - по 4 символа на светодиод: x, y
static void handleXyGet() {
  String body;
  body.reserve(60 + 4 * cfg.ledAm);
  char head[64];
  snprintf(head, sizeof(head), "{\"n\":%u,\"w\":%u,\"h\":%u,\"xy\":\"", (unsigned)cfg.ledAm, (unsigned)mm.w, (unsigned)mm.h);
  body += head;
  for (int i = 0; i < cfg.ledAm; i++) {
    for (int k = 0; k < 2; k++) {
      char two[3] = {HEX_DIGITS[xy[i][k] >> 4], HEX_DIGITS[xy[i][k] & 15], 0};
      body += two;
    }
  }
  body += "\"}";
  webJson(200, body.c_str());
}

// POST /api/xy?n=<число светодиодов>, тело - hex всех координат. n защищает от гонки: пока страница
// редактировала точки, число светодиодов могли изменить из приложения (409).
static void handleXyPost() {
  if (!webAllowed()) return;
  long n;
  if (webArgInt("n", 1, LED_MAX, n) <= 0) {
    webError(400, "n");
    return;
  }
  if (n != cfg.ledAm) {
    webError(409, "count");
    return;
  }
  String body = webServer.arg("plain");
  if (!webServer.hasArg("plain") || (long)body.length() != 4 * n) {
    webError(400, "xy");
    return;
  }
  for (unsigned i = 0; i < body.length(); i++) {
    if (hexVal(body[i]) < 0) {
      webError(400, "xy");
      return;
    }
  }
  // всё проверено: применяем целиком
  for (long i = 0; i < n; i++) {
    for (int k = 0; k < 2; k++) xy[i][k] = hexVal(body[4 * i + 2 * k]) * 16 + hexVal(body[4 * i + 2 * k + 1]);
  }
  recalcBounds();
  coordsSave();
  char r[40];
  snprintf(r, sizeof(r), "{\"n\":%ld,\"w\":%u,\"h\":%u}", n, (unsigned)mm.w, (unsigned)mm.h);
  webJson(200, r);
}

// POST /api/led: i - зажечь светодиод номер i, off=1 - погасить и вернуть эффекты
static void handleLed() {
  if (!webAllowed()) return;
  long i, off;
  int ri = webArgInt("i", 0, cfg.ledAm - 1, i);
  int ro = webArgInt("off", 0, 1, off);
  // нужно ровно одно из двух: i или off=1
  if (ri < 0 || ro < 0 || (ri > 0 && ro > 0 && off) || (ri == 0 && !(ro > 0 && off))) {
    webError(400, ri < 0 ? "i" : ro < 0 ? "off" : (ri > 0 ? "off" : "i"));
    return;
  }
  if (ri > 0) ledHighlight(i);
  else ledHighlightOff();
  webJson(200, "{}");
}

// ---------- калибровка по видео ----------
static void webSendVcalib() {
  uint32_t e = vcRun ? millis() - vcT0 : 0;
  uint16_t n = vcRun ? vcCount : cfg.ledAm;
  int i = -1;
  if (vcRun && vcFrame >= 3) i = vcFrame - 3;
  char b[300];
  snprintf(b, sizeof(b),
           "{\"run\":%d,\"phase\":\"%s\",\"e\":%lu,\"i\":%d,\"n\":%u,\"delay\":%u,\"step\":%u,"
           "\"dark\":%d,\"flash\":%d,\"gap\":%d,\"end\":%d,\"marker\":%d,\"total\":%lu}",
           vcRun ? 1 : 0, vcRun ? vcPhaseName(e) : "idle", (unsigned long)e, i, (unsigned)n, (unsigned)vcDelay, (unsigned)vcStep,
           VC_DARK_MS, VC_FLASH_MS, VC_GAP_MS, VC_END_MS, VC_MARKER_LEDS,
           (unsigned long)(vcDelay * 1000UL + VC_DARK_MS + VC_FLASH_MS + VC_GAP_MS + (uint32_t)n * vcStep + VC_END_MS));
  webJson(200, b);
}

static void handleVcalibGet() {
  webSendVcalib();
}

// POST /api/vcalib/start: delay 0-120 (с, по умолчанию 12), step 60-1000 (мс, по умолчанию 120)
static void handleVcalibStart() {
  if (!webAllowed()) return;
  long delaySec = VC_DELAY_DEFAULT, stepMs = VC_STEP_DEFAULT;
  if (webArgInt("delay", 0, 120, delaySec) < 0) {
    webError(400, "delay");
    return;
  }
  if (webArgInt("step", 60, 1000, stepMs) < 0) {
    webError(400, "step");
    return;
  }
  if (vcRun) {
    webError(409, "busy");
    return;
  }
  vcalibStart(delaySec, stepMs);
  webSendVcalib();
}

static void handleVcalibStop() {
  if (!webAllowed()) return;
  vcalibStop();
  webSendVcalib();
}

void webBegin() {
  scenesBegin();
  static const char* hdrs[] = {WEB_HDR};
  // (size_t): на ядре 3.x без приведения выбирается шаблон collectHeaders(const Args&...) и сборка падает
  webServer.collectHeaders(hdrs, (size_t)1);
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/api/state", HTTP_GET, handleState);
  webServer.on("/api/set", HTTP_POST, handleSet);
  webServer.on("/api/next", HTTP_POST, handleNext);
  webServer.on("/api/effect", HTTP_GET, handleEffectGet);
  webServer.on("/api/effect", HTTP_POST, handleEffectPost);
  webServer.on("/api/vcalib", HTTP_GET, handleVcalibGet);
  webServer.on("/api/vcalib/start", HTTP_POST, handleVcalibStart);
  webServer.on("/api/vcalib/stop", HTTP_POST, handleVcalibStop);
  webServer.on("/api/xy", HTTP_GET, handleXyGet);
  webServer.on("/api/xy", HTTP_POST, handleXyPost);
  webServer.on("/api/led", HTTP_POST, handleLed);
  webServer.on("/api/scenes", HTTP_GET, handleScenesGet);
  webServer.on("/api/scenes/save", HTTP_POST, handleSceneSave);
  webServer.on("/api/scenes/load", HTTP_POST, handleSceneLoad);
  webServer.on("/api/scenes/delete", HTTP_POST, handleSceneDelete);
  webServer.onNotFound([]() { webError(404, "not found"); });
  webServer.begin();
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

void webTick() {
  if (hlTmr.ready()) ledHighlightOff();
  webServer.handleClient();
  MDNS.update();
}

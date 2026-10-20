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
  webServer.on("/api/scenes", HTTP_GET, handleScenesGet);
  webServer.on("/api/scenes/save", HTTP_POST, handleSceneSave);
  webServer.on("/api/scenes/load", HTTP_POST, handleSceneLoad);
  webServer.on("/api/scenes/delete", HTTP_POST, handleSceneDelete);
  webServer.onNotFound([]() { webError(404, "not found"); });
  webServer.begin();
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

void webTick() {
  webServer.handleClient();
  MDNS.update();
}

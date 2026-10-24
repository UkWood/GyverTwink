// Веб-интерфейс: страница вшита в прошивку (web_page.h, gzip), управление - JSON API (docs/WEB_API.md).
// Использует встроенный ESP8266WebServer: без внешних библиотек, на ядрах 2.7.x и 3.x.
//
// Защита: все изменяющие запросы (POST) обязаны нести заголовок X-Requested-With: GyverTwink.
// Чужая страница в браузере не может отправить такой запрос (нужен CORS-предзапрос, устройство на него не отвечает),
// поэтому открытый в браузере сайт не сможет управлять гирляндой в вашей сети.

#define WEB_HDR "X-Requested-With"
#define WEB_HDR_VAL "GyverTwink"
#define WEB_TOKEN "X-GT-Token"
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

// проверка токена (guard.ino): true - можно. Нет токена или он неверный - 401, адрес заблокирован за подбор - 429
static bool webAuth() {
  if (!guardOn()) return true;
  String t = webServer.header(WEB_TOKEN);
  byte r = guardVerify((uint32_t)webServer.client().remoteIP(), t.c_str());
  if (r == 0) return true;
  if (r == 2) {
    webServer.sendHeader("Retry-After", "60");
    webError(429, "locked");
  } else {
    webError(401, "auth");
  }
  return false;
}

// только запросы нашей страницы (и с токеном, если он включён)
static bool webAllowed() {
  if (webServer.header(WEB_HDR) != WEB_HDR_VAL) {
    webError(403, "forbidden");
    return false;
  }
  return webAuth();
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

// аргумент со знаком: 1 - принят, 0 - его нет в запросе, -1 - не число или вне диапазона
static int webArgSigned(const char* name, long lo, long hi, long& v) {
  if (!webServer.hasArg(name)) return 0;
  String s = webServer.arg(name);
  unsigned i = (s.length() > 0 && s[0] == '-') ? 1 : 0;
  if (s.length() == i || s.length() > 7) return -1;
  for (unsigned k = i; k < s.length(); k++) {
    if (s[k] < '0' || s[k] > '9') return -1;
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
           (unsigned)(forceTmr.state() ? forceEff : curEff), EFFECT_COUNT, (unsigned)ESP.getFreeHeap());
  webJson(200, b);
}

static void webSendEffect(byte n) {
  char b[160];
  int len = snprintf(b, sizeof(b), "{\"n\":%u,\"fav\":%d,\"scale\":%u,\"speed\":%u", (unsigned)n, effAt(n).fav ? 1 : 0,
                     (unsigned)effAt(n).scale, (unsigned)effAt(n).speed);
  if (n == FIRE2_EFF)   // у «Огня v2» есть ещё четыре параметра
    len += snprintf(b + len, sizeof(b) - len, ",\"shape\":%u,\"sparks\":%u,\"flicker\":%u,\"white\":%d", (unsigned)fire2.shape,
                    (unsigned)fire2.sparks, (unsigned)fire2.flicker, fire2.white ? 1 : 0);
  snprintf(b + len, sizeof(b) - len, "}");
  webJson(200, b);
}

// ---------- расписание и время (schedule.ino) ----------
static void webSendSchedule() {
  char b[300];
  int onAt, offAt;
  schedToday(onAt, offAt);
  static const char* const src[] = {"none", "ntp", "manual"};
  snprintf(b, sizeof(b),
           "{\"en\":%d,\"tz\":%d,\"lat\":%d,\"lon\":%d,\"days\":%u,\"onT\":%u,\"onV\":%d,\"offT\":%u,\"offV\":%d,"
           "\"synced\":%d,\"src\":\"%s\",\"utc\":%lu,\"onAt\":%d,\"offAt\":%d}",
           sched.en ? 1 : 0, sched.tz, sched.lat, sched.lon, (unsigned)sched.days, (unsigned)sched.onT, sched.onV,
           (unsigned)sched.offT, sched.offV, timeValid() ? 1 : 0, src[timeSource()], timeValid() ? (unsigned long)GT_NOW() : 0ul,
           onAt, offAt);
  webJson(200, b);
}

static void handleScheduleGet() {
  if (!webAuth()) return;
  webSendSchedule();
}

// поле расписания: если есть в запросе - проверить и записать в cur; false - ошибка уже отправлена
static bool webSchedArg(const char* name, long lo, long hi, long& cur) {
  long v;
  int r = webArgSigned(name, lo, hi, v);
  if (r < 0) {
    webError(400, name);
    return false;
  }
  if (r > 0) cur = v;
  return true;
}

// POST /api/schedule: любые из en 0/1, tz (минуты), lat и lon (сотые доли градуса), days (маска 0-127),
// onT и offT (0 - время, 1 - закат, 2 - рассвет), onV и offV (минуты суток или смещение в минутах). Всё или ничего.
static void handleSchedulePost() {
  if (!webAllowed()) return;
  long en = sched.en, tz = sched.tz, lat = sched.lat, lon = sched.lon, days = sched.days;
  long onT = sched.onT, offT = sched.offT, onV = sched.onV, offV = sched.offV;
  if (!webSchedArg("en", 0, 1, en) || !webSchedArg("tz", -720, 840, tz) || !webSchedArg("lat", -9000, 9000, lat) ||
      !webSchedArg("lon", -18000, 18000, lon) || !webSchedArg("days", 0, 127, days) || !webSchedArg("onT", 0, 2, onT) ||
      !webSchedArg("offT", 0, 2, offT) || !webSchedArg("onV", -1439, 1439, onV) || !webSchedArg("offV", -1439, 1439, offV)) {
    return;
  }
  if (!schedValueOk(onT, onV)) {
    webError(400, "onV");
    return;
  }
  if (!schedValueOk(offT, offV)) {
    webError(400, "offV");
    return;
  }
  sched.en = en;
  sched.tz = tz;
  sched.lat = lat;
  sched.lon = lon;
  sched.days = days;
  sched.onT = onT;
  sched.offT = offT;
  sched.onV = onV;
  sched.offV = offV;
  schedChanged();
  webSendSchedule();
}

// POST /api/time: unix - время UTC в секундах (страница берёт его у телефона или компьютера)
static void handleTimePost() {
  if (!webAllowed()) return;
  String s = webServer.arg("unix");
  uint64_t t = 0;
  bool ok = webServer.hasArg("unix") && s.length() > 0 && s.length() <= 10;
  for (unsigned i = 0; ok && i < s.length(); i++) {
    ok = s[i] >= '0' && s[i] <= '9';
    t = t * 10 + (s[i] - '0');
  }
  if (!ok || t < TIME_VALID_MIN || t > TIME_VALID_MAX) {
    webError(400, "unix");
    return;
  }
  timeSet((uint32_t)t);
  webSendSchedule();
}

// ---------- защита токеном (guard.ino) ----------
// on - защита включена; ok - токен из запроса подходит (или защиты нет); locked - адрес заблокирован за подбор;
// win - окно обслуживания открыто (задать/сменить/снять токен можно только в нём); udpLock - команды записи по UDP закрыты
static void webSendAuth(byte r) {
  char b[110];
  snprintf(b, sizeof(b), "{\"on\":%d,\"ok\":%d,\"locked\":%d,\"win\":%d,\"udpLock\":%d,\"min\":%d,\"max\":%d}", guardOn() ? 1 : 0, r == 0 ? 1 : 0,
           r == 2 ? 1 : 0, otaOpen() ? 1 : 0, guard.udpOpen ? 0 : 1, TOKEN_MIN, TOKEN_MAX);
  webJson(200, b);
}

// GET /api/auth открыт: страница по нему узнаёт, нужен ли вход
static void handleAuthGet() {
  webSendAuth(guardVerify((uint32_t)webServer.client().remoteIP(), webServer.header(WEB_TOKEN).c_str()));
}

// POST /api/auth: token - новый токен (пустой - снять защиту; только при открытом окне обслуживания, оно после этого закрывается);
// udpLock - 1 закрыть команды записи по UDP, 0 открыть (нужен токен, если защита включена). Всё или ничего.
// В ответе ok = 1: запрос выполнен (токен нового запроса страница проверит сама через GET /api/auth).
static void handleAuthPost() {
  if (webServer.header(WEB_HDR) != WEB_HDR_VAL) {
    webError(403, "forbidden");
    return;
  }
  bool hasTok = webServer.hasArg("token"), hasUdp = webServer.hasArg("udpLock");
  if (!hasTok && !hasUdp) {
    webError(400, "token");
    return;
  }
  String t = webServer.arg("token");
  if (hasTok && t.length() > 0 && !guardTokenValid(t.c_str())) {
    webError(400, "token");
    return;
  }
  long udp = 0;
  if (hasUdp && webArgInt("udpLock", 0, 1, udp) <= 0) {
    webError(400, "udpLock");
    return;
  }
  if (hasTok) {
    if (!otaOpen()) {   // менять токен можно только рядом с гирляндой
      webError(403, "window");
      return;
    }
  } else if (!webAuth()) {
    return;
  }
  if (hasTok) guardSet(t.c_str());
  if (hasUdp) guard.udpOpen = !udp;
  EEguard.update();
  if (hasTok) otaCloseWindow();
  webSendAuth(0);
}

static void handleRoot() {
  webServer.sendHeader("Content-Encoding", "gzip");
  webServer.sendHeader("Cache-Control", "no-cache");
  webServer.send_P(200, "text/html", (PGM_P)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

static void handleState() {
  if (!webAuth()) return;
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
  if (webArgInt("n", 0, EFFECT_COUNT - 1, n) > 0) return true;
  webError(400, "n");
  return false;
}

static void handleEffectGet() {
  if (!webAuth()) return;
  long n;
  if (webEffectArg(n)) webSendEffect(n);
}

// POST /api/effect: n и любые из fav, scale, speed; show=1 - сделать этот эффект текущим (без временного показа).
// Для «Огня v2» (n = FIRE2_EFF) ещё shape, sparks, flicker (0-255) и white (0/1); у других эффектов эти поля - ошибка 400
static void handleEffectPost() {
  if (!webAllowed()) return;
  long n, fav, scale, speed, show, shape, sparks, flicker, white;
  if (!webEffectArg(n)) return;
  int rf = webArgInt("fav", 0, 1, fav);
  int rs = webArgInt("scale", 0, 255, scale);
  int rp = webArgInt("speed", 0, 255, speed);
  int rw = webArgInt("show", 0, 1, show);
  int rh = webArgInt("shape", 0, 255, shape);
  int rk = webArgInt("sparks", 0, 255, sparks);
  int rl = webArgInt("flicker", 0, 255, flicker);
  int rt = webArgInt("white", 0, 1, white);
  if (rf < 0 || rs < 0 || rp < 0 || rw < 0 || rh < 0 || rk < 0 || rl < 0 || rt < 0) {
    webError(400, rf < 0 ? "fav" : rs < 0 ? "scale" : rp < 0 ? "speed" : rw < 0 ? "show" : rh < 0 ? "shape" : rk < 0 ? "sparks" : rl < 0 ? "flicker" : "white");
    return;
  }
  if ((rh > 0 || rk > 0 || rl > 0 || rt > 0) && n != FIRE2_EFF) {
    webError(400, rh > 0 ? "shape" : rk > 0 ? "sparks" : rl > 0 ? "flicker" : "white");
    return;
  }
  if (rf > 0) effAt(n).fav = fav;
  if (rs > 0) effAt(n).scale = scale;
  if (rp > 0) effAt(n).speed = speed;
  if (rh > 0) fire2.shape = shape;
  if (rk > 0) fire2.sparks = sparks;
  if (rl > 0) fire2.flicker = flicker;
  if (rt > 0) fire2.white = white;
  if (rf > 0 || rs > 0 || rp > 0 || rh > 0 || rk > 0 || rl > 0 || rt > 0) effSave(n);
  if (rw > 0 && show) {
    forceTmr.stop();
    forceEff = n;   // и цель правок по UDP остаётся этим же эффектом
    curEff = n;
    if (cfg.autoCh) switchTmr.restart();   // отсчёт автосмены начинается заново
  }
  webSendEffect(n);
}

// ---------- сцены ----------
static void webSendScenes() {
  char b[480];   // худший случай (8 имён по 16 байт) - около 300; стек ESP8266 мал, лишнего не берём
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
  if (!webAuth()) return;
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
  if (!webAuth()) return;
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
  if (!webAuth()) return;
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

// ---------- обновление прошивки (ota.ino) ----------
static void handleOtaGet() {
  if (!webAuth()) return;
  char b[100];
  snprintf(b, sizeof(b), "{\"open\":%d,\"left\":%u,\"free\":%u,\"busy\":%d}", otaOpen() ? 1 : 0, (unsigned)otaLeftSec(),
           (unsigned)ESP.getFreeSketchSpace(), otaBusy ? 1 : 0);
  webJson(200, b);
}

static bool otaDenied = false, otaFailed = false;

static void otaFail(const char* why) {
  otaFailed = true;
  strncpy(otaDetail, why, sizeof(otaDetail) - 1);
  otaDetail[sizeof(otaDetail) - 1] = '\0';
}

// загрузка файла: вызывается по кускам. Файл принимается только при открытом окне и заголовке страницы
static void handleOtaUpload() {
  HTTPUpload& up = webServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaDenied = otaFailed = false;
    otaDetail[0] = '\0';
    if (!otaOpen() || webServer.header(WEB_HDR) != WEB_HDR_VAL) {
      otaDenied = true;
      return;
    }
    otaBusy = true;
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) otaFail("begin");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaDenied || otaFailed) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaFail("write");
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaDenied || otaFailed) return;
    if (!Update.end(true)) otaFail("end");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (!otaDenied) Update.end();
    otaBusy = false;
  }
}

// ответ после загрузки: успех -> закрыть окно и перезагрузиться через 0,8 с (чтобы ответ дошёл до страницы)
static void handleOtaDone() {
  if (otaDenied || !otaOpen() || webServer.header(WEB_HDR) != WEB_HDR_VAL) {
    otaBusy = false;
    webError(403, "ota-closed");
    return;
  }
  if (otaFailed || Update.hasError()) {
    otaBusy = false;
    webError(500, "update");
    return;
  }
  otaCloseWindow();
  otaRebootAt = millis() + 800;
  if (otaRebootAt == 0) otaRebootAt = 1;
  webJson(200, "{\"ok\":1}");
}

void webBegin() {
  scenesBegin();
  static const char* hdrs[] = {WEB_HDR, WEB_TOKEN};
  // (size_t): на ядре 3.x без приведения выбирается шаблон collectHeaders(const Args&...) и сборка падает
  webServer.collectHeaders(hdrs, (size_t)2);
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/api/state", HTTP_GET, handleState);
  webServer.on("/api/set", HTTP_POST, handleSet);
  webServer.on("/api/next", HTTP_POST, handleNext);
  webServer.on("/api/effect", HTTP_GET, handleEffectGet);
  webServer.on("/api/effect", HTTP_POST, handleEffectPost);
  webServer.on("/api/schedule", HTTP_GET, handleScheduleGet);
  webServer.on("/api/schedule", HTTP_POST, handleSchedulePost);
  webServer.on("/api/time", HTTP_POST, handleTimePost);
  webServer.on("/api/auth", HTTP_GET, handleAuthGet);
  webServer.on("/api/auth", HTTP_POST, handleAuthPost);
  webServer.on("/api/ota", HTTP_GET, handleOtaGet);
  webServer.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
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
  otaTick();
  if (hlTmr.ready()) ledHighlightOff();
  webServer.handleClient();
  MDNS.update();
}

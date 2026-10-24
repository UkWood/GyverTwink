// Сцены (пресеты): набор настроек, который можно сохранить и вернуть одним нажатием.
// Хранятся в LittleFS одним файлом /scenes.bin: заголовок + SCENE_SLOTS записей фиксированного размера.
// В сцену входят яркость, автосмена, случайный порядок, период, текущий эффект и настройки всех эффектов (EFFECT_COUNT).
// Формат файла: версия 2 (с новыми эффектами). Файл версии 1 (22 эффекта) читается и переводится в версию 2 при следующей записи.
// Питание и лимит тока - свойства установки, а не сцены: они не сохраняются и не меняются.
// Дополнения (отдельный файл /x<слот>.bin на сцену): настройки «Огня v2» всегда и схема светодиодов (число светодиодов, координаты),
// если при сохранении выбрано «Сохранить схему». Применение сцены со схемой ставит это число светодиодов и эти координаты.

#define SCENE_FILE "/scenes.bin"
#define SCENE_TMP "/scenes.tmp"

Scene scenes[SCENE_SLOTS];
static uint16_t sceneMapN[SCENE_SLOTS];   // число светодиодов в схеме сцены (0 - схемы в сцене нет); кэш заголовков файлов дополнений

// контрольная сумма Fletcher-16, считается по частям (a и b - накопители, начальное значение a = 1, b = 0)
static void fletcherAdd(uint16_t& a, uint16_t& b, const byte* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    a = (a + p[i]) % 255;
    b = (b + a) % 255;
  }
}

static uint16_t scenesSum() {
  uint16_t a = 1, b = 0;
  fletcherAdd(a, b, (const byte*)scenes, sizeof(scenes));
  return (b << 8) | a;
}

static void sceneExtraPath(byte slot, char* out, const char* ext) {
  snprintf(out, 14, "/x%u.%s", (unsigned)slot, ext);
}

// заголовок файла дополнений целиком и по размеру файла; n - число светодиодов схемы (0 - схемы нет)
static bool sceneExtraHeader(File& f, SceneExtraHeader& h, uint16_t& n) {
  if (f.size() < sizeof(h) || f.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) return false;
  if (h.magic[0] != 'G' || h.magic[1] != 'X' || h.version != SCENE_X_VERSION) return false;
  n = (h.flags & SCENE_X_MAP) ? h.n : 0;
  if ((h.flags & SCENE_X_MAP) && (n < 1 || n > LED_MAX)) return false;
  size_t want = sizeof(h) + h.fireSize + (n ? sizeof(MM) + 2 * (size_t)n : 0);
  return f.size() == want;
}

// допустима ли запись из файла
static bool sceneValid(const Scene& s) {
  if (s.used == 0) return true;
  return s.used == 1 && s.curEff < EFFECT_COUNT && s.prdCh >= 1;
}

// чтение файла версии 1: записи без новых эффектов читаются по одной (стек мал), настройки новых - по умолчанию
static bool scenesLoadV1(File& f, uint16_t sum) {
  Effects def[EXT_EFFECTS];
  fxDefaults(def);
  uint16_t a = 1, b = 0;
  for (int i = 0; i < SCENE_SLOTS; i++) {
    SceneV1 o;
    if (f.read((uint8_t*)&o, sizeof(o)) != sizeof(o)) return false;
    fletcherAdd(a, b, (const byte*)&o, sizeof(o));
    Scene& s = scenes[i];
    memset(&s, 0, sizeof(s));
    memcpy(s.name, o.name, sizeof(s.name));
    s.used = o.used;
    s.bright = o.bright;
    s.autoCh = o.autoCh;
    s.rndCh = o.rndCh;
    s.prdCh = o.prdCh;
    s.curEff = o.curEff;
    memcpy(s.effs, o.effs, sizeof(s.effs));
    memcpy(s.fx, def, sizeof(s.fx));
  }
  return sum == (uint16_t)((b << 8) | a);
}

// чтение файла; любое несоответствие (нет файла, другой размер, неверная сумма, мусор в записях) - сцен нет
static void scenesReadFile() {
  memset(scenes, 0, sizeof(scenes));
  File f = LittleFS.open(SCENE_FILE, "r");
  if (!f) return;
  SceneHeader h;
  bool ok = f.size() >= sizeof(SceneHeader) && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic[0] == 'G' &&
            h.magic[1] == 'S' && h.slots == SCENE_SLOTS;
  if (ok && h.version == 2) {
    ok = f.size() == sizeof(SceneHeader) + sizeof(scenes) && f.read((uint8_t*)scenes, sizeof(scenes)) == sizeof(scenes) &&
         h.sum == scenesSum();
  } else if (ok && h.version == 1) {
    ok = f.size() == sizeof(SceneHeader) + SCENE_SLOTS * sizeof(SceneV1) && scenesLoadV1(f, h.sum);
  } else {
    ok = false;
  }
  f.close();
  for (int i = 0; ok && i < SCENE_SLOTS; i++) ok = sceneValid(scenes[i]);
  if (!ok) {
    memset(scenes, 0, sizeof(scenes));
    return;
  }
  for (int i = 0; i < SCENE_SLOTS; i++) scenes[i].name[SCENE_NAME_LEN] = '\0';
}

// кэш числа светодиодов схем сцен; файлы дополнений пустых слотов удаляются
static void sceneExtrasScan() {
  for (int i = 0; i < SCENE_SLOTS; i++) {
    sceneMapN[i] = 0;
    char path[14];
    sceneExtraPath(i, path, "bin");
    if (scenes[i].used != 1) {
      LittleFS.remove(path);
      continue;
    }
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    SceneExtraHeader h;
    uint16_t n = 0;
    if (sceneExtraHeader(f, h, n)) sceneMapN[i] = n;
    f.close();
  }
}

// прочитать сцены и дополнения (при старте)
void scenesLoad() {
  scenesReadFile();
  sceneExtrasScan();
}

// запись через временный файл: при сбое питания остаётся прежняя версия
static bool scenesSave() {
  SceneHeader h = {{'G', 'S'}, 2, SCENE_SLOTS, scenesSum()};
  File f = LittleFS.open(SCENE_TMP, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) &&
            f.write((const uint8_t*)scenes, sizeof(scenes)) == sizeof(scenes);
  f.close();
  if (!ok) {
    LittleFS.remove(SCENE_TMP);
    return false;
  }
  LittleFS.remove(SCENE_FILE);
  return LittleFS.rename(SCENE_TMP, SCENE_FILE);
}

void scenesBegin() {
  LittleFS.begin();   // при первом запуске раздел форматируется сам
  scenesLoad();
}

// имя: UTF-8 без управляющих символов, не больше SCENE_NAME_LEN байт (обрезается по границе символа),
// кавычки и обратная косая заменяются, чтобы имя можно было вставлять в JSON как есть.
// Возвращает длину в out или -1, если имя пустое или не является корректным UTF-8.
int sceneCleanName(const char* in, char* out) {
  int n = 0;
  size_t i = 0;
  while (in[i] && n < SCENE_NAME_LEN) {
    byte c = in[i];
    int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0) return -1;
    for (int k = 1; k < len; k++) {
      if (((byte)in[i + k] & 0xC0) != 0x80) return -1;   // обрыв или лишние байты
    }
    if (len == 1 && (c < 0x20 || c == 0x7F)) return -1;
    if (n + len > SCENE_NAME_LEN) break;   // символ не помещается целиком
    for (int k = 0; k < len; k++) out[n++] = in[i + k];
    i += len;
  }
  // остаток строки отбрасывается, но управляющих символов в нём быть не должно
  for (; in[i]; i++) {
    if ((byte)in[i] < 0x20 || (byte)in[i] == 0x7F) return -1;
  }
  for (int k = 0; k < n; k++) {
    if (out[k] == '"' || out[k] == '\\') out[k] = '\'';
  }
  out[n] = '\0';
  // имя из одних пробелов - пустое
  bool blank = true;
  for (int k = 0; k < n; k++) blank &= out[k] == ' ';
  return blank ? -1 : n;
}

// записать дополнения слота во временный файл: настройки «Огня v2» и (withMap) схема светодиодов
static bool sceneExtraWriteTmp(byte slot, bool withMap) {
  char tmp[14];
  sceneExtraPath(slot, tmp, "tmp");
  SceneExtraHeader h = {{'G', 'X'}, SCENE_X_VERSION, (byte)(withMap ? SCENE_X_MAP : 0), (byte)sizeof(Fire2), 0, (uint16_t)(withMap ? cfg.ledAm : 0), 0};
  uint16_t a = 1, b = 0;
  fletcherAdd(a, b, (const byte*)&fire2, sizeof(Fire2));
  if (withMap) {
    fletcherAdd(a, b, (const byte*)&mm, sizeof(MM));
    fletcherAdd(a, b, (const byte*)xy, 2 * (size_t)cfg.ledAm);
  }
  h.sum = (b << 8) | a;
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) && f.write((const uint8_t*)&fire2, sizeof(Fire2)) == sizeof(Fire2);
  if (ok && withMap) {
    ok = f.write((const uint8_t*)&mm, sizeof(MM)) == sizeof(MM) && f.write((const uint8_t*)xy, 2 * (size_t)cfg.ledAm) == 2 * (size_t)cfg.ledAm;
  }
  f.close();
  if (!ok) LittleFS.remove(tmp);
  return ok;
}

// сохранить текущие настройки в слот (withMap - ещё и схему светодиодов); false - ошибка записи (прежнее содержимое слота сохраняется)
bool sceneSave(byte slot, const char* cleanName, bool withMap) {
  if (slot >= SCENE_SLOTS) return false;
  if (!sceneExtraWriteTmp(slot, withMap)) return false;   // сначала дополнения во временный файл: при отказе ничего не меняется
  Scene old = scenes[slot];
  Scene& s = scenes[slot];
  memset(&s, 0, sizeof(s));
  s.used = 1;
  strncpy(s.name, cleanName, SCENE_NAME_LEN);
  s.bright = cfg.bright;
  s.autoCh = cfg.autoCh;
  s.rndCh = cfg.rndCh;
  s.prdCh = cfg.prdCh ? cfg.prdCh : 1;
  s.curEff = curEff < EFFECT_COUNT ? curEff : 0;
  memcpy(s.effs, effs, sizeof(effs));
  memcpy(s.fx, effsX, sizeof(effsX));
  char tmp[14], path[14];
  sceneExtraPath(slot, tmp, "tmp");
  sceneExtraPath(slot, path, "bin");
  if (!scenesSave()) {
    scenes[slot] = old;
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {   // сцена записана, а дополнения нет: лучше сообщить об ошибке, чем оставить чужие
    sceneMapN[slot] = 0;
    return false;
  }
  sceneMapN[slot] = withMap ? cfg.ledAm : 0;
  return true;
}

// дополнения слота: проверить файл целиком (заголовок, размер, контрольная сумма) и только потом применить
static void sceneExtraApply(byte slot) {
  char path[14];
  sceneExtraPath(slot, path, "bin");
  File f = LittleFS.open(path, "r");
  if (!f) return;
  SceneExtraHeader h;
  uint16_t n = 0;
  if (!sceneExtraHeader(f, h, n)) {
    f.close();
    return;
  }
  uint16_t a = 1, b = 0;
  byte buf[64];
  size_t left = h.fireSize + (n ? sizeof(MM) + 2 * (size_t)n : 0);
  while (left) {
    size_t k = f.read(buf, left < sizeof(buf) ? left : sizeof(buf));
    if (k == 0) break;
    fletcherAdd(a, b, buf, k);
    left -= k;
  }
  f.close();
  if (left || h.sum != (uint16_t)((b << 8) | a)) return;   // повреждён: сцена применяется без дополнений
  f = LittleFS.open(path, "r");
  if (!f) return;
  f.read((uint8_t*)&h, sizeof(h));
  if (h.fireSize == sizeof(Fire2)) {
    f.read((uint8_t*)&fire2, sizeof(Fire2));
    EEfire2.update();
  } else {
    for (size_t i = 0; i < h.fireSize; i++) f.read(buf, 1);   // настройки огня другой версии не берём
  }
  if (n) {
    MM skip;
    f.read((uint8_t*)&skip, sizeof(skip));   // границы пересчитываются по координатам
    setLedAmount(n);                          // сцена со схемой ставит и число светодиодов
    f.read((uint8_t*)xy, 2 * (size_t)n);
    recalcBounds();
    coordsSave();
  }
  f.close();
}

// применить сцену; false - слот пуст
bool sceneLoad(byte slot) {
  if (slot >= SCENE_SLOTS || scenes[slot].used != 1) return false;
  const Scene& s = scenes[slot];
  settingsTouch();
  setBright(s.bright);
  setPeriod(s.prdCh);          // период раньше автосмены: автосмена перезапускает таймер с новым периодом
  setRandomSwitch(s.rndCh);
  setAutoSwitch(s.autoCh);
  memcpy(effs, s.effs, sizeof(effs));
  memcpy(effsX, s.fx, sizeof(effsX));
  EEeff.update();
  EEfx.update();
  curEff = s.curEff;
  sceneExtraApply(slot);       // «Огонь v2» и схема светодиодов, если они сохранены в сцене
  return true;
}

bool sceneDelete(byte slot) {
  if (slot >= SCENE_SLOTS || scenes[slot].used != 1) return false;
  Scene old = scenes[slot];
  memset(&scenes[slot], 0, sizeof(Scene));
  if (!scenesSave()) {
    scenes[slot] = old;
    return false;
  }
  char path[14];
  sceneExtraPath(slot, path, "bin");
  LittleFS.remove(path);
  sceneMapN[slot] = 0;
  return true;
}

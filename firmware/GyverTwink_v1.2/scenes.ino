// Сцены (пресеты): набор настроек, который можно сохранить и вернуть одним нажатием.
// Хранятся в LittleFS одним файлом /scenes.bin: заголовок + SCENE_SLOTS записей фиксированного размера.
// В сцену входят яркость, автосмена, случайный порядок, период, текущий эффект и настройки всех 22 эффектов.
// Питание, число светодиодов и лимит тока - свойства установки, а не сцены: они не сохраняются и не меняются.

#define SCENE_FILE "/scenes.bin"
#define SCENE_TMP "/scenes.tmp"

Scene scenes[SCENE_SLOTS];

static uint16_t scenesSum() {
  const byte* p = (const byte*)scenes;
  uint16_t a = 1, b = 0;   // Fletcher-16
  for (size_t i = 0; i < sizeof(scenes); i++) {
    a = (a + p[i]) % 255;
    b = (b + a) % 255;
  }
  return (b << 8) | a;
}

// допустима ли запись из файла
static bool sceneValid(const Scene& s) {
  if (s.used == 0) return true;
  return s.used == 1 && s.curEff < ACTIVE_PALETTES * 2 && s.prdCh >= 1;
}

// чтение файла; любое несоответствие (нет файла, другой размер, неверная сумма, мусор в записях) - сцен нет
void scenesLoad() {
  memset(scenes, 0, sizeof(scenes));
  File f = LittleFS.open(SCENE_FILE, "r");
  if (!f) return;
  SceneHeader h;
  bool ok = f.size() == sizeof(SceneHeader) + sizeof(scenes) && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) &&
            h.magic[0] == 'G' && h.magic[1] == 'S' && h.version == 1 && h.slots == SCENE_SLOTS &&
            f.read((uint8_t*)scenes, sizeof(scenes)) == sizeof(scenes) && h.sum == scenesSum();
  f.close();
  for (int i = 0; ok && i < SCENE_SLOTS; i++) ok = sceneValid(scenes[i]);
  if (!ok) {
    memset(scenes, 0, sizeof(scenes));
    return;
  }
  for (int i = 0; i < SCENE_SLOTS; i++) scenes[i].name[SCENE_NAME_LEN] = '\0';
}

// запись через временный файл: при сбое питания остаётся прежняя версия
static bool scenesSave() {
  SceneHeader h = {{'G', 'S'}, 1, SCENE_SLOTS, scenesSum()};
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

// сохранить текущие настройки в слот; false - ошибка записи (прежнее содержимое слота сохраняется)
bool sceneSave(byte slot, const char* cleanName) {
  if (slot >= SCENE_SLOTS) return false;
  Scene old = scenes[slot];
  Scene& s = scenes[slot];
  memset(&s, 0, sizeof(s));
  s.used = 1;
  strncpy(s.name, cleanName, SCENE_NAME_LEN);
  s.bright = cfg.bright;
  s.autoCh = cfg.autoCh;
  s.rndCh = cfg.rndCh;
  s.prdCh = cfg.prdCh ? cfg.prdCh : 1;
  s.curEff = curEff < ACTIVE_PALETTES * 2 ? curEff : 0;
  memcpy(s.effs, effs, sizeof(effs));
  if (scenesSave()) return true;
  scenes[slot] = old;
  return false;
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
  EEeff.update();
  curEff = s.curEff;
  return true;
}

bool sceneDelete(byte slot) {
  if (slot >= SCENE_SLOTS || scenes[slot].used != 1) return false;
  Scene old = scenes[slot];
  memset(&scenes[slot], 0, sizeof(Scene));
  if (scenesSave()) return true;
  scenes[slot] = old;
  return false;
}

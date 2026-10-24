// Структуры данных, которые хранятся в EEPROM.
// Вынесены в заголовок, чтобы их же использовали хост-тесты (tests/).
// ВАЖНО: размеры и порядок полей менять нельзя - сдвинутся адреса блоков и сбросится калибровка.
// Перед подключением должны быть определены POWER_LIMIT_MA и типы Arduino (byte, uint16_t).
#pragma once

#define ACTIVE_PALETTES 11
// Эффекты 0..21 - палитры (градиент и шум), 22.. - новые (effects.ino). Новые эффекты хранят настройки в отдельном блоке EEPROM
// в конце (effsX): блок effs[22] менять нельзя, иначе сдвинутся адреса и сбросится калибровка.
#define EXT_EFFECTS 6
#define EFFECT_COUNT (ACTIVE_PALETTES * 2 + EXT_EFFECTS)

struct Cfg {
  uint16_t ledAm = 50;
  bool power = 1;
  byte bright = 100;
  bool autoCh = 0;
  bool rndCh = 0;
  byte prdCh = 1;
  bool turnOff = 0;
  byte offTmr = 60;
};

struct MM {
  byte minY = 0;
  byte maxY = 255;
  byte minX = 0;
  byte maxX = 255;
  byte w = 255;
  byte h = 255;
};

// настройки питания лежат в отдельном блоке в конце EEPROM,
// чтобы не сдвигать блоки с калибровкой (иначе после обновления пришлось бы калибровать заново)
struct Pwr {
  uint16_t maxMa = POWER_LIMIT_MA;
};

struct Effects {
  bool fav = true;
  byte scale = 50;
  byte speed = 150;
};

extern Effects effs[ACTIVE_PALETTES * 2];
extern Effects effsX[EXT_EFFECTS];

// настройки эффекта по общему номеру 0..EFFECT_COUNT-1
inline Effects& effAt(byte n) {
  return n < ACTIVE_PALETTES * 2 ? effs[n] : effsX[n - ACTIVE_PALETTES * 2];
}

// значения новых эффектов по умолчанию: масштаб и скорость подобраны под каждый, в избранное по умолчанию не входят
// (иначе после обновления автосмена начала бы показывать их без ведома владельца)
inline void fxDefaults(Effects* fx) {
  static const byte sc[EXT_EFFECTS] = {50, 64, 50, 10, 40, 40};
  static const byte sp[EXT_EFFECTS] = {120, 160, 170, 140, 150, 150};
  for (byte i = 0; i < EXT_EFFECTS; i++) {
    fx[i].fav = true;   // новые эффекты сразу участвуют в автосмене и в переборе кнопкой
    fx[i].scale = sc[i];
    fx[i].speed = sp[i];
  }
}

// ---- расписание (schedule.ino): отдельный блок EEPROM в конце ----
#define SCHED_CLOCK 0     // событие в заданное время суток
#define SCHED_SUNSET 1    // относительно заката
#define SCHED_SUNRISE 2   // относительно рассвета

struct Sched {
  int16_t tz = 0;       // сдвиг местного времени от UTC, минуты (постоянный: на летнее время переходят вручную)
  int16_t lat = 0;      // широта, сотые доли градуса (север +)
  int16_t lon = 0;      // долгота, сотые доли градуса (восток +)
  int16_t onV = 1080;   // включение: минуты суток (SCHED_CLOCK) или смещение в минутах от заката/рассвета; по умолчанию 18:00
  int16_t offV = 1380;  // выключение; по умолчанию 23:00
  bool en = 0;
  byte days = 127;      // дни недели: бит 0 - понедельник ... бит 6 - воскресенье
  byte onT = SCHED_CLOCK;
  byte offT = SCHED_CLOCK;
};

// ---- защита доступа токеном (guard.ino): отдельный блок EEPROM в конце ----
// Имена без слов auth/access: они пересекаются с SDK ESP8266 (AUTH_MAX и др.) и POSIX.
#define TOKEN_MIN 8
#define TOKEN_MAX 32

struct Guard {
  char token[TOKEN_MAX + 1] = "";   // пусто - защита выключена
  byte udpOpen = 1;                 // 1 - команды записи по UDP (приложение) принимаются и при включённом токене
};

// ---- сцены (scenes.ino): типы здесь, потому что генератор прототипов Arduino вставляет их в начало скетча,
// и сигнатуры функций со структурами в .ino-вкладках не компилируются ("'Scene' does not name a type") ----
#define SCENE_SLOTS 8
#define SCENE_NAME_LEN 16   // байт (в UTF-8 кириллица занимает 2 байта на букву)

// формат файла версии 2: запись версии 1 (SceneV1, до новых эффектов) плюс настройки новых эффектов в конце
struct Scene {
  byte used;
  char name[SCENE_NAME_LEN + 1];
  byte bright;
  byte autoCh;
  byte rndCh;
  byte prdCh;
  byte curEff;
  Effects effs[ACTIVE_PALETTES * 2];
  Effects fx[EXT_EFFECTS];
};
static_assert(sizeof(Scene) == 1 + SCENE_NAME_LEN + 1 + 5 + 3 * (ACTIVE_PALETTES * 2 + EXT_EFFECTS), "размер Scene - формат файла, менять нельзя");

// запись версии 1: читается только при переходе со старой прошивки (scenesLoad)
struct SceneV1 {
  byte used;
  char name[SCENE_NAME_LEN + 1];
  byte bright;
  byte autoCh;
  byte rndCh;
  byte prdCh;
  byte curEff;
  Effects effs[ACTIVE_PALETTES * 2];
};
static_assert(sizeof(SceneV1) == 1 + SCENE_NAME_LEN + 1 + 5 + 3 * ACTIVE_PALETTES * 2, "размер SceneV1 - формат файла, менять нельзя");

// заголовок файла: версия формата, число слотов и контрольная сумма записей
struct SceneHeader {
  char magic[2];
  byte version;
  byte slots;
  uint16_t sum;
};

// Структуры данных, которые хранятся в EEPROM.
// Вынесены в заголовок, чтобы их же использовали хост-тесты (tests/).
// ВАЖНО: размеры и порядок полей менять нельзя - сдвинутся адреса блоков и сбросится калибровка.
// Перед подключением должны быть определены POWER_LIMIT_MA и типы Arduino (byte, uint16_t).
#pragma once

// Координаты светодиодов хранятся в двух блоках EEPROM: первые XY_BASE (500) - исторический блок xy (его размер и ключ менять нельзя,
// иначе сбросится калибровка и сдвинутся все блоки за ним), остальные (до LED_MAX) - отдельный блок xyHi в самом конце памяти.
#define XY_BASE 500

#define ACTIVE_PALETTES 11
// Эффекты 0..21 - палитры (градиент и шум), 22.. - новые (effects.ino). Новые эффекты хранят настройки в отдельном блоке EEPROM
// в конце (effsX): блок effs[22] менять нельзя, иначе сдвинутся адреса и сбросится калибровка.
#define EXT_EFFECTS 6
// «Огонь v2» (последний эффект) хранит настройки в своём блоке EEPROM (fire2): блок effsX не растёт, иначе сдвинутся расписание и токен
#define FIRE2_EFF (ACTIVE_PALETTES * 2 + EXT_EFFECTS)
#define EFFECT_COUNT (FIRE2_EFF + 1)

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

// порядок цветов ленты (colorder.ino): FastLED настроен на RGB, а ленте цвета уходят в выбранном порядке; блок EEPROM в конце.
// ORDER_PERM[порядок][k] - какой логический канал (0 R, 1 G, 2 B) идёт в k-м байте, который получает лента.
// Порядки: 0 RGB, 1 RBG, 2 GRB (по умолчанию, как было до настройки), 3 GBR, 4 BRG, 5 BGR.
#define ORDER_COUNT 6
#define ORDER_GRB 2
struct ColorOrder {
  byte order = ORDER_GRB;
};
extern ColorOrder colorOrder;
static const byte ORDER_PERM[ORDER_COUNT][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};

struct Effects {
  bool fav = true;
  byte scale = 50;
  byte speed = 150;
};

// «Огонь v2»: общие настройки, три параметра формы (0..255) и цвета шести уровней жара, блок EEPROM в самом конце
#define FIRE2_LEVELS 6
struct Fire2 {
  Effects e;
  byte shape = 120;     // колпак: насколько гаснет пламя к краям и к верху
  byte sparks = 80;     // искры над пламенем
  byte flicker = 100;   // трепет: быстрый мелкий шум поверх плавного
  // цвета уровней жара от самого холодного (0) до самого горячего (5), между ними цвет плавно переходит; по умолчанию - как на фото костра:
  // чёрная «шапка», тёмно-бордовый, красный, красно-оранжевый, жёлто-оранжевый, светло-жёлтый (без чисто белого)
  byte col[FIRE2_LEVELS][3] = {{0, 0, 0}, {70, 6, 0}, {190, 18, 0}, {255, 70, 0}, {255, 140, 8}, {255, 205, 60}};
};

extern Effects effs[ACTIVE_PALETTES * 2];
extern Effects effsX[EXT_EFFECTS];
extern Fire2 fire2;

// настройки эффекта по общему номеру 0..EFFECT_COUNT-1
inline Effects& effAt(byte n) {
  if (n >= FIRE2_EFF) return fire2.e;
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

// значения «Огня v2» по умолчанию (прочитанный из EEPROM блок их заменит)
inline void fire2Defaults(Fire2& f) {
  f = Fire2();
  f.e.fav = true;
  f.e.scale = 50;
  f.e.speed = 170;
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

// дополнения сцены - отдельный файл на слот /x<слот>.bin (scenes.ino): настройки «Огня v2» и, если выбрано при сохранении,
// схема светодиодов (число светодиодов, границы карты и координаты). Основной файл сцен при этом не меняется.
// После заголовка идут: Fire2 (fireSize байт, если fireSize > 0), затем при flags & 1 - MM и n пар координат.
#define SCENE_X_VERSION 1
#define SCENE_X_MAP 1
struct SceneExtraHeader {
  byte magic[2];    // 'G', 'X'
  byte version;     // SCENE_X_VERSION
  byte flags;       // SCENE_X_MAP - в файле есть схема
  byte fireSize;    // размер Fire2 в файле (0 - настроек огня нет)
  byte reserved;
  uint16_t n;       // число светодиодов схемы (0 без схемы)
  uint16_t sum;     // Fletcher-16 по данным после заголовка
};

// заголовок файла: версия формата, число слотов и контрольная сумма записей
struct SceneHeader {
  char magic[2];
  byte version;
  byte slots;
  uint16_t sum;
};

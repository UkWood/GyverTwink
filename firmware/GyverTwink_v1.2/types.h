// Структуры данных, которые хранятся в EEPROM.
// Вынесены в заголовок, чтобы их же использовали хост-тесты (tests/).
// ВАЖНО: размеры и порядок полей менять нельзя - сдвинутся адреса блоков и сбросится калибровка.
// Перед подключением должны быть определены POWER_LIMIT_MA и типы Arduino (byte, uint16_t).
#pragma once

#define ACTIVE_PALETTES 11

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

// ---- сцены (scenes.ino): типы здесь, потому что генератор прототипов Arduino вставляет их в начало скетча,
// и сигнатуры функций со структурами в .ino-вкладках не компилируются ("'Scene' does not name a type") ----
#define SCENE_SLOTS 8
#define SCENE_NAME_LEN 16   // байт (в UTF-8 кириллица занимает 2 байта на букву)

struct Scene {
  byte used;
  char name[SCENE_NAME_LEN + 1];
  byte bright;
  byte autoCh;
  byte rndCh;
  byte prdCh;
  byte curEff;
  Effects effs[ACTIVE_PALETTES * 2];
};
static_assert(sizeof(Scene) == 1 + SCENE_NAME_LEN + 1 + 5 + 3 * ACTIVE_PALETTES * 2, "размер Scene - формат файла, менять нельзя");

// заголовок файла: версия формата, число слотов и контрольная сумма записей
struct SceneHeader {
  char magic[2];
  byte version;
  byte slots;
  uint16_t sum;
};

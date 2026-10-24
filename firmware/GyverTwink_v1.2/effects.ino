// ---- новые эффекты (номера ACTIVE_PALETTES * 2 ... EFFECT_COUNT - 1) ----
// Общие настройки эффекта: scale - размер/плотность, speed - скорость (128 - стоп, меньше - в обратную сторону).
// Вся арифметика целочисленная: на ESP8266 float медленный, а кадр считается для сотен светодиодов.
// Ось Y направлена вниз, как в кадре камеры при калибровке.
#define FX_TWINKLE 0   // мерцание звёзд
#define FX_CHASE 1     // бегущие огни
#define FX_FIRE 2      // огонь
#define FX_BREATH 3    // дыхание
#define FX_WAVE_Y 4    // волны по высоте
#define FX_WAVE_R 5    // волны от центра
#define FX_FIRE2 6     // огонь v2 (номер эффекта FIRE2_EFF, настройки в fire2)

// треугольная волна 0..255..0 и её сглаженный вариант (smoothstep)
static byte fxTri(byte x) {
  return x < 128 ? x * 2 : (255 - x) * 2;
}

static byte fxEase(byte x) {
  uint32_t t = fxTri(x);
  return t * t * (765 - 2 * t) / 65025;
}

static CRGB fxScale(CRGB c, byte k) {
  return CRGB((uint16_t)c.r * k >> 8, (uint16_t)c.g * k >> 8, (uint16_t)c.b * k >> 8);
}

// положение светодиода на карте, 0..255 в пределах границ; ny = 0 внизу (там, где Y больше)
static void fxPos(int i, int& nx, int& ny) {
  nx = constrain((xy[i][0] - mm.minX) * 255 / max((int)mm.w, 1), 0, 255);
  ny = constrain((mm.maxY - xy[i][1]) * 255 / max((int)mm.h, 1), 0, 255);
}

// звёзды: кадр хранится в самом массиве leds, каждый кадр затухает и добавляются новые искры.
// scale - сколько искр (плотность), speed - как быстро гаснут (поэтому направления нет)
static void fxTwinkle(byte scale, byte speed) {
  int n = cfg.ledAm;
  uint16_t keep = 256 - (6 + speed / 8);
  for (int i = 0; i < n; i++) {
    leds[i].r = leds[i].r * keep >> 8;
    leds[i].g = leds[i].g * keep >> 8;
    leds[i].b = leds[i].b * keep >> 8;
  }
  long want = (long)n * scale;   // искр за кадр: want / 10000, дробная часть - вероятностью
  int sparks = want / 10000;
  if (random(10000) < want % 10000) sparks++;
  for (int k = 0; k < sparks; k++) leds[random(n)] = ColorFromPalette(paletteArr[0], random(256), 255, LINEARBLEND);
}

// цвет «Огня v2» по жару 0..255: шесть уровней fire2.col равномерно по шкале, между ними линейный переход (целые числа)
static CRGB fire2Color(int heat) {
  int pos = constrain(heat, 0, 255) * (FIRE2_LEVELS - 1);
  int k = pos / 255, f = pos % 255;
  const byte* a = fire2.col[k];
  if (k >= FIRE2_LEVELS - 1) return CRGB(a[0], a[1], a[2]);
  const byte* b = fire2.col[k + 1];
  return CRGB(a[0] + ((int)b[0] - a[0]) * f / 255, a[1] + ((int)b[1] - a[1]) * f / 255, a[2] + ((int)b[2] - a[2]) * f / 255);
}

// цвет светодиода i для эффектов, которые считаются независимо по каждому светодиоду
static CRGB fxColor(byte e, int i, byte scale, uint16_t phase) {
  int nx, ny;
  fxPos(i, nx, ny);
  byte ph = phase;
  byte k = 1 + scale / 32;   // сколько волн укладывается на карту
  switch (e) {
    case FX_CHASE: {
      // «точки» бегут по диагонали: яркая только вершина волны, цвет плывёт по радуге
      byte d = (nx + ny) / 2;
      byte arg = d * k + ph;
      byte v = fxTri(arg);
      byte b = v > 160 ? (v - 160) * 255 / 95 : 0;
      return fxScale(ColorFromPalette(paletteArr[1], d + (phase >> 2), 255, LINEARBLEND), b);
    }
    case FX_FIRE: {
      // пламя поднимается вверх: шум движется навстречу росту Y, жара больше внизу, наверху гаснет.
      // По вертикали шум растянут втрое (языки пламени выше, как и должно быть у огня), а сдвиг по фазе втрое меньше,
      // так что скорость в единицах карты прежняя. Иначе при редких рядах (схема «спираль»: ряд на ячейку шума в 256 единиц)
      // узор между рядами сдвигается почти на целый период, и направление движения кажется случайным или обратным.
      int lift = 255 - ny;
      int noise = inoise8(xy[i][0] * scale / 10, xy[i][1] * scale / 30 + phase, 0);
      int heat = constrain(lift * 3 / 2 + noise - 200, 0, 255);
      return CRGB(min(heat * 3, 255), heat > 85 ? min((heat - 85) * 3, 255) : 0, heat > 170 ? (heat - 170) * 3 : 0);
    }
    case FX_FIRE2: {
      // огонь v2: тот же плавный шум, что у «Огня», три параметра формы и цвета уровней жара (fire2)
      int lift = 255 - ny;
      int noise = inoise8(xy[i][0] * scale / 10, xy[i][1] * scale / 30 + phase, 0);
      int heat = lift * 3 / 2 + noise - 200;
      // трепет: второй, более мелкий и быстрый шум, поверх плавного
      if (fire2.flicker) heat += (inoise8(xy[i][0] * scale / 5, xy[i][1] * scale / 15 + phase * 2, 77) - 128) * fire2.flicker >> 8;
      // колпак: к краям карты и к верху пламя гаснет сильнее
      if (fire2.shape) heat -= ((abs(nx - 128) * 2 * fire2.shape >> 8) * (ny + 64)) >> 8;
      heat = constrain(heat, 0, 255);
      CRGB c = fire2Color(heat);
      // искры: на каждом шаге времени (около 8 раз в секунду на средней скорости) светодиод над основанием вспыхивает с вероятностью до 3% (sparks = 255)
      if (fire2.sparks && ny > 40) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u ^ (uint32_t)(phase >> 4) * 2246822519u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        if ((h & 0x1FFF) * 255 < (uint32_t)fire2.sparks * 8192u / 32) {
          const byte* sp = fire2.col[FIRE2_LEVELS - 1];   // искра - цвет самого горячего уровня
          c.r = max(c.r, sp[0]);
          c.g = max(c.g, sp[1]);
          c.b = max(c.b, sp[2]);
        }
      }
      return c;
    }
    case FX_BREATH: {
      // вся гирлянда «дышит» одним цветом, который медленно плывёт по палитре; scale сдвигает фазу по карте
      byte arg = ph + (nx + ny) / 2 * scale / 64;
      byte b = 30 + fxEase(arg) * 225 / 255;
      return fxScale(ColorFromPalette(paletteArr[0], phase >> 3, 255, LINEARBLEND), b);
    }
    case FX_WAVE_Y: {
      byte v = fxEase(ny * k + ph);
      return fxScale(ColorFromPalette(paletteArr[3], v, 255, LINEARBLEND), 60 + v * 195 / 255);
    }
    default: {   // FX_WAVE_R: кольца расходятся от центра карты (расстояние - быстрое приближение без корня)
      int dx = abs(nx - 128), dy = abs(ny - 128);
      int r = min((max(dx, dy) + min(dx, dy) / 2) * 4 / 3, 255);
      byte v = fxEase(r * k - ph);
      return fxScale(ColorFromPalette(paletteArr[6], v, 255, LINEARBLEND), 60 + v * 195 / 255);
    }
  }
}

void effects() {
  static Timer effTmr(30);
  static uint16_t countP = 0;
  static byte countSkip = 0;
  static byte prevEff = 255;
  static byte fadeCount = 0;

  if (effTmr.ready()) {
    byte thisEffect;

    if (forceTmr.state()) thisEffect = forceEff;
    else thisEffect = curEff;

    // эффект сменился
    if (prevEff != curEff) {
      prevEff = curEff;
      fadeCount = 25;
    }

    byte scale = effAt(thisEffect).scale;
    byte speed = effAt(thisEffect).speed;
    byte curPal = thisEffect;
    if (curPal >= ACTIVE_PALETTES) curPal -= ACTIVE_PALETTES;

    int mmH = max((int)mm.h, 1);   // высота 0 (все лампы на одной линии) - деление на ноль
    bool ext = thisEffect >= ACTIVE_PALETTES * 2;
    if (ext && thisEffect - ACTIVE_PALETTES * 2 == FX_TWINKLE) fxTwinkle(scale, speed);
    else for (int i = 0; i < cfg.ledAm; i++) {
      if (ext) {
        CRGB c = fxColor(thisEffect - ACTIVE_PALETTES * 2, i, scale, countP);
        leds[i] = fadeCount ? blend(leds[i], c, 40) : c;   // плавная смена эффекта, как у остальных
        continue;
      }
      byte idx;

      if (thisEffect < ACTIVE_PALETTES) {
        // первые ACTIVE_PALETTES эффектов - градиент
        // idx = map(xy[i][1], mm.minY, mm.maxY, 0, 255) + counter;   // прямой градиент
        idx = countP + ((mm.w * xy[i][0] / mmH) + xy[i][1]) * scale / 100;   // диагональный градиент
      } else {
        // следующие - перлин нойс
        idx = inoise8(xy[i][0] * scale / 10, xy[i][1] * scale / 10, countP);
      }
      CRGB color = ColorFromPalette(paletteArr[curPal], idx, 255, LINEARBLEND);

      // плавная смена эффекта
      // меняется за 25 фреймов
      if (fadeCount) leds[i] = blend(leds[i], color, 40);
      else leds[i] = color;
    }
    if (fadeCount) fadeCount--;

    countP += (speed - 128) / 10;
    ledsShow(limitedBright(cfg.bright));
  }
}

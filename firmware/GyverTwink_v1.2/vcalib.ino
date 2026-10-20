// Калибровка по видео: гирлянда сама проигрывает последовательность, телефон снимает её на камеру,
// страница разбирает видео (web/app.js) и находит положение каждого светодиода.
//
// Время идёт от t0 (команда старт):
//   [0, delay)            ожидание: ничего не горит, пользователь включает запись видео
//   тёмная пауза          VC_DARK_MS: опорные кадры без света
//   маркер                VC_FLASH_MS: горит до VC_MARKER_LEDS светодиодов, равномерно по ленте (ток мал)
//   пауза                 VC_GAP_MS
//   серия                 светодиод i горит один, шаг step мс, подряд без промежутков
//   конец                 VC_END_MS темноты, потом эффекты возвращаются
// Страница находит на видео маркер, от него по известным интервалам вычисляет момент, когда горел каждый светодиод.
// Время считается по millis() от t0 (не накапливается), поэтому не уплывает.

#define VC_DARK_MS 1500
#define VC_FLASH_MS 600
#define VC_GAP_MS 500
#define VC_END_MS 1500
#define VC_MARKER_LEDS 25
#define VC_STEP_DEFAULT 120
#define VC_DELAY_DEFAULT 12

bool vcRun = false;
uint32_t vcT0 = 0;
uint16_t vcDelay = VC_DELAY_DEFAULT;   // секунд до начала
uint16_t vcStep = VC_STEP_DEFAULT;     // мс на светодиод
uint16_t vcCount = 0;                  // сколько светодиодов в серии
int vcFrame = -1;                      // что показано сейчас: 0 темно, 1 маркер, 2 пауза/конец, 3+i - светодиод i

// моменты (мс от t0)
static uint32_t vcFlashStart() { return vcDelay * 1000UL + VC_DARK_MS; }
static uint32_t vcSeqStart() { return vcFlashStart() + VC_FLASH_MS + VC_GAP_MS; }
static uint32_t vcTotal() { return vcSeqStart() + (uint32_t)vcCount * vcStep + VC_END_MS; }

static int vcFrameAt(uint32_t e) {
  if (e < vcFlashStart()) return 0;
  if (e < vcFlashStart() + VC_FLASH_MS) return 1;
  if (e < vcSeqStart()) return 2;
  uint32_t k = (e - vcSeqStart()) / vcStep;
  if (k < vcCount) return 3 + k;
  return 2;
}

void vcalibStart(uint16_t delaySec, uint16_t stepMs) {
  hlTmr.stop();
  vcDelay = delaySec;
  vcStep = stepMs;
  vcCount = cfg.ledAm;
  vcT0 = millis();
  vcFrame = -1;
  vcRun = true;
  calibF = true;   // эффекты на время серии отключены
  strip->clearLedData();
  strip->showLeds(0);
}

void vcalibStop() {
  if (!vcRun) return;
  vcRun = false;
  vcFrame = -1;
  calibF = false;
  strip->clearLedData();
  strip->showLeds(0);
}

static void vcRender(int frame) {
  strip->clearLedData();
  if (frame == 1) {
    int m = min((int)vcCount, VC_MARKER_LEDS);
    for (int k = 0; k < m; k++) leds[k * vcCount / m] = CRGB::White;
    strip->showLeds(limitedBright(255));   // лимит тока действует и здесь
  } else if (frame >= 3) {
    leds[frame - 3] = CRGB::White;
    strip->showLeds(limitedBright(255));
  } else {
    strip->showLeds(0);
  }
}

// вызывать из loop()
void vcalibTick() {
  if (!vcRun) return;
  uint32_t e = millis() - vcT0;
  if (e >= vcTotal()) {
    vcalibStop();
    return;
  }
  int f = vcFrameAt(e);
  if (f != vcFrame) {   // перерисовка только при смене кадра
    vcFrame = f;
    vcRender(f);
  }
}

// описание для страницы: текущее состояние и параметры серии (последней или следующей)
const char* vcPhaseName(uint32_t e) {
  if (e < vcDelay * 1000UL) return "wait";
  if (e < vcFlashStart()) return "dark";
  if (e < vcFlashStart() + VC_FLASH_MS) return "flash";
  if (e < vcSeqStart()) return "gap";
  if (e < vcSeqStart() + (uint32_t)vcCount * vcStep) return "seq";
  return "end";
}

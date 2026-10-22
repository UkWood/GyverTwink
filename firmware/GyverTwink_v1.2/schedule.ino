// Расписание: включение и выключение гирлянды по времени суток или относительно рассвета/заката.
//
// Время берётся из NTP (если гирлянда подключена к роутеру с интернетом) или задаётся со страницы (POST /api/time).
// Часы без батарейки: после перезагрузки время пропадает, пока не ответит NTP или страница не отправит его снова.
// Часовой пояс - постоянный сдвиг в минутах (sched.tz), на летнее время переходят вручную.
// Событие срабатывает один раз в свою минуту, а не "держит" состояние: ручное включение или выключение
// действует до следующего события. После скачка времени (синхронизация, ручная установка) пропущенные события не повторяются.

#ifndef GT_NOW   // в хост-тестах время подменяется (tests/host_env.h)
#define GT_NOW() ((uint32_t)time(nullptr))
#define GT_SET_TIME(s) do { struct timeval tv_ = {(time_t)(s), 0}; settimeofday(&tv_, nullptr); } while (0)
#define GT_NTP_BEGIN() configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com")
#endif

#define TIME_VALID_MIN 1700000000u   // 14.11.2023: часы с меньшим значением считаются не установленными
#define TIME_VALID_MAX 4102444800u   // 01.01.2100

static bool schedNtp = false;        // NTP запущен
static uint32_t schedLastMin = 0;    // последняя проверенная минута местного времени (0 - ещё не определена)

void schedBegin(bool ntp) {
  schedNtp = ntp;
  if (ntp) GT_NTP_BEGIN();
}

bool timeValid() {
  return GT_NOW() >= TIME_VALID_MIN;
}

// дни от 1970-01-01 -> дата (алгоритм Говарда Хиннанта), только целые числа
static void schedCivil(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = yoe + era * 400 + (m <= 2);
}

// дата -> дни от 1970-01-01
static int32_t schedDays(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = y - era * 400;
  uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// порядковый номер дня в году (1..366) для дня с начала эпохи
static int schedDayOfYear(int32_t day) {
  int y, m, d;
  schedCivil(day, y, m, d);
  return day - schedDays(y, 1, 1) + 1;
}

// минута местного времени (с начала эпохи) для времени UTC
static uint32_t schedLocalMinute(uint32_t utc) {
  return (uint32_t)(((int64_t)utc + (int32_t)sched.tz * 60) / 60);
}

// Рассвет или закат по формулам NOAA (точность около минуты). Возвращает минуты суток по местному времени
// или -1, если солнце в этот день не восходит или не заходит (полярный день и ночь).
// Здесь используется float: функция вызывается раз в минуту и по запросу страницы, на кадры это не влияет.
static int schedSunMin(int doy, bool rise) {
  const float lat = sched.lat / 100.0f * 0.017453293f;
  const float lon = sched.lon / 100.0f;
  float g = 6.2831853f * (doy - 1) / 365.0f;   // дробный год в радианах (полдень)
  float eqtime = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) - 0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g) + 0.000907f * sinf(2 * g) -
               0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  float cosH = -0.014544f / (cosf(lat) * cosf(decl)) - tanf(lat) * tanf(decl);   // -0.014544 = cos(90.833 градуса): центр солнца на 0.833 ниже горизонта
  if (!(cosH > -1.0f && cosH < 1.0f)) return -1;   // заодно отсекает NaN при широте +-90
  float ha = acosf(cosH) * 57.29578f;               // половина светового дня в градусах
  float utc = 720.0f - 4.0f * (lon + (rise ? ha : -ha)) - eqtime;   // минуты UTC
  int m = (int)floorf(utc + 0.5f) + sched.tz;
  return ((m % 1440) + 1440) % 1440;
}

// минута суток, когда срабатывает событие (-1 - сегодня его нет)
static int schedEventMin(byte t, int v, int doy) {
  if (t == SCHED_CLOCK) return v;
  int sun = schedSunMin(doy, t == SCHED_SUNRISE);
  return sun < 0 ? -1 : constrain(sun + v, 0, 1439);
}

// допустимо ли значение события для его типа
static bool schedValueOk(long t, long v) {
  return t == SCHED_CLOCK ? (v >= 0 && v <= 1439) : (v >= -240 && v <= 240);
}

// сегодняшние минуты включения и выключения (-1 - нет: расписание выключено, время не установлено или день не выбран)
static void schedToday(int& onAt, int& offAt) {
  onAt = offAt = -1;
  if (!sched.en || !timeValid()) return;
  int32_t day = schedLocalMinute(GT_NOW()) / 1440;
  if (!((sched.days >> ((day + 3) % 7)) & 1)) return;   // 1970-01-01 - четверг: (day + 3) % 7 = 0 для понедельника
  int doy = schedDayOfYear(day);
  onAt = schedEventMin(sched.onT, sched.onV, doy);
  offAt = schedEventMin(sched.offT, sched.offV, doy);
}

static void schedPower(bool on) {
  if (calibF || otaBusy) return;   // не мешаем калибровке и записи прошивки
  setPower(on);
}

// события одной минуты местного времени; при совпадении включения и выключения побеждает включение
static void schedCheckMinute(uint32_t localMin) {
  int32_t day = localMin / 1440;
  int mod = localMin % 1440;
  if (!((sched.days >> ((day + 3) % 7)) & 1)) return;
  int doy = schedDayOfYear(day);
  if (schedEventMin(sched.offT, sched.offV, doy) == mod) schedPower(false);
  if (schedEventMin(sched.onT, sched.onV, doy) == mod) schedPower(true);
}

void schedTick() {
  static Timer tmr(1000);
  if (!tmr.ready()) return;
  if (!sched.en || !timeValid()) {
    schedLastMin = 0;   // выключено или времени нет: при включении ничего не "догоняем"
    return;
  }
  uint32_t now = schedLocalMinute(GT_NOW());
  if (schedLastMin == 0 || now < schedLastMin || now - schedLastMin > 5) {   // первый раз или время прыгнуло: без повтора событий
    schedLastMin = now;
    return;
  }
  while (schedLastMin < now) schedCheckMinute(++schedLastMin);
}

// настройки изменены: запись в EEPROM и сброс отслеживания (события старой настройки не повторяются)
void schedChanged() {
  EEsched.update();
  schedLastMin = 0;
}

// установка времени со страницы (UTC, секунды)
void timeSet(uint32_t utc) {
  GT_SET_TIME(utc);
  schedLastMin = 0;
}

// источник времени: 0 - не установлено, 1 - NTP, 2 - со страницы
byte timeSource() {
  if (!timeValid()) return 0;
  return schedNtp ? 1 : 2;
}

// Тесты расписания (schedule.ino и его API). Подключается в конец test_firmware.cpp после test_web_cases.h.

// UTC-время для местной даты и времени (tzMin - сдвиг пояса в минутах)
static uint32_t epochAt(int y, int m, int d, int hh, int mi, int tzMin) {
  return (uint32_t)((int64_t)schedDays(y, m, d) * 86400 + hh * 3600 + mi * 60 - tzMin * 60);
}

static void schedReset() {
  sched = Sched();
  EEsched = EEManager(sched);
  fakeEpoch() = 0;
  fakeNtpOn() = false;
  schedNtp = false;
  schedLastMin = 0;
}

// пройти секунды реального времени: часы и таймер прошивки идут вместе
static void schedRun(int seconds) {
  for (int i = 0; i < seconds; i++) {
    fakeEpoch() += 1;
    fakeMillis() += 1000;
    schedTick();
  }
}

static void schedAt(int y, int m, int d, int hh, int mi, int ss = 0) {   // поставить часы (местное время sched.tz) и запомнить минуту
  fakeEpoch() = epochAt(y, m, d, hh, mi, sched.tz) + ss;
  fakeMillis() += 1000;
  schedTick();
}

TEST(sched_calendar_roundtrip_and_weekdays) {
  // туда и обратно для каждого дня 1970-2100 + известные даты
  for (int32_t day = 0; day < 47500; day++) {
    int y, m, d;
    schedCivil(day, y, m, d);
    CHECK_EQ(schedDays(y, m, d), day);
  }
  int y, m, d;
  schedCivil(20383, y, m, d);   // 2025-10-22 (среда)
  CHECK(y == 2025 && m == 10 && d == 22);
  CHECK_EQ(schedDayOfYear(schedDays(2026, 12, 31)), 365);
  CHECK_EQ(schedDayOfYear(schedDays(2028, 12, 31)), 366);   // високосный
  CHECK_EQ(schedDayOfYear(schedDays(2026, 1, 1)), 1);
  CHECK_EQ(schedDayOfYear(schedDays(2026, 3, 1)), 60);
  CHECK_EQ((schedDays(2026, 10, 22) + 3) % 7, 3);   // четверг: 0 - понедельник
  CHECK_EQ((schedDays(2026, 10, 19) + 3) % 7, 0);   // понедельник
}

// рассвет и закат: ожидания - справочные значения (допуск 6 минут: формулы NOAA точны около минуты, плюс округление таблиц)
static void sunCheck(int lat100, int lon100, int tz, int y, int m, int d, int riseMin, int setMin) {
  schedReset();
  sched.lat = lat100;
  sched.lon = lon100;
  sched.tz = tz;
  int doy = schedDayOfYear(schedDays(y, m, d));
  int r = schedSunMin(doy, true), s = schedSunMin(doy, false);
  CHECK(abs(r - riseMin) <= 6);
  CHECK(abs(s - setMin) <= 6);
  if (abs(r - riseMin) > 6 || abs(s - setMin) > 6) std::printf("    рассвет %d (ждали %d), закат %d (ждали %d)\n", r, riseMin, s, setMin);
}

TEST(sched_sunrise_and_sunset_match_reference_values) {
  sunCheck(5575, 3762, 180, 2026, 6, 21, 3 * 60 + 44, 21 * 60 + 18);     // Москва, летнее солнцестояние (UTC+3)
  sunCheck(5575, 3762, 180, 2026, 12, 21, 8 * 60 + 58, 15 * 60 + 58);    // Москва, зимнее
  sunCheck(5151, -13, 0, 2026, 12, 21, 8 * 60 + 4, 15 * 60 + 53);        // Лондон (UTC+0)
  sunCheck(-3387, 15121, 660, 2026, 12, 21, 5 * 60 + 41, 20 * 60 + 5);   // Сидней, южное полушарие (UTC+11)
  sunCheck(4000, -7400, -300, 2026, 3, 20, 6 * 60, 18 * 60 + 9);          // Нью-Йорк, равноденствие (7:00/19:09 по летнему, здесь UTC-5)
}

TEST(sched_polar_day_and_night_give_no_event) {
  schedReset();
  sched.lat = 7800;   // Шпицберген
  sched.lon = 1560;
  sched.tz = 60;
  CHECK_EQ(schedSunMin(schedDayOfYear(schedDays(2026, 6, 21)), true), -1);    // полярный день
  CHECK_EQ(schedSunMin(schedDayOfYear(schedDays(2026, 6, 21)), false), -1);
  CHECK_EQ(schedSunMin(schedDayOfYear(schedDays(2026, 12, 21)), true), -1);   // полярная ночь
  CHECK_EQ(schedEventMin(SCHED_SUNSET, -30, schedDayOfYear(schedDays(2026, 6, 21))), -1);
  sched.lat = 9000;   // полюс: без NaN и выхода за диапазон
  CHECK_EQ(schedSunMin(100, true), -1);
  sched.lat = -9000;
  CHECK_EQ(schedSunMin(300, false), -1);
}

TEST(sched_fixed_time_events_fire_once_at_their_minute) {
  resetAll();
  schedReset();
  sched.tz = 180;
  sched.en = 1;
  cfg.power = 0;
  schedAt(2026, 10, 22, 17, 58);   // первый тик только запоминает минуту
  schedRun(60);                    // 17:59
  CHECK(!cfg.power);
  schedRun(59);                    // 17:59:59
  CHECK(!cfg.power);
  schedRun(1);                     // 18:00:00 - включить
  CHECK(cfg.power);
  CHECK(EEcfg.updates > 0);
  cfg.power = 0;                   // ручное выключение: следующее срабатывание только в 23:00
  EEcfg.updates = 0;
  schedRun(3600);                  // до 19:00
  CHECK(!cfg.power);
  schedRun(4 * 3600 - 1);          // 22:59:59
  CHECK(!cfg.power);
  cfg.power = 1;
  schedRun(1);                     // 23:00:00 - выключить
  CHECK(!cfg.power);
  schedRun(19 * 3600);             // следующие сутки, 18:00:00: снова включение (серия секунд без пропусков)
  CHECK(cfg.power);
}

TEST(sched_days_mask_and_next_day) {
  resetAll();
  schedReset();
  sched.tz = 180;
  sched.en = 1;
  sched.days = 1 << 4;   // только пятница; 22 октября 2026 - четверг
  cfg.power = 0;
  schedAt(2026, 10, 22, 17, 59, 30);
  schedRun(60);          // 18:00 четверга: не день расписания
  CHECK(!cfg.power);
  schedAt(2026, 10, 23, 17, 59, 30);   // скачок на сутки вперёд: сначала запоминание минуты
  schedRun(60);          // 18:00 пятницы
  CHECK(cfg.power);
}

TEST(sched_does_not_replay_events_after_a_time_jump) {
  resetAll();
  schedReset();
  sched.tz = 0;
  sched.en = 1;
  cfg.power = 0;
  schedAt(2026, 10, 22, 17, 0);
  fakeEpoch() = epochAt(2026, 10, 22, 19, 30, 0);   // время прыгнуло через событие 18:00 (синхронизация NTP)
  fakeMillis() += 1000;
  schedTick();
  schedRun(120);
  CHECK(!cfg.power);   // 18:00 не "догоняется"
  // короткая задержка (цикл занят, например записью прошивки) - событие не теряется
  schedAt(2026, 10, 23, 17, 58);
  fakeEpoch() = epochAt(2026, 10, 23, 18, 1, 0);
  fakeMillis() += 1000;
  schedTick();
  CHECK(cfg.power);
  // время пошло назад - тоже без событий
  cfg.power = 0;
  fakeEpoch() = epochAt(2026, 10, 23, 17, 0, 0);
  fakeMillis() += 1000;
  schedTick();
  schedRun(5);
  CHECK(!cfg.power);
}

TEST(sched_is_idle_when_disabled_or_clock_not_set) {
  resetAll();
  schedReset();
  sched.tz = 0;
  cfg.power = 0;
  sched.en = 0;
  schedAt(2026, 10, 22, 17, 59, 30);
  schedRun(120);
  CHECK(!cfg.power);              // выключено
  sched.en = 1;
  fakeEpoch() = 1000;             // часы не установлены (до 2023 года)
  CHECK(!timeValid());
  for (int i = 0; i < 100; i++) {
    fakeMillis() += 1000;
    schedTick();
  }
  CHECK(!cfg.power);
  CHECK_EQ(schedLastMin, 0);
  int a, b;
  schedToday(a, b);
  CHECK(a == -1 && b == -1);
  // включили расписание, когда уже 19:00: ничего не "догоняем"
  fakeEpoch() = epochAt(2026, 10, 22, 19, 0, 0);
  fakeMillis() += 1000;
  schedTick();
  schedRun(300);
  CHECK(!cfg.power);
}

TEST(sched_sun_events_use_offsets) {
  resetAll();
  schedReset();
  sched.tz = 180;
  sched.lat = 5575;
  sched.lon = 3762;
  sched.en = 1;
  sched.onT = SCHED_SUNSET;
  sched.onV = -30;   // за полчаса до заката
  sched.offT = SCHED_SUNRISE;
  sched.offV = 15;   // через 15 минут после рассвета
  int doy = schedDayOfYear(schedDays(2026, 10, 22));
  int sunset = schedSunMin(doy, false), sunrise = schedSunMin(doy, true);
  CHECK(sunset > 0 && sunrise > 0);
  int onAt = sunset - 30, offAt = sunrise + 15;
  cfg.power = 0;
  schedAt(2026, 10, 22, offAt / 60, offAt % 60, 0);   // с утра: на рассвете выключать
  schedRun(2 * 60);
  cfg.power = 1;
  schedAt(2026, 10, 22, (offAt - 1) / 60, (offAt - 1) % 60, 30);   // снова за 30 с до события выключения
  schedRun(60);
  CHECK(!cfg.power);                                     // выключилось в sunrise + 15
  schedAt(2026, 10, 22, (onAt - 1) / 60, (onAt - 1) % 60, 30);
  schedRun(60);
  CHECK(cfg.power);                                      // включилось в sunset - 30
  int a, b;
  schedToday(a, b);
  CHECK(a == onAt && b == offAt);
}

TEST(sched_on_wins_when_on_and_off_coincide_and_events_skip_during_calibration) {
  resetAll();
  schedReset();
  sched.tz = 0;
  sched.en = 1;
  sched.onV = sched.offV = 18 * 60;
  cfg.power = 0;
  schedAt(2026, 10, 22, 17, 59, 30);
  schedRun(60);
  CHECK(cfg.power);   // включение сильнее
  cfg.power = 0;
  calibF = true;      // идёт калибровка: расписание не вмешивается
  schedAt(2026, 10, 23, 17, 59, 30);
  schedRun(60);
  CHECK(!cfg.power);
  calibF = false;
  otaBusy = true;
  schedAt(2026, 10, 24, 17, 59, 30);
  schedRun(60);
  CHECK(!cfg.power);
  otaBusy = false;
}

TEST(sched_sun_event_clamps_to_the_day) {
  resetAll();
  schedReset();
  sched.lat = 5575;
  sched.lon = 3762;
  sched.tz = 180;
  int doy = schedDayOfYear(schedDays(2026, 6, 21));
  CHECK_EQ(schedEventMin(SCHED_SUNSET, 240, doy), 1439);   // закат 21:18 + 4 часа - не уходит за сутки
  CHECK_EQ(schedEventMin(SCHED_SUNRISE, -240, doy), 0);
  CHECK_EQ(schedEventMin(SCHED_CLOCK, 777, doy), 777);
}

// ---------- API ----------
static Resp schedPost(const Args& a) { return webPost("/api/schedule", a); }

TEST(sched_api_get_reports_state_and_today) {
  resetAll();
  schedReset();
  webReady();
  Resp r = webGet("/api/schedule");
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "en"), 0);
  CHECK_EQ(jnum(r, "tz"), 0);
  CHECK_EQ(jnum(r, "days"), 127);
  CHECK_EQ(jnum(r, "onV"), 1080);
  CHECK_EQ(jnum(r, "offV"), 1380);
  CHECK_EQ(jnum(r, "synced"), 0);
  CHECK_EQ(jnum(r, "onAt"), -1);
  CHECK(r.body.find("\"src\":\"none\"") != std::string::npos);
  CHECK(r.body.size() < 300);

  sched.en = 1;
  sched.tz = 180;
  fakeEpoch() = epochAt(2026, 10, 22, 12, 0, 180);
  r = webGet("/api/schedule");
  CHECK_EQ(jnum(r, "synced"), 1);
  CHECK_EQ(jnum(r, "utc"), (long)fakeEpoch());
  CHECK_EQ(jnum(r, "onAt"), 1080);
  CHECK_EQ(jnum(r, "offAt"), 1380);
  CHECK(r.body.find("\"src\":\"manual\"") != std::string::npos);
  schedNtp = true;
  CHECK(webGet("/api/schedule").body.find("\"src\":\"ntp\"") != std::string::npos);
}

TEST(sched_api_worst_case_fits_the_buffer) {
  resetAll();
  schedReset();
  webReady();
  sched.en = 1;
  sched.tz = -720;
  sched.lat = -9000;
  sched.lon = -18000;
  sched.days = 127;
  sched.onT = sched.offT = SCHED_SUNRISE;
  sched.onV = sched.offV = -240;
  schedNtp = true;
  fakeEpoch() = 4102444799u;
  Resp r = webGet("/api/schedule");
  CHECK_EQ(r.code, 200);
  CHECK(r.body.size() < 290);
  CHECK(r.body.back() == '}');
  CHECK_EQ(jnum(r, "lon"), -18000);
  CHECK_EQ(jnum(r, "offV"), -240);
}

TEST(sched_api_post_applies_all_fields_and_saves) {
  resetAll();
  schedReset();
  webReady();
  Resp r = schedPost({{"en", "1"}, {"tz", "-210"}, {"lat", "-3387"}, {"lon", "15121"}, {"days", "31"}, {"onT", "1"}, {"onV", "-45"}, {"offT", "0"}, {"offV", "1410"}});
  CHECK_EQ(r.code, 200);
  CHECK(sched.en && sched.tz == -210 && sched.lat == -3387 && sched.lon == 15121 && sched.days == 31);
  CHECK(sched.onT == SCHED_SUNSET && sched.onV == -45 && sched.offT == SCHED_CLOCK && sched.offV == 1410);
  CHECK_EQ(jnum(r, "tz"), -210);
  CHECK_EQ(jnum(r, "onV"), -45);
  CHECK(EEsched.updates > 0);
  CHECK_EQ(schedLastMin, 0);
  // частичная правка не трогает остальное
  r = schedPost({{"days", "96"}});
  CHECK_EQ(r.code, 200);
  CHECK(sched.days == 96 && sched.tz == -210 && sched.onV == -45);
  // граничные допустимые значения
  CHECK_EQ(schedPost({{"tz", "840"}, {"lat", "9000"}, {"lon", "-18000"}, {"days", "0"}, {"onT", "0"}, {"onV", "1439"}, {"offT", "2"}, {"offV", "-240"}}).code, 200);
  CHECK(sched.tz == 840 && sched.lat == 9000 && sched.lon == -18000 && sched.days == 0 && sched.onV == 1439 && sched.offV == -240);
}

TEST(sched_api_post_is_strict_and_all_or_nothing) {
  resetAll();
  schedReset();
  webReady();
  const Sched before = sched;
  EEsched.updates = 0;
  const Args bad[] = {
      {{"en", "2"}},        {{"en", "x"}},       {{"en", ""}},        {{"tz", "841"}},     {{"tz", "-721"}},    {{"tz", "1.5"}},
      {{"tz", "--5"}},      {{"tz", "-"}},       {{"tz", "+5"}},      {{"lat", "9001"}},   {{"lat", "-9001"}},  {{"lon", "18001"}},
      {{"lon", "-18001"}},  {{"days", "128"}},   {{"days", "-1"}},    {{"onT", "3"}},      {{"offT", "-1"}},    {{"onV", "1440"}},
      {{"offV", "-1440"}},  {{"onV", "99999999"}}, {{"onT", "0"}, {"onV", "-5"}},                               // для времени суток смещение недопустимо
      {{"offT", "1"}, {"offV", "241"}},               {{"onT", "2"}, {"onV", "-241"}},
      {{"offT", "1"}},                                // тип сменился, а значение (1380) для заката недопустимо
      {{"tz", "60"}, {"lat", "oops"}},                // первое поле верное, второе нет: не применяется ничего
      {{"en", "1"}, {"days", "200"}},
  };
  for (auto& a : bad) {
    Resp r = schedPost(a);
    CHECK_EQ(r.code, 400);
    CHECK(r.body.find("\"error\"") != std::string::npos);
    CHECK(sched.en == before.en && sched.tz == before.tz && sched.lat == before.lat && sched.lon == before.lon);
    CHECK(sched.days == before.days && sched.onT == before.onT && sched.offT == before.offT && sched.onV == before.onV && sched.offV == before.offV);
  }
  CHECK_EQ(EEsched.updates, 0);
  // без заголовка страницы
  CHECK_EQ(webServer.test(HTTP_POST, "/api/schedule", {{"en", "1"}}, {}).code, 403);
  CHECK(!sched.en);
  CHECK_EQ(webGet("/api/time").code, 404);   // GET на POST-маршрут
}

TEST(sched_api_time_sets_the_clock_with_validation) {
  resetAll();
  schedReset();
  webReady();
  CHECK(!timeValid());
  Resp r = webPost("/api/time", {{"unix", "1792000000"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(fakeEpoch(), 1792000000u);
  CHECK(timeValid());
  CHECK_EQ(jnum(r, "synced"), 1);
  CHECK(r.body.find("\"src\":\"manual\"") != std::string::npos);
  schedLastMin = 77;
  webPost("/api/time", {{"unix", "1792003600"}});
  CHECK_EQ(schedLastMin, 0);   // скачок времени: отслеживание начинается заново
  const uint32_t t = fakeEpoch();
  const Args bad[] = {{}, {{"unix", ""}}, {{"unix", "abc"}}, {{"unix", "-1"}}, {{"unix", "1699999999"}}, {{"unix", "4102444801"}},
                      {{"unix", "12345678901"}}, {{"unix", "1792000000.5"}}, {{"unix", "0"}}, {{"unix", "99999999999999999999"}}};
  for (auto& a : bad) {
    CHECK_EQ(webPost("/api/time", a).code, 400);
    CHECK_EQ(fakeEpoch(), t);
  }
  CHECK_EQ(webPost("/api/time", {{"unix", "1700000000"}}).code, 200);   // границы допустимого
  CHECK_EQ(webPost("/api/time", {{"unix", "4102444800"}}).code, 200);
  CHECK_EQ(webServer.test(HTTP_POST, "/api/time", {{"unix", "1792000000"}}, {}).code, 403);
}

TEST(sched_ntp_only_when_connected) {
  resetAll();
  schedReset();
  schedBegin(false);
  CHECK(!fakeNtpOn() && !schedNtp);
  schedBegin(true);
  CHECK(fakeNtpOn() && schedNtp);
  CHECK_EQ(timeSource(), 0);          // NTP запущен, но время ещё не пришло
  fakeEpoch() = 1792000000u;
  CHECK_EQ(timeSource(), 1);
  schedNtp = false;
  CHECK_EQ(timeSource(), 2);
}

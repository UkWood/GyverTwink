// Тесты калибровки по видео (vcalib.ino): расписание кадров, ток, остановка, API.
// Подключается в конец test_firmware.cpp.

typedef CLEDController::Frame Frame;

// прогнать время от t0 шагом dt мс, вызывая vcalibTick() как loop(); возвращает кадры
static std::vector<Frame> vcRunFor(uint32_t ms, uint32_t dt = 5) {
  stripObj.record = true;
  stripObj.frames.clear();
  uint32_t t0 = fakeMillis();
  for (uint32_t t = 0; t <= ms; t += dt) {
    fakeMillis() = t0 + t;
    vcalibTick();
  }
  return stripObj.frames;
}
static Resp vcStart(const Args& a = {}) { return webPost("/api/vcalib/start", a); }

TEST(vcalib_schedule_dark_marker_gap_sequence_end) {
  resetAll();
  cfg.ledAm = 40;
  stripObj.setLeds(leds, 40);
  Resp r = vcStart({{"delay", "2"}, {"step", "100"}});
  CHECK_EQ(r.code, 200);
  CHECK(vcRun && calibF);
  const uint32_t t0 = vcT0;
  const uint32_t flash = 2000 + VC_DARK_MS;
  const uint32_t seq = flash + VC_FLASH_MS + VC_GAP_MS;
  const uint32_t total = seq + 40 * 100 + VC_END_MS;
  CHECK_EQ(jnum(r, "total"), total);
  std::vector<Frame> fr = vcRunFor(total + 100);

  // кадры по порядку: тьма (старт), маркер, пауза, 40 светодиодов, конец
  CHECK_EQ(fr.size(), 1 + 1 + 1 + 40 + 1 + 1);   // + остановка (вызов showLeds(0) в vcalibStop)
  CHECK(fr[0].lit.empty() && fr[0].bri == 0);
  // маркер: ровно в нужный момент, горит не больше VC_MARKER_LEDS светодиодов
  CHECK(fr[1].t - t0 >= flash && fr[1].t - t0 < flash + 10);
  CHECK_EQ(fr[1].lit.size(), VC_MARKER_LEDS < 40 ? VC_MARKER_LEDS : 40);
  CHECK(fr[1].bri > 0);
  // пауза после маркера
  CHECK(fr[2].t - t0 >= flash + VC_FLASH_MS && fr[2].lit.empty());
  // светодиод i: один, в своём слоте
  for (int i = 0; i < 40; i++) {
    const Frame& f = fr[3 + i];
    CHECK_EQ(f.lit.size(), 1);
    CHECK_EQ(f.lit[0], i);
    uint32_t want = seq + i * 100;
    CHECK(f.t - t0 >= want && f.t - t0 < want + 10);   // не позже одного шага опроса
  }
  // конец: темнота и возврат эффектов
  CHECK(fr[43].lit.empty());
  CHECK(!vcRun);
  CHECK(!calibF);
}

TEST(vcalib_marker_is_limited_even_on_a_long_strip) {
  resetAll();
  cfg.ledAm = LED_MAX;
  stripObj.setLeds(leds, LED_MAX);
  vcStart({{"delay", "0"}, {"step", "60"}});
  stripObj.record = true;
  stripObj.frames.clear();
  fakeMillis() = vcT0 + VC_DARK_MS + 10;   // внутри маркера
  vcalibTick();
  CHECK_EQ(stripObj.frames.size(), 1);
  const Frame& f = stripObj.frames[0];
  CHECK_EQ(f.lit.size(), VC_MARKER_LEDS);   // не 500: ток маркера мал
  for (size_t k = 1; k < f.lit.size(); k++) CHECK(f.lit[k] > f.lit[k - 1] + 10);   // равномерно по ленте
  CHECK(f.lit.front() == 0 && f.lit.back() > LED_MAX * 0.9);
  vcalibStop();
}

TEST(vcalib_respects_the_current_limit) {
  resetAll();
  cfg.ledAm = 100;
  stripObj.setLeds(leds, 100);
  pwr.maxMa = 60;   // очень слабый блок питания
  vcStart({{"delay", "0"}});
  stripObj.record = true;
  stripObj.frames.clear();
  fakeMillis() = vcT0 + VC_DARK_MS + 10;
  vcalibTick();
  CHECK(stripObj.frames.size() == 1 && stripObj.frames[0].bri < 255);
  vcalibStop();
}

TEST(vcalib_stop_restores_effects_and_darkens) {
  resetAll();
  cfg.ledAm = 20;
  stripObj.setLeds(leds, 20);
  vcStart({{"delay", "0"}, {"step", "100"}});
  fakeMillis() = vcT0 + VC_DARK_MS + VC_FLASH_MS + VC_GAP_MS + 350;   // посреди серии
  vcalibTick();
  CHECK(calibF && leds[3] == CRGB(CRGB::White));
  Resp r = webPost("/api/vcalib/stop");
  CHECK_EQ(r.code, 200);
  CHECK(!vcRun && !calibF);
  for (int i = 0; i < 20; i++) CHECK(leds[i] == CRGB());
  CHECK_EQ(stripObj.shown.back(), 0);
  CHECK_EQ(jnum(r, "run"), 0);
  CHECK_EQ(webPost("/api/vcalib/stop").code, 200);   // повторная остановка безвредна
}

TEST(vcalib_api_validation_status_and_busy) {
  resetAll();
  cfg.ledAm = 30;
  const Args bad[] = {{{"delay", "121"}}, {{"delay", "-1"}}, {{"delay", "x"}}, {{"step", "59"}}, {{"step", "1001"}}, {{"step", ""}}};
  for (auto& a : bad) {
    CHECK_EQ(vcStart(a).code, 400);
    CHECK(!vcRun && !calibF);
  }
  CHECK_EQ(webServer.test(HTTP_POST, "/api/vcalib/start", {}, {}).code, 403);
  CHECK(!vcRun);
  // состояние до запуска: параметры следующей серии
  Resp r = webGet("/api/vcalib");
  CHECK_EQ(jnum(r, "run"), 0);
  CHECK(r.body.find("\"phase\":\"idle\"") != std::string::npos);
  CHECK_EQ(jnum(r, "n"), 30);
  CHECK_EQ(jnum(r, "flash"), VC_FLASH_MS);
  CHECK_EQ(jnum(r, "marker"), VC_MARKER_LEDS);

  r = vcStart({{"delay", "3"}, {"step", "150"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "run"), 1);
  CHECK_EQ(jnum(r, "step"), 150);
  CHECK_EQ(jnum(r, "delay"), 3);
  CHECK_EQ(jnum(r, "n"), 30);
  CHECK_EQ(vcStart().code, 409);                    // уже идёт
  CHECK(vcStep == 150 && vcDelay == 3);             // повторный старт параметры не трогает

  struct { uint32_t at; const char* phase; int i; } probes[] = {
    {1000, "wait", -1}, {3000 + 100, "dark", -1}, {3000 + VC_DARK_MS + 100, "flash", -1},
    {3000 + VC_DARK_MS + VC_FLASH_MS + 100, "gap", -1}, {3000 + VC_DARK_MS + VC_FLASH_MS + VC_GAP_MS + 150 * 5 + 10, "seq", 5},
    {3000 + VC_DARK_MS + VC_FLASH_MS + VC_GAP_MS + 150 * 30 + 100, "end", -1},
  };
  for (auto& p : probes) {
    fakeMillis() = vcT0 + p.at;
    vcalibTick();
    Resp s = webGet("/api/vcalib");
    CHECK(s.body.find(std::string("\"phase\":\"") + p.phase + "\"") != std::string::npos);
    CHECK_EQ(jnum(s, "i"), p.i);
    CHECK_EQ(jnum(s, "e"), p.at);
  }
  vcalibStop();
}

TEST(vcalib_overrides_editor_highlight) {
  resetAll();
  cfg.ledAm = 10;
  stripObj.setLeds(leds, 10);
  webPost("/api/led", {{"i", "4"}});
  CHECK(hlTmr.state());
  vcStart({{"delay", "0"}});
  CHECK(!hlTmr.state());   // автоотключение подсветки не вмешивается в серию
  fakeMillis() += 61000;
  webTick();
  CHECK(calibF && vcRun);
  vcalibStop();
}

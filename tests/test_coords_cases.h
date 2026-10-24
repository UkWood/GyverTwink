// Тесты редактора координат: GET/POST /api/xy, подсветка светодиода /api/led, recalcBounds.
// Подключается в конец test_firmware.cpp.

static std::string hex2(int v) {
  char b[3];
  snprintf(b, sizeof(b), "%02x", v & 255);
  return b;
}
static Resp xyPost(const std::string& n, const std::string& body, bool header = true) {
  webReady();
  return webServer.test(HTTP_POST, "/api/xy", {{"n", n}}, header ? kHdr : Args{}, body);
}

TEST(coords_get_format) {
  resetAll();
  cfg.ledAm = 3;
  const uint8_t pts[3][2] = {{10, 40}, {50, 20}, {255, 0}};
  memcpy(xy, pts, sizeof(pts));
  xy[3][0] = 77;   // за пределами ledAm: в ответ не попадает
  mm.w = 245;
  mm.h = 40;
  Resp r = webGet("/api/xy");
  CHECK_EQ(r.code, 200);
  CHECK(r.type == "application/json");
  CHECK(r.body == "{\"n\":3,\"w\":245,\"h\":40,\"xy\":\"0a283214ff00\"}");
}

TEST(coords_post_applies_and_recalculates_bounds) {
  resetAll();
  cfg.ledAm = 4;
  xy[4][0] = 99;   // светодиод за пределами ledAm не затрагивается
  Resp r = xyPost("4", "0a28" "3214" "1e5a" "1440");
  CHECK_EQ(r.code, 200);
  const uint8_t want[4][2] = {{10, 40}, {50, 20}, {30, 90}, {20, 64}};
  CHECK(memcmp(xy, want, sizeof(want)) == 0);
  CHECK_EQ(xy[4][0], 99);
  CHECK_EQ(mm.minX, 10);
  CHECK_EQ(mm.maxX, 50);
  CHECK_EQ(mm.minY, 20);
  CHECK_EQ(mm.maxY, 90);
  CHECK_EQ(mm.w, 40);
  CHECK_EQ(mm.h, 70);
  CHECK_EQ(jnum(r, "w"), 40);
  CHECK_EQ(jnum(r, "h"), 70);
  CHECK(EExy.nows > 0 && EEmm.nows > 0);   // сохранено в EEPROM
}

// Координаты хранятся двумя блоками EEPROM: исторический (первые XY_BASE светодиодов, его ключ и размер менять нельзя) и xyHi
TEST(coords_second_eeprom_block_is_written_only_for_more_than_xy_base_leds) {
  resetAll();
  CHECK_EQ(XY_BASE, 500);                    // исторический размер блока: от него зависит ключ и адреса всех блоков за ним
  CHECK_EQ((uint8_t)XY_BASE, 244);           // ключ блоков mm и xy, как было при LED_MAX = 500
  CHECK(LED_MAX > XY_BASE);
  CHECK((void*)&xy[XY_BASE] == (void*)((uint8_t*)xy + 2 * XY_BASE));   // массив в ОЗУ один и непрерывный, блоки - его части
  EExy.nows = EExyHi.nows = 0;
  cfg.ledAm = 40;
  std::string few;
  for (int i = 0; i < 40; i++) few += hex2(i) + hex2(2 * i);
  CHECK_EQ(xyPost("40", few).code, 200);
  CHECK(EExy.nows > 0);
  CHECK_EQ(EExyHi.nows, 0);                  // светодиодов не больше XY_BASE: второй блок не трогаем
  cfg.ledAm = LED_MAX;
  std::string all;
  for (int i = 0; i < LED_MAX; i++) all += hex2(i) + hex2(i / 4);
  CHECK_EQ(xyPost(std::to_string(LED_MAX), all).code, 200);
  CHECK(EExyHi.nows > 0);
  CHECK_EQ(xy[XY_BASE][0], XY_BASE & 255);   // светодиод за границей блока пишется и читается как все
  CHECK_EQ(xy[LED_MAX - 1][1], ((LED_MAX - 1) / 4) & 255);
}

TEST(coords_post_accepts_uppercase_hex_and_full_strip) {
  resetAll();
  cfg.ledAm = LED_MAX;
  std::string body;
  for (int i = 0; i < LED_MAX; i++) body += hex2(i * 7) + hex2(255 - i / 3);
  CHECK_EQ(body.size(), 4 * LED_MAX);
  CHECK_EQ(xyPost(std::to_string(LED_MAX), body).code, 200);
  CHECK_EQ(xy[LED_MAX - 1][0], (LED_MAX - 1) * 7 & 255);
  Resp g = webGet("/api/xy");
  CHECK(g.body.find(body) != std::string::npos);   // чтение возвращает то, что записали
  std::string up = body;
  for (auto& c : up) c = (char)toupper(c);
  CHECK_EQ(xyPost(std::to_string(LED_MAX), up).code, 200);
}

TEST(coords_post_rejects_bad_requests_and_changes_nothing) {
  resetAll();
  cfg.ledAm = 2;
  xy[0][0] = 1;
  xy[1][1] = 2;
  const std::string ok = "01020304";
  struct Case {
    std::string n, body;
    int code;
    const char* err;
  };
  const Case cases[] = {
    {"2", "010203", 400, "xy"},          // короче
    {"2", "0102030405", 400, "xy"},      // длиннее
    {"2", "0102030g", 400, "xy"},        // не hex
    {"2", "0102 304", 400, "xy"},        // пробел
    {"2", "", 400, "xy"},                // пустое тело
    {"3", "010203040506", 409, "count"},   // число светодиодов изменилось
    {"1", "01020304", 409, "count"},
    {"0", ok, 400, "n"},
    {"1001", ok, 400, "n"},
    {"x", ok, 400, "n"},
    {"-2", ok, 400, "n"},
  };
  for (auto& c : cases) {
    Resp r = xyPost(c.n, c.body);
    CHECK_EQ(r.code, c.code);
    CHECK(r.body.find(std::string("\"error\":\"") + c.err + "\"") != std::string::npos);
  }
  CHECK_EQ(webServer.test(HTTP_POST, "/api/xy", {}, kHdr, ok).code, 400);   // нет n
  CHECK_EQ(xyPost("2", ok, false).code, 403);                                 // нет заголовка страницы
  CHECK_EQ(xy[0][0], 1);
  CHECK_EQ(xy[1][1], 2);
  CHECK_EQ(xy[0][1], 0);
  CHECK_EQ(EExy.nows, 0);
  CHECK_EQ(EEmm.nows, 0);
}

TEST(coords_bounds_with_zero_height) {
  resetAll();
  cfg.ledAm = 3;
  CHECK_EQ(xyPost("3", "0a05" "1405" "1e05").code, 200);   // все на одной линии
  CHECK_EQ(mm.h, 0);
  CHECK_EQ(mm.w, 20);
  curEff = 0;
  forceTmr.stop();
  fakeMillis() += 40;
  effects();   // с h == 0 не должно быть деления на ноль
}

TEST(led_highlight_lights_exactly_one_led) {
  resetAll();
  cfg.ledAm = 10;
  stripObj.setLeds(leds, 10);
  Resp r = webPost("/api/led", {{"i", "5"}});
  CHECK_EQ(r.code, 200);
  CHECK(calibF);
  CHECK(hlTmr.state());
  for (int i = 0; i < LED_MAX; i++) CHECK(leds[i] == (i == 5 ? CRGB(CRGB::White) : CRGB()));
  CHECK_EQ(stripObj.shown.back(), 200);
  webPost("/api/led", {{"i", "7"}});   // перенос подсветки: прежняя гаснет
  CHECK(leds[5] == CRGB() && leds[7] == CRGB(CRGB::White));
  webPost("/api/led", {{"i", "9"}});   // последний светодиод допустим
  CHECK(leds[9] == CRGB(CRGB::White));
  webPost("/api/led", {{"i", "0"}});   // первый тоже
  CHECK(leds[0] == CRGB(CRGB::White));

  r = webPost("/api/led", {{"off", "1"}});
  CHECK_EQ(r.code, 200);
  CHECK(!calibF);
  CHECK(!hlTmr.state());
  for (int i = 0; i < LED_MAX; i++) CHECK(leds[i] == CRGB());
  CHECK_EQ(stripObj.shown.back(), 0);
}

TEST(led_highlight_validation) {
  resetAll();
  cfg.ledAm = 10;
  const Args bad[] = {{}, {{"i", "10"}}, {{"i", "-1"}}, {{"i", "x"}}, {{"i", ""}}, {{"off", "0"}}, {{"off", "2"}}, {{"i", "3"}, {"off", "1"}}};
  for (auto& a : bad) {
    CHECK_EQ(webPost("/api/led", a).code, 400);
    CHECK(!calibF);
  }
  CHECK_EQ(webServer.test(HTTP_POST, "/api/led", {{"i", "1"}}, {}).code, 403);
  CHECK(!calibF);
  CHECK_EQ(webGet("/api/led").code, 404);
}

TEST(led_highlight_turns_off_by_itself) {
  resetAll();
  cfg.ledAm = 10;
  webPost("/api/led", {{"i", "2"}});
  fakeMillis() += 59000;
  webTick();
  CHECK(calibF);                       // минута ещё не прошла
  webPost("/api/led", {{"i", "3"}});   // новая подсветка продлевает срок
  fakeMillis() += 59000;
  webTick();
  CHECK(calibF && leds[3] == CRGB(CRGB::White));
  fakeMillis() += 2000;
  webTick();
  CHECK(!calibF);
  CHECK(leds[3] == CRGB());
  CHECK_EQ(stripObj.shown.back(), 0);
  CHECK(!hlTmr.state());
}

TEST(udp_calibration_still_computes_bounds_through_recalc) {
  resetAll();
  cfg.ledAm = 3;
  const uint8_t pts[3][2] = {{5, 100}, {60, 20}, {30, 50}};
  memcpy(xy, pts, sizeof(pts));
  send({3, 0});
  send({3, 2});
  CHECK_EQ(mm.minX, 5);
  CHECK_EQ(mm.maxX, 60);
  CHECK_EQ(mm.minY, 20);
  CHECK_EQ(mm.maxY, 100);
  CHECK_EQ(mm.w, 55);
  CHECK_EQ(mm.h, 80);
  CHECK(EExy.nows > 0 && EEmm.nows > 0);
}

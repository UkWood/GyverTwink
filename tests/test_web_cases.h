// Тесты веб-интерфейса: настоящие обработчики web.ino вызываются через ESP8266WebServer::test() без сети.
// Подключается в конец test_firmware.cpp.

typedef ESP8266WebServer::Response Resp;
typedef std::map<std::string, std::string> Args;

static bool webRoutesReady = false;
static void webReady() {
  if (!webRoutesReady) {
    webBegin();
    webRoutesReady = true;
  }
}

static const Args kHdr = {{"X-Requested-With", "GyverTwink"}};

static Resp webGet(const std::string& uri, const Args& args = {}) {
  webReady();
  return webServer.test(HTTP_GET, uri, args, {});
}
static Resp webPost(const std::string& uri, const Args& args = {}) {
  webReady();
  return webServer.test(HTTP_POST, uri, args, kHdr);
}
// значение числового поля из плоского JSON
static long jnum(const Resp& r, const char* key) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = r.body.find(k);
  return p == std::string::npos ? -999999 : atol(r.body.c_str() + p + k.size());
}

TEST(web_root_serves_gzip_page) {
  resetAll();
  Resp r = webGet("/");
  CHECK_EQ(r.code, 200);
  CHECK(r.type == "text/html");
  CHECK(r.headers["Content-Encoding"] == "gzip");
  CHECK_EQ(r.body.size(), WEB_PAGE_GZ_LEN);
  CHECK(r.body.size() > 100 && (uint8_t)r.body[0] == 0x1f && (uint8_t)r.body[1] == 0x8b);
}

TEST(web_state_reflects_settings) {
  resetAll();
  cfg.ledAm = 123;
  cfg.power = 1;
  cfg.bright = 77;
  cfg.autoCh = 1;
  cfg.rndCh = 0;
  cfg.prdCh = 4;
  cfg.turnOff = 1;
  cfg.offTmr = 33;
  pwr.maxMa = 2500;
  curEff = 9;
  Resp r = webGet("/api/state");
  CHECK_EQ(r.code, 200);
  CHECK(r.type == "application/json");
  CHECK_EQ(jnum(r, "ledAm"), 123);
  CHECK_EQ(jnum(r, "ledMax"), LED_MAX);
  CHECK_EQ(jnum(r, "power"), 1);
  CHECK_EQ(jnum(r, "bright"), 77);
  CHECK_EQ(jnum(r, "auto"), 1);
  CHECK_EQ(jnum(r, "rnd"), 0);
  CHECK_EQ(jnum(r, "period"), 4);
  CHECK_EQ(jnum(r, "offOn"), 1);
  CHECK_EQ(jnum(r, "offMin"), 33);
  CHECK_EQ(jnum(r, "maxMa"), 2500);
  CHECK_EQ(jnum(r, "cur"), 9);
  CHECK_EQ(jnum(r, "count"), EFFECT_COUNT);
  CHECK(r.body.find("\"fw\":\"host\"") != std::string::npos);
}

TEST(web_post_requires_the_page_header) {
  resetAll();
  webReady();
  Resp r = webServer.test(HTTP_POST, "/api/set", {{"bright", "5"}}, {});
  CHECK_EQ(r.code, 403);
  r = webServer.test(HTTP_POST, "/api/set", {{"bright", "5"}}, {{"X-Requested-With", "evil"}});
  CHECK_EQ(r.code, 403);
  r = webServer.test(HTTP_POST, "/api/next", {}, {});
  CHECK_EQ(r.code, 403);
  r = webServer.test(HTTP_POST, "/api/effect", {{"n", "1"}, {"scale", "9"}}, {});
  CHECK_EQ(r.code, 403);
  CHECK_EQ(cfg.bright, 100);
  CHECK_EQ(curEff, 0);
  CHECK_EQ(effs[1].scale, 50);
}

TEST(web_set_applies_every_parameter) {
  resetAll();
  forceTmr.restart();
  Resp r = webPost("/api/set", {{"ledAm", "200"}, {"power", "0"}, {"bright", "180"}, {"auto", "1"}, {"rnd", "1"},
                                {"period", "7"}, {"offOn", "1"}, {"offMin", "45"}, {"maxMa", "3000"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(cfg.ledAm, 200);
  CHECK_EQ(stripObj.count, 200);
  CHECK(!cfg.power);
  CHECK_EQ(cfg.bright, 180);
  CHECK(cfg.autoCh && cfg.rndCh);
  CHECK_EQ(cfg.prdCh, 7);
  CHECK(switchTmr.state());
  CHECK(cfg.turnOff);
  CHECK(offTmr.state());
  CHECK_EQ(cfg.offTmr, 45);
  CHECK_EQ(pwr.maxMa, 3000);
  CHECK(EEcfg.updates > 0 && EEpwr.updates > 0);
  CHECK(!forceTmr.state());              // любое изменение настроек останавливает показ эффекта
  CHECK_EQ(stripObj.shown.back(), 0);    // выключено - лента погашена
  CHECK_EQ(jnum(r, "bright"), 180);      // в ответе уже новое состояние
}

TEST(web_set_rejects_bad_values_and_changes_nothing) {
  resetAll();
  const char* bad[][2] = {{"bright", "256"}, {"bright", "-1"}, {"bright", "abc"}, {"bright", ""}, {"bright", "1.5"},
                          {"ledAm", "0"},    {"ledAm", "1001"}, {"period", "0"},   {"offMin", "0"}, {"offMin", "251"},
                          {"maxMa", "25501"}, {"power", "2"},  {"auto", "x"},     {"bright", "1234567"}};
  for (auto& b : bad) {
    // вместе с неверным значением идёт допустимое: оно не должно примениться
    bool isMa = std::string(b[0]) == "maxMa";
    Resp r = webPost("/api/set", {{isMa ? "period" : "maxMa", isMa ? "3" : "100"}, {b[0], b[1]}});
    CHECK_EQ(r.code, 400);
    CHECK(r.body.find(std::string("\"error\":\"") + b[0] + "\"") != std::string::npos);
    CHECK_EQ(pwr.maxMa, 0);
    CHECK_EQ(cfg.prdCh, 1);
  }
  CHECK_EQ(cfg.bright, 100);
  CHECK_EQ(cfg.ledAm, 50);
  CHECK_EQ(EEcfg.updates, 0);
}

TEST(web_set_boundaries_are_accepted) {
  resetAll();
  CHECK_EQ(webPost("/api/set", {{"bright", "0"}}).code, 200);
  CHECK_EQ(webPost("/api/set", {{"bright", "255"}}).code, 200);
  CHECK_EQ(cfg.bright, 255);
  CHECK_EQ(webPost("/api/set", {{"ledAm", "1"}}).code, 200);
  CHECK_EQ(cfg.ledAm, 1);
  CHECK_EQ(webPost("/api/set", {{"ledAm", "500"}}).code, 200);
  CHECK_EQ(cfg.ledAm, 500);
  CHECK_EQ(webPost("/api/set", {{"maxMa", "25500"}}).code, 200);
  CHECK_EQ(pwr.maxMa, 25500);
  CHECK_EQ(webPost("/api/set", {{"maxMa", "0"}}).code, 200);
  CHECK_EQ(pwr.maxMa, 0);
  CHECK_EQ(webPost("/api/set").code, 200);   // пустой запрос допустим и ничего не меняет
}

TEST(web_and_udp_settings_behave_the_same) {
  // каждый параметр: тот же результат по UDP-команде {2, sub, ...}
  struct Case {
    const char* key;
    int sub;
    int val;
  };
  const Case cases[] = {{"power", 1, 0}, {"bright", 2, 99}, {"auto", 3, 1}, {"rnd", 4, 1},
                        {"period", 5, 6}, {"offOn", 7, 1}, {"offMin", 8, 12}};
  for (auto& c : cases) {
    resetAll();
    send({2, c.sub, c.val});
    Cfg viaUdp = cfg;
    bool swUdp = switchTmr.state(), offUdp = offTmr.state();
    resetAll();
    CHECK_EQ(webPost("/api/set", {{c.key, std::to_string(c.val)}}).code, 200);
    CHECK(memcmp(&viaUdp, &cfg, sizeof(Cfg)) == 0);
    CHECK_EQ(swUdp, switchTmr.state());
    CHECK_EQ(offUdp, offTmr.state());
  }
}

TEST(web_next_switches_effect) {
  resetAll();
  for (auto& e : effs) e.fav = false;
  effs[3].fav = effs[8].fav = true;
  curEff = 0;
  forceTmr.restart();
  Resp r = webPost("/api/next");
  CHECK_EQ(r.code, 200);
  CHECK_EQ(curEff, 3);
  CHECK_EQ(jnum(r, "cur"), 3);
  CHECK(!forceTmr.state());
  webPost("/api/next");
  CHECK_EQ(curEff, 8);
}

TEST(web_effect_get_and_edit) {
  resetAll();
  effs[5].fav = false;
  effs[5].scale = 11;
  effs[5].speed = 22;
  Resp r = webGet("/api/effect", {{"n", "5"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "n"), 5);
  CHECK_EQ(jnum(r, "fav"), 0);
  CHECK_EQ(jnum(r, "scale"), 11);
  CHECK_EQ(jnum(r, "speed"), 22);
  CHECK(!forceTmr.state());   // просмотр не включает показ

  r = webPost("/api/effect", {{"n", "5"}, {"fav", "1"}, {"scale", "99"}, {"speed", "77"}});
  CHECK_EQ(r.code, 200);
  CHECK(effs[5].fav);
  CHECK_EQ(effs[5].scale, 99);
  CHECK_EQ(effs[5].speed, 77);
  CHECK_EQ(jnum(r, "scale"), 99);
  CHECK(EEeff.updates > 0);
  CHECK(!forceTmr.state());   // без show показ не включается
  CHECK_EQ(effs[4].scale, 50);   // соседние эффекты не тронуты
}

TEST(web_effect_show_makes_it_current_and_state_reports_it) {
  resetAll();
  curEff = 3;
  CHECK_EQ(jnum(webGet("/api/state"), "cur"), 3);
  Resp r = webPost("/api/effect", {{"n", "24"}, {"show", "1"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(webGet("/api/state"), "cur"), 24);   // выбранный эффект сразу становится текущим
  CHECK_EQ(curEff, 24);
  CHECK(!forceTmr.state());                          // без временного показа
  forceTmr.restart();                                // показ по UDP (приложение) тоже виден в «Сейчас»
  forceEff = 5;
  CHECK_EQ(jnum(webGet("/api/state"), "cur"), 5);
}

TEST(web_new_effects_use_their_own_settings_block) {
  resetAll();
  Resp r = webGet("/api/effect", {{"n", std::to_string(ACTIVE_PALETTES * 2 + 2)}});   // огонь
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "n"), ACTIVE_PALETTES * 2 + 2);
  CHECK_EQ(jnum(r, "fav"), 1);   // новые эффекты по умолчанию в избранном (участвуют в переборе)
  CHECK_EQ(jnum(r, "scale"), effsX[2].scale);
  CHECK_EQ(jnum(r, "speed"), effsX[2].speed);

  Effects before[ACTIVE_PALETTES * 2];
  memcpy(before, effs, sizeof(effs));
  r = webPost("/api/effect", {{"n", std::to_string(FIRE2_EFF - 1)}, {"fav", "1"}, {"scale", "123"}, {"speed", "45"}, {"show", "1"}});
  CHECK_EQ(r.code, 200);
  CHECK(effsX[EXT_EFFECTS - 1].fav);
  CHECK_EQ(effsX[EXT_EFFECTS - 1].scale, 123);
  CHECK_EQ(effsX[EXT_EFFECTS - 1].speed, 45);
  CHECK_EQ(curEff, FIRE2_EFF - 1);
  CHECK(EEfx.updates > 0);
  CHECK_EQ(EEeff.updates, 0);                        // блок старых эффектов не записывался
  CHECK(memcmp(before, effs, sizeof(effs)) == 0);
  CHECK_EQ(webPost("/api/effect", {{"n", std::to_string(EFFECT_COUNT)}, {"scale", "1"}}).code, 400);
}

TEST(web_fire2_has_shape_and_level_colours_in_its_own_block) {
  resetAll();
  Resp r = webGet("/api/effect", {{"n", std::to_string(FIRE2_EFF)}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "shape"), 120);
  CHECK_EQ(jnum(r, "sparks"), 80);
  CHECK_EQ(jnum(r, "flicker"), 100);
  CHECK_EQ(jnum(r, "fav"), 1);
  CHECK(r.body.find("\"white\"") == std::string::npos);                      // белого огня больше нет
  CHECK(r.body.find("\"cols\":\"000000460600be1200ff4600ff8c08ffcd3c\"") != std::string::npos);   // шесть уровней по умолчанию (rrggbb)
  CHECK_EQ(jnum(webGet("/api/effect", {{"n", "2"}}), "shape"), -999999);   // у обычного эффекта этих полей нет
  Effects before[ACTIVE_PALETTES * 2];
  memcpy(before, effs, sizeof(effs));
  r = webPost("/api/effect", {{"n", std::to_string(FIRE2_EFF)}, {"shape", "7"}, {"sparks", "255"}, {"flicker", "0"}, {"scale", "33"}});
  CHECK_EQ(r.code, 200);
  CHECK(fire2.shape == 7 && fire2.sparks == 255 && fire2.flicker == 0);
  CHECK_EQ(fire2.e.scale, 33);
  CHECK(EEfire2.updates > 0);
  CHECK_EQ(EEfx.updates, 0);   // блок остальных новых эффектов не записывался
  CHECK_EQ(EEeff.updates, 0);
  CHECK(memcmp(before, effs, sizeof(effs)) == 0);
  // цвет уровня: lv и любые из r, g, b - меняется только указанное
  r = webPost("/api/effect", {{"n", std::to_string(FIRE2_EFF)}, {"lv", "2"}, {"g", "99"}});
  CHECK_EQ(r.code, 200);
  CHECK(fire2.col[2][0] == 190 && fire2.col[2][1] == 99 && fire2.col[2][2] == 0);
  r = webPost("/api/effect", {{"n", std::to_string(FIRE2_EFF)}, {"lv", "5"}, {"r", "1"}, {"g", "2"}, {"b", "3"}});
  CHECK(r.body.find("\"cols\":\"") != std::string::npos && r.body.find("010203\"") != std::string::npos);
  CHECK(fire2.col[5][0] == 1 && fire2.col[5][1] == 2 && fire2.col[5][2] == 3);
  CHECK(fire2.col[4][0] == 255);   // соседние уровни не тронуты
  // ошибки: всё или ничего
  const Args bad[] = {{{"shape", "256"}}, {{"sparks", "-1"}}, {{"flicker", "x"}}, {{"lv", "6"}, {"r", "1"}}, {{"lv", "1"}, {"b", "256"}},
                      {{"r", "5"}}, {{"g", "5"}}};   // цвет без номера уровня - ошибка
  for (Args a : bad) {
    a["n"] = std::to_string(FIRE2_EFF);
    a["scale"] = "99";
    CHECK_EQ(webPost("/api/effect", a).code, 400);
  }
  CHECK_EQ(fire2.e.scale, 33);   // при ошибке ничего не применилось
  CHECK_EQ(webPost("/api/effect", {{"n", "2"}, {"shape", "5"}}).code, 400);   // чужому эффекту параметры огня не принадлежат
  CHECK_EQ(webPost("/api/effect", {{"n", "2"}, {"lv", "1"}, {"r", "5"}}).code, 400);
  CHECK_EQ(webPost("/api/effect", {{"n", std::to_string(EFFECT_COUNT)}, {"scale", "1"}}).code, 400);
}

TEST(web_effect_show_makes_it_current_and_restarts_auto_timer) {
  resetAll();
  forceTmr.restart();   // идущий показ по UDP отменяется выбором
  cfg.autoCh = 1;
  switchTmr.setPrd(60000);
  switchTmr.restart();
  Resp r = webPost("/api/effect", {{"n", "14"}, {"show", "1"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(curEff, 14);
  CHECK_EQ(forceEff, 14);
  CHECK(!forceTmr.state());
  CHECK(switchTmr.state());
  CHECK_EQ(EEeff.updates, 0);   // только выбор, настройки не менялись
  webPost("/api/effect", {{"n", "2"}, {"show", "0"}});
  CHECK_EQ(curEff, 14);         // show=0 текущий эффект не меняет
}

TEST(web_effect_index_and_values_are_checked) {
  resetAll();
  const Args bad[] = {{}, {{"n", std::to_string(EFFECT_COUNT)}}, {{"n", "-1"}}, {{"n", "x"}}, {{"n", "255"}},
                      {{"n", "1"}, {"scale", "256"}}, {{"n", "1"}, {"speed", "-5"}}, {{"n", "1"}, {"fav", "2"}},
                      {{"n", "1"}, {"show", "7"}}};
  for (auto& a : bad) {
    CHECK_EQ(webGet("/api/effect", a).code, a.count("scale") || a.count("speed") || a.count("fav") || a.count("show") ? 200 : 400);
    CHECK_EQ(webPost("/api/effect", a).code, 400);
  }
  CHECK_EQ(effs[1].scale, 50);
  CHECK(!forceTmr.state());
  CHECK_EQ(EEeff.updates, 0);
}

TEST(web_unknown_routes_and_methods) {
  resetAll();
  Resp r = webGet("/nope");
  CHECK_EQ(r.code, 404);
  CHECK(r.body.find("\"error\"") != std::string::npos);
  CHECK_EQ(webGet("/api/set").code, 404);                                  // GET на POST-маршрут
  webReady();
  CHECK_EQ(webServer.test(HTTP_POST, "/api/state", {}, kHdr).code, 404);   // POST на GET-маршрут
}

TEST(web_responses_are_not_cached) {
  resetAll();
  CHECK(webGet("/api/state").headers["Cache-Control"] == "no-store");
  CHECK(webPost("/api/set", {{"bright", "1"}}).headers["Cache-Control"] == "no-store");
}

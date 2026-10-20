// Тесты сцен (scenes.ino и их API). Подключается в конец test_firmware.cpp после test_web_cases.h.

static std::string cleanName(const char* in, int* rc = nullptr) {
  char out[SCENE_NAME_LEN + 1] = {0};
  int r = sceneCleanName(in, out);
  if (rc) *rc = r;
  return r < 0 ? std::string() : std::string(out);
}

static Resp scenePost(const char* what, const Args& a) { return webPost(std::string("/api/scenes/") + what, a); }
static void sceneSetup() {   // типичное "рабочее" состояние для сохранения
  cfg.bright = 150;
  cfg.autoCh = 1;
  cfg.rndCh = 1;
  cfg.prdCh = 5;
  curEff = 13;
  effs[3].fav = false;
  effs[3].scale = 91;
  effs[3].speed = 17;
}

TEST(scene_name_cleaning) {
  int rc;
  CHECK(cleanName("Вечер", &rc) == "Вечер" && rc == 10);
  CHECK(cleanName("Evening 1") == "Evening 1");
  CHECK(cleanName("ВосемьБукв") == "ВосемьБу");            // 16 байт = 8 букв: девятая не помещается целиком
  CHECK(cleanName("abcdefghijklmnopqrstuvwxyz") == "abcdefghijklmnop");
  CHECK(cleanName("abcdefghijklmn😀") == "abcdefghijklmn");   // 4-байтовый символ не режется пополам
  CHECK(cleanName("a\"b\\c") == "a'b'c");                     // кавычки и обратная косая заменены (JSON-безопасность)
  const char* bad[] = {"", "   ", "\n", "a\tb", "a\x7f", "\x80", "\xC3", "\xC3(", "\xE2\x82", "\xFF", "abc\x01",
                       "abcdefghijklmnopqrstuvwxyz\n"};   // управляющий символ даже в отбрасываемом хвосте
  for (const char* b : bad) {
    cleanName(b, &rc);
    CHECK_EQ(rc, -1);
  }
}

TEST(scene_save_load_roundtrip) {
  resetAll();
  webReady();
  sceneSetup();
  Resp r = scenePost("save", {{"slot", "2"}, {"name", "Вечер"}});
  CHECK_EQ(r.code, 200);
  CHECK(r.body.find("{\"i\":2,\"name\":\"Вечер\"}") != std::string::npos);
  CHECK(LittleFS.exists("/scenes.bin") && !LittleFS.exists("/scenes.tmp"));

  // меняем всё, что входит в сцену, и то, что не входит
  cfg.bright = 10;
  cfg.autoCh = 0;
  cfg.rndCh = 0;
  cfg.prdCh = 1;
  cfg.power = 0;
  cfg.ledAm = 77;
  pwr.maxMa = 4200;
  curEff = 1;
  for (auto& e : effs) e = Effects();
  forceTmr.restart();

  r = scenePost("load", {{"slot", "2"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(cfg.bright, 150);
  CHECK(cfg.autoCh && cfg.rndCh);
  CHECK_EQ(cfg.prdCh, 5);
  CHECK_EQ(curEff, 13);
  CHECK(!effs[3].fav);
  CHECK_EQ(effs[3].scale, 91);
  CHECK_EQ(effs[3].speed, 17);
  CHECK(effs[4].fav && effs[4].scale == 50);   // остальные эффекты - как были при сохранении
  CHECK(switchTmr.state());                    // автосмена запущена
  CHECK_EQ(switchTmr._prd, 5 * 60000u);        // с периодом сцены
  CHECK(!forceTmr.state());                    // принудительный показ остановлен
  CHECK(EEeff.updates > 0);
  // не входит в сцену: питание, число светодиодов, лимит тока
  CHECK(!cfg.power);
  CHECK_EQ(cfg.ledAm, 77);
  CHECK_EQ(pwr.maxMa, 4200);
  CHECK_EQ(jnum(r, "bright"), 150);   // ответ - новое состояние
}

TEST(scene_list_overwrite_delete_and_limits) {
  resetAll();
  webReady();
  sceneSetup();
  CHECK(webGet("/api/scenes").body == "{\"max\":8,\"scenes\":[]}");
  for (int i = 0; i < SCENE_SLOTS; i++) CHECK_EQ(scenePost("save", {{"slot", std::to_string(i)}, {"name", "S" + std::to_string(i)}}).code, 200);
  Resp r = webGet("/api/scenes");
  for (int i = 0; i < SCENE_SLOTS; i++) CHECK(r.body.find("{\"i\":" + std::to_string(i) + ",\"name\":\"S" + std::to_string(i) + "\"}") != std::string::npos);
  CHECK(r.body.size() < 700);

  cfg.bright = 33;
  scenePost("save", {{"slot", "4"}, {"name", "Новое"}});   // перезапись
  r = webGet("/api/scenes");
  CHECK(r.body.find("S4") == std::string::npos && r.body.find("Новое") != std::string::npos);
  scenePost("load", {{"slot", "4"}});
  CHECK_EQ(cfg.bright, 33);
  scenePost("load", {{"slot", "3"}});
  CHECK_EQ(cfg.bright, 150);   // остальные слоты не затронуты

  CHECK_EQ(scenePost("delete", {{"slot", "4"}}).code, 200);
  CHECK(webGet("/api/scenes").body.find("Новое") == std::string::npos);
  CHECK_EQ(scenePost("load", {{"slot", "4"}}).code, 404);
  CHECK_EQ(scenePost("delete", {{"slot", "4"}}).code, 404);
}

TEST(scene_survives_reboot) {
  resetAll();
  webReady();
  sceneSetup();
  scenePost("save", {{"slot", "0"}, {"name", "Ночь"}});
  scenePost("save", {{"slot", "7"}, {"name", "Утро"}});
  memset(scenes, 0, sizeof(scenes));   // перезагрузка: оперативная память пуста
  scenesLoad();
  CHECK(scenes[0].used == 1 && std::string(scenes[0].name) == "Ночь");
  CHECK(scenes[7].used == 1 && std::string(scenes[7].name) == "Утро");
  CHECK(scenes[3].used == 0);
  cfg.bright = 1;
  CHECK(sceneLoad(7));
  CHECK_EQ(cfg.bright, 150);
}

TEST(scene_corrupted_file_means_no_scenes) {
  resetAll();
  webReady();
  sceneSetup();
  scenePost("save", {{"slot", "1"}, {"name", "X"}});
  const std::string good = LittleFS.files["/scenes.bin"];
  CHECK_EQ(good.size(), sizeof(SceneHeader) + sizeof(scenes));

  auto reload = [&](const std::string& content, bool exists = true) {
    LittleFS.files.clear();
    if (exists) LittleFS.files["/scenes.bin"] = content;
    scenesLoad();
    return scenes[1].used == 1;
  };
  CHECK(reload(good));                                                  // исправный файл читается
  std::string flipped = good;
  flipped[sizeof(SceneHeader) + 40] ^= 0x55;
  CHECK(!reload(flipped));                                              // испорчен байт - контрольная сумма
  CHECK(!reload(good.substr(0, good.size() - 1)));                      // обрезан
  CHECK(!reload(good + "x"));                                           // лишний байт
  std::string badMagic = good;
  badMagic[0] = 'X';
  CHECK(!reload(badMagic));
  std::string badVer = good;
  badVer[2] = 9;
  CHECK(!reload(badVer));
  CHECK(!reload("", true));                                             // пустой файл
  CHECK(!reload("", false));                                            // файла нет
  for (auto& s : scenes) CHECK_EQ(s.used, 0);
  // допустимая сумма, но запись с недопустимым значением (curEff вне диапазона) - файл не принимается
  scenes[1].used = 1;
  scenes[1].prdCh = 1;
  scenes[1].curEff = 200;
  SceneHeader h = {{'G', 'S'}, 1, SCENE_SLOTS, scenesSum()};
  std::string evil((const char*)&h, sizeof(h));
  evil.append((const char*)scenes, sizeof(scenes));
  CHECK(!reload(evil));
}

TEST(scene_write_failure_keeps_previous_state) {
  resetAll();
  webReady();
  sceneSetup();
  scenePost("save", {{"slot", "0"}, {"name", "Старая"}});
  const std::string before = LittleFS.files["/scenes.bin"];
  LittleFS.failWrites = true;
  cfg.bright = 9;
  Resp r = scenePost("save", {{"slot", "0"}, {"name", "Новая"}});
  CHECK_EQ(r.code, 500);
  CHECK(r.body.find("\"error\":\"fs\"") != std::string::npos);
  CHECK(std::string(scenes[0].name) == "Старая" && scenes[0].bright == 150);   // в памяти прежнее содержимое
  CHECK(LittleFS.files["/scenes.bin"] == before);                                // на диске тоже
  CHECK(!LittleFS.exists("/scenes.tmp"));
  CHECK_EQ(scenePost("save", {{"slot", "1"}, {"name", "Вторая"}}).code, 500);
  CHECK_EQ(scenes[1].used, 0);
  CHECK_EQ(scenePost("delete", {{"slot", "0"}}).code, 500);
  CHECK_EQ(scenes[0].used, 1);
  LittleFS.failWrites = false;
  CHECK_EQ(scenePost("save", {{"slot", "1"}, {"name", "Вторая"}}).code, 200);   // после сбоя всё работает
}

TEST(scene_api_validation_and_csrf) {
  resetAll();
  webReady();
  sceneSetup();
  const Args badSlot[] = {{{"name", "A"}}, {{"slot", "8"}, {"name", "A"}}, {{"slot", "-1"}, {"name", "A"}}, {{"slot", "x"}, {"name", "A"}}, {{"slot", ""}, {"name", "A"}}};
  for (auto& a : badSlot) {
    Resp r = scenePost("save", a);
    CHECK_EQ(r.code, 400);
    CHECK(r.body.find("\"error\":\"slot\"") != std::string::npos);
  }
  const Args badName[] = {{{"slot", "0"}}, {{"slot", "0"}, {"name", ""}}, {{"slot", "0"}, {"name", "  "}}, {{"slot", "0"}, {"name", "a\nb"}}, {{"slot", "0"}, {"name", "\xC3"}}};
  for (auto& a : badName) {
    Resp r = scenePost("save", a);
    CHECK_EQ(r.code, 400);
    CHECK(r.body.find("\"error\":\"name\"") != std::string::npos);
  }
  CHECK_EQ(scenePost("load", {{"slot", "9"}}).code, 400);
  CHECK_EQ(scenePost("delete", {}).code, 400);
  CHECK(LittleFS.files.empty());   // ничего не записано

  // POST без заголовка страницы
  webServer.test(HTTP_POST, "/api/scenes/save", {{"slot", "0"}, {"name", "A"}}, {});
  CHECK(LittleFS.files.empty());
  CHECK_EQ(webServer.test(HTTP_POST, "/api/scenes/save", {{"slot", "0"}, {"name", "A"}}, {}).code, 403);
  scenePost("save", {{"slot", "0"}, {"name", "A"}});
  CHECK_EQ(webServer.test(HTTP_POST, "/api/scenes/load", {{"slot", "0"}}, {}).code, 403);
  CHECK_EQ(webServer.test(HTTP_POST, "/api/scenes/delete", {{"slot", "0"}}, {}).code, 403);
  CHECK_EQ(scenes[0].used, 1);
  CHECK_EQ(webGet("/api/scenes/save").code, 404);   // GET на POST-маршрут
}

TEST(scene_load_does_not_trust_a_slot_it_cannot_apply) {
  resetAll();
  webReady();
  CHECK(!sceneLoad(200));
  CHECK(!sceneLoad(0));   // пустой слот
  CHECK(!sceneSave(200, "x"));
  CHECK(!sceneDelete(200));
}

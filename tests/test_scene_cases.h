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
  effsX[4].fav = true;
  effsX[4].scale = 201;
  effsX[4].speed = 33;
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
  fxDefaults(effsX);
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
  CHECK(EEeff.updates > 0 && EEfx.updates > 0);
  CHECK(effsX[4].fav);                         // настройки новых эффектов тоже в сцене
  CHECK_EQ(effsX[4].scale, 201);
  CHECK_EQ(effsX[4].speed, 33);
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

TEST(scene_list_worst_case_fits_the_buffer) {
  resetAll();
  webReady();
  sceneSetup();
  for (int i = 0; i < SCENE_SLOTS; i++) CHECK_EQ(scenePost("save", {{"slot", std::to_string(i)}, {"name", "0123456789abcdef"}}).code, 200);   // 8 имён по 16 байт
  Resp r = webGet("/api/scenes");
  CHECK_EQ(r.code, 200);
  CHECK(r.body.size() < 400);
  CHECK(r.body.size() > 250);
  CHECK(r.body.back() == '}');
  size_t n = 0;
  for (size_t p = 0; (p = r.body.find("\"name\":", p)) != std::string::npos; p++) n++;
  CHECK_EQ(n, SCENE_SLOTS);   // ничего не обрезано
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
  SceneHeader h = {{'G', 'S'}, 2, SCENE_SLOTS, scenesSum()};
  std::string evil((const char*)&h, sizeof(h));
  evil.append((const char*)scenes, sizeof(scenes));
  CHECK(!reload(evil));
}

// файл версии 1 (до новых эффектов) из реальной прошивки 1.3: сцены не теряются, новые эффекты - по умолчанию
TEST(scene_file_version_1_is_migrated) {
  resetAll();
  webReady();
  SceneV1 old[SCENE_SLOTS];
  memset(old, 0, sizeof(old));
  old[2].used = 1;
  strcpy(old[2].name, "Старая");
  old[2].bright = 120;
  old[2].autoCh = 1;
  old[2].prdCh = 3;
  old[2].curEff = 17;
  for (auto& e : old[2].effs) e = Effects();
  old[2].effs[9].scale = 66;
  old[2].effs[9].fav = false;
  uint16_t a = 1, b = 0;
  fletcherAdd(a, b, (const byte*)old, sizeof(old));
  SceneHeader h = {{'G', 'S'}, 1, SCENE_SLOTS, (uint16_t)((b << 8) | a)};
  std::string file((const char*)&h, sizeof(h));
  file.append((const char*)old, sizeof(old));

  LittleFS.files["/scenes.bin"] = file;
  scenesLoad();
  CHECK(scenes[2].used == 1 && std::string(scenes[2].name) == "Старая");
  CHECK(scenes[2].bright == 120 && scenes[2].autoCh == 1 && scenes[2].prdCh == 3 && scenes[2].curEff == 17);
  CHECK_EQ(scenes[2].effs[9].scale, 66);
  CHECK(!scenes[2].effs[9].fav);
  Effects def[EXT_EFFECTS];
  fxDefaults(def);
  CHECK(memcmp(scenes[2].fx, def, sizeof(def)) == 0);   // новые эффекты - значения по умолчанию
  CHECK_EQ(scenes[0].used, 0);

  CHECK(sceneLoad(2));   // сцену можно применить
  CHECK_EQ(curEff, 17);
  CHECK_EQ(effs[9].scale, 66);

  // испорченный файл версии 1 - сцен нет
  std::string bad = file;
  bad[sizeof(SceneHeader) + 5] ^= 0x40;
  LittleFS.files["/scenes.bin"] = bad;
  scenesLoad();
  CHECK_EQ(scenes[2].used, 0);
  LittleFS.files["/scenes.bin"] = file.substr(0, file.size() - 3);   // обрезан
  scenesLoad();
  CHECK_EQ(scenes[2].used, 0);

  // после следующей записи файл становится версией 2 и читается снова
  LittleFS.files["/scenes.bin"] = file;
  scenesLoad();
  sceneSetup();
  CHECK(sceneSave(5, "Новая", false));
  CHECK_EQ(LittleFS.files["/scenes.bin"].size(), sizeof(SceneHeader) + sizeof(scenes));
  CHECK_EQ((int)LittleFS.files["/scenes.bin"][2], 2);
  memset(scenes, 0, sizeof(scenes));
  scenesLoad();
  CHECK(scenes[2].used == 1 && scenes[5].used == 1);
  CHECK_EQ(scenes[5].fx[4].scale, 201);
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
  CHECK(!sceneSave(200, "x", false));
  CHECK(!sceneDelete(200));
}

// ---- схема светодиодов и «Огонь v2» в сцене (файл дополнений /x<слот>.bin) ----
static void sceneMapSetup(int n) {
  cfg.ledAm = n;
  for (int i = 0; i < LED_MAX; i++) {
    xy[i][0] = (i * 7 + 3) & 255;
    xy[i][1] = (i * 13 + 5) & 255;
  }
  recalcBounds();
  fire2.shape = 11;
  fire2.col[3][0] = 9;
  fire2.col[3][1] = 8;
  fire2.col[3][2] = 7;
}

TEST(scene_with_map_restores_led_count_coordinates_and_fire2) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(300);
  MM mmSaved = mm;
  Resp r = scenePost("save", {{"slot", "2"}, {"name", "Соты"}, {"map", "1"}});
  CHECK_EQ(r.code, 200);
  CHECK(r.body.find("{\"i\":2,\"name\":\"Соты\",\"map\":300}") != std::string::npos);
  CHECK(LittleFS.exists("/x2.bin") && !LittleFS.exists("/x2.tmp"));
  CHECK(webGet("/api/scenes").body.find("\"map\":300") != std::string::npos);

  // всё меняем: число светодиодов, координаты, границы, огонь v2
  cfg.ledAm = 50;
  for (int i = 0; i < LED_MAX; i++) xy[i][0] = xy[i][1] = 1;
  recalcBounds();
  fire2 = Fire2();
  EExy.nows = EEmm.nows = EEfire2.updates = 0;
  stripObj.setLeds(leds, 50);

  r = scenePost("load", {{"slot", "2"}});
  CHECK_EQ(r.code, 200);
  CHECK_EQ(cfg.ledAm, 300);
  CHECK_EQ(stripObj.count, 300);
  bool same = true;
  for (int i = 0; i < 300; i++) same = same && xy[i][0] == ((i * 7 + 3) & 255) && xy[i][1] == ((i * 13 + 5) & 255);
  CHECK(same);
  CHECK(mm.w == mmSaved.w && mm.h == mmSaved.h && mm.minX == mmSaved.minX && mm.maxY == mmSaved.maxY);   // границы пересчитаны
  CHECK(EExy.nows > 0 && EEmm.nows > 0);   // координаты записаны в EEPROM
  CHECK_EQ(jnum(r, "ledAm"), 300);          // ответ - новое состояние
  CHECK_EQ(fire2.shape, 11);                // «Огонь v2» тоже вернулся
  CHECK(fire2.col[3][0] == 9 && fire2.col[3][1] == 8 && fire2.col[3][2] == 7);
  CHECK(EEfire2.updates > 0);
}

TEST(scene_without_map_keeps_the_map_but_restores_fire2) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(120);
  CHECK_EQ(scenePost("save", {{"slot", "1"}, {"name", "Без схемы"}}).code, 200);   // map не задан
  Resp l = webGet("/api/scenes");
  CHECK(l.body.find("\"map\"") == std::string::npos);
  CHECK(LittleFS.exists("/x1.bin"));   // настройки огня v2 сохранены всегда
  cfg.ledAm = 77;
  xy[0][0] = 200;
  fire2.shape = 99;
  CHECK_EQ(scenePost("load", {{"slot", "1"}}).code, 200);
  CHECK_EQ(cfg.ledAm, 77);      // число светодиодов и координаты не тронуты
  CHECK_EQ(xy[0][0], 200);
  CHECK_EQ(fire2.shape, 11);    // огонь v2 вернулся
  CHECK_EQ(scenePost("save", {{"slot", "1"}, {"name", "Нулевой"}, {"map", "0"}}).code, 200);   // явный 0 - то же самое
  CHECK(webGet("/api/scenes").body.find("\"map\"") == std::string::npos);
}

TEST(scene_overwrite_and_delete_replace_and_remove_the_map) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(200);
  scenePost("save", {{"slot", "3"}, {"name", "Схема"}, {"map", "1"}});
  CHECK(webGet("/api/scenes").body.find("\"map\":200") != std::string::npos);
  cfg.ledAm = 90;
  scenePost("save", {{"slot", "3"}, {"name", "Схема"}, {"map", "1"}});   // перезапись с другой схемой
  CHECK(webGet("/api/scenes").body.find("\"map\":90") != std::string::npos);
  scenePost("save", {{"slot", "3"}, {"name", "Схема"}});                 // перезапись без схемы
  CHECK(webGet("/api/scenes").body.find("\"map\"") == std::string::npos);
  cfg.ledAm = 40;
  scenePost("load", {{"slot", "3"}});
  CHECK_EQ(cfg.ledAm, 40);   // схемы в сцене больше нет
  scenePost("save", {{"slot", "3"}, {"name", "Схема"}, {"map", "1"}});
  CHECK(LittleFS.exists("/x3.bin"));
  CHECK_EQ(scenePost("delete", {{"slot", "3"}}).code, 200);
  CHECK(!LittleFS.exists("/x3.bin") && !LittleFS.exists("/x3.tmp"));
  CHECK(webGet("/api/scenes").body.find("\"map\"") == std::string::npos);
}

TEST(scene_map_survives_reboot_and_strays_are_removed) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(150);
  scenePost("save", {{"slot", "5"}, {"name", "Ночь"}, {"map", "1"}});
  LittleFS.files["/x6.bin"] = "лишний файл дополнений пустого слота";
  memset(scenes, 0, sizeof(scenes));
  memset(sceneMapN, 0, sizeof(sceneMapN));   // перезагрузка: оперативная память пуста
  scenesLoad();
  CHECK_EQ(sceneMapN[5], 150);
  CHECK(webGet("/api/scenes").body.find("\"map\":150") != std::string::npos);
  CHECK(!LittleFS.exists("/x6.bin"));   // файл пустого слота удалён
  cfg.ledAm = 10;
  CHECK(sceneLoad(5));
  CHECK_EQ(cfg.ledAm, 150);
}

TEST(scene_damaged_extras_are_ignored_but_the_scene_still_loads) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(100);
  scenePost("save", {{"slot", "0"}, {"name", "Сцена"}, {"map", "1"}});
  const std::string good = LittleFS.files["/x0.bin"];
  std::string bad[6] = {good, good, good, good, good, good};
  bad[0][bad[0].size() - 5] ^= 0x40;                     // испорчен байт координат: контрольная сумма
  bad[1].resize(good.size() - 3);                        // файл обрезан
  bad[2] += "лишнее";                                    // лишние байты в конце
  bad[3][0] = 'Z';                                       // чужая метка
  bad[4][2] = 9;                                         // другая версия формата
  bad[5][6] = (char)0xFF; bad[5][7] = (char)0xFF;        // число светодиодов 65535 больше предела
  for (auto& b : bad) {
    LittleFS.files["/x0.bin"] = b;
    memset(sceneMapN, 0, sizeof(sceneMapN));
    cfg.bright = 1;
    cfg.ledAm = 20;
    xy[0][0] = 250;
    fire2.shape = 99;
    CHECK(sceneLoad(0));
    CHECK_EQ(cfg.bright, 150);     // основные настройки сцены применились
    CHECK_EQ(cfg.ledAm, 20);       // схема из испорченного файла - нет
    CHECK_EQ(xy[0][0], 250);
    CHECK_EQ(fire2.shape, 99);     // и огонь v2 тоже не тронут
  }
  scenesLoad();
  CHECK_EQ(sceneMapN[0], 0);       // кэш числа светодиодов после проверки заголовка: у испорченных файлов схемы нет
}

TEST(scene_map_write_failure_keeps_the_previous_scene) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(100);
  scenePost("save", {{"slot", "4"}, {"name", "Старая"}, {"map", "1"}});
  const std::string extrasBefore = LittleFS.files["/x4.bin"];
  const std::string scenesBefore = LittleFS.files["/scenes.bin"];
  cfg.ledAm = 60;
  LittleFS.failWrites = true;
  CHECK_EQ(scenePost("save", {{"slot", "4"}, {"name", "Новая"}, {"map", "1"}}).code, 500);
  LittleFS.failWrites = false;
  CHECK(LittleFS.files["/x4.bin"] == extrasBefore);       // прежняя схема на месте
  CHECK(LittleFS.files["/scenes.bin"] == scenesBefore);
  CHECK(!LittleFS.exists("/x4.tmp"));
  CHECK_EQ(sceneMapN[4], 100);
  CHECK(std::string(scenes[4].name) == "Старая");
}

TEST(scene_with_full_thousand_leds_map) {
  resetAll();
  webReady();
  sceneSetup();
  sceneMapSetup(LED_MAX);
  CHECK_EQ(scenePost("save", {{"slot", "7"}, {"name", "Тысяча"}, {"map", "1"}}).code, 200);
  CHECK(webGet("/api/scenes").body.find("\"map\":1000") != std::string::npos);
  cfg.ledAm = 10;
  for (int i = 0; i < LED_MAX; i++) xy[i][0] = xy[i][1] = 0;
  CHECK_EQ(scenePost("load", {{"slot", "7"}}).code, 200);
  CHECK_EQ(cfg.ledAm, LED_MAX);
  CHECK(xy[LED_MAX - 1][0] == ((LED_MAX - 1) * 7 + 3 & 255) && xy[LED_MAX - 1][1] == ((LED_MAX - 1) * 13 + 5 & 255));
  CHECK(EExyHi.nows > 0);   // координаты после 500-го записаны во второй блок EEPROM
  CHECK(webGet("/api/scenes").body.size() < 640);
}

TEST(scene_save_map_argument_is_checked) {
  resetAll();
  webReady();
  sceneSetup();
  for (const char* bad : {"2", "-1", "x", ""}) {
    Resp r = scenePost("save", {{"slot", "0"}, {"name", "A"}, {"map", bad}});
    CHECK_EQ(r.code, 400);
    CHECK(r.body.find("\"error\":\"map\"") != std::string::npos);
  }
  CHECK(LittleFS.files.empty());   // ничего не записано
}

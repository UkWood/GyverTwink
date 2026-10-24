// Тесты защиты токеном (guard.ino и проверки в web.ino / parsing.ino). Подключается в конец test_firmware.cpp.

static const char* TOK = "Secret-Token_01";
static const Args kHdrTok = {{"X-Requested-With", "GyverTwink"}, {"X-GT-Token", "Secret-Token_01"}};

static Resp getTok(const std::string& uri, const std::string& token, const Args& args = {}) {
  webReady();
  Args h = {{"X-GT-Token", token}};
  return webServer.test(HTTP_GET, uri, args, h);
}
static Resp postTok(const std::string& uri, const std::string& token, const Args& args = {}) {
  webReady();
  Args h = {{"X-Requested-With", "GyverTwink"}, {"X-GT-Token", token}};
  return webServer.test(HTTP_POST, uri, args, h);
}
static void guardOnWith(const char* t) {
  strncpy(guard.token, t, TOKEN_MAX);
  guard.token[TOKEN_MAX] = '\0';
}
static void openWindow() { otaOpenWindow(); }

TEST(guard_token_validation) {
  CHECK(guardTokenValid("abcdefgh"));
  CHECK(guardTokenValid("Secret-Token_01.~x"));
  CHECK(guardTokenValid("12345678901234567890123456789012"));        // ровно 32
  CHECK(!guardTokenValid("1234567890123456789012345678901234"));     // 34
  CHECK(!guardTokenValid("12345678901234567890123456789012x"));      // 33
  CHECK(!guardTokenValid("abcdefg"));                                 // 7
  CHECK(!guardTokenValid(""));
  const char* bad[] = {"abcd efgh", "abcdefg!", "abcdefgh\n", "абвгдежз", "abc\"defgh", "abc:defgh", "abc/defgh", "ab\tcdefgh", "abcdefg;", "abcdefg%"};
  for (const char* b : bad) CHECK(!guardTokenValid(b));
}

TEST(guard_check_compares_the_whole_token) {
  resetAll();
  guardOnWith("Secret-Token_01");
  CHECK(guardCheck("Secret-Token_01"));
  CHECK(!guardCheck(""));
  CHECK(!guardCheck("Secret-Token_0"));       // короче
  CHECK(!guardCheck("Secret-Token_011"));     // длиннее
  CHECK(!guardCheck("secret-token_01"));      // регистр
  CHECK(!guardCheck("Xecret-Token_01"));      // первый символ
  CHECK(!guardCheck("Secret-Token_0X"));      // последний
  CHECK(!guardCheck("Secret-Token_01\x01"));  // лишний байт
  std::string longer(200, 'a');
  CHECK(!guardCheck(longer.c_str()));         // сильно длиннее лимита
  guardOnWith("12345678901234567890123456789012");   // токен максимальной длины
  CHECK(guardCheck("12345678901234567890123456789012"));
  CHECK(!guardCheck("123456789012345678901234567890123"));   // та же основа + лишний символ
}

TEST(guard_off_changes_nothing) {
  resetAll();
  CHECK(!guardOn());
  CHECK_EQ(webGet("/api/state").code, 200);
  CHECK_EQ(webGet("/api/schedule").code, 200);
  CHECK_EQ(webPost("/api/set", {{"bright", "50"}}).code, 200);
  CHECK_EQ(cfg.bright, 50);
  CHECK_EQ(getTok("/api/state", "лишний-токен-игнорируется").code, 200);   // токен без включённой защиты безвреден
  Resp r = webGet("/api/auth");
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "on"), 0);
  CHECK_EQ(jnum(r, "ok"), 1);
  CHECK_EQ(jnum(r, "udpLock"), 0);
  CHECK_EQ(jnum(r, "min"), TOKEN_MIN);
  CHECK_EQ(jnum(r, "max"), TOKEN_MAX);
  CHECK(r.body.size() < 110);
}

// все маршруты, которым нужен токен
struct GuardRoute {
  HTTPMethod m;
  const char* uri;
  Args args;
};
static std::vector<GuardRoute> guardedRoutes() {
  return {
      {HTTP_GET, "/api/state", {}},
      {HTTP_POST, "/api/set", {{"bright", "77"}}},
      {HTTP_POST, "/api/next", {}},
      {HTTP_GET, "/api/effect", {{"n", "3"}}},
      {HTTP_POST, "/api/effect", {{"n", "3"}, {"scale", "9"}}},
      {HTTP_GET, "/api/scenes", {}},
      {HTTP_POST, "/api/scenes/save", {{"slot", "0"}, {"name", "A"}}},
      {HTTP_POST, "/api/scenes/load", {{"slot", "0"}}},
      {HTTP_POST, "/api/scenes/delete", {{"slot", "0"}}},
      {HTTP_GET, "/api/xy", {}},
      {HTTP_POST, "/api/xy", {{"n", "100"}, {"i", "0"}, {"x", "5"}, {"y", "6"}}},
      {HTTP_POST, "/api/led", {{"i", "0"}}},
      {HTTP_GET, "/api/vcalib", {}},
      {HTTP_POST, "/api/vcalib/start", {}},
      {HTTP_POST, "/api/vcalib/stop", {}},
      {HTTP_GET, "/api/schedule", {}},
      {HTTP_POST, "/api/schedule", {{"en", "0"}}},
      {HTTP_POST, "/api/time", {{"unix", "1792000000"}}},
      {HTTP_GET, "/api/ota", {}},
  };
}

TEST(guard_every_api_route_needs_the_token) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  cfg.bright = 10;
  const Sched schedBefore = sched;
  for (auto& r : guardedRoutes()) {
    Args h = r.m == HTTP_POST ? kHdr : Args{};
    Resp none = webServer.test(r.m, r.uri, r.args, h);   // без токена
    CHECK_EQ(none.code, 401);
    CHECK(none.body.find("\"error\":\"auth\"") != std::string::npos);
    Args hw = h;
    hw["X-GT-Token"] = "Secret-Token_02";   // неверный
    Resp bad = webServer.test(r.m, r.uri, r.args, hw);
    CHECK_EQ(bad.code, 401);
    memset(guardCnt, 0, sizeof(guardCnt));   // одна неверная попытка на маршрут: блокировку этот тест не проверяет
    Args hg = h;
    hg["X-GT-Token"] = TOK;
    Resp good = webServer.test(r.m, r.uri, r.args, hg);   // верный: маршрут отрабатывает (не 401/403/429)
    CHECK(good.code != 401 && good.code != 403 && good.code != 429);
    if (good.code == 401 || good.code == 403 || good.code == 429) std::printf("    маршрут %s\n", r.uri);
  }
  CHECK_EQ(cfg.bright, 77);                  // изменил только запрос с токеном
  CHECK(sched.tz == schedBefore.tz);
  // ничего из отклонённого не применилось: после отказов состояние прежнее
  cfg.bright = 10;
  CHECK_EQ(webServer.test(HTTP_POST, "/api/set", {{"bright", "200"}}, kHdr).code, 401);
  CHECK_EQ(cfg.bright, 10);
}

TEST(guard_page_and_status_stay_open_and_csrf_comes_first) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  Resp r = webGet("/");
  CHECK_EQ(r.code, 200);                                 // сама страница открыта: на ней вход
  CHECK_EQ(webGet("/nope").code, 404);
  r = webGet("/api/auth");
  CHECK_EQ(r.code, 200);
  CHECK_EQ(jnum(r, "on"), 1);
  CHECK_EQ(jnum(r, "ok"), 0);                            // без токена
  CHECK(r.body.find(TOK) == std::string::npos);          // токен не раскрывается
  r = getTok("/api/auth", TOK);
  CHECK_EQ(jnum(r, "ok"), 1);
  CHECK_EQ(jnum(getTok("/api/auth", "Secret-Token_02"), "ok"), 0);
  // POST без заголовка страницы - 403 раньше проверки токена, даже с верным токеном
  Args onlyTok = {{"X-GT-Token", TOK}};
  CHECK_EQ(webServer.test(HTTP_POST, "/api/set", {{"bright", "1"}}, onlyTok).code, 403);
  CHECK_EQ(webServer.test(HTTP_POST, "/api/auth", {{"udpLock", "1"}}, onlyTok).code, 403);
}

TEST(guard_brute_force_is_limited_per_address) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  fakeClientIp() = 0x0A000063;   // злоумышленник
  for (int i = 0; i < GUARD_TRIES - 1; i++) CHECK_EQ(getTok("/api/state", "wrong-token-" + std::to_string(i)).code, 401);
  Resp r = getTok("/api/state", "wrong-token-x");
  CHECK_EQ(r.code, 429);                                            // пятая неверная блокирует
  CHECK(r.headers["Retry-After"] == "60");
  CHECK(r.body.find("\"error\":\"locked\"") != std::string::npos);
  CHECK_EQ(getTok("/api/state", TOK).code, 429);                    // и верный токен с этого адреса не принимается
  CHECK_EQ(jnum(getTok("/api/auth", TOK), "locked"), 1);
  CHECK_EQ(webServer.test(HTTP_POST, "/api/set", {{"bright", "1"}}, kHdrTok).code, 429);
  fakeClientIp() = 0x0A00000A;   // владелец с другого адреса не пострадал
  CHECK_EQ(getTok("/api/state", TOK).code, 200);
  fakeClientIp() = 0x0A000063;
  fakeMillis() += GUARD_LOCK_MS - 1000;
  CHECK_EQ(getTok("/api/state", TOK).code, 429);                    // срок ещё не вышел
  fakeMillis() += 2000;
  CHECK_EQ(getTok("/api/state", TOK).code, 200);                    // вышел: снова можно, счётчик сброшен
  for (int i = 0; i < GUARD_TRIES - 1; i++) getTok("/api/state", "wrong-again");
  CHECK_EQ(getTok("/api/state", TOK).code, 200);                    // верный токен обнуляет серию неверных
  for (int i = 0; i < GUARD_TRIES - 1; i++) CHECK_EQ(getTok("/api/state", "wrong-again").code, 401);
}

TEST(guard_missing_token_is_not_a_guess_and_old_failures_expire) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  fakeClientIp() = 0x0A000063;
  for (int i = 0; i < 50; i++) CHECK_EQ(webGet("/api/state").code, 401);   // страница без токена: не блокируется
  CHECK_EQ(getTok("/api/state", TOK).code, 200);
  for (int i = 0; i < GUARD_TRIES - 1; i++) getTok("/api/state", "wrong-token");
  fakeMillis() += GUARD_QUIET_MS + 1;                                       // долгая тишина: серия забыта
  CHECK_EQ(getTok("/api/state", "wrong-token").code, 401);
  CHECK_EQ(getTok("/api/state", TOK).code, 200);
}

TEST(guard_many_addresses_do_not_evict_an_existing_lock) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  fakeClientIp() = 0x0A000001;
  for (int i = 0; i < GUARD_TRIES; i++) getTok("/api/state", "wrong-token");   // первый адрес заблокирован
  CHECK_EQ(getTok("/api/state", TOK).code, 429);
  // другие адреса (больше, чем мест в таблице) делают неверные попытки: незаблокированные записи вытесняют друг друга,
  // блокировка остаётся
  for (uint32_t ip = 2; ip < 2 + 3 * GUARD_SLOTS; ip++) {
    fakeClientIp() = 0x0A000000 + ip;
    CHECK_EQ(getTok("/api/state", "wrong-token").code, 401);
  }
  fakeClientIp() = 0x0A000001;
  CHECK_EQ(getTok("/api/state", TOK).code, 429);
  // если заблокированы ВСЕ места, новый нарушитель вытесняет самую давнюю блокировку, а не ломает таблицу
  for (uint32_t ip = 10; ip < 10 + GUARD_SLOTS; ip++) {
    fakeClientIp() = 0x0A000000 + ip;
    for (int i = 0; i < GUARD_TRIES; i++) getTok("/api/state", "wrong-token");
    CHECK_EQ(getTok("/api/state", TOK).code, 429);
  }
  fakeClientIp() = 0x0A0000F0;   // владелец с адреса, которого нет в таблице
  CHECK_EQ(getTok("/api/state", TOK).code, 200);
}

TEST(guard_set_token_only_while_the_window_is_open) {
  resetAll();
  webReady();
  CHECK(!otaOpen());
  Resp r = webPost("/api/auth", {{"token", "NewToken-123456"}});
  CHECK_EQ(r.code, 403);
  CHECK(r.body.find("\"error\":\"window\"") != std::string::npos);
  CHECK(!guardOn());
  CHECK_EQ(EEguard.updates, 0);

  openWindow();
  r = webPost("/api/auth", {{"token", "NewToken-123456"}});
  CHECK_EQ(r.code, 200);
  CHECK(guardOn() && std::string(guard.token) == "NewToken-123456");
  CHECK(EEguard.updates > 0);
  CHECK(!otaOpen());                                       // окно после смены закрылось
  CHECK_EQ(jnum(r, "on"), 1);
  CHECK_EQ(webGet("/api/state").code, 401);                // защита действует сразу
  CHECK_EQ(getTok("/api/state", "NewToken-123456").code, 200);

  // сменить без окна нельзя, даже зная токен
  r = postTok("/api/auth", "NewToken-123456", {{"token", "Another-Token-99"}});
  CHECK_EQ(r.code, 403);
  CHECK(std::string(guard.token) == "NewToken-123456");
  // снять без окна нельзя
  CHECK_EQ(postTok("/api/auth", "NewToken-123456", {{"token", ""}}).code, 403);
  CHECK(guardOn());
  // с окном можно и сменить, и снять - токен старый не нужен (забыл токен: три нажатия кнопки)
  openWindow();
  CHECK_EQ(webPost("/api/auth", {{"token", "Another-Token-99"}}).code, 200);
  CHECK(std::string(guard.token) == "Another-Token-99");
  openWindow();
  r = webPost("/api/auth", {{"token", ""}});
  CHECK_EQ(r.code, 200);
  CHECK(!guardOn());
  CHECK_EQ(jnum(r, "on"), 0);
  CHECK_EQ(webGet("/api/state").code, 200);                // защита снята
}

TEST(guard_post_auth_is_strict_and_all_or_nothing) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  openWindow();
  const Args bad[] = {{},
                      {{"token", "short"}},
                      {{"token", "has space token"}},
                      {{"token", "кириллица-токен"}},
                      {{"token", std::string(33, 'a')}},
                      {{"token", "bad\"quote-token"}},
                      {{"udpLock", "2"}},
                      {{"udpLock", "x"}},
                      {{"udpLock", ""}},
                      {{"token", "ValidToken-1234"}, {"udpLock", "7"}}};   // токен верный, второе поле нет: не применяется ничего
  for (auto& a : bad) {
    Resp r = postTok("/api/auth", TOK, a);
    CHECK_EQ(r.code, 400);
    CHECK(std::string(guard.token) == TOK);
    CHECK(guard.udpOpen);
    CHECK(otaOpen());   // окно от неудачной попытки не закрывается
  }
  CHECK_EQ(EEguard.updates, 0);
  // граничные длины
  CHECK_EQ(postTok("/api/auth", TOK, {{"token", "12345678"}}).code, 200);
  openWindow();
  CHECK_EQ(postTok("/api/auth", "12345678", {{"token", std::string(32, 'z')}}).code, 200);
  CHECK(std::string(guard.token) == std::string(32, 'z'));
}

TEST(guard_udp_lock_needs_the_token_not_the_window) {
  resetAll();
  webReady();
  // защиты нет: переключатель UDP безвреден и доступен
  CHECK_EQ(webPost("/api/auth", {{"udpLock", "1"}}).code, 200);
  CHECK(!guard.udpOpen);
  CHECK_EQ(webPost("/api/auth", {{"udpLock", "0"}}).code, 200);
  CHECK(guard.udpOpen);
  guardOnWith(TOK);
  CHECK(!otaOpen());
  CHECK_EQ(webPost("/api/auth", {{"udpLock", "1"}}).code, 401);   // защита включена: нужен токен
  CHECK(guard.udpOpen);
  Resp r = postTok("/api/auth", TOK, {{"udpLock", "1"}});          // с токеном окно не нужно
  CHECK_EQ(r.code, 200);
  CHECK(!guard.udpOpen);
  CHECK_EQ(jnum(r, "udpLock"), 1);
  CHECK_EQ(jnum(getTok("/api/auth", TOK), "udpLock"), 1);
  CHECK(EEguard.updates > 0);
}

// ---------- UDP ----------
TEST(guard_udp_stays_open_by_default_even_with_a_token) {
  resetAll();
  guardOnWith(TOK);
  CHECK(guard.udpOpen);
  send({2, 2, 120});
  CHECK_EQ(cfg.bright, 120);   // приложение (без токена) управляет как раньше
}

TEST(guard_udp_lock_blocks_writes_but_not_queries) {
  resetAll();
  guardOnWith(TOK);
  guard.udpOpen = 0;
  cfg.bright = 33;
  Effects e5 = effs[5];
  send({2, 2, 120});
  send({2, 1, 0});
  send({3, 0});
  send({4, 0, 5});
  send({4, 2, 99});
  CHECK_EQ(cfg.bright, 33);
  CHECK(cfg.power);
  CHECK(!calibF);
  CHECK(memcmp(&e5, &effs[5], sizeof(e5)) == 0);
  CHECK_EQ(forceEff, 0);
  CHECK(udp.out.empty());   // даже ответ {4,0,n} не выдаётся
  // запросы работают: поиск устройства, настройки, возможности, лимит
  send({0});
  CHECK_EQ(udp.out.size(), 1);
  udp.out.clear();
  send({1});
  Bytes r = lastOut();
  CHECK(r.size() == 12 && r[2] == 1 && r[6] == 33);   // видны текущие настройки (яркость 33)
  udp.out.clear();
  send({5});
  CHECK(lastOut()[3] == PROTO_EXT_VER);
  udp.out.clear();
  send({7});
  CHECK(lastOut()[2] == 7);
  // с ACK: отказ получает статус 3, повтор того же seq тоже
  udp.out.clear();
  int s = newSeq();
  sendAck(s, {2, 2, 200});
  r = lastOut();
  CHECK(r.size() == 6 && r[2] == 6 && r[3] == 2 && r[4] == s && r[5] == PKT_DENIED);
  CHECK_EQ(cfg.bright, 33);
  sendAck(s, {2, 2, 200});   // повтор
  CHECK_EQ(lastOut()[5], PKT_DENIED);
  CHECK_EQ(cfg.bright, 33);
  // замок снят - команды снова проходят
  guard.udpOpen = 1;
  send({2, 2, 120});
  CHECK_EQ(cfg.bright, 120);
}

TEST(guard_udp_lock_without_a_token_does_nothing) {
  resetAll();
  guard.udpOpen = 0;   // токена нет: флаг ни на что не влияет
  CHECK(!guardOn());
  send({2, 2, 90});
  CHECK_EQ(cfg.bright, 90);
  send({3, 0});
  CHECK(calibF);
  send({3, 2});
}

TEST(guard_ota_upload_does_not_need_the_token_only_the_window) {
  resetAll();
  webReady();
  guardOnWith(TOK);
  otaOpenWindow();
  // /update защищён физическим окном и заголовком страницы; токен не нужен (владелец мог его забыть)
  Resp r = otaUpload(fwImage(6000));
  CHECK_EQ(r.code, 200);
}

TEST(guard_eeprom_block_defaults) {
  resetAll();
  CHECK_EQ((int)sizeof(Guard), TOKEN_MAX + 1 + 1);
  Guard g;
  CHECK(g.token[0] == '\0' && g.udpOpen == 1);   // по умолчанию защиты нет, UDP открыт
}

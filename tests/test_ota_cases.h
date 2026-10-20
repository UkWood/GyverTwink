// Тесты обновления прошивки по WiFi (ota.ino, /update): окно открывает кнопка, остальное - защита и ошибки.
// Подключается в конец test_firmware.cpp.

static std::string fwImage(size_t n, uint8_t first = 0xE9) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; i++) s[i] = (char)((i * 131 + 7) & 0xFF);
  if (n) s[0] = (char)first;
  return s;
}
static std::vector<std::string> split(const std::string& s, size_t k) {
  std::vector<std::string> v;
  for (size_t i = 0; i < s.size(); i += k) v.push_back(s.substr(i, k));
  return v;
}
static Resp otaUpload(const std::string& img, const Args& hdr = kHdr) {
  webReady();
  return webServer.testUpload("/update", hdr, "firmware.bin", split(img, 1460));
}
static bool rebooted() {
  otaTick();
  return ESP.restarted;
}

TEST(ota_closed_window_rejects_everything) {
  resetAll();
  webReady();
  Resp st = webGet("/api/ota");
  CHECK_EQ(jnum(st, "open"), 0);
  CHECK_EQ(jnum(st, "left"), 0);
  CHECK_EQ(jnum(st, "free"), (long)ESP.freeSketchSpace);
  Resp r = otaUpload(fwImage(5000));
  CHECK_EQ(r.code, 403);
  CHECK(r.body.find("\"error\":\"ota-closed\"") != std::string::npos);
  CHECK(!Update.began && Update.data.empty());   // запись даже не начиналась
  CHECK(!otaBusy);
  CHECK(!rebooted());
}

TEST(ota_window_opens_counts_down_and_closes) {
  resetAll();
  webReady();
  otaOpenWindow();
  Resp r = webGet("/api/ota");
  CHECK_EQ(jnum(r, "open"), 1);
  CHECK_EQ(jnum(r, "left"), 300);
  fakeMillis() += 100000;
  r = webGet("/api/ota");
  CHECK_EQ(jnum(r, "left"), 200);
  fakeMillis() += 199500;
  CHECK_EQ(jnum(webGet("/api/ota"), "left"), 1);   // округление вверх: не показываем 0 при открытом окне
  CHECK(otaOpen());
  fakeMillis() += 1000;
  otaTick();
  CHECK(!otaOpen());
  CHECK_EQ(jnum(webGet("/api/ota"), "open"), 0);
  CHECK_EQ(otaUpload(fwImage(3000)).code, 403);
  otaOpenWindow();   // повторное открытие снова даёт полные 5 минут
  CHECK_EQ(jnum(webGet("/api/ota"), "left"), 300);
}

TEST(ota_successful_update) {
  resetAll();
  otaOpenWindow();
  const std::string img = fwImage(1460 * 2 + 500);   // три куска, последний неполный
  Resp r = otaUpload(img);
  CHECK_EQ(r.code, 200);
  CHECK(r.body == "{\"ok\":1}");
  CHECK(Update.data.size() == img.size() && std::string(Update.data.begin(), Update.data.end()) == img);
  CHECK(Update.ended && !Update.error);
  CHECK(!otaOpen());                        // окно закрыто: второй раз пришлось бы снова жать кнопку
  CHECK(otaBusy);                           // эффекты отключены до перезагрузки
  CHECK(!ESP.restarted);                    // ответ должен успеть дойти до страницы
  fakeMillis() += 500;
  CHECK(!rebooted());
  fakeMillis() += 400;
  CHECK(rebooted());                        // через 0,8 с
  // повторная загрузка без нового открытия окна отклоняется
  Update = UpdateClass();
  CHECK_EQ(otaUpload(fwImage(2000)).code, 403);
  CHECK(Update.data.empty());
}

TEST(ota_requires_the_page_header_even_when_open) {
  resetAll();
  otaOpenWindow();
  const std::string img = fwImage(3000);
  CHECK_EQ(otaUpload(img, {}).code, 403);
  CHECK_EQ(otaUpload(img, {{"X-Requested-With", "evil"}}).code, 403);
  CHECK(!Update.began && Update.data.empty());
  CHECK(otaOpen());   // отказ окно не закрывает
  CHECK(!otaBusy);
  CHECK(!rebooted());
  CHECK_EQ(otaUpload(img).code, 200);   // с заголовком - принимается
}

TEST(ota_rejects_bad_images_and_allows_retry) {
  resetAll();
  otaOpenWindow();
  // не прошивка: первый байт не 0xE9
  Resp r = otaUpload(fwImage(4000, 0x7F));
  CHECK_EQ(r.code, 500);
  CHECK(r.body.find("\"error\":\"update\"") != std::string::npos);
  CHECK(!otaBusy);
  CHECK(otaOpen());
  CHECK(!rebooted());
  // пустой файл
  CHECK_EQ(otaUpload("").code, 500);
  CHECK(!rebooted());
  // не помещается
  ESP.freeSketchSpace = 0x1000 + 0x1000;   // после вычитания служебной страницы остаётся 4 КБ
  CHECK_EQ(otaUpload(fwImage(10000)).code, 500);
  CHECK(!rebooted());
  // нет места вообще
  ESP.freeSketchSpace = 1019904;
  Update.failBegin = true;
  CHECK_EQ(otaUpload(fwImage(4000)).code, 500);
  Update.failBegin = false;
  // сбой записи на половине файла
  Update.failWriteAfter = 3000;
  CHECK_EQ(otaUpload(fwImage(5000)).code, 500);
  CHECK(!otaBusy && !rebooted());
  Update.failWriteAfter = -1;
  // после всех неудач нормальный образ проходит
  CHECK_EQ(otaUpload(fwImage(5000)).code, 200);
  CHECK(rebooted() || (fakeMillis() += 900, rebooted()));
}

TEST(ota_aborted_upload_does_not_reboot) {
  resetAll();
  otaOpenWindow();
  Resp r = webServer.testUpload("/update", kHdr, "firmware.bin", split(fwImage(6000), 1460), true);
  CHECK_EQ(r.code, 0);    // ответа нет: соединение оборвано
  CHECK(!otaBusy);
  CHECK(!Update.began && Update.error);   // Update.end() вызван, недозаписанный образ не принят
  fakeMillis() += 2000;
  CHECK(!rebooted());
  CHECK(otaOpen());
}

TEST(ota_button_three_clicks_open_the_window) {
  resetAll();
  webReady();
  CHECK(!otaOpen());
  btn.clicks = 3;
  button();
  CHECK(otaOpen());
  CHECK(cfg.power);                    // три нажатия не выключают гирлянду
  CHECK(!stripObj.shown.empty());      // голубая вспышка показана
  CHECK_EQ(jnum(webGet("/api/ota"), "open"), 1);
  CHECK_EQ(otaUpload(fwImage(2500)).code, 200);
}

TEST(button_other_clicks_do_not_open_ota) {
  resetAll();
  btn.clicks = 1;
  button();
  CHECK(!cfg.power && !otaOpen());     // одно нажатие - питание
  btn.clicks = 1;
  button();
  CHECK(cfg.power);
  btn.clicks = 2;
  curEff = 0;
  button();
  CHECK_EQ(curEff, 1);                  // два нажатия - следующий эффект
  CHECK(!otaOpen());
  btn.clicks = 4;
  button();
  CHECK(!otaOpen());
}

TEST(button_brightness_hold_is_limited_and_safe) {
  resetAll();
  cfg.bright = 240;
  btn.stepF = true;
  button();
  CHECK_EQ(cfg.bright, 255);            // не выходит за 255
  btn.relF = true;
  button();                              // смена направления
  btn.stepF = true;
  button();
  CHECK_EQ(cfg.bright, 225);
  cfg.power = false;
  btn.stepF = true;
  button();
  CHECK_EQ(cfg.bright, 225);            // при выключенной ленте яркость не меняется
}

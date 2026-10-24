// Хост-тесты прошивки: настоящие parsing.ino, switcher.ino, power.ino и effects.ino
// собираются на ПК вместе с заглушками из host_stubs.h (см. tests/README.md).
#include "host_env.h"
#include "minitest.h"

// ---- помощники ----
typedef std::vector<uint8_t> Bytes;

static void resetAll() {
  cfg = Cfg();
  mm = MM();
  pwr = Pwr();
  for (auto& e : effs) e = Effects();
  fxDefaults(effsX);
  sched = Sched();   // расписание и часы
  fakeEpoch() = 0;
  fakeNtpOn() = false;
  schedNtp = false;
  schedLastMin = 0;
  guard = Guard();   // защита токеном выключена, таблица неверных попыток пуста
  memset(guardCnt, 0, sizeof(guardCnt));
  memset(guardLocked, 0, sizeof(guardLocked));
  memset(guardIp, 0, sizeof(guardIp));
  fakeClientIp() = 0x0A00000A;
  memset(xy, 0, sizeof(xy));
  for (auto& l : leds) l = CRGB();
  stripObj = CLEDController();
  stripObj.record = false;
  stripObj.setLeds(leds, LED_MAX);
  udp.in.clear();
  udp.out.clear();
  forceTmr = Timer(30000, false);
  hlTmr = Timer(60000, false);
  otaTmr = Timer(300000, false);
  otaBusy = false;
  otaRebootAt = 0;
  Update = UpdateClass();
  ESP = EspClass();
  btn = EncButtonStub();
  vcRun = false;
  vcFrame = -1;
  vcDelay = VC_DELAY_DEFAULT;
  vcStep = VC_STEP_DEFAULT;
  switchTmr = Timer(0, false);
  offTmr = Timer(60000, false);
  calibF = false;
  curEff = forceEff = 0;
  EEcfg = EEManager(cfg);
  EEeff = EEManager(effs);
  EEfx = EEManager(effsX);
  EEsched = EEManager(sched);
  EEguard = EEManager(guard);
  EExy = EEManager(xy);
  EEmm = EEManager(mm);
  EEpwr = EEManager(pwr);
  LittleFS.files.clear();
  LittleFS.failWrites = false;
  memset(scenes, 0, sizeof(scenes));
  fakeMillis() += 1000;   // чтобы не попасть в "антидребезг" предыдущего теста
}

static void push(const Bytes& b) { udp.in.push_back(b); }

// пакет старого клиента: 'G','T', cmd, args...
static Bytes oldPkt(std::initializer_list<int> a) {
  Bytes b = {'G', 'T'};
  for (int v : a) b.push_back((uint8_t)v);
  return b;
}
// пакет с запросом ACK: 'G','T', cmd|0x80, seq, args...
static Bytes ackPkt(int seq, std::initializer_list<int> a) {
  Bytes b = {'G', 'T'};
  bool first = true;
  for (int v : a) {
    b.push_back((uint8_t)(first ? (v | 0x80) : v));
    if (first) b.push_back((uint8_t)seq);
    first = false;
  }
  return b;
}
// отправить пакет старого клиента и обработать (время сдвигается, чтобы пройти "антидребезг")
static void send(std::initializer_list<int> a) {
  fakeMillis() += 60;
  push(oldPkt(a));
  parsing();
}
static void sendAck(int seq, std::initializer_list<int> a) {
  push(ackPkt(seq, a));
  parsing();
}
static int seqCounter = 0;
// seq не должен повторяться между тестами: прошивка помнит последний seq в статической переменной
static int newSeq() { return (seqCounter += 7) & 0xFF; }
static Bytes lastOut() { return udp.out.empty() ? Bytes() : udp.out.back(); }

// ================= старые клиенты =================
TEST(old_client_sets_brightness) {
  resetAll();
  send({2, 2, 77});
  CHECK_EQ(cfg.bright, 77);
  CHECK(EEcfg.updates > 0);
  CHECK(udp.out.empty());   // старые клиенты ответа на запись не получают
}

TEST(old_client_debounce_drops_fast_repeat) {
  resetAll();
  send({2, 2, 10});
  push(oldPkt({2, 2, 20}));   // сразу, без паузы
  parsing();
  CHECK_EQ(cfg.bright, 10);
  send({2, 2, 30});
  CHECK_EQ(cfg.bright, 30);
}

TEST(garbage_is_ignored) {
  resetAll();
  send({'X', 'Y', 2, 2, 5});   // неверный заголовок
  push(Bytes{'G', 'T'});
  parsing();
  push(Bytes{'G'});
  parsing();
  CHECK_EQ(cfg.bright, 100);
  CHECK(udp.out.empty());
}

TEST(oversize_packet_does_not_overflow) {
  resetAll();
  Bytes big = {'G', 'T', 2, 2, 55};
  big.resize(200, 0xEE);   // сборка с AddressSanitizer поймает выход за ubuf
  fakeMillis() += 60;
  push(big);
  parsing();
  CHECK_EQ(cfg.bright, 55);
  Bytes exact = {'G', 'T', 2, 2, 56};
  exact.resize(MAX_UDP_PACKET, 0);   // ровно по границе буфера
  fakeMillis() += 60;
  push(exact);
  parsing();
  CHECK_EQ(cfg.bright, 56);
}

TEST(query_ip) {
  resetAll();
  uint32_t t = fakeMillis() + 60;
  send({0});
  CHECK_EQ(udp.out.size(), 1);
  Bytes r = lastOut();
  CHECK_EQ(r.size(), 4);
  CHECK(r[2] == 0 && r[3] == 42);
  CHECK(fakeMillis() >= t + 84);   // задержка myIP[3]*2 против одновременных ответов
}

TEST(query_cfg_reply) {
  resetAll();
  cfg.ledAm = 345;
  cfg.power = 1;
  cfg.bright = 200;
  cfg.autoCh = 1;
  cfg.rndCh = 0;
  cfg.prdCh = 7;
  cfg.turnOff = 1;
  cfg.offTmr = 90;
  send({1});
  Bytes r = lastOut();
  CHECK_EQ(r.size(), 12);
  const uint8_t want[10] = {1, 3, 45, 1, 200, 1, 0, 7, 1, 90};
  for (int i = 0; i < 10; i++) CHECK_EQ(r[2 + i], want[i]);
}

// ================= проверка входных данных =================
TEST(led_amount_is_clamped) {
  resetAll();
  send({2, 0, 255, 255});
  CHECK_EQ(cfg.ledAm, LED_MAX);
  CHECK_EQ(stripObj.count, LED_MAX);
  send({2, 0, 0, 0});
  CHECK_EQ(cfg.ledAm, 1);
  send({2, 0, 1, 20});
  CHECK_EQ(cfg.ledAm, 120);
  CHECK_EQ(stripObj.count, 120);
}

TEST(calibration_index_is_checked) {
  resetAll();
  send({3, 0});
  CHECK(calibF);
  send({3, 1, 5, 1, 9, 9});   // номер 501 > LED_MAX: игнорируется
  CHECK_EQ(xy[0][0], 0);
  CHECK_EQ(xy[LED_MAX - 1][0], 0);
  send({3, 1, 5, 0, 11, 22});   // номер 500: только сохранить координаты последнего (xy[499])
  CHECK_EQ(xy[LED_MAX - 1][0], 11);
  CHECK_EQ(xy[LED_MAX - 1][1], 22);
  send({3, 1, 0, 3, 33, 44});   // номер 3: сохраняем координаты 2-го, зажигаем 3-й
  CHECK_EQ(xy[2][0], 33);
  CHECK_EQ(xy[2][1], 44);
  CHECK(leds[3] == CRGB(CRGB::White));
  CHECK_EQ(stripObj.shown.back(), 200);
}

TEST(calibration_finish_computes_bounds) {
  resetAll();
  cfg.ledAm = 4;
  const uint8_t pts[4][2] = {{10, 40}, {50, 20}, {30, 90}, {20, 60}};
  memcpy(xy, pts, sizeof(pts));
  send({3, 0});
  send({3, 2});
  CHECK(!calibF);
  CHECK_EQ(mm.minX, 10);
  CHECK_EQ(mm.maxX, 50);
  CHECK_EQ(mm.minY, 20);
  CHECK_EQ(mm.maxY, 90);
  CHECK_EQ(mm.w, 40);
  CHECK_EQ(mm.h, 70);
  CHECK(EExy.nows > 0 && EEmm.nows > 0);
}

TEST(effect_select_and_edit) {
  resetAll();
  effs[5].fav = false;
  effs[5].scale = 11;
  effs[5].speed = 22;
  send({4, 0, 5});
  Bytes r = lastOut();
  CHECK_EQ(r.size(), 6);
  CHECK(r[2] == 4 && r[3] == 0 && r[4] == 11 && r[5] == 22);
  CHECK_EQ(forceEff, 5);
  CHECK(forceTmr.state());
  send({4, 2, 99});
  send({4, 3, 77});
  send({4, 1, 1});
  CHECK(effs[5].fav);
  CHECK_EQ(effs[5].scale, 99);
  CHECK_EQ(effs[5].speed, 77);
}

TEST(effect_index_is_checked) {
  resetAll();
  forceEff = 3;
  udp.out.clear();
  send({4, 0, EFFECT_COUNT});   // нет такого эффекта
  send({4, 0, 255});
  CHECK_EQ(forceEff, 3);
  CHECK(udp.out.empty());
  send({4, 0, EFFECT_COUNT - 1});   // последний новый эффект есть
  CHECK_EQ(forceEff, EFFECT_COUNT - 1);
}

TEST(new_effect_settings_go_to_their_own_block) {
  resetAll();
  Effects before[ACTIVE_PALETTES * 2];
  memcpy(before, effs, sizeof(effs));
  send({4, 0, ACTIVE_PALETTES * 2 + 2});   // огонь
  CHECK_EQ(forceEff, ACTIVE_PALETTES * 2 + 2);
  Bytes r = lastOut();   // ответ с настройками нового эффекта
  CHECK_EQ(r.size(), 6);
  CHECK(r[2] == 4 && r[3] == 0 && r[4] == effsX[2].scale && r[5] == effsX[2].speed);
  send({4, 1, 1});
  send({4, 2, 99});
  send({4, 3, 77});
  CHECK(effsX[2].fav);
  CHECK_EQ(effsX[2].scale, 99);
  CHECK_EQ(effsX[2].speed, 77);
  CHECK(EEfx.updates > 0);
  CHECK(memcmp(before, effs, sizeof(effs)) == 0);   // блок старых эффектов не тронут
}

// ================= расширения протокола =================
TEST(capabilities_and_power_limit) {
  resetAll();
  send({5});
  CHECK(lastOut() == (Bytes{'G', 'T', 5, 1}));
  send({7});
  CHECK(lastOut() == (Bytes{'G', 'T', 7, 0, 0}));
  send({2, 9, 3, 50});
  CHECK_EQ(pwr.maxMa, 350);
  CHECK(EEpwr.updates > 0);
  send({7});
  CHECK(lastOut() == (Bytes{'G', 'T', 7, 3, 50}));
}

TEST(unknown_command_is_silent_for_old_clients) {
  resetAll();
  send({99});
  CHECK(udp.out.empty());
}

// ================= ACK =================
TEST(ack_executes_and_confirms) {
  resetAll();
  int s = newSeq();
  sendAck(s, {2, 2, 50});
  CHECK_EQ(cfg.bright, 50);
  CHECK_EQ(udp.out.size(), 1);
  CHECK(lastOut() == (Bytes{'G', 'T', 6, 2, (uint8_t)s, 0}));
}

TEST(ack_shifts_arguments_correctly) {
  resetAll();
  sendAck(newSeq(), {2, 0, 1, 20});
  CHECK_EQ(cfg.ledAm, 120);
  cfg.turnOff = true;
  sendAck(newSeq(), {2, 7});   // без значения: прошивка видит 0
  CHECK(!cfg.turnOff);
}

TEST(ack_duplicate_is_not_executed_twice) {
  resetAll();
  int s = newSeq();
  sendAck(s, {2, 6});   // следующий эффект
  CHECK_EQ(curEff, 1);
  sendAck(s, {2, 6});   // повтор с тем же seq (ACK потерялся)
  CHECK_EQ(curEff, 1);
  CHECK_EQ(udp.out.size(), 2);
  CHECK(udp.out[0] == udp.out[1]);   // ACK повторён
  int s2 = newSeq();
  sendAck(s2, {2, 6});   // новая команда выполняется
  CHECK_EQ(curEff, 2);
}

TEST(ack_duplicate_keeps_newer_state) {
  resetAll();
  int s = newSeq();
  sendAck(s, {2, 2, 10});
  cfg.bright = 99;   // состояние изменилось после выполнения
  sendAck(s, {2, 2, 10});   // запоздавший повтор не должен его затирать
  CHECK_EQ(cfg.bright, 99);
}

TEST(ack_queries_are_reexecuted) {
  resetAll();
  int s = newSeq();
  sendAck(s, {1});
  CHECK_EQ(udp.out.size(), 2);   // данные + ACK
  sendAck(s, {1});
  CHECK_EQ(udp.out.size(), 4);   // повтор запроса снова даёт данные
  s = newSeq();
  sendAck(s, {7});
  sendAck(s, {7});
  CHECK_EQ(udp.out.size(), 8);
  s = newSeq();
  sendAck(s, {4, 0, 2});
  sendAck(s, {4, 0, 2});
  CHECK_EQ(udp.out.size(), 12);
}

TEST(ack_skips_debounce) {
  resetAll();
  push(ackPkt(newSeq(), {2, 2, 10}));
  push(ackPkt(newSeq(), {2, 2, 20}));   // без паузы между пакетами
  parsing();
  parsing();
  CHECK_EQ(cfg.bright, 20);
}

TEST(ack_reports_invalid_arguments_and_unknown_commands) {
  resetAll();
  int s = newSeq();
  sendAck(s, {3, 1, 5, 1, 0, 0});   // номер светодиода 501
  CHECK(lastOut() == (Bytes{'G', 'T', 6, 3, (uint8_t)s, 1}));
  sendAck(s, {3, 1, 5, 1, 0, 0});   // повтор: тот же статус
  CHECK(lastOut() == (Bytes{'G', 'T', 6, 3, (uint8_t)s, 1}));
  s = newSeq();
  sendAck(s, {4, 0, 200});
  CHECK(lastOut() == (Bytes{'G', 'T', 6, 4, (uint8_t)s, 1}));
  s = newSeq();
  sendAck(s, {99});
  CHECK(lastOut() == (Bytes{'G', 'T', 6, 99, (uint8_t)s, 2}));
}

TEST(ack_short_packet_is_ignored) {
  resetAll();
  push(Bytes{'G', 'T', 0x82});
  parsing();
  CHECK(udp.out.empty());
}

// ================= смена эффектов =================
TEST(switch_without_favorites_does_not_hang) {
  resetAll();
  for (auto& e : effs) e.fav = false;
  curEff = 4;
  switchEff();
  CHECK_EQ(curEff, 4);
  cfg.rndCh = 1;
  switchEff();
  CHECK_EQ(curEff, 4);
}

TEST(switch_sequential_skips_non_favorites_and_wraps) {
  resetAll();
  for (auto& e : effs) e.fav = false;
  effs[3].fav = effs[7].fav = true;
  curEff = 0;
  switchEff();
  CHECK_EQ(curEff, 3);
  switchEff();
  CHECK_EQ(curEff, 7);
  switchEff();
  CHECK_EQ(curEff, 3);
}

TEST(switch_random_picks_only_favorites) {
  resetAll();
  for (auto& e : effs) e.fav = false;
  effs[4].fav = effs[15].fav = true;
  cfg.rndCh = 1;
  bool saw4 = false, saw15 = false;
  for (int i = 0; i < 300; i++) {
    switchEff();
    CHECK(curEff == 4 || curEff == 15);
    saw4 |= curEff == 4;
    saw15 |= curEff == 15;
  }
  CHECK(saw4 && saw15);
}

TEST(switch_includes_new_effects_only_when_favorite) {
  resetAll();
  for (auto& e : effs) e.fav = false;   // новые по умолчанию не избранные
  effs[3].fav = true;
  for (int i = 0; i < 40; i++) {
    switchEff();
    CHECK_EQ(curEff, 3);   // новые эффекты в обход не попадают
  }
  effsX[1].fav = true;   // бегущие огни
  curEff = 3;
  switchEff();
  CHECK_EQ(curEff, ACTIVE_PALETTES * 2 + 1);
  switchEff();
  CHECK_EQ(curEff, 3);   // после последнего - круг сначала
  for (auto& e : effs) e.fav = false;
  cfg.rndCh = 1;
  for (int i = 0; i < 30; i++) {
    switchEff();
    CHECK_EQ(curEff, ACTIVE_PALETTES * 2 + 1);   // случайный порядок тоже находит избранный новый
  }
}

// ================= ограничение тока =================
TEST(power_limit_off_keeps_brightness) {
  resetAll();
  for (int i = 0; i < 100; i++) leds[i] = CRGB(CRGB::White);
  cfg.ledAm = 100;
  CHECK_EQ(limitedBright(255), 255);
  CHECK_EQ(limitedBright(77), 77);
}

TEST(power_limit_reduces_brightness_with_factor) {
  resetAll();
  for (int i = 0; i < 100; i++) leds[i] = CRGB(CRGB::White);
  cfg.ledAm = 100;
  pwr.maxMa = 1000;   // x0.7 = 700 мА, 5 В = 3500 мВт
  uint8_t b = limitedBright(255);
  CHECK_EQ(b, 41);   // без коэффициента 0.7 было бы 59
  uint32_t mW = calculate_unscaled_power_mW(leds, 100) * b / 256;
  CHECK(mW <= 3500);
}

TEST(power_limit_does_not_touch_dark_frame) {
  resetAll();
  cfg.ledAm = 100;
  pwr.maxMa = 1000;   // лимит 3500 мВт, чёрный кадр потребляет 100 * 5 = 500 мВт
  CHECK_EQ(limitedBright(200), 200);
}

// ================= эффекты =================
static void runEffect(int eff) {
  curEff = eff;
  forceTmr.stop();
  fakeMillis() += 40;
  effects();
}

TEST(effects_survive_zero_height_and_full_strip) {
  resetAll();
  cfg.ledAm = LED_MAX;
  mm.h = 0;   // все светодиоды на одной линии: раньше деление на ноль
  mm.w = 0;
  for (int i = 0; i < LED_MAX; i++) {
    xy[i][0] = (uint8_t)(i * 3);
    xy[i][1] = 10;
  }
  runEffect(0);    // градиент
  runEffect(15);   // шум Перлина
  CHECK_EQ(stripObj.shown.size(), 2);
}

TEST(effects_respect_led_amount_and_limit) {
  resetAll();
  cfg.ledAm = 10;
  cfg.bright = 200;
  leds[10] = CRGB(1, 2, 3);   // за пределами ledAm
  for (int i = 0; i < 10; i++) {
    xy[i][0] = i * 20;
    xy[i][1] = i * 10;
  }
  runEffect(1);
  CHECK(leds[10] == CRGB(1, 2, 3));
  CHECK_EQ(stripObj.shown.back(), 200);   // лимит выключен
  pwr.maxMa = 30;                         // очень мало: яркость должна упасть
  runEffect(1);
  CHECK(stripObj.shown.back() < 200);
}

// ---- новые эффекты ----
static const int NEW0 = ACTIVE_PALETTES * 2;

// вертикальная гирлянда: светодиод 0 вверху (Y мал), последний внизу (Y велик, ось Y направлена вниз как в кадре камеры)
static void lineUp(int n) {
  cfg.ledAm = n;
  for (int i = 0; i < n; i++) {
    xy[i][0] = 100 + (i % 7);
    xy[i][1] = i * 200 / (n - 1);
  }
  recalcBounds();
}

static int sumRgb(int a, int b) {
  int s = 0;
  for (int i = a; i < b; i++) s += leds[i].r + leds[i].g + leds[i].b;
  return s;
}

TEST(new_effects_render_inside_the_strip_and_stay_alive) {
  resetAll();
  lineUp(100);
  leds[100] = CRGB(9, 8, 7);   // за пределами ledAm
  for (int e = 0; e < EXT_EFFECTS; e++) {
    for (auto& l : leds) l = CRGB();
    leds[100] = CRGB(9, 8, 7);
    size_t before = stripObj.shown.size();
    for (int f = 0; f < 120; f++) runEffect(NEW0 + e);   // 120 кадров: хватит и на искры, и на разгон дыхания
    CHECK_EQ(stripObj.shown.size(), before + 120);
    CHECK(leds[100] == CRGB(9, 8, 7));                    // ничего за пределами ledAm
    CHECK(sumRgb(0, 100) > 0);                            // эффект что-то показывает
  }
}

TEST(new_effects_survive_zero_size_map_and_extreme_settings) {
  resetAll();
  cfg.ledAm = LED_MAX;
  for (int i = 0; i < LED_MAX; i++) xy[i][0] = xy[i][1] = 77;   // все в одной точке: ширина и высота 0
  recalcBounds();
  CHECK(mm.w == 0 && mm.h == 0);
  const int settings[][2] = {{0, 0}, {255, 255}, {0, 255}, {255, 0}, {128, 128}};
  for (int e = 0; e < EXT_EFFECTS; e++) {
    for (auto& s : settings) {
      effsX[e].scale = s[0];
      effsX[e].speed = s[1];
      for (int f = 0; f < 5; f++) runEffect(NEW0 + e);
    }
  }
  CHECK(true);   // главное - без деления на ноль и выхода за массивы (ASan/UBSan)
}

TEST(fire_is_hotter_at_the_bottom_than_at_the_top) {
  resetAll();
  lineUp(300);
  for (int f = 0; f < 40; f++) runEffect(NEW0 + 2);
  int top = sumRgb(0, 60), bottom = sumRgb(240, 300);
  CHECK(bottom > top * 2);
}

TEST(twinkle_sparks_fade_out_and_scale_controls_density) {
  resetAll();
  lineUp(200);
  effsX[0].scale = 0;   // без новых искр: оставшееся гаснет
  for (auto& l : leds) l = CRGB(200, 200, 200);
  for (int f = 0; f < 80; f++) runEffect(NEW0);
  CHECK_EQ(sumRgb(0, 200), 0);

  auto litAfter = [&](byte scale) {
    for (auto& l : leds) l = CRGB();
    effsX[0].scale = scale;
    effsX[0].speed = 150;
    for (int f = 0; f < 200; f++) runEffect(NEW0);
    int lit = 0;
    for (int i = 0; i < 200; i++) lit += leds[i].r + leds[i].g + leds[i].b > 0;
    return lit;
  };
  int few = litAfter(20), many = litAfter(250);
  CHECK(many > few);
  CHECK(many <= 200);
}

TEST(new_effect_cross_fade_from_old_effect) {
  resetAll();
  lineUp(50);
  runEffect(1);   // обычный эффект
  for (int i = 0; i < 50; i++) leds[i] = CRGB(200, 200, 200);
  runEffect(NEW0 + 3);   // дыхание: на первом кадре цвет ещё близок к прежнему, а не рывок в тёмное
  CHECK(leds[10].r > 150 && leds[10].r < 200);
}

#include "test_web_cases.h"
#include "test_scene_cases.h"
#include "test_sched_cases.h"
#include "test_coords_cases.h"
#include "test_vcalib_cases.h"
#include "test_ota_cases.h"
#include "test_guard_cases.h"

int main() { return runAllTests(); }

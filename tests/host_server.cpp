// Хост-сервер для стенда протокола (tests/protocol_stand.py) и e2e-тестов веб-интерфейса (tests/web/ui_test.mjs):
// настоящий код прошивки (parsing.ino, web.ino и др.) слушает UDP-порт и (если задан GT_WEB_PORT) HTTP-порт на 127.0.0.1.
// После каждого изменения состояния в stdout печатается строка STATE: по ней тесты проверяют, что на самом деле
// изменилось в прошивке, независимо от ответов API.
#define HOST_SOCKET_UDP
#include "host_env.h"
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: gt_host PORT\n");
    return 2;
  }
  if (!udp.begin((uint16_t)std::atoi(argv[1]))) {
    std::fprintf(stderr, "bind failed\n");
    return 1;
  }
  stripObj.setLeds(leds, LED_MAX);
  fxDefaults(effsX);   // как в setup() прошивки
  fire2Defaults(fire2);
  webBegin();
  fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);   // команды тестов по stdin (например BUTTON3 - три нажатия кнопки)
  std::string cmdBuf;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::printf("READY\n");

  auto t0 = std::chrono::steady_clock::now();
  std::string last;
  for (;;) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    fakeMillis() = 100000 + (uint32_t)ms;   // настоящее время для "антидребезга" и таймеров
    char rb[64];
    ssize_t rn = read(0, rb, sizeof(rb));
    if (rn > 0) {
      cmdBuf.append(rb, rn);
      size_t nl;
      while ((nl = cmdBuf.find('\n')) != std::string::npos) {
        std::string cmd = cmdBuf.substr(0, nl);
        cmdBuf.erase(0, nl + 1);
        if (cmd.rfind("BUTTON", 0) == 0) {   // BUTTON1, BUTTON2, BUTTON3: нажатия кнопки
          btn.clicks = atoi(cmd.c_str() + 6);
          button();
        }
      }
    }
    // часы расписания идут вместе с настоящим временем после установки (POST /api/time)
    static uint32_t timeBase = 0, timeExpected = 0;
    static int64_t timeBaseMs = 0;
    if (fakeEpoch() != timeExpected) {
      timeBase = timeExpected = fakeEpoch();
      timeBaseMs = ms;
    } else if (timeBase) {
      timeExpected = fakeEpoch() = timeBase + (uint32_t)((ms - timeBaseMs) / 1000);
    }
    parsing();
    webTick();
    vcalibTick();
    schedTick();
    if (ESP.restarted) {   // «перезагрузка» после обновления: сохраняем принятый образ и выходим
      if (const char* outp = getenv("GT_OTA_OUT")) {
        FILE* f = fopen(outp, "wb");
        if (f) {
          fwrite(Update.data.data(), 1, Update.data.size(), f);
          fclose(f);
        }
      }
      std::printf("REBOOT\n");
      return 0;
    }
    int lit = -1;   // номер горящего светодиода (подсветка в редакторе координат)
    for (int i = 0; i < LED_MAX && lit < 0; i++) {
      if (leds[i].r || leds[i].g || leds[i].b) lit = i;
    }
    char st[400];
    std::snprintf(st, sizeof(st),
                  "STATE bright=%d curEff=%d ledAm=%d maxMa=%d power=%d auto=%d rnd=%d period=%d offOn=%d offMin=%d force=%d forceEff=%d "
                  "fav=%d scale=%d speed=%d calib=%d lit=%d mmw=%d mmh=%d ota=%d count=%d order=%d",
                  cfg.bright, curEff, cfg.ledAm, pwr.maxMa, cfg.power, cfg.autoCh, cfg.rndCh, cfg.prdCh, cfg.turnOff, cfg.offTmr,
                  forceTmr.state(), forceEff, effAt(forceEff).fav, effAt(forceEff).scale, effAt(forceEff).speed, calibF, lit, mm.w, mm.h, otaOpen() ? 1 : 0, EFFECT_COUNT, colorOrder.order);
    if (last != st) {
      last = st;
      std::printf("%s\n", st);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(300));
  }
}

// Хост-сервер для стенда протокола (tests/protocol_stand.py) и e2e-тестов веб-интерфейса (tests/web/ui_test.mjs):
// настоящий код прошивки (parsing.ino, web.ino и др.) слушает UDP-порт и (если задан GT_WEB_PORT) HTTP-порт на 127.0.0.1.
// После каждого изменения состояния в stdout печатается строка STATE: по ней тесты проверяют, что на самом деле
// изменилось в прошивке, независимо от ответов API.
#define HOST_SOCKET_UDP
#include "host_env.h"
#include <chrono>
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
  webBegin();
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::printf("READY\n");

  auto t0 = std::chrono::steady_clock::now();
  std::string last;
  for (;;) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    fakeMillis() = 100000 + (uint32_t)ms;   // настоящее время для "антидребезга" и таймеров
    parsing();
    webTick();
    char st[320];
    std::snprintf(st, sizeof(st),
                  "STATE bright=%d curEff=%d ledAm=%d maxMa=%d power=%d auto=%d rnd=%d period=%d offOn=%d offMin=%d force=%d forceEff=%d "
                  "fav=%d scale=%d speed=%d",
                  cfg.bright, curEff, cfg.ledAm, pwr.maxMa, cfg.power, cfg.autoCh, cfg.rndCh, cfg.prdCh, cfg.turnOff, cfg.offTmr,
                  forceTmr.state(), forceEff, effs[forceEff].fav, effs[forceEff].scale, effs[forceEff].speed);
    if (last != st) {
      last = st;
      std::printf("%s\n", st);
    }
    std::this_thread::sleep_for(std::chrono::microseconds(300));
  }
}

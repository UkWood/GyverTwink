// Общая среда хост-сборки прошивки: заглушки, глобальные объекты (как в GyverTwink_v1.2.ino) и сами .ino.
// Используется юнит-тестами (test_firmware.cpp) и хост-сервером для стенда протокола (host_server.cpp).
#pragma once
#include "host_stubs.h"

#define LED_MAX 500
#define POWER_LIMIT_MA 0
#define POWER_VOLTS 5
#define FW_VERSION "host"
#define DEBUGLN(x)
#define DEBUG(x)

// время для расписания (schedule.ino): тесты задают его сами
inline uint32_t& fakeEpoch() { static uint32_t e = 0; return e; }
inline bool& fakeNtpOn() { static bool n = false; return n; }
#define GT_NOW() fakeEpoch()
#define GT_SET_TIME(s) (fakeEpoch() = (s))
#define GT_NTP_BEGIN() (fakeNtpOn() = true)

#include "../firmware/GyverTwink_v1.2/Timer.h"
#include "../firmware/GyverTwink_v1.2/web_page.h"
#include "../firmware/GyverTwink_v1.2/types.h"

// ---- глобальные объекты, как в GyverTwink_v1.2.ino ----
WiFiUDP udp;
CRGB leds[LED_MAX];
CLEDController stripObj;
CLEDController* strip = &stripObj;
IPAddress myIP(192, 168, 1, 42);
Cfg cfg;
byte xy[LED_MAX][2];
MM mm;
Pwr pwr;
Effects effs[ACTIVE_PALETTES * 2];
Effects effsX[EXT_EFFECTS];
Fire2 fire2;
Sched sched;
Guard guard;
EEManager EEcfg(cfg), EExy(xy), EEmm(mm), EEpwr(pwr), EEeff(effs), EEfx(effsX), EEsched(sched), EEguard(guard), EEfire2(fire2);
CRGBPalette16 paletteArr[ACTIVE_PALETTES];
// порт веб-сервера берётся из GT_WEB_PORT; без него сервер без сети (юнит-тесты вызывают обработчики через test())
EncButtonStub btn;
ESP8266WebServer webServer(getenv("GT_WEB_PORT") ? atoi(getenv("GT_WEB_PORT")) : 0);
Timer forceTmr(30000, false);
Timer switchTmr(0, false);
Timer offTmr(60000, false);
Timer hlTmr(60000, false);
Timer otaTmr(300000, false);
bool calibF = false;
bool otaBusy = false;
byte curEff = 0;
byte forceEff = 0;

// ---- тестируемый код ----
#include "../firmware/GyverTwink_v1.2/power.ino"
#include "../firmware/GyverTwink_v1.2/switcher.ino"
#include "../firmware/GyverTwink_v1.2/settings.ino"
#include "../firmware/GyverTwink_v1.2/scenes.ino"
#include "../firmware/GyverTwink_v1.2/schedule.ino"
#include "../firmware/GyverTwink_v1.2/guard.ino"
#include "../firmware/GyverTwink_v1.2/coords.ino"
#include "../firmware/GyverTwink_v1.2/vcalib.ino"
#include "../firmware/GyverTwink_v1.2/faders.ino"
#include "../firmware/GyverTwink_v1.2/ota.ino"
#include "../firmware/GyverTwink_v1.2/button.ino"
#include "../firmware/GyverTwink_v1.2/effects.ino"
#include "../firmware/GyverTwink_v1.2/parsing.ino"
#include "../firmware/GyverTwink_v1.2/web.ino"


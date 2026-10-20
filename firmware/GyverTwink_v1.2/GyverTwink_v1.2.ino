/*
  Скетч к проекту "GyverTwink"
  - Страница проекта (схемы, описания): https://alexgyver.ru/gyvertwink/
  - Исходники на GitHub: https://github.com/AlexGyver/GyverTwink
  Проблемы с загрузкой? Читай гайд для новичков: https://alexgyver.ru/arduino-first/
  AlexGyver, AlexGyver Technologies, 2021
*/

/*
  1.1 - исправлена калибровка больше 255 светодиодов
  1.2 - исправлена ошибка с калибровкой
*/

/*
  Мигает синим - открыт портал
  Мигает жёлтым - подключаемся к точке
  Мигнул зелёным - подключился к точке
  Мигнул красным - ошибка подключения к точке
  Мигнул розовым - создал точку
*/

// ================ НАСТРОЙКИ ================
#define BTN_PIN D3      // пин кнопки
#define BTN_TOUCH 0     // 1 - сенсорная кнопка, 0 - нет

#define LED_PIN D1      // пин ленты
#define LED_TYPE WS2812 // чип ленты
#define LED_ORDER GRB   // порядок цветов ленты
#define LED_MAX 500     // макс. светодиодов

#define FW_VERSION "1.3-dev"   // версия прошивки (показывается в веб-интерфейсе)

// ограничение тока ленты: яркость автоматически снижается, чтобы не перегрузить блок питания
#define POWER_LIMIT_MA 0  // лимит по умолчанию в мА, 0 - выключен (меняется из приложения/по UDP)
#define POWER_VOLTS 5     // напряжение питания ленты

// имя точки в режиме AP
#define GT_AP_SSID "GyverTwink"
// пароль по умолчанию: используется, только если пароль не задан в портале настройки
// (старые установки и выход из портала без выбора). Рекомендуется задавать свой в портале: Access Point
#define GT_AP_PASS "12345678"
//#define DEBUG_SERIAL_GT   // раскомментируй, чтобы включить отладку

// ================== LIBS ==================
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <SimplePortal.h>
#include <FastLED.h>
#include <EEManager.h>
#include <EncButton.h>
#include "palettes.h"
#include "Timer.h"
#include "types.h"
#include "web_page.h"

// ================== OBJECTS ==================
ESP8266WebServer webServer(80);
WiFiUDP udp;
EEManager EEwifi(portalCfg);
CRGB leds[LED_MAX];
CLEDController *strip;
EncButton<EB_TICK, BTN_PIN> btn;
IPAddress myIP;

// ================== EEPROM BLOCKS ==================
Cfg cfg;
EEManager EEcfg(cfg);

byte xy[LED_MAX][2];
EEManager EExy(xy);

MM mm;
EEManager EEmm(mm);

Pwr pwr;
EEManager EEpwr(pwr);

Effects effs[ACTIVE_PALETTES * 2];
EEManager EEeff(effs);

// ================== MISC DATA ==================
Timer forceTmr(30000, false);
Timer switchTmr(0, false);
Timer offTmr(60000, false);
bool calibF = false;
byte curEff = 0;
byte forceEff = 0;

#ifdef DEBUG_SERIAL_GT
#define DEBUGLN(x) Serial.println(x)
#define DEBUG(x) Serial.print(x)
#else
#define DEBUGLN(x)
#define DEBUG(x)
#endif

// ================== SETUP ==================
void setup() {
#ifdef DEBUG_SERIAL_GT
  Serial.begin(115200);
  DEBUGLN();
#endif
  delay(200);
  if (BTN_TOUCH) btn.setButtonLevel(HIGH);
  startStrip();
  EEPROM.begin(2048); // с запасом!

  // если это первый запуск или щелчок по кнопке, открываем портал
  if (EEwifi.begin(0, 'a') || checkButton()) portalRoutine();

  // создаём точку или подключаемся к AP
  // в режиме AP с пустым SSID поле pass хранит пароль точки, заданный в портале
  bool apPass = portalCfg.mode == WIFI_AP && portalCfg.SSID[0] == '\0';
  if (portalCfg.mode == WIFI_AP || (portalCfg.mode == WIFI_STA && portalCfg.SSID[0] == '\0')) setupAP(apPass);
  else setupSTA();
  DEBUGLN(myIP);

  EEcfg.begin(EEwifi.nextAddr(), 'a');
  EEeff.begin(EEcfg.nextAddr(), 'a');
  EEmm.begin(EEeff.nextAddr(), (uint8_t)LED_MAX);
  EExy.begin(EEmm.nextAddr(), (uint8_t)LED_MAX);
  EEpwr.begin(EExy.nextAddr(), 'a');

  cfg.ledAm = constrain(cfg.ledAm, 1, LED_MAX);   // защита от мусора в епром
  switchTmr.setPrd(cfg.prdCh * 60000ul);
  offTmr.setPrd(cfg.offTmr * 60000ul);
  if (cfg.autoCh) switchTmr.restart();
  switchEff();
  cfg.turnOff = false;
  strip->setLeds(leds, cfg.ledAm);
  udp.begin(8888);
  webBegin();   // веб-интерфейс и mDNS (gyvertwink.local)
}

// ================== LOOP ==================
void loop() {
  button();   // опрос кнопки

  // менеджер епром
  EEcfg.tick();
  EEeff.tick();
  EEpwr.tick();

  parsing();  // парсим udp
  webTick();  // веб-интерфейс

  // таймер принудительного показа эффектов
  if (forceTmr.ready()) {
    forceTmr.stop();
    switchEff();
  }

  // форс выключен и настало время менять эффект
  if (!forceTmr.state() && switchTmr.ready()) switchEff();

  // таймер выключения
  if (offTmr.ready()) {
    offTmr.stop();
    cfg.turnOff = false;
    cfg.power = false;
    strip->showLeds(0);
    EEcfg.update();
    DEBUGLN("Off tmr");
  }

  // показываем эффект, если включены
  if (!calibF && cfg.power) effects();
}

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
  1.3 - веб-интерфейс (gyvertwink.local): управление, эффекты, сцены, калибровка по видео, редактор координат, обновление по WiFi;
        проверка входных данных UDP, подтверждение команд (ACK), ограничение тока, пароль точки доступа
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
#define LED_ORDER RGB   // порядок байт у FastLED не меняем: порядок цветов ленты - настройка в вебе (colorder.ino), по умолчанию GRB
#define LED_MAX 1000    // макс. светодиодов (не меньше XY_BASE, см. types.h)
#define EEPROM_SIZE 2560   // размер EEPROM (ограничение ESP8266 - 4096); хватает ли его блокам, проверяет static_assert ниже

#define FW_VERSION "1.4"   // версия прошивки (показывается в веб-интерфейсе)

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
#include <time.h>
#include <sys/time.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <Updater.h>
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

// координаты: один массив в ОЗУ, в EEPROM два блока (первые XY_BASE светодиодов и остальные, блок EExyHi лежит в конце памяти)
byte xy[LED_MAX][2];
EEManager EExy(*(byte (*)[XY_BASE][2])xy);
EEManager EExyHi(*(byte (*)[LED_MAX - XY_BASE][2])&xy[XY_BASE]);

MM mm;
EEManager EEmm(mm);

Pwr pwr;
EEManager EEpwr(pwr);

Effects effs[ACTIVE_PALETTES * 2];
EEManager EEeff(effs);

// новые эффекты (с номера ACTIVE_PALETTES * 2): отдельный блок в конце EEPROM, чтобы не сдвигать остальные
Effects effsX[EXT_EFFECTS];
EEManager EEfx(effsX);

// расписание (schedule.ino): тоже отдельный блок в конце
Sched sched;
EEManager EEsched(sched);

// защита токеном (guard.ino): тоже отдельный блок в конце
Guard guard;
EEManager EEguard(guard);

// настройки «Огня v2» (effects.ino): самый последний блок
Fire2 fire2;
EEManager EEfire2(fire2);

// все блоки EEPROM (12 штук, у каждого ещё байт-ключ) должны помещаться в EEPROM_SIZE; XY_BASE и LED_MAX связаны блоком координат
static_assert(LED_MAX >= XY_BASE, "LED_MAX не может быть меньше XY_BASE");
static_assert(sizeof(PortalCfg) + sizeof(Cfg) + sizeof(effs) + sizeof(MM) + sizeof(byte[XY_BASE][2]) + sizeof(Pwr) + sizeof(effsX) + sizeof(Sched) +
                  sizeof(Guard) + sizeof(Fire2) + sizeof(byte[LED_MAX - XY_BASE][2]) + sizeof(ColorOrder) + 12 <= EEPROM_SIZE,
              "блоки EEPROM не помещаются в EEPROM_SIZE");

// порядок цветов ленты (colorder.ino): тоже отдельный блок в конце
ColorOrder colorOrder;
EEManager EEcolor(colorOrder);

// ================== MISC DATA ==================
Timer forceTmr(30000, false);
Timer switchTmr(0, false);
Timer offTmr(60000, false);
Timer hlTmr(60000, false);   // автоматическое выключение подсветки светодиода из редактора координат
Timer otaTmr(300000, false);   // окно обновления прошивки по WiFi (ota.ino)
bool calibF = false;
bool otaBusy = false;   // идёт запись прошивки (ota.ino): эффекты отключены
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
  EEPROM.begin(EEPROM_SIZE);

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
  // ключ этих двух блоков всегда (uint8_t)XY_BASE = 244, как раньше при LED_MAX = 500: от LED_MAX он зависеть не должен
  EEmm.begin(EEeff.nextAddr(), (uint8_t)XY_BASE);
  EExy.begin(EEmm.nextAddr(), (uint8_t)XY_BASE);
  EEpwr.begin(EExy.nextAddr(), 'a');
  fxDefaults(effsX);   // значения по умолчанию, если блока ещё нет (прочитанный из EEPROM их заменит)
  EEfx.begin(EEpwr.nextAddr(), 'a');
  EEsched.begin(EEfx.nextAddr(), 'a');
  EEguard.begin(EEsched.nextAddr(), 'a');
  fire2Defaults(fire2);   // значения по умолчанию, если блока ещё нет
  EEfire2.begin(EEguard.nextAddr(), 'a');
  EExyHi.begin(EEfire2.nextAddr(), (uint8_t)(LED_MAX - XY_BASE));   // координаты светодиодов после XY_BASE-го
  EEcolor.begin(EExyHi.nextAddr(), 'a');   // порядок цветов ленты
  colorOrderApply();
  guard.token[TOKEN_MAX] = '\0';   // защита от мусора в EEPROM

  cfg.ledAm = constrain(cfg.ledAm, 1, LED_MAX);   // защита от мусора в епром
  switchTmr.setPrd(cfg.prdCh * 60000ul);
  offTmr.setPrd(cfg.offTmr * 60000ul);
  if (cfg.autoCh) switchTmr.restart();
  switchEff();
  cfg.turnOff = false;
  strip->setLeds(leds, cfg.ledAm);
  udp.begin(8888);
  schedBegin(WiFi.status() == WL_CONNECTED);   // NTP только при подключении к роутеру (в режиме точки доступа интернета нет)
  webBegin();   // веб-интерфейс и mDNS (gyvertwink.local)
}

// ================== LOOP ==================
void loop() {
  button();   // опрос кнопки

  // менеджер епром
  EEcfg.tick();
  EEeff.tick();
  EEpwr.tick();
  EEfx.tick();
  EEsched.tick();
  EEguard.tick();
  EEfire2.tick();
  EEcolor.tick();

  parsing();  // парсим udp
  webTick();  // веб-интерфейс
  vcalibTick();   // калибровка по видео (если запущена)
  schedTick();    // расписание

  // таймер принудительного показа эффектов: после него возвращается текущий эффект,
  // а менять его на следующий можно только при включенной автосмене
  if (forceTmr.ready()) {
    forceTmr.stop();
    if (cfg.autoCh) {
      switchEff();
      switchTmr.restart();
    }
  }

  // форс выключен и настало время менять эффект
  if (!forceTmr.state() && switchTmr.ready()) switchEff();

  // таймер выключения
  if (offTmr.ready()) {
    offTmr.stop();
    cfg.turnOff = false;
    cfg.power = false;
    ledsShow(0);
    EEcfg.update();
    DEBUGLN("Off tmr");
  }

  // показываем эффект, если включены
  if (!calibF && !otaBusy && cfg.power) effects();   // во время записи прошивки эффекты не рисуем
}

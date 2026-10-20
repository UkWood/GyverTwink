// Обновление прошивки по WiFi (OTA): страница загружает firmware.bin на /update (web.ino).
//
// Защита: обновление возможно только пока открыто «окно» - оно открывается ФИЗИЧЕСКИМ действием: три быстрых
// нажатия кнопки на гирлянде (button.ino), живёт OTA_WINDOW_MS и закрывается после успешного обновления.
// Так посторонний в сети WiFi не сможет прошить гирлянду, даже зная её адрес. Кроме окна запрос должен нести
// заголовок страницы (защита от CSRF), а образ - проходить проверки библиотеки Update (байт-маркер 0xE9, размер).

#define OTA_WINDOW_MS 300000UL   // 5 минут

// otaBusy (идёт запись, эффекты отключены) объявлена в главном файле: переменная нужна в loop(), а вкладки склеиваются после него
uint32_t otaRebootAt = 0;      // момент перезагрузки после успеха (0 - не назначена)
char otaDetail[48] = "";       // причина отказа библиотеки Update

void otaOpenWindow() {
  otaTmr.restart();
}

bool otaOpen() {
  return otaTmr.state();
}

void otaCloseWindow() {
  otaTmr.stop();
}

// сколько секунд осталось у окна
uint16_t otaLeftSec() {
  if (!otaTmr.state()) return 0;
  uint32_t used = millis() - otaTmr._tmr;
  return used >= OTA_WINDOW_MS ? 0 : (OTA_WINDOW_MS - used + 999) / 1000;
}

// вызывать из loop(): закрывает окно по времени и перезагружает плату после успешного обновления
void otaTick() {
  if (otaTmr.ready()) otaTmr.stop();
  if (otaRebootAt && (int32_t)(millis() - otaRebootAt) >= 0) {
    otaRebootAt = 0;
    ESP.restart();
  }
}

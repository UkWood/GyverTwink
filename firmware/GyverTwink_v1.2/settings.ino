// Изменение настроек. Общий код для UDP (parsing.ino) и веб-интерфейса (web.ino),
// чтобы оба способа управления вели себя одинаково.

// любое изменение настроек останавливает принудительный показ эффекта (как команды {2,...} по UDP)
void settingsTouch() {
  forceTmr.stop();
}

// выключено - гасим ленту; настройки пишем в EEPROM отложенно
void settingsChanged() {
  if (!cfg.power) ledsShow(0);
  EEcfg.update();
}

// настройки эффекта n изменены: запись нужного блока EEPROM (у новых эффектов он свой, см. effsX)
void effSave(byte n) {
  if (n < ACTIVE_PALETTES * 2) EEeff.update();
  else if (n >= FIRE2_EFF) EEfire2.update();
  else EEfx.update();
}

void setLedAmount(int n) {
  cfg.ledAm = constrain(n, 1, LED_MAX);
  strip->setLeds(leds, cfg.ledAm);
  settingsChanged();
}

// порядок цветов ленты: сразу виден на следующем кадре
void setColorOrder(byte o) {
  colorOrder.order = o;
  colorOrderApply();
  EEcolor.update();
}

void setPower(bool on) {
  cfg.power = on;
  settingsChanged();
}

void setBright(byte b) {
  cfg.bright = b;
  settingsChanged();
}

void setAutoSwitch(bool on) {
  cfg.autoCh = on;
  if (cfg.autoCh) switchTmr.restart();
  else switchTmr.stop();
  settingsChanged();
}

void setRandomSwitch(bool on) {
  cfg.rndCh = on;
  settingsChanged();
}

void setPeriod(byte minutes) {
  cfg.prdCh = minutes;
  switchTmr.setPrd(cfg.prdCh * 60000ul);
  if (cfg.autoCh) switchTmr.restart();
  settingsChanged();
}

void setOffTimer(bool on) {
  cfg.turnOff = on;
  if (cfg.turnOff) offTmr.restart();
  else offTmr.stop();
  settingsChanged();
}

void setOffMinutes(byte minutes) {
  cfg.offTmr = minutes;
  offTmr.setPrd(cfg.offTmr * 60000ul);
  if (cfg.turnOff) offTmr.restart();
  settingsChanged();
}

// лимит тока в мА, 0 - выключен
void setMaxMa(uint16_t ma) {
  pwr.maxMa = ma;
  EEpwr.update();
}

void nextEffect() {
  switchEff();
  if (cfg.autoCh) switchTmr.restart();
}

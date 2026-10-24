void button() {
  static bool dir = 1;

  btn.tick();
  if (btn.hasClicks(1)) {
    DEBUGLN("1x click - power");
    cfg.power = !cfg.power;
    if (!cfg.power) ledsShow(0);
    EEcfg.update();
  }
  
  if (btn.hasClicks(2)) {
    DEBUGLN("2x click - next");
    switchEff();
  }
  
  if (btn.hasClicks(3)) {
    DEBUGLN("3x click - OTA window");
    otaOpenWindow();       // окно обновления прошивки по WiFi (ota.ino)
    fadeBlink(CRGB::Cyan);   // голубая вспышка: окно открыто
  }

  if (btn.step()) {
    DEBUGLN("change bright");
    if (cfg.power) {
      // меняем безопасно
      int bri = cfg.bright;
      bri += dir ? 30 : -30;
      bri = constrain(bri, 0, 255);      
      cfg.bright = bri;
      if (bri == 255) {
        ledsShow(0);
        delay(100);
        ledsShow(limitedBright(bri));
      }
      EEcfg.update();
    }
  }
  
  if (btn.releaseStep()) {
    DEBUGLN("change dir");
    dir = !dir;
  }
}

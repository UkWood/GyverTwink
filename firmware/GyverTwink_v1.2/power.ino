// Ограничение тока ленты.
// FastLED считает полностью белый светодиод за 42 мА (210 мВт при 5 В), а реальные WS2812 берут до 60 мА,
// поэтому лимит пересчитывается ×0.7: значение pwr.maxMa близко к реальному току, который отдаёт блок питания.
// Своя функция нужна потому, что кадр выводится через strip->showLeds(), в обход FastLED.show() с его лимитом.

byte limitedBright(byte bri) {
  if (pwr.maxMa == 0) return bri;   // лимит выключен
  uint32_t ma = (uint32_t)pwr.maxMa * 7 / 10;
  return calculate_max_brightness_for_power_vmA(leds, cfg.ledAm, bri, POWER_VOLTS, ma);
}

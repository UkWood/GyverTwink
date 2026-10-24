// Порядок цветов ленты (настройка в веб-интерфейсе, блок EEPROM colorOrder).
// FastLED собран с порядком RGB (LED_ORDER в главном скетче), а байты в нужном ленте порядке переставляем сами перед выводом
// и возвращаем после: эффекты, палитры и калибровка работают с обычными R, G, B.
// Поправка цвета TypicalLEDStrip у FastLED привязана к позиции байта (она компенсирует яркость излучателей), поэтому
// переставляем и её: каждая позиция получает поправку того канала, который в ней лежит.

static byte colorOrderIndex() {
  return colorOrder.order < ORDER_COUNT ? colorOrder.order : ORDER_GRB;   // мусор в EEPROM - порядок по умолчанию
}

// применить порядок: поправка для FastLED (вызывать при старте и при смене порядка)
void colorOrderApply() {
  static const byte corr[3] = {0xFF, 0xB0, 0xF0};   // TypicalLEDStrip по каналам R, G, B
  const byte* p = ORDER_PERM[colorOrderIndex()];
  strip->setCorrection(CRGB(corr[p[0]], corr[p[1]], corr[p[2]]));
}

// единственное место, откуда кадр уходит на ленту (вместо прямого strip->showLeds)
void ledsShow(uint8_t bri) {
  byte o = colorOrderIndex();
  if (o == 0) {   // RGB: переставлять нечего
    strip->showLeds(bri);
    return;
  }
  const byte* p = ORDER_PERM[o];
  int n = strip->size();
  for (int i = 0; i < n; i++) {
    byte v[3] = {leds[i].r, leds[i].g, leds[i].b};
    leds[i].r = v[p[0]];
    leds[i].g = v[p[1]];
    leds[i].b = v[p[2]];
  }
  strip->showLeds(bri);
  for (int i = 0; i < n; i++) {   // вернуть: в позиции k лежал логический канал p[k]
    byte v[3] = {leds[i].r, leds[i].g, leds[i].b};
    byte w[3];
    for (byte k = 0; k < 3; k++) w[p[k]] = v[k];
    leds[i].r = w[0];
    leds[i].g = w[1];
    leds[i].b = w[2];
  }
}

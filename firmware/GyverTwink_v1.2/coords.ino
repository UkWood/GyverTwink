// Координаты светодиодов (xy[]) и их границы (mm): общий код калибровки по UDP и редактора в веб-интерфейсе.

// границы и размеры карты по текущим координатам первых cfg.ledAm светодиодов
void recalcBounds() {
  mm.minY = 255;
  mm.maxY = 0;
  mm.minX = 255;
  mm.maxX = 0;
  for (int i = 0; i < cfg.ledAm; i++) {
    mm.minX = min(mm.minX, xy[i][0]);
    mm.maxX = max(mm.maxX, xy[i][0]);
    mm.minY = min(mm.minY, xy[i][1]);
    mm.maxY = max(mm.maxY, xy[i][1]);
  }
  mm.w = mm.maxX - mm.minX;
  mm.h = mm.maxY - mm.minY;
}

// зажечь один светодиод (подсветка выбранной точки в редакторе). Эффекты на это время отключены (calibF).
void ledHighlight(int i) {
  calibF = true;
  strip->clearLedData();
  leds[i] = CRGB::White;
  strip->showLeds(200);
  hlTmr.restart();   // страница могла закрыться: через минуту подсветка гаснет сама
}

void ledHighlightOff() {
  hlTmr.stop();
  calibF = false;
  strip->clearLedData();
  strip->showLeds(0);
}

// записать координаты и границы в EEPROM
void coordsSave() {
  EExy.updateNow();
  if (cfg.ledAm > XY_BASE) EExyHi.updateNow();   // второй блок нужен, только если светодиодов больше XY_BASE
  EEmm.updateNow();
}

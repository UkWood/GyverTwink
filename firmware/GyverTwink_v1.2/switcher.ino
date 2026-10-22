void switchEff() {
  // поиск favorite эффектов
  // если избранных нет, остаёмся на текущем (иначе цикл не завершится)
  bool anyFav = false;
  for (byte i = 0; i < EFFECT_COUNT; i++) {
    if (effAt(i).fav) {
      anyFav = true;
      break;
    }
  }
  if (!anyFav) return;

  while (true) {
    if (cfg.rndCh) curEff = random(0, EFFECT_COUNT);
    else {
      if (++curEff >= EFFECT_COUNT) curEff = 0;
    }
    if (effAt(curEff).fav) break;
  }
  DEBUG("switch to: ");
  DEBUGLN(curEff);
}

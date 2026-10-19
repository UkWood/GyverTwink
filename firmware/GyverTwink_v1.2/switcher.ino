void switchEff() {
  // поиск favorite эффектов
  // если избранных нет, остаёмся на текущем (иначе цикл не завершится)
  bool anyFav = false;
  for (byte i = 0; i < ACTIVE_PALETTES * 2; i++) {
    if (effs[i].fav) {
      anyFav = true;
      break;
    }
  }
  if (!anyFav) return;

  while (true) {
    if (cfg.rndCh) curEff = random(0, ACTIVE_PALETTES * 2);
    else {
      if (++curEff >= ACTIVE_PALETTES * 2) curEff = 0;
    }
    if (effs[curEff].fav) break;
  }
  DEBUG("switch to: ");
  DEBUGLN(curEff);
}

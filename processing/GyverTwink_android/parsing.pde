void receive(byte[] ubuf) {
  if (ubuf[0] != 'G' || ubuf[1] != 'T') return;

  // расширение протокола: ответ на {5} и подтверждения команд {6, cmd, seq, status}
  if (ubuf.length >= 4 && (ubuf[2] & 0xFF) == 5) {
    ackEnable();
    return;
  }
  if (ubuf.length >= 5 && (ubuf[2] & 0xFF) == 7) {   // лимит тока {7, mA/100, mA%100}
    maLimit.text = str((ubuf[3] & 0xFF) * 100 + (ubuf[4] & 0xFF));
    limitKnown = true;
    return;
  }
  if (ubuf.length >= 6 && (ubuf[2] & 0xFF) == 6) {
    ackReceived(ubuf[4] & 0xFF, ubuf[5] & 0xFF);
    return;
  }

  int[] data = new int[10];
  for (int i = 0; i < ubuf.length - 2; i++) {
    data[i] = int(ubuf[i+2]);
    //println(data[i]);
  }

  if (parseMode != data[0]) return;

  switch (data[0]) {
  case 0: // Поиск
    String ip = brIP.substring(0, brIP.lastIndexOf('.')+1) + str(data[1]);
    if (!ips.hasValue(ip)) ips.append(ip);
    break;

  case 1: // Настройки 
    searchF = false;
    leds.text = str(data[1] * 100 + data[2]);
    power.value = boolean(data[3]);
    bri.value = data[4];
    auto.value = boolean(data[5]);
    rnd.value = boolean(data[6]);
    prd.value = data[7];
    offT.value = boolean(data[8]);
    offS.value = data[9];
    break;

  case 4: // Эффект
    fav.value = boolean(data[1]);
    scl.value = data[2];
    spd.value = data[3];
    break;
  }
}

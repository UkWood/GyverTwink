void startSearch() {
  int[] ipv4 = int(split(KetaiNet.getIP(), '.'));
  int[] mask = int(split(subnet.text, '.'));
  found = false;
  curIP = "";
  brIP = "";
  for (int i = 0; i < 4; i++) {
    brIP += ipv4[i] | (mask[i] ^ 0xFF);
    if (i != 3) brIP += '.';
  }

  searchF = true;
  parseMode = 0;

  // выводим однократно
  ips.clear();
  ips.append("searching...");
  dropIP.selected = 0;
  ui();
  ips.clear();

  curIP = brIP;
  sendData(new int[] {0});
  actionTmr = millis();
}

void requestCfg() {
  parseMode = 1;
  // узнаём, поддерживает ли прошивка ACK (ответ {5} включит ackMode)
  ackReset();
  sendRaw(new int[] {5});
  delay(60);   // прошивка отсеивает пакеты чаще 50 мс
  sendRaw(new int[] {1});
}

void sendData(int[] data) {
  // {0} - широковещательный поиск, {5} - запрос возможностей: всегда без ACK
  if (ackMode && data[0] != 0 && data[0] != 5) ackSend(data);
  else sendRaw(data);
}

void sendRaw(int[] data) {
  int[] buf = {'G', 'T'};
  buf = concat(buf, data);
  sendData(byte(buf));
}

void sendData(byte[] data) {  
  if (curIP.charAt(0) != 'n') {
    udp.send(data, curIP, port);
    delay(15);
    udp.send(data, curIP, port);
  }
}

// одиночная отправка (повторами управляет ACK)
void sendBytesOnce(byte[] data) {
  if (curIP.charAt(0) != 'n') udp.send(data, curIP, port);
}

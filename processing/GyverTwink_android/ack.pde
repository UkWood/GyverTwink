// Подтверждение команд (ACK) - необязательное расширение протокола, см. docs/UDP_protocol.txt
// Если прошивка отвечает на {5}, команды отправляются с флагом 0x80 и номером seq,
// по одной за раз: следующая уходит после ACK предыдущей. Нет ACK - повтор с тем же seq.

boolean ackMode = false;     // прошивка поддерживает ACK
int ackSeq = 0;
int ACK_TIMEOUT = 200;       // мс до повтора
int ACK_TRIES = 5;           // максимум отправок одной команды

class PendingCmd {
  byte[] pkt;
  int seq, key;
  int tries = 0;
  int sentAt = 0;
}
// [0] - команда "в полёте", остальные ждут своей очереди
ArrayList<PendingCmd> ackQueue = new ArrayList<PendingCmd>();

// сбросить режим ACK (смена устройства)
void ackReset() {
  synchronized (ackQueue) {
    ackMode = false;
    limitKnown = false;
    ackQueue.clear();
  }
}

// прошивка ответила на {5}
void ackEnable() {
  boolean first;
  synchronized (ackQueue) {
    first = !ackMode;
    ackMode = true;
    if (first) ackSeq = int(random(256));
  }
  // запрос настроек с ACK задаёт прошивке известный seq (запросы выполняются всегда)
  if (first) {
    sendData(new int[] {1});
    sendData(new int[] {7});   // лимит тока (ответ покажет поле в настройках)
  }
}

// ключ слияния: новое значение параметра заменяет ещё не отправленное старое (ползунки)
int ackKey(int[] d) {
  if (d.length < 2) return -1;
  if (d[0] == 2 && d[1] != 6) return d[0] * 256 + d[1];
  if (d[0] == 4 && d[1] >= 1 && d[1] <= 3) return d[0] * 256 + d[1];
  return -1;
}

void ackSend(int[] d) {
  PendingCmd c = new PendingCmd();
  int[] buf = {'G', 'T', d[0] | 0x80, 0};
  buf = concat(buf, subset(d, 1));
  synchronized (ackQueue) {
    ackSeq = (ackSeq + 1) & 0xFF;
    buf[3] = ackSeq;
    c.pkt = byte(buf);
    c.seq = ackSeq;
    c.key = ackKey(d);
    boolean merged = false;
    if (c.key >= 0) {
      for (int i = 1; i < ackQueue.size(); i++) {
        if (ackQueue.get(i).key == c.key) {
          ackQueue.set(i, c);
          merged = true;
          break;
        }
      }
    }
    if (!merged) ackQueue.add(c);
  }
  ackTick();
}

// пришёл {6, cmd, seq, status}
void ackReceived(int seq, int status) {
  synchronized (ackQueue) {
    if (ackQueue.size() == 0 || ackQueue.get(0).seq != seq) return;   // чужой или запоздавший ACK
    ackQueue.remove(0);
  }
  if (status != 0) println("ACK: command rejected, status " + status);
  ackTick();
}

// отправка и повторы, вызывается из draw() и при изменении очереди
void ackTick() {
  synchronized (ackQueue) {
    while (ackQueue.size() > 0) {
      PendingCmd c = ackQueue.get(0);
      if (c.tries > 0 && millis() - c.sentAt < ACK_TIMEOUT) return;
      if (c.tries >= ACK_TRIES) {
        println("ACK: no answer, command dropped");
        ackQueue.remove(0);
        continue;
      }
      sendBytesOnce(c.pkt);
      c.tries++;
      c.sentAt = millis();
      return;
    }
  }
}

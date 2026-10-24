#define MAX_UDP_PACKET 30
byte ubuf[MAX_UDP_PACKET + 1];  // +1 под завершающий ноль

// Расширение протокола (необязательное, старые клиенты его не используют):
// если в старшем бите команды (ubuf[2]) стоит ACK_FLAG, то следом идёт байт seq,
// остальные аргументы сдвинуты на 1. После выполнения прошивка отвечает {6, cmd, seq, status}.
// Запрос {5} возвращает {5, PROTO_EXT_VER} - так клиент узнаёт, что ACK поддерживается.
#define ACK_FLAG 0x80
#define PROTO_EXT_VER 1

#define PKT_OK 0        // команда выполнена
#define PKT_INVALID 1   // недопустимый аргумент
#define PKT_UNKNOWN 2   // неизвестная команда
#define PKT_DENIED 3    // запрещено: включён токен и команды записи по UDP закрыты (guard.ino)
byte pktStatus = PKT_OK;

void reply(byte* data, byte size) {
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write("GT");
  udp.write(data, size);
  udp.endPacket();
}

void sendAck(byte cmd, byte seq, byte status) {
  byte answ[4] = {6, cmd, seq, status};
  reply(answ, 4);
}

// запросы можно безопасно выполнять повторно (повтор нужен, чтобы заново получить ответ)
bool isQuery() {
  return ubuf[2] == 0 || ubuf[2] == 1 || ubuf[2] == 7 || (ubuf[2] == 4 && ubuf[3] == 0);
}

void handlePacket();

void parsing() {
  static uint32_t tmr;
  static byte lastSeq, lastStatus;
  static bool haveSeq = false;

  if (!udp.parsePacket()) return;
  int n = udp.read(ubuf, MAX_UDP_PACKET);
  if (n < 3 || ubuf[0] != 'G' || ubuf[1] != 'T') return;

  bool ackReq = ubuf[2] & ACK_FLAG;
  byte seq = 0;
  if (ackReq) {
    // клиент с поддержкой ACK: повторы отсеиваются по seq, а не по времени
    if (n < 4) return;
    seq = ubuf[3];
    ubuf[2] &= ~ACK_FLAG;
    memmove(ubuf + 3, ubuf + 4, n - 4);
    n--;
  } else {
    // старый клиент: "антидребезг" приёма (приложение отправляет каждый пакет дважды)
    if (millis() - tmr < 50) return;
    tmr = millis();
  }
  ubuf[n] = 0;
  /*for (int i = 2; i < n; i++) {
    DEBUG(ubuf[i]);
    DEBUG(',');
    }
    DEBUGLN();*/

  // повтор уже выполненной команды: не выполняем снова, только повторяем ACK
  if (ackReq && haveSeq && seq == lastSeq && !isQuery()) {
    sendAck(ubuf[2], seq, lastStatus);
    return;
  }

  pktStatus = PKT_OK;
  byte cmd = ubuf[2];
  handlePacket();

  if (ackReq) {
    lastSeq = seq;
    lastStatus = pktStatus;
    haveSeq = true;
    sendAck(cmd, seq, pktStatus);
  }
}

void handlePacket() {
  byte answ[10];

  if (!guardUdpAllowed(ubuf[2])) {   // запись по UDP закрыта владельцем (запросы 0, 1, 5, 7 доступны всегда)
    pktStatus = PKT_DENIED;
    return;
  }

  switch (ubuf[2]) {
    case 0:   // запрос IP
      delay(myIP[3] * 2);
      answ[0] = 0;
      answ[1] = myIP[3];
      reply(answ, 2);
      break;

    case 1:   // запрос настроек
      answ[0] = 1;
      answ[1] = cfg.ledAm / 100;
      answ[2] = cfg.ledAm % 100;
      answ[3] = cfg.power;
      answ[4] = cfg.bright;
      answ[5] = cfg.autoCh;
      answ[6] = cfg.rndCh;
      answ[7] = cfg.prdCh;
      answ[8] = cfg.turnOff;
      answ[9] = cfg.offTmr;
      reply(answ, 10);
      break;

    case 2:   // приём настроек
      settingsTouch();
      switch (ubuf[3]) {
        case 0: setLedAmount(ubuf[4] * 100 + ubuf[5]);
          break;
        case 1: setPower(ubuf[4]);
          break;
        case 2: setBright(ubuf[4]);
          break;
        case 3: setAutoSwitch(ubuf[4]);
          break;
        case 4: setRandomSwitch(ubuf[4]);
          break;
        case 5: setPeriod(ubuf[4]);
          break;
        case 6: nextEffect();   // следующий эффект
          break;
        case 7: setOffTimer(ubuf[4]);
          break;
        case 8: setOffMinutes(ubuf[4]);
          break;
        case 9: setMaxMa(ubuf[4] * 100 + ubuf[5]);   // лимит тока в мА (расширение), 0 - выключен
          break;
      }
      break;

    case 3:
      switch (ubuf[3]) {
        case 0:   // запуск калибровки
          DEBUGLN("Calibration start");
          calibF = true;
          strip->clearLedData();
          strip->showLeds(0);
          break;

        case 1:   // следующий лед
          {
            int curLed = ubuf[4] * 100 + ubuf[5];
            if (curLed > LED_MAX) {   // за пределами массивов
              pktStatus = PKT_INVALID;
              break;
            }
            if (curLed > 0) {
              xy[curLed - 1][0] = ubuf[6];
              xy[curLed - 1][1] = ubuf[7];
            }
            strip->clearLedData();
            if (curLed < LED_MAX) leds[curLed] = CRGB::White;   // curLed == LED_MAX - только сохранить координаты последнего
            strip->showLeds(200);
          }
          break;

        case 2:   // калибровка окончена
          DEBUGLN("Finished");
          calibF = false;
          strip->clearLedData();
          strip->showLeds(0);
          recalcBounds();
          coordsSave();
          break;
      }
      break;

    case 4:   // управление эффектами
      forceTmr.restart();
      effSave(forceEff);
      switch (ubuf[3]) {
        case 0:   // выбор эффекта в дропе (номера от ACTIVE_PALETTES * 2 - новые эффекты, расширение)
          if (ubuf[4] >= EFFECT_COUNT) {   // нет такого эффекта
            pktStatus = PKT_INVALID;
            break;
          }
          forceEff = ubuf[4];
          answ[0] = 4;
          answ[1] = effAt(forceEff).fav;
          answ[2] = effAt(forceEff).scale;
          answ[3] = effAt(forceEff).speed;
          reply(answ, 4);
          break;
        case 1:   // флажок избранное
          effAt(forceEff).fav = ubuf[4];
          break;
        case 2:   // масштаб
          effAt(forceEff).scale = ubuf[4];
          break;
        case 3:   // скорость
          effAt(forceEff).speed = ubuf[4];
          break;
      }
      break;

    case 7:   // запрос лимита тока (расширение)
      answ[0] = 7;
      answ[1] = pwr.maxMa / 100;
      answ[2] = pwr.maxMa % 100;
      reply(answ, 3);
      break;

    case 5:   // запрос возможностей протокола (расширения)
      answ[0] = 5;
      answ[1] = PROTO_EXT_VER;
      reply(answ, 2);
      break;

    default:
      pktStatus = PKT_UNKNOWN;
      break;
  }
}

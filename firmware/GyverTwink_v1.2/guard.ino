// Защита доступа токеном (необязательная): без токена гирлянда управляется всеми в сети, как раньше.
//
// Когда токен задан, веб-API (кроме страницы и GET /api/auth) требует заголовок X-GT-Token.
// Задать, сменить или снять токен можно только пока открыто «окно обслуживания» (три нажатия кнопки на гирлянде,
// то же окно, что для обновления прошивки): так никто из сети не заблокирует владельца и не отберёт управление,
// а забытый токен сбрасывается физически.
// Подбор ограничен: после GUARD_TRIES неверных токенов с одного адреса он блокируется на GUARD_LOCK_MS.
// UDP-протокол токена не знает (приложение его отправить не может, а менять протокол можно только вместе с приложением):
// по умолчанию UDP остаётся открытым, владелец может закрыть команды записи (guard.udpOpen = 0).
// Токен передаётся по обычному HTTP без шифрования: защита от случайных гостей сети, а не от того, кто слушает трафик.

#define GUARD_TRIES 5
#define GUARD_LOCK_MS 60000UL
#define GUARD_QUIET_MS 600000UL   // столько тишины - и счётчик неверных попыток обнуляется
#define GUARD_SLOTS 4             // сколько адресов запоминаем

static uint32_t guardIp[GUARD_SLOTS];
static uint32_t guardLast[GUARD_SLOTS];    // время последней неверной попытки
static uint32_t guardUntil[GUARD_SLOTS];   // конец блокировки
static byte guardCnt[GUARD_SLOTS];         // неверных попыток подряд
static bool guardLocked[GUARD_SLOTS];

bool guardOn() {
  return guard.token[0] != '\0';
}

// допустимый токен: TOKEN_MIN..TOKEN_MAX символов из латинских букв, цифр и . _ ~ - (безопасны для заголовка)
bool guardTokenValid(const char* s) {
  size_t n = strlen(s);
  if (n < TOKEN_MIN || n > TOKEN_MAX) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '~' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// сравнение токена за время, не зависящее от того, где первое несовпадение
bool guardCheck(const char* given) {
  char buf[TOKEN_MAX + 1];
  memset(buf, 0, sizeof(buf));
  size_t n = strlen(given);
  memcpy(buf, given, n < TOKEN_MAX ? n : TOKEN_MAX);
  byte diff = n > TOKEN_MAX;   // слишком длинный не подходит, но сравнение всё равно проходит целиком
  for (int i = 0; i <= TOKEN_MAX; i++) diff |= (byte)(buf[i] ^ guard.token[i]);
  return diff == 0;
}

static int guardSlot(uint32_t ip, bool create) {
  for (int i = 0; i < GUARD_SLOTS; i++) {
    if (guardIp[i] == ip && (guardCnt[i] || guardLocked[i])) return i;
  }
  if (!create) return -1;
  // свободное место; иначе самое давнее среди незаблокированных; блокировку вытесняем, только если заблокированы все:
  // так перебор с множества адресов не снимает блокировку с уже пойманного
  int best = -1;
  for (int i = 0; i < GUARD_SLOTS; i++) {
    if (!guardCnt[i] && !guardLocked[i]) {
      best = i;
      break;
    }
  }
  for (int pass = 0; pass < 2 && best < 0; pass++) {
    for (int i = 0; i < GUARD_SLOTS; i++) {
      if (guardLocked[i] != (pass == 1)) continue;
      if (best < 0 || (int32_t)(guardLast[best] - guardLast[i]) > 0) best = i;
    }
  }
  guardIp[best] = ip;
  guardCnt[best] = 0;
  guardLocked[best] = false;
  return best;
}

// адрес заблокирован за подбор
bool guardBlocked(uint32_t ip) {
  int i = guardSlot(ip, false);
  if (i < 0 || !guardLocked[i]) return false;
  if ((int32_t)(millis() - guardUntil[i]) >= 0) {   // срок вышел
    guardLocked[i] = false;
    guardCnt[i] = 0;
    return false;
  }
  return true;
}

void guardBad(uint32_t ip) {
  int i = guardSlot(ip, true);
  if (guardCnt[i] && millis() - guardLast[i] > GUARD_QUIET_MS) guardCnt[i] = 0;
  guardLast[i] = millis();
  if (++guardCnt[i] >= GUARD_TRIES) {
    guardLocked[i] = true;
    guardUntil[i] = millis() + GUARD_LOCK_MS;
  }
}

void guardGood(uint32_t ip) {
  int i = guardSlot(ip, false);
  if (i >= 0) guardCnt[i] = 0;
}

// проверка токена из запроса: 0 - можно, 1 - нет токена или неверный, 2 - адрес заблокирован.
// Запрос без токена подбором не считается (так страница при первом открытии узнаёт, что нужен вход).
byte guardVerify(uint32_t ip, const char* given) {
  if (!guardOn()) return 0;
  if (guardBlocked(ip)) return 2;
  if (given[0] == '\0') return 1;
  if (guardCheck(given)) {
    guardGood(ip);
    return 0;
  }
  guardBad(ip);
  return guardBlocked(ip) ? 2 : 1;
}

// UDP: можно ли выполнить команду записи (2 - настройки, 3 - калибровка, 4 - эффекты). Запросы 0, 1, 5, 7 доступны всегда.
bool guardUdpAllowed(byte cmd) {
  return !guardOn() || guard.udpOpen || !(cmd == 2 || cmd == 3 || cmd == 4);
}

// записать токен (пустой - снять защиту). Допустимость токена проверяет вызывающий.
void guardSet(const char* token) {
  memset(guard.token, 0, sizeof(guard.token));
  strncpy(guard.token, token, TOKEN_MAX);
  EEguard.update();
  memset(guardCnt, 0, sizeof(guardCnt));
  memset(guardLocked, 0, sizeof(guardLocked));
}

import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const app = require('../app.js');

test('время суток: минуты <-> ЧЧ:ММ, неверный ввод даёт -1', () => {
  assert.equal(app.minToTime(0), '00:00');
  assert.equal(app.minToTime(1080), '18:00');
  assert.equal(app.minToTime(1439), '23:59');
  assert.equal(app.timeToMin('18:00'), 1080);
  assert.equal(app.timeToMin('0:05'), 5);
  assert.equal(app.timeToMin('23:59'), 1439);
  for (const bad of ['', '24:00', '12:60', '12', 'ab:cd', '12:5', null, undefined, '-1:00']) assert.equal(app.timeToMin(bad), -1, String(bad));
  for (let m = 0; m < 1440; m++) assert.equal(app.timeToMin(app.minToTime(m)), m);
});

test('часовой пояс: подпись', () => {
  assert.equal(app.tzLabel(0), 'UTC+0');
  assert.equal(app.tzLabel(180), 'UTC+3');
  assert.equal(app.tzLabel(330), 'UTC+5:30');
  assert.equal(app.tzLabel(-300), 'UTC−5');
  assert.equal(app.tzLabel(-210), 'UTC−3:30');
  assert.equal(app.tzLabel(840), 'UTC+14');
});

test('дни недели: маска <-> флаги (бит 0 - понедельник)', () => {
  assert.deepEqual(app.daysFromMask(127), Array(7).fill(true));
  assert.deepEqual(app.daysFromMask(0), Array(7).fill(false));
  assert.deepEqual(app.daysFromMask(31), [true, true, true, true, true, false, false]);
  for (let m = 0; m < 128; m++) assert.equal(app.maskFromDays(app.daysFromMask(m)), m);
  assert.equal(app.maskFromDays([false, false, false, false, true, false, false]), 16);
});

test('часы гирлянды: местное время из UTC и пояса, в том числе через полночь', () => {
  const noon = Date.UTC(2026, 9, 22, 12, 0, 0) / 1000;
  assert.equal(app.clockText(noon, 0), '12:00:00');
  assert.equal(app.clockText(noon, 180), '15:00:00');
  assert.equal(app.clockText(noon, -330), '06:30:00');
  assert.equal(app.clockText(noon + 59, 0), '12:00:59');
  const late = Date.UTC(2026, 9, 22, 22, 30, 0) / 1000;
  assert.equal(app.clockText(late, 180), '01:30:00');    // уже следующие сутки
  assert.equal(app.clockText(late, -1380), '23:30:00');   // пояс сильно отрицательный: вчера
});

test('градусы -> сотые доли: границы, запятая, мусор', () => {
  assert.equal(app.degToCenti('55.75', 90), 5575);
  assert.equal(app.degToCenti('55,75', 90), 5575);
  assert.equal(app.degToCenti('-33.87', 90), -3387);
  assert.equal(app.degToCenti('90', 90), 9000);
  assert.equal(app.degToCenti('-180', 180), -18000);
  assert.equal(app.degToCenti(0.1 + 0.2, 90), 30);   // без хвоста двоичной дроби
  for (const bad of ['', 'abc', '90.01', '-90.01', null, NaN, Infinity]) assert.equal(app.degToCenti(bad, 90), null, String(bad));
});

test('подпись на сегодня: выключено, нет времени, есть события, нет события в этот день', () => {
  assert.equal(app.todayText({ en: 0 }), 'Расписание выключено.');
  assert.match(app.todayText({ en: 1, synced: 0 }), /не работает/);
  assert.equal(app.todayText({ en: 1, synced: 1, onAt: 1080, offAt: 1380 }), 'Сегодня: включится в 18:00, выключится в 23:00.');
  assert.match(app.todayText({ en: 1, synced: 1, onAt: -1, offAt: 1380 }), /включится: сегодня нет/);
});

test('токен: проверка допустимых символов и длины (как в прошивке)', () => {
  for (const ok of ['abcdefgh', 'Secret-Token_01', 'a.b~c-d_e1', 'x'.repeat(32)]) assert.equal(app.tokenValid(ok), true, ok);
  for (const bad of ['', 'abcdefg', 'x'.repeat(33), 'abc defgh', 'абвгдежз', 'abcdefg!', 'abc"defgh', 'abcdefgh\n', 'abc:defgh', 'abc/defgh']) assert.equal(app.tokenValid(bad), false, JSON.stringify(bad));
});

test('генерация токена: 16 понятных символов, допустим для прошивки, не повторяется', () => {
  const seen = new Set();
  for (let i = 0; i < 300; i++) {
    const t = app.genToken();
    assert.match(t, /^[A-HJ-NP-Za-km-z2-9]{16}$/);
    assert.equal(app.tokenValid(t), true);
    seen.add(t);
  }
  assert.equal(seen.size, 300);
});

test('генерация токена: хвост диапазона отбрасывается, символы равновероятны', () => {
  // 256 % 56 = 32: байты 224..255 отбрасываются, остальные дают символы алфавита по порядку
  const calls = [];
  const rand = (a) => {
    calls.push(a.length);
    if (calls.length === 1) a.fill(255);                      // всё отброшено: нужен ещё вызов
    else for (let i = 0; i < a.length; i++) a[i] = i;         // 0..31 -> первые 32 символа алфавита
    return a;
  };
  const t = app.genToken(rand);
  assert.equal(t.length, 16);
  assert.equal(t, 'ABCDEFGHJKLMNPQR');
  assert.ok(calls.length >= 2, 'отброшенные байты не используются');
  // частоты на большой выборке: все 56 символов встречаются, перекоса нет
  const freq = {};
  for (let i = 0; i < 5000; i++) for (const ch of app.genToken()) freq[ch] = (freq[ch] || 0) + 1;
  const counts = Object.values(freq);
  assert.equal(counts.length, 56);
  const mean = (5000 * 16) / 56;
  for (const c of counts) assert.ok(c > mean * 0.85 && c < mean * 1.15, 'равномерность: ' + c + ' при среднем ' + Math.round(mean));
});

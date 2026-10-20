// e2e-тест веб-интерфейса: настоящий код прошивки (gt_host) + страница из web_page.h в настоящем Chromium.
// Запуск: node tests/web/ui_test.mjs ./tests/gt_host   (из корня репозитория)
import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import net from 'node:net';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
function loadPlaywright() {
  const tries = ['playwright', '/opt/node22/lib/node_modules/playwright', '/opt/node-tools/node_modules/playwright'];
  for (const t of tries) {
    try { return require(t); } catch (e) { /* пробуем дальше */ }
  }
  throw new Error('playwright не найден: npm i playwright (см. tests/web/package.json)');
}
const { chromium } = loadPlaywright();

let failed = 0;
function check(cond, msg) {
  console.log((cond ? '  ok:   ' : '  FAIL: ') + msg);
  if (!cond) failed++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(fn, ms = 4000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    try { if (await fn()) return true; } catch (e) { /* ещё нет */ }
    await sleep(40);
  }
  return false;
}
function freePort(type = 'tcp') {
  return new Promise((resolve) => {
    if (type === 'udp') {
      const s = dgram.createSocket('udp4');
      s.bind(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); });
    } else {
      const s = net.createServer();
      s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => resolve(p)); });
    }
  });
}

// ---------- хост-сервер ----------
const exe = process.argv[2] || './tests/gt_host';
const udpPort = await freePort('udp');
const webPort = await freePort('tcp');
const fsDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gt-fs-'));   // "флеш" гирлянды: переживает перезапуск сервера
let server = null;
let state = {};

async function startServer() {
  state = {};
  let buf = '';
  server = spawn(exe, [String(udpPort)], { env: { ...process.env, GT_WEB_PORT: String(webPort), GT_FS_DIR: fsDir } });
  const ready = new Promise((resolve) => {
    server.stdout.on('data', (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i).trim();
        buf = buf.slice(i + 1);
        if (line === 'READY') resolve();
        if (line.startsWith('STATE')) {
          state = Object.fromEntries(line.split(' ').slice(1).map((kv) => { const [k, v] = kv.split('='); return [k, Number(v)]; }));
        }
      }
    });
  });
  server.stderr.on('data', (d) => process.stderr.write(d));
  await ready;
}
async function stopServer() {
  const done = new Promise((resolve) => server.once('exit', resolve));
  server.kill();
  await done;
}
await startServer();
const base = `http://127.0.0.1:${webPort}`;

// отправка UDP-команды старого формата (для проверки, что страница видит изменения, сделанные другим способом)
function udpSend(bytes) {
  return new Promise((resolve) => {
    const s = dgram.createSocket('udp4');
    s.send(Buffer.from([0x47, 0x54, ...bytes]), udpPort, '127.0.0.1', () => { s.close(); resolve(); });
  });
}

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
const page = await ctx.newPage();
const jsErrors = [];
page.on('pageerror', (e) => jsErrors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') jsErrors.push(m.text()); });

async function setRange(sel, value) {
  await page.$eval(sel, (el, v) => { el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); el.dispatchEvent(new Event('change', { bubbles: true })); }, String(value));
}
async function setNumber(sel, value) {
  await page.fill(sel, String(value));
  await page.dispatchEvent(sel, 'change');
}
const stateIs = (k, v) => until(() => state[k] === v);
// страница не должна выходить за ширину экрана ни на одной вкладке (узкий телефон)
const fitsScreen = () => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth);

try {
  console.log('страница и связь');
  const resp = await fetch(base + '/');
  check(resp.status === 200 && resp.headers.get('content-encoding') === 'gzip', 'GET / отдаёт gzip-страницу');
  await page.goto(base + '/');
  check((await page.title()) === 'GyverTwink', 'заголовок страницы');
  check(await until(async () => (await page.textContent('#conn')) === 'на связи'), 'индикатор: на связи');
  check((await page.textContent('#foot')).includes('Прошивка host'), 'версия прошивки показана в подвале');
  check((await page.$eval('#bright', (e) => e.value)) === '100', 'яркость из состояния устройства (100)');

  console.log('вкладка «Управление»');
  await page.click('#power');
  check(await stateIs('power', 0), 'кнопка питания выключает ленту');
  check(await until(async () => (await page.textContent('#power')) === 'Выключено'), 'надпись кнопки: Выключено');
  await page.click('#power');
  check(await stateIs('power', 1), 'кнопка питания включает ленту');

  await setRange('#bright', 180);
  check(await stateIs('bright', 180), 'ползунок яркости -> прошивка (180)');
  check((await page.textContent('#bright-val')) === '180', 'значение яркости рядом с ползунком');

  await page.check('#auto');
  check(await stateIs('auto', 1), 'автосмена включена');
  await page.check('#rnd');
  check(await stateIs('rnd', 1), 'случайный порядок включён');
  await setRange('#period', 5);
  check(await stateIs('period', 5), 'период смены 5');
  await page.check('#offOn');
  check(await stateIs('offOn', 1), 'таймер выключения включён');
  await setNumber('#offMin', 42);
  check(await stateIs('offMin', 42), 'минут до выключения 42');
  await setNumber('#ledAm', 120);
  check(await stateIs('ledAm', 120), 'количество светодиодов 120');
  await setNumber('#maxMa', 2500);
  check(await stateIs('maxMa', 2500), 'лимит тока 2500 мА');

  const before = state.curEff;
  await page.click('#next');
  check(await until(() => state.curEff !== before), 'кнопка «Следующий» переключает эффект');
  check(await until(async () => (await page.textContent('#cur')).startsWith(state.curEff + '.')), 'название текущего эффекта обновилось');

  console.log('ошибки');
  await setNumber('#ledAm', 9999);
  check(await until(async () => (await page.textContent('#toast')).includes('количество светодиодов')), 'неверное значение: понятное сообщение об ошибке');
  check(await until(async () => (await page.$eval('#ledAm', (e) => e.value)) === '120'), 'поле вернулось к значению устройства');
  check(state.ledAm === 120, 'прошивка не изменилась');
  jsErrors.length = 0;   // браузер записал ожидаемые ответы 400 в консоль

  console.log('изменения другим способом (UDP) видны на странице');
  await udpSend([2, 2, 33]);   // яркость 33
  check(await until(async () => (await page.$eval('#bright', (e) => e.value)) === '33', 5000), 'страница подхватила яркость, заданную по UDP');

  console.log('вкладка «Эффекты»');
  await page.click('[data-tab=effects]');
  check(await page.isVisible('#effList'), 'вкладка открылась');
  check((await page.$$('#effList button')).length === 22, '22 эффекта в списке');
  check(await page.isHidden('#effEditor'), 'редактор скрыт, пока эффект не выбран');
  await page.click('#eff-7');
  check(await stateIs('force', 1), 'выбор эффекта запускает его показ');
  check(state.forceEff === 7, 'показывается эффект 7');
  check(await until(async () => (await page.textContent('#effName')).startsWith('7.')), 'в редакторе название эффекта');
  await page.uncheck('#effFav');
  check(await stateIs('fav', 0), 'флаг «избранное» снят');
  await setRange('#effScale', 77);
  check(await stateIs('scale', 77), 'масштаб эффекта 77');
  await setRange('#effSpeed', 200);
  check(await stateIs('speed', 200), 'скорость эффекта 200');
  await page.click('#eff-3');
  check(await until(() => state.forceEff === 3), 'переключение на другой эффект');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '50'), 'у эффекта 3 свои значения (масштаб 50)');
  await page.click('#eff-7');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '77'), 'настройки эффекта 7 сохранились');


  console.log('вкладка «Сцены»');
  await page.click('[data-tab=control]');
  await setRange('#bright', 140);
  check(await stateIs('bright', 140), 'подготовка: яркость 140');
  await setRange('#period', 4);
  await page.check('#auto');
  await page.click('[data-tab=scenes]');
  check(await page.isVisible('#sceneEmpty'), 'пока нет сцен: подсказка видна');
  await page.click('#sceneSave');
  check(await until(async () => (await page.textContent('#toast')).includes('Введите название')), 'пустое название: сообщение');
  await page.fill('#sceneName', 'ДлинноеНазваниеСцены');
  check((await page.$eval('#sceneName', (e) => e.value)) === 'ДлинноеН', 'название обрезается до 16 байт (8 русских букв) уже в поле ввода');
  await page.fill('#sceneName', 'Вечер');
  await page.click('#sceneSave');
  check(await until(async () => (await page.$$('#sceneList li')).length === 1), 'сцена сохранена и показана в списке');
  check((await page.textContent('#sceneList .name')) === 'Вечер', 'название сцены');
  check(await page.isHidden('#sceneEmpty'), 'подсказка скрыта');
  check((await page.$eval('#sceneName', (e) => e.value)) === '', 'поле названия очищено');

  await page.click('[data-tab=control]');
  await setRange('#bright', 20);
  check(await stateIs('bright', 20), 'яркость изменена на 20');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList [data-act=load]');
  check(await stateIs('bright', 140), 'применение сцены вернуло яркость 140');
  check(await stateIs('period', 4), 'и период смены 4');
  await page.click('[data-tab=control]');
  check(await until(async () => (await page.$eval('#bright', (e) => e.value)) === '140'), 'вкладка «Управление» показывает значения сцены');

  await setRange('#bright', 60);
  check(await stateIs('bright', 60), 'яркость 60');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList [data-act=overwrite]');
  check(await until(async () => (await page.textContent('#toast')).includes('Сохранено')), 'перезапись сцены подтверждена');
  check((await page.$$('#sceneList li')).length === 1, 'перезапись не создаёт вторую сцену');

  console.log('сцены переживают перезагрузку гирлянды');
  await stopServer();
  await startServer();
  await page.reload();
  await page.click('[data-tab=scenes]');
  check(await until(async () => (await page.$$('#sceneList li')).length === 1), 'после перезапуска сцена на месте');
  check((await page.textContent('#sceneList .name')) === 'Вечер', 'и с тем же названием');
  await page.click('[data-tab=control]');
  await setRange('#bright', 5);
  check(await stateIs('bright', 5), 'яркость 5');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList [data-act=load]');
  check(await stateIs('bright', 60), 'сцена после перезапуска применяется (яркость 60 - из перезаписи)');

  console.log('заполнение всех мест и удаление');
  for (let i = 2; i <= 8; i++) {
    await page.fill('#sceneName', 'Сцена ' + i);
    await page.click('#sceneSave');
    await until(async () => (await page.$$('#sceneList li')).length === i);
  }
  check((await page.$$('#sceneList li')).length === 8, 'сохранены все 8 сцен');
  await page.fill('#sceneName', 'Лишняя');
  await page.click('#sceneSave');
  check(await until(async () => (await page.textContent('#toast')).includes('Нет свободных мест')), 'девятая сцена: понятное сообщение');
  page.once('dialog', (d) => d.dismiss());
  await page.click('#sceneList li:nth-child(3) [data-act=delete]');
  await sleep(200);
  check((await page.$$('#sceneList li')).length === 8, 'отказ в подтверждении ничего не удаляет');
  page.once('dialog', (d) => d.accept());
  await page.click('#sceneList li:nth-child(3) [data-act=delete]');
  check(await until(async () => (await page.$$('#sceneList li')).length === 7), 'после подтверждения сцена удалена');
  await page.fill('#sceneName', 'Новая');
  await page.click('#sceneSave');
  check(await until(async () => (await page.$$('#sceneList li')).length === 8), 'освободившееся место снова доступно');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_scenes.png'), fullPage: true });
  jsErrors.length = 0;


  console.log('вкладка «Координаты»');
  const api = async (method, p, body, q = '') => {
    const r = await fetch(base + p + q, { method, headers: { 'X-Requested-With': 'GyverTwink', 'Content-Type': 'text/plain' }, body });
    return { status: r.status, json: await r.json().catch(() => ({})) };
  };
  const N = 120;
  const ring = (i) => [Math.round(128 + 100 * Math.cos((i / N) * 2 * Math.PI)), Math.round(128 + 100 * Math.sin((i / N) * 2 * Math.PI))];
  const hex2 = (v) => v.toString(16).padStart(2, '0');
  await page.click('[data-tab=control]');
  await setNumber('#ledAm', N);   // сервер перезапускался: число светодиодов снова по умолчанию
  check(await stateIs('ledAm', N), 'подготовка: ' + N + ' светодиодов');
  await page.click('[data-tab=coords]');
  check(await until(async () => await page.isVisible('#cEmpty')), 'координаты не заданы: подсказка про калибровку');
  await page.click('[data-tab=control]');
  check(await stateIs('calib', 0), 'ушли с вкладки: подсветка выключена');

  let hex = '';
  for (let i = 0; i < N; i++) { const [x, y] = ring(i); hex += hex2(x) + hex2(y); }
  const outlier = hex.slice(0, 60 * 4) + hex2(5) + hex2(5) + hex.slice(61 * 4);   // 61-й светодиод улетел в угол
  let r = await api('POST', '/api/xy', outlier, '?n=' + N);
  check(r.status === 200 && r.json.w > 100, 'координаты загружены на гирлянду (w=' + r.json.w + ')');
  await page.click('[data-tab=coords]');
  check(await until(async () => (await page.textContent('#cInfo')).includes('Светодиодов: 120, подозрительных: 1')), 'найдена одна подозрительная точка');
  check(await page.isHidden('#cEmpty'), 'подсказка про калибровку скрыта');
  check(await until(() => state.calib === 1 && state.lit === 0), 'первый светодиод горит на гирлянде');
  await page.click('#cNext');
  check(await until(() => state.lit === 1), 'кнопка ▶: горит второй светодиод');
  check((await page.$eval('#cIdx', (e) => e.value)) === '2', 'номер в поле (с единицы)');
  await page.click('#cPrev');
  await page.click('#cPrev');
  check(await until(() => state.lit === 119), 'кнопка ◀ с первого переходит на последний (по кругу)');
  await page.fill('#cIdx', '61');
  await page.dispatchEvent('#cIdx', 'change');
  check(await until(() => state.lit === 60), 'ввод номера 61: горит светодиод 60 (счёт с нуля в протоколе)');
  check((await page.$eval('#cX', (e) => e.value)) === '5' && (await page.$eval('#cY', (e) => e.value)) === '5', 'его координаты 5, 5');

  // перетаскивание точки мышью
  const where = () => page.evaluate(() => { const q = toScreen(coords.pts[coords.sel], coords.view); const b = document.getElementById('cCanvas').getBoundingClientRect(); return { x: b.left + q.x, y: b.top + q.y }; });
  let pt = await where();
  await page.mouse.move(pt.x, pt.y);
  await page.mouse.down();
  await page.mouse.move(pt.x + 60, pt.y + 40, { steps: 6 });
  await page.mouse.up();
  const dragged = [Number(await page.$eval('#cX', (e) => e.value)), Number(await page.$eval('#cY', (e) => e.value))];
  check(dragged[0] > 5 && dragged[1] > 5, 'перетаскивание меняет координаты (' + dragged.join(', ') + ')');
  check(!(await page.$eval('#cSave', (e) => e.disabled)), 'кнопка «Сохранить» стала активной');
  check((await page.textContent('#cInfo')).includes('несохранённые'), 'в сводке: есть несохранённые изменения');

  // отмена
  await page.click('#cUndo');
  check((await page.$eval('#cX', (e) => e.value)) === '5', 'отмена вернула исходное значение');
  check(await page.$eval('#cSave', (e) => e.disabled), 'после отмены сохранять нечего');

  // точное исправление числами и сохранение
  const [rx, ry] = ring(60);
  await page.fill('#cX', String(rx));
  await page.dispatchEvent('#cX', 'change');
  await page.fill('#cY', String(ry));
  await page.dispatchEvent('#cY', 'change');
  check(await until(async () => (await page.textContent('#cInfo')).includes('подозрительных: 0')), 'после исправления подозрительных точек нет');
  await page.click('#cSave');
  check(await until(async () => (await page.textContent('#toast')).includes('размер карты')), 'сохранение подтверждено');
  check(await page.$eval('#cSave', (e) => e.disabled), 'после сохранения кнопка снова неактивна');
  r = await api('GET', '/api/xy');
  check(r.json.xy.slice(60 * 4, 61 * 4) === hex2(rx) + hex2(ry), 'на гирлянде новые координаты точки');
  check(r.json.xy.slice(0, 60 * 4) === hex.slice(0, 60 * 4) && r.json.xy.slice(61 * 4) === hex.slice(61 * 4), 'остальные точки не тронуты');
  check(await stateIs('mmw', r.json.w) && state.mmh === r.json.h && state.mmw > 150, 'границы карты пересчитаны (' + state.mmw + ' x ' + state.mmh + ')');

  // число светодиодов изменили, пока страница редактировала точки
  await page.fill('#cX', '33');
  await page.dispatchEvent('#cX', 'change');
  await udpSend([2, 0, 1, 10]);   // 110 светодиодов по UDP
  await until(() => state.ledAm === 110);
  await page.click('#cSave');
  check(await until(async () => (await page.textContent('#toast')).includes('Число светодиодов изменилось')), 'число светодиодов изменилось: сохранение отклонено с понятным сообщением');
  await udpSend([2, 0, 1, 20]);   // вернуть 120
  await until(() => state.ledAm === 120);
  await page.click('[data-tab=control]');
  check(await stateIs('calib', 0), 'ушли с вкладки: подсветка выключена');
  check(await fitsScreen(), 'вкладка «Управление» по ширине помещается');
  await page.click('[data-tab=coords]');
  check(await fitsScreen(), 'вкладка «Координаты» помещается по ширине экрана 390 px');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_coords.png'), fullPage: true });
  await page.click('[data-tab=control]');
  jsErrors.length = 0;

  for (const tab of ['control', 'effects', 'scenes']) {
    await page.click('[data-tab=' + tab + ']');
    check(await fitsScreen(), 'вкладка «' + tab + '» помещается по ширине экрана 390 px');
  }

  console.log('безопасность');
  const noHdr = await fetch(base + '/api/set', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: 'bright=1' });
  check(noHdr.status === 403, 'POST без заголовка страницы отклоняется (403)');
  const preflight = await fetch(base + '/api/set', { method: 'OPTIONS', headers: { origin: 'http://evil.example', 'access-control-request-method': 'POST', 'access-control-request-headers': 'x-requested-with' } });
  check(!preflight.headers.get('access-control-allow-origin'), 'на CORS-предзапрос разрешение не выдаётся');

  await page.screenshot({ path: process.env.GT_SHOT || '/tmp/gt_web_effects.png', fullPage: true });
  await page.click('[data-tab=control]');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_control.png'), fullPage: true });

  check(jsErrors.length === 0, 'в консоли браузера нет ошибок' + (jsErrors.length ? ': ' + jsErrors.join(' | ') : ''));
} finally {
  await browser.close();
  server.kill();
  fs.rmSync(fsDir, { recursive: true, force: true });
}
console.log('\n' + (failed ? `FAILED: ${failed} check(s)` : 'all checks passed'));
process.exit(failed ? 1 : 0);

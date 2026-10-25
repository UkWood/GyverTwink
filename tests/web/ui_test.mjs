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

// Подмена моста приложения для Android: «камера» рисует кадры серии тем же способом, что make_calib_video.py (лампа, шум, пятна светодиодов)
function fakeCameraBridge(cfg) {
  const W = cfg.W, H = cfg.H, FPS = 22;
  const cam = { state: 'off', rec: false, recStart: 0, recEnd: 0, seriesStart: null, cleared: 0, started: 0, stopped: 0 };
  window.__cam = cam;
  const origFetch = window.fetch;
  window.fetch = function (url, opts) {
    const r = origFetch.apply(this, arguments);
    if (String(url).includes('/api/vcalib/start') && opts && opts.method === 'POST') r.then(() => { cam.seriesStart = performance.now(); });
    return r;
  };
  let seed = 12345;
  const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
  const gauss = () => Math.sqrt(-2 * Math.log(rnd() + 1e-9)) * Math.cos(2 * Math.PI * rnd());
  const sigma = 2.2 * W / 320;
  function blob(img, cx, cy, amp, sg) {
    const r = Math.floor(sg * 3) + 1;
    for (let y = Math.max(0, Math.floor(cy) - r); y < Math.min(H, Math.floor(cy) + r + 1); y++) {
      for (let x = Math.max(0, Math.floor(cx) - r); x < Math.min(W, Math.floor(cx) + r + 1); x++) {
        img[y * W + x] += amp * Math.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * sg * sg));
      }
    }
  }
  const n = cfg.p.n, step = cfg.step / 1000;
  const flashStart = cfg.delay + cfg.p.dark / 1000;
  const seqStart = flashStart + (cfg.p.flash + cfg.p.gap) / 1000;
  const markers = [];
  for (let k = 0; k < Math.min(n, 25); k++) markers.push(Math.floor(k * n / Math.min(n, 25)));
  function render(tRec) {   // tRec - секунды от начала записи
    const e = cam.seriesStart === null ? -1 : tRec - (cam.seriesStart - cam.recStart) / 1000;
    const img = new Float32Array(W * H).fill(22);
    blob(img, W * 0.8, H * 0.15, 90, 6 * W / 320);
    blob(img, W * 0.2 + 3.5 * tRec, H * 0.75 - tRec, 35, 9 * W / 320);
    if (e >= 0) {
      if (e >= flashStart && e < flashStart + cfg.p.flash / 1000) for (const i of markers) blob(img, cfg.pts[i][0], cfg.pts[i][1], 150, sigma);
      if (e >= seqStart && e < seqStart + n * step) blob(img, cfg.pts[Math.floor((e - seqStart) / step)][0], cfg.pts[Math.floor((e - seqStart) / step)][1], 150, sigma);
    }
    const k = 1 + 0.08 * Math.sin(tRec / 4);
    const out = new Uint8Array(W * H);
    for (let i = 0; i < out.length; i++) out[i] = Math.max(0, Math.min(255, img[i] * k + 2.5 * gauss()));
    let bin = '';
    for (let i = 0; i < out.length; i += 8192) bin += String.fromCharCode.apply(null, out.subarray(i, i + 8192));
    return btoa(bin);
  }
  const elapsed = () => (cam.rec ? performance.now() : cam.recEnd) - cam.recStart;
  window.GTApp = {
    info: () => '{}',
    changeDevice: () => {},
    cameraStart() { cam.started++; cam.state = 'asking'; setTimeout(() => { if (cam.state === 'asking') cam.state = 'ready'; }, 250); },
    cameraStop() { cam.stopped++; cam.state = 'off'; cam.rec = false; },
    cameraStatus() {
      const ms = Math.max(0, Math.round(elapsed()));
      return JSON.stringify({ state: cam.state, message: '', w: W, h: H, rec: cam.rec, frames: Math.floor(ms / 1000 * FPS), ms });
    },
    cameraLatest: () => render(Math.max(0, elapsed()) / 1000),
    cameraRecord(on) { if (on) { cam.rec = true; cam.recStart = performance.now(); cam.seriesStart = null; } else if (cam.rec) { cam.rec = false; cam.recEnd = performance.now(); } },
    cameraFrameAt: (t) => render(t),
    cameraClear() { cam.cleared++; },
  };
}

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
const otaOut = path.join(fsDir, 'ota-received.bin');   // сюда хост-сервер сохраняет принятую прошивку перед «перезагрузкой»
let state = {};

async function startServer() {
  state = {};
  let buf = '';
  server = spawn(exe, [String(udpPort)], { env: { ...process.env, GT_WEB_PORT: String(webPort), GT_FS_DIR: fsDir, GT_OTA_OUT: otaOut } });
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

  check((await page.$$('#tabs [role=tab]')).length === 7 && (await page.getAttribute('#tabs [data-tab=control]', 'aria-selected')) === 'true', 'вкладки размечены для скринридеров (role=tab, aria-selected)');
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

  console.log('порядок цветов ленты');
  check((await page.$eval('#colorOrder', (e) => e.value)) === '2', 'по умолчанию порядок GRB');
  await page.selectOption('#colorOrder', '4');
  check(await stateIs('order', 4), 'порядок BRG дошёл до гирлянды');
  await page.selectOption('#colorOrder', '2');
  check(await stateIs('order', 2), 'порядок возвращён');

  console.log('предел светодиодов');
  check((await page.$eval('#ledAm', (e) => e.max)) === '1000', 'в поле количества светодиодов предел 1000');
  await setNumber('#ledAm', 1000);
  check(await stateIs('ledAm', 1000), '1000 светодиодов принимаются');
  await setNumber('#ledAm', 120);
  check(await stateIs('ledAm', 120), 'число светодиодов возвращено к 120');

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
  check((await page.$$('#effList button')).length === 29, '29 эффектов в списке (22 палитры, 6 новых и Огонь v2)');
  check(await page.isHidden('#effEditor'), 'редактор скрыт, пока эффект не выбран');
  await page.click('#eff-7');
  check(await stateIs('curEff', 7), 'выбор эффекта делает его текущим');
  check(await stateIs('force', 0), 'временного показа нет');
  check(await until(async () => (await page.textContent('#cur')).startsWith('7.')), '«Сейчас» показывает выбранный эффект');
  check(await until(async () => (await page.textContent('#effName')).startsWith('7.')), 'в редакторе название эффекта');
  await page.uncheck('#effFav');
  check(await stateIs('fav', 0), 'флаг «избранное» снят');
  await setRange('#effScale', 77);
  check(await stateIs('scale', 77), 'масштаб эффекта 77');
  await setRange('#effSpeed', 200);
  check(await stateIs('speed', 200), 'скорость эффекта 200');
  await page.click('#eff-3');
  check(await stateIs('curEff', 3), 'переключение на другой эффект');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '50'), 'у эффекта 3 свои значения (масштаб 50)');
  await page.click('#eff-7');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '77'), 'настройки эффекта 7 сохранились');

  console.log('новые эффекты');
  check((await page.textContent('#effHint')) === '', 'у обычного эффекта подсказки нет');
  await page.click('#eff-24');   // огонь
  check(await stateIs('curEff', 24), 'новый эффект становится текущим');
  check(await until(async () => (await page.textContent('#effName')).startsWith('24.')), 'в редакторе название нового эффекта');
  check((await page.textContent('#effHint')).includes('Масштаб'), 'у нового эффекта есть подсказка про масштаб и скорость');
  check(await until(async () => (await page.$eval('#effFav', (e) => e.checked)) === true), 'новый эффект по умолчанию в избранном');
  await page.uncheck('#effFav');
  check(await stateIs('fav', 0), 'новый эффект можно убрать из избранного');
  await page.check('#effFav');
  check(await stateIs('fav', 1), 'новый эффект можно вернуть в избранное');
  await setRange('#effScale', 120);
  check(await stateIs('scale', 120), 'масштаб нового эффекта 120');
  await page.click('#eff-7');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '77'), 'настройки старых эффектов не затронуты');
  await page.click('#eff-24');
  check(await until(async () => (await page.$eval('#effScale', (e) => e.value)) === '120'), 'настройки нового эффекта сохранились');
  check(await until(async () => (await page.textContent('#effHint')).includes('Масштаб')), 'подсказка вернулась');

  console.log('Огонь v2');
  check(await page.isHidden('#fire2Box'), 'у обычных эффектов настроек формы нет');
  await page.click('#eff-28');
  check(await stateIs('curEff', 28), 'Огонь v2 становится текущим');
  check(await until(async () => await page.isVisible('#fire2Box')), 'появились настройки формы');
  check(await until(async () => (await page.$eval('#effShape', (e) => e.value)) === '120' && (await page.$eval('#effSparks', (e) => e.value)) === '80'), 'значения по умолчанию: колпак 120, искры 80');
  await setRange('#effShape', 200);
  await setRange('#effSparks', 10);
  await setRange('#effFlicker', 250);
  const fxGet = () => page.evaluate(async () => (await fetch('/api/effect?n=28')).json());
  check(await until(async () => {
    const fx = await fxGet();
    return fx.shape === 200 && fx.sparks === 10 && fx.flicker === 250;
  }), 'настройки формы дошли до гирлянды');
  await page.click('#eff-24');
  check(await until(() => page.isHidden('#fire2Box')), 'у другого эффекта настройки формы скрыты');
  await page.click('#eff-28');
  check(await until(async () => (await page.$eval('#effShape', (e) => e.value)) === '200'), 'настройки формы запомнились');
  console.log('Огонь v2: цвета уровней');
  check((await page.$$('#fire2Levels button')).length === 6, 'шесть кнопок уровней');
  check(await until(async () => (await page.isVisible('#effR')) && (await page.isVisible('#effG')) && (await page.isVisible('#effB'))), 'три ползунка RGB');
  check(await until(async () => (await fxGet()).cols === '000000460600be1200ff4600ff8c08ffcd3c'), 'цвета по умолчанию (по фото костра) пришли с гирлянды');
  check(await until(async () => (await page.$eval('#effR', (e) => e.value)) === '0' && (await page.$eval('#fire2LevelName', (e) => e.textContent)).includes('самый холодный')), 'выбран уровень 1: чёрный, подпись «самый холодный»');
  await page.click('#fire2Lv3');
  check(await until(async () => (await page.$eval('#effR', (e) => e.value)) === '255' && (await page.$eval('#effG', (e) => e.value)) === '70' && (await page.$eval('#effB', (e) => e.value)) === '0'), 'уровень 4: ползунки показывают его цвет (255, 70, 0)');
  await setRange('#effG', 111);
  check(await until(async () => (await fxGet()).cols.substr(18, 6) === 'ff6f00'), 'движение ползунка сразу уходит на гирлянду (уровень 4 = ff6f00)');
  check((await page.$eval('#fire2Lv3', (e) => e.style.background)).includes('255, 111, 0'), 'кнопка уровня и образец градиента сразу показывают новый цвет');
  await setRange('#effB', 200);
  await setRange('#effR', 5);
  check(await until(async () => (await fxGet()).cols.substr(18, 6) === '056fc8'), 'все три канала уровня настраиваются');
  check((await fxGet()).cols.substr(0, 18) === '000000460600be1200', 'соседние уровни не тронуты');
  await page.click('#fire2Lv5');
  await setRange('#effB', 33);
  check(await until(async () => (await fxGet()).cols.substr(30, 6) === 'ffcd21'), 'уровень 6 настраивается отдельно');
  await page.click('#eff-24');
  await page.click('#eff-28');
  await page.click('#fire2Lv3');
  check(await until(async () => (await page.$eval('#effB', (e) => e.value)) === '200'), 'после возврата к эффекту цвета на месте');
  page.once('dialog', (d) => d.dismiss());
  await page.click('#fire2Reset');
  await new Promise((r) => setTimeout(r, 300));
  check((await fxGet()).cols.substr(18, 6) === '056fc8', 'отказ в подтверждении: цвета не сброшены');
  page.once('dialog', (d) => d.accept());
  await page.click('#fire2Reset');
  check(await until(async () => (await fxGet()).cols === '000000460600be1200ff4600ff8c08ffcd3c'), 'кнопка «Сбросить цвета» вернула значения по умолчанию');
  check(await until(async () => (await page.$eval('#effG', (e) => e.value)) === '70' && (await page.$eval('#effB', (e) => e.value)) === '0'), 'ползунки выбранного уровня 4 показывают значения по умолчанию (255, 70, 0)');
  check((await fxGet()).shape === 200, 'форма пламени сбросом не тронута');
  await page.click('#eff-24');


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
  check((await page.textContent('#sceneList .name')) === 'Вечер · схема 120', 'название сцены и метка схемы на 120 светодиодов (по умолчанию схема сохраняется)');
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
  check((await page.textContent('#sceneList .name')) === 'Вечер · схема 120', 'и с тем же названием и схемой');
  await page.click('[data-tab=control]');
  await setRange('#bright', 5);
  check(await stateIs('bright', 5), 'яркость 5');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList [data-act=load]');
  check(await stateIs('bright', 60), 'сцена после перезапуска применяется (яркость 60 - из перезаписи)');

  console.log('схема светодиодов в сцене');
  await page.click('[data-tab=control]');
  await setNumber('#ledAm', 90);
  check(await stateIs('ledAm', 90), 'число светодиодов изменено на 90');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList [data-act=load]');
  check(await stateIs('ledAm', 120), 'применение сцены вернуло число светодиодов 120 (схема из сцены, в том числе после перезапуска гирлянды)');
  check(await until(async () => (await page.textContent('#toast')).includes('схема: 120')), 'сообщение: применена схема на 120 светодиодов');
  await page.uncheck('#sceneMap');
  await page.fill('#sceneName', 'БезСхемы');
  await page.click('#sceneSave');
  check(await until(async () => (await page.$$('#sceneList li')).length === 2), 'сцена без схемы сохранена');
  const noMapName = await page.textContent('#sceneList li:nth-child(2) .name');
  check(noMapName === 'БезСхемы', 'у неё нет метки схемы (' + noMapName + ')');
  await page.check('#sceneMap');
  await page.click('[data-tab=control]');
  await setNumber('#ledAm', 90);
  check(await stateIs('ledAm', 90), 'число светодиодов снова 90');
  await page.click('[data-tab=scenes]');
  await page.click('#sceneList li:nth-child(2) [data-act=load]');
  await sleep(300);
  check(state.ledAm === 90, 'сцена без схемы число светодиодов не меняет');
  page.once('dialog', (d) => d.accept());
  await page.click('#sceneList li:nth-child(2) [data-act=delete]');
  check(await until(async () => (await page.$$('#sceneList li')).length === 1), 'сцена без схемы удалена');
  await page.click('[data-tab=control]');
  await setNumber('#ledAm', 120);
  check(await stateIs('ledAm', 120), 'число светодиодов возвращено к 120');
  await page.click('[data-tab=scenes]');

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

  console.log('вкладка «Расписание»');
  const form = async (p, params) => {
    const r = await fetch(base + p, { method: 'POST', headers: { 'X-Requested-With': 'GyverTwink', 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(params) });
    return { status: r.status, json: await r.json().catch(() => ({})) };
  };
  const getSched = async () => (await fetch(base + '/api/schedule')).json();
  await page.click('[data-tab=sched]');
  check(await until(async () => (await page.textContent('#sClock')) === 'Время не установлено'), 'часы не установлены: так и написано');
  check((await getSched()).synced === 0, 'устройство подтверждает: времени нет');
  check(await page.isHidden('#sGeo'), 'координаты скрыты, пока нет событий по солнцу');
  await page.click('#sSync');
  check(await until(async () => /^\d\d:\d\d:\d\d \(UTC/.test(await page.textContent('#sClock'))), 'после синхронизации страница показывает часы гирлянды');
  let sc = await getSched();
  const browserTz = await page.evaluate(() => -new Date().getTimezoneOffset());
  check(sc.synced === 1 && sc.src === 'manual', 'время установлено со страницы (источник: manual)');
  check(sc.tz === browserTz, 'часовой пояс взят у этого устройства');
  check(Math.abs(sc.utc - Date.now() / 1000) < 15, 'время гирлянды совпадает с часами компьютера');
  await page.check('#sEn');
  check(await until(async () => (await getSched()).en === 1), 'расписание включено со страницы');
  await page.uncheck('#sDays [data-day="5"]');
  await page.uncheck('#sDays [data-day="6"]');
  check(await until(async () => (await getSched()).days === 31), 'дни недели: только будни (маска 31)');
  await page.selectOption('#sOnT', '1');
  check(await page.isVisible('#sGeo') && await page.isVisible('#sOnOff') && await page.isHidden('#sOnTime'), 'выбран закат: поля смещения и координат появились сразу');
  check(await until(async () => (await getSched()).onT === 1), 'тип включения - закат');
  await setNumber('#sOnOff', -30);
  await page.fill('#sLat', '55.75');
  await page.dispatchEvent('#sLat', 'change');
  await page.fill('#sLon', '37.62');
  await page.dispatchEvent('#sLon', 'change');
  check(await until(async () => { const s = await getSched(); return s.onV === -30 && s.lat === 5575 && s.lon === 3762; }), 'смещение -30 минут, координаты 55,75 и 37,62 записаны');
  check(await until(async () => (await page.textContent('#sToday')).includes('включится')), 'на странице видно, когда сегодня включится гирлянда');
  await setNumber('#sOnOff', 999);
  check(await until(async () => (await page.textContent('#toast')).includes('Смещение')), 'смещение 999: сообщение об ошибке');
  check((await getSched()).onV === -30, 'неверное значение устройству не отправлено');
  await page.selectOption('#sOnT', '0');
  check(await until(async () => (await getSched()).onT === 0), 'снова включение по времени');
  check(await page.isVisible('#sOnTime') && (await page.$eval('#sOnTime', (e) => e.value)) !== '', 'у поля времени есть значение по умолчанию');
  // срабатывание по часам гирлянды: ставим время за несколько секунд до события и ждём
  check((await form('/api/schedule', { en: 1, tz: 0, days: 127, onT: 0, onV: 1080, offT: 0, offV: 1380 })).status === 200, 'подготовка: включение 18:00, выключение 23:00 (UTC)');
  await form('/api/set', { power: 0 });
  check(await stateIs('power', 0), 'подготовка: гирлянда выключена');
  await form('/api/time', { unix: Math.floor(Date.UTC(2026, 9, 22, 17, 59, 55) / 1000) });
  check(await until(() => state.power === 1, 20000), 'в 18:00 по часам гирлянды расписание её включило');
  check(await until(async () => (await page.textContent('#sToday')).includes('включится в 18:00'), 15000), 'страница показывает события сегодняшнего дня');
  await form('/api/time', { unix: Math.floor(Date.UTC(2026, 9, 22, 22, 59, 55) / 1000) });
  check(await until(() => state.power === 0, 20000), 'в 23:00 расписание её выключило');
  check((await form('/api/time', { unix: 5 })).status === 400, 'неверное время отклоняется');
  check((await form('/api/schedule', { tz: 9999 })).status === 400, 'неверный пояс отклоняется');
  await page.selectOption('#sOnT', '0');
  check(await fitsScreen(), 'вкладка «Расписание» помещается по ширине экрана 390 px');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_sched.png'), fullPage: true });
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
  const where = () => page.evaluate(() => { const c = document.getElementById('cCanvas'); c.scrollIntoView({ block: 'center' }); const q = toScreen(coords.pts[coords.sel], coords.view); const b = c.getBoundingClientRect(); return { x: b.left + q.x, y: b.top + q.y }; });
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

  console.log('схема «спираль»');
  check(await page.isVisible('#spiralCard') && await page.isDisabled('#spBuild'), 'карточка схемы есть, строить пока нечего (поле N1 пусто)');
  check((await page.$eval('#spGap', (e) => e.value)) === '3', 'расстояние между рядами по умолчанию 3 (ряды разведены для удобной правки)');
  for (let i = 0; i < 3; i++) await page.click('#spAdd');
  const spVals = [30, 28, 26, 24];
  for (let i = 0; i < 4; i++) await page.fill('#spN' + (i + 1), String(spVals[i]));
  check((await page.$$('#spRows input')).length === 4, 'четыре поля N1…N4');
  check(await until(async () => (await page.textContent('#spInfo')).includes('последний ряд — остаток: 12')), 'подсказка: остаток 120 − 108 = 12 светодиодов в верхнем ряду');
  check(await page.isEnabled('#spBuild'), 'схему можно строить');
  await page.fill('#spN4', '60');   // 30 + 28 + 26 + 60 = 144 > 120
  check(await until(async () => (await page.textContent('#spInfo')).includes('больше числа светодиодов')) && await page.isDisabled('#spBuild'), 'сумма рядов больше S: понятная ошибка, построить нельзя');
  await page.fill('#spN4', '24');
  check(await until(async () => await page.isEnabled('#spBuild')), 'после исправления снова можно строить');
  await page.click('#spBuild');
  check(await until(async () => (await page.textContent('#cInfo')).includes('несохранённые')), 'схема попала на карту как несохранённая правка');
  check(await page.isEnabled('#cSave') && await page.isEnabled('#cUndo'), 'сохранить и отменить доступны');
  await page.click('#cUndo');
  check(await until(async () => !(await page.textContent('#cInfo')).includes('несохранённые')), 'схему можно отменить, как любую правку');
  await page.click('#spBuild');
  check(await until(async () => (await page.textContent('#cInfo')).includes('несохранённые')), 'построена заново');
  await page.click('#spFlip');
  check(await until(async () => (await page.textContent('#cInfo')).includes('несохранённые')), 'переворот карты - несохранённая правка');
  await page.click('#cUndo');
  check(await until(async () => !(await page.textContent('#cInfo')).includes('несохранённые')), 'переворот можно отменить');
  await page.click('#spBuild');
  await page.click('#cSave');
  check(await until(async () => (await page.textContent('#toast')).includes('размер карты')), 'схема сохранена на гирлянду');
  r = await api('GET', '/api/xy');
  const spPts = Array.from({ length: 120 }, (_, i) => ({ x: parseInt(r.json.xy.substr(i * 4, 2), 16), y: parseInt(r.json.xy.substr(i * 4 + 2, 2), 16) }));
  const spRows = [30, 28, 26, 24, 12];
  let off = 0;
  const spYs = [];
  let spOk = true;
  const spWidths = [];
  for (const n of spRows) {
    const row = spPts.slice(off, off + n);
    off += n;
    const sameY = new Set(row.map((q) => q.y)).size === 1;
    const centred = Math.abs(row.reduce((a, q) => a + q.x, 0) / n - 127.5) <= 0.6;
    const ordered = row.every((q, i) => i === 0 || q.x > row[i - 1].x);
    spOk = spOk && sameY && centred && ordered;
    spYs.push(row[0].y);
    spWidths.push(row[n - 1].x - row[0].x);
  }
  check(spOk, 'на гирлянде 5 рядов по 30, 28, 26, 24 и 12: каждый на своей высоте, по центру, слева направо');
  check(spYs.every((y, i) => i === 0 || y < spYs[i - 1]), 'ряды идут снизу вверх (нижний - с наибольшим Y)');
  const spPitch = spWidths[0] / 29;
  check(Math.abs((spYs[0] - spYs[1]) / spPitch - 3) < 0.3, 'расстояние между рядами втрое больше шага по ряду (' + ((spYs[0] - spYs[1]) / spPitch).toFixed(2) + ')');
  check(spWidths.every((w, i) => i === 0 || w <= spWidths[i - 1] + 1), 'конус: верхние ряды не шире нижних (ширины ' + spWidths.join(', ') + ')');
  check(await stateIs('mmw', Math.max(...spPts.map((q) => q.x)) - Math.min(...spPts.map((q) => q.x))), 'границы карты пересчитаны по новой схеме');
  await page.click('#spFlip');
  await page.click('#cSave');
  check(await until(async () => (await page.textContent('#toast')).includes('размер карты')), 'перевёрнутая карта сохранена');
  r = await api('GET', '/api/xy');
  const flipY = (i) => parseInt(r.json.xy.substr(i * 4 + 2, 2), 16);
  check(flipY(0) === spYs[4] && flipY(119) === spYs[0], 'ряды зеркально поменялись местами');
  await page.reload();
  await page.click('[data-tab=coords]');
  check(await until(async () => (await page.$eval('#spN1', (e) => e.value)) === '30' && (await page.$eval('#spN4', (e) => e.value)) === '24'), 'введённые ряды запомнены в браузере');
  check(await fitsScreen(), 'вкладка «Координаты» со схемой помещается по ширине экрана 390 px');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_spiral.png'), fullPage: true });

  console.log('схема «соты»');
  const hexPath = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../../web/test/fixtures/gyvertwink-map-example.json');
  const hexLib = require('../../web/hexmap.js');
  check(await page.isVisible('#hexCard') && await page.isDisabled('#hxBuild'), 'карточка схемы «соты» есть, файл не выбран: строить нечего');
  await page.setInputFiles('#hxFile', { name: 'bad.json', mimeType: 'application/json', buffer: Buffer.from('{"leds": []}') });
  check(await until(async () => (await page.textContent('#hxInfo')).includes('gyvertwink_map')), 'чужой файл: понятное сообщение');
  check(await page.isDisabled('#hxBuild'), 'чужой файл: строить нельзя');
  await page.setInputFiles('#hxFile', hexPath);
  check(await until(async () => (await page.textContent('#hxInfo')).includes('378 светодиодов') && (await page.textContent('#hxInfo')).includes('866 × 800 мм')), 'файл прочитан: 378 светодиодов, габариты 866 × 800 мм');
  check((await page.textContent('#hxInfo')).includes('На гирлянде сейчас 120') && (await page.textContent('#hxInfo')).includes('изменено на 378 автоматически'), 'видно, что на гирлянде 120 светодиодов и число изменится автоматически');
  check(await page.isEnabled('#hxBuild'), 'число не совпадает, но в пределах 1000: строить можно');
  await page.click('#hxBuild');
  check(await stateIs('ledAm', 378), 'число светодиодов на гирлянде автоматически стало 378');
  check(await until(async () => (await page.textContent('#cInfo')).includes('несохранённые')), 'схема попала на карту как несохранённая правка');
  check(await until(async () => (await page.textContent('#toast')).includes('120 → 378')), 'сообщение: число светодиодов изменено 120 → 378');
  check(await page.evaluate(() => coords.pts.length === 378 && coords.under.length === 54), 'на карте 378 точек и контур из 54 рёбер');
  await page.uncheck('#hxOutline');
  await page.check('#hxOutline');
  await page.click('#cSave');
  check(await until(async () => (await page.textContent('#toast')).includes('размер карты')), 'схема сохранена на гирлянду');
  r = await api('GET', '/api/xy');
  const hexExpect = hexLib.layoutHexMap(hexLib.parseHexMap(fs.readFileSync(hexPath, 'utf8')).map).pts;
  let hexSame = r.json.n === 378;
  for (let i = 0; i < 378; i++) {
    const px = parseInt(r.json.xy.substr(i * 4, 2), 16), py = parseInt(r.json.xy.substr(i * 4 + 2, 2), 16);
    hexSame = hexSame && px === hexExpect[i].x && py === hexExpect[i].y;
  }
  check(hexSame, 'на гирлянде точки схемы в том же порядке и на тех же местах, что в расчёте страницы');
  check(await fitsScreen(), 'вкладка «Координаты» со схемой «соты» помещается по ширине экрана 390 px');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_hex.png'), fullPage: true });
  // контур относится к своей карте
  const flipRes = await page.evaluate(() => {
    let lo = 255, hi = 0;
    for (const p of coords.pts) { lo = Math.min(lo, p.y); hi = Math.max(hi, p.y); }
    return { lo, hi, before: coords.under.map((e) => e.slice()) };
  });
  await page.click('#spFlip');
  check(await page.evaluate((f) => coords.under.length === f.before.length && coords.under.every((e, i) => e[0] === f.before[i][0] && Math.abs(e[1] - (f.lo + f.hi - f.before[i][1])) < 0.11 && Math.abs(e[3] - (f.lo + f.hi - f.before[i][3])) < 0.11), flipRes), 'переворот карты зеркалит и контур схемы «соты»');
  await page.click('#spFlip');   // обратно
  await page.click('#cUndo');
  check(await page.evaluate(() => coords.under.length === 54), 'отмена правок на сохранённой карте контур не трогает');
  await page.click('#spBuild');
  check(await until(async () => await page.evaluate(() => coords.pts.length === 378 && coords.under.length === 0)), 'схема «спираль» убирает контур «сот»: каши из двух схем нет');
  await page.click('#hxBuild');
  check(await until(async () => await page.evaluate(() => coords.under.length === 54)), 'схема «соты» построена заново вместе с контуром');
  await page.click('#cUndo');
  check(await page.evaluate(() => coords.under.length === 0), 'отмена несохранённой схемы убирает её контур');
  // схема больше предела прошивки: предупреждение и построить нельзя
  const tooMany = JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 1001, leds: Array.from({ length: 1001 }, (_, i) => [i, 0]) });
  await page.setInputFiles('#hxFile', { name: 'big.json', mimeType: 'application/json', buffer: Buffer.from(tooMany) });
  check(await until(async () => (await page.textContent('#hxInfo')).includes('предел прошивки 1000')), 'схема на 1001 светодиод: понятное предупреждение о пределе');
  check((await page.textContent('#toast')).includes('предел'), 'и всплывающее сообщение');
  check(await page.isDisabled('#hxBuild') && state.ledAm === 378, 'построить такую схему нельзя, число светодиодов не менялось');

  console.log('масштаб карты');
  // точка холста (доли ширины и высоты) в координатах окна: холст сначала прокручивается в кадр
  const cpt = (fx, fy) => page.evaluate(([a, b]) => { const c = document.getElementById('cCanvas'); c.scrollIntoView({ block: 'center' }); const r = c.getBoundingClientRect(); return { x: r.left + a * r.width, y: r.top + b * r.height, w: r.width, h: r.height }; }, [fx, fy]);
  const worldAt = (mx, my) => page.evaluate(([x, y]) => { const r = document.getElementById('cCanvas').getBoundingClientRect(); return { x: (x - r.left - coords.view.ox) / coords.view.s, y: (y - r.top - coords.view.oy) / coords.view.s }; }, [mx, my]);
  check(await page.evaluate(() => coords.zoom === 1 && coords.pan.x === 0), 'по умолчанию карта вписана целиком (масштаб 1)');
  const m0 = await cpt(0.3, 0.3);
  const mx = Math.round(m0.x), my = Math.round(m0.y);
  const w0 = await worldAt(mx, my);
  const scrollBefore = await page.evaluate(() => window.scrollY);
  await page.mouse.move(mx, my);
  await page.mouse.wheel(0, -800);
  check(await until(() => page.evaluate(() => coords.zoom > 1.5)), 'колесо мыши увеличивает карту');
  const w1 = await worldAt(mx, my);
  check(Math.abs(w1.x - w0.x) < 0.3 && Math.abs(w1.y - w0.y) < 0.3, 'точка карты под курсором осталась на месте (' + [w0.x, w1.x, w0.y, w1.y].map((v) => v.toFixed(2)).join(' / ') + ')');
  check((await page.evaluate(() => window.scrollY)) === scrollBefore, 'страница при этом не прокручивается');
  for (let i = 0; i < 4; i++) await page.mouse.wheel(0, 400);
  check(await until(() => page.evaluate(() => coords.zoom === 1 && coords.pan.x === 0 && coords.pan.y === 0)), 'колесо в обратную сторону возвращает вписанный вид, сдвиг сброшен');
  await page.click('#cZoomIn');
  check(await page.evaluate(() => Math.abs(coords.zoom - 1.5) < 1e-9) && (await page.textContent('#cZoomVal')) === '×1.5', 'кнопка «+»: масштаб 1,5');
  await page.click('#cZoomOut');
  check(await page.evaluate(() => coords.zoom === 1), 'кнопка «−»: обратно к 1');
  for (let i = 0; i < 20; i++) await page.click('#cZoomIn');
  check(await page.evaluate(() => coords.zoom === ZOOM_MAX), 'масштаб ограничен сверху (' + 20 + ')');
  await page.click('#cZoomFit');
  check(await page.evaluate(() => coords.zoom === 1) && (await page.textContent('#cZoomVal')) === '×1', 'кнопка «Вписать»');
  // два пальца (события указателей: живой мультитач в Playwright недоступен)
  const pinch = (cx, cy, from, to) => page.evaluate(([x, y, a, b]) => {
    const cv = document.getElementById('cCanvas');
    const r = cv.getBoundingClientRect();
    const ev = (type, id, px) => cv.dispatchEvent(new PointerEvent(type, { pointerId: id, clientX: r.left + px, clientY: r.top + y, bubbles: true, pointerType: 'touch' }));
    ev('pointerdown', 11, x - a); ev('pointerdown', 12, x + a);
    ev('pointermove', 12, x + b); ev('pointermove', 11, x - b);
    ev('pointerup', 11, x - b); ev('pointerup', 12, x + b);
  }, [cx, cy, from, to]);
  const cc = await cpt(0.5, 0.5);
  await pinch(cc.w / 2, cc.h / 2, 40, 90);
  check(await page.evaluate(() => coords.zoom > 2.0 && coords.zoom < 2.5), 'два пальца раздвинуты в 2,25 раза: масштаб растёт');
  check(await page.evaluate(() => !coords.dirty && !coords.drag && coords.pointers.size === 0), 'жест не правит точки и не оставляет «зажатых» указателей');
  await pinch(cc.w / 2, cc.h / 2, 90, 30);
  check(await page.evaluate(() => coords.zoom < 1.3), 'два пальца сведены: масштаб уменьшается');
  await page.click('#cZoomFit');
  // сдвиг при увеличении: тянем пустое место
  for (let i = 0; i < 3; i++) await page.click('#cZoomIn');
  const free = await page.evaluate(() => {
    const cv = document.getElementById('cCanvas');
    cv.scrollIntoView({ block: 'center' });
    const r = cv.getBoundingClientRect();
    for (let y = 10; y < r.height - 10; y += 10) {   // любое место холста, где рядом нет светодиодов
      for (let x = 10; x < r.width - 10; x += 10) {
        if (nearestPoint(coords.pts, x, y, coords.view, 24) < 0) return { x: r.left + x, y: r.top + y };
      }
    }
    return null;
  });
  check(free !== null, 'в кадре есть свободное от светодиодов место для сдвига');
  if (free) {
    const panBefore = await page.evaluate(() => ({ x: coords.pan.x, y: coords.pan.y }));
    await page.mouse.move(free.x, free.y);
    await page.mouse.down();
    await page.mouse.move(free.x + 25, free.y + 15, { steps: 3 });
    await page.mouse.up();
    const panAfter = await page.evaluate(() => ({ x: coords.pan.x, y: coords.pan.y }));
    check(Math.abs(panAfter.x - panBefore.x - 25) < 1 && Math.abs(panAfter.y - panBefore.y - 15) < 1, 'перетаскивание пустого места сдвигает карту вместе с пальцем');
    check(await page.evaluate(() => !coords.dirty), 'сдвиг карты не правит точки');
  }
  // правка точки при увеличении: точка идёт за указателем
  const pt0 = await page.evaluate(() => { coordsSelect(0); const c = document.getElementById('cCanvas'); c.scrollIntoView({ block: 'center' }); const r = c.getBoundingClientRect(); const q = toScreen(coords.pts[0], coords.view); return { x: r.left + q.x, y: r.top + q.y, s: coords.view.s }; });
  await page.mouse.move(pt0.x, pt0.y);
  await page.mouse.down();
  await page.mouse.move(pt0.x + 12, pt0.y + 9, { steps: 3 });
  await page.mouse.up();
  const moved = await page.evaluate(() => { const r = document.getElementById('cCanvas').getBoundingClientRect(); const q = toScreen(coords.pts[0], coords.view); return { x: r.left + q.x, y: r.top + q.y, dirty: coords.dirty }; });
  check(moved.dirty && Math.abs(moved.x - (pt0.x + 12)) <= pt0.s / 2 + 1 && Math.abs(moved.y - (pt0.y + 9)) <= pt0.s / 2 + 1, 'при увеличении перетаскиваемая точка идёт за указателем (шаг карты ' + pt0.s.toFixed(1) + ' px)');
  await page.click('#cUndo');
  // выбор светодиода вне кадра подводит его в кадр
  await page.click('#cZoomFit');
  for (let i = 0; i < 8; i++) await page.click('#cZoomIn');
  await setNumber('#cIdx', 300);
  check(await page.evaluate(() => { const cv = document.getElementById('cCanvas'); const q = toScreen(coords.pts[299], coords.view); return q.x >= 0 && q.x <= cv.clientWidth && q.y >= 0 && q.y <= cv.clientHeight; }), 'светодиод №300 выбран по номеру при сильном увеличении: карта сдвинулась, он в кадре');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_zoom.png'), fullPage: true });
  await page.click('#spBuild');
  check(await until(() => page.evaluate(() => coords.zoom === 1 && coords.pan.x === 0)), 'новая схема показывается целиком: масштаб сброшен');
  await page.click('#cUndo');
  await page.click('[data-tab=control]');
  await setNumber('#ledAm', 120);   // как было до проверки
  check(await stateIs('ledAm', 120), 'число светодиодов возвращено к 120');
  await page.click('[data-tab=coords]');
  await page.click('[data-tab=control]');
  check(await stateIs('calib', 0), 'ушли с вкладки: подсветка выключена');
  check(await fitsScreen(), 'вкладка «Управление» по ширине помещается');
  await page.click('[data-tab=coords]');
  check(await fitsScreen(), 'вкладка «Координаты» помещается по ширине экрана 390 px');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_coords.png'), fullPage: true });
  await page.click('[data-tab=control]');
  jsErrors.length = 0;


  console.log('вкладка «Калибровка» (по видео)');
  {
    const here = path.dirname(new URL(import.meta.url).pathname);
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gt-video-'));
    // ledAm = 120 и координаты - кольцо (см. выше). Серию запускаем из интерфейса, но видео синтезируем сами по параметрам из прошивки
    await page.click('[data-tab=vcalib]');
    check(await until(async () => (await page.textContent('#vEstimate')).includes('Светодиодов: 120')), 'оценка длительности серии показана');
    await page.fill('#vDelay', '2');
    await page.fill('#vStep', '120');
    await page.click('#vStart');
    check(await until(() => state.calib === 1), 'серия запущена: эффекты на гирлянде отключены');
    check(await until(async () => (await page.textContent('#vStatus')).includes('Включай запись')), 'пока идёт пауза до начала, подсказка про запись');
    check(await page.isDisabled('#vStart') && !(await page.isDisabled('#vStop')), 'кнопка старта заблокирована, остановки доступна');
    check(await until(async () => (await page.textContent('#vStatus')).includes('Вспышка') || (await page.textContent('#vStatus')).includes('тёмная пауза'), 6000), 'статус сменился на «вспышка/тёмная пауза»');
    await page.click('#vStop');
    check(await stateIs('calib', 0), 'остановка вернула эффекты');
    check(await until(async () => !(await page.isDisabled('#vStart'))), 'кнопка старта снова доступна');

    const p = (await (await fetch(base + '/api/vcalib')).json());
    const cell = 320 / 60;
    const pxOf = (g) => [(g[0] + 0.5) * cell, (g[1] + 0.5) * cell];
    // положения для «съёмки»: кольцо в пределах кадра 60 x 106 клеток (ring() выше - для редактора, сетка 0..255)
    const truth = Array.from({ length: p.n }, (_, i) => [Math.round(30 + 25 * Math.cos((i / p.n) * 2 * Math.PI)), Math.round(40 + 25 * Math.sin((i / p.n) * 2 * Math.PI))]);
    const mkVideo = async (name, extra) => {
      const spec = { w: 320, h: 568, fps: 30, t0: 1.1, seed: 5, params: { n: p.n, step: 120, flash: p.flash, gap: p.gap, dark: p.dark, marker: p.marker, delay: 2 }, points: truth.map(pxOf), hidden: [], ...extra };
      const sp = path.join(tmp, name + '.json');
      const out = path.join(tmp, name + '.webm');
      fs.writeFileSync(sp, JSON.stringify(spec));
      await new Promise((resolve, reject) => {
        const c = spawn('python3', [path.join(here, 'make_calib_video.py'), sp, out], { stdio: 'inherit' });
        c.on('exit', (code) => (code ? reject(new Error('генератор видео: ' + code)) : resolve()));
      });
      return out;
    };

    // 1) нет вспышки на видео - понятная ошибка
    const dark = await mkVideo('dark', { no_leds: true, params: { n: p.n, step: 120, flash: p.flash, gap: p.gap, dark: p.dark, marker: p.marker, delay: 0 } });
    await page.setInputFiles('#vFile', dark);
    await page.click('#vAnalyze');
    check(await until(async () => /Не найдена вспышка|неразличима/.test(await page.textContent('#vResult')), 180000), 'видео без вспышки: сообщение про маркер');
    check(await until(async () => !(await page.isDisabled('#vAnalyze')), 10000), 'после ошибки можно выбрать другое видео');

    // 2) нормальное видео: часть светодиодов вне кадра
    const hidden = [0, 1, 2, 3, 4, 60];
    const good = await mkVideo('good', { hidden });
    await page.setInputFiles('#vFile', good);
    await page.click('#vAnalyze');
    check(await until(async () => (await page.textContent('#vBar2')) !== null && (await page.$eval('#vBar2', (e) => parseInt(e.style.width) || 0)) > 10, 60000), 'идёт разбор: полоса прогресса двигается');
    check(await until(async () => (await page.$eval('#tab-coords', (e) => e.classList.contains('active'))), 240000), 'разбор закончен: открылась вкладка «Координаты»');
    check((await page.textContent('#cInfo')).includes('несохранённые'), 'результат лежит в редакторе как несохранённая правка');
    const infoText = await page.textContent('#cInfo');
    check(infoText.includes('оценено по соседям: ' + hidden.length), 'светодиоды вне кадра помечены как оценённые (' + hidden.length + '); в сводке: «' + infoText + '»');
    const got = await page.evaluate(() => coords.pts.map((q) => [q.x, q.y]));
    const bad = [];
    got.forEach((q, i) => { if (!hidden.includes(i) && (Math.abs(q[0] - truth[i][0]) > 1 || Math.abs(q[1] - truth[i][1]) > 1)) bad.push(i); });
    check(bad.length === 0, 'найденные светодиоды совпадают с настоящими с точностью до клетки сетки' + (bad.length ? ' (расхождения: ' + bad.slice(0, 10).join(',') + ')' : ''));
    check(Math.abs(got[60][0] - truth[60][0]) <= 3 && Math.abs(got[60][1] - truth[60][1]) <= 3, 'оценка спрятанного светодиода внутри цепочки близка к настоящему положению');
    await page.click('#cSave');
    check(await until(async () => (await page.textContent('#toast')).includes('размер карты')), 'результат сохранён на гирлянду');
    const saved = (await (await fetch(base + '/api/xy')).json()).xy;
    const dev = Array.from({ length: p.n }, (_, i) => [parseInt(saved.substr(i * 4, 2), 16), parseInt(saved.substr(i * 4 + 2, 2), 16)]);
    check(dev.every((q, i) => q[0] === got[i][0] && q[1] === got[i][1]), 'на гирлянде ровно те координаты, что показал редактор');
    check(await page.$eval('#vFile', (e) => e.files.length) === 1, 'файл остался выбранным');

    // 3) отмена разбора
    await page.click('[data-tab=vcalib]');
    await page.click('#vAnalyze');
    await until(async () => (await page.$eval('#vBar2', (e) => parseInt(e.style.width) || 0)) > 3, 60000);
    await page.click('#vCancel');
    check(await until(async () => (await page.textContent('#vResult')).includes('Прервано'), 60000), 'отмена прерывает разбор');
    check(await fitsScreen(), 'вкладка «Калибровка» помещается по ширине экрана 390 px');
    await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_vcalib.png'), fullPage: true });
    fs.rmSync(tmp, { recursive: true, force: true });
    await page.click('[data-tab=control]');
    jsErrors.length = 0;
  }


  console.log('калибровка живой камерой (мост window.GTApp)');
  {
    const p = (await (await fetch(base + '/api/vcalib')).json());
    const W = 240, H = 426, cell = W / 60;
    const truth = Array.from({ length: p.n }, (_, i) => [Math.round(30 + 25 * Math.cos((i / p.n) * 2 * Math.PI)), Math.round(40 + 25 * Math.sin((i / p.n) * 2 * Math.PI))]);
    const pts = truth.map((g) => [(g[0] + 0.5) * cell, (g[1] + 0.5) * cell]);
    check(await page.isHidden('#liveCard'), 'в браузере карточка «Камера телефона» скрыта');
    const lp = await ctx.newPage();
    const lerrs = [];
    lp.on('pageerror', (e) => lerrs.push(e.message));
    await lp.addInitScript(fakeCameraBridge, { W, H, pts, p: { n: p.n, flash: p.flash, gap: p.gap, dark: p.dark }, step: 120, delay: 2 });
    await lp.goto(base + '/');
    await lp.click('[data-tab=vcalib]');
    check(await lp.isVisible('#liveCard'), 'в приложении карточка «Камера телефона» видна');
    check(await lp.isDisabled('#lvStart') && await lp.isHidden('#lvCanvas'), 'камера выключена: съёмка недоступна, кадра нет');
    await lp.click('#lvCam');
    check(await until(async () => (await lp.textContent('#lvStatus')).includes('Разреши доступ')), 'пока система спрашивает разрешение, подсказка про доступ');
    check(await until(async () => !(await lp.isDisabled('#lvStart')), 4000), 'камера включилась: съёмка доступна');
    check(await lp.isVisible('#lvCanvas'), 'кадр камеры показан');
    check(await until(async () => (await lp.$eval('#lvCanvas', (c) => c.width)) === W, 3000), 'размер холста совпал с кадром камеры');
    const lit = await lp.evaluate(() => { const c = document.getElementById('lvCanvas'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let mx = 0; for (let i = 0; i < d.length; i += 4) mx = Math.max(mx, d[i]); return mx; });
    check(lit > 100, 'на показанном кадре яркость растянута (в тёмной комнате виден свет): ' + lit);

    await lp.fill('#vStep', '120');
    await lp.click('#lvStart');
    check(await until(() => state.calib === 1, 6000), 'серия на гирлянде запущена кнопкой «Снять и разобрать»');
    check(await lp.isVisible('#lvCancel') && await lp.isDisabled('#lvStart') && await lp.isDisabled('#lvCam'), 'во время съёмки: доступна только отмена');
    check(await until(async () => await lp.$eval('#tab-coords', (e) => e.classList.contains('active')), 120000), 'запись разобрана: открылась вкладка «Координаты»');
    check((await lp.textContent('#lvStatus')).includes('Найдено 120 из 120'), 'в строке состояния сводка: ' + (await lp.textContent('#lvStatus')));
    const got = await lp.evaluate(() => coords.pts.map((q) => [q.x, q.y]));
    const bad = [];
    got.forEach((q, i) => { if (Math.abs(q[0] - truth[i][0]) > 1 || Math.abs(q[1] - truth[i][1]) > 1) bad.push(i); });
    check(got.length === p.n && bad.length === 0, 'найденные светодиоды совпадают с настоящими с точностью до клетки сетки' + (bad.length ? ' (расхождения: ' + bad.slice(0, 10).join(',') + ')' : ''));
    check((await (await fetch(base + '/api/vcalib')).json()).run === 0, 'серия на гирлянде закончилась');
    check((await lp.evaluate(() => window.__cam.cleared)) >= 1, 'память записи освобождена');

    // отмена посреди серии
    await lp.click('[data-tab=vcalib]');
    check(await lp.isVisible('#lvCanvas') && !(await lp.isDisabled('#lvStart')), 'после съёмки камера осталась включённой');
    await lp.click('#lvStart');
    check(await until(() => state.calib === 1, 6000), 'вторая серия запущена');
    await sleep(1500);
    await lp.click('#lvCancel');
    check(await until(async () => (await lp.textContent('#lvStatus')).includes('Прервано'), 8000), 'отмена прерывает съёмку');
    check(await until(() => state.calib === 0, 8000), 'отмена останавливает серию на гирлянде');
    check(await until(async () => !(await lp.isDisabled('#lvStart')), 4000), 'после отмены можно снимать снова');

    // уход с вкладки освобождает камеру
    await lp.click('[data-tab=control]');
    check(await until(async () => (await lp.evaluate(() => window.__cam.state)) === 'off', 3000), 'уход с вкладки выключает камеру');
    check(lerrs.length === 0, 'в консоли приложения нет ошибок' + (lerrs.length ? ': ' + lerrs.join(' | ') : ''));
    await lp.close();
    await page.click('[data-tab=control]');
    jsErrors.length = 0;
  }


  console.log('вкладка «Прошивка» (обновление по WiFi)');
  {
    await page.click('[data-tab=ota]');
    check((await page.textContent('#otaVer')) === 'host', 'показана текущая версия');
    check(await until(async () => (await page.textContent('#otaStatus')).includes('Окно закрыто')), 'окно закрыто: подсказка нажать кнопку три раза');
    const notFw = Buffer.alloc(4096, 0x7f);
    const fw = Buffer.alloc(40000);
    for (let i = 0; i < fw.length; i++) fw[i] = (i * 131 + 7) & 255;
    fw[0] = 0xe9;
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gt-fw-'));
    fs.writeFileSync(path.join(tmp, 'notfw.bin'), notFw);
    fs.writeFileSync(path.join(tmp, 'firmware.bin'), fw);
    await page.setInputFiles('#otaFile', path.join(tmp, 'notfw.bin'));
    check(await until(async () => (await page.textContent('#otaResult')).includes('0xE9')), 'файл не с байта 0xE9: понятное предупреждение до загрузки');
    await page.setInputFiles('#otaFile', path.join(tmp, 'firmware.bin'));
    check(await until(async () => (await page.textContent('#otaResult')) === ''), 'подходящий файл: предупреждения нет');
    check(await page.isDisabled('#otaGo'), 'пока окно закрыто, загрузка недоступна');

    // закрытое окно отклоняет запрос и «по сети»
    const mp = () => { const fd = new FormData(); fd.append('firmware', new Blob([fw]), 'firmware.bin'); return fd; };
    let r = await fetch(base + '/update', { method: 'POST', headers: { 'X-Requested-With': 'GyverTwink' }, body: mp() });
    check(r.status === 403, 'закрытое окно: загрузка отклоняется (403)');

    // физическое действие: три нажатия кнопки
    server.stdin.write('BUTTON3\n');
    check(await stateIs('ota', 1), 'три нажатия кнопки открыли окно на гирлянде');
    check(await until(async () => (await page.textContent('#otaStatus')).includes('Окно обновления открыто'), 5000), 'страница показала открытое окно с обратным отсчётом');
    check(await until(async () => !(await page.isDisabled('#otaGo')), 3000), 'кнопка загрузки стала доступна');
    r = await fetch(base + '/update', { method: 'POST', body: mp() });
    check(r.status === 403, 'без заголовка страницы загрузка отклоняется даже при открытом окне');
    check(state.ota === 1, 'отказ окно не закрывает');

    page.once('dialog', (d) => d.accept());
    await page.click('#otaGo');
    check(await until(async () => (await page.textContent('#otaResult')).includes('Прошивка записана'), 20000), 'загрузка завершена: прошивка записана');
    await new Promise((resolve) => (server.exitCode !== null ? resolve() : server.once('exit', resolve)));
    check(fs.existsSync(otaOut) && Buffer.compare(fs.readFileSync(otaOut), fw) === 0, 'гирлянда получила файл побайтно без искажений (' + fw.length + ' байт)');
    await startServer();   // «перезагрузка»
    check(await until(async () => (await page.textContent('#otaResult')).includes('Готово. Версия прошивки: host'), 30000), 'страница дождалась возвращения гирлянды и показала версию');
    check(await stateIs('ota', 0), 'после обновления окно закрыто');
    await page.setInputFiles('#otaFile', path.join(tmp, 'firmware.bin'));
    check(await page.isDisabled('#otaGo'), 'повторная загрузка без новых трёх нажатий недоступна');
    check(await fitsScreen(), 'вкладка «Прошивка» помещается по ширине экрана 390 px');
    await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_ota.png'), fullPage: true });
    fs.rmSync(tmp, { recursive: true, force: true });
    await page.click('[data-tab=control]');
    jsErrors.length = 0;
  }

  await page.click('[data-tab=effects]');
  check((await page.getAttribute('#tabs [data-tab=effects]', 'aria-selected')) === 'true' && (await page.getAttribute('#tabs [data-tab=control]', 'aria-selected')) === 'false', 'aria-selected переключается вместе с вкладкой');
  for (const tab of ['control', 'effects', 'scenes', 'sched', 'vcalib', 'coords', 'ota']) {
    await page.click('[data-tab=' + tab + ']');
    check(await fitsScreen(), 'вкладка «' + tab + '» помещается по ширине экрана 390 px');
  }

  console.log('защита токеном');
  const authInfo = async (tok) => (await fetch(base + '/api/auth', { headers: tok ? { 'X-GT-Token': tok } : {} })).json();
  const stateWith = async (tok) => (await fetch(base + '/api/state', { headers: tok ? { 'X-GT-Token': tok } : {} })).status;
  const openWindow = async () => { server.stdin.write('BUTTON3\n'); check(await stateIs('ota', 1), 'три нажатия кнопки открыли окно'); };
  check((await authInfo()).on === 0 && (await stateWith('')) === 200, 'по умолчанию защиты нет: API открыт');
  await page.click('[data-tab=ota]');
  check(await until(async () => (await page.textContent('#gStatus')).includes('Защита выключена')), 'карточка защиты: выключена');
  check(await page.isDisabled('#gSave'), 'пока окно закрыто, сохранить токен нельзя');
  await page.click('#gGen');
  const gen = await page.$eval('#gToken', (e) => e.value);
  check(/^[A-HJ-NP-Za-km-z2-9]{16}$/.test(gen), 'кнопка «Сгенерировать»: 16 символов без похожих (' + gen + ')');
  await openWindow();
  check(await until(async () => !(await page.isDisabled('#gSave')), 5000), 'окно открыто: сохранение доступно');
  await page.fill('#gToken', 'short');
  await page.click('#gSave');
  check(await until(async () => (await page.textContent('#toast')).includes('8–32')), 'короткий токен: сообщение, на гирлянду не уходит');
  check((await authInfo()).on === 0, 'гирлянда осталась без защиты');
  await page.fill('#gToken', gen);
  await page.click('#gSave');
  check(await until(async () => (await authInfo()).on === 1), 'токен сохранён на гирлянде');
  check((await stateWith('')) === 401 && (await stateWith('wrong-token-1')) === 401, 'без токена и с неверным API закрыт (401)');
  check((await stateWith(gen)) === 200, 'с верным токеном API открыт');
  // гонка, из-за которой на CI падала main: плановый опрос ушёл без токена, пока страница его сохраняла. Теперь запрос повторяется
  // с новым токеном, а окно входа не появляется (запрос отправляется без токена, токен записывается до прихода ответа)
  check(await page.evaluate(async (tok) => {
    tokenClear();
    const p = api.get('/api/state');
    tokenSet(tok);
    const s = await p;
    return typeof s.ledAm === 'number' && $('login').hidden && !guardUi.needed;
  }, gen), 'запрос, ушедший без токена, пока страница его сохраняла, повторяется с новым токеном; окно входа не показывается');
  check(await stateIs('ota', 0), 'после смены токена окно закрылось');
  check(await until(async () => (await page.textContent('#gStatus')).includes('Защита включена')), 'карточка защиты: включена');
  check((await page.textContent('#gHint')).includes('остаётся открытым'), 'предупреждение: управление по UDP пока открыто');
  await page.click('[data-tab=control]');
  await setRange('#bright', 55);
  check(await stateIs('bright', 55), 'эта страница знает новый токен и продолжает управлять');
  await page.reload();
  check(await until(async () => (await page.textContent('#conn')) === 'на связи'), 'после перезагрузки страница входит сама (токен запомнен в браузере)');
  check(await page.isHidden('#login'), 'окно входа не показано');

  // UDP: приложение без токена; по умолчанию работает, после закрытия - нет
  await udpSend([2, 2, 66]);
  check(await stateIs('bright', 66), 'UDP при включённом токене по умолчанию работает (для приложения)');
  await page.click('[data-tab=ota]');
  await page.check('#gUdp');
  check(await until(async () => (await authInfo(gen)).udpLock === 1), 'управление по UDP закрыто со страницы (нужен только токен, окно не нужно)');
  await udpSend([2, 2, 99]);
  await new Promise((r) => setTimeout(r, 400));
  check(state.bright === 66, 'команда записи по UDP отклонена');
  await page.uncheck('#gUdp');
  check(await until(async () => (await authInfo(gen)).udpLock === 0), 'управление по UDP снова открыто');
  await udpSend([2, 2, 77]);
  check(await stateIs('bright', 77), 'UDP снова работает');

  // вход по токену
  await page.evaluate(() => localStorage.removeItem('gt-token'));
  await page.reload();
  check(await until(async () => await page.isVisible('#login')), 'без сохранённого токена показан вход');
  await page.fill('#loginToken', 'short');
  await page.click('#loginGo');
  check((await page.textContent('#loginMsg')).includes('8–32'), 'слишком короткий токен отклонён на странице');
  await page.fill('#loginToken', 'wrong-token-1234');
  await page.click('#loginGo');
  check(await until(async () => (await page.textContent('#loginMsg')).includes('Неверный токен')), 'неверный токен: сообщение');
  check(await page.isVisible('#login') && (await page.evaluate(() => localStorage.getItem('gt-token') || '')) === '', 'вход остаётся, неверный токен не запоминается');
  await page.fill('#loginToken', gen);
  await page.click('#loginGo');
  check(await until(async () => await page.isHidden('#login')), 'верный токен: вход выполнен');
  check(await until(async () => (await page.textContent('#conn')) === 'на связи'), 'после входа страница работает');
  check((await page.evaluate(() => localStorage.getItem('gt-token'))) === gen, 'токен запомнен');

  // токен сменили с другого устройства: страница не засыпает гирлянду неверными запросами, а просит войти
  await openWindow();
  const gen2 = gen.split('').reverse().join('');
  let rr = await form('/api/auth', { token: gen2 });
  check(rr.status === 200, 'токен сменён «с другого устройства»');
  check(await until(async () => await page.isVisible('#login'), 8000), 'у этой страницы токен устарел: показан вход');
  check((await authInfo(gen2)).locked === 0, 'адрес не заблокирован (запросы после первого отказа не уходили)');
  await page.fill('#loginToken', gen2);
  await page.click('#loginGo');
  check(await until(async () => await page.isHidden('#login')), 'вход с новым токеном');

  // подбор ограничен: после нескольких неверных токенов адрес блокируется, даже верный токен не принимается
  let last = 0;
  for (let i = 0; i < 6; i++) last = await stateWith('guess-' + i);
  check(last === 429, 'после серии неверных токенов адрес заблокирован (429)');
  check((await stateWith(gen2)) === 429, 'в блокировке верный токен тоже не принимается');
  check((await authInfo(gen2)).locked === 1, 'статус сообщает о блокировке');
  // снять защиту: токен не нужен (физическое окно), блокировка на это не влияет
  await openWindow();
  rr = await form('/api/auth', { token: '' });
  check(rr.status === 200 && (await authInfo()).on === 0, 'защита снята через окно, токен для этого не нужен');
  check((await stateWith('')) === 200, 'после снятия защиты API снова открыт');
  await page.evaluate(() => localStorage.removeItem('gt-token'));
  await page.reload();
  check(await until(async () => (await page.textContent('#conn')) === 'на связи'), 'страница работает без токена');
  await page.click('[data-tab=ota]');
  check(await until(async () => (await page.textContent('#gStatus')).includes('Защита выключена')), 'карточка защиты: выключена');
  check(await page.isHidden('#login'), 'вход не показан');
  await page.screenshot({ path: (process.env.GT_SHOT || '/tmp/gt_web_effects.png').replace('.png', '_guard.png'), fullPage: true });
  await page.click('[data-tab=control]');
  jsErrors.length = 0;

  console.log('внутри приложения для Android');
  check(await page.isHidden('#appSwitch'), 'в браузере кнопка «Сменить гирлянду» скрыта');
  const appPage = await ctx.newPage();
  await appPage.addInitScript(() => { window.__gtSwitched = 0; window.GTApp = { info: () => '{}', changeDevice: () => { window.__gtSwitched++; } }; });
  await appPage.goto(base + '/');
  check(await until(async () => await appPage.isVisible('#appSwitch')), 'при наличии window.GTApp кнопка видна');
  await appPage.click('#appSwitch');
  check((await appPage.evaluate(() => window.__gtSwitched)) === 1, 'кнопка вызывает GTApp.changeDevice()');
  await appPage.close();

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

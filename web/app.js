'use strict';
// Веб-интерфейс GyverTwink. Один файл без зависимостей: страница вшита в прошивку (gzip),
// интернет не нужен. API описан в docs/WEB_API.md. Исходники: web/, сборка: tools/gen_web.py.

const EFFECT_NAMES = [
  'Party_grad', 'Raibow_grad', 'Stripe_grad', 'Sunset_grad', 'Pepsi_grad', 'Warm_grad',
  'Cold_grad', 'Hot_grad', 'Pink_grad', 'Cyber_grad', 'RedWhite_grad',
  'Party_noise', 'Raibow_noise', 'Stripe_noise', 'Sunset_noise', 'Pepsi_noise', 'Warm_noise',
  'Cold_noise', 'Hot_noise', 'Pink_noise', 'Cyber_noise', 'RedWhite_noise',
  // новые эффекты (номера с ACTIVE_PALETTES * 2 в прошивке; страница вшита в прошивку, поэтому списки всегда совпадают)
  'Звёзды', 'Бегущие огни', 'Огонь', 'Дыхание', 'Волны по высоте', 'Волны от центра',
  'Огонь v2',
];

// что значат масштаб и скорость у новых эффектов (у первых 22 - масштаб узора и скорость течения)
const EFFECT_HINTS = {
  22: 'Масштаб — сколько искр одновременно, скорость — как быстро они гаснут.',
  23: 'Масштаб — сколько бегущих огней на карте, скорость — как быстро бегут (ниже 128 — в обратную сторону).',
  24: 'Масштаб — размер языков пламени (они вытянуты вверх), скорость — как быстро они поднимаются (ниже 128 — пламя течёт вниз). Низ гирлянды — там, где Y на карте больше.',
  25: 'Масштаб — разница фаз по карте (0 — все дышат одновременно), скорость — частота дыхания.',
  26: 'Масштаб — сколько волн по высоте, скорость — как быстро они идут (ниже 128 — в обратную сторону).',
  27: 'Масштаб — сколько колец от центра карты, скорость — как быстро они расходятся (ниже 128 — к центру).',
  28: 'Огонь с настройками формы и цвета. Масштаб и скорость — как у «Огня». Колпак — насколько пламя гаснет к краям и вверху, искры — вылетающие угольки над пламенем (цвета самого горячего уровня), трепет — мелкая быстрая дрожь. Ниже — цвета шести уровней жара. Эти настройки хранятся на гирлянде отдельно и в сцены не входят.',
};

const $ = (id) => document.getElementById(id);

// имена параметров в сообщениях об ошибках (прошивка отвечает {"error":"<параметр>"})
const FIELD_NAMES = {
  ledAm: 'количество светодиодов', power: 'питание', bright: 'яркость', auto: 'автосмена', rnd: 'случайный порядок',
  period: 'период смены', offOn: 'таймер выключения', offMin: 'минуты до выключения', maxMa: 'лимит тока',
  n: 'номер эффекта', fav: 'избранное', scale: 'масштаб', speed: 'скорость', show: 'показ эффекта', shape: 'колпак', sparks: 'искры', flicker: 'трепет', lv: 'уровень цвета', reset: 'сброс цветов', order: 'порядок цветов', r: 'красный', g: 'зелёный', b: 'синий',
  tz: 'часовой пояс', lat: 'широта', lon: 'долгота', days: 'дни недели', onT: 'тип включения', offT: 'тип выключения',
  onV: 'время или смещение включения', offV: 'время или смещение выключения', unix: 'время',
  token: 'токен (8–32 символа: латиница, цифры, . _ ~ -)', udpLock: 'блокировка управления по UDP',
};
// не относящиеся к параметрам коды ошибок прошивки
const ERROR_TEXT = {
  forbidden: 'Запрос отклонён устройством',
  count: 'Число светодиодов изменилось, данные перезагружены. Повтори правку',
  xy: 'Некорректные координаты',
  fs: 'Не удалось записать во флеш-память гирлянды',
  empty: 'Это место пусто',
  slot: 'Недопустимое место для сцены',
  name: 'Недопустимое название',
  i: 'Недопустимый номер светодиода',
  off: 'Недопустимый запрос подсветки',
  'not found': 'Устройство не знает такого запроса (устаревшая прошивка?)',
  'ota-closed': 'Окно обновления закрыто. Нажми кнопку на гирлянде три раза подряд',
  auth: 'Нужен токен доступа',
  locked: 'Слишком много неверных попыток с этого устройства. Подожди минуту',
  window: 'Окно закрыто: нажми кнопку на гирлянде три раза подряд',
  update: 'Гирлянда отклонила файл: это не подходящая прошивка или она не помещается',
};
function describeError(code, status) {
  if (ERROR_TEXT[code]) return ERROR_TEXT[code];
  if (FIELD_NAMES[code]) return 'Недопустимое значение: ' + FIELD_NAMES[code];
  return code || ('Ошибка ' + status);
}

// ---------- запросы к устройству ----------
// Все изменяющие запросы несут заголовок X-Requested-With: чужая страница в браузере не сможет
// отправить такой запрос без CORS-предзапроса, на который устройство не отвечает (защита от CSRF).
// Токен доступа (если защита включена) хранится в браузере и уходит в заголовке X-GT-Token.
const TOKEN_KEY = 'gt-token';
const tokenGet = () => { try { return localStorage.getItem(TOKEN_KEY) || ''; } catch (e) { return ''; } };
const tokenSet = (t) => { try { localStorage.setItem(TOKEN_KEY, t); } catch (e) { /* без хранилища токен придётся вводить заново */ } };
const tokenClear = () => { try { localStorage.removeItem(TOKEN_KEY); } catch (e) { /* ничего */ } };
const guardUi = { needed: false, started: false, saving: false };   // saving - идёт сохранение нового токена с этой страницы

// ждём конца сохранения токена (до 3 с), чтобы повторить запрос уже с новым токеном
async function guardSettled() {
  for (let i = 0; i < 30 && guardUi.saving; i++) await new Promise((resolve) => setTimeout(resolve, 100));
}

const api = {
  async call(method, path, params, body) {
    if (guardUi.needed && path !== '/api/auth') {   // вход не выполнен: пока не вводят токен, запросы не уходят (неверные считаются попытками подбора)
      const e = new Error(describeError('auth'));
      e.code = 'auth';
      throw e;
    }
    for (let attempt = 0; ; attempt++) {
      const opt = { method, headers: { 'X-Requested-With': 'GyverTwink' } };
      const tok = tokenGet();
      if (tok) opt.headers['X-GT-Token'] = tok;
      let url = path;
      if (params) {
        const qs = new URLSearchParams(params).toString();
        if (method === 'POST' && body === undefined) {
          opt.body = qs;
          opt.headers['Content-Type'] = 'application/x-www-form-urlencoded';
        } else {
          url += '?' + qs;
        }
      }
      if (body !== undefined) {
        opt.body = body;
        opt.headers['Content-Type'] = 'text/plain';
      }
      const r = await fetch(url, opt);
      let data = {};
      try { data = await r.json(); } catch (e) { /* не JSON */ }
      if (!r.ok) {
        // токен только что сменили с этой страницы, а запрос (например, плановый опрос) ушёл ещё со старым: повторяем один раз
        // с новым, а не показываем окно входа
        if (data.error === 'auth' && attempt === 0 && (guardUi.saving || tokenGet() !== tok)) {
          await guardSettled();
          continue;
        }
        if (data.error === 'auth') guardNeeded();
        const err = new Error(describeError(data.error, r.status));
        err.code = data.error;
        throw err;
      }
      return data;
    }
  },
  get(path, params) { return this.call('GET', path, params); },
  post(path, params, body) { return this.call('POST', path, params, body); },
};

let toastTimer = 0;
function toast(msg, good) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.toggle('good', !!good);
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 3500);
}

function debounced(fn, ms) {
  let t = 0;
  let last = [];
  return (...a) => {
    last = a;
    clearTimeout(t);
    t = setTimeout(() => fn(...last), ms);
  };
}

// элементы, которые пользователь сейчас двигает: опрос состояния их не трогает
const busy = new Set();
function trackBusy(el) {
  el.addEventListener('pointerdown', () => busy.add(el.id));
  el.addEventListener('focus', () => busy.add(el.id));
  const free = () => busy.delete(el.id);
  el.addEventListener('change', free);
  el.addEventListener('blur', free);
  el.addEventListener('pointerup', () => setTimeout(free, 300));
}

// ---------- состояние ----------
let st = null;

function setConn(ok) {
  const c = $('conn');
  c.className = ok ? 'ok' : 'bad';
  c.textContent = ok ? 'на связи' : 'нет связи';
}

// force: после ошибки вернуть в поле значение устройства, даже если оно в фокусе
function setVal(id, v, force) {
  const el = $(id);
  if (!force && (busy.has(id) || document.activeElement === el)) return;
  if (el.type === 'checkbox') el.checked = !!v;
  else el.value = v;
}

function render(force) {
  if (!st) return;
  const p = $('power');
  p.textContent = st.power ? 'Включено' : 'Выключено';
  p.classList.toggle('off', !st.power);
  setVal('bright', st.bright, force);
  $('bright-val').textContent = $('bright').value;
  setVal('auto', st.auto, force);
  setVal('rnd', st.rnd, force);
  $('period').max = Math.max(10, st.period);
  setVal('period', st.period, force);
  $('period-val').textContent = $('period').value;
  setVal('offOn', st.offOn, force);
  setVal('offMin', st.offMin, force);
  $('ledAm').max = st.ledMax;
  setVal('ledAm', st.ledAm, force);
  setVal('maxMa', st.maxMa, force);
  setVal('colorOrder', st.order, force);
  $('cur').textContent = st.cur + '. ' + EFFECT_NAMES[st.cur];
  $('foot').textContent = 'Прошивка ' + st.fw + ' · свободно ' + Math.round(st.heap / 1024) + ' КБ';
  markCurrentEffect();
}

async function refresh(force) {
  if (guardUi.needed) return;
  try {
    st = await api.get('/api/state');
    setConn(true);
    render(force);
    spiralInfo();   // число светодиодов могло измениться (S в подсказке схемы)
    hexInfo();
  } catch (e) {
    setConn(false);
  }
}

async function setParams(params) {
  try {
    st = await api.post('/api/set', params);
    setConn(true);
    render();
  } catch (e) {
    toast(e.message);
    busy.clear();
    refresh(true);
  }
}

// ---------- вкладка «Управление» ----------
function initControl() {
  $('power').addEventListener('click', () => setParams({ power: st && st.power ? 0 : 1 }));
  $('next').addEventListener('click', async () => {
    try { st = await api.post('/api/next'); render(); } catch (e) { toast(e.message); }
  });

  for (const [id, key] of [['bright', 'bright'], ['period', 'period']]) {
    const el = $(id);
    trackBusy(el);
    const send = debounced((v) => setParams({ [key]: v }), 70);
    el.addEventListener('input', () => {
      $(id + '-val').textContent = el.value;
      send(el.value);
    });
  }
  for (const [id, key] of [['auto', 'auto'], ['rnd', 'rnd'], ['offOn', 'offOn']]) {
    $(id).addEventListener('change', () => setParams({ [key]: $(id).checked ? 1 : 0 }));
  }
  $('colorOrder').addEventListener('change', () => setParams({ order: $('colorOrder').value }));
  for (const [id, key] of [['offMin', 'offMin'], ['ledAm', 'ledAm'], ['maxMa', 'maxMa']]) {
    const el = $(id);
    trackBusy(el);
    el.addEventListener('change', () => {
      if (el.value === '') { refresh(true); return; }
      setParams({ [key]: el.value });
    });
  }
}

// ---------- вкладка «Эффекты» ----------
let selEff = -1;

function markCurrentEffect() {
  if (!st) return;
  for (const b of document.querySelectorAll('#effList button')) {
    b.classList.toggle('cur', Number(b.dataset.n) === st.cur);
  }
}

function fillEffect(e) {
  $('effName').textContent = e.n + '. ' + EFFECT_NAMES[e.n];
  $('effHint').textContent = EFFECT_HINTS[e.n] || '';
  $('effFav').checked = !!e.fav;
  $('effScale').value = e.scale;
  $('effScale-val').textContent = e.scale;
  $('effSpeed').value = e.speed;
  $('effSpeed-val').textContent = e.speed;
  const fire2 = e.shape !== undefined;   // у «Огня v2» есть ещё четыре настройки формы
  $('fire2Box').hidden = !fire2;
  if (fire2) {
    for (const [id, key] of FIRE2_SLIDERS) {
      if (busy.has(id)) continue;
      $(id).value = e[key];
      $(id + '-val').textContent = e[key];
    }
    fire2Cols = hexToCols(e.cols);
    fire2Draw();
  }
}

// цвета шести уровней жара «Огня v2»: строка rrggbb по уровням <-> массив [r, g, b]
function hexToCols(hex) {
  const out = [];
  for (let l = 0; l < FIRE2_LEVELS; l++) out.push([0, 1, 2].map((c) => parseInt(hex.substr(l * 6 + c * 2, 2), 16) || 0));
  return out;
}

const FIRE2_LEVELS = 6;
let fire2Cols = hexToCols('');
let fire2Level = 0;

// перерисовать кнопки уровней, полоску-образец градиента и ползунки выбранного уровня
function fire2Draw() {
  const css = (c) => 'rgb(' + c[0] + ',' + c[1] + ',' + c[2] + ')';
  document.querySelectorAll('#fire2Levels button').forEach((b, l) => {
    b.style.background = css(fire2Cols[l]);
    b.classList.toggle('sel', l === fire2Level);
  });
  $('fire2Bar').style.background = 'linear-gradient(to right, ' + fire2Cols.map(css).join(', ') + ')';
  ['effR', 'effG', 'effB'].forEach((id, c) => {
    if (busy.has(id)) return;
    $(id).value = fire2Cols[fire2Level][c];
    $(id + '-val').textContent = fire2Cols[fire2Level][c];
  });
  $('fire2LevelName').textContent = 'Уровень ' + (fire2Level + 1) + ' из ' + FIRE2_LEVELS + (fire2Level === 0 ? ' (самый холодный)' : fire2Level === FIRE2_LEVELS - 1 ? ' (самый горячий)' : '');
}

const FIRE2_SLIDERS = [['effShape', 'shape'], ['effSparks', 'sparks'], ['effFlicker', 'flicker']];

async function selectEffect(n) {
  selEff = n;
  for (const b of document.querySelectorAll('#effList button')) b.classList.toggle('sel', Number(b.dataset.n) === n);
  $('effEditor').hidden = false;
  try {
    // show=1: прошивка делает этот эффект текущим
    fillEffect(await api.post('/api/effect', { n, show: 1 }));
  } catch (e) {
    toast(e.message);
  }
}

async function editEffect(params) {
  if (selEff < 0) return;
  try {
    const e = await api.post('/api/effect', Object.assign({ n: selEff, show: 1 }, params));
    if (!['effScale', 'effSpeed', 'effShape', 'effSparks', 'effFlicker', 'effR', 'effG', 'effB'].some((id) => busy.has(id))) fillEffect(e);
  } catch (e) {
    toast(e.message);
  }
}

function initEffects() {
  const list = $('effList');
  EFFECT_NAMES.forEach((name, n) => {
    const li = document.createElement('li');
    const b = document.createElement('button');
    b.id = 'eff-' + n;
    b.dataset.n = n;
    b.textContent = n + '. ' + name;
    b.addEventListener('click', () => selectEffect(n));
    li.appendChild(b);
    list.appendChild(li);
  });
  $('effFav').addEventListener('change', () => editEffect({ fav: $('effFav').checked ? 1 : 0 }));
  for (const [id, key] of [['effScale', 'scale'], ['effSpeed', 'speed']]) {
    const el = $(id);
    trackBusy(el);
    const send = debounced((v) => editEffect({ [key]: v }), 70);
    el.addEventListener('input', () => {
      $(id + '-val').textContent = el.value;
      send(el.value);
    });
  }
  for (const [id, key] of FIRE2_SLIDERS) {
    const el = $(id);
    trackBusy(el);
    const send = debounced((v) => editEffect({ [key]: v }), 70);
    el.addEventListener('input', () => {
      $(id + '-val').textContent = el.value;
      send(el.value);
    });
  }
  $('fire2Reset').addEventListener('click', async () => {
    if (!window.confirm('Вернуть цвета пламени к значениям по умолчанию (как на фото костра)?')) return;
    await editEffect({ reset: 1 });
  });
  // цвета уровней: выбираешь уровень - три ползунка RGB; движение сразу уходит на гирлянду (она показывает этот эффект)
  const lvBox = $('fire2Levels');
  for (let l = 0; l < FIRE2_LEVELS; l++) {
    const b = document.createElement('button');
    b.id = 'fire2Lv' + l;
    b.type = 'button';
    b.textContent = String(l + 1);
    b.addEventListener('click', () => { fire2Level = l; fire2Draw(); });
    lvBox.appendChild(b);
  }
  // отправка с задержкой у каждого уровня своя: быстрый переход на другой уровень не должен терять последнее движение предыдущего
  const sendCol = Array.from({ length: FIRE2_LEVELS }, () => debounced((lv, r, g, b) => editEffect({ lv, r, g, b }), 60));
  ['effR', 'effG', 'effB'].forEach((id, c) => {
    const el = $(id);
    trackBusy(el);
    el.addEventListener('input', () => {
      fire2Cols[fire2Level][c] = Number(el.value);
      $(id + '-val').textContent = el.value;
      fire2Draw();
      const col = fire2Cols[fire2Level];
      sendCol[fire2Level](fire2Level, col[0], col[1], col[2]);
    });
  });
}

// ---------- вкладка «Сцены» ----------
let sceneData = { max: 8, scenes: [] };
const SCENE_NAME_BYTES = 16;   // как в прошивке: в UTF-8 русская буква занимает 2 байта

// обрезать строку до n байт UTF-8 по границе символа
function fitBytes(str, n) {
  const enc = new TextEncoder();
  let out = '';
  let used = 0;
  for (const ch of str) {
    const len = enc.encode(ch).length;
    if (used + len > n) break;
    out += ch;
    used += len;
  }
  return out;
}

function renderScenes() {
  const list = $('sceneList');
  list.textContent = '';
  for (const s of sceneData.scenes) {
    const li = document.createElement('li');
    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = s.name + (s.map ? ' · схема ' + s.map : '');
    const apply = document.createElement('button');
    apply.className = 'apply';
    apply.textContent = 'Применить';
    apply.dataset.act = 'load';
    apply.addEventListener('click', () => sceneLoad(s));
    const over = document.createElement('button');
    over.textContent = 'Перезаписать';
    over.dataset.act = 'overwrite';
    over.addEventListener('click', () => sceneSave(s.i, s.name));
    const del = document.createElement('button');
    del.textContent = 'Удалить';
    del.dataset.act = 'delete';
    del.addEventListener('click', () => sceneDelete(s));
    li.append(name, apply, over, del);
    list.appendChild(li);
  }
  $('sceneEmpty').hidden = sceneData.scenes.length > 0;
}

async function loadScenes() {
  try {
    sceneData = await api.get('/api/scenes');
    renderScenes();
  } catch (e) {
    toast(e.message);
  }
}

async function sceneSave(slot, name) {
  try {
    sceneData = await api.post('/api/scenes/save', { slot, name, map: $('sceneMap').checked ? 1 : 0 });
    renderScenes();
    toast('Сохранено: ' + name, true);
  } catch (e) {
    toast(e.message);
  }
}

async function sceneLoad(s) {
  try {
    st = await api.post('/api/scenes/load', { slot: s.i });
    render(true);
    if (s.map) {
      // сцена вернула и схему светодиодов: карта на странице больше не та (правки, не записанные на гирлянду, отбрасываются)
      coordsClearUnder();
      coordsZoomFit();
      await coordsLoad();
      spiralInfo();
      hexInfo();
    }
    toast('Применена сцена «' + s.name + '»' + (s.map ? ' (схема: ' + s.map + ' светодиодов)' : ''), true);
  } catch (e) {
    toast(e.message);
  }
}

async function sceneDelete(s) {
  if (!confirm('Удалить сцену «' + s.name + '»?')) return;
  try {
    sceneData = await api.post('/api/scenes/delete', { slot: s.i });
    renderScenes();
  } catch (e) {
    toast(e.message);
  }
}

function initScenes() {
  const input = $('sceneName');
  input.addEventListener('input', () => {
    const fit = fitBytes(input.value, SCENE_NAME_BYTES);
    if (fit !== input.value) input.value = fit;
  });
  $('sceneSave').addEventListener('click', () => {
    const name = input.value.trim();
    if (!name) { toast('Введите название сцены'); return; }
    const used = new Set(sceneData.scenes.map((s) => s.i));
    let slot = -1;
    for (let i = 0; i < sceneData.max; i++) if (!used.has(i)) { slot = i; break; }
    if (slot < 0) { toast('Нет свободных мест: удалите ненужную сцену'); return; }
    sceneSave(slot, name).then(() => { input.value = ''; });
  });
  document.addEventListener('gt-tab', (e) => { if (e.detail === 'scenes') loadScenes(); });
}

// ---------- геометрия карты координат (чистые функции, проверяются в web/test) ----------
function hexToPoints(hex) {
  const pts = [];
  for (let i = 0; i + 3 < hex.length; i += 4) {
    pts.push({ x: parseInt(hex.substr(i, 2), 16), y: parseInt(hex.substr(i + 2, 2), 16) });
  }
  return pts;
}

function pointsToHex(pts) {
  let s = '';
  for (const p of pts) s += (p.x < 16 ? '0' : '') + p.x.toString(16) + (p.y < 16 ? '0' : '') + p.y.toString(16);
  return s;
}

const clamp255 = (v) => Math.max(0, Math.min(255, Math.round(v)));

// масштаб и сдвиг, при которых вся карта помещается в w x h с отступом pad; пропорции сохраняются
function fitView(pts, w, h, pad) {
  let minX = 255, maxX = 0, minY = 255, maxY = 0;
  for (const p of pts) {
    minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x);
    minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y);
  }
  if (!pts.length) { minX = minY = 0; maxX = maxY = 1; }
  const rx = Math.max(maxX - minX, 1);
  const ry = Math.max(maxY - minY, 1);
  const s = Math.max(0.01, Math.min((w - 2 * pad) / rx, (h - 2 * pad) / ry));
  return { s, ox: (w - rx * s) / 2 - minX * s, oy: (h - ry * s) / 2 - minY * s };
}

// Масштаб карты. base - вид, вписывающий всю карту (fitView), zoom - во сколько раз ближе, pan - сдвиг в пикселях;
// увеличение идёт вокруг центра холста (w x h). Чистые функции: проверяются в web/test.
const ZOOM_MIN = 1;
const ZOOM_MAX = 20;

function zoomedView(base, zoom, pan, w, h) {
  const cx = w / 2;
  const cy = h / 2;
  return { s: base.s * zoom, ox: (base.ox - cx) * zoom + cx + pan.x, oy: (base.oy - cy) * zoom + cy + pan.y };
}

// сдвиг не уводит карту за пределы кадра дальше, чем на её размер при данном масштабе
function clampPan(zoom, pan, w, h) {
  const lx = (w * zoom) / 2;
  const ly = (h * zoom) / 2;
  return { x: Math.max(-lx, Math.min(lx, pan.x)), y: Math.max(-ly, Math.min(ly, pan.y)) };
}

// новое {zoom, pan} после увеличения в factor раз вокруг точки p экрана: точка карты под p остаётся на месте.
// Масштаб 1 - вся карта по центру (сдвиг сбрасывается).
function zoomAround(zoom, pan, factor, p, w, h) {
  const nz = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, zoom * factor));
  if (nz <= ZOOM_MIN + 1e-9) return { zoom: ZOOM_MIN, pan: { x: 0, y: 0 } };
  const k = nz / zoom;
  const cx = w / 2;
  const cy = h / 2;
  return { zoom: nz, pan: clampPan(nz, { x: p.x - cx - (p.x - cx - pan.x) * k, y: p.y - cy - (p.y - cy - pan.y) * k }, w, h) };
}

const toScreen = (p, v) => ({ x: v.ox + p.x * v.s, y: v.oy + p.y * v.s });
const toWorld = (sx, sy, v) => ({ x: clamp255((sx - v.ox) / v.s), y: clamp255((sy - v.oy) / v.s) });

// ближайшая к экранной точке карта-точка в пределах maxDist пикселей; при равенстве - с меньшим номером
function nearestPoint(pts, sx, sy, v, maxDist) {
  let best = -1;
  let bestD = maxDist * maxDist;
  for (let i = 0; i < pts.length; i++) {
    const q = toScreen(pts[i], v);
    const d = (q.x - sx) * (q.x - sx) + (q.y - sy) * (q.y - sy);
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

// подозрительные точки: далеко от обоих соседей по ленте (для крайних - от единственного соседа).
// Порог - k медианных шагов; один длинный шаг между секциями гирлянды точку подозрительной не делает.
function suspiciousPoints(pts, k) {
  const n = pts.length;
  if (n < 4) return [];
  const steps = [];
  for (let i = 0; i + 1 < n; i++) steps.push(Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y));
  const sorted = steps.slice().sort((a, b) => a - b);
  const med = sorted[Math.floor(sorted.length / 2)];
  if (!(med > 0)) return [];
  const lim = (k || 4) * med;
  const out = [];
  for (let i = 0; i < n; i++) {
    const prev = i > 0 ? steps[i - 1] : null;
    const next = i < n - 1 ? steps[i] : null;
    const bad = (prev === null || prev > lim) && (next === null || next > lim);
    if (bad) out.push(i);
  }
  return out;
}

// ---------- вкладка «Координаты» ----------
const coords = { zoom: 1, pan: { x: 0, y: 0 }, base: null, baseFrozen: null, pointers: new Map(), pinch: null, panning: null, pts: [], orig: [], sel: 0, dirty: false, drag: false, lit: false, view: null, sus: [], est: new Set(), under: [], underOnBoard: false };   // under - контур схемы «соты» (подложка, не записывается); underOnBoard - карта с этим контуром уже сохранена на гирлянде

// контур относится к конкретной карте: любая другая схема (спираль, калибровка, сцена) его убирает
function coordsClearUnder() {
  coords.under = [];
  coords.underOnBoard = false;
}

function coordsSetDirty(d) {
  coords.dirty = d;
  $('cSave').disabled = !d;
  $('cUndo').disabled = !d;
}

function coordsLedLight() {
  if (!coords.pts.length) return;
  coords.lit = true;
  api.post('/api/led', { i: coords.sel }).catch((e) => toast(e.message));
}
const coordsLedLightSoon = debounced(coordsLedLight, 120);

function coordsLedOff() {
  if (!coords.lit) return;
  coords.lit = false;
  api.post('/api/led', { off: 1 }).catch(() => {});
}

function coordsDraw() {
  const cv = $('cCanvas');
  const dpr = window.devicePixelRatio || 1;
  const w = cv.clientWidth;
  const h = cv.clientHeight;
  if (!w || !h) return;
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const g = cv.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  const pts = coords.pts;
  if (!pts.length) return;
  const cs = getComputedStyle(document.documentElement);
  const accent = cs.getPropertyValue('--accent').trim() || '#ffb020';
  const line = cs.getPropertyValue('--muted').trim() || '#888';
  const text = cs.getPropertyValue('--text').trim() || '#000';
  const under = coords.under.length && $('hxOutline').checked ? coords.under : [];
  // вид подгоняется и под контур схемы, если он показан (светодиоды лежат внутри него и от краёв отступают)
  const viewPts = under.length ? pts.concat(under.flatMap((e) => [{ x: e[0], y: e[1] }, { x: e[2], y: e[3] }])) : pts;
  // вид: вписывающий всю карту, затем масштаб и сдвиг; пока тянут точку, вписывающий вид не меняется (карта не уезжает из-под пальца)
  coords.base = coords.baseFrozen || fitView(viewPts, w, h, 28);
  coords.view = zoomedView(coords.base, coords.zoom, coords.pan, w, h);
  const v = coords.view;
  if (under.length) {
    g.strokeStyle = text;
    g.globalAlpha = 0.28;
    g.lineWidth = 1.5;
    g.beginPath();
    for (const e of under) {
      const a = toScreen({ x: e[0], y: e[1] }, v);
      const b = toScreen({ x: e[2], y: e[3] }, v);
      g.moveTo(a.x, a.y);
      g.lineTo(b.x, b.y);
    }
    g.stroke();
    g.globalAlpha = 1;
  }
  if ($('cLine').checked) {
    g.strokeStyle = line;
    g.globalAlpha = 0.45;
    g.lineWidth = 1;
    g.beginPath();
    pts.forEach((p, i) => { const q = toScreen(p, v); if (i) g.lineTo(q.x, q.y); else g.moveTo(q.x, q.y); });
    g.stroke();
    g.globalAlpha = 1;
  }
  const r = (pts.length > 300 ? 2.5 : 4) * Math.min(2.5, 1 + (coords.zoom - 1) * 0.3);   // при увеличении точки крупнее
  pts.forEach((p, i) => {
    const q = toScreen(p, v);
    g.fillStyle = 'hsl(' + Math.round((i / pts.length) * 300) + ',80%,55%)';
    g.beginPath();
    g.arc(q.x, q.y, r, 0, 7);
    g.fill();
  });
  g.lineWidth = 2;
  g.strokeStyle = '#ff9f1a';   // оценённые по соседям (светодиод не нашли на видео): пунктирное кольцо
  g.setLineDash([3, 3]);
  for (const i of coords.est) {
    if (!pts[i]) continue;
    const q = toScreen(pts[i], v);
    g.beginPath();
    g.arc(q.x, q.y, r + 5, 0, 7);
    g.stroke();
  }
  g.setLineDash([]);
  g.strokeStyle = '#ff4d4d';
  for (const i of coords.sus) {
    const q = toScreen(pts[i], v);
    g.beginPath();
    g.arc(q.x, q.y, r + 5, 0, 7);
    g.stroke();
  }
  const sp = pts[coords.sel];
  if (sp) {
    const q = toScreen(sp, v);
    g.strokeStyle = accent;
    g.lineWidth = 3;
    g.beginPath();
    g.arc(q.x, q.y, r + 6, 0, 7);
    g.stroke();
    g.fillStyle = text;
    g.font = '12px system-ui, sans-serif';
    g.fillText(String(coords.sel + 1), q.x + r + 8, q.y - r - 6);
  }
}

function coordsRefreshInfo() {
  coords.sus = suspiciousPoints(coords.pts, 4);
  const n = coords.pts.length;
  const empty = n > 0 && coords.pts.every((p) => p.x === coords.pts[0].x && p.y === coords.pts[0].y);
  $('cEmpty').hidden = !empty;
  $('cInfo').textContent = 'Светодиодов: ' + n + ', подозрительных: ' + coords.sus.length +
    (coords.est.size ? ', оценено по соседям: ' + coords.est.size : '') + (coords.dirty ? ' · есть несохранённые изменения' : '');
}

function coordsSync() {
  const p = coords.pts[coords.sel];
  if (p) {
    if (document.activeElement !== $('cX')) $('cX').value = p.x;
    if (document.activeElement !== $('cY')) $('cY').value = p.y;
    if (document.activeElement !== $('cIdx')) $('cIdx').value = coords.sel + 1;
  }
  $('cIdx').max = Math.max(1, coords.pts.length);
  coordsRefreshInfo();
  coordsDraw();
}

function coordsSelect(i) {
  const n = coords.pts.length;
  if (!n) return;
  coords.sel = ((i % n) + n) % n;
  coordsReveal();
  coordsSync();
  coordsLedLightSoon();
}

// при увеличении выбранный светодиод не должен оказаться за краем кадра: карта сдвигается так, чтобы он был виден
function coordsReveal() {
  const cv = $('cCanvas');
  const p = coords.pts[coords.sel];
  if (!p || !coords.view || coords.zoom <= ZOOM_MIN) return;
  const w = cv.clientWidth;
  const h = cv.clientHeight;
  const q = toScreen(p, coords.view);
  const m = 30;
  if (q.x >= m && q.x <= w - m && q.y >= m && q.y <= h - m) return;
  coords.pan = clampPan(coords.zoom, { x: coords.pan.x + w / 2 - q.x, y: coords.pan.y + h / 2 - q.y }, w, h);
}

// масштаб вокруг точки экрана (по умолчанию центра холста) в factor раз
function coordsZoom(factor, at) {
  const cv = $('cCanvas');
  const w = cv.clientWidth;
  const h = cv.clientHeight;
  const r = zoomAround(coords.zoom, coords.pan, factor, at || { x: w / 2, y: h / 2 }, w, h);
  coords.zoom = r.zoom;
  coords.pan = r.pan;
  coordsZoomLabel();
  coordsDraw();
}

// вписать карту целиком (после построения новой схемы, по кнопке)
function coordsZoomFit() {
  coords.zoom = ZOOM_MIN;
  coords.pan = { x: 0, y: 0 };
  coordsZoomLabel();
  coordsDraw();
}

function coordsZoomLabel() {
  const el = $('cZoomVal');
  if (el) el.textContent = '×' + (Math.round(coords.zoom * 10) / 10);
}

async function coordsLoad() {
  try {
    const d = await api.get('/api/xy');
    coords.pts = hexToPoints(d.xy);
    coords.orig = coords.pts.map((p) => ({ x: p.x, y: p.y }));
    coords.sel = Math.min(coords.sel, Math.max(0, coords.pts.length - 1));
    coords.est = new Set();
    coordsSetDirty(false);
    coordsSync();
  } catch (e) {
    toast(e.message);
  }
}

async function coordsSave() {
  try {
    const r = await api.post('/api/xy', { n: coords.pts.length }, pointsToHex(coords.pts));
    coords.orig = coords.pts.map((p) => ({ x: p.x, y: p.y }));
    if (coords.under.length) coords.underOnBoard = true;
    coordsSetDirty(false);
    coordsRefreshInfo();
    toast('Сохранено: размер карты ' + r.w + ' × ' + r.h, true);
  } catch (e) {
    toast(e.message);
    if (e.code === 'count') coordsLoad();
  }
}

function coordsEdit(i, x, y) {
  coords.est.delete(i);   // поправленная вручную точка больше не «оценка»
  coords.pts[i] = { x: clamp255(x), y: clamp255(y) };
  coordsSetDirty(true);
  coordsSync();
}

// результат калибровки по видео: точки попадают в редактор как несохранённая правка
async function coordsApplyResult(pts, est) {
  await coordsLoad();   // orig - то, что сейчас на гирлянде
  if (pts.length !== coords.orig.length) throw new Error('Число светодиодов изменилось, повтори калибровку');
  coordsClearUnder();   // другая схема: контур «сот» больше не подходит
  coordsZoomFit();
  coords.pts = pts.map((p) => ({ x: p.x, y: p.y }));
  coords.est = new Set(est);
  coords.sel = 0;
  coordsSetDirty(true);
  coordsSync();
}

// ---------- схема «спираль» (цилиндр, конус) ----------
// Лента намотана рядами снизу вверх: counts - светодиодов в рядах N1, N2, ... (нижний первый), остаток до total образует
// последний (верхний) ряд. Ряды идут слева направо, каждый центрируется; шаг между светодиодами одинаковый во всех рядах
// (у конуса верхние ряды уже), gap - расстояние между рядами в шагах светодиода (в интерфейсе по умолчанию 3: так точки
// разных рядов не сливаются и их удобно править вручную). Ось Y направлена вниз, как в кадре камеры:
// нижний ряд получает наибольший Y (так считает эффект «огонь»). Возвращает {pts, rows} или {error}.
function spiralLayout(counts, total, gap) {
  if (!Number.isInteger(total) || total < 1) return { error: 'Нет светодиодов' };
  if (!Number.isFinite(gap) || gap < 0.2 || gap > 10) return { error: 'Расстояние между рядами: от 0,2 до 10 шагов' };
  let used = 0;
  for (let i = 0; i < counts.length; i++) {
    const n = counts[i];
    if (!Number.isInteger(n) || n < 1) return { error: 'N' + (i + 1) + ': целое число от 1' };
    used += n;
  }
  if (used > total) return { error: 'Сумма рядов (' + used + ') больше числа светодиодов S (' + total + ')' };
  const rows = counts.slice();
  if (used < total || rows.length === 0) rows.push(total - used);   // остаток - верхний ряд
  const widthU = Math.max(...rows.map((n) => n - 1));   // ширина самого длинного ряда в шагах светодиода
  const heightU = (rows.length - 1) * gap;
  const AREA = 240;                                      // поле 0..255 с отступом около 7 по краям
  let s = Infinity;
  if (widthU > 0) s = Math.min(s, AREA / widthU);
  if (heightU > 0) s = Math.min(s, AREA / heightU);
  if (!Number.isFinite(s)) s = 0;                        // единственный светодиод
  const pts = [];
  rows.forEach((n, r) => {
    const yu = (rows.length - 1 - r) * gap;              // верхний ряд - 0, нижний - самый большой Y
    for (let i = 0; i < n; i++) {
      pts.push({
        x: clamp255(Math.round(127.5 + (i - (n - 1) / 2) * s)),
        y: clamp255(Math.round(127.5 + (yu - heightU / 2) * s)),
      });
    }
  });
  return { pts, rows };
}

// Карта, перевёрнутая по вертикали: Y зеркалится внутри занятой высоты (min + max − y), X не меняется.
// Нужна, если на ленте физически первый светодиод внизу, а схема построена наоборот (или камера видела ленту вверх ногами).
function flipVertical(pts) {
  if (!pts.length) return [];
  let lo = 255, hi = 0;
  for (const p of pts) {
    if (p.y < lo) lo = p.y;
    if (p.y > hi) hi = p.y;
  }
  return pts.map((p) => ({ x: p.x, y: clamp255(lo + hi - p.y) }));
}

const SPIRAL_KEY = 'gt-spiral-v2';   // v2: расстояние между рядами по умолчанию 3; значение из прежней версии (1) не восстанавливается
const spiral = { counts: [''] };   // значения полей N1, N2... как введены (строки)

function spiralRead() {
  const counts = spiral.counts.map((v) => (v === '' ? NaN : Number(v)));
  return { counts, gap: Number($('spGap').value.replace(',', '.')) };
}

function spiralRender() {
  const box = $('spRows');
  box.textContent = '';
  spiral.counts.forEach((v, i) => {
    const lab = document.createElement('label');
    lab.append('N' + (i + 1));
    const inp = document.createElement('input');
    inp.type = 'number';
    inp.min = '1';
    inp.inputMode = 'numeric';
    inp.value = v;
    inp.id = 'spN' + (i + 1);
    inp.addEventListener('input', () => { spiral.counts[i] = inp.value; spiralInfo(); });
    lab.append(inp);
    box.append(lab);
  });
  $('spDel').disabled = spiral.counts.length <= 1;
  spiralInfo();
}

// пояснение под полями: сколько светодиодов в последнем ряду и можно ли строить
function spiralInfo() {
  const total = st ? st.ledAm : coords.orig.length;   // актуальное число светодиодов (опрос раз в 2 с), а не загруженной ранее карты
  const { counts, gap } = spiralRead();
  const r = spiralLayout(counts, total, gap);
  const sum = counts.reduce((a, b) => a + (Number.isFinite(b) ? b : 0), 0);
  $('spBuild').disabled = !!r.error;
  if (!total) {
    $('spInfo').textContent = 'Число светодиодов ещё не получено с гирлянды.';
  } else if (r.error) {
    $('spInfo').textContent = 'S = ' + total + ', в указанных рядах: ' + sum + '. ' + r.error + '.';
  } else {
    const last = r.rows[r.rows.length - 1];
    $('spInfo').textContent = 'S = ' + total + '. Рядов: ' + r.rows.length + ' (' + r.rows.join(' + ') + '), последний ряд — остаток: ' + last + '.';
  }
}

// ---------- схема «соты» (файл экспорта из Hex Router Designer) ----------
const hex = { map: null, layout: null, error: '' };

// пояснение под выбором файла и состояние кнопки
function hexInfo() {
  const info = $('hxInfo');
  const build = $('hxBuild');
  build.disabled = true;
  if (hex.error) { info.textContent = hex.error; return; }
  if (!hex.map) { info.textContent = 'Файл не выбран.'; return; }
  const m = hex.map;
  let text = (m.name ? '«' + m.name + '»: ' : '') + m.count + ' светодиодов, габариты ' + m.widthMm + ' × ' + m.heightMm + ' мм, рёбер контура: ' + m.outline.length + '.';
  for (const w of hex.layout.warnings) text += ' ' + w;
  if (!st) {
    text += ' Связь с гирляндой ещё не установлена.';
  } else if (m.count > st.ledMax) {
    text += ' ВНИМАНИЕ: схеме нужно ' + m.count + ' светодиодов, а прошивка поддерживает не больше ' + st.ledMax + '. Построить такую схему нельзя.';
  } else if (st.ledAm !== m.count) {
    text += ' На гирлянде сейчас ' + st.ledAm + ': при построении число светодиодов будет изменено на ' + m.count + ' автоматически.';
    build.disabled = false;
  } else {
    text += ' Число светодиодов совпадает с гирляндой.';
    build.disabled = false;
  }
  info.textContent = text;
}

async function hexLoadFile(file) {
  hex.map = null;
  hex.layout = null;
  hex.error = '';
  coordsClearUnder();
  if (file) {
    let text = '';
    try { text = await file.text(); } catch (e) { hex.error = 'Не удалось прочитать файл.'; }
    if (!hex.error) {
      const r = parseHexMap(text, st ? st.ledMax : 0);
      if (r.error) hex.error = r.error;
      else { hex.map = r.map; hex.layout = layoutHexMap(r.map); }
    }
  }
  if (hex.error) toast(hex.error);
  hexInfo();
  if (hex.map && st && hex.map.count > st.ledMax) toast('Схема требует ' + hex.map.count + ' светодиодов: больше предела прошивки (' + st.ledMax + ')');
}

async function hexBuild() {
  if (!hex.map || !st) return;
  const need = hex.map.count;
  if (need > st.ledMax) { toast('Схема требует ' + need + ' светодиодов: больше предела прошивки (' + st.ledMax + ')'); hexInfo(); return; }
  const was = st.ledAm;
  if (was !== need) {
    await setParams({ ledAm: need });   // число светодиодов берём из файла (ошибку покажет setParams)
    if (!st || st.ledAm !== need) { hexInfo(); return; }
  }
  await coordsLoad();   // число светодиодов берём с гирлянды: сохранение требует то же число
  if (coords.orig.length !== need) { toast('Число светодиодов на гирлянде (' + coords.orig.length + ') не совпадает со схемой (' + need + ').'); hexInfo(); return; }
  coordsZoomFit();   // новая схема показывается целиком
  coords.pts = hex.layout.pts.map((p) => ({ x: p.x, y: p.y }));
  coords.under = hex.layout.outline;
  coords.underOnBoard = false;
  coords.est = new Set();
  coords.sel = 0;
  coordsSetDirty(true);
  coordsSync();
  coordsLedLight();
  hexInfo();
  toast('Схема построена: ' + need + ' светодиодов' + (was !== need ? ' (число светодиодов на гирлянде изменено: ' + was + ' → ' + need + ')' : '') + '. Проверь карту и нажми «Сохранить на гирлянду»', true);
}

function initHex() {
  $('hxFile').addEventListener('change', () => hexLoadFile($('hxFile').files[0] || null));
  $('hxBuild').addEventListener('click', hexBuild);
  $('hxOutline').addEventListener('change', coordsDraw);
  hexInfo();
}

async function spiralBuild() {
  await coordsLoad();   // число светодиодов берём с гирлянды: сохранение требует то же число
  const { counts, gap } = spiralRead();
  const r = spiralLayout(counts, coords.orig.length, gap);
  if (r.error) { toast(r.error); spiralInfo(); return; }
  try { localStorage.setItem(SPIRAL_KEY, JSON.stringify({ counts: spiral.counts, gap: $('spGap').value })); } catch (e) { /* не страшно */ }
  coordsClearUnder();   // другая схема: контур «сот» больше не подходит
  coordsZoomFit();
  coords.pts = r.pts.map((p) => ({ x: p.x, y: p.y }));
  coords.est = new Set();
  coords.sel = 0;
  coordsSetDirty(true);
  coordsSync();
  coordsLedLight();
  toast('Схема построена: рядов ' + r.rows.length + '. Проверь карту и нажми «Сохранить на гирлянду»', true);
}

function spiralFlip() {
  if (!coords.pts.length) { toast('Карта пока пустая'); return; }
  let lo = 255, hi = 0;
  for (const p of coords.pts) { lo = Math.min(lo, p.y); hi = Math.max(hi, p.y); }
  if (coords.under.length) coords.under = flipOutline(coords.under, lo, hi);   // контур зеркалится вместе с точками
  coords.pts = flipVertical(coords.pts);
  coords.est = new Set(coords.est);
  coordsSetDirty(true);
  coordsSync();
  coordsLedLight();
  toast('Карта перевёрнута по вертикали. Проверь её и нажми «Сохранить на гирлянду»', true);
}

function initSpiral() {
  try {
    const v2 = JSON.parse(localStorage.getItem(SPIRAL_KEY) || 'null');
    const old = v2 ? null : JSON.parse(localStorage.getItem('gt-spiral') || 'null');   // прежняя версия: берём только ряды
    const saved = v2 || old;
    if (saved && Array.isArray(saved.counts) && saved.counts.length >= 1 && saved.counts.length <= 60) {
      spiral.counts = saved.counts.map((v) => String(v));
      if (v2 && Number(v2.gap) >= 0.2 && Number(v2.gap) <= 10) $('spGap').value = v2.gap;
    }
  } catch (e) { /* без сохранённого просто пустые поля */ }
  $('spAdd').addEventListener('click', () => {
    if (spiral.counts.length >= 60) return;
    const prev = spiral.counts[spiral.counts.length - 1];
    spiral.counts.push(prev);   // новый ряд начинается со значения предыдущего: правят только отличия
    spiralRender();
  });
  $('spDel').addEventListener('click', () => {
    if (spiral.counts.length > 1) spiral.counts.pop();
    spiralRender();
  });
  $('spGap').addEventListener('input', spiralInfo);
  $('spBuild').addEventListener('click', spiralBuild);
  $('spFlip').addEventListener('click', spiralFlip);
  document.addEventListener('gt-tab', (e) => { if (e.detail === 'coords') setTimeout(spiralInfo, 300); });
  spiralRender();
}

function initCoords() {
  const cv = $('cCanvas');
  const pos = (e) => { const r = cv.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  // жесты: колесо мыши и два пальца - масштаб, перетаскивание пустого места при увеличении - сдвиг, перетаскивание точки - правка
  const two = () => {
    const [a, b] = Array.from(coords.pointers.values());
    return { dist: Math.hypot(a.x - b.x, a.y - b.y), mid: { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 } };
  };
  cv.addEventListener('wheel', (e) => {
    if (!coords.pts.length) return;
    e.preventDefault();   // страница не прокручивается, пока крутят колесо над картой
    const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 300 : 1;
    coordsZoom(Math.exp(-e.deltaY * unit * (e.ctrlKey ? 0.01 : 0.0015)), pos(e));
  }, { passive: false });
  cv.addEventListener('pointerdown', (e) => {
    if (!coords.view) return;
    const p = pos(e);
    coords.pointers.set(e.pointerId, p);
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* синтетическое событие */ }
    if (coords.pointers.size === 2) {   // второй палец: правка точки и сдвиг отменяются, начинается масштабирование
      coords.drag = false;
      coords.panning = null;
      coords.baseFrozen = null;
      coords.pinch = two();
      return;
    }
    if (coords.pointers.size > 2) return;
    const i = nearestPoint(coords.pts, p.x, p.y, coords.view, 24);
    if (i >= 0) {
      coords.baseFrozen = coords.base;
      coords.drag = true;
      coordsSelect(i);
    } else if (coords.zoom > ZOOM_MIN) {
      coords.panning = { x: p.x, y: p.y, pan: { x: coords.pan.x, y: coords.pan.y } };
    }
  });
  cv.addEventListener('pointermove', (e) => {
    const p = pos(e);
    if (coords.pointers.has(e.pointerId)) coords.pointers.set(e.pointerId, p);
    if (coords.pointers.size === 2 && coords.pinch) {
      const now = two();
      if (coords.pinch.dist > 0 && now.dist > 0) {
        const w = cv.clientWidth;
        const h = cv.clientHeight;
        const r = zoomAround(coords.zoom, coords.pan, now.dist / coords.pinch.dist, now.mid, w, h);
        // середина между пальцами ещё и тянет карту за собой
        coords.zoom = r.zoom;
        coords.pan = r.zoom > ZOOM_MIN ? clampPan(r.zoom, { x: r.pan.x + now.mid.x - coords.pinch.mid.x, y: r.pan.y + now.mid.y - coords.pinch.mid.y }, w, h) : r.pan;
        coordsZoomLabel();
        coordsDraw();
      }
      coords.pinch = now;
      return;
    }
    if (coords.drag) {
      const w = toWorld(p.x, p.y, coords.view);
      coordsEdit(coords.sel, w.x, w.y);
    } else if (coords.panning) {
      coords.pan = clampPan(coords.zoom, { x: coords.panning.pan.x + p.x - coords.panning.x, y: coords.panning.pan.y + p.y - coords.panning.y }, cv.clientWidth, cv.clientHeight);
      coordsDraw();
    }
  });
  const end = (e) => {
    coords.pointers.delete(e.pointerId);
    if (coords.pointers.size < 2) coords.pinch = null;
    if (coords.pointers.size === 0) {
      coords.drag = false;
      coords.panning = null;
      coords.baseFrozen = null;
    }
  };
  cv.addEventListener('pointerup', end);
  cv.addEventListener('pointercancel', end);
  $('cZoomIn').addEventListener('click', () => coordsZoom(1.5));
  $('cZoomOut').addEventListener('click', () => coordsZoom(1 / 1.5));
  $('cZoomFit').addEventListener('click', coordsZoomFit);
  $('cPrev').addEventListener('click', () => coordsSelect(coords.sel - 1));
  $('cNext').addEventListener('click', () => coordsSelect(coords.sel + 1));
  $('cIdx').addEventListener('change', () => {
    const v = parseInt($('cIdx').value, 10);
    if (!isNaN(v)) coordsSelect(Math.max(1, Math.min(v, coords.pts.length)) - 1);
  });
  for (const [id, axis] of [['cX', 'x'], ['cY', 'y']]) {
    $(id).addEventListener('change', () => {
      const v = parseInt($(id).value, 10);
      const p = coords.pts[coords.sel];
      if (isNaN(v) || !p) { coordsSync(); return; }
      coordsEdit(coords.sel, axis === 'x' ? v : p.x, axis === 'y' ? v : p.y);
    });
  }
  $('cLine').addEventListener('change', coordsDraw);
  $('cSave').addEventListener('click', coordsSave);
  $('cUndo').addEventListener('click', () => {
    if (!coords.underOnBoard) coordsClearUnder();   // на гирлянде другая карта: контур несохранённой схемы к ней не относится
    coords.pts = coords.orig.map((p) => ({ x: p.x, y: p.y }));
    coords.est = new Set();
    coordsSetDirty(false);
    coordsSync();
  });
  window.addEventListener('resize', coordsDraw);
  document.addEventListener('visibilitychange', () => { if (document.hidden) coordsLedOff(); });
  document.addEventListener('gt-tab', async (e) => {
    if (e.detail === 'coords') {
      if (!coords.dirty) await coordsLoad(); else coordsSync();
      coordsLedLight();
    } else {
      coordsLedOff();
    }
  });
}

// ---------- вкладка «Калибровка» (по видео) ----------
const vc = { st: null, timer: 0, abort: null, analyzing: false };

const PHASE_TEXT = {
  idle: 'Серия не запущена.',
  wait: 'Включай запись видео: серия начнётся через',
  dark: 'Серия начинается: тёмная пауза…',
  flash: 'Вспышка-маркер (нужна для синхронизации).',
  gap: 'Пауза перед серией…',
  seq: 'Идёт серия: светодиод',
  end: 'Серия закончена, ещё секунду снимай…',
};

function vcTotalSec(delay, step, n, p) {
  return delay + (p.dark + p.flash + p.gap + p.end) / 1000 + (n * step) / 1000;
}

function vcRenderEstimate() {
  const p = vc.st;
  if (!p) return;
  const total = vcTotalSec(Number($('vDelay').value) || 0, Number($('vStep').value) || p.step, p.n, p);
  $('vEstimate').textContent = 'Светодиодов: ' + p.n + '. Вся серия займёт около ' + Math.round(total) + ' с (видео будет такой же длины).';
}

function vcRenderStatus() {
  const p = vc.st;
  if (!p) return;
  let text = PHASE_TEXT[p.phase] || '';
  let frac = 0;
  if (p.run) {
    if (p.phase === 'wait') text += ' ' + Math.max(0, Math.ceil((p.delay * 1000 - p.e) / 1000)) + ' с';
    if (p.phase === 'seq') text += ' ' + (p.i + 1) + ' из ' + p.n;
    frac = Math.min(1, p.e / p.total);
  }
  $('vStatus').textContent = text;
  $('vBar').style.width = Math.round(frac * 100) + '%';
  $('vStart').disabled = !!p.run;
  $('vStop').disabled = !p.run;
}

async function vcRefresh() {
  try {
    vc.st = await api.get('/api/vcalib');
    if (!vc.stepEdited && !vc.st.run) $('vStep').value = vc.st.step;
    vcRenderStatus();
    vcRenderEstimate();
    clearTimeout(vc.timer);
    if (vc.st.run || document.getElementById('tab-vcalib').classList.contains('active')) vc.timer = setTimeout(vcRefresh, vc.st.run ? 400 : 2000);
  } catch (e) {
    clearTimeout(vc.timer);
    vc.timer = setTimeout(vcRefresh, 3000);
  }
}

// кадр видео в нужный момент: перемотка и копирование в маленький холст (серый).
// После события seeked браузер может ещё не вывести новый кадр - тогда drawImage рисует прежний (из-за этого
// терялись отдельные светодиоды). Поэтому ждём показа кадра (requestVideoFrameCallback) и дополнительно
// перепроверяем: кадр, побитово совпавший с предыдущим при другом времени, считается устаревшим и снимается заново.
function videoSampler(video, width) {
  const ratio = video.videoHeight / video.videoWidth;
  const w = width;
  const h = Math.max(2, Math.round(width * ratio));
  const cv = document.createElement('canvas');
  cv.width = w;
  cv.height = h;
  const g = cv.getContext('2d', { willReadFrequently: true });
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  let last = null;
  let lastT = -1;

  function grab() {
    g.drawImage(video, 0, 0, w, h);
    const d = g.getImageData(0, 0, w, h).data;
    const gray = new Float32Array(w * h);
    for (let i = 0, j = 0; i < gray.length; i++, j += 4) gray[i] = 0.299 * d[j] + 0.587 * d[j + 1] + 0.114 * d[j + 2];
    return gray;
  }
  const same = (a, b) => { for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; };

  return async (t) => {
    const target = Math.max(0, Math.min(video.duration - 0.001, t));
    if (Math.abs(video.currentTime - target) > 1e-4) {
      await new Promise((resolve) => {
        const done = () => { video.removeEventListener('seeked', done); clearTimeout(to); resolve(); };
        const to = setTimeout(done, 4000);   // некоторые браузеры не присылают seeked
        video.addEventListener('seeked', done);
        video.currentTime = target;
      });
      if (video.requestVideoFrameCallback) {
        await new Promise((resolve) => {
          const to = setTimeout(resolve, 250);
          video.requestVideoFrameCallback(() => { clearTimeout(to); resolve(); });
        });
      } else {
        await sleep(40);
      }
    }
    let gray = grab();
    for (let retry = 0; retry < 3 && last && Math.abs(target - lastT) > 0.05 && same(gray, last); retry++) {
      await sleep(60);   // кадр не обновился: даём браузеру вывести его
      gray = grab();
    }
    last = gray;
    lastT = target;
    return { w, h, g: gray };
  };
}

async function openVideo(file) {
  const video = document.createElement('video');
  video.muted = true;
  video.playsInline = true;
  video.preload = 'auto';
  const url = URL.createObjectURL(file);
  video.src = url;
  await new Promise((resolve, reject) => {
    video.onloadedmetadata = resolve;
    video.onerror = () => reject(new Error('Не удалось открыть видео: формат не поддерживается этим браузером'));
  });
  if (video.readyState < 2) {   // ждём первый кадр: иначе перемотка в 0 вернёт пустой (чёрный) кадр
    await new Promise((resolve) => {
      const to = setTimeout(resolve, 5000);
      video.onloadeddata = () => { clearTimeout(to); resolve(); };
    });
  }
  if (!isFinite(video.duration)) {   // у некоторых роликов (запись из браузера) длина неизвестна до перемотки в конец
    await new Promise((resolve) => {
      video.ontimeupdate = () => { video.ontimeupdate = null; resolve(); };
      video.currentTime = 1e7;
    });
    video.currentTime = 0;
  }
  if (!video.videoWidth) throw new Error('В файле нет видео');
  return { video, url };
}

async function vcAnalyze() {
  const file = $('vFile').files[0];
  if (!file || vc.analyzing) return;
  const p = vc.st;
  const step = Number($('vStep').value);
  if (!p || !(step >= 60 && step <= 1000)) { toast('Укажи шаг серии от 60 до 1000 мс'); return; }
  vc.analyzing = true;
  vc.abort = { aborted: false };
  $('vAnalyze').disabled = true;
  $('vCancel').hidden = false;
  $('vResult').textContent = 'Открываю видео…';
  $('vBar2').style.width = '0%';
  let opened = null;
  try {
    opened = await openVideo(file);
    const sample = videoSampler(opened.video, 320);
    const res = await analyzeVideo(sample, opened.video.duration, { n: p.n, step, flash: p.flash, gap: p.gap }, (stage, done, total) => {
      const base = stage === 'marker' ? 0 : 0.35;
      const span = stage === 'marker' ? 0.35 : 0.65;
      $('vBar2').style.width = Math.round((base + span * (done / total)) * 100) + '%';
      $('vResult').textContent = stage === 'marker' ? 'Ищу вспышку-маркер…' : 'Нахожу светодиоды: ' + done + ' из ' + total;
    }, vc.abort);
    const grid = toGridCoords(res.points, res.w);
    const est = [];
    res.points.forEach((q, i) => { if (q.est) est.push(i); });
    $('vBar2').style.width = '100%';
    $('vResult').textContent = 'Найдено ' + res.found + ' из ' + res.total + ' светодиодов' + (est.length ? ' (остальные оценены по соседям)' : '') + '. Проверь точки и сохрани.';
    await coordsApplyResult(grid, est);
    showTab('coords');
    toast('Видео разобрано: проверь точки и нажми «Сохранить»', true);
  } catch (e) {
    $('vResult').textContent = e.message;
    toast(e.message);
  } finally {
    if (opened) { URL.revokeObjectURL(opened.url); opened.video.removeAttribute('src'); opened.video.load(); }
    vc.analyzing = false;
    $('vCancel').hidden = true;
    $('vAnalyze').disabled = !$('vFile').files.length;
  }
}

function initVcalib() {
  $('vStart').addEventListener('click', async () => {
    try {
      vc.st = await api.post('/api/vcalib/start', { delay: $('vDelay').value, step: $('vStep').value });
      vcRenderStatus();
      vcRefresh();
    } catch (e) {
      toast(e.message === 'busy' ? 'Серия уже идёт' : e.message);
    }
  });
  $('vStop').addEventListener('click', async () => {
    try { vc.st = await api.post('/api/vcalib/stop'); vcRenderStatus(); } catch (e) { toast(e.message); }
  });
  for (const id of ['vDelay', 'vStep']) {
    $(id).addEventListener('input', () => { if (id === 'vStep') vc.stepEdited = true; vcRenderEstimate(); });
  }
  $('vFile').addEventListener('change', () => { $('vAnalyze').disabled = !$('vFile').files.length || vc.analyzing; });
  $('vAnalyze').addEventListener('click', vcAnalyze);
  $('vCancel').addEventListener('click', () => { if (vc.abort) vc.abort.aborted = true; });
  document.addEventListener('gt-tab', (e) => { if (e.detail === 'vcalib') vcRefresh(); });
}

// ---------- вкладка «Прошивка» (OTA) ----------
const ota = { st: null, timer: 0, busy: false };

// прошивка ESP8266 начинается с байта 0xE9; проверяем до загрузки, чтобы не гонять по сети заведомо неподходящий файл
async function otaCheckFile(file) {
  if (!file) return 'Выбери файл firmware.bin';
  if (file.size < 1024) return 'Файл слишком маленький для прошивки';
  if (file.size > 1048576) return 'Файл больше 1 МБ: для обновления по воздуху прошивка должна помещаться в раздел OTA';
  const first = new Uint8Array(await file.slice(0, 1).arrayBuffer());
  if (first[0] !== 0xE9) return 'Это не прошивка для ESP8266: файл должен начинаться с байта 0xE9. Нужен firmware.bin, а не .elf или .map';
  return '';
}

function otaRender() {
  guardRender();
  const s = ota.st;
  if (!s) return;
  const mm = Math.floor(s.left / 60);
  const ss = String(s.left % 60).padStart(2, '0');
  $('otaStatus').textContent = s.open ? 'Окно обновления открыто: осталось ' + mm + ':' + ss + '.' : 'Окно закрыто. Нажми кнопку на гирлянде три раза подряд.';
  $('otaGo').disabled = !(s.open && $('otaFile').files.length) || ota.busy;
}

async function otaRefresh() {
  try {
    ota.st = await api.get('/api/ota');
    otaRender();
  } catch (e) { /* связь могла пропасть из-за перезагрузки */ }
  clearTimeout(ota.timer);
  if (document.getElementById('tab-ota').classList.contains('active') && !ota.busy) ota.timer = setTimeout(otaRefresh, 1000);
}

function otaSend(file, onProgress) {
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();   // fetch не умеет сообщать о ходе загрузки
    x.open('POST', '/update');
    x.setRequestHeader('X-Requested-With', 'GyverTwink');
    x.upload.onprogress = (e) => { if (e.lengthComputable) onProgress(e.loaded / e.total); };
    x.onload = () => {
      let j = {};
      try { j = JSON.parse(x.responseText); } catch (e) { /* не JSON */ }
      if (x.status === 200) { resolve(j); return; }
      const err = new Error(describeError(j.error, x.status));
      err.code = j.error;
      reject(err);
    };
    x.onerror = () => reject(new Error('Нет связи с гирляндой во время загрузки'));
    const fd = new FormData();
    fd.append('firmware', file, file.name);
    x.send(fd);
  });
}

async function otaUpload() {
  const file = $('otaFile').files[0];
  const bad = await otaCheckFile(file);
  if (bad) { $('otaResult').textContent = bad; toast(bad); return; }
  if (!confirm('Прошить гирлянду файлом ' + file.name + ' (' + Math.round(file.size / 1024) + ' КБ)? Не выключай питание, пока идёт запись.')) return;
  ota.busy = true;
  $('otaGo').disabled = true;
  $('otaBar').style.width = '0%';
  $('otaResult').textContent = 'Загружаю…';
  try {
    await otaSend(file, (f) => {
      $('otaBar').style.width = Math.round(f * 100) + '%';
      $('otaResult').textContent = 'Загружаю: ' + Math.round(f * 100) + '%';
    });
    $('otaBar').style.width = '100%';
    $('otaResult').textContent = 'Прошивка записана, гирлянда перезагружается…';
    // ждём возвращения: сначала связь пропадает, потом гирлянда снова отвечает
    const old = st && st.fw;
    for (let i = 0; i < 60; i++) {
      await new Promise((r) => setTimeout(r, 1000));
      try {
        const s = await api.get('/api/state');
        if (i >= 2) {
          st = s;
          render(true);
          $('otaResult').textContent = 'Готово. Версия прошивки: ' + s.fw + (old && old !== s.fw ? ' (была ' + old + ')' : '') + '.';
          $('otaVer').textContent = s.fw;
          toast('Прошивка обновлена', true);
          break;
        }
      } catch (e) { /* ещё перезагружается */ }
      if (i === 59) $('otaResult').textContent = 'Гирлянда долго не отвечает. Подожди минуту и обнови страницу.';
    }
  } catch (e) {
    $('otaResult').textContent = e.message;
    toast(e.message);
  } finally {
    ota.busy = false;
    otaRefresh();
  }
}

function initOta() {
  $('otaFile').addEventListener('change', async () => {
    const msg = await otaCheckFile($('otaFile').files[0]);
    $('otaResult').textContent = msg;
    otaRender();
  });
  $('otaGo').addEventListener('click', otaUpload);
  document.addEventListener('gt-tab', (e) => {
    if (e.detail === 'ota') {
      if (st) $('otaVer').textContent = st.fw;
      otaRefresh();
    }
  });
}

// ---------- вкладка «Расписание» ----------
// Чистые функции (проверяются в web/test/schedule.test.mjs)
const pad2 = (n) => String(n).padStart(2, '0');
function minToTime(m) {
  return pad2(Math.floor(m / 60)) + ':' + pad2(m % 60);
}
// 'ЧЧ:ММ' -> минуты суток, -1 если не время
function timeToMin(s) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(s || '');
  if (!m) return -1;
  const h = Number(m[1]);
  const mi = Number(m[2]);
  return h > 23 || mi > 59 ? -1 : h * 60 + mi;
}
function tzLabel(min) {
  const a = Math.abs(min);
  return 'UTC' + (min < 0 ? '−' : '+') + Math.floor(a / 60) + (a % 60 ? ':' + pad2(a % 60) : '');
}
// маска дней (бит 0 - понедельник) <-> массив из 7 флагов
const daysFromMask = (mask) => Array.from({ length: 7 }, (_, i) => !!((mask >> i) & 1));
const maskFromDays = (flags) => flags.reduce((m, f, i) => m | (f ? 1 << i : 0), 0);
// местное время из UTC (секунды) и пояса (минуты): 'ЧЧ:ММ:СС'
function clockText(utc, tzMin) {
  const t = Math.floor(utc + tzMin * 60);
  const s = ((t % 86400) + 86400) % 86400;
  return pad2(Math.floor(s / 3600)) + ':' + pad2(Math.floor(s / 60) % 60) + ':' + pad2(s % 60);
}
// градусы -> сотые доли с проверкой диапазона; null - не число или вне диапазона
function degToCenti(v, limit) {
  const n = typeof v === 'string' ? Number(v.replace(',', '.')) : v;
  if (v === '' || v === null || !Number.isFinite(n) || Math.abs(n) > limit) return null;
  return Math.round(n * 100);
}
const SRC_TEXT = { none: 'время не установлено', ntp: 'время из интернета (NTP)', manual: 'время со страницы, до перезагрузки гирлянды' };
function todayText(s) {
  if (!s.en) return 'Расписание выключено.';
  if (!s.synced) return 'Расписание не работает, пока не установлено время.';
  const part = (m, what) => (m < 0 ? what + ': сегодня нет' : what + ' в ' + minToTime(m));
  return 'Сегодня: ' + part(s.onAt, 'включится') + ', ' + part(s.offAt, 'выключится') + '.';
}

const sched = { st: null, fetched: 0, timer: 0, tick: 0, pending: 0 };   // pending: сколько отправок настроек в пути

function schedFill(force) {
  const s = sched.st;
  if (!s) return;
  if (sched.pending && !force) return;   // пользователь только что менял поля: устаревший ответ их не трогает (текущий придёт следом)
  setVal('sEn', s.en, force);
  setVal('sTz', s.tz / 60, force);
  const days = daysFromMask(s.days);
  document.querySelectorAll('#sDays input').forEach((el) => { el.checked = days[Number(el.dataset.day)]; });
  for (const [p, t, v] of [['On', s.onT, s.onV], ['Off', s.offT, s.offV]]) {
    setVal('s' + p + 'T', t, force);
    if (t === 0) setVal('s' + p + 'Time', minToTime(v), force);
    else setVal('s' + p + 'Off', v, force);
  }
  schedLayout();
  setVal('sLat', s.lat / 100, force);
  setVal('sLon', s.lon / 100, force);
  $('sToday').textContent = todayText(s);
  schedClock();
}

// какое поле показывать у события (время суток или смещение от солнца), подписи, блок координат
function schedLayout() {
  for (const p of ['On', 'Off']) {
    const t = Number($('s' + p + 'T').value);
    $('s' + p + 'Time').hidden = t !== 0;
    $('s' + p + 'Off').hidden = t === 0;
    $('s' + p + 'Label').textContent = (p === 'On' ? 'Включение' : 'Выключение') + (t === 0 ? ': время' : ': минут от ' + (t === 1 ? 'заката' : 'рассвета') + ' (− раньше)');
    $('s' + p + 'Label').htmlFor = t === 0 ? 's' + p + 'Time' : 's' + p + 'Off';
  }
  $('sGeo').hidden = Number($('sOnT').value) === 0 && Number($('sOffT').value) === 0;
}

// сменили тип события: поля сразу перестраиваются, пустым дают разумные значения
function schedTypeChanged(p) {
  const t = Number($('s' + p + 'T').value);
  if (t !== 0 && $('s' + p + 'Off').value === '') $('s' + p + 'Off').value = 0;
  if (t === 0 && !$('s' + p + 'Time').value) $('s' + p + 'Time').value = p === 'On' ? '18:00' : '23:00';
  schedLayout();
  schedSend();
}

function schedClock() {
  const s = sched.st;
  if (!s) return;
  $('sClock').textContent = s.synced
    ? clockText(s.utc + (Date.now() - sched.fetched) / 1000, s.tz) + ' (' + tzLabel(s.tz) + '), ' + SRC_TEXT[s.src]
    : 'Время не установлено';
}

async function schedLoad() {
  try {
    sched.st = await api.get('/api/schedule');
    sched.fetched = Date.now();
    schedFill(false);
  } catch (e) { /* связь пропала: покажет значок */ }
}

// собрать настройки со страницы; null - что-то заполнено неверно (сообщение уже показано)
function schedCollect() {
  const bad = (msg) => { toast(msg); return null; };
  const tz = Math.round(Number($('sTz').value.replace(',', '.')) * 60);
  if ($('sTz').value === '' || !Number.isFinite(tz) || tz < -720 || tz > 840) return bad('Часовой пояс: от −12 до +14 часов');
  const p = { en: $('sEn').checked ? 1 : 0, tz, days: maskFromDays([...document.querySelectorAll('#sDays input')].map((e) => e.checked)) };
  for (const e of ['On', 'Off']) {
    const t = Number($('s' + e + 'T').value);
    p[e.toLowerCase() + 'T'] = t;
    if (t === 0) {
      const m = timeToMin($('s' + e + 'Time').value);
      if (m < 0) return bad('Укажи время ' + (e === 'On' ? 'включения' : 'выключения'));
      p[e.toLowerCase() + 'V'] = m;
    } else {
      const o = Number($('s' + e + 'Off').value);
      if ($('s' + e + 'Off').value === '' || !Number.isInteger(o) || o < -240 || o > 240) return bad('Смещение: целое число минут от −240 до 240');
      p[e.toLowerCase() + 'V'] = o;
    }
  }
  const lat = degToCenti($('sLat').value, 90);
  const lon = degToCenti($('sLon').value, 180);
  if (!$('sGeo').hidden && (lat === null || lon === null)) return bad('Координаты: широта от −90 до 90, долгота от −180 до 180');
  if (lat !== null && lon !== null) {
    p.lat = lat;
    p.lon = lon;
  }
  return p;
}

async function schedSend() {
  const p = schedCollect();
  if (!p) return;
  sched.pending++;
  try {
    sched.st = await api.post('/api/schedule', p);
    sched.fetched = Date.now();
  } catch (e) {
    toast(e.message);
    sched.pending--;
    schedLoad().then(() => schedFill(true));
    return;
  }
  sched.pending--;
  schedFill(false);
}

// время и часовой пояс этого устройства -> гирлянде (тихо - при автоматической отправке)
async function schedSyncTime(quiet) {
  try {
    const now = Math.floor(Date.now() / 1000);
    sched.st = await api.post('/api/time', { unix: now });
    sched.fetched = Date.now();
    if (!quiet) {
      const tz = -new Date().getTimezoneOffset();
      sched.st = await api.post('/api/schedule', { tz });
    }
    schedFill(true);
    toast(quiet ? 'Время гирлянды установлено с этого устройства' : 'Время и часовой пояс взяты с этого устройства', true);
  } catch (e) {
    toast(e.message);
  }
}

function initSched() {
  const send = debounced(schedSend, 500);
  $('sEn').addEventListener('change', schedSend);
  $('sOnT').addEventListener('change', () => schedTypeChanged('On'));
  $('sOffT').addEventListener('change', () => schedTypeChanged('Off'));
  // числа и время отправляются по завершении ввода (change), а не на каждую цифру
  for (const id of ['sTz', 'sOnTime', 'sOnOff', 'sOffTime', 'sOffOff', 'sLat', 'sLon']) {
    trackBusy($(id));
    $(id).addEventListener('change', send);
  }
  document.querySelectorAll('#sDays input').forEach((el) => el.addEventListener('change', schedSend));
  $('sSync').addEventListener('click', () => schedSyncTime(false));
  const poll = () => {
    clearTimeout(sched.timer);
    clearInterval(sched.tick);
    if (!document.getElementById('tab-sched').classList.contains('active')) return;
    sched.tick = setInterval(schedClock, 1000);   // часы идут между опросами
    schedLoad().finally(() => { sched.timer = setTimeout(poll, 5000); });
  };
  document.addEventListener('gt-tab', poll);
}

// при включённом расписании и неустановленном времени страница сама передаёт время (режим точки доступа)
function schedStart() {
  schedLoad().then(() => { if (sched.st && sched.st.en && !sched.st.synced) schedSyncTime(true); });
}

// ---------- защита токеном ----------
// допустимый токен (как в прошивке): 8-32 символа из латинских букв, цифр и . _ ~ -
const tokenValid = (s) => /^[A-Za-z0-9._~-]{8,32}$/.test(s);
// случайный токен без похожих символов (0/O, 1/l/I); rand - источник случайных байт (по умолчанию crypto)
function genToken(rand) {
  const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789';
  const fill = rand || ((a) => crypto.getRandomValues(a));
  const limit = 256 - (256 % alphabet.length);   // отбрасываем хвост, чтобы символы выпадали равновероятно
  let out = '';
  while (out.length < 16) {
    const bytes = fill(new Uint8Array(32));
    for (const b of bytes) {
      if (b < limit && out.length < 16) out += alphabet[b % alphabet.length];
    }
  }
  return out;
}

function guardNeeded() {
  if (guardUi.needed) return;
  guardUi.needed = true;
  $('login').hidden = false;
  $('loginMsg').textContent = tokenGet() ? 'Сохранённый токен не подошёл (возможно, его сменили).' : '';
  $('loginToken').value = '';
  setTimeout(() => $('loginToken').focus(), 0);
}

// проверка доступа при загрузке: true - можно работать
async function guardInit() {
  try {
    const s = await api.get('/api/auth');
    if (s.on && !s.ok) {
      guardNeeded();
      if (s.locked) $('loginMsg').textContent = describeError('locked');
      return false;
    }
  } catch (e) { /* связи нет: дальше покажет значок, опрос продолжится */ }
  return true;
}

async function guardLogin() {
  const t = $('loginToken').value.trim();
  if (!tokenValid(t)) {
    $('loginMsg').textContent = 'Токен: 8–32 символа (латиница, цифры, . _ ~ -).';
    return;
  }
  tokenSet(t);
  guardUi.needed = false;
  try {
    const s = await api.get('/api/auth');
    if (s.locked) throw new Error(describeError('locked'));
    if (s.on && !s.ok) throw new Error('Неверный токен.');
  } catch (e) {
    tokenClear();
    guardUi.needed = true;
    $('loginMsg').textContent = e.message;
    return;
  }
  $('login').hidden = true;
  $('loginToken').value = '';
  startApp();
  refresh(true);
  const active = document.querySelector('#tabs button.active');
  if (active) document.dispatchEvent(new CustomEvent('gt-tab', { detail: active.dataset.tab }));
}

const guardCard = { st: null };

function guardRender() {
  const s = guardCard.st;
  if (!s) return;
  const win = ota.st ? !!ota.st.open : !!s.win;   // окно обслуживания то же, что для обновления прошивки
  $('gStatus').textContent = (s.on ? 'Защита включена: управление требует токен.' : 'Защита выключена: гирляндой может управлять любой в этой сети.') +
    (win ? ' Окно изменения открыто.' : ' Окно изменения закрыто.');
  $('gSave').disabled = !win;
  $('gClear').disabled = !(win && s.on);
  $('gUdp').checked = !!s.udpLock;
  $('gHint').textContent = !s.on ? '' : s.udpLock
    ? 'Приложение не сможет управлять гирляндой, пока в нём нет поддержки токена.'
    : 'Управление из приложения (UDP) остаётся открытым для всех в сети: приложение не умеет вводить токен. Закрой его, если важнее защита.';
}

async function guardLoad() {
  try {
    guardCard.st = await api.get('/api/auth');
    guardRender();
  } catch (e) { /* связь пропала или нужен вход */ }
}

function initGuard() {
  $('loginGo').addEventListener('click', guardLogin);
  $('loginToken').addEventListener('keydown', (e) => { if (e.key === 'Enter') guardLogin(); });
  $('gGen').addEventListener('click', () => { $('gToken').value = genToken(); });
  $('gSave').addEventListener('click', async () => {
    const t = $('gToken').value.trim();
    if (!tokenValid(t)) {
      toast('Токен: 8–32 символа (латиница, цифры, . _ ~ -)');
      return;
    }
    guardUi.saving = true;   // запросы, получившие отказ, пока токен меняется, подождут и повторятся с новым (см. api.call)
    try {
      await api.post('/api/auth', { token: t });
      tokenSet(t);   // эту страницу не разлогиниваем: она уже знает новый токен
      $('gToken').value = '';
      toast('Токен сохранён. Запомни его: он нужен на других устройствах', true);
    } catch (e) {
      toast(e.message);
    } finally {
      guardUi.saving = false;
    }
    guardLoad();
  });
  $('gClear').addEventListener('click', async () => {
    if (!confirm('Убрать защиту? Гирляндой сможет управлять любой в этой сети.')) return;
    try {
      await api.post('/api/auth', { token: '' });
      tokenClear();
      toast('Защита выключена', true);
    } catch (e) {
      toast(e.message);
    }
    guardLoad();
  });
  $('gUdp').addEventListener('change', async () => {
    try {
      await api.post('/api/auth', { udpLock: $('gUdp').checked ? 1 : 0 });
    } catch (e) {
      toast(e.message);
    }
    guardLoad();
  });
  document.addEventListener('gt-tab', (e) => { if (e.detail === 'ota') guardLoad(); });
}

// ---------- вкладки ----------
function showTab(name) {
  for (const b of document.querySelectorAll('#tabs button')) {
    b.classList.toggle('active', b.dataset.tab === name);
    b.setAttribute('aria-selected', b.dataset.tab === name ? 'true' : 'false');
  }
  for (const s of document.querySelectorAll('.tab')) s.classList.toggle('active', s.id === 'tab-' + name);
  document.dispatchEvent(new CustomEvent('gt-tab', { detail: name }));
}

function initTabs() {
  for (const b of document.querySelectorAll('#tabs button')) {
    b.addEventListener('click', () => showTab(b.dataset.tab));
  }
}

// ---------- запуск ----------
function init() {
  initTabs();
  initControl();
  initEffects();
  initScenes();
  initCoords();
  initSpiral();
  initHex();
  initVcalib();
  initOta();
  initSched();
  initGuard();
  // сеть - только после проверки доступа: с чужим токеном несколько запросов подряд заблокировали бы это устройство
  guardInit().then((ok) => { if (ok) startApp(); });
}

function startApp() {
  if (guardUi.started) return;
  guardUi.started = true;
  schedStart();
  refresh();
  setInterval(refresh, 2000);
}

if (typeof document !== 'undefined') init();
if (typeof module !== 'undefined') module.exports = { EFFECT_NAMES, spiralLayout, flipVertical, zoomedView, zoomAround, clampPan, ZOOM_MIN, ZOOM_MAX, tokenValid, genToken, minToTime, timeToMin, tzLabel, daysFromMask, maskFromDays, clockText, degToCenti, todayText, debounced, describeError, fitBytes, hexToPoints, pointsToHex, fitView, toScreen, toWorld, nearestPoint, suspiciousPoints };

'use strict';
// Веб-интерфейс GyverTwink. Один файл без зависимостей: страница вшита в прошивку (gzip),
// интернет не нужен. API описан в docs/WEB_API.md. Исходники: web/, сборка: tools/gen_web.py.

const EFFECT_NAMES = [
  'Party_grad', 'Raibow_grad', 'Stripe_grad', 'Sunset_grad', 'Pepsi_grad', 'Warm_grad',
  'Cold_grad', 'Hot_grad', 'Pink_grad', 'Cyber_grad', 'RedWhite_grad',
  'Party_noise', 'Raibow_noise', 'Stripe_noise', 'Sunset_noise', 'Pepsi_noise', 'Warm_noise',
  'Cold_noise', 'Hot_noise', 'Pink_noise', 'Cyber_noise', 'RedWhite_noise',
];

const $ = (id) => document.getElementById(id);

// имена параметров в сообщениях об ошибках (прошивка отвечает {"error":"<параметр>"})
const FIELD_NAMES = {
  ledAm: 'количество светодиодов', power: 'питание', bright: 'яркость', auto: 'автосмена', rnd: 'случайный порядок',
  period: 'период смены', offOn: 'таймер выключения', offMin: 'минуты до выключения', maxMa: 'лимит тока',
  n: 'номер эффекта', fav: 'избранное', scale: 'масштаб', speed: 'скорость', show: 'показ эффекта',
};
function describeError(code, status) {
  if (code === 'forbidden') return 'Запрос отклонён устройством';
  if (FIELD_NAMES[code]) return 'Недопустимое значение: ' + FIELD_NAMES[code];
  return code || ('Ошибка ' + status);
}

// ---------- запросы к устройству ----------
// Все изменяющие запросы несут заголовок X-Requested-With: чужая страница в браузере не сможет
// отправить такой запрос без CORS-предзапроса, на который устройство не отвечает (защита от CSRF).
const api = {
  async call(method, path, params, body) {
    const opt = { method, headers: { 'X-Requested-With': 'GyverTwink' } };
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
    if (!r.ok) throw new Error(describeError(data.error, r.status));
    return data;
  },
  get(path, params) { return this.call('GET', path, params); },
  post(path, params, body) { return this.call('POST', path, params, body); },
};

let toastTimer = 0;
function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
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
  $('cur').textContent = st.cur + '. ' + EFFECT_NAMES[st.cur];
  $('foot').textContent = 'Прошивка ' + st.fw + ' · свободно ' + Math.round(st.heap / 1024) + ' КБ';
  markCurrentEffect();
}

async function refresh(force) {
  try {
    st = await api.get('/api/state');
    setConn(true);
    render(force);
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
  $('effFav').checked = !!e.fav;
  $('effScale').value = e.scale;
  $('effScale-val').textContent = e.scale;
  $('effSpeed').value = e.speed;
  $('effSpeed-val').textContent = e.speed;
}

async function selectEffect(n) {
  selEff = n;
  for (const b of document.querySelectorAll('#effList button')) b.classList.toggle('sel', Number(b.dataset.n) === n);
  $('effEditor').hidden = false;
  try {
    // show=1: прошивка сразу показывает этот эффект
    fillEffect(await api.post('/api/effect', { n, show: 1 }));
  } catch (e) {
    toast(e.message);
  }
}

async function editEffect(params) {
  if (selEff < 0) return;
  try {
    const e = await api.post('/api/effect', Object.assign({ n: selEff, show: 1 }, params));
    if (!busy.has('effScale') && !busy.has('effSpeed')) fillEffect(e);
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
}

// ---------- вкладки ----------
function showTab(name) {
  for (const b of document.querySelectorAll('#tabs button')) b.classList.toggle('active', b.dataset.tab === name);
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
  refresh();
  setInterval(refresh, 2000);
}

if (typeof document !== 'undefined') init();
if (typeof module !== 'undefined') module.exports = { EFFECT_NAMES, debounced, describeError };

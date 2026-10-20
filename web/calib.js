'use strict';
// Калибровка по видео: разбор ролика, снятого во время серии (см. vcalib.ino).
// Здесь только вычисления без DOM: кадр - {w, h, g}, где g - яркость пикселей 0..255 (массив длиной w*h);
// источник кадров - функция sample(t) (секунды -> Promise кадра). В браузере это видео с перемоткой,
// в тестах - синтетические кадры (web/test/calib.test.mjs).

const VC_GRID_COLUMNS = 60;   // как в приложении: карта - 60 клеток по ширине кадра

function medianOf(arr) {
  if (!arr.length) return 0;
  const s = Array.from(arr).sort((a, b) => a - b);
  return s[Math.floor(s.length / 2)];
}

function percentileOf(arr, q) {
  if (!arr.length) return 0;
  const s = Array.from(arr).sort((a, b) => a - b);
  return s[Math.min(s.length - 1, Math.floor(s.length * q))];
}

// средние положительные и отрицательные изменения яркости между кадрами (для поиска вспышки: вверх на её начале, вниз на конце).
// Мёртвая зона dead отбрасывает шум матрицы: считается только то, что выше неё.
const DEAD_ZONE = 8;
function meanChange(prev, cur) {
  const n = cur.g.length;
  let up = 0;
  let down = 0;
  for (let i = 0; i < n; i++) {
    const d = cur.g[i] - prev.g[i];
    if (d > DEAD_ZONE) up += d - DEAD_ZONE;
    else if (d < -DEAD_ZONE) down += -d - DEAD_ZONE;
  }
  return { up: up / n, down: down / n };
}

function meanBrightness(f) {
  let s = 0;
  for (let i = 0; i < f.g.length; i++) s += f.g[i];
  return s / f.g.length;
}

// размытие окном (2r+1) x (2r+1): гасит шум и склеивает пятно светодиода
function boxBlur(src, w, h, r) {
  const tmp = new Float32Array(w * h);
  const out = new Float32Array(w * h);
  const k = 2 * r + 1;
  for (let y = 0; y < h; y++) {
    let acc = 0;
    for (let x = -r; x <= r; x++) acc += src[y * w + Math.min(w - 1, Math.max(0, x))];
    for (let x = 0; x < w; x++) {
      tmp[y * w + x] = acc / k;
      acc += src[y * w + Math.min(w - 1, x + r + 1)] - src[y * w + Math.max(0, x - r)];
    }
  }
  for (let x = 0; x < w; x++) {
    let acc = 0;
    for (let y = -r; y <= r; y++) acc += tmp[Math.min(h - 1, Math.max(0, y)) * w + x];
    for (let y = 0; y < h; y++) {
      out[y * w + x] = acc / k;
      acc += tmp[Math.min(h - 1, y + r + 1) * w + x] - tmp[Math.max(0, y - r) * w + x];
    }
  }
  return out;
}

// положение светодиода, который загорелся между кадрами prev и cur: самое яркое пятно среди ПОЯВИВШИХСЯ
// (погасший соседний светодиод даёт отрицательную разность и игнорируется)
function locateLed(prev, cur) {
  const { w, h } = cur;
  const diff = new Float32Array(w * h);
  for (let i = 0; i < diff.length; i++) diff[i] = Math.max(0, cur.g[i] - prev.g[i]);
  const bl = boxBlur(diff, w, h, 2);
  let pi = 0;
  for (let i = 1; i < bl.length; i++) if (bl[i] > bl[pi]) pi = i;
  const peak = bl[pi];
  const px = pi % w;
  const py = (pi / w) | 0;
  // уровень шума: 95-й процентиль размытой разности (пятно занимает ничтожную долю кадра)
  const noise = percentileOf(bl, 0.95);
  // центр тяжести пятна в окне вокруг пика, веса - разность выше трети максимума в окне
  const R = 5;
  let wmax = 0;
  for (let y = Math.max(0, py - R); y <= Math.min(h - 1, py + R); y++) {
    for (let x = Math.max(0, px - R); x <= Math.min(w - 1, px + R); x++) wmax = Math.max(wmax, diff[y * w + x]);
  }
  let sw = 0;
  let sx = 0;
  let sy = 0;
  for (let y = Math.max(0, py - R); y <= Math.min(h - 1, py + R); y++) {
    for (let x = Math.max(0, px - R); x <= Math.min(w - 1, px + R); x++) {
      const v = diff[y * w + x] - wmax / 3;
      if (v > 0) { sw += v; sx += v * x; sy += v * y; }
    }
  }
  return { x: sw > 0 ? sx / sw : px, y: sw > 0 ? sy / sw : py, peak, snr: peak / Math.max(noise, 0.5) };
}

// найден ли светодиод: пятно заметно и выделяется на фоне шума
function ledFound(loc) {
  return loc.peak >= 8 && loc.snr >= 3;
}

// светодиоды, которых не нашли, получают положение по соседям (линейно по номеру); возвращает признак "оценено"
function fillMissing(pts) {
  const n = pts.length;
  const out = pts.map((p) => ({ x: p.x, y: p.y, found: p.found, est: false }));
  const idx = [];
  for (let i = 0; i < n; i++) if (pts[i].found) idx.push(i);
  if (!idx.length) return out;
  for (let i = 0; i < n; i++) {
    if (pts[i].found) continue;
    let a = -1;
    let b = -1;
    for (let j = i - 1; j >= 0; j--) if (pts[j].found) { a = j; break; }
    for (let j = i + 1; j < n; j++) if (pts[j].found) { b = j; break; }
    let x;
    let y;
    if (a >= 0 && b >= 0) {
      const t = (i - a) / (b - a);
      x = pts[a].x + (pts[b].x - pts[a].x) * t;
      y = pts[a].y + (pts[b].y - pts[a].y) * t;
    } else {
      const s = a >= 0 ? pts[a] : pts[b];
      x = s.x;
      y = s.y;
    }
    out[i].x = x;
    out[i].y = y;
    out[i].est = true;
  }
  return out;
}

// пиксели кадра -> карта прошивки: 60 клеток по ширине, y в тех же клетках (как в приложении)
function toGridCoords(pts, frameW) {
  const cell = frameW / VC_GRID_COLUMNS;
  const c = (v) => Math.max(0, Math.min(255, Math.floor(v / cell)));
  return pts.map((p) => ({ x: c(p.x), y: c(p.y) }));
}

class CalibError extends Error {}

// Поиск маркера и вычисление положений. params: {n, step, flash, gap} (мс; как в ответе /api/vcalib),
// duration - длина видео в секундах; onProgress(этап, сделано, всего); abort.aborted - прервать.
async function analyzeVideo(sample, duration, params, onProgress, abort) {
  const note = onProgress || (() => {});
  const stop = () => { if (abort && abort.aborted) throw new CalibError('Прервано'); };
  const flash = params.flash / 1000;
  const gap = params.gap / 1000;
  const seq = (params.n * params.step) / 1000;
  const need = flash + gap + seq;
  if (!(duration > need + 0.5)) throw new CalibError('Видео слишком короткое: нужно хотя бы ' + (need + 0.5).toFixed(0) + ' с, а в ролике ' + duration.toFixed(0) + ' с');

  // 1. маркер: ищем начало и конец вспышки грубым шагом 0.1 с
  const dt = 0.1;
  const fk = Math.round(flash / dt);
  const tMax = duration - need + 0.3;
  const K = Math.floor((tMax + flash) / dt) + 2;
  const P = new Float32Array(K);
  const N = new Float32Array(K);
  // первые 0,2 с пропускаем: у многих роликов самые первые кадры (ещё не декодированные, чёрные) дают ложный «скачок яркости»
  const k0 = 2;
  let prev = await sample(k0 * dt);
  for (let k = k0 + 1; k < K; k++) {
    stop();
    const cur = await sample(k * dt);
    const c = meanChange(prev, cur);
    P[k] = c.up;
    N[k] = c.down;
    prev = cur;
    if (k % 5 === 0) note('marker', k, K);
  }
  let best = -1;
  let bestScore = 0;
  const scores = new Float32Array(K);
  for (let k = k0 + 1; k + fk < K; k++) {
    scores[k] = P[k] + N[k + fk];
    if (k * dt <= tMax && scores[k] > bestScore) { bestScore = scores[k]; best = k; }
  }
  if (best < 0) throw new CalibError('Не найдена вспышка-маркер. Убедись, что запись началась до старта серии и гирлянда в кадре.');
  // "выразительность": превышение лучшей пары над фоном (медианой) к превышению второй по силе пары вне окна ±1 с
  const floor = medianOf(scores.subarray(k0 + 1, K - fk));
  let second = floor;
  for (let k = k0 + 1; k + fk < K; k++) if (Math.abs(k - best) * dt > 1.0) second = Math.max(second, scores[k]);
  const markerConf = (bestScore - floor) / Math.max(second - floor, 0.02);
  if (bestScore - floor < 0.05 || markerConf < 3) throw new CalibError('Вспышка-маркер неразличима на видео (уверенность ' + markerConf.toFixed(1) + '). В кадре должна быть заметная часть гирлянды, в комнате темно, камера неподвижна.');

  // 2. уточняем фронты шагом 20 мс: начало и конец вспышки по яркости кадра
  async function edge(tc, rising) {
    const ts = [];
    const bs = [];
    for (let t = Math.max(0, tc - 0.2); t <= tc + 0.2 + 1e-9; t += 0.02) {
      stop();
      ts.push(t);
      bs.push(meanBrightness(await sample(t)));
    }
    let bi = 1;
    let bd = 0;
    for (let j = 1; j < bs.length; j++) {
      const d = rising ? bs[j] - bs[j - 1] : bs[j - 1] - bs[j];
      if (d > bd) { bd = d; bi = j; }
    }
    return ts[bi];
  }
  const tOn = await edge(best * dt - dt / 2, true);
  const tOff = await edge((best + fk) * dt - dt / 2, false);
  const center = (tOn + tOff) / 2;
  const seqStart = center + flash / 2 + gap;
  if (seqStart + seq > duration + 0.05) throw new CalibError('После маркера видео обрывается раньше конца серии: запись остановлена слишком рано.');

  // 3. светодиоды: кадр в середине слота i против кадра предыдущего слота (для первого - кадр паузы)
  const raw = [];
  let before = await sample(seqStart - 0.04);   // кадр в самом конце паузы: между ним и первым светодиодом почти ничего не успевает измениться
  let w = before.w;
  for (let i = 0; i < params.n; i++) {
    stop();
    const cur = await sample(seqStart + (i + 0.5) * (params.step / 1000));
    const loc = locateLed(before, cur);
    raw.push({ x: loc.x, y: loc.y, snr: loc.snr, peak: loc.peak, found: false });
    before = cur;
    if (i % 5 === 0) note('leds', i, params.n);
  }
  note('leds', params.n, params.n);
  // «найден» - пятно заметно само по себе и по сравнению с типичным светодиодом этого ролика
  // (медленное движение людей и дрейф экспозиции дают намного более слабые пятна)
  const ref = percentileOf(raw.map((p) => p.peak), 0.75);
  for (const p of raw) p.found = p.peak >= Math.max(10, 0.25 * ref) && p.snr >= 3;
  const found = raw.filter((p) => p.found).length;
  if (found < Math.max(1, 0.3 * params.n)) throw new CalibError('Найдено только ' + found + ' светодиодов из ' + params.n + ': проверь, что гирлянда в кадре, в комнате темно, а камера неподвижна.');
  const filled = fillMissing(raw);
  return {
    w,
    points: filled,
    found,
    total: params.n,
    markerConf,
    medianSnr: medianOf(raw.filter((p) => p.found).map((p) => p.snr)),
    seqStart,
    raw,   // сырые измерения по светодиодам (положение, пик, snr, found) - для диагностики
  };
}

if (typeof module !== 'undefined') {
  module.exports = { analyzeVideo, locateLed, ledFound, fillMissing, toGridCoords, boxBlur, meanChange, medianOf, percentileOf, CalibError, VC_GRID_COLUMNS };
}

// Тесты разбора видео калибровки на синтетических кадрах: node --test web/test
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const calib = require('../calib.js');

// ---------- синтетическая "съёмка" ----------
// Параметры серии как у прошивки (/api/vcalib)
const P = { n: 40, step: 120, flash: 600, gap: 500, dark: 1500, marker: 25, delay: 3 };
const W = 320;
const H = 180;
const FPS = 30;

function rng(seed) {   // детерминированный ГПСЧ
  let s = seed >>> 0;
  return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
}

function blob(g, cx, cy, amp, sigma) {
  const R = Math.ceil(sigma * 3);
  for (let y = Math.max(0, Math.round(cy) - R); y <= Math.min(H - 1, Math.round(cy) + R); y++) {
    for (let x = Math.max(0, Math.round(cx) - R); x <= Math.min(W - 1, Math.round(cx) + R); x++) {
      g[y * W + x] += amp * Math.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * sigma * sigma));
    }
  }
}

// положения светодиодов в кадре: волнистая линия вдоль кадра, часть за его пределами (как у настоящей гирлянды)
function truthPositions(n, outside = []) {
  return Array.from({ length: n }, (_, i) => ({
    x: 30 + (i / (n - 1)) * 260 + 6 * Math.sin(i / 3),
    y: 25 + (i / (n - 1)) * 130 + 12 * Math.cos(i / 4),
    visible: !outside.includes(i),
  }));
}

// видео: firmware стартует в момент t0 (с начала ролика); возвращает sample(t) как у перемотки видео (кадры по 1/FPS)
function makeVideo(opts) {
  const o = Object.assign({ t0: 1.3, noise: 2.5, drift: 0.0, distract: true, seed: 7, truth: truthPositions(P.n), markerVisible: true }, opts);
  const flashStart = o.t0 + P.delay + P.dark / 1000;
  const seqStart = flashStart + (P.flash + P.gap) / 1000;
  const total = seqStart + (P.n * P.step) / 1000 + 1.5;
  const markerIdx = Array.from({ length: Math.min(P.n, P.marker) }, (_, k) => Math.floor((k * P.n) / Math.min(P.n, P.marker)));
  const cache = new Map();
  const rand = rng(o.seed);
  const gauss = () => { let s = 0; for (let k = 0; k < 6; k++) s += rand(); return (s - 3) * 1.41; };
  function frameAt(tq) {
    const g = new Float32Array(W * H).fill(22);
    // «комната»: статичная лампа и движущееся тусклое пятно (человек), медленный дрейф экспозиции
    if (o.distract) {
      blob(g, 250, 40, 90, 6);
      blob(g, 60 + 12 * tq, 140 - 3 * tq, 35, 9);
    }
    const lit = [];
    if (tq >= flashStart && tq < flashStart + P.flash / 1000 && o.markerVisible) lit.push(...markerIdx);
    if (tq >= seqStart && tq < seqStart + (P.n * P.step) / 1000) lit.push(Math.floor((tq - seqStart) / (P.step / 1000)));
    for (const i of lit) if (o.truth[i].visible) blob(g, o.truth[i].x, o.truth[i].y, 150, 2.2);
    const gain = 1 + o.drift * Math.sin(tq / 4);
    for (let i = 0; i < g.length; i++) g[i] = Math.max(0, Math.min(255, g[i] * gain + o.noise * gauss()));
    return { w: W, h: H, g };
  }
  const sample = async (t) => {
    const tq = Math.floor(Math.max(0, Math.min(total, t)) * FPS) / FPS;   // перемотка попадает на кадр
    if (!cache.has(tq)) cache.set(tq, frameAt(tq));
    return cache.get(tq);
  };
  return { sample, duration: total, flashStart, seqStart, truth: o.truth };
}

const err = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

// ---------- тесты ----------
test('locateLed: находит появившийся светодиод с точностью до долей пикселя и игнорирует погасший', () => {
  const g0 = new Float32Array(W * H).fill(20);
  const g1 = new Float32Array(W * H).fill(20);
  blob(g0, 100, 60, 150, 2.2);   // горел светодиод A (погас)
  blob(g1, 200.4, 90.7, 150, 2.2);   // загорелся B
  const loc = calib.locateLed({ w: W, h: H, g: g0 }, { w: W, h: H, g: g1 });
  assert.ok(Math.abs(loc.x - 200.4) < 0.7 && Math.abs(loc.y - 90.7) < 0.7, `(${loc.x.toFixed(2)}, ${loc.y.toFixed(2)})`);
  assert.ok(calib.ledFound(loc));
});

test('ledFound: пустая разность и шум - не светодиод', () => {
  const a = { w: W, h: H, g: new Float32Array(W * H).fill(30) };
  assert.ok(!calib.ledFound(calib.locateLed(a, a)));
  const r = rng(3);
  const noisy = { w: W, h: H, g: Float32Array.from({ length: W * H }, () => 30 + (r() - 0.5) * 10) };
  assert.ok(!calib.ledFound(calib.locateLed(a, noisy)));
});

test('fillMissing: оценка по соседям, края копируют ближайшую найденную', () => {
  const pts = [
    { x: 0, y: 0, found: false }, { x: 10, y: 10, found: true }, { x: 0, y: 0, found: false },
    { x: 0, y: 0, found: false }, { x: 40, y: 10, found: true }, { x: 0, y: 0, found: false },
  ];
  const f = calib.fillMissing(pts);
  assert.deepEqual([f[2].x, f[2].y, f[2].est], [20, 10, true]);
  assert.deepEqual([f[3].x, f[3].y], [30, 10]);
  assert.deepEqual([f[0].x, f[0].y, f[0].est], [10, 10, true]);
  assert.deepEqual([f[5].x, f[5].y], [40, 10]);
  assert.ok(!f[1].est && !f[4].est);
  assert.ok(calib.fillMissing([{ x: 1, y: 1, found: false }])[0].found === false);   // нечем оценивать - без изменений
});

test('toGridCoords: 60 клеток по ширине, как в приложении', () => {
  const g = calib.toGridCoords([{ x: 0, y: 0 }, { x: 319.9, y: 179 }, { x: 160, y: 90 }, { x: 5.34, y: 5.34 }], 320);
  assert.deepEqual(g, [{ x: 0, y: 0 }, { x: 59, y: 33 }, { x: 30, y: 16 }, { x: 1, y: 1 }]);
});

test('analyzeVideo: чистая съёмка - все светодиоды найдены, ошибка меньше 1,2 пикселя', async () => {
  const v = makeVideo({ noise: 0, distract: false });
  const r = await calib.analyzeVideo(v.sample, v.duration, P);
  assert.equal(r.found, P.n);
  assert.ok(Math.abs(r.seqStart - v.seqStart) < 0.06, `старт серии ${r.seqStart.toFixed(3)} против ${v.seqStart.toFixed(3)}`);
  let worst = 0;
  r.points.forEach((p, i) => { worst = Math.max(worst, err(p, v.truth[i])); });
  assert.ok(worst < 1.2, `худшая ошибка ${worst.toFixed(2)} px`);
});

test('analyzeVideo: шум, статичная лампа, движущееся пятно и дрейф экспозиции', async () => {
  for (const seed of [1, 2, 3, 4]) {
    const v = makeVideo({ seed, noise: 3, drift: 0.12, t0: 0.5 + seed * 0.9 });
    const r = await calib.analyzeVideo(v.sample, v.duration, P);
    assert.ok(r.found >= P.n - 1, `seed ${seed}: найдено ${r.found} из ${P.n}`);
    let worst = 0;
    r.points.forEach((p, i) => { worst = Math.max(worst, err(p, v.truth[i])); });
    assert.ok(worst < 2.0, `seed ${seed}: худшая ошибка ${worst.toFixed(2)} px`);
  }
});

test('analyzeVideo: светодиоды за кадром оцениваются и помечаются, найденные точны', async () => {
  const outside = [0, 1, 2, 17, 18, 38, 39];
  const v = makeVideo({ truth: truthPositions(P.n, outside) });
  const r = await calib.analyzeVideo(v.sample, v.duration, P);
  assert.equal(r.found, P.n - outside.length);
  for (const i of outside) assert.ok(r.points[i].est, `светодиод ${i} должен быть помечен как оценка`);
  const mid = [3, 10, 25, 37];
  for (const i of mid) assert.ok(!r.points[i].est && err(r.points[i], v.truth[i]) < 1.5, `светодиод ${i}`);
  // оценка внутри цепочки лежит между соседями
  assert.ok(err(r.points[17], v.truth[17]) < 12, 'оценка недалеко от настоящего положения');
});

test('analyzeVideo: ошибки понятны - нет вспышки, слишком короткое видео, мало найдено, прерывание', async () => {
  const noMarker = makeVideo({ markerVisible: false, distract: false, noise: 0.5, truth: truthPositions(P.n, Array.from({ length: P.n }, (_, i) => i)) });
  await assert.rejects(calib.analyzeVideo(noMarker.sample, noMarker.duration, P), (e) => e instanceof calib.CalibError && /маркер/i.test(e.message));
  const v = makeVideo({});
  await assert.rejects(calib.analyzeVideo(v.sample, 3, P), (e) => /слишком короткое/.test(e.message));
  const dark = makeVideo({ truth: truthPositions(P.n, Array.from({ length: P.n }, (_, i) => i)), markerVisible: true });
  await assert.rejects(calib.analyzeVideo(dark.sample, dark.duration, P), calib.CalibError);   // ничего не светится в кадре
  const abort = { aborted: false };
  const w = makeVideo({});
  await assert.rejects(calib.analyzeVideo(w.sample, w.duration, P, (stage, done) => { if (stage === 'leds' && done > 5) abort.aborted = true; }, abort), /Прервано/);
});

test('analyzeVideo: чёрные первые кадры ролика не принимаются за маркер', async () => {
  const v = makeVideo({ noise: 1.5 });
  const blackStart = async (t) => (t < 0.06 ? { w: W, h: H, g: new Float32Array(W * H) } : v.sample(t));   // кадр 0 ещё не декодирован
  const r = await calib.analyzeVideo(blackStart, v.duration, P);
  assert.ok(Math.abs(r.seqStart - v.seqStart) < 0.06, `старт серии ${r.seqStart.toFixed(3)} против ${v.seqStart.toFixed(3)}`);
  assert.equal(r.found, P.n);
});

test('analyzeVideo: ошибка синхронизации до 40 мс не ломает привязку светодиодов к слотам', async () => {
  const v = makeVideo({ noise: 1, distract: false });
  const shifted = async (t) => v.sample(t - 0.04);   // «часы видео» отстают на 40 мс
  const r = await calib.analyzeVideo(shifted, v.duration + 0.04, P);
  let worst = 0;
  r.points.forEach((p, i) => { worst = Math.max(worst, err(p, v.truth[i])); });
  assert.ok(worst < 1.5, `худшая ошибка ${worst.toFixed(2)} px`);
});

test('analyzeVideo: длинная гирлянда (500 светодиодов) укладывается в время и точна', async () => {
  const big = { n: 500, step: 100, flash: 600, gap: 500, dark: 1500, marker: 25, delay: 3 };
  const truth = Array.from({ length: 500 }, (_, i) => ({ x: 20 + ((i * 37) % 280), y: 15 + Math.floor(i / 500 * 140) + (i % 7) * 2, visible: true }));
  // 500 светодиодов с пересекающимися положениями: у соседних по ленте положения разные - этого достаточно
  const flashStart = 3 + 1.5 + 0.7;
  const seqStart = flashStart + 1.1;
  const total = seqStart + 50 + 1.5;
  const markerIdx = Array.from({ length: 25 }, (_, k) => Math.floor((k * 500) / 25));
  const sample = async (t) => {
    const tq = Math.floor(Math.max(0, Math.min(total, t)) * FPS) / FPS;
    const g = new Float32Array(W * H).fill(22);
    const lit = [];
    if (tq >= flashStart && tq < flashStart + 0.6) lit.push(...markerIdx);
    if (tq >= seqStart && tq < seqStart + 50) lit.push(Math.floor((tq - seqStart) / 0.1));
    for (const i of lit) blob(g, truth[i].x, truth[i].y, 150, 2.2);
    return { w: W, h: H, g };
  };
  const t = Date.now();
  const r = await calib.analyzeVideo(sample, total, big);
  assert.equal(r.found, 500);
  let worst = 0;
  r.points.forEach((p, i) => { worst = Math.max(worst, err(p, truth[i])); });
  assert.ok(worst < 1.5, `худшая ошибка ${worst.toFixed(2)} px`);
  assert.ok(Date.now() - t < 20000, 'вычисления без перемотки укладываются в 20 с');
});

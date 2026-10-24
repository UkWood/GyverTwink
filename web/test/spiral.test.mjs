import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const app = require('../app.js');

// разбить точки схемы на ряды по их числу
function split(pts, rows) {
  const out = [];
  let i = 0;
  for (const n of rows) {
    out.push(pts.slice(i, i + n));
    i += n;
  }
  return out;
}
const xs = (row) => row.map((p) => p.x);
const mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;

test('спираль: число точек равно S, остаток образует верхний ряд', () => {
  const r = app.spiralLayout([12, 11, 12, 10], 50, 1);
  assert.equal(r.error, undefined);
  assert.equal(r.pts.length, 50);
  assert.deepEqual(r.rows, [12, 11, 12, 10, 5]);   // как на рисунке: последний ряд до S - остаток
  const exact = app.spiralLayout([4, 3, 3], 10, 1);
  assert.deepEqual(exact.rows, [4, 3, 3]);          // остатка нет: лишнего ряда нет
  assert.equal(exact.pts.length, 10);
  assert.deepEqual(app.spiralLayout([], 7, 1).rows, [7]);   // без рядов: вся лента одним рядом
});

test('спираль: каждый ряд центрирован, шаг светодиодов одинаков во всех рядах', () => {
  const r = app.spiralLayout([30, 28, 26, 24], 120, 1);
  const rows = split(r.pts, r.rows);
  let pitch = null;
  rows.forEach((row, k) => {
    if (row.length < 2) return;
    const x = xs(row);
    assert.ok(Math.abs(mean(x) - 127.5) <= 0.51, 'ряд ' + (k + 1) + ' по центру: среднее ' + mean(x));
    const steps = x.slice(1).map((v, i) => v - x[i]);
    const p = (x[x.length - 1] - x[0]) / (x.length - 1);
    pitch = pitch === null ? p : pitch;
    assert.ok(Math.abs(p - pitch) < 0.35, 'одинаковый шаг во всех рядах: ' + p + ' и ' + pitch);
    for (const s of steps) assert.ok(Math.abs(s - p) <= 1, 'равномерно внутри ряда');
    assert.ok(x.every((v, i) => i === 0 || v > x[i - 1]), 'слева направо');
  });
  // у конуса верхние ряды уже
  const widths = rows.map((row) => Math.max(...xs(row)) - Math.min(...xs(row)));
  for (let i = 1; i < widths.length; i++) assert.ok(widths[i] <= widths[i - 1] + 1, 'ряд ' + (i + 1) + ' не шире предыдущего');
});

test('спираль: ряды идут снизу вверх (у нижнего самый большой Y), точки ряда на одной высоте', () => {
  const r = app.spiralLayout([10, 10, 10, 10], 40, 1);
  const rows = split(r.pts, r.rows);
  const ys = rows.map((row) => {
    assert.equal(new Set(row.map((p) => p.y)).size, 1, 'ряд на одной высоте');
    return row[0].y;
  });
  for (let i = 1; i < ys.length; i++) assert.ok(ys[i] < ys[i - 1], 'следующий ряд выше (меньше Y)');
  const gaps = ys.slice(1).map((y, i) => ys[i] - y);
  for (const g of gaps) assert.ok(Math.abs(g - gaps[0]) <= 1, 'ряды через равные промежутки');
  // и порядок светодиодов по ленте: первый - в левом углу нижнего ряда, последний - в правом верхнего
  assert.ok(r.pts[0].y === Math.max(...r.pts.map((p) => p.y)) && r.pts[0].x === Math.min(...xs(rows[0])));
  assert.ok(r.pts[39].y === Math.min(...r.pts.map((p) => p.y)) && r.pts[39].x === Math.max(...xs(rows[3])));
});

test('спираль: пропорции сохраняются, шаг между рядами регулируется, схема в пределах 0..255', () => {
  const sq = app.spiralLayout([11, 11, 11, 11, 11], 55, 1);   // ширина 10 шагов, высота 4 шага
  const w = Math.max(...sq.pts.map((p) => p.x)) - Math.min(...sq.pts.map((p) => p.x));
  const h = Math.max(...sq.pts.map((p) => p.y)) - Math.min(...sq.pts.map((p) => p.y));
  assert.ok(Math.abs(w / 10 - h / 4) < 0.6, 'шаг по горизонтали равен шагу между рядами: ' + w / 10 + ' и ' + h / 4);
  const wide = app.spiralLayout([11, 11, 11, 11, 11], 55, 2);
  const h2 = Math.max(...wide.pts.map((p) => p.y)) - Math.min(...wide.pts.map((p) => p.y));
  const w2 = Math.max(...wide.pts.map((p) => p.x)) - Math.min(...wide.pts.map((p) => p.x));
  assert.ok(h2 > h * 1.6, 'при шаге 2 ряды дальше друг от друга');
  assert.ok(w2 <= w + 1 && w2 > 0);
  for (const r of [sq, wide, app.spiralLayout([2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2], 40, 1), app.spiralLayout([60], 60, 1)]) {
    for (const p of r.pts) assert.ok(p.x >= 0 && p.x <= 255 && p.y >= 0 && p.y <= 255 && Number.isInteger(p.x) && Number.isInteger(p.y));
    const minx = Math.min(...r.pts.map((p) => p.x)), maxx = Math.max(...r.pts.map((p) => p.x));
    const miny = Math.min(...r.pts.map((p) => p.y)), maxy = Math.max(...r.pts.map((p) => p.y));
    assert.ok(minx >= 7 && maxx <= 248 && miny >= 7 && maxy <= 248, 'отступ около 7 от краёв');
    assert.ok(Math.max(maxx - minx, maxy - miny) >= 238, 'схема занимает поле');
    assert.ok(Math.abs((minx + maxx) / 2 - 127.5) <= 1 && Math.abs((miny + maxy) / 2 - 127.5) <= 1, 'схема по центру карты');
  }
});

test('спираль: высокая схема ограничивается высотой, широкая - шириной', () => {
  const tall = app.spiralLayout(Array(30).fill(2), 61, 1);   // 31 ряд по 2 светодиода, узкая и высокая
  const ys = tall.pts.map((p) => p.y);
  assert.ok(Math.max(...ys) - Math.min(...ys) >= 238);
  const flat = app.spiralLayout([100], 101, 1);              // два ряда: ширина 99 шагов, высота 1
  const x = flat.pts.map((p) => p.x);
  assert.ok(Math.max(...x) - Math.min(...x) >= 238);
});

test('спираль: крайние случаи (один светодиод, одна колонка, один ряд)', () => {
  const one = app.spiralLayout([], 1, 1);
  assert.deepEqual(one.pts, [{ x: 128, y: 128 }]);          // 127.5 округляется вверх
  const col = app.spiralLayout([1, 1, 1], 4, 1);             // ширина 0: столбик
  assert.equal(new Set(col.pts.map((p) => p.x)).size, 1);
  assert.equal(col.pts[0].y > col.pts[3].y, true);
  const line = app.spiralLayout([], 20, 1);                  // один ряд: горизонтальная линия
  assert.equal(new Set(line.pts.map((p) => p.y)).size, 1);
  assert.equal(line.pts.length, 20);
  const max = app.spiralLayout([100, 100, 100, 100], 500, 1);
  assert.equal(max.pts.length, 500);
});

test('спираль: ошибки ввода понятны и ничего не строят', () => {
  const bad = [
    [[12, 11], 20, 1, /больше числа светодиодов/],        // 23 > 20
    [[0], 10, 1, /N1/],
    [[5, -3], 10, 1, /N2/],
    [[5, 2.5], 10, 1, /N2/],
    [[5, NaN], 10, 1, /N2/],
    [[3], 0, 1, /Нет светодиодов/],
    [[3], 10, 0.1, /Расстояние/],
    [[3], 10, 6, /Расстояние/],
    [[3], 10, NaN, /Расстояние/],
  ];
  for (const [counts, total, gap, re] of bad) {
    const r = app.spiralLayout(counts, total, gap);
    assert.match(r.error || '', re, JSON.stringify([counts, total, gap]));
    assert.equal(r.pts, undefined);
  }
  assert.equal(app.spiralLayout([20], 20, 1).error, undefined);   // сумма ровно S
});

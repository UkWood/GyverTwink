// Тесты чистых функций страницы (карта координат, имена): node --test web/test
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const app = require('../app.js');

test('hex <-> точки: туда и обратно, нули и 255', () => {
  const pts = [{ x: 0, y: 0 }, { x: 15, y: 16 }, { x: 255, y: 1 }, { x: 10, y: 200 }];
  const hex = app.pointsToHex(pts);
  assert.equal(hex, '00000f10ff010ac8');
  assert.deepEqual(app.hexToPoints(hex), pts);
  assert.deepEqual(app.hexToPoints(''), []);
  assert.deepEqual(app.hexToPoints('0a'), []);   // неполная запись отбрасывается
  assert.deepEqual(app.hexToPoints('0A14'), [{ x: 10, y: 20 }]);
});

test('fitView: карта помещается и центрируется, пропорции сохраняются', () => {
  const pts = [{ x: 10, y: 20 }, { x: 110, y: 70 }];   // 100 x 50
  const v = app.fitView(pts, 400, 300, 20);
  assert.equal(v.s, 3.6);   // ограничивает ширина: (400 - 40) / 100
  const a = app.toScreen(pts[0], v);
  const b = app.toScreen(pts[1], v);
  assert.ok(a.x >= 20 - 1e-9 && b.x <= 380 + 1e-9, 'по ширине в пределах отступов');
  assert.ok(a.y >= 0 && b.y <= 300);
  assert.ok(Math.abs((a.x + b.x) / 2 - 200) < 1e-9, 'по центру по x');
  assert.ok(Math.abs((a.y + b.y) / 2 - 150) < 1e-9, 'по центру по y');
  assert.ok(Math.abs((b.x - a.x) / (b.y - a.y) - 2) < 1e-9, 'пропорции 2:1');
});

test('fitView: вырожденные карты не дают NaN и бесконечностей', () => {
  for (const pts of [[], [{ x: 5, y: 5 }], [{ x: 5, y: 5 }, { x: 5, y: 5 }], [{ x: 0, y: 9 }, { x: 80, y: 9 }]]) {
    const v = app.fitView(pts, 300, 200, 10);
    assert.ok(Number.isFinite(v.s) && v.s > 0 && Number.isFinite(v.ox) && Number.isFinite(v.oy), JSON.stringify(pts));
  }
});

test('toWorld обратна toScreen и ограничивает 0..255', () => {
  const v = { s: 2.5, ox: 13, oy: -7 };
  for (const p of [{ x: 0, y: 0 }, { x: 100, y: 37 }, { x: 255, y: 255 }]) {
    assert.deepEqual(app.toWorld(...Object.values(app.toScreen(p, v)), v), p);
  }
  assert.deepEqual(app.toWorld(-1000, 5000, v), { x: 0, y: 255 });
  assert.deepEqual(app.toWorld(9999, -9999, v), { x: 255, y: 0 });
});

test('nearestPoint: в радиусе, за радиусом и при равных расстояниях', () => {
  const pts = [{ x: 10, y: 10 }, { x: 20, y: 10 }, { x: 15, y: 10 }];
  const v = { s: 10, ox: 0, oy: 0 };
  assert.equal(app.nearestPoint(pts, 101, 101, v, 24), 0);
  assert.equal(app.nearestPoint(pts, 150, 100, v, 24), 2);
  assert.equal(app.nearestPoint(pts, 125, 100, v, 24), -1);  // 25 px до обеих ближайших: дальше радиуса
  assert.equal(app.nearestPoint(pts, 125, 100, v, 30), 0);   // равные расстояния: выбирается меньший номер
  assert.equal(app.nearestPoint(pts, 400, 400, v, 24), -1);
  assert.equal(app.nearestPoint([], 1, 1, v, 24), -1);
});

test('suspiciousPoints: выброс найден, разрыв между секциями - нет', () => {
  const line = (n) => Array.from({ length: n }, (_, i) => ({ x: i * 5, y: 50 }));
  assert.deepEqual(app.suspiciousPoints(line(20), 4), []);
  const out = line(20);
  out[10] = { x: 50, y: 200 };   // точка улетела далеко от обоих соседей
  assert.deepEqual(app.suspiciousPoints(out, 4), [10]);
  const gap = line(10).concat(line(10).map((p) => ({ x: p.x + 500 % 256, y: p.y + 120 })));   // скачок между секциями
  assert.deepEqual(app.suspiciousPoints(gap, 4), []);
  const lastBad = line(12);
  lastBad[11] = { x: 250, y: 250 };   // крайняя точка далеко от единственного соседа
  assert.deepEqual(app.suspiciousPoints(lastBad, 4), [11]);
  assert.deepEqual(app.suspiciousPoints(line(3), 4), []);    // слишком мало точек
  assert.deepEqual(app.suspiciousPoints(Array(30).fill({ x: 7, y: 7 }), 4), []);   // все в одной точке (нет калибровки)
});

test('fitBytes: обрезка по границе символа UTF-8', () => {
  assert.equal(app.fitBytes('ВосемьБукв', 16), 'ВосемьБу');
  assert.equal(app.fitBytes('abc', 16), 'abc');
  assert.equal(app.fitBytes('abcdefghijklmn😀', 16), 'abcdefghijklmn');
  assert.equal(app.fitBytes('', 16), '');
});

test('describeError: понятные сообщения', () => {
  assert.match(app.describeError('bright', 400), /яркость/);
  assert.match(app.describeError('forbidden', 403), /отклонён/);
  assert.match(app.describeError('count', 409), /Число светодиодов изменилось/);
  assert.match(app.describeError('fs', 500), /флеш/);
  assert.equal(app.describeError('', 500), 'Ошибка 500');
});

// ---- масштаб карты: колесо мыши и два пальца ----
const near = (a, b, eps = 1e-6) => assert.ok(Math.abs(a - b) < eps, a + ' != ' + b);
const W = 400, H = 300;
const base = app.fitView([{ x: 0, y: 0 }, { x: 255, y: 255 }], W, H, 28);

test('zoomedView: масштаб 1 без сдвига - исходный вид, масштаб вокруг центра холста', () => {
  const v1 = app.zoomedView(base, 1, { x: 0, y: 0 }, W, H);
  assert.deepEqual(v1, base);
  const v3 = app.zoomedView(base, 3, { x: 0, y: 0 }, W, H);
  near(v3.s, base.s * 3);
  // центр холста остаётся на месте: точка, лежавшая в центре, и после увеличения в центре
  const cw = { x: (W / 2 - base.ox) / base.s, y: (H / 2 - base.oy) / base.s };
  near(app.toScreen(cw, v3).x, W / 2);
  near(app.toScreen(cw, v3).y, H / 2);
  const vp = app.zoomedView(base, 2, { x: 10, y: -5 }, W, H);
  near(vp.ox, app.zoomedView(base, 2, { x: 0, y: 0 }, W, H).ox + 10);
});

test('zoomAround: точка карты под курсором остаётся на месте', () => {
  for (const [z0, f, p] of [[1, 2, { x: 90, y: 40 }], [2, 1.5, { x: 300, y: 250 }], [4, 0.5, { x: 10, y: 10 }], [1.5, 3, { x: 200, y: 150 }]]) {
    const pan0 = z0 === 1 ? { x: 0, y: 0 } : { x: 17, y: -9 };
    const before = app.zoomedView(base, z0, pan0, W, H);
    const world = { x: (p.x - before.ox) / before.s, y: (p.y - before.oy) / before.s };   // без округления до целых, как у toWorld
    const r = app.zoomAround(z0, pan0, f, p, W, H);
    near(r.zoom, Math.max(1, Math.min(20, z0 * f)));
    if (r.zoom > 1) {
      const after = app.zoomedView(base, r.zoom, r.pan, W, H);
      const q = app.toScreen({ x: world.x, y: world.y }, after);
      assert.ok(Math.abs(q.x - p.x) < 1 && Math.abs(q.y - p.y) < 1, JSON.stringify({ z0, f, q }));
    }
  }
});

test('zoomAround: пределы 1 и 20, возврат к масштабу 1 сбрасывает сдвиг', () => {
  let r = app.zoomAround(15, { x: 5, y: 5 }, 10, { x: 100, y: 100 }, W, H);
  near(r.zoom, app.ZOOM_MAX);
  r = app.zoomAround(3, { x: 120, y: -80 }, 0.01, { x: 100, y: 100 }, W, H);
  assert.deepEqual(r, { zoom: 1, pan: { x: 0, y: 0 } });
  r = app.zoomAround(1, { x: 0, y: 0 }, 0.5, { x: 50, y: 50 }, W, H);   // дальше вписанного отдалять нельзя
  assert.deepEqual(r, { zoom: 1, pan: { x: 0, y: 0 } });
});

test('clampPan: карту нельзя утащить за пределы кадра дальше её размера', () => {
  assert.deepEqual(app.clampPan(2, { x: 0, y: 0 }, W, H), { x: 0, y: 0 });
  assert.deepEqual(app.clampPan(2, { x: 99999, y: -99999 }, W, H), { x: W, y: -H });
  assert.deepEqual(app.clampPan(4, { x: 10, y: 20 }, W, H), { x: 10, y: 20 });
});

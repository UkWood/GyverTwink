import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const hm = require('../hexmap.js');
const dir = path.dirname(fileURLToPath(import.meta.url));
const exampleText = fs.readFileSync(path.join(dir, 'fixtures/gyvertwink-map-example.json'), 'utf8');   // экспорт Designer на проекте заказчика
const example = JSON.parse(exampleText);

const make = (patch) => JSON.stringify(Object.assign({}, example, patch));

test('пример из Designer читается: 378 светодиодов по порядку, контур 54 ребра', () => {
  const r = hm.parseHexMap(exampleText, 1000);
  assert.equal(r.error, undefined);
  const m = r.map;
  assert.equal(m.count, 378);
  assert.equal(m.leds.length, 378);
  assert.deepEqual(m.leds[0], [507.2, 642.9]);       // порядок как в файле
  assert.deepEqual(m.leds[377], [705.2, 757.1]);
  assert.equal(m.outline.length, 54);
  assert.equal(m.widthMm, 866);
  assert.equal(m.heightMm, 800);
  assert.equal(m.name, 'Пример проект "Без названия"');
});

test('раскладка: порядок сохранён, всё в поле 0..255, пропорции и контур в том же преобразовании', () => {
  const m = hm.parseHexMap(exampleText).map;
  const L = hm.layoutHexMap(m);
  assert.equal(L.pts.length, 378);
  assert.ok(L.pts.every((p) => Number.isInteger(p.x) && Number.isInteger(p.y) && p.x >= 0 && p.x <= 255 && p.y >= 0 && p.y <= 255));
  const s = 240 / 866;   // длиннее сторона - ширина 866 мм: масштаб единиц карты на миллиметр
  assert.ok(Math.abs(L.scale - s) < 1e-9);
  // габариты в единицах карты: ширина 240, высота 800 * s, фигура по центру поля
  const xs = L.outline.flatMap((e) => [e[0], e[2]]);
  const ys = L.outline.flatMap((e) => [e[1], e[3]]);
  assert.ok(Math.abs(Math.max(...xs) - Math.min(...xs) - 240) < 0.3);
  assert.ok(Math.abs(Math.max(...ys) - Math.min(...ys) - 800 * s) < 0.3);
  assert.ok(Math.abs((Math.max(...xs) + Math.min(...xs)) / 2 - 127.5) < 0.3);
  assert.ok(Math.abs((Math.max(...ys) + Math.min(...ys)) / 2 - 127.5) < 0.3);
  // первый светодиод: (507.2 - 433) * s + 127.5 по X, (642.9 - 400) * s + 127.5 по Y; ось Y вниз сохранена
  assert.equal(L.pts[0].x, Math.round(127.5 + (507.2 - 433) * s));
  assert.equal(L.pts[0].y, Math.round(127.5 + (642.9 - 400) * s));
  assert.ok(L.pts[0].y > L.pts[200].y);   // светодиод 1 (START) ниже, чем 200-й, как в файле (Y вниз)
  assert.deepEqual(L.warnings, []);
});

test('светодиоды повторного прохода (сдвиг 1 мм в файле) не уезжают от первого прохода дальше чем на 1 единицу', () => {
  const L = hm.layoutHexMap(hm.parseHexMap(exampleText).map);
  // в примере светодиод 61 лежит рядом с 60-м (повторный проход ребра со сдвигом 1 мм)
  const a = L.pts[59], b = L.pts[60];
  assert.ok(Math.abs(a.x - b.x) <= 1 && Math.abs(a.y - b.y) <= 1);
});

test('чужие и повреждённые файлы отклоняются с понятным сообщением', () => {
  const bad = [
    ['не json', '{{{', /JSON/],
    ['массив', '[]', /JSON-объект/],
    ['нет метки формата', JSON.stringify({ leds: [[1, 2]], led_count: 1 }), /gyvertwink_map/],
    ['другая версия', make({ gyvertwink_map: 2 }), /Версия формата 2/],
    ['версия строкой', make({ gyvertwink_map: '1' }), /Версия формата 1/],
    ['не миллиметры', make({ unit: 'cm' }), /миллиметр/],
    ['нет leds', make({ leds: undefined, led_count: 0 }), /нет светодиодов/],
    ['пустой leds', make({ leds: [], led_count: 0 }), /нет светодиодов/],
    ['led_count не совпадает', make({ led_count: 377 }), /led_count/],
    ['led_count не число', make({ led_count: '378' }), /led_count/],
    ['точка не пара', make({ leds: [[1, 2, 3]], led_count: 1 }), /Светодиод 1/],
    ['точка с NaN', make({ leds: [[1, null]], led_count: 1 }), /Светодиод 1/],
    ['точка строкой', make({ leds: [['1', '2']], led_count: 1 }), /Светодиод 1/],
    ['outline не список', make({ outline: {} }), /outline/],
    ['отрезок из трёх чисел', make({ outline: [[1, 2, 3]] }), /Отрезок контура 1/],
    ['отрезок с текстом', make({ outline: [[1, 2, 3, 'x']] }), /Отрезок контура 1/],
  ];
  for (const [name, text, re] of bad) {
    const r = hm.parseHexMap(text, 1000);
    assert.ok(r.error && re.test(r.error), name + ': ' + r.error);
    assert.equal(r.map, undefined, name);
  }
});

test('больше предела прошивки и гигантский файл', () => {
  const r = hm.parseHexMap(exampleText, 300);
  assert.match(r.error, /378.*предел прошивки 300/);
  const many = Array.from({ length: 6000 }, (_, i) => [i, 0]);
  assert.match(hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 6000, leds: many })).error, /Слишком много/);
});

test('необязательные поля: без контура и без названия', () => {
  const r = hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 3, leds: [[0, 0], [10, 0], [20, 0]] }), 1000);
  assert.equal(r.error, undefined);
  assert.deepEqual(r.map.outline, []);
  assert.equal(r.map.name, '');
  const L = hm.layoutHexMap(r.map);
  assert.deepEqual(L.pts.map((p) => p.x), [8, 128, 248]);   // 240 единиц на 20 мм, по центру
  assert.ok(L.pts.every((p) => p.y === 128));               // нулевая высота: ряд по середине
  assert.deepEqual(L.outline, []);
});

test('один светодиод и совпадающие точки не ломают раскладку', () => {
  const one = hm.layoutHexMap(hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 1, leds: [[50, 60]] })).map);
  assert.deepEqual(one.pts, [{ x: 128, y: 128 }]);
  assert.equal(one.scale, 0);
  const same = hm.layoutHexMap(hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 2, leds: [[5, 5], [5, 5]] })).map);
  assert.equal(same.pts.length, 2);
  assert.deepEqual(same.warnings, []);
});

test('очень крупная схема: предупреждение о слипании точек', () => {
  // 20 светодиодов на 4 мм в схеме шириной 5000 мм: шаг после масштабирования около 0,02 единицы
  const leds = Array.from({ length: 20 }, (_, i) => [i * 4, 0]);
  leds.push([5000, 0]);
  const L = hm.layoutHexMap(hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 21, leds })).map);
  assert.equal(L.warnings.length, 1);
  assert.match(L.warnings[0], /слипаться/);
});

test('отрицательные и дробные координаты сдвигаются в поле без искажения', () => {
  const m = hm.parseHexMap(JSON.stringify({ gyvertwink_map: 1, unit: 'mm', led_count: 2, leds: [[-100.5, -40.25], [100.5, 40.25]] })).map;
  const L = hm.layoutHexMap(m);
  // масштаб 240 / 201 единиц на мм (длиннее сторона - ширина), центр схемы в (127.5; 127.5)
  assert.deepEqual(L.pts[0], { x: 8, y: 79 });
  assert.deepEqual(L.pts[1], { x: 248, y: 176 });
  assert.ok(L.pts.every((p) => p.x >= 0 && p.x <= 255 && p.y >= 0 && p.y <= 255));
});

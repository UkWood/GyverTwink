'use strict';
// Схема «соты»: файл gyvertwink-map.json из Hex Router Designer (формат 1) -> точки карты гирлянды.
// Здесь только вычисления без DOM (тесты: web/test/hexmap.test.mjs). Маршрут и положение светодиодов считает Designer,
// страница ничего не пересчитывает: берёт список светодиодов строго в порядке ленты и вписывает в сетку 256 x 256.
//
// Формат файла (версия 1):
//   gyvertwink_map: 1, unit: "mm", project: {name}, led_count, width_mm, height_mm, start: {x, y}, end: {x, y},
//   leds: [[x, y], ...]            все светодиоды по порядку ленты, мм «как на экране Designer» (Y вниз)
//   outline: [[x1, y1, x2, y2], ...]  каждое ребро схемы один раз (подложка для рисования, необязательно)

const HEXMAP_FORMAT = 1;
const HEXMAP_AREA = 240;        // как у спирали: поле 0..255 с отступом около 7 по краям
const HEXMAP_HARD_LIMIT = 5000; // защита от гигантского файла (предел прошивки меньше)
const HEXMAP_MIN_STEP = 1.5;    // меньше этого шага между светодиодами (единиц карты) точки сливаются

const hmFinite = (v) => typeof v === 'number' && Number.isFinite(v);
const hmRound1 = (v) => Math.round(v * 10) / 10;

// text -> {map} или {error}. limit - предел светодиодов прошивки (для понятного сообщения)
function parseHexMap(text, limit) {
  let d;
  try {
    d = JSON.parse(text);
  } catch (e) {
    return { error: 'Файл не читается как JSON: ' + e.message };
  }
  if (!d || typeof d !== 'object' || Array.isArray(d)) return { error: 'В файле должен быть JSON-объект.' };
  if (d.gyvertwink_map === undefined) {
    return { error: 'Это не файл экспорта для GyverTwink (нет поля gyvertwink_map). В Hex Router Designer нажми «Экспорт для GyverTwink».' };
  }
  if (d.gyvertwink_map !== HEXMAP_FORMAT) {
    return { error: 'Версия формата ' + String(d.gyvertwink_map) + ' не поддерживается (нужна ' + HEXMAP_FORMAT + '). Обнови страницу гирлянды или Designer.' };
  }
  if (d.unit !== 'mm') return { error: 'Единицы измерения должны быть миллиметрами (unit: "mm").' };
  if (!Array.isArray(d.leds) || d.leds.length < 1) return { error: 'В файле нет светодиодов (leds).' };
  if (d.leds.length > HEXMAP_HARD_LIMIT) return { error: 'Слишком много светодиодов в файле: ' + d.leds.length + '.' };
  if (limit && d.leds.length > limit) {
    return { error: 'В схеме ' + d.leds.length + ' светодиодов, а предел прошивки ' + limit + '.' };
  }
  if (!Number.isInteger(d.led_count) || d.led_count !== d.leds.length) {
    return { error: 'led_count (' + String(d.led_count) + ') не совпадает с числом точек в leds (' + d.leds.length + '): файл повреждён.' };
  }
  for (let i = 0; i < d.leds.length; i++) {
    const p = d.leds[i];
    if (!Array.isArray(p) || p.length !== 2 || !hmFinite(p[0]) || !hmFinite(p[1])) {
      return { error: 'Светодиод ' + (i + 1) + ': нужны два числа [x, y] в миллиметрах.' };
    }
  }
  let outline = [];
  if (d.outline !== undefined) {
    if (!Array.isArray(d.outline) || d.outline.length > HEXMAP_HARD_LIMIT) return { error: 'outline должен быть списком отрезков [x1, y1, x2, y2].' };
    for (let i = 0; i < d.outline.length; i++) {
      const s = d.outline[i];
      if (!Array.isArray(s) || s.length !== 4 || !s.every(hmFinite)) return { error: 'Отрезок контура ' + (i + 1) + ': нужны четыре числа [x1, y1, x2, y2].' };
    }
    outline = d.outline.map((s) => s.slice());
  }
  const leds = d.leds.map((p) => [p[0], p[1]]);
  const b = hexMapBounds(leds, outline);
  const name = d.project && typeof d.project.name === 'string' ? d.project.name : '';
  return { map: { name, count: leds.length, leds, outline, widthMm: hmRound1(b.maxX - b.minX), heightMm: hmRound1(b.maxY - b.minY) } };
}

// габариты светодиодов и контура
function hexMapBounds(leds, outline) {
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  const add = (x, y) => {
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
  };
  for (const p of leds) add(p[0], p[1]);
  for (const s of outline) { add(s[0], s[1]); add(s[2], s[3]); }
  return { minX, maxX, minY, maxY };
}

// map -> {pts, outline, scale, warnings}: точки карты 0..255 в том же порядке, контур в тех же единицах (для рисования).
// Одно преобразование для всего: масштаб одинаковый по осям (пропорции схемы сохраняются), фигура по центру поля, ось Y вниз как в файле.
function layoutHexMap(map) {
  const b = hexMapBounds(map.leds, map.outline);
  const w = b.maxX - b.minX;
  const h = b.maxY - b.minY;
  const span = Math.max(w, h);
  const s = span > 0 ? HEXMAP_AREA / span : 0;   // единиц карты на миллиметр (один светодиод - все точки в центре)
  const cx = (b.minX + b.maxX) / 2;
  const cy = (b.minY + b.maxY) / 2;
  const tx = (x) => 127.5 + (x - cx) * s;
  const ty = (y) => 127.5 + (y - cy) * s;
  const clamp = (v) => Math.max(0, Math.min(255, v));
  const pts = map.leds.map((p) => ({ x: clamp(Math.round(tx(p[0]))), y: clamp(Math.round(ty(p[1]))) }));
  const outline = map.outline.map((e) => [hmRound1(tx(e[0])), hmRound1(ty(e[1])), hmRound1(tx(e[2])), hmRound1(ty(e[3]))]);
  const warnings = [];
  const steps = [];
  for (let i = 1; i < map.leds.length; i++) {
    const dd = Math.hypot(map.leds[i][0] - map.leds[i - 1][0], map.leds[i][1] - map.leds[i - 1][1]);
    if (dd > 0) steps.push(dd);
  }
  if (steps.length) {
    steps.sort((a, c) => a - c);
    const med = steps[Math.floor(steps.length / 2)] * s;
    if (med < HEXMAP_MIN_STEP) {
      warnings.push('Схема очень крупная для сетки 256 × 256: шаг между светодиодами меньше ' + HEXMAP_MIN_STEP + ' единицы, соседние точки могут слипаться.');
    }
  }
  return { pts, outline, scale: s, warnings };
}

// контур после переворота карты по вертикали: та же ось зеркала (lo + hi - y), что у точек (flipVertical в app.js)
function flipOutline(outline, lo, hi) {
  return outline.map((e) => [e[0], hmRound1(lo + hi - e[1]), e[2], hmRound1(lo + hi - e[3])]);
}

if (typeof module !== 'undefined') {
  module.exports = { parseHexMap, layoutHexMap, hexMapBounds, flipOutline, HEXMAP_FORMAT, HEXMAP_AREA, HEXMAP_MIN_STEP };
}

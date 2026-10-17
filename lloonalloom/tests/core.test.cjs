const { test } = require('node:test');
const assert = require('node:assert/strict');
const Lamp = require('../web/core.js');

test('RGBW and per-pixel fire flags survive the existing wire format', () => {
  const scene = Array.from({ length: 144 }, (_, i) => ({ r: i, g: 255 - i, b: i % 2 ? 0 : 255, w: i % 17, f: i % 3 === 0 }));
  const wire = Lamp.encode(scene);
  assert.equal(wire.ext.length, 648);
  assert.equal(wire.int.length, 648);
  assert.deepEqual(Lamp.decode(wire), scene);
});
test('malformed or truncated scenes are rejected before use', () => {
  const valid = Lamp.encode(Lamp.blank());
  for (const ext of ['', valid.ext.slice(9), valid.ext + '0', 'GG' + valid.ext.slice(2), valid.ext.slice(0, -1) + '2']) {
    assert.throws(() => Lamp.decode({ ...valid, ext }));
  }
  assert.throws(() => Lamp.decode({ ext: valid.ext }));
});
test('fast strokes and wide brushes wrap continuously across 12 o’clock', () => {
  assert.deepEqual(Lamp.arcIndices(70, 2), [70, 71, 0, 1, 2]);
  assert.deepEqual(Lamp.arcIndices(2, 70), [2, 1, 0, 71, 70]);
  assert.deepEqual(Lamp.brushIndices(0, 3), [71, 0, 1]);
});
test('eyedropper preserves exact low RGB values through HSV conversion', () => {
  for (let r = 0; r < 256; r += 7) for (let g = 0; g < 256; g += 11) for (let b = 0; b < 256; b += 13) {
    const hsv = Lamp.toHSV({ r, g, b });
    assert.deepEqual(Lamp.hsv(hsv.h, hsv.s, hsv.v), { r, g, b });
  }
});
test('draft copies cannot mutate presets, and blank LEDs are independent', () => {
  const blank = Lamp.blank();
  const copy = Lamp.clone(blank);
  copy[0].w = 99;
  assert.equal(blank[0].w, 0);
  blank[0].g = 55;
  assert.equal(blank[1].g, 0);
});

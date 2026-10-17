/* Pure colour/scene operations. No DOM, networking or hardware assumptions beyond
   the existing 72 + 72 RGBW/F wire format. Shared by the UI and regression tests. */
const Lamp = (() => {
  'use strict';
  const COUNT = 72;
  const black = () => ({ r: 0, g: 0, b: 0, w: 0, f: false });
  const blank = () => Array.from({ length: COUNT * 2 }, black);
  const clone = scene => scene.map(pixel => ({ ...pixel }));
  const byte = n => Math.max(0, Math.min(255, Math.round(n)));
  const hex = n => byte(n).toString(16).padStart(2, '0').toUpperCase();
  const rgbHex = c => '#' + hex(c.r) + hex(c.g) + hex(c.b);
  function encode(scene) {
    if (scene.length !== COUNT * 2) throw new Error('Escena incompleta.');
    const pack = pixels => pixels.map(c => hex(c.r) + hex(c.g) + hex(c.b) + hex(c.w) + (c.f ? '1' : '0')).join('');
    return { ext: pack(scene.slice(0, COUNT)), int: pack(scene.slice(COUNT)) };
  }
  function decode(data) {
    const unpack = value => {
      if (typeof value !== 'string' || value.length !== COUNT * 9 || !/^(?:[0-9a-f]{8}[01])+$/i.test(value)) throw new Error('L’escena conté dades invàlides.');
      return Array.from({ length: COUNT }, (_, i) => {
        const valueAt = channel => parseInt(value.slice(i * 9 + channel * 2, i * 9 + channel * 2 + 2), 16);
        return { r: valueAt(0), g: valueAt(1), b: valueAt(2), w: valueAt(3), f: value[i * 9 + 8] === '1' };
      });
    };
    return [...unpack(data.ext), ...unpack(data.int)];
  }
  function hsv(h, s, v) {
    h = ((h % 360) + 360) % 360;
    const f = n => { const k = (n + h / 60) % 6; return byte(v * (1 - s * Math.max(0, Math.min(k, 4 - k, 1)))); };
    return { r: f(5), g: f(3), b: f(1) };
  }
  function toHSV(c) {
    const max = Math.max(c.r, c.g, c.b), min = Math.min(c.r, c.g, c.b), d = max - min;
    let h = 0;
    if (d) h = (max === c.r ? (c.g - c.b) / d : max === c.g ? (c.b - c.r) / d + 2 : (c.r - c.g) / d + 4) * 60;
    return { h: (h + 360) % 360, s: max ? d / max : 0, v: max };
  }
  // Lift very dim LEDs for legibility, preserving hue instead of clipping every
  // channel separately. This is a digital approximation, not colour calibration.
  function displayColor(c, flicker = 1) {
    const channels = [c.r + c.w, c.g + c.w, c.b + c.w];
    const peak = Math.max(...channels);
    if (!peak) return '#d4d8cc';
    const brightness = (100 + 155 * Math.sqrt(Math.min(peak, 255) / 255)) * flicker;
    return `rgb(${channels.map(n => byte(n / peak * brightness)).join(',')})`;
  }
  function arcIndices(from, to, count = COUNT) {
    let distance = (to - from + count) % count;
    if (distance > count / 2) distance -= count;
    const step = Math.sign(distance);
    return Array.from({ length: Math.abs(distance) + 1 }, (_, n) => (from + step * n + count) % count);
  }
  function brushIndices(center, width, count = COUNT) {
    const half = Math.floor(width / 2);
    return Array.from({ length: width }, (_, n) => (center - half + n + count) % count);
  }
  function signature(scene) { const packed = encode(scene); return packed.ext + packed.int; }
  return { COUNT, blank, clone, byte, rgbHex, encode, decode, hsv, toHSV, displayColor, arcIndices, brushIndices, signature };
})();
if (typeof module !== 'undefined') module.exports = Lamp;

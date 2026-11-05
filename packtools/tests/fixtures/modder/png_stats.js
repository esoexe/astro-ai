// png_stats.js - facts about an 8-bit RGBA PNG for the modder tests: size, covered pixels and their box, alpha/rgb
// extremes, and the centroid of reddish pixels (where a model's red nose points), and the RGBA of any X,Y given.   node png_stats.js FILE.png [X,Y ...]
'use strict';
const fs = require('fs'), zlib = require('zlib');
const d = fs.readFileSync(process.argv[2]);
let p = 8, w = 0, h = 0, ct = 0, bd = 0; const idat = [];
while (p < d.length) { const len = d.readUInt32BE(p), type = d.toString('latin1', p + 4, p + 8), body = d.subarray(p + 8, p + 8 + len); if (type === 'IHDR') { w = body.readUInt32BE(0); h = body.readUInt32BE(4); bd = body[8]; ct = body[9]; } else if (type === 'IDAT') idat.push(body); p += 12 + len; }
if (bd !== 8 || ![0, 2, 4, 6].includes(ct)) throw new Error('only 8-bit gray, gray+alpha, RGB, RGBA');
const bpp = { 0: 1, 4: 2, 2: 3, 6: 4 }[ct], raw = zlib.inflateSync(Buffer.concat(idat)), px = Buffer.alloc(w * h * 4), stride = w * bpp; let prev = Buffer.alloc(stride);
for (let y = 0; y < h; y++) {
  const ft = raw[y * (stride + 1)], cur = Buffer.from(raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
  for (let x = 0; x < stride; x++) { const a = x >= bpp ? cur[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0; let v = cur[x];
    if (ft === 1) v += a; else if (ft === 2) v += b; else if (ft === 3) v += (a + b) >> 1; else if (ft === 4) { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
    cur[x] = v & 255; }
  for (let x = 0; x < w; x++) { const gray = bpp <= 2; for (let k = 0; k < 3; k++) px[(y * w + x) * 4 + k] = cur[x * bpp + (gray ? 0 : k)]; px[(y * w + x) * 4 + 3] = bpp === 4 ? cur[x * bpp + 3] : bpp === 2 ? cur[x * bpp + 1] : 255; }
  prev = cur;
}
let n = 0, x0 = w, y0 = h, x1 = -1, y1 = -1, maxrgb = 0, minA = 255, maxA = 0, rn = 0, rx = 0, ry = 0;
for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const i = (y * w + x) * 4, a = px[i + 3]; minA = Math.min(minA, a); maxA = Math.max(maxA, a);
  if (a > 0) { n++; x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); maxrgb = Math.max(maxrgb, px[i], px[i + 1], px[i + 2]);
    if (a > 128 && px[i] > 150 && px[i] > 1.8 * px[i + 1] && px[i] > 1.8 * px[i + 2]) { rn++; rx += x; ry += y; } } }
const pick = {}; for (const xy of process.argv.slice(3)) { const [x, y] = xy.split(',').map(Number); const i = (y * w + x) * 4; pick[xy] = [px[i], px[i + 1], px[i + 2], px[i + 3]]; }
console.log(JSON.stringify({ px: pick, w, h, covered: n, bbox: n ? [x0, y0, x1 + 1, y1 + 1] : null, max_rgb: maxrgb, min_alpha: minA, max_alpha: maxA, red: rn, red_cx: rn ? +(rx / rn).toFixed(1) : null, red_cy: rn ? +(ry / rn).toFixed(1) : null }));

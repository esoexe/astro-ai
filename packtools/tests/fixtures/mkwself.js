// The make_elf(text, data) ELFs of console-skill/tests/test_graphics.py (widescreen part), byte for byte: two
// segments (.text R-X at 0x00100000, .data RW- at 0x00200000), no section headers.
// usage: node mkwself.js OUT "TEXT WORDS" "DATA WORDS"   (hex words; f:EXPR is the float32 of a JS expression, e.g. f:4/3)
const fs = require('fs');
const word = (t) => { if (t.startsWith('f:')) { const b = Buffer.alloc(4); b.writeFloatLE(Function('return (' + t.slice(2) + ')')(), 0); return b.readUInt32LE(0); } return parseInt(t, 16) >>> 0; };
const words = (s) => { const ws = s.trim() ? s.trim().split(/\s+/).map(word) : []; const b = Buffer.alloc(4 * ws.length); ws.forEach((w, i) => b.writeUInt32LE(w, 4 * i)); return b; };
const [out, text, data] = process.argv.slice(2);
const segs = [[0x00100000, words(text), 5], [0x00200000, words(data), 6]];
const off = 52 + 32 * segs.length; let body = Buffer.alloc(0); const placed = [];
for (const [, blob] of segs) { body = Buffer.concat([body, Buffer.alloc(((-(off + body.length)) % 16 + 16) % 16)]); placed.push(off + body.length); body = Buffer.concat([body, blob]); }
const hdr = Buffer.alloc(52); Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 1, 1, 0]).copy(hdr, 0);
[[16, 2, 2], [18, 8, 2], [20, 1, 4], [24, 0x00100000, 4], [28, 52, 4], [32, 0, 4], [36, 0, 4], [40, 52, 2], [42, 32, 2], [44, segs.length, 2], [46, 40, 2], [48, 0, 2], [50, 0, 2]]
  .forEach(([at, v, n]) => n === 2 ? hdr.writeUInt16LE(v, at) : hdr.writeUInt32LE(v, at));
const ph = Buffer.concat(segs.map(([v, b, fl], i) => { const p = Buffer.alloc(32); [1, placed[i], v, v, b.length, b.length, fl, 16].forEach((x, k) => p.writeUInt32LE(x >>> 0, 4 * k)); return p; }));
fs.writeFileSync(out, Buffer.concat([hdr, ph, body]));

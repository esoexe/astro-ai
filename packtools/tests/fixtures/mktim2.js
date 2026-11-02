// Fixtures for console-graphics/tim2: the synthetic TIM2 builder of console-skill/tests/test_graphics.py (byte for
// byte), plus an independent PNG encoder/decoder (node's zlib) and TIM2 picture decoder used as the test oracle.
//   node mktim2.js tm2 OUT [--align1] PIC...      PIC: pic32 pic24 pic16 pic16n pic8 pic8l pic4 pic4x2 pic8mips slot8
//   node mktim2.js pngdecode FILE                 -> {"w","h","rgba":[[r,g,b,a]..],"idx":[..]|null,"pal":[[..]]|null}
//   node mktim2.js pngencode OUT JSON             JSON {"w","h","rgba"} or {"w","h","idx","pal"}
//   node mktim2.js pngraw OUT CASE                decoder cases: filters grey greyalpha rgbtrns lowdepth2 lowdepth4
//                                                 notpng crc interlaced 16bit truncated badfilter noplte badindex colortype
//   node mktim2.js decode FILE [PIC] [LEVEL] [PAL] -> the picture's colours as JSON (oracle for rebuilt files)
//   node mktim2.js container OUT                  -> archive.bin (junk, a fake TIM2, two real ones); prints offsets
const fs = require('fs'); const zlib = require('zlib');
const u16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); return b; };
const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
const u64 = (v) => { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(v)); return b; };
const TEX0 = (0x13n << 20n) | (4n << 26n) | (4n << 30n) | (2n << 14n) | (5n << 37n);
const swizzle = (i) => { const m = i & 0x1F; return m >= 8 && m < 16 ? i + 8 : m >= 16 && m < 24 ? i - 8 : i; };
function picture(w, h, type, image, clut = Buffer.alloc(0), clutType = 0, clutColors = 0, mips = 1, mipSizes = null, tex0 = TEX0) {
  let extra = Buffer.alloc(0);
  if (mips > 1) extra = Buffer.concat([u64(0), u64(0), ...mipSizes.map(u32)]);
  extra = Buffer.concat([extra, Buffer.alloc(((-(0x30 + extra.length)) % 16 + 16) % 16)]);
  const hs = 0x30 + extra.length, total = hs + image.length + clut.length;
  const ph = Buffer.concat([u32(total), u32(clut.length), u32(image.length), u16(hs), u16(clutColors), Buffer.from([0, mips, clutType, type]), u16(w), u16(h),
    u64(tex0), u64(0x60), u32(0), u32(0), extra]);
  return Buffer.concat([ph, image, clut]);
}
function tim2File(pics, fmtId = 0, version = 4) {
  let head = Buffer.concat([Buffer.from('TIM2'), Buffer.from([version, fmtId]), u16(pics.length), Buffer.alloc(8)]);
  if (fmtId === 1) head = Buffer.concat([head, Buffer.alloc(0x80 - 16)]);
  return Buffer.concat([head, ...pics]);
}
const logical = (k) => [k, 255 - k, (k * 7) & 0xFF, 0x80];
const rgba32 = (cs) => Buffer.from(cs.flat());
const PICS = {
  pic32: () => picture(4, 2, 3, rgba32([[255, 0, 0, 0x80], [0, 255, 0, 0x40], [0, 0, 255, 0], [10, 20, 30, 0xFF], [1, 2, 3, 0x80], [4, 5, 6, 0x80], [7, 8, 9, 0x20], [200, 100, 50, 0x80]])),
  pic24: () => picture(3, 2, 2, Buffer.from([...Array(18).keys()])),
  pic16: (alpha = true) => { let v = [0x801F, 0x83E0, 0xFC00, 0x7FFF, 0x0000, 0x8000, 0x4210, 0xC631]; if (!alpha) v = v.map((x) => x & 0x7FFF); return picture(4, 2, 1, Buffer.concat(v.map(u16))); },
  pic16n: () => PICS.pic16(false),
  pic8: (sw = true, w = 16, h = 16) => {
    const idx = Buffer.from([...Array(w * h).keys()].map((i) => (i * 37 + 11) & 0xFF));
    const lg = [...Array(256).keys()].map(logical);
    return sw ? picture(w, h, 5, idx, rgba32([...Array(256).keys()].map((s) => lg[swizzle(s)])), 0x03, 256) : picture(w, h, 5, idx, rgba32(lg), 0x83, 256);
  },
  pic8l: () => PICS.pic8(false),
  pic4: (palettes = 1) => {
    const w = 8, h = 4, idx = []; for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) idx.push((x + y * 3) & 15);
    const image = []; for (let i = 0; i < idx.length; i += 2) image.push(idx[i] | (idx[i + 1] << 4));
    const pal = []; for (let p = 0; p < palettes; p++) for (let k = 0; k < 16; k++) pal.push(((k * 2 + p) & 31) | (((31 - k) & 31) << 5) | (((k + 5 * p) & 31) << 10) | 0x8000);
    return picture(w, h, 4, Buffer.from(image), Buffer.concat(pal.map(u16)), 0x01, 16 * palettes);
  },
  pic4x2: () => PICS.pic4(2),
  pic8mips: () => {
    const l0 = [], l1 = []; for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) l0.push((x ^ y) & 0xFF);
    for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) l1.push((x * 3) & 0xFF);
    return picture(8, 8, 5, Buffer.from([...l0, ...l1]), rgba32([...Array(256).keys()].map(logical)), 0x83, 256, 2, [64, 16]);
  },
  slot8: () => {   // 8x8 using logical entries 0..3 only (free-slot test)
    const lg = [...Array(256).keys()].map(logical);
    return picture(8, 8, 5, Buffer.from([...Array(64).keys()].map((i) => i % 4)), rgba32([...Array(256).keys()].map((s) => lg[swizzle(s)])), 0x03, 256);
  },
};
// ---- PNG
const crcT = (() => { const t = []; for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t.push(c >>> 0); } return t; })();
const crc32 = (b) => { let c = 0xFFFFFFFF; for (const x of b) c = crcT[(c ^ x) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
const be32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32BE(v >>> 0); return b; };
const SIG = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
const chunk = (kind, p) => { const k = Buffer.from(kind, 'latin1'); return Buffer.concat([be32(p.length), k, p, be32(crc32(Buffer.concat([k, p])))]); };
const ihdr = (w, h, depth, ctype, interlace = 0) => Buffer.concat([be32(w), be32(h), Buffer.from([depth, ctype, 0, 0, interlace])]);
function encodePng(j) {
  const rows = [];
  if (j.idx) {
    for (let y = 0; y < j.h; y++) rows.push(0, ...j.idx.slice(y * j.w, (y + 1) * j.w));
    let extra = chunk('PLTE', Buffer.from(j.pal.map((c) => c.slice(0, 3)).flat()));
    const al = j.pal.map((c) => c[3]); let last = -1; al.forEach((a, i) => { if (a !== 255) last = i; });
    if (last >= 0) extra = Buffer.concat([extra, chunk('tRNS', Buffer.from(al.slice(0, last + 1)))]);
    return Buffer.concat([SIG, chunk('IHDR', ihdr(j.w, j.h, 8, 3)), extra, chunk('IDAT', zlib.deflateSync(Buffer.from(rows))), chunk('IEND', Buffer.alloc(0))]);
  }
  const hasA = j.rgba.some((c) => c[3] !== 255), n = hasA ? 4 : 3;
  for (let y = 0; y < j.h; y++) { rows.push(0); for (const c of j.rgba.slice(y * j.w, (y + 1) * j.w)) rows.push(...c.slice(0, n)); }
  return Buffer.concat([SIG, chunk('IHDR', ihdr(j.w, j.h, 8, hasA ? 6 : 2)), chunk('IDAT', zlib.deflateSync(Buffer.from(rows))), chunk('IEND', Buffer.alloc(0))]);
}
const paeth = (a, b, c) => { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; };
function decodePng(blob) {   // 8-bit (and low-depth palette) non-interlaced, enough for what tim2 writes
  let pos = 8, H, plte = null, trns = null; const idat = [];
  while (pos + 8 <= blob.length) {
    const len = blob.readUInt32BE(pos), kind = blob.toString('latin1', pos + 4, pos + 8), p = blob.subarray(pos + 8, pos + 8 + len);
    if (crc32(blob.subarray(pos + 4, pos + 8 + len)) !== blob.readUInt32BE(pos + 8 + len)) throw new Error('bad crc ' + kind);
    if (kind === 'IHDR') H = { w: p.readUInt32BE(0), h: p.readUInt32BE(4), depth: p[8], ctype: p[9] };
    else if (kind === 'PLTE') plte = p; else if (kind === 'tRNS') trns = p; else if (kind === 'IDAT') idat.push(p); else if (kind === 'IEND') break;
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const ch = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[H.ctype], stride = Math.ceil(H.w * ch * H.depth / 8), bpp = Math.max(1, ch * H.depth / 8 | 0);
  let prev = Buffer.alloc(stride); const rows = [];
  for (let y = 0; y < H.h; y++) {
    const ft = raw[y * (stride + 1)], cur = Buffer.from(raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      cur[i] = (cur[i] + [0, a, b, (a + b) >> 1, paeth(a, b, c)][ft]) & 0xFF;
    }
    rows.push(cur); prev = cur;
  }
  const o = { w: H.w, h: H.h, ctype: H.ctype, rgba: [], idx: null, pal: null };
  if (H.ctype === 3) {
    o.pal = []; for (let i = 0; i + 2 < plte.length; i += 3) o.pal.push([plte[i], plte[i + 1], plte[i + 2], trns && i / 3 < trns.length ? trns[i / 3] : 255]);
    o.idx = []; const per = 8 / H.depth, mask = (1 << H.depth) - 1;
    for (const r of rows) for (let x = 0; x < H.w; x++) o.idx.push(H.depth === 8 ? r[x] : (r[Math.floor(x / per)] >> (8 - H.depth * (x % per + 1))) & mask);
    o.rgba = o.idx.map((i) => o.pal[i]);
  } else for (const r of rows) for (let x = 0; x < H.w; x++) { const p = [...r.subarray(x * ch, (x + 1) * ch)]; o.rgba.push(ch === 4 ? p : ch === 3 ? [...p, 255] : ch === 2 ? [p[0], p[0], p[0], p[1]] : [p[0], p[0], p[0], 255]); }
  return o;
}
function pngRaw(w, h, ctype, depth, rows, filt = null, interlace = 0, extra = Buffer.alloc(0), plte = null) {
  const body = []; rows.forEach((r, y) => body.push(filt ? filt[y] : 0, ...r));
  return Buffer.concat([SIG, chunk('IHDR', ihdr(w, h, depth, ctype, interlace)), plte ? chunk('PLTE', plte) : Buffer.alloc(0), extra,
    chunk('IDAT', zlib.deflateSync(Buffer.from(body))), chunk('IEND', Buffer.alloc(0))]);
}
// ---- independent TIM2 picture decoder (oracle)
function decodeTim2(d, pi = 0, level = 0, which = 0) {
  const fmtId = d[5], count = d.readUInt16LE(6); let pos = fmtId === 1 ? 0x80 : 0x10;
  for (let n = 0; n < count; n++) {
    const total = d.readUInt32LE(pos);
    if (n === pi) {
      const clutSize = d.readUInt32LE(pos + 4), imageSize = d.readUInt32LE(pos + 8), hs = d.readUInt16LE(pos + 12), cc = d.readUInt16LE(pos + 14);
      const mips = Math.max(1, d[pos + 17]), ct = d[pos + 18], type = d[pos + 19], W = d.readUInt16LE(pos + 20), Hh = d.readUInt16LE(pos + 22);
      const bpp = { 1: 16, 2: 24, 3: 32, 4: 4, 5: 8 }[type]; let off = pos + hs;
      const sized = mips > 1 && hs >= 0x30 + 16 + 4 * mips; let w = W, h = Hh, size = 0;
      for (let k = 0; k <= level; k++) { w = Math.max(1, W >> k); h = Math.max(1, Hh >> k); const need = Math.ceil(w * h * bpp / 8); size = sized ? d.readUInt32LE(pos + 0x40 + 4 * k) : need; if (k < level) off += size; }
      const row = Math.ceil(w * bpp / 8), stride = size >= row * h ? row : null;
      const exp5 = (x) => (x << 3) | (x >> 2), a2p = (a) => a >= 128 ? 255 : Math.floor((a * 255 + 64) / 128);
      const col = (buf, at, f, au) => f === 32 ? [buf[at], buf[at + 1], buf[at + 2], a2p(buf[at + 3])] : f === 24 ? [buf[at], buf[at + 1], buf[at + 2], 255]
        : ((v) => [exp5(v & 31), exp5((v >> 5) & 31), exp5((v >> 10) & 31), au || v & 0x8000 ? 255 : 0])(buf.readUInt16LE(at));
      const px = [];
      if (type === 4 || type === 5) {
        const cbpp = { 1: 16, 2: 24, 3: 32 }[ct & 0x3F], es = cbpp / 8, ps = type === 5 ? 256 : 16, sw = type === 5 ? !(ct & 0x80) : !!(ct & 0x40);
        const n2 = Math.min(ps, cc - which * ps), start = pos + hs + imageSize + which * ps * es;
        let au = false; if (cbpp === 16) { au = true; for (let i = 0; i < n2; i++) if (d.readUInt16LE(start + 2 * i) & 0x8000) au = false; }
        const pal = []; for (let k = 0; k < ps; k++) { const s = sw ? swizzle(k) : k; pal.push(s < n2 ? col(d, start + s * es, cbpp, au) : [0, 0, 0, 0]); }
        for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
          let i; if (bpp === 8) i = d[off + y * (stride ?? w) + x];
          else { const pix = y * w + x, b = stride !== null ? d[off + y * stride + (x >> 1)] : d[off + (pix >> 1)]; i = (stride !== null ? x % 2 === 0 : pix % 2 === 0) ? b & 15 : b >> 4; }
          px.push(pal[i]);
        }
      } else {
        const step = bpp / 8; let au = false;
        if (bpp === 16) { au = true; const o0 = pos + hs; for (let i = 0; i < W * Hh; i++) if (d.readUInt16LE(o0 + 2 * i) & 0x8000) au = false; }
        for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) px.push(col(d, off + y * (stride ?? w * step) + x * step, bpp, au));
      }
      void clutSize; return { w, h, rgba: px };
    }
    pos += total;
  }
  throw new Error('no picture ' + pi);
}
// ---- main
const [cmd, ...a] = process.argv.slice(2);
if (cmd === 'tm2') {
  const align = a.includes('--align1') ? 1 : 0; const names = a.slice(1).filter((x) => x !== '--align1');
  fs.writeFileSync(a[0], tim2File(names.map((n) => { if (!PICS[n]) throw new Error('no pic ' + n); return PICS[n](); }), align));
} else if (cmd === 'pngdecode') console.log(JSON.stringify(decodePng(fs.readFileSync(a[0]))));
else if (cmd === 'pngencode') fs.writeFileSync(a[0], encodePng(JSON.parse(a[1].startsWith('@') ? fs.readFileSync(a[1].slice(1), 'utf8') : a[1])));
else if (cmd === 'decode') console.log(JSON.stringify(decodeTim2(fs.readFileSync(a[0]), +(a[1] || 0), +(a[2] || 0), +(a[3] || 0))));
else if (cmd === 'pngraw') {
  const c = a[1]; let b;
  const good = () => encodePng({ w: 2, h: 2, rgba: Array(4).fill([1, 2, 3, 255]) });
  const plte = Buffer.from([0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255]);
  if (c === 'filters') {   // 5x5 RGBA, one row per filter type; prints the unfiltered pixels
    let seed = 3; const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24;
    const rows = [...Array(5)].map(() => Buffer.from([...Array(20)].map(rnd))); const types = [0, 1, 2, 3, 4];
    let prev = Buffer.alloc(20); const f = rows.map((r, y) => { const o = Buffer.alloc(20); for (let i = 0; i < 20; i++) { const A = i >= 4 ? r[i - 4] : 0, B = prev[i], C = i >= 4 ? prev[i - 4] : 0; o[i] = (r[i] - [0, A, B, (A + B) >> 1, paeth(A, B, C)][types[y]]) & 0xFF; } prev = r; return o; });
    b = pngRaw(5, 5, 6, 8, f, types); const flat = Buffer.concat(rows); const px = []; for (let i = 0; i < flat.length; i += 4) px.push([...flat.subarray(i, i + 4)]);
    console.log(JSON.stringify(px));
  } else if (c === 'grey') b = pngRaw(2, 1, 0, 8, [Buffer.from([7, 9])], null, 0, chunk('tRNS', Buffer.from([0, 9])));
  else if (c === 'greyalpha') b = pngRaw(1, 1, 4, 8, [Buffer.from([5, 6])]);
  else if (c === 'rgbtrns') b = pngRaw(2, 1, 2, 8, [Buffer.from([1, 2, 3, 4, 5, 6])], null, 0, chunk('tRNS', Buffer.from([0, 4, 0, 5, 0, 6])));
  else if (c === 'lowdepth2') b = pngRaw(4, 1, 3, 2, [Buffer.from([0b00011011])], null, 0, Buffer.alloc(0), plte);
  else if (c === 'lowdepth4') b = pngRaw(3, 1, 3, 4, [Buffer.from([0x12, 0x00])], null, 0, Buffer.alloc(0), plte);
  else if (c === 'notpng') b = Buffer.concat([Buffer.from('GIF89a'), Buffer.alloc(30)]);
  else if (c === 'crc') { b = good(); b[30] ^= 0xFF; }
  else if (c === 'interlaced') b = pngRaw(1, 1, 2, 8, [Buffer.alloc(3)], null, 1);
  else if (c === '16bit') b = pngRaw(1, 1, 2, 16, [Buffer.alloc(6)]);
  else if (c === 'truncated') b = good().subarray(0, 40);
  else if (c === 'truncated2') b = good().subarray(0, 45);
  else if (c === 'badfilter') b = pngRaw(1, 1, 2, 8, [Buffer.alloc(3)], [9]);
  else if (c === 'noplte') b = pngRaw(1, 1, 3, 8, [Buffer.alloc(1)]);
  else if (c === 'badindex') b = pngRaw(1, 1, 3, 8, [Buffer.from([5])], null, 0, Buffer.alloc(0), Buffer.alloc(6));
  else if (c === 'colortype') b = pngRaw(1, 1, 5, 8, [Buffer.alloc(1)]);
  else if (c === 'badzlib') { b = pngRaw(1, 1, 2, 8, [Buffer.alloc(3)]); const i = b.indexOf('IDAT'); b[i + 4] ^= 0x01; const len = b.readUInt32BE(i - 4); b.writeUInt32BE(crc32(b.subarray(i, i + 4 + len)), i + 4 + len); }
  else if (c === 'shortdata') b = pngRaw(4, 4, 2, 8, [Buffer.alloc(12)]);
  else throw new Error('no case ' + c);
  fs.writeFileSync(a[0], b);
} else if (cmd === 'container') {
  let seed = 11; const junk = (n) => Buffer.from([...Array(n)].map(() => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24 & 0xFF).map((x) => x === 0x54 ? 0x55 : x));   // no 'T': no stray magic
  const A = tim2File([PICS.pic8(true)]), B = tim2File([PICS.pic32(), PICS.pic16()], 1);
  const fake = Buffer.concat([Buffer.from('TIM2'), Buffer.from([4, 9, 1, 0]), Buffer.alloc(8)]);
  fs.writeFileSync(a[0], Buffer.concat([junk(100), fake, junk(37), A, junk(5), B, junk(64)]));
  const o1 = 100 + 16 + 37, o2 = o1 + A.length + 5;
  console.log(JSON.stringify({ offs: [o1, o2], sizes: [A.length, B.length] }));
  if (a[1]) { fs.writeFileSync(a[1] + '_a.tm2', A); fs.writeFileSync(a[1] + '_b.tm2', B); }
} else { console.error('usage: see the header'); process.exit(2); }

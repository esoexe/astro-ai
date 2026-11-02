#!/usr/bin/env node
// mkmemcard.js - synthetic PS2 memory card fixtures for tests/console_memcard.sh (a port of build_card() and friends in
// console-skill's tests/test_saves.py). Every card is formatted here and holds made-up saves; nothing real is used.
//   card <out> [--ecc] [--clusters N] [--no-deleted] [--odd]  -> prints {"data_start": N} (the DATA file's first cluster)
//   patchfat <in> <out> <rel> <value>                         -> copy with one FAT entry replaced (plain cards)
//   poke32 <in> <out> <offset> <value>                        -> copy with one little-endian u32 replaced
//   psu <out> <folder> <count|-> [name=hexdata ...]            -> a hand-made .psu (count "-" = number of files)
//   iconsys <out> <line1> <line2>                              -> an icon.sys with a Shift-JIS title
//   zip <out> <name>=<text> ...                                -> a stored zip (names taken as given)
//   sjis <text>                                                -> hex of the cp932 bytes (from Node's Shift_JIS decoder)
//   iconraw <out> <hex> [break] / zipread <zip> [name] / mtime <f> <t> / getmtime <f> / bytes <f> <a> <b> / datahex game|big
//   decode <hex>                                               -> WHATWG Shift_JIS decode of the bytes, as JSON
const fs = require('fs');
const MAGIC = Buffer.from('Sony PS2 Memory Card Format ', 'latin1');
const MODE_DIR = 0x8427, MODE_FILE = 0x8497, FREE = 0x7FFFFFFF, END = 0xFFFFFFFF;

let ENC = null;
function sjis(text) {   // encoder from Node's own decoder (first code wins), independent of packtools/cp932_table.h
  if (!ENC) {
    ENC = new Map(); const td = new TextDecoder('shift_jis');
    for (let lead = 0x81; lead <= 0xFC; lead++) {
      if (lead >= 0xA0 && lead <= 0xDF) continue;
      for (let tr = 0x40; tr <= 0xFC; tr++) {
        if (tr === 0x7F) continue;
        const s = td.decode(Buffer.from([lead, tr]));
        if (s.length && s !== '\uFFFD' && !ENC.has(s)) ENC.set(s, [lead, tr]);
      }
    }
  }
  const out = [];
  for (const ch of text) {
    const c = ch.codePointAt(0);
    if (c < 0x80) out.push(c);
    else if (c >= 0xFF61 && c <= 0xFF9F) out.push(c - 0xFEC0);
    else if (ENC.has(ch)) out.push(...ENC.get(ch));
    else throw new Error('no cp932 code for ' + ch);
  }
  return Buffer.from(out);
}
function tod(y, mo, d, h, mi, s) { const b = Buffer.alloc(8); b[1] = s; b[2] = mi; b[3] = h; b[4] = d; b[5] = mo; b.writeUInt16LE(y, 6); return b; }
function dirent(mode, length, t, cluster, name, parent = 0) {
  const b = Buffer.alloc(512);
  b.writeUInt16LE(mode, 0); b.writeUInt32LE(length >>> 0, 4); t.copy(b, 8); b.writeUInt32LE(cluster >>> 0, 16); b.writeUInt32LE(parent, 20);
  t.copy(b, 24); Buffer.from(name).copy(b, 64, 0, Math.min(name.length, 448));
  return b;
}
function iconSys(l1, l2) {
  const blob = Buffer.alloc(964); blob.write('PS2D', 0, 'latin1');
  const t1 = sjis(l1), t2 = sjis(l2); blob.writeUInt16LE(t1.length, 6);
  Buffer.concat([t1, t2]).copy(blob, 0xC0); blob.write('icon.ico', 0x104, 'latin1');
  return blob;
}
const T_SAVE = tod(2004, 5, 6, 7, 8, 9);
function bytesOf(n, f) { const b = Buffer.alloc(n); for (let i = 0; i < n; i++) b[i] = f(i) & 0xFF; return b; }
const SAVES = [
  [Buffer.from('BASLUS-20312GAME'), T_SAVE, [
    [Buffer.from('icon.sys'), iconSys('ＧＡＭＥ', 'セーブ')],
    [Buffer.from('DATA'), bytesOf(3000, i => i * 7 + 3)],
    [Buffer.from('icon.ico'), Buffer.alloc(100, 1)]]],
  [Buffer.from('BESLES-50001SAVE'), tod(2005, 12, 31, 23, 59, 58), [
    [Buffer.from('BIG.DAT'), bytesOf(2048, i => i * 13)],
    [Buffer.from('EMPTY'), Buffer.alloc(0)]]],
];
const ODD = [[Buffer.from('BASLUS-20312GAME'), T_SAVE, [[Buffer.from('OK'), Buffer.from('fine')], [Buffer.from('A:B'), Buffer.from('bad')]]]];

function buildCard(saves, ecc, clusters, deleted) {
  const page = 512, ppc = 2, ppb = 16, cs = 1024, allocOffset = 41;
  const backup2 = Math.floor(clusters / (ppb / ppc)) - 2;
  const allocEnd = backup2 * (ppb / ppc) - allocOffset;
  const img = Buffer.alloc(clusters * cs);
  MAGIC.copy(img, 0); img.write('1.2.0.0', 0x1C, 'latin1');
  img.writeUInt16LE(page, 0x28); img.writeUInt16LE(ppc, 0x2A); img.writeUInt16LE(ppb, 0x2C); img.writeUInt16LE(0xFF00, 0x2E);
  [clusters, allocOffset, allocEnd, 0, backup2 + 1, backup2].forEach((v, i) => img.writeUInt32LE(v, 0x30 + 4 * i));
  const nfat = Math.ceil(allocEnd / 256);
  img.writeUInt32LE(8, 0x50);
  img.fill(0xFF, 0xD0, 0x150); img[0x150] = 2; img[0x151] = 0x52;
  for (let i = 0; i < nfat; i++) img.writeUInt32LE(9 + i, 8 * cs + 4 * i);
  const fat = new Array(nfat * 256).fill(FREE);
  let pool = []; for (let i = 1; i < allocEnd; i++) pool.push(i);
  pool = pool.filter((_, i) => i % 3 === 0).concat(pool.filter((_, i) => i % 3 === 1), pool.filter((_, i) => i % 3 === 2));
  const alloc = (n, first) => {
    const got = first !== undefined ? [first] : [];
    while (got.length < n) got.push(pool.shift());
    for (let i = 0; i + 1 < got.length; i++) fat[got[i]] = (0x80000000 | got[i + 1]) >>> 0;
    fat[got[got.length - 1]] = END; return got;
  };
  const write = (chain, blob) => chain.forEach((c, i) => blob.subarray(i * cs, (i + 1) * cs).copy(img, (allocOffset + c) * cs));
  let dataStart = -1;
  const root = [null, null];
  for (const [name, t, files] of saves) {
    const ents = [null, null];
    for (const [fname, data] of files) {
      if (data.length) {
        const ch = alloc(Math.ceil(data.length / cs)); write(ch, data);
        if (fname.toString() === 'DATA' && dataStart < 0) dataStart = ch[0];
        ents.push(dirent(MODE_FILE, data.length, t, ch[0], fname));
      } else ents.push(dirent(MODE_FILE, 0, t, FREE, fname));
    }
    const ch = alloc(Math.ceil(ents.length / 2));
    ents[0] = dirent(MODE_DIR, ents.length, t, 0, Buffer.from('.'));
    ents[1] = dirent(0xA426, 0, t, 0, Buffer.from('..'));
    write(ch, Buffer.concat(ents));
    root.push(dirent(MODE_DIR, ents.length, t, ch[0], name));
  }
  if (deleted) root.push(dirent(MODE_DIR & 0x7FFF, 2, T_SAVE, 5, Buffer.from('OLDSAVE')));
  root[0] = dirent(MODE_DIR, root.length, T_SAVE, 0, Buffer.from('.'));
  root[1] = dirent(0xA426, 0, T_SAVE, 0, Buffer.from('..'));
  write(alloc(Math.ceil(root.length / 2), 0), Buffer.concat(root));
  fat.forEach((v, i) => img.writeUInt32LE(v >>> 0, (9 + Math.floor(i / 256)) * cs + 4 * (i % 256)));
  if (!ecc) return { img, dataStart };
  const spare = Buffer.alloc(16, 0x5A), parts = [];
  for (let p = 0; p < clusters * ppc; p++) { parts.push(img.subarray(p * page, (p + 1) * page), spare); }
  return { img: Buffer.concat(parts), dataStart };
}

function crc32(buf) {
  let c, crc = 0xFFFFFFFF;
  for (const b of buf) { c = (crc ^ b) & 0xFF; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1; crc = (crc >>> 8) ^ c; }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
function storedZip(entries) {
  const locals = [], cds = []; let off = 0;
  for (const [name, data] of entries) {
    const n = Buffer.from(name), crc = crc32(data);
    const h = Buffer.alloc(30); h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt16LE(0x21, 10); h.writeUInt16LE(0x21, 12);
    h.writeUInt32LE(crc, 14); h.writeUInt32LE(data.length, 18); h.writeUInt32LE(data.length, 22); h.writeUInt16LE(n.length, 26);
    const c = Buffer.alloc(46); c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6); c.writeUInt16LE(0x21, 12); c.writeUInt16LE(0x21, 14);
    c.writeUInt32LE(crc, 16); c.writeUInt32LE(data.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(n.length, 28); c.writeUInt32LE(off, 42);
    locals.push(h, n, data); cds.push(c, n); off += 30 + n.length + data.length;
  }
  const cd = Buffer.concat(cds), e = Buffer.alloc(22);
  e.writeUInt32LE(0x06054b50, 0); e.writeUInt16LE(entries.length, 8); e.writeUInt16LE(entries.length, 10); e.writeUInt32LE(cd.length, 12); e.writeUInt32LE(off, 16);
  return Buffer.concat([...locals, cd, e]);
}

const [cmd, ...a] = process.argv.slice(2);
if (cmd === 'card') {
  const out = a[0], ecc = a.includes('--ecc'), deleted = !a.includes('--no-deleted'), odd = a.includes('--odd');
  const ci = a.indexOf('--clusters'), clusters = ci >= 0 ? parseInt(a[ci + 1], 10) : 8192;
  const { img, dataStart } = buildCard(odd ? ODD : SAVES, ecc, clusters, deleted && !odd);
  fs.writeFileSync(out, img); console.log(JSON.stringify({ data_start: dataStart }));
} else if (cmd === 'patchfat') {
  const img = fs.readFileSync(a[0]), rel = parseInt(a[2], 10), v = Number(a[3]);
  img.writeUInt32LE(v >>> 0, (9 + Math.floor(rel / 256)) * 1024 + 4 * (rel % 256)); fs.writeFileSync(a[1], img);
} else if (cmd === 'poke32') {
  const img = fs.readFileSync(a[0]); img.writeUInt32LE(Number(a[3]) >>> 0, Number(a[2])); fs.writeFileSync(a[1], img);
} else if (cmd === 'psu') {
  const [out, folder, count, ...files] = a;
  const fl = files.map(f => { const k = f.indexOf('='); return [Buffer.from(f.slice(0, k)), Buffer.from(f.slice(k + 1), 'hex')]; });
  const n = count === '-' ? fl.length : parseInt(count, 10), t = T_SAVE;
  const parts = [dirent(MODE_DIR, n + 2, t, 0, Buffer.from(folder)), dirent(MODE_DIR, 0, t, 0, Buffer.from('.')), dirent(MODE_DIR, 0, t, 0, Buffer.from('..'))];
  for (const [name, data] of fl) parts.push(dirent(MODE_FILE, data.length, t, 0, name), data, Buffer.alloc((1024 - data.length % 1024) % 1024));
  fs.writeFileSync(out, Buffer.concat(parts));
} else if (cmd === 'iconsys') {
  fs.writeFileSync(a[0], iconSys(a[1], a[2]));
} else if (cmd === 'zip') {
  const [out, ...es] = a;
  fs.writeFileSync(out, storedZip(es.map(e => { const k = e.indexOf('='); return [e.slice(0, k), Buffer.from(e.slice(k + 1))]; })));
} else if (cmd === 'sjis') {
  console.log(sjis(a[0]).toString('hex'));
} else if (cmd === 'decode') {
  console.log(JSON.stringify(new TextDecoder('shift_jis').decode(Buffer.from(a[0], 'hex'))));
} else if (cmd === 'iconraw') {   // iconraw <out> <hex title bytes> [break]
  const blob = Buffer.alloc(964); blob.write('PS2D', 0, 'latin1'); const t = Buffer.from(a[1], 'hex');
  blob.writeUInt16LE(a[2] !== undefined ? parseInt(a[2], 10) : t.length, 6); t.copy(blob, 0xC0, 0, Math.min(t.length, 68)); fs.writeFileSync(a[0], blob);
} else if (cmd === 'zipread') {   // zipread <zip> [name]: names as JSON, or one member's bytes as hex (Node's own inflate)
  const z = fs.readFileSync(a[0]); let e = z.length - 22; while (e >= 0 && z.readUInt32LE(e) !== 0x06054b50) e--;
  const n = z.readUInt16LE(e + 10); let at = z.readUInt32LE(e + 16); const names = []; let found = null;
  for (let i = 0; i < n; i++) {
    const method = z.readUInt16LE(at + 10), crc = z.readUInt32LE(at + 16), cs = z.readUInt32LE(at + 20), nl = z.readUInt16LE(at + 28), xl = z.readUInt16LE(at + 30), cl = z.readUInt16LE(at + 32), loc = z.readUInt32LE(at + 42);
    const name = z.subarray(at + 46, at + 46 + nl).toString(); names.push(name);
    if (name === a[1]) {
      const d = loc + 30 + z.readUInt16LE(loc + 26) + z.readUInt16LE(loc + 28), raw = z.subarray(d, d + cs);
      found = method === 8 ? require('zlib').inflateRawSync(raw) : raw;
      if (crc32(found) !== crc) throw new Error('bad crc for ' + name);
    }
    at += 46 + nl + xl + cl;
  }
  console.log(a[1] === undefined ? JSON.stringify(names) : found === null ? 'MISSING' : found.toString('hex'));
} else if (cmd === 'mtime') {   // mtime <file> <epoch seconds>
  fs.utimesSync(a[0], Number(a[1]), Number(a[1]));
} else if (cmd === 'getmtime') {
  console.log(Math.floor(fs.statSync(a[0]).mtimeMs / 1000));
} else if (cmd === 'bytes') {   // bytes <file> <start> <end>: hex
  console.log(fs.readFileSync(a[0]).subarray(+a[1], +a[2]).toString('hex'));
} else if (cmd === 'datahex') {   // datahex game|big: the fixture save bytes
  console.log((a[0] === 'game' ? bytesOf(3000, i => i * 7 + 3) : bytesOf(2048, i => i * 13)).toString('hex'));
} else { console.error('usage: see header'); process.exit(2); }

#!/usr/bin/env node
// mkdatascan.js - fixtures for tests/console_datascan.sh (console-levels/datascan), modelled on
// console-skill/tests/test_levels.py (make_afs, make_toc, elf, the scan tree).
//   blob <out> <n> <seed>                      deterministic random bytes
//   afs <out> <version> [default|evil]         AFS archive (version 1: name-table pointer after the TOC, 2: before the data)
//   toc <out> [magic=PAK1] [off_scale=2048] [kind=count+offsets] [sentinel=1] [gap_junk=1]   generic table archive
//   tree <dir>                                 the ScanAndSniff tree
//   afsread <file>                             {"entries": [[o, s]...], "names": [...], "meta_where": ...}
//   u32 <file> <offset>                        one little-endian u32
//   slice <file> <offset> <len> <out>          copy a byte range
//   hex <out> <hex>                            write bytes from hex (no spaces)
//   zlib <out> <text> [mode]                   zlib stream; mode: cut=N (first N bytes), badsum, garbage
"use strict";
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");

function rng(seed) {   // mulberry32
  let a = (seed * 0x9E3779B1) >>> 0;
  return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) & 0xFF; };
}
function blob(n, seed) { const r = rng(seed); const b = Buffer.alloc(n); for (let i = 0; i < n; i++) b[i] = r(); return b; }
const up = (v, a) => Math.floor((v + a - 1) / a) * a;

function makeAfs(entries, version = 1, block = 2048) {
  const n = entries.length;
  const first = up(16 + 8 * n, block);
  let out = Buffer.alloc(first);
  out.write("AFS\0", 0, "latin1");
  out.writeUInt32LE(n, 4);
  let pos = first;
  const toc = [];
  for (const [, data] of entries) {
    if (out.length < pos) out = Buffer.concat([out, Buffer.alloc(pos - out.length)]);
    out = Buffer.concat([out, data]);
    toc.push([pos, data.length]);
    pos = up(pos + data.length, block);
  }
  toc.forEach(([o, s], i) => { out.writeUInt32LE(o, 8 + 8 * i); out.writeUInt32LE(s, 12 + 8 * i); });
  const meta = pos;
  out = Buffer.concat([out, Buffer.alloc(meta - out.length)]);
  const at = version === 1 ? 8 + 8 * n : first - 8;
  out.writeUInt32LE(meta, at); out.writeUInt32LE(48 * n, at + 4);
  const recs = [];
  entries.forEach(([name, data], i) => {
    const rec = Buffer.alloc(48);
    Buffer.from(name, "latin1").copy(rec, 0);
    [2003, 7, 14 + i, 10, 20, 30].forEach((v, k) => rec.writeUInt16LE(v, 32 + 2 * k));
    const tail = version === 1 ? toc[i][0] : out.readUInt32LE(4 + 4 * i);
    rec.writeUInt32LE(tail, 44);
    recs.push(rec);
  });
  out = Buffer.concat([out, ...recs]);
  out = Buffer.concat([out, Buffer.alloc(up(out.length, block) - out.length)]);
  return out;
}
const AFS_ENTRIES = () => [["STAGE1.DAT", blob(3000, 1)], ["STAGE1.TM2", Buffer.concat([Buffer.from("TIM2"), blob(5000, 2)])],
  ["MUSIC.VAG", Buffer.concat([Buffer.from("VAGp"), blob(100, 3)])]];
const BLOBS = () => [blob(700, 11), blob(1500, 12), blob(33, 13), blob(2100, 14)];

function makeToc(blobs, { align = 16, magic = "", off_scale = 1, kind = "count+pairs", sentinel = false, gap_junk = null } = {}) {
  const n = blobs.length;
  const stride = kind === "count+pairs" ? 8 : 4;
  const tableAt = magic.length + 4;
  const tableEnd = tableAt + n * stride + (sentinel ? 4 : 0);
  const A = Math.max(align, off_scale);
  let pos = up(tableEnd, A);
  let out = Buffer.alloc(pos);
  out.write(magic, 0, "latin1");
  out.writeUInt32LE(n, magic.length);
  const ents = [];
  blobs.forEach((b, i) => {
    pos = up(pos, A);
    out = Buffer.concat([out, Buffer.alloc(pos - out.length), b]);
    ents.push([pos, b.length]);
    pos += b.length;
    if (gap_junk !== null && i === gap_junk) { out = Buffer.concat([out, Buffer.alloc(8, 0xAB)]); pos += 8; }
  });
  const end = up(out.length, A);
  out = Buffer.concat([out, Buffer.alloc(end - out.length)]);
  ents.forEach(([o, s], i) => {
    if (kind === "count+pairs") { out.writeUInt32LE(o / off_scale, tableAt + 8 * i); out.writeUInt32LE(s, tableAt + 8 * i + 4); }
    else out.writeUInt32LE(o / off_scale, tableAt + 4 * i);
  });
  if (sentinel) out.writeUInt32LE(Math.floor(out.length / off_scale), tableAt + 4 * n);
  return out;
}

function elf(eType = 2, machine = 8, iopmod = false, bits = 1) {
  const hdr = Buffer.alloc(52);
  hdr.write("\x7fELF", 0, "latin1");
  hdr[4] = bits; hdr[5] = 1; hdr[6] = 1;
  hdr.writeUInt16LE(eType, 16); hdr.writeUInt16LE(machine, 18); hdr.writeUInt32LE(1, 20);
  if (!iopmod) return Buffer.concat([hdr, Buffer.alloc(64)]);
  const strtab = Buffer.from("\0.iopmod\0.shstrtab\0", "latin1");
  const shoff = 52 + strtab.length;
  hdr.writeUInt32LE(shoff, 0x20);
  hdr.writeUInt16LE(40, 0x2E); hdr.writeUInt16LE(3, 0x30); hdr.writeUInt16LE(2, 0x32);
  const sh = Buffer.alloc(120);
  [1, 0x70000080, 0, 0, 0, 0, 0, 0, 4, 0].forEach((v, k) => sh.writeUInt32LE(v >>> 0, 40 + 4 * k));
  [9, 3, 0, 0, 52, strtab.length, 0, 0, 1, 0].forEach((v, k) => sh.writeUInt32LE(v, 80 + 4 * k));
  return Buffer.concat([hdr, strtab, sh]);
}

function storedZip(name, data) {   // one stored member; enough for the "PK\3\4" sniff and for unzip
  const crcTable = [...Array(256)].map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  let crc = 0xFFFFFFFF; for (const b of data) crc = crcTable[(crc ^ b) & 0xFF] ^ (crc >>> 8); crc = (crc ^ 0xFFFFFFFF) >>> 0;
  const nm = Buffer.from(name);
  const lh = Buffer.alloc(30); lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(20, 4); lh.writeUInt32LE(crc, 14); lh.writeUInt32LE(data.length, 18); lh.writeUInt32LE(data.length, 22); lh.writeUInt16LE(nm.length, 26);
  const cd = Buffer.alloc(46); cd.writeUInt32LE(0x02014b50, 0); cd.writeUInt16LE(20, 4); cd.writeUInt16LE(20, 6); cd.writeUInt32LE(crc, 16); cd.writeUInt32LE(data.length, 20); cd.writeUInt32LE(data.length, 24); cd.writeUInt16LE(nm.length, 28);
  const cdOff = 30 + nm.length + data.length;
  const eocd = Buffer.alloc(22); eocd.writeUInt32LE(0x06054b50, 0); eocd.writeUInt16LE(1, 8); eocd.writeUInt16LE(1, 10); eocd.writeUInt32LE(46 + nm.length, 12); eocd.writeUInt32LE(cdOff, 16);
  return Buffer.concat([lh, nm, data, cd, nm, eocd]);
}

function put(file, data) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, data); }

function afsread(file) {
  const d = fs.readFileSync(file);
  const n = d.readUInt32LE(4);
  const entries = [...Array(n)].map((_, i) => [d.readUInt32LE(8 + 8 * i), d.readUInt32LE(12 + 8 * i)]);
  const real = entries.filter(([o, s]) => o || s).map(([o]) => o).sort((a, b) => a - b);
  const first = real.length ? real[0] : up(8 + 8 * n, 2048);
  const cands = [];
  if (8 + 8 * n + 8 <= first) cands.push(["after_toc", 8 + 8 * n]);
  if (first - 8 >= 8 + 8 * n && first - 8 !== 8 + 8 * n) cands.push(["before_data", first - 8]);
  let where = null, mo = 0;
  for (const [w, at] of cands) {
    const o = d.readUInt32LE(at), l = d.readUInt32LE(at + 4);
    if (o >= first && l >= 48 * n && o + 48 * n <= d.length) { where = w; mo = o; break; }
  }
  const names = where ? entries.map((_, i) => { const r = d.subarray(mo + 48 * i, mo + 48 * i + 32); const z = r.indexOf(0); return r.subarray(0, z < 0 ? 32 : z).toString("latin1"); }) : [];
  return { entries, names, meta_where: where, size: d.length };
}

const [cmd, ...a] = process.argv.slice(2);
const kv = Object.fromEntries(a.filter((x) => x.includes("=")).map((x) => [x.slice(0, x.indexOf("=")), x.slice(x.indexOf("=") + 1)]));
if (cmd === "blob") put(a[0], blob(+a[1], +a[2]));
else if (cmd === "afs") {
  const ents = a[2] === "evil" ? [["../../evil.bin", Buffer.alloc(10, "x")], ["a/b\\c", Buffer.alloc(10, "y")]] : AFS_ENTRIES();
  put(a[0], makeAfs(ents, +a[1]));
} else if (cmd === "toc") {
  put(a[0], makeToc(BLOBS(), { magic: kv.magic || "", off_scale: +(kv.off_scale || 1), kind: kv.kind || "count+pairs", sentinel: kv.sentinel === "1",
    gap_junk: kv.gap_junk === undefined ? null : +kv.gap_junk }));
} else if (cmd === "tree") {
  const d = a[0];
  const files = {
    "SYSTEM.CNF": Buffer.from("BOOT2 = cdrom0:\\SLUS_203.12;1\nVER = 1.00\nVMODE = NTSC\n"),
    "SLUS_203.12": elf(2),
    "MODULES/SIO2MAN.IRX": elf(0xFF80),
    "MODULES/OLD.IRX": elf(1, 8, true),
    "OTHER.ELF": elf(2, 40),
    "DATA/LEVEL.AFS": makeAfs(AFS_ENTRIES()),
    "DATA/STAGE.PAK": makeToc(BLOBS()),
    "DATA/TITLE.TM2": Buffer.concat([Buffer.from("TIM2"), Buffer.alloc(60)]),
    "SOUND/SE.VAG": Buffer.concat([Buffer.from("VAGp"), Buffer.alloc(60)]),
    "SOUND/SE.HD": Buffer.concat([Buffer.from("IECSsreV"), Buffer.alloc(56)]),
    "MOVIE/OPEN.PSS": Buffer.concat([Buffer.from([0, 0, 1, 0xBA]), Buffer.alloc(60)]),
    "MOVIE/OPEN.IPU": Buffer.concat([Buffer.from("ipum"), Buffer.alloc(60)]),
    "PACK/A.GZ": zlib.gzipSync(Buffer.alloc(1000, "x")),
    "PACK/B.Z": zlib.deflateSync(Buffer.alloc(1000, "y")),
    "PACK/C.ZIP": storedZip("a.txt", Buffer.from("hello")),
    "PACK/D.WAV": Buffer.concat([Buffer.from("RIFF"), Buffer.alloc(4), Buffer.from("WAVE"), Buffer.alloc(40)]),
    "PACK/E.CPK": Buffer.concat([Buffer.from("CPK "), Buffer.alloc(60)]),
    "PACK/NOISE.BIN": blob(5000, 3),
    "PACK/ZERO.BIN": Buffer.alloc(5000),
    "EMPTY.DAT": Buffer.alloc(0),
  };
  for (const [k, v] of Object.entries(files)) put(path.join(d, k), v);
} else if (cmd === "afsread") console.log(JSON.stringify(afsread(a[0])));
else if (cmd === "u32") console.log(fs.readFileSync(a[0]).readUInt32LE(+a[1]));
else if (cmd === "slice") put(a[3], fs.readFileSync(a[0]).subarray(+a[1], +a[1] + +a[2]));
else if (cmd === "hex") put(a[0], Buffer.from(a[1], "hex"));
else if (cmd === "zlib") {
  let z = zlib.deflateSync(Buffer.from(a[1].repeat(50)));
  const mode = a[2] || "";
  if (mode.startsWith("cut=")) z = z.subarray(0, +mode.slice(4));
  else if (mode === "badsum") { z = Buffer.from(z); z[z.length - 1] ^= 0xFF; }
  put(a[0], z);
} else if (cmd === "zfuzz") {
  // files that start like zlib (78 01/5E/9C/DA); prints {name: true|false}, true when Node's inflate with
  // Z_SYNC_FLUSH accepts the first 4096 bytes (the same contract as zlib.decompressobj().decompress)
  const dir = a[0], count = +a[1], r = rng(4242), verdict = {};
  const heads = [[0x78, 0x01], [0x78, 0x5E], [0x78, 0x9C], [0x78, 0xDA]];
  const levels = { 0x01: 1, 0x5E: 3, 0x9C: 6, 0xDA: 9 };
  for (let i = 0; i < count; i++) {
    const h = heads[r() & 3];
    const n = 8 + ((r() << 8 | r()) % 6000);
    let buf;
    const mode = i % 6;
    if (mode === 0) { buf = Buffer.concat([Buffer.from(h), blob(n, 1000 + i)]); }   // random body
    else {
      const words = ["stage", "enemy", "boss", "\x00\x01\x02", "tex", "VAGp"];
      let s = ""; for (let k = 0; k < n / 4; k++) s += words[r() % words.length] + String.fromCharCode(r());
      const src = (i % 12 < 6) ? Buffer.from(s, "latin1") : blob(n, 2000 + i);
      let z = Buffer.from(zlib.deflateSync(src, { level: levels[h[1]] }));
      z[1] = h[1];   // keep the FLEVEL bits of the chosen header byte (the header check stays valid)
      if (mode === 1) { z = z.subarray(0, 2 + (r() << 8 | r()) % Math.max(1, z.length - 2)); }        // truncated
      else if (mode === 2) { const k = 2 + (r() << 8 | r()) % Math.max(1, z.length - 2); z[k] ^= 1 << (r() & 7); }  // one flipped bit
      else if (mode === 3) { z[z.length - 1 - (r() & 3)] ^= 0x5A; }                                     // bad Adler-32
      else if (mode === 4) { z = Buffer.concat([z, blob(1 + (r() % 300), 3000 + i)]); }                 // trailing junk
      buf = z;
    }
    const name = "z" + String(i).padStart(4, "0") + ".bin";
    put(path.join(dir, name), buf);
    let ok = true;
    try { zlib.inflateSync(buf.subarray(0, 4096), { finishFlush: zlib.constants.Z_SYNC_FLUSH }); } catch (e) { ok = false; }
    verdict[name] = ok;
  }
  console.log(JSON.stringify(verdict));
} else { console.error("unknown command " + cmd); process.exit(2); }

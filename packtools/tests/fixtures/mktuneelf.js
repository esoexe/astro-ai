// The synthetic PS2 ELFs of console-skill/tests/test_physics.py (make_elf / standard_elf and the per-test variants), byte for byte.
// usage: node mktuneelf.js standard|nosections|rwx|two|empty OUT
//        node mktuneelf.js words OUT HEX...        standard .text, .data = the given words (then zero padding to 0x100)
//        node mktuneelf.js apply ELF RESULT.json OUT   write a patch --json result's "words" into ELF (the round trip)
const fs = require('fs');
const PF_X = 1, PF_W = 2, PF_R = 4;
const words = (...ws) => { const b = Buffer.alloc(ws.length * 4); ws.forEach((w, i) => b.writeUInt32LE(w >>> 0, i * 4)); return b; };
const f2u = (f) => { const b = Buffer.alloc(4); b.writeFloatLE(f, 0); return b.readUInt32LE(0); };
const TEXT = words(
  0x3C1C0021, 0x279C8000, 0xC7818000, 0x3C02411C, 0x3442CCCD, 0x44826000, 0x3C014148, 0x44811000,
  0x3C043C89, 0x24848889, 0x44841800, 0x3C080020, 0xC5040004, 0x03E00008, 0x00000000, 0x3C0341F0,
  0x00001821, 0x44832800, 0x3C0942F0, 0x08040000, 0x00000000, 0x44893000, 0x3C0A4270, 0x448A3800,
  0x03E00008, 0x00000000);
const dataOf = (ws) => Buffer.concat([words(...ws), Buffer.alloc(Math.max(0, 0x100 - 4 * ws.length))]);
const DATA = dataOf([f2u(9.8), f2u(1.0 / 60.0), f2u(30.0), f2u(9.81), f2u(-9.8), 0, f2u(0.75), f2u(1.0)]);
function makeElf(segments, entry = 0x00100000, sections = true) {
  const phnum = segments.length, off = 52 + 32 * phnum;
  let body = Buffer.alloc(0); const placed = [];
  for (const [, data] of segments) {
    const pad = ((-(off + body.length)) % 16 + 16) % 16;
    body = Buffer.concat([body, Buffer.alloc(pad)]); placed.push(off + body.length); body = Buffer.concat([body, data]);
  }
  let shdrs = Buffer.alloc(0), shoff = 0, shnum = 0, shstrndx = 0;
  if (sections) {
    let names = Buffer.from([0]); const sh = [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0]];
    segments.forEach(([vaddr, data, memsz, flags, name], i) => {
      const foff = placed[i];
      let nidx = names.length; names = Buffer.concat([names, Buffer.from(name + '\0')]);
      sh.push([nidx, 1, 2 | (flags & PF_X ? 4 : 0) | (flags & PF_W ? 1 : 0), vaddr, foff, data.length, 0, 0, 16, 0]);
      if (memsz > data.length) { nidx = names.length; names = Buffer.concat([names, Buffer.from('.bss\0')]); sh.push([nidx, 8, 3, vaddr + data.length, foff + data.length, memsz - data.length, 0, 0, 16, 0]); }
    });
    const nidx = names.length; names = Buffer.concat([names, Buffer.from('.shstrtab\0')]);
    sh.push([nidx, 3, 0, 0, off + body.length, names.length, 0, 0, 1, 0]);
    body = Buffer.concat([body, names]);
    body = Buffer.concat([body, Buffer.alloc(((-(off + body.length)) % 4 + 4) % 4)]);
    shoff = off + body.length;
    shdrs = Buffer.concat(sh.map(s => words(...s))); shnum = sh.length; shstrndx = sh.length - 1;
  }
  const hdr = Buffer.alloc(52);
  Buffer.from([0x7f, 0x45, 0x4c, 0x46, 1, 1, 1, 0]).copy(hdr, 0);
  hdr.writeUInt16LE(2, 16); hdr.writeUInt16LE(8, 18); hdr.writeUInt32LE(1, 20); hdr.writeUInt32LE(entry, 24); hdr.writeUInt32LE(52, 28);
  hdr.writeUInt32LE(shoff, 32); hdr.writeUInt32LE(0, 36); hdr.writeUInt16LE(52, 40); hdr.writeUInt16LE(32, 42); hdr.writeUInt16LE(phnum, 44);
  hdr.writeUInt16LE(40, 46); hdr.writeUInt16LE(shnum, 48); hdr.writeUInt16LE(shstrndx, 50);
  const ph = Buffer.concat(segments.map(([vaddr, data, memsz, flags], i) => words(1, placed[i], vaddr, vaddr, data.length, memsz, flags, 16)));
  return Buffer.concat([hdr, ph, body, shdrs]);
}
const standard = (sections = true, text = TEXT, data = DATA) =>
  makeElf([[0x00100000, text, text.length, PF_R | PF_X, '.text'], [0x00200000, data, 0x1000, PF_R | PF_W, '.data']], 0x00100000, sections);
const [kind, out, ...rest] = process.argv.slice(2);
if (kind === 'apply') {   // RESULT.json words -> bytes at the segment offset (vaddr == ram in these ELFs)
  const [res, dst] = rest, data = Buffer.from(fs.readFileSync(out)), r = JSON.parse(fs.readFileSync(res, 'utf8'));
  const phoff = data.readUInt32LE(28), phnum = data.readUInt16LE(44);
  for (const w of r.words) {
    const addr = parseInt(w.addr, 16);
    for (let i = 0; i < phnum; i++) {
      const p = phoff + 32 * i, off = data.readUInt32LE(p + 4), va = data.readUInt32LE(p + 8), fs_ = data.readUInt32LE(p + 16);
      if (va <= addr && addr < va + fs_) { data.writeUInt32LE(parseInt(w.new, 16) >>> 0, off + addr - va); break; }
    }
  }
  fs.writeFileSync(dst, data);
} else {
  let b;
  if (kind === 'nosections') b = standard(false);
  else if (kind === 'rwx') { const blob = Buffer.concat([TEXT, DATA]); b = makeElf([[0x00100000, blob, blob.length, 7, '.x']], 0x00100000, false); }
  else if (kind === 'two') b = standard(true, Buffer.concat([TEXT, words(0xC7818000)]));
  else if (kind === 'empty') b = makeElf([[0x00100000, words(0x03E00008, 0), 8, PF_R | PF_X, '.text']]);
  else if (kind === 'words') b = standard(true, TEXT, dataOf(rest.map(h => parseInt(h, 16))));
  else b = standard(true);
  fs.writeFileSync(out, b);
}

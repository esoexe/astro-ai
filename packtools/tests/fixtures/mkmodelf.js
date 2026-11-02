// The synthetic PS2 ELFs of console-skill/tests/test_mod.py (make_elf / standard_elf / rwx_elf), byte for byte.
// usage: node mkmodelf.js standard|nosections|rwx OUT
const fs = require('fs');
const PF_X = 1, PF_W = 2, PF_R = 4;
const words = (...ws) => { const b = Buffer.alloc(ws.length * 4); ws.forEach((w, i) => b.writeUInt32LE(w >>> 0, i * 4)); return b; };
const TEXT = words(0x3C02411C, 0x3442CCCD, 0x44826000, 0x03E00008, 0x00000000, 0x0000000C, 0x8C820010, 0x10400003,
                   0x00000000, 0x03E00008, 0x00000000, 0x42000038, 0x0C040006, 0x00000000, 0x08040009, 0x00000000);
const DATA = Buffer.concat([words(0x00000064, 0x3F800000, 0x11111111, 0x22222222), Buffer.alloc(0x100 - 16)]);
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
      const shf = 2 | (flags & PF_X ? 4 : 0) | (flags & PF_W ? 1 : 0);
      sh.push([nidx, 1, shf, vaddr, foff, data.length, 0, 0, 16, 0]);
      if (memsz > data.length) { nidx = names.length; names = Buffer.concat([names, Buffer.from('.bss\0')]); sh.push([nidx, 8, 3, vaddr + data.length, foff + data.length, memsz - data.length, 0, 0, 16, 0]); }
    });
    const nidx = names.length; names = Buffer.concat([names, Buffer.from('.shstrtab\0')]);
    const stroff = off + body.length; body = Buffer.concat([body, names]);
    sh.push([nidx, 3, 0, 0, stroff, names.length, 0, 0, 1, 0]);
    const pad = ((-(off + body.length)) % 4 + 4) % 4; body = Buffer.concat([body, Buffer.alloc(pad)]);
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
const standard = (sections) => makeElf([[0x00100000, TEXT, TEXT.length, PF_R | PF_X, '.text'], [0x00200000, DATA, 0x1000, PF_R | PF_W, '.data']], 0x00100000, sections);
const rwx = () => { const blob = Buffer.concat([TEXT, Buffer.alloc(0x100 - TEXT.length), DATA]); return makeElf([[0x00100000, blob, blob.length + 0x100, PF_R | PF_W | PF_X, '.x']], 0x00100000, false); };
const [kind, out] = process.argv.slice(2);
fs.writeFileSync(out, kind === 'rwx' ? rwx() : standard(kind !== 'nosections'));

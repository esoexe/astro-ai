// The synthetic DMA / VIF1 / GIF buffers of console-skill/tests/test_gspacket.py (vifcode, giftag, ad, dmatag,
// upload_32, draw_sprite, Vif.stream, Dma.chain_dump and the per-test buffers), byte for byte.
// usage: node mkgsp.js NAME OUT      (node mkgsp.js list prints the names)
//        node mkgsp.js vifop OUT OP NUM [STCYCL_IMM]    one VIF code with no data (UNPACK sizes through the truncation message)
const fs = require('fs');
const u32 = (...ws) => { const b = Buffer.alloc(4 * ws.length); ws.forEach((w, i) => b.writeUInt32LE(w >>> 0, 4 * i)); return b; };
const q2 = (lo, hi) => { const b = Buffer.alloc(16); b.writeBigUInt64LE(BigInt.asUintN(64, BigInt(lo)), 0); b.writeBigUInt64LE(BigInt.asUintN(64, BigInt(hi)), 8); return b; };
const zeros = (n) => Buffer.alloc(n);
const cat = (...b) => Buffer.concat(b);
const vifcode = (op, num = 0, imm = 0, irq = 0) => u32((irq << 31) | (op << 24) | (num << 16) | imm);
function giftag(nloop, flg, regs, eop = 1, pre = 0, prim = 0) {
  const nreg = BigInt(regs.length % 16);
  const lo = BigInt(nloop) | (BigInt(eop) << 15n) | (BigInt(pre) << 46n) | (BigInt(prim) << 47n) | (BigInt(flg) << 58n) | (nreg << 60n);
  let hi = 0n; regs.forEach((r, i) => { hi |= BigInt(r) << BigInt(4 * i); });
  return q2(lo, hi);
}
const ad = (reg, value) => q2(value, reg);
const dmatag = (qwc, tid, addr = 0, vif0 = 0, vif1 = 0, irq = 0) => u32(qwc | (tid << 28) | (irq << 31), addr, vif0, vif1);
const bitbltbuf = (dbp, dbw, dpsm) => (BigInt(dbp) << 32n) | (BigInt(dbw) << 48n) | (BigInt(dpsm) << 56n);
const trxreg = (w, h) => BigInt(w) | (BigInt(h) << 32n);
function upload32(w, h, imageQw = null, eop = 1) {
  const setup = cat(giftag(4, 0, [0xE], 0), ad(0x50, bitbltbuf(0x2000, 1, 0x00)), ad(0x51, 0), ad(0x52, trxreg(w, h)), ad(0x53, 0));
  const qw = imageQw === null ? Math.floor((w * h * 4) / 16) : imageQw;
  return cat(setup, giftag(qw, 2, [], eop), Buffer.alloc(qw * 16, 0x11));
}
const drawSprite = (primType = 6, eop = 1) => cat(giftag(1, 0, [0x0, 0x1, 0x5, 0x5], eop), q2(primType, 0), q2(0x80808080, 0), q2(0, 0), q2((100n << 4n) | ((50n << 4n) << 32n), 0));
const padTo = (b, n) => cat(b, zeros(n - b.length));
function vifStream() {
  const gif = drawSprite();
  return cat(vifcode(0x01, 0, 0x0101), vifcode(0x6C, 2, 0x0010), zeros(32), vifcode(0x00), vifcode(0x4A, 1, 0), zeros(8),
             vifcode(0x14, 0, 0x10), vifcode(0x00), vifcode(0x00), vifcode(0x00), vifcode(0x00), vifcode(0x50, 0, gif.length / 16), gif);
}
function chainDump() {   // cnt -> next -> ref -> call(sub: ret) -> end, a 64 KB dump at 0x00100000
  const base = 0x00100000, mem = zeros(0x10000);
  const put = (addr, data) => data.copy(mem, addr - base);
  const nop = vifcode(0x00).readUInt32LE(0), mark = (n) => vifcode(0x07, 0, n).readUInt32LE(0);
  put(0x00100000, dmatag(1, 1, 0, nop, mark(1)));
  put(0x00100010, cat(...Array(4).fill(vifcode(0x07, 0, 2))));
  put(0x00100020, dmatag(0, 2, 0x00100400, nop, mark(3)));
  put(0x00100400, dmatag(1, 3, 0x00100800, nop, nop));
  put(0x00100800, cat(...Array(4).fill(vifcode(0x07, 0, 4))));
  put(0x00100410, dmatag(0, 5, 0x00100C00, nop, nop));
  put(0x00100C00, dmatag(1, 6, 0, nop, nop));
  put(0x00100C10, cat(...Array(4).fill(vifcode(0x07, 0, 5))));
  put(0x00100420, dmatag(0, 7, 0, nop, mark(6)));
  return mem;
}
const tex0 = 0x1000n | (4n << 14n) | (0x13n << 20n) | (8n << 26n) | (7n << 30n) | (0x3000n << 37n);
const B = {
  upload_sprite: () => cat(upload32(8, 8), drawSprite()),
  upload44: () => cat(upload32(4, 4), drawSprite()),
  short: () => upload32(8, 8, 8),
  overrun: () => upload32(8, 8, 20),
  noset: () => cat(giftag(2, 2, []), zeros(32)),
  prim7: () => drawSprite(7),
  pre: () => cat(giftag(1, 0, [0x1], 1, 1, 3), q2(0, 0)),
  reglist: () => cat(giftag(3, 1, [0x1]), zeros(32)),
  nreg16: () => cat(giftag(1, 0, Array(16).fill(0xF)), zeros(256)),
  truncgif: () => cat(giftag(4, 0, [0x1]), zeros(32)),
  noeop: () => drawSprite(6, 0),
  unkreg: () => cat(giftag(1, 0, [0xE]), ad(0x7E, 1)),
  regs: () => cat(giftag(8, 0, [0xE]), ad(0x06, tex0), ad(0x00, 6), ad(0x53, 0), ad(0x4C, (5n) | (10n << 16n) | (0x02n << 24n) | (0xFF00FF00n << 32n)),
                  ad(0x4E, 7n | (1n << 24n) | (1n << 32n)), ad(0x51, 1n | (2n << 16n) | (3n << 32n) | (4n << 48n)), ad(0x40, 639n << 16n | (447n << 48n)),
                  ad(0x3F, 0xFFFFFFFFFFFFFFFFn)),
  vifstream: () => vifStream(),
  vifpad: () => cat(zeros(32), vifStream()),
  vifunk: () => cat(vifcode(0x00), vifcode(0x08), vifcode(0x00)),
  viftrunc: () => cat(vifcode(0x6C, 4), zeros(16)),
  vifmis: () => cat(vifcode(0x50, 0, 1), giftag(0, 0, [0x1])),
  vifstcycl: () => cat(vifcode(0x01, 0, 0x0401), vifcode(0x6C, 8), zeros(32), vifcode(0x00)),
  vifstmask: () => cat(vifcode(0x20), Buffer.from([1, 2, 3, 4]), vifcode(0x30), zeros(16), vifcode(0x07, 0, 5)),
  vifbadfmt: () => cat(vifcode(0x00), vifcode(0x63, 1)),
  vifmpgmis: () => cat(vifcode(0x4A, 1, 2), zeros(8)),
  vifstray: () => cat(vifcode(0x00), Buffer.from([1, 2])),
  vifirq: () => cat(vifcode(0x00, 0, 0, 1), vifcode(0x02, 0, 0x7FF), vifcode(0x6C | 0x10, 1, 0xC123), zeros(16)),
  chain: () => chainDump(),
  loop: () => padTo(dmatag(0, 2, 0x0), 64),
  offend: () => padTo(dmatag(0, 2, 0x00200000), 64),
  call3: () => { const m = zeros(256); dmatag(0, 5, 0x40).copy(m, 0); dmatag(0, 5, 0x80).copy(m, 0x40); dmatag(0, 5, 0xC0).copy(m, 0x80); dmatag(0, 7).copy(m, 0xC0); return m; },
  tie: () => { const m = zeros(128); dmatag(0, 2, 0x40, 0, 0, 1).copy(m, 0); dmatag(0, 7).copy(m, 0x40); return m; },
  zeros64: () => zeros(64),
  maxtags: () => cat(...Array(40).fill(0).map(() => dmatag(0, 1))),
  dmagif: () => { const m = zeros(0x200), p = cat(upload32(4, 4), drawSprite()); dmatag(p.length / 16, 7).copy(m, 0); p.copy(m, 16); return m; },
  spr: () => padTo(cat(dmatag(1, 3, 0x80000040), dmatag(0, 7)), 128),
  qwcout: () => padTo(dmatag(8, 1), 64),
};
const [name, out, ...rest] = process.argv.slice(2);
if (name === 'list') console.log(Object.keys(B).join(' '));
else if (name === 'vifop') { const [op, num, cyc] = rest.map(Number); fs.writeFileSync(out, cat(cyc === undefined ? zeros(0) : vifcode(0x01, 0, cyc), vifcode(op, num))); }
else { if (!B[name]) throw new Error('unknown buffer ' + name); fs.writeFileSync(out, B[name]()); }

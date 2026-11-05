// gen_fixtures.js - writes the binary fixtures for packtools/tests/modder_3d.sh into a folder (default: argv[2]):
//   unit.glb      a two-part textured model (box body with an embedded PNG checker, untextured red nose on +X via a
//                 rotated child node, no normals on the nose) for `um render3d`
//   viewmodel.b3d a skinned, animated Blitz3D viewmodel (2 brushes, 3 bones + a leaf node, a duplicate node name, keys
//                 split across two KEYS chunks, ANIM and two SEQS ranges) for `um b3d`
//   arms.png, gun.png  its textures
//   lzo.upk       a fully-compressed UE3 package whose one chunk is a hand-encoded LZO1X stream (literal runs, M1, M2,
//                 M3 matches and the M4 end marker) for `um upk`; it decompresses to UPK_EXPECTED
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const out = process.argv[2] || '.';
fs.mkdirSync(out, { recursive: true });

// ---------------------------------------------------------------- PNG
function crc32(buf) { let c, crc = 0xffffffff; for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 0xff; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; } return (crc ^ 0xffffffff) >>> 0; }
function chunk(type, data) { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type, 'latin1'), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); }
function png(w, h, pixel) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; for (let x = 0; x < w; x++) { const p = pixel(x, y); for (let k = 0; k < 4; k++) raw[y * (w * 4 + 1) + 1 + x * 4 + k] = p[k]; } }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
const checker = png(8, 8, (x, y) => ((x >> 1) + (y >> 1)) % 2 ? [40, 90, 200, 255] : [230, 230, 230, 255]);

// ---------------------------------------------------------------- GLB
function f32(arr) { const b = Buffer.alloc(arr.length * 4); arr.forEach((v, i) => b.writeFloatLE(v, i * 4)); return b; }
function u16(arr) { const b = Buffer.alloc(arr.length * 2); arr.forEach((v, i) => b.writeUInt16LE(v, i * 2)); return b; }
function glb() {
  // box body: 2 x 1 x 1 (glTF: x long side, y up), 24 vertices with normals and uvs
  const P = [], N = [], U = [], I = [];
  const faces = [[[1, 0, 0], [0, 1, 0]], [[-1, 0, 0], [0, 1, 0]], [[0, 1, 0], [0, 0, 1]], [[0, -1, 0], [0, 0, 1]], [[0, 0, 1], [0, 1, 0]], [[0, 0, -1], [0, 1, 0]]];
  const half = [1, 0.5, 0.5];
  for (const [n, up] of faces) {
    const r = [up[1] * n[2] - up[2] * n[1], up[2] * n[0] - up[0] * n[2], up[0] * n[1] - up[1] * n[0]];
    const base = P.length / 3;
    for (const [a, b, u, v] of [[-1, -1, 0, 1], [1, -1, 1, 1], [1, 1, 1, 0], [-1, 1, 0, 0]]) {
      for (let k = 0; k < 3; k++) P.push((n[k] + r[k] * a + up[k] * b) * half[k] + (k === 1 ? 0.5 : 0));
      N.push(...n); U.push(u, v);
    }
    I.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  // nose: a pyramid, no normals, positions in its own node space (node turned 90 deg about y, so local +z -> +x)
  const NP = [0, 0, 0.6, -0.3, -0.25, 0, 0.3, -0.25, 0, 0.3, 0.25, 0, -0.3, 0.25, 0];
  const NI = [0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1, 1, 3, 2, 1, 4, 3];
  const parts = [f32(P), f32(N), f32(U), u16(I), f32(NP), u16(NI), checker];
  const views = []; let off = 0; const bufs = [];
  for (const p of parts) { views.push({ buffer: 0, byteOffset: off, byteLength: p.length }); bufs.push(p); off += p.length; const pad = (4 - (off % 4)) % 4; if (pad) { bufs.push(Buffer.alloc(pad)); off += pad; } }
  const bin = Buffer.concat(bufs);
  const mm = (arr, n) => { const mn = Array(n).fill(Infinity), mx = Array(n).fill(-Infinity); for (let i = 0; i < arr.length; i++) { mn[i % n] = Math.min(mn[i % n], arr[i]); mx[i % n] = Math.max(mx[i % n], arr[i]); } return { min: mn, max: mx }; };
  const j = {
    asset: { version: '2.0', generator: 'gen_fixtures.js' }, scene: 0, scenes: [{ nodes: [0] }],
    nodes: [{ name: 'unit', mesh: 0, children: [1], translation: [5, 0, -3] }, { name: 'nose', mesh: 1, translation: [1, 0.5, 0], rotation: [0, Math.SQRT1_2, 0, Math.SQRT1_2] }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3, material: 0 }] }, { primitives: [{ attributes: { POSITION: 4 }, indices: 5, material: 1 }] }],
    materials: [{ pbrMetallicRoughness: { baseColorTexture: { index: 0 } } }, { pbrMetallicRoughness: { baseColorFactor: [0.8, 0.05, 0.05, 1] } }],
    textures: [{ source: 0, sampler: 0 }], samplers: [{ magFilter: 9728, wrapS: 10497, wrapT: 10497 }], images: [{ bufferView: 6, mimeType: 'image/png' }],
    accessors: [
      { bufferView: 0, componentType: 5126, count: P.length / 3, type: 'VEC3', ...mm(P, 3) }, { bufferView: 1, componentType: 5126, count: N.length / 3, type: 'VEC3' },
      { bufferView: 2, componentType: 5126, count: U.length / 2, type: 'VEC2' }, { bufferView: 3, componentType: 5123, count: I.length, type: 'SCALAR' },
      { bufferView: 4, componentType: 5126, count: NP.length / 3, type: 'VEC3', ...mm(NP, 3) }, { bufferView: 5, componentType: 5123, count: NI.length, type: 'SCALAR' },
    ],
    bufferViews: views, buffers: [{ byteLength: bin.length }],
  };
  let js = Buffer.from(JSON.stringify(j)); const jp = (4 - (js.length % 4)) % 4; js = Buffer.concat([js, Buffer.alloc(jp, 0x20)]);
  const head = Buffer.alloc(12); head.write('glTF', 0, 'latin1'); head.writeUInt32LE(2, 4); head.writeUInt32LE(12 + 8 + js.length + 8 + bin.length, 8);
  const ch = (len, type) => { const b = Buffer.alloc(8); b.writeUInt32LE(len, 0); b.writeUInt32LE(type, 4); return b; };
  return Buffer.concat([head, ch(js.length, 0x4e4f534a), js, ch(bin.length, 0x004e4942), bin]);
}
fs.writeFileSync(path.join(out, 'unit.glb'), glb());
// the same model as a .gltf with the buffer in a data: URI
{
  const g = fs.readFileSync(path.join(out, 'unit.glb')); const jl = g.readUInt32LE(12); const j = JSON.parse(g.subarray(20, 20 + jl).toString('utf8'));
  const bin = g.subarray(20 + jl + 8); j.buffers[0].uri = 'data:application/octet-stream;base64,' + bin.toString('base64');
  fs.writeFileSync(path.join(out, 'unit.gltf'), JSON.stringify(j));
}

// ---------------------------------------------------------------- B3D
const i32 = (...v) => { const b = Buffer.alloc(4 * v.length); v.forEach((x, k) => b.writeInt32LE(x, k * 4)); return b; };
const fl = (...v) => f32(v);
const cs = (s) => Buffer.concat([Buffer.from(s, 'latin1'), Buffer.from([0])]);
const ck = (tag, ...parts) => { const body = Buffer.concat(parts); return Buffer.concat([Buffer.from(tag, 'latin1'), i32(body.length), body]); };
function b3d() {
  const texs = ck('TEXS', cs('Textures\\arms.png'), i32(1, 2), fl(0, 0), fl(1, 1), fl(0), cs('gun.png'), i32(4, 2), fl(0, 0), fl(1, 1), fl(0));
  const brus = ck('BRUS', i32(1), cs('arms'), fl(1, 1, 1, 1), fl(0), i32(1, 0), i32(0), cs('gun'), fl(1, 0.9, 0.8, 1), fl(0.5), i32(1, 0), i32(1));
  // a box from (-3,-3,10) to (3,3,20): 8 vertices, normals point outwards (corner directions), uv per corner
  const vr = [];
  for (let k = 0; k < 8; k++) {
    const x = k & 1 ? 3 : -3, y = k & 2 ? 3 : -3, z = k & 4 ? 20 : 10;
    const n = [Math.sign(x), Math.sign(y), z > 15 ? 1 : -1].map((c) => c / Math.sqrt(3));
    vr.push(fl(x, y, z), fl(...n), fl(k & 1 ? 1 : 0, k & 2 ? 0 : 1));
  }
  const vrts = ck('VRTS', i32(1, 1, 2), ...vr);
  const tris0 = ck('TRIS', i32(0), i32(0, 2, 1, 1, 2, 3, 0, 1, 5, 0, 5, 4, 0, 4, 6, 0, 6, 2));   // brush 0: front-ish faces
  const tris1 = ck('TRIS', i32(1), i32(4, 5, 7, 4, 7, 6, 1, 3, 7, 1, 7, 5, 2, 6, 7, 2, 7, 3));   // brush 1
  const mesh = ck('MESH', i32(-1), vrts, tris0, tris1);
  const keysRot = []; for (let f = 1; f <= 10; f++) { const a = (f - 1) * 0.05; keysRot.push(i32(f), fl(Math.cos(a / 2), 0, Math.sin(a / 2), 0)); }
  const keysPos = ck('KEYS', i32(1), i32(1), fl(1, 2, 3), i32(10), fl(1, 2.5, 3));
  const weaponfire = ck('NODE', cs('weaponfire'), fl(0, 0, 17.03), fl(1, 1, 1), fl(1, 0, 0, 0));
  const gun = ck('NODE', cs('gun'), fl(0, 0, 5), fl(1, 1, 1), fl(1, 0, 0, 0), ck('BONE', i32(4), fl(0.5), i32(5), fl(1), i32(6), fl(1), i32(7), fl(1)), weaponfire);
  const arm = ck('NODE', cs('arm'), fl(1, 2, 3), fl(1, 1, 1), fl(1, 0, 0, 0), ck('BONE', i32(0), fl(1), i32(1), fl(1), i32(2), fl(1), i32(3), fl(1), i32(4), fl(0.5)), ck('KEYS', i32(4), ...keysRot), keysPos, gun);
  const dup = ck('NODE', cs('Scene_Root'), fl(0, 0, 0), fl(1, 1, 1), fl(1, 0, 0, 0), ck('XTRA', i32(7)));
  const root = ck('NODE', cs('Scene_Root'), fl(0, 0, 0), fl(1, 1, 1), fl(1, 0, 0, 0), mesh, ck('ANIM', i32(0, 10), fl(30)), ck('SEQS', cs('idle'), i32(1, 5, 0)), ck('SEQS', cs('fire'), i32(6, 10, 0)), arm, dup);
  return ck('BB3D', i32(1), texs, brus, root);
}
fs.writeFileSync(path.join(out, 'viewmodel.b3d'), b3d());
fs.writeFileSync(path.join(out, 'arms.png'), png(4, 4, (x, y) => [200, 150 + x * 10, 120 + y * 10, 255]));
fs.writeFileSync(path.join(out, 'gun.png'), png(4, 4, (x, y) => (x + y) % 2 ? [60, 60, 60, 255] : [90, 90, 90, 0]));

// ---------------------------------------------------------------- UPK (LZO1X)
const lits = Buffer.from('0123456789ABCDEFGHIJ', 'latin1');
const stream = Buffer.concat([
  Buffer.from([17 + 4]), Buffer.from('abcd', 'latin1'),      // first literal run: 4 bytes
  Buffer.from([0x6e, 0x00]), Buffer.from('XY', 'latin1'),    // M2: length 4, distance 4, then 2 trailing literals
  Buffer.from([0x04, 0x00]),                                 // M1: length 2, distance 2
  Buffer.from([0x28, 0x2c, 0x00]),                           // M3: length 10, distance 12
  Buffer.from([0x00, 0x02]), lits,                           // literal run: 15 + 2 + 3 = 20 bytes
  Buffer.from([0x11, 0x00, 0x00]),                           // M4 distance 0: end of stream
]);
const expected = 'abcdabcdXYXYabcdabcdXY0123456789ABCDEFGHIJ';
const hdrEnd = 0x79 + 8 + 16;
const head = Buffer.alloc(hdrEnd);
head.writeUInt32LE(0x9e2a83c1, 0); head.writeUInt16LE(801, 4); head.writeUInt16LE(30, 6); head.writeUInt32LE(hdrEnd, 8);
head.write('None', 0x20, 'latin1');
const chunkBuf = Buffer.concat([i32(0x9e2a83c1 | 0, 0x20000, stream.length, expected.length, stream.length, expected.length), stream]);
head.writeUInt32LE(2, 0x79); head.writeUInt32LE(1, 0x7d);
head.writeUInt32LE(hdrEnd, 0x81); head.writeUInt32LE(expected.length, 0x85); head.writeUInt32LE(hdrEnd, 0x89); head.writeUInt32LE(chunkBuf.length, 0x8d);
fs.writeFileSync(path.join(out, 'lzo.upk'), Buffer.concat([head, chunkBuf]));
fs.writeFileSync(path.join(out, 'UPK_EXPECTED'), expected);
// a zlib-flagged copy (refused) and one with a corrupt stream (bad lookbehind)
const z = Buffer.concat([head, chunkBuf]); z.writeUInt32LE(1, 0x79); fs.writeFileSync(path.join(out, 'zlib.upk'), z);
const bad = Buffer.concat([head, chunkBuf]); bad[hdrEnd + 24 + 5 + 1] = 0x7f; fs.writeFileSync(path.join(out, 'bad.upk'), bad);

// ---------------------------------------------------------------- AoE2 DE .sld (um sld)
const u16s = (...v) => { const b = Buffer.alloc(2 * v.length); v.forEach((x, i) => b.writeUInt16LE(x & 0xffff, 2 * i)); return b; };
const u32s = (...v) => { const b = Buffer.alloc(4 * v.length); v.forEach((x, i) => b.writeUInt32LE(x >>> 0, 4 * i)); return b; };
const sldLayer = (box, flags, unk, cmds, blocks) => {
  const body = Buffer.concat([box ? u16s(...box) : Buffer.alloc(0), Buffer.from([flags, unk]), u16s(cmds.length), Buffer.from(cmds.flat()), ...blocks]);
  const len = 4 + body.length; return Buffer.concat([u32s(len), body, Buffer.alloc((4 - len % 4) % 4)]);
};
const bc1 = (c0, c1, idx) => Buffer.concat([u16s(c0, c1), u32s(idx)]);
const bc4 = (a0, a1, sels) => { const b = Buffer.alloc(8); b[0] = a0; b[1] = a1; let bits = 0n; for (let i = 0; i < 16; i++) bits |= BigInt(Array.isArray(sels) ? sels[i] || 0 : sels) << BigInt(3 * i); for (let k = 0; k < 6; k++) b[2 + k] = Number((bits >> BigInt(8 * k)) & 255n); return b; };
fs.writeFileSync(path.join(out, 'unit.sld'), Buffer.concat([
  Buffer.from('SLDX', 'latin1'), u16s(4, 2, 0, 0x10), u32s(0xff),
  // frame 0: 8x8 canvas, hotspot (4, 6); main = red block 0 + blue block 3, shadow 200 under block 3, empty 0x04, player on block 0
  u16s(8, 8, 4, 6), Buffer.from([0x17, 0]), u16s(0),
  sldLayer([0, 0, 8, 8], 0, 1, [[0, 1], [2, 1]], [bc1(0xF800, 0x001F, 0), bc1(0xF800, 0x001F, 0x55555555)]),
  sldLayer([4, 4, 8, 8], 1, 1, [[0, 1]], [bc4(200, 0, 0)]),
  sldLayer(null, 5, 0, [], []),
  sldLayer(null, 1, 1, [[0, 1]], [bc4(255, 0, 0)]),
  // frame 1: main only, flag 0x80 = keep the previous frame's blocks, draw a green block 1
  u16s(8, 8, 4, 6), Buffer.from([0x01, 0]), u16s(1),
  sldLayer([0, 0, 8, 8], 0x80, 1, [[1, 1]], [bc1(0x07E0, 0x0000, 0)]),
]));
// frames for `um sld write`: a 12x8 gradient sprite with a shadow file next to one of them, and an odd-sized one
for (let f = 0; f < 2; f++) {
  fs.writeFileSync(path.join(out, `sp_${f}.png`), png(12, 8, (x, y) => (x >= 2 && x < 10 && y >= 1 && y < 7) ? [20 * x, 30 * y, 100 + 40 * f, 255] : [0, 0, 0, 0]));
  fs.writeFileSync(path.join(out, `sp_${f}_p.png`), png(12, 8, (x, y) => [0, 0, 0, (x >= 2 && x < 6 && y >= 1 && y < 7) ? 255 : 0]));
}
fs.writeFileSync(path.join(out, 'sp_0_s.png'), png(12, 8, (x, y) => [0, 0, 0, (y >= 5) ? 150 : 0]));
fs.writeFileSync(path.join(out, 'odd.png'), png(10, 7, () => [255, 255, 255, 255]));

// ---------------------------------------------------------------- Valve VTF (um vtf)
const vtfHeader = (minor, hsize, w, h, frames, fmt, mips) => {
  const b = Buffer.alloc(hsize); b.write('VTF\0', 0, 'latin1'); b.writeUInt32LE(7, 4); b.writeUInt32LE(minor, 8); b.writeUInt32LE(hsize, 12);
  b.writeUInt16LE(w, 16); b.writeUInt16LE(h, 18); b.writeUInt32LE(0, 20); b.writeUInt16LE(frames, 24); b.writeInt32LE(fmt, 52); b[56] = mips; b.writeInt32LE(-1, 57);
  if (minor >= 2) b.writeUInt16LE(1, 63);
  return b;
};
// 7.2 DXT1 4x4: pixel 0 red (index 0), 1 blue (1), 2 (2a+b)/3, 3 (a+2b)/3
fs.writeFileSync(path.join(out, 'dxt1.vtf'), Buffer.concat([vtfHeader(2, 80, 4, 4, 1, 13, 1), bc1(0xF800, 0x001F, 0xE4)]));
// 7.1 DXT5 4x4: alpha 255 / 0 / 218 for pixels 0-2, colour green
fs.writeFileSync(path.join(out, 'dxt5.vtf'), Buffer.concat([vtfHeader(1, 64, 4, 4, 1, 15, 1), bc4(255, 0, [0, 1, 2]), bc1(0x07E0, 0, 0)]));
// 7.3 BGRA8888 2x2, 2 mips (1x1 first), 2 frames, image found through the resource table
{
  const h = vtfHeader(3, 96, 2, 2, 2, 12, 2); h.writeUInt32LE(2, 68);
  Buffer.from([0x01, 0, 0, 0]).copy(h, 80); h.writeUInt32LE(96, 84); Buffer.from([0x30, 0, 0, 0]).copy(h, 88); h.writeUInt32LE(96, 92);
  const px = (b, g, r, a) => Buffer.from([b, g, r, a]);
  fs.writeFileSync(path.join(out, 'bgra.vtf'), Buffer.concat([h, px(9, 9, 9, 9), px(8, 8, 8, 8),
    px(10, 20, 30, 40), px(50, 60, 70, 80), px(90, 100, 110, 120), px(130, 140, 150, 160),
    px(1, 2, 3, 4), px(5, 6, 7, 8), px(9, 10, 11, 12), px(13, 14, 15, 16)]));
}

// ---------------------------------------------------------------- Valve VPK (um vpk)
const vpkEntry = (preload, arch, off, len) => Buffer.concat([u32s(0), u16s(preload.length, arch), u32s(off, len), u16s(0xffff), Buffer.from(preload, 'latin1')]);
const cz = (s) => Buffer.from(s + '\0', 'latin1');
const tree = Buffer.concat([
  cz('vmt'), cz('materials/models'), cz('gun'), vpkEntry('AB', 0x7fff, 0, 4), cz(''), cz(''),
  cz('wav'), cz('sound'), cz('Fire'), vpkEntry('', 0, 3, 5), cz(''), cz(''),
  cz(' '), cz(' '), cz('README'), vpkEntry('', 0x7fff, 4, 3), cz(''), cz(''),
  cz(''),
]);
fs.writeFileSync(path.join(out, 'pak01_dir.vpk'), Buffer.concat([u32s(0x55aa1234, 1, tree.length), tree, Buffer.from('CDEFXYZ', 'latin1')]));
fs.writeFileSync(path.join(out, 'pak01_000.vpk'), Buffer.from('zzzBOOM!', 'latin1'));
fs.writeFileSync(path.join(out, 'pak02_dir.vpk'), Buffer.concat([u32s(0x55aa1234, 2, tree.length, 7, 0, 0, 0), tree, Buffer.from('CDEFXYZ', 'latin1')]));
const evil = Buffer.concat([cz('txt'), cz('../../evil'), cz('x'), vpkEntry('hi', 0x7fff, 0, 0), cz(''), cz(''), cz('')]);
fs.writeFileSync(path.join(out, 'evil_dir.vpk'), Buffer.concat([u32s(0x55aa1234, 1, evil.length), evil]));

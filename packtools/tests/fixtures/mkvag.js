// mkvag.js - synthetic audio for the console-sound/vag suite (mirrors console-skill tests/test_sound.py, MIT).
// All audio is generated here (sines, chords, squares); WAVs are written and read back by this file's own RIFF code,
// so the tool's WAV code is checked against an independent reader. No game data anywhere.
//   node mkvag.js wav <out.wav> '<json>'        json: {"rate":22050,"width":2,"chans":[SPEC,...]}
//   node mkvag.js snr '<SPEC>' <file.wav> <skip> [chan]   SNR (dB) of the WAV's samples [skip:skip+len] vs SPEC
//   node mkvag.js samples <file.wav> [chan] [start] [count]   JSON {rate, channels, n, head}
//   node mkvag.js bytes <out> <hex>...          raw bytes (frames for the decoder checks)
//   node mkvag.js refdecode <hex>...            JSON samples of the straight-line ps2_audio_vag.cpp reference
//   node mkvag.js strip <in.vag> <out>          the ADPCM after a 48-byte header (raw bank data)
//   node mkvag.js cat <out> <in|zeros:N|junk:N>...   concatenate pieces
// SPEC: {"sine":[n,freq,amp,rate]} | {"sum":[SPEC,SPEC]} | {"square":n} | {"list":[...]} | {"zeros":n}
const fs = require('fs');

function gen(spec) {
  if (spec.sine) {
    const [n, freq = 440, amp = 12000, rate = 22050] = spec.sine;
    const o = new Array(n);
    for (let i = 0; i < n; i++) o[i] = Math.trunc(amp * Math.sin(2 * Math.PI * freq * i / rate));
    return o;
  }
  if (spec.sum) { const a = gen(spec.sum[0]), b = gen(spec.sum[1]); return a.map((v, i) => v + b[i]); }
  if (spec.square !== undefined) { const o = []; for (let i = 0; i < spec.square; i++) o.push(Math.floor(i / 20) % 2 ? 32767 : -32768); return o; }
  if (spec.list) return spec.list.slice();
  if (spec.zeros !== undefined) return new Array(spec.zeros).fill(0);
  throw new Error('bad spec ' + JSON.stringify(spec));
}

function writeWav(path, chans, rate, width) {
  const ch = chans.length, n = ch ? chans[0].length : 0;
  const data = Buffer.alloc(n * ch * width);
  let p = 0;
  for (let i = 0; i < n; i++) for (const c of chans) {
    if (width === 2) { data.writeInt16LE(c[i], p); p += 2; } else { data.writeUInt8((c[i] >> 8) + 128, p); p += 1; }
  }
  const fmt = Buffer.alloc(24);
  fmt.write('fmt ', 0); fmt.writeUInt32LE(16, 4); fmt.writeUInt16LE(1, 8); fmt.writeUInt16LE(ch, 10);
  fmt.writeUInt32LE(rate, 12); fmt.writeUInt32LE(rate * ch * width, 16); fmt.writeUInt16LE(ch * width, 20); fmt.writeUInt16LE(width * 8, 22);
  const dh = Buffer.alloc(8); dh.write('data', 0); dh.writeUInt32LE(data.length, 4);
  const pad = data.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0);
  const body = Buffer.concat([Buffer.from('WAVE'), fmt, dh, data, pad]);
  const riff = Buffer.alloc(8); riff.write('RIFF', 0); riff.writeUInt32LE(body.length, 4);
  fs.writeFileSync(path, Buffer.concat([riff, body]));
}

function readWav(path) {
  const b = fs.readFileSync(path);
  if (b.toString('latin1', 0, 4) !== 'RIFF' || b.toString('latin1', 8, 12) !== 'WAVE') throw new Error('not RIFF');
  let pos = 12, ch = 0, rate = 0, bits = 0, data = null, smpl = null;
  while (pos + 8 <= b.length) {
    const id = b.toString('latin1', pos, pos + 4), size = b.readUInt32LE(pos + 4), body = b.subarray(pos + 8, pos + 8 + size);
    if (id === 'fmt ') { ch = body.readUInt16LE(2); rate = body.readUInt32LE(4); bits = body.readUInt16LE(14); }
    else if (id === 'data') data = body;
    else if (id === 'smpl' && body.length >= 60) smpl = [body.readUInt32LE(44), body.readUInt32LE(48)];
    pos += 8 + size + (size & 1);
  }
  if (bits !== 16) throw new Error('bits ' + bits);
  const n = Math.floor(data.length / (2 * ch)), chans = [];
  for (let c = 0; c < ch; c++) { const o = new Array(n); for (let i = 0; i < n; i++) o[i] = data.readInt16LE((i * ch + c) * 2); chans.push(o); }
  return { rate, chans, smpl };
}

function snr(ref, test) {
  const n = Math.min(ref.length, test.length);
  let sig = 0n, noise = 0n;
  for (let i = 0; i < n; i++) { const r = BigInt(ref[i]), d = r - BigInt(test[i]); sig += r * r; noise += d * d; }
  if (noise === 0n) return Infinity;
  if (sig === 0n) return 0;
  return 10 * Math.log10(Number(sig) / Number(noise));
}

// straight-line reference of ps2_audio_vag.cpp for a run of frames (C division truncates toward zero)
function refDecode(frames) {
  const tab = [[0, 0], [60, 0], [115, -52], [98, -55], [122, -60]];
  let s1 = 0, s2 = 0; const out = [];
  for (const f of frames) {
    let shift = f[0] & 0x0F, filt = (f[0] >> 4) & 7;
    if (shift > 12) shift = 9;
    if (filt > 4) filt = 0;
    for (let i = 0; i < 28; i++) {
      const b = f[2 + (i >> 1)];
      let nb = i & 1 ? b >> 4 : b & 0x0F;
      if (nb & 8) nb -= 16;
      const [k1, k2] = tab[filt];
      const t = k1 * s1 + k2 * s2 + 32;
      let v = nb * (1 << (12 - shift)) + Math.trunc(t / 64);
      v = Math.max(-32768, Math.min(32767, v));
      s2 = s1; s1 = v; out.push(v);
    }
  }
  return out;
}

const [cmd, ...a] = process.argv.slice(2);
if (cmd === 'wav') {
  const j = JSON.parse(a[1]);
  writeWav(a[0], j.chans.map(gen), j.rate || 22050, j.width || 2);
} else if (cmd === 'snr') {
  const ref = gen(JSON.parse(a[0])), w = readWav(a[1]), skip = +a[2], c = +(a[3] || 0);
  const v = snr(ref, w.chans[c].slice(skip, skip + ref.length));
  console.log(v === Infinity ? 'inf' : v.toFixed(3));
} else if (cmd === 'samples') {
  const w = readWav(a[0]), c = +(a[1] || 0), st = +(a[2] || 0), cnt = +(a[3] || 8);
  console.log(JSON.stringify({ rate: w.rate, channels: w.chans.length, n: w.chans[c].length, head: w.chans[c].slice(st, st + cnt), smpl: w.smpl }));
} else if (cmd === 'bytes') {
  fs.writeFileSync(a[0], Buffer.from(a.slice(1).join(''), 'hex'));
} else if (cmd === 'refdecode') {
  const frames = a.map(h => Buffer.from(h, 'hex'));
  console.log(JSON.stringify(refDecode(frames)));
} else if (cmd === 'strip') {
  fs.writeFileSync(a[1], fs.readFileSync(a[0]).subarray(48));
} else if (cmd === 'cat') {
  const parts = a.slice(1).map(p => {
    if (p.startsWith('zeros:')) return Buffer.alloc(+p.slice(6));
    if (p.startsWith('junk:')) { const n = +p.slice(5), b = Buffer.alloc(n); for (let i = 0; i < n; i++) b[i] = (i * 31) & 0xFF; return b; }
    return fs.readFileSync(p);
  });
  fs.writeFileSync(a[0], Buffer.concat(parts));
} else {
  console.error('usage: see the header'); process.exit(2);
}

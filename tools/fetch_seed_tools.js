#!/usr/bin/env node
// Prepares tools to bundle with the personal desktop build (seed-tools/), laid out exactly as the engine's own
// installer leaves them in <data>/tools/<id>/. Downloads and SHA-256 values come from the engine's tool catalog
// (TOOLS_CATALOG in astro_tools.inc), so they are the same pinned files Settings would install.
//   node tools/fetch_seed_tools.js PLATFORM OUT_DIR ID...     e.g.  win-x64 .cache/seed-tools/win-x64 piper piper-voice-fr
// Then: PERSONAL_SEED_TOOLS=OUT_DIR bash desktop/package.sh     (the engine installs them once, on first start)
// Supports the catalog's "pinned" (zip / tar.gz archive) and "files" sources plus their "extras" (voice models).
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto'), { execFileSync } = require('child_process');
const [plat, out, ...ids] = process.argv.slice(2);
if (!plat || !out || !ids.length) { console.error('usage: fetch_seed_tools.js PLATFORM OUT_DIR ID...'); process.exit(2); }

const src = fs.readFileSync(path.join(__dirname, '..', 'astro_tools.inc'), 'utf8');
function spec(id) {
  const line = src.split('\n').find(l => l.trimStart().startsWith(`{"id":"${id}"`));
  if (!line) throw new Error(`${id} is not in the tool catalog`);
  return JSON.parse(line.trim().replace(/,$/, ''));
}
async function download(url, sha, dest) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  const buf = Buffer.from(await r.arrayBuffer());
  const got = crypto.createHash('sha256').update(buf).digest('hex');
  if (got !== sha.toLowerCase()) throw new Error(`checksum mismatch for ${url}`);
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.writeFileSync(dest, buf);
  return buf.length;
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const seedPath = path.join(out, 'seed.json');
  const seed = fs.existsSync(seedPath) ? JSON.parse(fs.readFileSync(seedPath, 'utf8')) : { tools: {} };
  for (const id of ids) {
    const s = spec(id), dir = path.join(out, id);
    fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true });
    let source = '', version = s.src.version || '1';
    if (s.src.type === 'pinned') {
      const f = s.src.files[plat] || (plat === 'win-arm64' && s.src.files['win-x64']) || s.src.files['*'];
      if (!f) throw new Error(`${id}: no download for ${plat}`);
      const name = f.url.slice(f.url.lastIndexOf('/') + 1), tmp = path.join(out, '.dl-' + name);
      const n = await download(f.url, f.sha256, tmp);
      if (/\.zip$/i.test(name)) execFileSync('unzip', ['-q', tmp, '-d', dir]);
      else if (/\.(tar\.gz|tgz)$/i.test(name)) execFileSync('tar', ['-xzf', tmp, '-C', dir]);
      else throw new Error(`${id}: unsupported archive ${name}`);
      fs.rmSync(tmp); source = f.url;
      console.log(`${id}: ${name} (${(n / 1048576).toFixed(1)} MB, SHA-256 OK)`);
    } else if (s.src.type !== 'files') throw new Error(`${id}: source type ${s.src.type} cannot be bundled`);
    for (const e of s.extras || []) {
      if (!e.path || e.path.startsWith('/') || e.path.includes('..')) throw new Error(`${id}: bad extra path`);
      const n = await download(e.url, e.sha256, path.join(dir, e.path));
      if (!source) source = e.url;
      console.log(`${id}: ${e.path} (${(n / 1048576).toFixed(1)} MB, SHA-256 OK)`);
    }
    seed.tools[id] = { version, source, verified: 'sha256' };
  }
  fs.writeFileSync(seedPath, JSON.stringify(seed, null, 2) + '\n');
  console.log(`seed.json: ${Object.keys(seed.tools).join(', ')}`);
})().catch(e => { console.error('FAILED: ' + e.message); process.exit(1); });

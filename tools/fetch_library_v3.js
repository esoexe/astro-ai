#!/usr/bin/env node
// Downloads an offline copy of the community Library (schema 3) for the personal desktop build:
//   node tools/fetch_library_v3.js OUT_DIR [BASE_URL]
// Then: PERSONAL_LIBRARY_V3=OUT_DIR bash desktop/package.sh
// Files: manifest.json, list/<kind>-<n>.json.gz, detail/<3 hex>.json.gz, q/<2 chars>.json.gz (missing q/ files are normal).
'use strict';
const fs = require('fs'), path = require('path');
const out = process.argv[2];
const base = (process.argv[3] || 'https://esoexe.github.io/astro-library/v3/').replace(/\/?$/, '/');
if (!out) { console.error('usage: fetch_library_v3.js OUT_DIR [BASE_URL]'); process.exit(2); }

async function get(rel, optional) {
  const dest = path.join(out, rel);
  if (fs.existsSync(dest) && fs.statSync(dest).size > 0 && rel !== 'manifest.json') return 'skip';
  for (let attempt = 1; ; attempt++) {
    try {
      const r = await fetch(base + rel);
      if (r.status === 404 && optional) return 'missing';
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const buf = Buffer.from(await r.arrayBuffer());
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.writeFileSync(dest + '.part', buf); fs.renameSync(dest + '.part', dest);
      return 'ok';
    } catch (e) {
      if (attempt >= 5) throw new Error(rel + ': ' + e.message);
      await new Promise(res => setTimeout(res, 1000 * attempt * attempt));
    }
  }
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  await get('manifest.json');
  const m = JSON.parse(fs.readFileSync(path.join(out, 'manifest.json'), 'utf8'));
  if (m.schema !== 3) throw new Error('not a schema 3 manifest');
  const jobs = [];
  for (const k of m.order) for (let i = 0; i < m.kinds[k].shards; i++) jobs.push([`list/${k}-${i}.json.gz`, false]);
  for (let i = 0; i < 4096; i++) jobs.push([`detail/${i.toString(16).padStart(3, '0')}.json.gz`, false]);
  const al = 'abcdefghijklmnopqrstuvwxyz0123456789';
  for (const a of al) for (const b of al) jobs.push([`q/${a}${b}.json.gz`, true]);
  const count = { ok: 0, skip: 0, missing: 0 }; let next = 0, done = 0;
  const worker = async () => { while (next < jobs.length) { const [rel, opt] = jobs[next++]; count[await get(rel, opt)]++; if (++done % 500 === 0) console.log(`${done}/${jobs.length}`); } };
  await Promise.all(Array.from({ length: 16 }, worker));
  console.log(`done: ${m.total} items; downloaded ${count.ok}, already had ${count.skip}, q/ files not on the server ${count.missing}`);
})().catch(e => { console.error('FAILED: ' + e.message); process.exit(1); });

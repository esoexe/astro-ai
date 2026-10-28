#!/usr/bin/env node
// Turns coding-bench results into web/model_rank.json, which the app uses to nudge coding leads:
// models that pass more bench tasks move up a little (live health from the server still wins).
//   node tests/coding_bench/make_rank.js [results-*.json ...]     (default: every results-single*.json here)
// Only the newest condition ("new" when present) counts, and models need at least 3 graded tasks.
'use strict';
const fs = require('fs');
const path = require('path');

const here = __dirname;
const files = process.argv.slice(2).length ? process.argv.slice(2) : fs.readdirSync(here).filter((f) => /^results-single.*\.json$/.test(f)).map((f) => path.join(here, f));
const rows = [];
for (const f of files) {
  try { const j = JSON.parse(fs.readFileSync(f, 'utf8')); if (Array.isArray(j)) rows.push(...j); } catch (e) { console.error(`skip ${f}: ${e.message}`); }
}
const hasNew = rows.some((r) => r.cond === 'new');
const by = new Map();
for (const r of rows) {
  if (!r || !r.model || (hasNew && r.cond && r.cond !== 'new')) continue;
  // Provider trouble (rate limits, outages) says nothing about code quality: leave it out.
  if (!r.pass && /rate.?limit|quota|unavailable|queue|server error|http (4\d\d|5\d\d)|invalid model/i.test(r.why || '')) continue;
  const m = by.get(r.model) || { n: 0, pass: 0, ms: [] };
  m.n++; if (r.pass) m.pass++; if (r.ms) m.ms.push(r.ms);
  by.set(r.model, m);
}
const models = {};
for (const [id, m] of [...by].sort((a, b) => a[0].localeCompare(b[0]))) {
  if (m.n < 3) continue;
  const med = m.ms.sort((a, b) => a - b)[Math.floor(m.ms.length / 2)] || 0;
  models[id] = { score: Math.round((m.pass / m.n) * 1000) / 1000, pass: m.pass, n: m.n, median_ms: med };
}
const out = { schema: 1, generated: new Date().toISOString().slice(0, 10), source: files.map((f) => path.basename(f)), models };
const dest = path.join(here, '..', '..', 'web', 'model_rank.json');
fs.writeFileSync(dest, JSON.stringify(out, null, 1) + '\n');
console.log(`model_rank.json: ${Object.keys(models).length} models from ${rows.length} results`);

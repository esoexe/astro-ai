#!/usr/bin/env node
// Merges tools/providers_extra.json (paid cloud platforms + local model servers) into a providers document.
// Used by tools/build_providers.js, and runnable alone: node tools/merge_providers.js
'use strict';
const fs = require('fs');
const path = require('path');

function mergeExtra(doc) {
  const extra = JSON.parse(fs.readFileSync(path.join(__dirname, 'providers_extra.json'), 'utf8'));
  const paid = extra.paid.map((p) => ({ kind: 'openai', ...p, paid: true, auto_models: p.auto_models || 1, wait_seconds: p.wait_seconds || 90 }));
  const local = extra.local.map((p) => ({
    kind: 'openai', ...p, local: true, keyless: true, discover: true, models: p.models || [], max_models: p.max_models || 40,
    auto_models: 1, parallel: 1, wait_seconds: p.wait_seconds || 240,   // Colibri streams experts from disk: slower, so it waits longer
    limits: p.limits || `Runs on your own computer: free, uses your RAM/CPU/GPU. ${p.hint || ''}`.trim(),
    privacy: 'Stays on your computer',
  }));
  const astro = (extra.astro || []).map((p) => ({ ...p }));
  // Routers on this computer that call cloud providers with your own keys (FreeLLMAPI): a key, a fixed model list
  const routers = (extra.routers || []).map((p) => ({ kind: 'openai', ...p }));   // Astro API gateway (ASTRO_API_KEY): one key for the user's whole Astro account
  const ids = new Set([...astro, ...paid, ...local, ...routers].map((p) => p.id));
  const list = doc.providers.filter((p) => !ids.has(p.id));
  const at = (id, fallback) => { const i = list.findIndex((p) => p.id === id); return i < 0 ? fallback : i; };
  list.splice(at('opencode-paid', list.length), 0, ...astro, ...paid);
  list.splice(at('local', list.length), 0, ...routers, ...local);
  doc.providers = list;
  return { astro: astro.length, paid: paid.length, local: local.length, routers: routers.length };
}
module.exports = { mergeExtra };

if (require.main === module) {
  const file = path.join(__dirname, '..', 'providers.json');
  const doc = JSON.parse(fs.readFileSync(file, 'utf8'));
  const n = mergeExtra(doc);
  fs.writeFileSync(file, JSON.stringify(doc, null, 2) + '\n');
  console.log(`providers.json: ${doc.providers.length} platforms (${n.astro} Astro, ${n.paid} paid, ${n.local} local servers, ${n.routers} routers merged)`);
}

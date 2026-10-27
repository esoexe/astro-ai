#!/usr/bin/env node
// Refuses to let secrets out: scans every file under <dir> (text AND binary) for
//   1) the actual values of every secret we know about locally (keys files, env files, tokens), and
//   2) common API-key shapes (Groq, Gemini, OpenAI-style, NVIDIA, Hugging Face, GitHub, ...).
// Prints file names and the *name* of the matching secret only. Never prints a secret value.
//   node tools/scan_secrets.js <dir>       exit 0 = clean, 1 = something found
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const dir = process.argv[2];
if (!dir || !fs.existsSync(dir)) { console.error('usage: scan_secrets.js <dir>'); process.exit(2); }

// ---- 1. collect known secret values (never printed)
const secrets = [];                                           // { name, value }
const add = (name, v) => {
  v = String(v || '').trim().replace(/^["']|["']$/g, '');
  if (v.length < 12) return;
  if (!/[0-9]/.test(v) || !/[A-Za-z]/.test(v)) return;        // skip plain words / numbers
  if (/^https?:\/\//i.test(v) && !/[?&](key|token)=/i.test(v)) {
    // URLs: only the project-specific host part is sensitive (e.g. a Supabase project ref)
    const m = v.match(/^https?:\/\/([a-z0-9]{16,})\./i); if (m) secrets.push({ name: name + ' (host)', value: m[1] });
    return;
  }
  secrets.push({ name, value: v });
};
// settings, not secrets (model names, ports, limits)
const NOT_SECRET = /(^|_)(MODEL|MODELS|PORT|WAIT_SECONDS|MAX_TOKENS|TIMEOUT)$|^USE_/;
const envFile = (f) => {
  const p = path.join(ROOT, f); if (!fs.existsSync(p)) return;
  for (const line of fs.readFileSync(p, 'utf8').split(/\r?\n/)) {
    const m = line.match(/^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/); if (m && !NOT_SECRET.test(m[1])) add(f + ':' + m[1], m[2]);
  }
};
const tokensIn = (f, label) => {
  const p = path.join(ROOT, f); if (!fs.existsSync(p) || fs.statSync(p).isDirectory()) return;
  const t = fs.readFileSync(p, 'utf8');
  let i = 0;
  for (const m of t.matchAll(/[A-Za-z0-9_\-:.]{20,}/g)) {
    const v = m[0].replace(/^[.:]+|[.:]+$/g, '');
    if (/^[a-z.\-]+$/i.test(v)) continue;                     // domains / words
    add(`${label || f}#${++i}`, v);
  }
};
['.astro_keys.env', '.workspace.env', '.env'].forEach(envFile);
tokensIn('.astro_auth.json'); tokensIn('.astro_sessions'); tokensIn('desktop-builds/.gofile-links.txt'); tokensIn('.cache/gh/token', 'github token');
if (fs.existsSync(path.join(ROOT, 'uploads'))) for (const f of fs.readdirSync(path.join(ROOT, 'uploads'))) tokensIn(path.join('uploads', f), 'uploads/<file>');
const uniq = new Map(secrets.map((s) => [s.value, s])); const known = [...uniq.values()];

// ---- 2. generic key shapes
const shapes = [
  ['OpenAI-style sk- key', /sk-(?:proj-|or-v1-|ant-)?[A-Za-z0-9_\-]{24,}/],
  ['Groq key', /gsk_[A-Za-z0-9]{30,}/], ['Google API key', /AIza[0-9A-Za-z_\-]{35}/],
  ['NVIDIA key', /nvapi-[A-Za-z0-9_\-]{30,}/], ['Hugging Face token', /hf_[A-Za-z0-9]{30,}/],
  ['GitHub token', /gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{40,}/], ['Cerebras key', /csk-[a-z0-9]{30,}/],
  ['Slack token', /xox[baprs]-[A-Za-z0-9-]{20,}/], ['Private key block', /-----BEGIN [A-Z ]*PRIVATE KEY-----/],
  ['JWT', /eyJ[A-Za-z0-9_\-]{10,}\.eyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}/],
];

// ---- the Supabase project URL is public by design (the browser sign-in page must call it; data is protected by RLS).
// Allow the project host ONLY in these website files; it is still flagged anywhere else.
const PUBLIC_HOST_OK = new Set(['website/src/static/assets/js/account.js', 'website/src/pages/api.html', 'website/src/static/_headers', 'website/src/static/vercel.json', 'website/serve.js',
  // the Astro API gateway URL (same public host) for the built-in "astro" provider: the app needs it to use astro_sk_ keys
  'providers.json', 'tools/providers_extra.json']);

// ---- scan
const walk = (d) => fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => {
  const p = path.join(d, e.name);
  if (e.isDirectory()) return e.name === '.git' ? [] : walk(p);
  return e.isFile() ? [p] : [];                                  // skip symlinks, sockets, ...
});
let problems = 0, files = 0;
for (const f of walk(dir)) {
  files++;
  const buf = fs.readFileSync(f);
  const rel = path.relative(dir, f);
  for (const s of known) if (buf.includes(s.value) && !(s.name.endsWith(' (host)') && PUBLIC_HOST_OK.has(rel.split(path.sep).join('/')))) { problems++; console.log(`SECRET VALUE  ${rel}  <- ${s.name} (${s.value.length} chars)`); }
  const isText = !buf.subarray(0, 8000).includes(0);
  if (isText) {
    const t = buf.toString('utf8');
    for (const [label, re] of shapes) { const m = t.match(re); if (m) { problems++; console.log(`KEY SHAPE     ${rel}  <- looks like a ${label} (${m[0].length} chars)`); } }
  }
  if (/(^|\/)(\.astro_keys\.env|\.workspace\.env|\.astro_auth\.json|\.astro_sessions|seed-keys\.env|gateway\.env|\.gofile-links\.txt)$/.test(rel) || /WITH-KEYS/i.test(rel)) {
    problems++; console.log(`FORBIDDEN FILE ${rel}`);
  }
}
console.log(`scanned ${files} files against ${known.length} known secret values and ${shapes.length} key patterns: ${problems ? problems + ' PROBLEM(S)' : 'clean'}`);
process.exit(problems ? 1 : 0);

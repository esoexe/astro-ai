#!/usr/bin/env node
// Builds the native Android app's translations (android/app/src/main/assets/i18n.json) from ONE source:
// the web app dictionary (web/i18n.js: exact strings D + patterns P) plus native-only strings
// (android/i18n-native.json). The app looks strings up by their English text, like the web app does.
//
//   node tools/android_i18n.js          write the asset
//   node tools/android_i18n.js --check  fail if the asset is stale or a UI string in the Kotlin code
//                                       (T("..."), tr("...")) has no French/Arabic translation
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '..');
const OUT = path.join(root, 'android/app/src/main/assets/i18n.json');
const NATIVE = path.join(root, 'android/i18n-native.json');
const SRC = path.join(root, 'android/app/src/main/java');

function loadWeb() {
  const src = fs.readFileSync(path.join(root, 'web/i18n.js'), 'utf8');
  const a = src.indexOf('const D = {');
  const b = src.indexOf('const SKIP');
  if (a < 0 || b < 0) throw new Error('web/i18n.js layout changed: const D / const SKIP not found');
  return vm.runInNewContext(`${src.slice(a, b)}; ({ D, P })`, {});
}

function build() {
  const { D, P } = loadWeb();
  const native = JSON.parse(fs.readFileSync(NATIVE, 'utf8'));
  const d = {};
  for (const [k, v] of Object.entries(D)) if (Array.isArray(v) && v.length === 2) d[k] = v;
  for (const [k, v] of Object.entries(native)) {
    if (k.startsWith('//')) continue;
    if (!Array.isArray(v) || v.length !== 2 || !v[0] || !v[1]) throw new Error(`android/i18n-native.json: bad entry ${JSON.stringify(k)}`);
    d[k] = v;   // native wording wins where both exist
  }
  // JS regex -> Java regex: same syntax for everything used here; flags become inline (?i).
  const p = P.map(([re, fr, ar]) => [(re.flags.includes('i') ? '(?i)' : '') + re.source, fr, ar]);
  const keys = Object.keys(d).sort();
  const sorted = {};
  for (const k of keys) sorted[k] = d[k];
  return `${JSON.stringify({ d: sorted, p }, null, 0)}\n`;
}

function kotlinStrings() {
  const out = new Map();
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const f = path.join(dir, e.name);
      if (e.isDirectory()) walk(f);
      else if (e.name.endsWith('.kt') && e.name !== 'AppIcons.kt') {   // AppIcons.kt: generated SVG path data
        const text = fs.readFileSync(f, 'utf8');
        // T("...") / tr("..."), plus labels that reach T() through a variable: "key" to "Label" pairs,
        // NavItem("route", "Label"), Triple("key", "Label", ...) and confirmLabel = "Label".
        const re = new RegExp([
          String.raw`\b(?:T|tr)\(\s*"((?:[^"\\$]|\\.)*)"\s*[,)]`,
          String.raw`\bto\s+"([A-Z][^"\\$]*)"`,
          String.raw`\b(?:NavItem|Triple)\(\s*"[^"]*",\s*"([A-Z][^"\\$]*)"`,
          String.raw`\bconfirmLabel\s*=\s*"((?:[^"\\$]|\\.)*)"`,
        ].join('|'), 'g');
        let m;
        while ((m = re.exec(text))) {
          if (m[1] === undefined) {
            // label patterns: a line marked "i18n-ignore" holds internal values or native language names
            const lineStart = text.lastIndexOf('\n', m.index) + 1;
            const lineEnd = text.indexOf('\n', m.index);
            if (text.slice(lineStart, lineEnd < 0 ? undefined : lineEnd).includes('i18n-ignore')) continue;
          }
          m[1] = m[1] ?? m[2] ?? m[3] ?? m[4];
          const s = JSON.parse(`"${m[1]}"`);
          if (!/[A-Za-z]/.test(s)) continue;
          if (!out.has(s)) out.set(s, path.relative(root, f));
        }
      }
    }
  };
  if (fs.existsSync(SRC)) walk(SRC);
  return out;
}

const norm = (s) => s.replace(/\s+/g, ' ').trim();
const NUM = /\d+(?:[.,]\d+)*/g;

function main() {
  const text = build();
  if (!process.argv.includes('--check')) {
    fs.mkdirSync(path.dirname(OUT), { recursive: true });
    fs.writeFileSync(OUT, text);
    const j = JSON.parse(text);
    console.log(`i18n.json: ${Object.keys(j.d).length} strings, ${j.p.length} patterns`);
    return;
  }
  let bad = 0;
  if (!fs.existsSync(OUT) || fs.readFileSync(OUT, 'utf8') !== text) { console.error('i18n.json is stale: run node tools/android_i18n.js'); bad++; }
  const j = JSON.parse(text);
  const pats = j.p.map(([re]) => new RegExp(re.replace(/^\(\?i\)/, ''), re.startsWith('(?i)') ? 'i' : ''));
  const missing = [];
  for (const [s, file] of kotlinStrings()) {
    const k = norm(s);
    if (j.d[k] || j.d[k.replace(NUM, '#')] || pats.some((re) => re.test(s))) continue;
    missing.push(`${file}: ${JSON.stringify(s)}`);
  }
  if (missing.length) { console.error(`${missing.length} UI string(s) without a translation:\n  ${missing.join('\n  ')}`); bad++; }
  if (bad) process.exit(1);
  console.log(`i18n OK (${kotlinStrings().size} UI strings in the app, all translated)`);
}
main();

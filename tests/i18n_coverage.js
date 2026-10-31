// UI translation coverage. Opens every main screen, settings tab and menu in English and
// lists visible text (and placeholder/title/aria-label) that has no French/Arabic entry in web/i18n.js.
// Names are allowed to stay as they are: providers, models, themes, files, languages.
// Usage: start an engine (ASTRO_AUTH=off ... PORT=4370 ./workspace_server), then
//   node tests/i18n_coverage.js [http://127.0.0.1:4370]     (needs the playwright package)
// Exit 0 = every non-name string is translated; 1 = missing strings (written to i18n-missing.tsv).
const fs = require('fs');
const path = require('path');
let chromium;
try { ({ chromium } = require('playwright')); } catch { console.log('i18n coverage: SKIP (playwright not installed)'); process.exit(0); }
const BASE = process.argv[2] || process.env.ASTRO_URL || 'http://127.0.0.1:4370';
const ROOT = path.join(__dirname, '..');
const NUM = /\d+(?:[.,]\d+)*/g;
const NAMES = new Set(['A.S.T.R.O AI', 'MCP', 'Markdown', 'JS', 'JavaScript', 'TypeScript', 'Python', 'Java', 'Bash', 'HTML/CSS', 'C++', 'Go', 'Rust',
  'Ctrl Shift O', 'Local', 'my-project', 'English', 'Français', 'العربية', 'astro_sk_…']);

(async () => {
  // Names come only from name fields, never from descriptions (the provider notes must be translated).
  const names = new Set(NAMES);
  const prov = JSON.parse(fs.readFileSync(path.join(ROOT, 'providers.json'), 'utf8')).providers || [];
  for (const pr of prov) { names.add(pr.name); names.add(pr.id); for (const m of pr.models || []) names.add(typeof m === 'string' ? m : m.id || m.name); }
  for (const m of fs.readFileSync(path.join(ROOT, 'web/themes.js'), 'utf8').matchAll(/P\('[^']+', '([^']+)'/g)) names.add(m[1]);
  try {
    const st = await (await fetch(BASE + '/api/status', { headers: { 'X-Astro-Client': '1' } })).json();
    for (const m of st.models || []) { names.add(m.name); names.add(m.label); names.add(m.id); names.add(m.provider_name); }
  } catch { /* the page load below reports a dead engine */ }
  const isName = (raw) => {
    const t = raw.replace(/^@[a-z-]+: /, '').trim();
    if (names.has(t)) return true;
    if (!/\s/.test(t) && /[-_./:\d]/.test(t)) return true;            // ids, file names, URLs
    if (/^[\w.+-]+ · \d+(?:[.,]\d+)* [KMG]?B$/.test(t)) return true;    // "main.cpp · 120 B"
    const m = t.match(/^(.*) \(([^()]+)\)$/);                         // "model (Provider)"
    return !!(m && names.has(m[1]) && names.has(m[2]));
  };

  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1400, height: 900 } });
  const errs = []; p.on('pageerror', (e) => errs.push(e.message));
  await p.goto(BASE + '/', { waitUntil: 'networkidle' });
  await p.evaluate(() => window.AstroI18n.set('en')); await p.waitForTimeout(1500);
  const miss = new Map();
  const collect = async (where) => {
    const list = await p.evaluate(() => {
      const out = []; const SK = window.AstroI18n.skip;
      const vis = (el) => { if (!el) return false; const r = el.getBoundingClientRect(); const cs = getComputedStyle(el); return r.width > 0 && r.height > 0 && cs.visibility !== 'hidden' && cs.display !== 'none'; };
      const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
      for (let n = w.nextNode(); n; n = w.nextNode()) {
        const el = n.parentElement; if (!el || el.closest(SK) || !vis(el)) continue;
        const t = n.data.replace(/\s+/g, ' ').trim(); if (!/[A-Za-z]{2}/.test(t)) continue;
        if (!window.AstroI18n.has(n.data)) out.push(t);
      }
      for (const el of document.querySelectorAll('[placeholder],[title],[aria-label]')) {
        if (el.closest(SK) && !el.matches('textarea,[contenteditable]')) continue;
        for (const a of ['placeholder', 'title', 'aria-label']) { const v = el.getAttribute(a); if (v && /[A-Za-z]{2}/.test(v) && !window.AstroI18n.has(v) && vis(el)) out.push('@' + a + ': ' + v.replace(/\s+/g, ' ').trim()); }
      }
      return out;
    });
    for (const t of list) if (!isName(t) && !miss.has(t)) miss.set(t, where);
  };
  await collect('home');
  for (const id of ['nav-workspace', 'nav-agent', 'nav-notes', 'nav-tasks', 'nav-reader', 'nav-lab', 'nav-assistant']) {
    await p.click('#' + id).catch(() => {}); await p.waitForTimeout(1200); await collect(id);
  }
  for (const id of ['open-knowledge', 'open-calculator', 'brain-status']) {
    await p.click('#' + id).catch(() => {}); await p.waitForTimeout(900); await collect(id);
    await p.evaluate(() => document.getElementById('modal-close')?.click()); await p.keyboard.press('Escape'); await p.waitForTimeout(300);
  }
  for (const tab of ['general', 'appearance', 'brain', 'providers', 'account', 'voice', 'memory', 'data', 'phone', 'shortcuts', 'about']) {
    await p.evaluate((t) => window.AstroAgent.openSettings(t), tab); await p.waitForTimeout(700); await collect('settings:' + tab);
  }
  await p.evaluate(() => document.getElementById('modal-close')?.click()); await p.waitForTimeout(300);
  await p.click('#model-switch').catch(() => {}); await p.waitForTimeout(500); await collect('model-menu'); await p.keyboard.press('Escape');
  await p.keyboard.press('Control+k'); await p.waitForTimeout(600); await collect('palette'); await p.keyboard.press('Escape');

  // Arabic must switch the page to right-to-left.
  await p.evaluate(() => window.AstroI18n.set('ar')); await p.waitForTimeout(800);
  const dir = await p.evaluate(() => document.documentElement.getAttribute('dir'));
  // Live pass in French: UI drawn after the switch must be translated too (catches observer bugs).
  await p.evaluate(() => window.AstroI18n.set('fr')); await p.waitForTimeout(800);
  const stale = new Map();
  const staleCheck = async (where) => {
    const list = await p.evaluate(() => {
      const out = []; const I = window.AstroI18n;
      const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
      for (let n = w.nextNode(); n; n = w.nextNode()) {
        const el = n.parentElement; if (!el || el.closest(I.skip) || !el.getClientRects().length) continue;
        const t = n.data.replace(/\s+/g, ' ').trim(); if (!/[A-Za-z]{2}/.test(t) || !I.has(t)) continue;
        if (I.t(t) !== t) out.push(t);   // still shows English although a translation exists
      }
      return out;
    });
    for (const t of list) if (!stale.has(t)) stale.set(t, where);
  };
  for (const id of ['nav-assistant', 'nav-workspace', 'nav-agent', 'nav-notes', 'nav-tasks', 'nav-reader', 'nav-lab']) {
    await p.click('#' + id).catch(() => {}); await p.waitForTimeout(1200); await staleCheck('fr:' + id);
  }
  for (const tab of ['general', 'brain', 'providers']) {
    await p.evaluate((t) => window.AstroAgent.openSettings(t), tab); await p.waitForTimeout(700); await staleCheck('fr:settings:' + tab);
  }
  await p.evaluate(() => document.getElementById('modal-close')?.click());
  await p.evaluate(() => window.AstroI18n.set('en'));
  await b.close();

  const rows = [...miss.entries()].map(([t, w]) => `${w}\t${t}`);
  let fail = false;
  if (rows.length) { fs.writeFileSync('i18n-missing.tsv', rows.join('\n') + '\n'); console.log(rows.slice(0, 40).join('\n')); console.log(`i18n coverage: ${rows.length} untranslated (full list in i18n-missing.tsv)`); fail = true; }
  if (stale.size) { console.log([...stale].slice(0, 20).map(([t, w]) => w + '\t' + t).join('\n')); console.log(`i18n coverage: ${stale.size} strings still in English after switching to French`); fail = true; }
  if (dir !== 'rtl') { console.log('i18n coverage: Arabic did not set dir=rtl (got ' + dir + ')'); fail = true; }
  if (errs.length) { console.log('i18n coverage: page errors:', errs.slice(0, 3)); fail = true; }
  if (!fail) console.log('i18n coverage: OK (every visible non-name string has French and Arabic)');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error('i18n coverage FAILED', e); process.exit(1); });

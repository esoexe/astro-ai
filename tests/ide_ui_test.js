#!/usr/bin/env node
// End-to-end browser test of the Workspace IDE (editor, autocomplete, search/replace, versions + diff,
// review/undo of an AI change, preview + console, interactive terminal, app server + live preview, ZIP).
// Usage: node tests/ide_ui_test.js [port]
// Needs: a running test instance with ASTRO_AUTH=off, puppeteer-core and a Chrome build:
//   PUPPETEER_DIR (default ~/.cache/uitest/node_modules/puppeteer-core), CHROME (path to chrome/headless shell).
'use strict';
const fs = require('fs');
const path = require('path');
const PORT = Number(process.argv[2] || 4189);
const BASE = `http://127.0.0.1:${PORT}`;
const PUP = process.env.PUPPETEER_DIR || path.join(process.env.HOME || '/home/user', '.cache/uitest/node_modules/puppeteer-core');
const CHROME = process.env.CHROME || (() => {
  const root = path.join(process.env.HOME || '/home/user', '.cache/uitest/browsers/chrome-headless-shell');
  try { const v = fs.readdirSync(root).find((d) => d.startsWith('linux-')); return path.join(root, v, 'chrome-headless-shell-linux64', 'chrome-headless-shell'); } catch (_e) { return ''; }
})();
const SHOTS = process.env.SHOTS || '/tmp/ide-shots';
fs.mkdirSync(SHOTS, { recursive: true });
const puppeteer = require(PUP);

const H = { 'Content-Type': 'application/json', 'x-astro-client': '1' };
const api = async (p, body) => { const r = await fetch(BASE + p, { method: 'POST', headers: H, body: JSON.stringify(body) }); const j = await r.json().catch(() => ({})); if (!r.ok) throw new Error(`${p}: ${j.error || r.status}`); return j; };
let pass = 0; let fail = 0;
const ok = (cond, name, extra = '') => { if (cond) { pass++; console.log(`  ✓ ${name}`); } else { fail++; console.log(`  ✗ ${name} ${extra}`); } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const project = `ui-${Date.now() % 1000000}`;
  await api('/api/fs/project/create', { project, starter: false });
  const files = {
    'README.md': '# UI test\n\nhello **world** from the readme.\n',
    'index.html': '<!doctype html><html><head><title>t</title></head><body><h1>Hello page</h1><script src="script.js"></script></body></html>\n',
    'script.js': 'console.log("page says hello");\nconsole.error("boom: something broke");\n',
    'main.py': 'name = "hello"\nprint(name.upper())\n',
    'server.js': 'const http = require("http");\nconst port = process.env.PORT || 3000;\nhttp.createServer((req, res) => { res.setHeader("Content-Type", "text/html"); res.end("<h1>Hello from server</h1><script>console.log(\'srv page\')</script>"); }).listen(port, "0.0.0.0");\n',
    'icon.svg': '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40"><circle cx="20" cy="20" r="18" fill="#5161f0"/></svg>\n',
  };
  for (const [p, content] of Object.entries(files)) await api('/api/fs/write', { project, path: p, content });
  // Simulate an AI change: version first, then a modified README; the team card points at that version.
  const cp = await api('/api/ide/history/create', { project, label: 'Before AI: improve the readme', kind: 'ai' });
  await api('/api/fs/write', { project, path: 'README.md', content: '# UI test\n\nhello **world** from the readme, now improved by the team.\n' });
  const team = [{ role: 'user', text: 'Improve the readme' }, { role: 'team', text: 'I improved the readme.', files: [{ path: 'README.md', action: 'updated', added: 1, removed: 1 }], checkpoint: cp.id, ms: 4200, models: 3 }];

  const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ['--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 1440, height: 900 });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e.stack || e.message || e).slice(0, 400)));
  const termLog = [];
  page.on('request', (r) => { if (/term\/(input|open)/.test(r.url())) termLog.push(`${r.url().split('/api/')[1]} ${(r.postData() || '').slice(0, 60)}`); });
  page.on('console', (m) => { const u = m.location()?.url || ''; if (/\/(preview|app)\//.test(u)) return; if (m.type() === 'error' && !/favicon|Failed to load resource/.test(m.text())) errors.push(`${m.text()} @${u}`); });
  await page.evaluateOnNewDocument((proj, teamJson) => {
    if (window !== window.top) return;   // also runs in sandboxed preview frames
    localStorage.setItem('astro_ws_v1:project', proj);
    localStorage.setItem(`astro_ws_v1:team:${proj}`, teamJson);
    localStorage.setItem(`astro_ws_v1:open:${proj}`, JSON.stringify({ open: ['README.md'], active: 'README.md' }));
    localStorage.setItem('astro_ws_v1:side', 'files');
  }, project, JSON.stringify(team));
  await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
  await page.click('#nav-workspace');

  console.log('Editor');
  await page.waitForSelector('#ws-cm .cm-editor', { timeout: 30000 });
  await page.waitForFunction(() => document.querySelector('#ws-cm .cm-content')?.textContent.includes('UI test'), { timeout: 10000 });
  ok(true, 'CodeMirror editor loaded with README.md');
  ok(await page.$eval('#ws-status', (e) => !e.hidden && /Ln 1, Col 1/.test(e.textContent)), 'status bar shows cursor position');
  ok(await page.$('.cm-foldGutter') !== null, 'code folding gutter present');
  await page.click('.wt-row[data-file="main.py"]');
  await page.waitForFunction(() => document.querySelector('#ws-cm .cm-content')?.textContent.includes('print(name.upper())'));
  await page.click('#ws-cm .cm-content');
  await page.keyboard.down('Control'); await page.keyboard.press('End'); await page.keyboard.up('Control');
  await page.keyboard.type('pri');
  const ac = await page.waitForSelector('.cm-tooltip-autocomplete', { timeout: 5000 }).then(() => true).catch(() => false);
  ok(ac, 'autocomplete pops up while typing');
  await page.keyboard.press('Escape');
  await page.keyboard.type('nt("typed in the editor")');
  await page.keyboard.down('Control'); await page.keyboard.press('s'); await page.keyboard.up('Control');
  await sleep(800);
  const saved = await api('/api/fs/read', { project, path: 'main.py' });
  ok(saved.content.includes('print("typed in the editor")') || saved.content.includes('typed in the editor'), 'Ctrl+S saves the edit to the server', JSON.stringify(saved.content.slice(-60)));
  await page.keyboard.down('Control'); await page.keyboard.press('f'); await page.keyboard.up('Control');
  ok(await page.waitForSelector('#ws-cm .cm-search', { timeout: 3000 }).then(() => true).catch(() => false), 'Ctrl+F opens find & replace in the file');
  await page.keyboard.press('Escape');
  await page.screenshot({ path: `${SHOTS}/1-editor.png` });

  console.log('Search across files');
  await page.click('[data-side="search"]');
  await page.type('#ws-sq', 'hello');
  await page.waitForSelector('.sres-hit', { timeout: 8000 });
  const sum = await page.$eval('.sres-sum', (e) => e.textContent);
  ok(/results? in [3-9] files/.test(sum), 'search finds hits in several files', sum);
  await page.type('#ws-sr', 'howdy');
  await sleep(200);
  ok(await page.$('.sres-hit ins') !== null, 'replace preview shown inline');
  await page.screenshot({ path: `${SHOTS}/2-search.png` });
  await page.click('.sres-hit');
  await sleep(500);
  ok(await page.$eval('.ws-tab.on', (e) => e.dataset.tab).then((t) => Boolean(t)), 'clicking a hit opens the file');
  await page.click('#ws-sr-all');
  await page.waitForSelector('.ws-dialog button[type=submit]');
  await page.click('.ws-dialog button[type=submit]');
  await page.waitForFunction(() => /No results/.test(document.querySelector('#ws-sres')?.textContent || ''), { timeout: 10000 }).catch(() => {});
  const afterRepl = await api('/api/fs/read', { project, path: 'script.js' });
  ok(afterRepl.content.includes('page says howdy'), 'Replace all rewrote the files');

  console.log('History, diff and restore');
  await page.click('[data-side="history"]');
  await page.waitForSelector('.ver', { timeout: 8000 });
  const labels = await page.$$eval('.ver-main b', (els) => els.map((e) => e.textContent));
  ok(labels.some((l) => /Before replacing/.test(l)) && labels.some((l) => /Before AI/.test(l)), 'versions list has the automatic versions', JSON.stringify(labels));
  await page.click('.ver [data-vact="changes"]');
  await page.waitForSelector('.ver-file', { timeout: 8000 });
  await page.click('.ver-file');
  await page.waitForSelector('#ws-diff:not([hidden]) #ws-diff-host .cm-editor', { timeout: 8000 });
  ok(true, 'diff view opens from a version');
  await page.screenshot({ path: `${SHOTS}/3-history-diff.png` });
  await page.click('[data-dmode="split"]');
  ok(await page.waitForSelector('#ws-diff-host .cm-mergeView', { timeout: 4000 }).then(() => true).catch(() => false), 'side-by-side diff works');
  await page.click('[data-dact="close"]');

  console.log('Team change: review and undo');
  await page.waitForSelector('[data-tundo]', { timeout: 5000 });
  ok(await page.$eval('.tm-stat', (e) => e.textContent.includes('+1')), 'team card shows +/- stats');
  await page.click('.tm-acts [data-tdiff]');
  await page.waitForSelector('#ws-diff:not([hidden]) #ws-diff-host .cm-editor', { timeout: 8000 });
  const dtitle = await page.$eval('#ws-diff-title', (e) => e.textContent);
  ok(/README\.md/.test(dtitle) && /before this AI change/.test(dtitle), 'Review changes shows the AI diff', dtitle);
  await page.screenshot({ path: `${SHOTS}/4-team-diff.png` });
  await page.click('[data-tundo]');
  await page.waitForFunction(() => /Redo/.test(document.querySelector('[data-tundo]')?.textContent || ''), { timeout: 8000 });
  const undone = await api('/api/fs/read', { project, path: 'README.md' });
  ok(!undone.content.includes('improved by the team'), 'Undo restored the files from before the AI change');
  await page.click('[data-tundo]');
  await page.waitForFunction(() => /Undo/.test(document.querySelector('[data-tundo]')?.textContent || ''), { timeout: 8000 });
  const redone = await api('/api/fs/read', { project, path: 'README.md' });
  ok(redone.content.includes('improved by the team'), 'Redo brought the AI change back');
  await page.click('[data-dact="close"]').catch(() => {});

  console.log('Preview and console');
  await page.click('[data-side="files"]');
  await page.click('.wt-row[data-file="index.html"]');
  await page.click('#ws-run');
  await page.waitForFunction(() => /error/.test(document.querySelector('#ws-con')?.textContent || ''), { timeout: 10000 }).catch(() => {});
  const con = await page.$eval('#ws-con', (e) => e.textContent);
  ok(/1 error/.test(con) && /Ask team to fix/.test(con), 'console strip shows the page error with a fix button', con.slice(0, 120));
  ok(/boom: something broke/.test(con), 'console strip lists the error text');
  await page.screenshot({ path: `${SHOTS}/5-preview-console.png` });
  await page.click('.wt-row[data-file="icon.svg"]');
  ok(await page.$eval('#ws-status', (e) => /Preview/.test(e.textContent)), 'SVG files get a Preview button');

  console.log('Terminal');
  await page.click('[data-panel="terminal"]');
  await page.waitForSelector('#ws-xterm .xterm', { timeout: 8000 });
  await sleep(1200);
  const under = await page.evaluate(() => { const r = document.querySelector('#ws-xterm').getBoundingClientRect(); const e = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return `${e?.tagName}.${e?.className} "${e?.textContent.slice(0, 80)}" pe=${e && getComputedStyle(e).pointerEvents} rect=${Math.round(r.width)}x${Math.round(r.height)}@${Math.round(r.top)}`; });
  console.log('    (under terminal center:', under, ')');
  await page.waitForFunction(() => !document.querySelector('.toast'), { timeout: 10000 }).catch(() => {});
  await page.click('#ws-xterm');
  await page.keyboard.type('echo astro-$((6*7)) && pwd\n');
  const termOk = await page.waitForFunction(() => /astro-42/.test(document.querySelector('#ws-xterm .xterm-rows')?.textContent || ''), { timeout: 8000 }).then(() => true).catch(() => false);
  ok(termOk, 'interactive terminal runs commands', `${termLog.join(' | ')} || active=${await page.evaluate(() => document.activeElement?.className)} rows=${await page.$eval('#ws-xterm .xterm-rows', (e) => e.textContent.slice(0, 160))}`);
  await page.keyboard.type('python3 -c "n=input(\'name? \');print(\'hi\',n)"\n');
  await sleep(900); await page.keyboard.type('Astro\n');
  const interOk = await page.waitForFunction(() => /hi Astro/.test(document.querySelector('#ws-xterm .xterm-rows')?.textContent || ''), { timeout: 8000 }).then(() => true).catch(() => false);
  ok(interOk, 'terminal programs can read typed input');
  await page.screenshot({ path: `${SHOTS}/6-terminal.png` });

  console.log('App server');
  await page.click('.wt-row[data-file="server.js"]');
  await page.click('#ws-run');
  const up = await page.waitForSelector('#ws-srv-badge.up', { timeout: 30000 }).then(() => true).catch(() => false);
  ok(up, 'Run detects a server and starts it');
  await sleep(1500);
  const src = await page.$eval('#ws-frame', (f) => f.getAttribute('src') || '');
  ok(/\/app\//.test(src), 'Preview shows the app through /app/', src);
  const frame = page.frames().find((f) => /\/app\//.test(f.url()));
  const body = frame ? await frame.evaluate(() => document.body.innerText).catch(() => '') : '';
  ok(/(hello|howdy) from server/i.test(body), 'app page renders in Preview', body.slice(0, 80));
  await page.screenshot({ path: `${SHOTS}/7-server.png` });
  await page.click('#ws-srv-badge');
  await page.waitForSelector('[data-srv="stop"]');
  await page.click('[data-srv="stop"]');
  await sleep(800);
  const st = await api('/api/ide/proc/status', { project });
  ok(!st.running, 'Stop ends the server');

  console.log('ZIP and mobile layout');
  const zip = await fetch(`${BASE}/api/ide/zip`, { method: 'POST', headers: H, body: JSON.stringify({ project }) });
  const buf = Buffer.from(await zip.arrayBuffer());
  ok(zip.ok && buf.slice(0, 2).toString() === 'PK', 'ZIP download is a valid archive');
  await page.setViewport({ width: 390, height: 844 });
  await sleep(500);
  await page.screenshot({ path: `${SHOTS}/8-mobile.png` });
  ok(await page.$eval('.ws-mtabs', (e) => getComputedStyle(e).display !== 'none'), 'mobile section tabs visible');

  ok(errors.length === 0, 'no JavaScript errors in the app', errors.slice(0, 5).join(' | '));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed · project ${project} · screenshots in ${SHOTS}`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error('FATAL', e); process.exit(2); });

#!/usr/bin/env node
// Browser test of the Workspace intelligence layer: Context (memory, repo index, outline), knowledge map,
// Git (init, diff vs HEAD, commit, log), profiler stats + assembly view, clickable file:line errors,
// three.js loading inside the sandboxed preview, Plan mode chip, plan card + memory chip rendering.
// Usage: node tests/intel_ui_test.js [port]   (test instance with ASTRO_AUTH=off; same deps as ide_ui_test.js)
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
  const project = `intel-${Date.now() % 1000000}`;
  await api('/api/fs/project/create', { project, starter: false });
  const files = {
    'main.c': '#include <stdio.h>\n#include "util.h"\n\nint square(int x) { return x * x; }\n\nint main(void) {\n  long s = 0;\n  for (int i = 0; i < 2000000; i++) s += square(i % 100);\n  printf("sum=%ld\\n", s);\n  return 0;\n}\n',
    'util.h': '#pragma once\nstruct Vec3 { float x, y, z; };\n',
    'app.js': 'import * as THREE from "three";\nimport { helper } from "./lib/helper.js";\nexport function boot() { return helper(); }\nexport const tick = (dt) => dt * 2;\n',
    'lib/helper.js': 'export function helper() { return 42; }\n',
    'crash.py': 'def explode():\n    raise ValueError("kaboom")\n\nexplode()\n',
    'index.html': '<!doctype html><html><head><title>HUD</title><script type="importmap">{"imports":{"three":"/vendor/three.module.min.js","three/addons/controls/OrbitControls.js":"/vendor/OrbitControls.js"}}</script></head><body><h1>HUD</h1>'
      + '<script type="module">import * as THREE from "three"; import { OrbitControls } from "three/addons/controls/OrbitControls.js"; const v = new THREE.Vector3(1, 2, 2); document.body.insertAdjacentHTML("beforeend", `<p id="r">three r${THREE.REVISION} len=${v.length()} orbit=${typeof OrbitControls}</p>`);</script>'
      + '<script type="module" src="app.js"></script><link rel="stylesheet" href="style.css"></body></html>\n',
    'style.css': ':root { --neon: #0ff; }\nbody { background: #05070d; color: var(--neon); }\n',
  };
  for (const [p, content] of Object.entries(files)) await api('/api/fs/write', { project, path: p, content });
  const team = [
    { role: 'user', text: 'Build a HUD' },
    { role: 'plan', text: 'Plan for: Build a HUD', plan: 'Build a HUD', steps: [{ title: 'Scaffold the scene', task: 'index.html + app.js', status: 'done' }, { title: 'Add the HUD panels', task: 'glass panels', status: 'failed' }, { title: 'Polish', task: 'animations', status: 'skipped' }] },
    { role: 'team', text: 'Scaffolded the scene.', files: [], memory: ['UI style: dark glassmorphism with neon cyan accents'], ms: 3000, models: 1 },
  ];

  const browser = await puppeteer.launch({ executablePath: CHROME, headless: true, args: ['--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 1440, height: 900 });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e.stack || e.message || e).slice(0, 400)));
  page.on('console', (m) => { const u = m.location()?.url || ''; if (/\/(preview|app)\//.test(u)) return; if (m.type() === 'error' && !/favicon|Failed to load resource/.test(m.text())) errors.push(`${m.text()} @${u}`); });
  await page.evaluateOnNewDocument((proj, teamJson) => {
    if (window !== window.top) return;
    localStorage.setItem('astro_ws_v1:project', proj);
    localStorage.setItem(`astro_ws_v1:team:${proj}`, teamJson);
    localStorage.setItem(`astro_ws_v1:open:${proj}`, JSON.stringify({ open: ['main.c'], active: 'main.c' }));
    localStorage.setItem('astro_ws_v1:side', 'files');
    localStorage.setItem('astro_ws_v1:plan', '0');
  }, project, JSON.stringify(team));
  await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
  await page.click('#nav-workspace');
  await page.waitForSelector('#ws-cm .cm-editor', { timeout: 30000 });
  await page.waitForFunction(() => document.querySelector('#ws-cm .cm-content')?.textContent.includes('square'), { timeout: 10000 });
  const clickRow = async (file) => { await page.waitForSelector(`.wt-row[data-file="${file}"]`); await page.evaluate((f) => document.querySelector(`.wt-row[data-file="${f}"]`).click(), file); };
  const clickText = (sel, text) => page.evaluate((s, t) => { const b = [...document.querySelectorAll(s)].find((x) => x.textContent.includes(t) && x.offsetParent !== null); if (b) b.click(); return Boolean(b); }, sel, text);

  console.log('Team: plan card, memory chip, Plan mode');
  ok(await page.$$eval('.tm-plan .plan-steps li', (l) => l.length) === 3, 'plan card shows 3 steps');
  ok(await page.$eval('.tm-plan', (e) => e.querySelector('.ps-done') && e.querySelector('.ps-failed') && /1\/3 done/.test(e.textContent)), 'plan step states (done / failed) and progress');
  ok(await page.$eval('.tm-mem', (e) => /glassmorphism/.test(e.textContent)).catch(() => false), 'memory chip shows what the team remembered');
  await page.click('#ws-plan');
  ok(await page.$eval('#ws-plan', (e) => e.classList.contains('on')) && await page.$eval('#ws-team-in', (e) => /goal/.test(e.placeholder)), 'Plan chip toggles plan mode');
  await page.click('#ws-plan');
  await page.waitForFunction(() => !document.querySelector('.toast'), { timeout: 15000 }).catch(() => {});

  console.log('Context: memory, index, outline');
  await page.click('[data-side="context"]');
  await page.waitForSelector('#ws-mem', { timeout: 10000 });
  await page.waitForFunction(() => document.querySelector('.ctx-stats'), { timeout: 10000 });
  ok(await page.$eval('.ctx-stats', (e) => /7\s*files/.test(e.textContent.replace(/\s+/g, ' '))), 'repository index stats shown', await page.$eval('.ctx-stats', (e) => e.textContent));
  ok(await page.$$eval('.ctx-outline .ctx-sym', (l) => l.map((x) => x.textContent).join('|')).then((t) => /square/.test(t) && /main/.test(t)), 'outline lists functions of the open file');
  await page.evaluate(() => [...document.querySelectorAll('.ctx-outline .ctx-sym')].find((x) => /main/.test(x.textContent)).click());
  await page.waitForFunction(() => /Ln 6\b/.test(document.querySelector('#ws-status')?.textContent || ''), { timeout: 5000 }).then(() => ok(true, 'outline click jumps to the line')).catch(async () => ok(false, 'outline click jumps to the line', await page.$eval('#ws-status', (e) => e.textContent)));
  await page.click('#ws-mem');
  await page.keyboard.type('- Use C11 and keep hot loops allocation-free');
  await sleep(1500);
  const mem = await api('/api/fs/read', { project, path: '.workspace/memory.md' }).catch(() => ({}));
  ok(/allocation-free/.test(mem.content || ''), 'memory is saved to .workspace/memory.md', JSON.stringify(mem).slice(0, 120));
  ok(await page.$eval('#ws-mem-state', (e) => /Saved/.test(e.textContent)), 'memory shows Saved');

  console.log('Knowledge map');
  await page.click('[data-ctx="map"]');
  await page.waitForSelector('#ws-map:not([hidden]) svg .mn', { timeout: 10000 });
  const nodes = await page.$$eval('#ws-map .mn', (l) => l.map((g) => g.dataset.mnode));
  const edges = await page.$$eval('#ws-map .me', (l) => l.length);
  ok(nodes.length >= 6 && nodes.includes('lib/helper.js'), 'map draws a node per code file', nodes.join(','));
  ok(edges >= 4, 'map draws dependency links', String(edges));
  await page.click('#ws-map .mn[data-mnode="app.js"]');
  await page.waitForFunction(() => /Uses/.test(document.querySelector('#ws-map-info')?.textContent || ''));
  const info = await page.$eval('#ws-map-info', (e) => e.textContent);
  ok(/lib\/helper\.js/.test(info) && /Used by/.test(info) && /three/.test(info) && /boot/.test(info), 'node details: uses, used by, packages, symbols', info.slice(0, 200));
  await page.screenshot({ path: path.join(SHOTS, 'intel-map.png') });
  await page.evaluate(() => [...document.querySelectorAll('#ws-map-info .ctx-sym')].find((x) => /tick/.test(x.textContent)).click());
  await page.waitForFunction(() => document.querySelector('#ws-map').hidden && document.querySelector('.ws-tab.on, [data-tab].on')?.dataset.tab === 'app.js', { timeout: 5000 }).then(() => ok(true, 'symbol click closes the map and opens the file')).catch(() => ok(false, 'symbol click closes the map and opens the file'));

  console.log('Git');
  await page.click('[data-side="git"]');
  await page.waitForSelector('[data-git="init"]', { timeout: 10000 });
  await page.click('[data-git="init"]');
  await page.waitForSelector('.git-file', { timeout: 10000 });
  const changes = await page.$$eval('.git-file', (l) => l.length);
  ok(changes >= 8, 'git init then changes list shows untracked files', String(changes));
  await page.click('.git-file[data-gfile="main.c"]');
  await page.waitForFunction(() => !document.querySelector('#ws-diff').hidden && /new since/.test(document.querySelector('#ws-diff-title')?.textContent || ''), { timeout: 8000 }).then(() => ok(true, 'change opens diff against last commit')).catch(async () => ok(false, 'change opens diff against last commit', await page.$eval('#ws-diff-title', (e) => e.textContent)));
  await page.click('[data-dact="close"]');
  await page.click('#ws-git-msg');
  await page.keyboard.type('feat(core): initial HUD scaffold');
  await page.click('[data-git="commit"]');
  await page.waitForFunction(() => /initial HUD scaffold/.test(document.querySelector('.git-log')?.textContent || ''), { timeout: 10000 }).then(() => ok(true, 'commit appears in the log')).catch(() => ok(false, 'commit appears in the log'));
  ok(await page.$eval('.git-files', (e) => /clean/.test(e.textContent)), 'working tree clean after commit');
  await api('/api/fs/write', { project, path: 'lib/helper.js', content: 'export function helper() { return 43; }\n' });
  await page.click('[data-git="refresh"]');
  await page.waitForSelector('.git-file[data-gfile="lib/helper.js"]', { timeout: 8000 });
  ok(await page.$eval('.git-file[data-gfile="lib/helper.js"] .git-st', (e) => e.textContent === 'M'), 'modified file marked M');
  await page.click('.git-file[data-gfile="lib/helper.js"]');
  await page.waitForFunction(() => /\+1/.test(document.querySelector('#ws-diff-title')?.textContent || ''), { timeout: 8000 }).then(() => ok(true, 'diff vs HEAD shows +1 −1')).catch(async () => ok(false, 'diff vs HEAD shows +1 −1', await page.$eval('#ws-diff-title', (e) => e.textContent)));
  await page.screenshot({ path: path.join(SHOTS, 'intel-git.png') });
  await page.click('[data-dact="close"]');
  await page.waitForFunction(() => !document.querySelector('.toast'), { timeout: 15000 }).catch(() => {});

  console.log('Profiling, assembly, error links');
  await page.click('[data-side="files"]');
  await clickRow('main.c');
  await page.waitForFunction(() => document.querySelector('#ws-cm .cm-content')?.textContent.includes('square'));
  await page.click('#ws-run');
  await page.waitForSelector('#ws-out .o-exit', { timeout: 60000 });
  const exitTxt = await page.$eval('#ws-out', (e) => e.textContent);
  ok(/sum=6567000000/.test(exitTxt), 'C program ran', exitTxt.slice(-200));
  ok(await page.$('#ws-out .o-stats') !== null && /CPU \d+ ms · peak RAM/.test(await page.$eval('#ws-out .o-stats', (e) => e.textContent)), 'run shows CPU time and peak RAM');
  ok(await page.$('#ws-out [data-optimize]') !== null, 'Optimize button offered after a profiled run');
  ok(!/astro_measure/.test(exitTxt), 'profiler wrapper hidden in output');
  await page.click('#ws-run-more');
  ok(await clickText('#menu button', 'Show assembly'), 'Run menu has Show assembly');
  await page.waitForFunction(() => /main:/.test(document.querySelector('#ws-out')?.textContent || '') && document.querySelector('#ws-out .o-exit'), { timeout: 30000 }).then(() => ok(true, 'assembly listing appears')).catch(async () => ok(false, 'assembly listing appears', (await page.$eval('#ws-out', (e) => e.textContent)).slice(0, 300)));
  await clickRow('crash.py');
  await page.waitForFunction(() => document.querySelector('#ws-cm .cm-content')?.textContent.includes('kaboom'));
  await page.click('#ws-run');
  await page.waitForSelector('#ws-out .o-exit.bad', { timeout: 30000 });
  const links = await page.$$eval('#ws-out .o-loc', (l) => l.map((a) => a.dataset.goto));
  ok(links.some((g) => /crash\.py",2/.test(g)), 'traceback lines become links', JSON.stringify(links));
  await page.evaluate(() => [...document.querySelectorAll('#ws-out .o-loc')].find((a) => /crash\.py",2/.test(a.dataset.goto)).click());
  await page.waitForFunction(() => /Ln 2\b/.test(document.querySelector('#ws-status')?.textContent || ''), { timeout: 5000 }).then(() => ok(true, 'clicking an error link jumps to the line')).catch(() => ok(false, 'clicking an error link jumps to the line'));

  console.log('three.js in the sandboxed preview');
  await clickRow('index.html');
  await page.click('#ws-run');
  let threeOk = '';
  for (let i = 0; i < 40 && !threeOk; i++) {
    await sleep(250);
    for (const f of page.frames()) { try { const t = await f.evaluate(() => document.querySelector('#r')?.textContent || ''); if (t) threeOk = t; } catch (_e) { /* cross-origin frame not ready */ } }
  }
  ok(/three r186 len=3 orbit=function/.test(threeOk), 'three.js + OrbitControls load via the import map in the preview', threeOk);
  await sleep(800);
  const con = await page.$eval('#ws-con', (e) => e.textContent).catch(() => '');
  ok(!/Failed to load/.test(con), 'the project\'s own ES modules load in the sandboxed preview (CORS)', con.slice(0, 200));
  await page.screenshot({ path: path.join(SHOTS, 'intel-preview.png') });

  console.log('Dark theme screenshot');
  await page.evaluate(() => { document.documentElement.dataset.theme = 'dark'; });
  await page.click('[data-side="context"]');
  await sleep(800);   // the context pane re-indexes when it opens
  await page.waitForSelector('[data-ctx="map"]');
  await page.evaluate(() => document.querySelector('[data-ctx="map"]').click());
  await page.waitForSelector('#ws-map:not([hidden]) svg .mn');
  await sleep(700);   // the map re-renders once the fresh index arrives
  await page.evaluate(() => document.querySelector('#ws-map .mn[data-mnode="main.c"]').dispatchEvent(new MouseEvent('click', { bubbles: true })));
  await sleep(300);
  await page.screenshot({ path: path.join(SHOTS, 'intel-map-dark.png') });
  await page.keyboard.press('Escape');

  ok(errors.length === 0, 'no JavaScript errors', errors.join(' | ').slice(0, 600));
  await browser.close();
  await api('/api/fs/project/delete', { project }).catch(() => {});
  console.log(`\n${pass} passed, ${fail} failed · project ${project} · screenshots in ${SHOTS}`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error('FATAL', e); process.exit(2); });

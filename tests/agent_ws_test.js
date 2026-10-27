// Workspace Agent mode: run a job from the Workspace team panel, check the files and Undo all.
// Usage: BASE=http://127.0.0.1:4189 node tests/agent_ws_test.js
const path = require('path');
const fs = require('fs');
const root = process.env.UITEST || path.join(__dirname, '..', '.cache', 'uitest');
const puppeteer = require(path.join(root, 'node_modules', 'puppeteer-core'));
const BASE = process.env.BASE || 'http://127.0.0.1:4189';
const SHOTS = process.env.SHOTS || path.join(__dirname, '..', '.cache', 'shots');
fs.mkdirSync(SHOTS, { recursive: true });
const findChrome = () => { const b = path.join(root, 'browsers', 'chrome-headless-shell'); for (const d of fs.readdirSync(b)) { const p = path.join(b, d, 'chrome-headless-shell-linux64', 'chrome-headless-shell'); if (fs.existsSync(p)) return p; } throw new Error('no chrome'); };
let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log('  ok  ' + m); } else { fail++; console.log('  FAIL ' + m); } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const api = (p, body) => fetch(BASE + p, { method: 'POST', headers: { 'content-type': 'application/json', 'x-astro-client': '1' }, body: JSON.stringify(body) }).then((r) => r.json());
(async () => {
  await api('/api/fs/project/delete', { project: 'agt' }).catch(() => {});
  await api('/api/fs/project/create', { project: 'agt', starter: false });
  await api('/api/fs/write', { project: 'agt', path: 'keep.txt', content: 'original\n' });
  const browser = await puppeteer.launch({ executablePath: findChrome(), args: ['--no-sandbox', '--disable-gpu'], headless: true });
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.setViewport({ width: 1500, height: 900 });
  await page.evaluateOnNewDocument(() => { if (window !== window.top) return; localStorage.setItem('astro_agent:team', '0'); localStorage.setItem('astro_ws_v1:project', 'agt'); localStorage.setItem('astro_ws_v1:agent', '1'); });
  await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
  await page.evaluate(() => document.querySelector('#nav-workspace').click());
  await page.waitForSelector('#ws-team.agent-mode', { timeout: 20000 });
  ok(true, 'workspace opens in Agent mode (remembered)');
  await sleep(1200);
  await page.type('#ws-team-in', 'Change keep.txt so it says "changed by agent", create notes/todo.md with a 3-item checklist, then show both files with cat.');
  await page.evaluate(() => document.querySelector('#ws-team-send').click());
  const t0 = Date.now();
  await sleep(2000);
  ok(await page.$('#ws-team-send.stop'), 'send button turns into stop while working');
  for (;;) { await sleep(2000); if (!(await page.$('#ws-team-send.stop'))) break; if (Date.now() - t0 > 400000) break; }
  console.log('     took ' + Math.round((Date.now() - t0) / 1000) + 's');
  const tl = await page.$eval('#ws-team-msgs', (e) => e.innerText);
  ok(await page.$('#ws-team-msgs .ag-done'), 'done card in the workspace panel');
  ok((await page.$$('#ws-team-msgs .ag-tool')).length >= 2, 'tool steps shown in the workspace panel');
  const tree = await page.$eval('#ws-view', (e) => e.innerText);
  ok(/todo\.md/.test(tree), 'new file appears in the workspace file tree');
  const k = await api('/api/fs/read', { project: 'agt', path: 'keep.txt' });
  ok(/changed by agent/.test(k.content || ''), 'keep.txt changed on disk');
  await page.screenshot({ path: path.join(SHOTS, 'ws-agent-done.png') });
  // Undo all
  await page.evaluate(() => document.querySelector('#ws-team-msgs [data-agundo]')?.click());
  await page.waitForSelector('.ws-dialog', { timeout: 5000 });
  await page.evaluate(() => document.querySelector('.ws-dialog button[type=submit], .ws-dialog .primary')?.click());
  await sleep(2500);
  const k2 = await api('/api/fs/read', { project: 'agt', path: 'keep.txt' });
  const t2 = await api('/api/fs/tree', { project: 'agt' });
  ok(/original/.test(k2.content || ''), 'Undo all restored keep.txt');
  ok(!(t2.files || []).some((f) => f.path === 'notes/todo.md'), 'Undo all removed the created file');
  ok(/undone/.test(await page.$eval('#ws-team-msgs', (e) => e.innerText)), 'undo noted in the timeline');
  // full view hand-off
  await page.evaluate(() => document.querySelector('#ws-agfull').click());
  await sleep(1800);
  ok(await page.$('#agent-view:not([hidden]) #ag-tl .ag-done'), 'Expand opens the same session in the Agent page');
  await page.screenshot({ path: path.join(SHOTS, 'ws-agent-full.png') });
  ok(errors.length === 0, 'no page errors ' + errors.slice(0, 3).join(' | '));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

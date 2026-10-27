// End-to-end UI test for Agent mode (Agent page + Workspace Agent mode).
// Usage: BASE=http://127.0.0.1:4189 TEAM=0 node tests/agent_ui_test.js
// Needs puppeteer-core and chrome-headless-shell (see README "Tests").
const path = require('path');
const fs = require('fs');
const root = process.env.UITEST || path.join(__dirname, '..', '.cache', 'uitest');
const puppeteer = require(path.join(root, 'node_modules', 'puppeteer-core'));
const BASE = process.env.BASE || 'http://127.0.0.1:4189';
const TEAM = process.env.TEAM === '1';
const SHOTS = process.env.SHOTS || path.join(__dirname, '..', '.cache', 'shots');
const JOB = process.env.JOB || 'Create sum.py that prints the sum of the numbers 1 to 100, run it, and tell me the result.';
const EXPECT = new RegExp(process.env.EXPECT || '5050');
const EXPECT_FILE = process.env.EXPECT_FILE || 'sum.py';
const REVIEW_WAIT = Number(process.env.REVIEW_WAIT || 180) * 1000;
fs.mkdirSync(SHOTS, { recursive: true });

function findChrome() {
  const base = path.join(root, 'browsers', 'chrome-headless-shell');
  for (const d of fs.readdirSync(base)) {
    const p = path.join(base, d, 'chrome-headless-shell-linux64', 'chrome-headless-shell');
    if (fs.existsSync(p)) return p;
  }
  throw new Error('chrome-headless-shell not found');
}
let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log('  ok  ' + m); } else { fail++; console.log('  FAIL ' + m); } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const api = (p, body) => fetch(BASE + p, { method: 'POST', headers: { 'content-type': 'application/json', 'x-astro-client': '1' }, body: JSON.stringify(body) }).then((r) => r.json());

(async () => {
  await api('/api/fs/project/delete', { project: 'agt' }).catch(() => {});
  const cr = await api('/api/fs/project/create', { project: 'agt', starter: false });
  ok(!cr.error, 'test project created');
  const browser = await puppeteer.launch({ executablePath: findChrome(), args: ['--no-sandbox', '--disable-gpu'], headless: true });
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error' && !/\/preview\/|\/app\/|favicon|Failed to load resource/.test(m.text() + (m.location()?.url || ''))) errors.push('console: ' + m.text()); });
  await page.setViewport({ width: 1500, height: 900 });
  await page.evaluateOnNewDocument((team) => {
    if (window !== window.top) return;
    localStorage.setItem('astro_agent:team', team ? '1' : '0');
    localStorage.setItem('astro_ws_v1:project', 'agt');
  }, TEAM);
  await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
  ok(await page.$('#nav-agent'), 'Agent nav button exists');
  await page.evaluate(() => document.querySelector('#nav-agent').click());
  await page.waitForSelector('#agent-view:not([hidden]) .ag', { timeout: 15000 });
  ok(true, 'Agent page mounted');
  await sleep(1500);
  // pick the test project
  const proj = await page.$eval('#ag-proj-name', (e) => e.textContent.trim());
  console.log('     project:', proj);
  if (proj !== 'agt') {
    await page.evaluate(() => document.querySelector('#ag-proj').click());
    await sleep(500);
    await page.evaluate(() => { const b = [...document.querySelectorAll('.menu button, .menu-item, [role=menuitem]')].find((x) => x.textContent.trim().startsWith('agt')); b && b.click(); });
    await sleep(1200);
  }
  ok((await page.$eval('#ag-proj-name', (e) => e.textContent.trim())) === 'agt', 'project agt selected');
  ok(await page.$('#ag-tl .ag-empty'), 'empty state shown');
  await page.screenshot({ path: path.join(SHOTS, 'ag-empty.png') });

  // run a job
  await page.type('#ag-in', JOB);
  await page.evaluate(() => document.querySelector('#ag-send').click());
  await sleep(1500);
  ok(await page.$('#ag-tl .ag-user'), 'user message in timeline');
  ok(await page.$('.ag-srow'), 'session listed');
  const t0 = Date.now();
  let shotMid = false;
  for (;;) {
    await sleep(2000);
    const st = await page.evaluate(() => ({ reviewing: /reviewing/i.test(document.querySelector('#ag-status')?.textContent || ''), planNote: !!document.querySelector('#ag-tl .ag-it.ag-note') && /team plan arrived/i.test(document.querySelector('#ag-tl').innerText), status: document.querySelector('#ag-status')?.className || '', text: document.querySelector('#ag-status')?.textContent || '', tools: document.querySelectorAll('#ag-tl .ag-tool').length, done: !!document.querySelector('#ag-tl .ag-done') }));
    if (st.reviewing) {
      if (!globalThis.revAt) { globalThis.revAt = Date.now(); console.log('     team review started at ' + Math.round((Date.now() - t0) / 1000) + 's'); await page.screenshot({ path: path.join(SHOTS, 'ag-review.png') }); }
      else if (Date.now() - globalThis.revAt > REVIEW_WAIT && !globalThis.skipped) { globalThis.skipped = true; console.log('     skipping the review (pressing stop)'); await page.evaluate(() => document.querySelector('#ag-send').click()); }
    }
    if (!shotMid && st.tools >= 1) { shotMid = true; await page.screenshot({ path: path.join(SHOTS, 'ag-running.png') }); }
    if (!/st-running|st-waiting/.test(st.status) && Date.now() - t0 > 4000) { console.log('     final status:', st.text, '| tools:', st.tools, '| ' + Math.round((Date.now() - t0) / 1000) + 's'); break; }
    if (Date.now() - t0 > (TEAM ? 1500 : 420) * 1000) { console.log('     timeout, status:', st.text); break; }
  }
  const res = await page.evaluate(() => ({
    tools: [...document.querySelectorAll('#ag-tl .ag-tool .ag-th b')].map((b) => b.textContent),
    done: document.querySelector('#ag-tl .ag-done')?.innerText || '',
    errs: [...document.querySelectorAll('#ag-tl .ag-note.bad')].map((n) => n.innerText),
    ccount: document.querySelector('#ag-ccount')?.textContent || '',
  }));
  console.log('     tools:', res.tools.join(' | '));
  console.log('     errors:', res.errs.join(' | ') || 'none');
  console.log('     done:', res.done.slice(0, 400).replace(/\n+/g, ' / '));
  ok(res.tools.length >= 2, 'agent used tools');
  ok(res.done.length > 0, 'done summary present');
  ok(EXPECT.test(res.done + (await page.$eval('#ag-tl', (e) => e.innerText))), 'expected result reported (' + EXPECT + ')');
  console.log('     plan:', await page.evaluate(() => [...document.querySelectorAll('#ag-tl .ag-card')].map((c) => c.innerText.slice(0, 160).replace(/\n+/g, ' / ')).join(' || ') || 'none'));
  const tree = await api('/api/fs/tree', { project: 'agt' });
  ok((tree.files || []).some((f) => f.path === EXPECT_FILE), EXPECT_FILE + ' written to the project');
  // expand a tool card
  await page.evaluate(() => { const b = document.querySelector('#ag-tl .ag-tool .ag-th'); b && b.click(); });
  await sleep(400);
  await page.screenshot({ path: path.join(SHOTS, 'ag-done.png') });
  // side tabs
  for (const tab of ['changes', 'files', 'terminal']) {
    await page.evaluate((t) => { const b = document.querySelector(`[data-agtab="${t}"]`); b && b.click(); }, tab);
    await sleep(700);
    const txt = await page.$eval('#ag-pane', (e) => e.innerText);
    ok(txt.includes(EXPECT_FILE) || EXPECT.test(txt) || /\$ /.test(txt), `side tab ${tab} shows the agent's work`);
    await page.screenshot({ path: path.join(SHOTS, `ag-side-${tab}.png`) });
  }
  // diff view from changes
  await page.evaluate(() => { const b = document.querySelector('[data-agtab="changes"]'); b && b.click(); });
  await sleep(500);
  await page.evaluate(() => { const r = document.querySelector('.ag-crow'); r && r.click(); });
  await sleep(1200);
  ok(await page.$('#ag-pane .cm-editor, #ag-pane .ag-code'), 'diff/file view opens from Changes');
  await page.screenshot({ path: path.join(SHOTS, 'ag-diff.png') });
  // persistence: reload and reopen
  await page.reload({ waitUntil: 'networkidle2' });
  await page.evaluate(() => document.querySelector('#nav-agent').click());
  await sleep(2000);
  ok((await page.$$('.ag-srow')).length >= 1, 'session persists after reload');
  await page.evaluate(() => document.querySelector('.ag-srow')?.click());
  await sleep(1200);
  ok((await page.$$('#ag-tl .ag-tool')).length >= 2, 'timeline restored after reload');

  // Workspace Agent mode
  await page.evaluate(() => document.querySelector('#nav-workspace').click());
  await page.waitForSelector('#ws-view:not([hidden]) #ws-team', { timeout: 15000 });
  await sleep(1500);
  await page.evaluate(() => document.querySelector('[data-tmode="agent"]')?.click());
  await sleep(600);
  ok(await page.$('#ws-team.agent-mode'), 'workspace team panel switches to Agent mode');
  ok(await page.$('#ws-team-msgs .ag-empty'), 'workspace agent empty state');
  await page.screenshot({ path: path.join(SHOTS, 'ws-agent.png') });
  await page.evaluate(() => document.querySelector('[data-tmode="team"]')?.click());
  await sleep(400);
  ok(!(await page.$('#ws-team.agent-mode')), 'back to Team mode');

  // narrow layout
  await page.setViewport({ width: 420, height: 860, isMobile: true });
  await sleep(800);
  await page.evaluate(() => document.querySelector('#nav-agent')?.click());
  await sleep(1200);
  await page.screenshot({ path: path.join(SHOTS, 'ag-mobile.png') });

  ok(errors.length === 0, 'no page errors' + (errors.length ? ': ' + errors.slice(0, 5).join(' || ') : ''));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

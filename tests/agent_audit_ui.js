// Browser test: the web Agent page applies the finish audit (web/agent_audit.js) like the engine does.
// Needs a server started with the mock model (tests/mock_llm.js) as its only provider. Usage:
//   PW=/tmp/pw/node_modules/playwright BASE=http://127.0.0.1:4189 node tests/agent_audit_ui.js
const { chromium } = require(process.env.PW || 'playwright');
const BASE = process.env.BASE || 'http://127.0.0.1:4189';
const api = (p, body) => fetch(BASE + p, { method: 'POST', headers: { 'content-type': 'application/json', 'x-astro-client': '1' }, body: JSON.stringify(body) }).then((r) => r.json());
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let pass = 0, fail = 0; const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? '  ok   ' : '  FAIL ') + m); };
async function job(page, text) {
  await page.evaluate(() => { const b = [...document.querySelectorAll('button')].find((x) => /new session|new agent/i.test(x.getAttribute('aria-label') || x.title || x.textContent)); b && b.click(); });
  await sleep(600);
  await page.fill('#ag-in', text); await page.click('#ag-send');
  const t0 = Date.now();
  for (;;) { await sleep(1000); const st = await page.$eval('#ag-status', (e) => e.className + ' ' + e.textContent).catch(() => ''); if (!/st-running|st-waiting/.test(st) && Date.now() - t0 > 3000) break; if (Date.now() - t0 > 90000) break; }
  return page.$eval('#ag-tl', (e) => e.innerText);
}
(async () => {
  await api('/api/fs/project/delete', { project: 'aud' }).catch(() => {});
  await api('/api/fs/project/create', { project: 'aud', starter: false });
  await api('/api/fs/write', { project: 'aud', path: 'README.md', content: 'seed\n' }).catch(() => {});
  const browser = await chromium.launch(); const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript(() => { if (window !== window.top) return; localStorage.setItem('astro_agent:team', '0'); localStorage.setItem('astro_ws_v1:project', 'aud'); });
  await page.goto(BASE + '/', { waitUntil: 'networkidle' });
  ok(await page.evaluate(() => typeof window.AstroAudit?.audit === 'function'), 'agent_audit.js is loaded in the app');
  await page.evaluate(() => document.querySelector('#nav-agent').click());
  await page.waitForSelector('#agent-view:not([hidden]) .ag', { timeout: 15000 }); await sleep(1200);
  let tl = await job(page, 'is the app complete?');
  ok(/Checking the real state of the project/.test(tl) && /STATUS REPORT/.test(tl), 'status question: inspected and answered with a status report');
  tl = await job(page, 'build parts one and two');
  const tree = await api('/api/fs/tree', { project: 'aud' });
  ok(/keeps going with the rest of the job/.test(tl) && (tree.files || []).some((f) => f.path === 'two.txt'), 'lazy ending refused; the whole job was done');
  tl = await job(page, 'write the adder');
  const add = await api('/api/fs/read', { project: 'aud', path: 'add.js' }).catch(() => ({}));
  ok(/placeholders/i.test(tl) && /return a \+ b/.test(add.content || ''), 'stub sent back and implemented');
  ok(!errors.length, 'no page errors ' + errors.join(' | '));
  await page.screenshot({ path: '/tmp/aud/agent.png' });
  await browser.close();
  console.log(`agent audit ui: ${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });

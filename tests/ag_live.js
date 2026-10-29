// Real Agent-page run through the browser (uses live free models).
//   NODE_PATH=/tmp/pw/node_modules node tests/ag_live.js <outdir> "<job>" [--url http://127.0.0.1:4173] [--project todo-api] [--mode auto|ask] [--team] [--light]
// Writes <outdir>/mid.png while running and <outdir>/end.png when done, and prints the final status + done-card text.
const { chromium } = require('playwright');
const fs = require('fs');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
(async () => {
  const out = process.argv[2]; const job = process.argv[3];
  fs.mkdirSync(out, { recursive: true });
  const url = arg('--url', 'http://127.0.0.1:4173'); const project = arg('--project', 'todo-api'); const mode = arg('--mode', 'auto');
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1568, height: 907 }, colorScheme: process.argv.includes('--light') ? 'light' : 'dark' });
  const errs = []; p.on('pageerror', (e) => errs.push(e.message));
  await p.addInitScript(({ pr, m, team }) => { try { localStorage.setItem('astro_agent:project', pr); localStorage.setItem(`astro_dev:mode:${pr}`, m); localStorage.setItem('astro_agent:team', team ? '1' : '0'); localStorage.setItem('astro_agent:side', 'session'); } catch (_e) { /* frame */ } }, { pr: project, m: mode, team: process.argv.includes('--team') });
  await p.goto(url); await p.waitForTimeout(1500); await p.goto(url); await p.waitForTimeout(1500);
  await p.evaluate(async () => { window.AstroAgent.switchView('agent'); await new Promise((r) => setTimeout(r, 800)); document.querySelector('#ag-new').click(); });
  await p.waitForTimeout(400);
  await p.fill('#ag-in', job); await p.press('#ag-in', 'Enter');
  const t0 = Date.now(); let mid = false;
  for (;;) {
    await p.waitForTimeout(2000);
    const st = await p.evaluate(() => { const s = window.AstroAgent.agCurSession(); return s ? { status: s.status, steps: s.steps, busy: window.AstroAgent.AG.runs.has(s.id) } : null; });
    if (!mid && st && st.steps >= 2) { await p.screenshot({ path: `${out}/mid.png` }); mid = true; }
    if (st && !st.busy && ['done', 'error', 'stopped', 'paused'].includes(st.status)) { console.log('status:', st.status, 'steps:', st.steps, 'secs:', Math.round((Date.now() - t0) / 1000)); break; }
    if (Date.now() - t0 > 600000) { console.log('timeout', st); break; }
  }
  await p.waitForTimeout(2500);
  await p.screenshot({ path: `${out}/end.png` });
  const done = await p.evaluate(() => { const d = [...document.querySelectorAll('#ag-tl .ag-done')].pop(); return d ? d.innerText.slice(0, 900) : '(no done card)'; });
  console.log('--- done card ---\n' + done);
  if (errs.length) console.log('page errors:', errs);
  await b.close();
})();

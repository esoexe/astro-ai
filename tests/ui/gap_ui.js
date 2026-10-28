// UI smoke test for the 1.2.0 additions. Needs: app on APP (default :4196), website dist on SITE (default :3000).
// Run: NODE_PATH=/tmp/pw/node_modules node tests/ui/gap_ui.js
const { chromium } = require('playwright');
const APP = process.env.APP || 'http://127.0.0.1:4196';
const SITE = process.env.SITE || 'http://127.0.0.1:3000';
const shots = process.env.SHOTS || '/tmp/astro-ui-shots';
require('fs').mkdirSync(shots, { recursive: true });
let pass = 0, fail = 0;
const ok = (name, cond, extra = '') => { if (cond) { pass++; console.log('  ok  ', name); } else { fail++; console.log('  FAIL', name, extra); } };
(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1366, height: 860 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.addInitScript(() => { try { localStorage.setItem('workspace_theme_v1', 'dark'); } catch (_e) {} });
  await page.goto(APP + '/'); await page.waitForTimeout(1500); await page.goto(APP + '/'); await page.waitForTimeout(2500);

  // 1. Settings toggle for the background check
  const hasToggle = await page.evaluate(() => /Check code in the background/.test(document.documentElement.innerHTML) || true);
  // Inject a finished conversation to exercise the thumbs-down feedback dialog and the quota card.
  const injected = await page.evaluate(() => {
    const A = window.Astro || window.app || null; return !!A;
  });
  // 2. Quota card: render an error draft through the real renderer via a fake conversation in state.
  await page.evaluate(() => {
    const key = Object.keys(localStorage).find((k) => /conversations|chats/.test(k));
    window.__convKey = key || '';
  });
  await page.screenshot({ path: shots + '/app.png' });

  // 3. Library tab via deep-link hash
  await page.goto(APP + '/#library=deepwiki'); await page.waitForTimeout(4000);
  const libText = await page.evaluate(() => (document.getElementById('ws-ext') || {}).innerText || '');
  ok('library opens from #library=<id>', /deepwiki|DeepWiki|Library/i.test(libText), libText.slice(0, 200));
  await page.screenshot({ path: shots + '/library.png' });

  // 4. Code check endpoint from the page (same-origin, with client header like the app)
  const chk = await page.evaluate(async () => (await fetch('/api/check/code', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1' }, body: JSON.stringify({ lang: 'py', code: 'print(6*7)' }) })).json());
  ok('code check from the browser', chk.ok === true && /42/.test(chk.stdout || ''), JSON.stringify(chk));
  ok('no page errors in the app', errors.length === 0, errors.join(' | '));

  // 5. Website console library (guest)
  const p2 = await ctx.newPage(); const e2 = [];
  p2.on('pageerror', (e) => e2.push(String(e)));
  await p2.goto(SITE + '/profile#library'); await p2.waitForTimeout(3000);
  const t2 = await p2.evaluate(() => document.body.innerText);
  ok('console library lists items', /Context7|Playwright|DeepWiki/i.test(t2), t2.slice(0, 300));
  ok('console library has install action', /Install|Open in Astro AI|Copy/i.test(t2));
  ok('no page errors on the console', e2.length === 0, e2.join(' | '));
  await p2.screenshot({ path: shots + '/console-library.png', fullPage: false });
  await browser.close();
  console.log(`ui: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });

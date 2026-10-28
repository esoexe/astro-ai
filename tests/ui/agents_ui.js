// UI test for the Background & parallel agents panel (web/agents.js).
// Needs the app on APP (default :4193) with the mock model and a project "demo".
// Run: NODE_PATH=/tmp/pw/node_modules node tests/ui/agents_ui.js
const { chromium } = require('playwright');
const APP = process.env.APP || 'http://127.0.0.1:4193';
const shots = process.env.SHOTS || '/tmp/astro-ui-shots';
require('fs').mkdirSync(shots, { recursive: true });
let pass = 0, fail = 0;
const ok = (name, cond, extra = '') => { if (cond) { pass++; console.log('  ok  ', name); } else { fail++; console.log('  FAIL', name, extra); } };
(async () => {
  const browser = await chromium.launch();
  const page = await (await browser.newContext({ viewport: { width: 1366, height: 860 } })).newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.addInitScript(() => { try { localStorage.setItem('workspace_theme_v1', 'dark'); } catch (_e) {} });
  await page.goto(APP + '/'); await page.waitForTimeout(1200); await page.goto(APP + '/'); await page.waitForTimeout(2000);
  await page.click('#nav-agent'); await page.waitForTimeout(800);
  ok('Parallel chip on the Agent page', await page.locator('[data-bgagents]').count() === 1);
  await page.evaluate(() => { window.AstroAgent.AG.project = 'demo'; });
  await page.click('[data-bgagents]'); await page.waitForTimeout(500);
  ok('panel opens', await page.locator('#bg-new').count() === 1);
  await page.fill('#bg-tasks', 'create file u1.txt containing one\ncreate file u2.txt containing two');
  await page.click('#bg-new [type=submit]');
  await page.waitForFunction(() => document.querySelectorAll('.bg-row').length >= 2, null, { timeout: 10000 }).catch(() => {});
  ok('two agents listed', await page.locator('.bg-row').count() >= 2);
  await page.waitForFunction(() => [...document.querySelectorAll('.bg-row small')].filter((s) => /^done/.test(s.textContent)).length >= 2, null, { timeout: 15000 }).catch(() => {});
  ok('both finish', (await page.locator('.bg-row small', { hasText: /^done/ }).count()) >= 2);
  await page.screenshot({ path: shots + '/agents-list.png' });
  await page.locator('.bg-row [data-act="view"]').first().click();
  await page.waitForSelector('.bg-ev.done', { timeout: 8000 }).catch(() => {});
  ok('detail shows the timeline', await page.locator('.bg-ev.done').count() === 1);
  await page.click('[data-d="diff"]'); await page.waitForSelector('.bg-diffpre .add', { timeout: 5000 }).catch(() => {});
  ok('diff renders', await page.locator('.bg-diffpre .add').count() > 0);
  await page.screenshot({ path: shots + '/agents-detail.png' });
  await page.click('[data-d="merge"]'); await page.waitForTimeout(1200);
  ok('merge returns to the list', await page.locator('#bg-new').count() === 1);
  const merged = await page.evaluate(async () => (await fetch('/api/agent/list', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1' }, body: '{"project":"demo"}' })).json());
  ok('one agent merged', merged.sessions.filter((s) => s.merged).length === 1, JSON.stringify(merged).slice(0, 300));
  ok('no page errors', errors.length === 0, errors.join(' | '));
  await browser.close();
  console.log(`agents ui: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();

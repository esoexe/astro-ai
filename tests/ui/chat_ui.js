// Live chat: answer, background code check runs, thumbs-down opens the feedback dialog, Library install works.
const { chromium } = require('playwright');
const APP = process.env.APP || 'http://127.0.0.1:4196';
const shots = process.env.SHOTS || '/tmp/astro-ui-shots'; require('fs').mkdirSync(shots, { recursive: true });
let pass = 0, fail = 0;
const ok = (n, c, x = '') => { if (c) { pass++; console.log('  ok  ', n); } else { fail++; console.log('  FAIL', n, x); } };
(async () => {
  const browser = await chromium.launch();
  const page = await (await browser.newContext({ viewport: { width: 1366, height: 860 } })).newPage();
  const errors = []; page.on('pageerror', (e) => errors.push(String(e)));
  const checks = []; page.on('request', (r) => { if (r.url().includes('/api/check/code')) checks.push(r.postData()); });
  await page.addInitScript((m) => { try { localStorage.setItem('workspace_model_v1', m); localStorage.setItem('workspace_brain_default_v1', '1'); } catch (_e) {} }, process.env.MODEL || 'ovh:Mistral-Small-3.2-24B-Instruct-2506');
  await page.goto(APP + '/'); await page.waitForTimeout(1500); await page.goto(APP + '/'); await page.waitForTimeout(2500);
  await page.click('#nav-assistant').catch(() => {});
  await page.fill('#message-input', process.env.PROMPT || 'Write a short Python function is_prime(n) and print is_prime(7). Code only.');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(30000);
  await page.locator('button', { hasText: 'Answer now' }).first().click({ timeout: 3000 }).catch(() => {});
  await page.waitForFunction(() => !document.querySelector('.cursor') && /def /.test(document.body.innerText), null, { timeout: 150000 }).catch(() => {});
  await page.waitForTimeout(8000);
  const txt = await page.evaluate(() => document.body.innerText);
  ok('answer with code arrived', /def /.test(txt), txt.slice(-300));
  if (process.env.EXPECT_FIX) { await page.waitForFunction(() => /Background check/.test(document.body.innerText), null, { timeout: 120000 }).catch(() => {}); const t3 = await page.evaluate(() => document.body.innerText); ok('background check posted a result', /Background check/.test(t3), t3.slice(-600)); await page.screenshot({ path: shots + '/bgcheck.png' }); }
  ok('background check ran after the answer', checks.length >= 1, String(checks.length));
  // thumbs down → feedback dialog
  const down = page.locator('[data-act="down"]').last();
  if (await down.count()) {
    await down.click(); await page.waitForTimeout(600);
    ok('feedback dialog opens', await page.locator('#fb-share').count() === 1);
    ok('sharing is off by default', !(await page.locator('#fb-share').isChecked().catch(() => true)));
    await page.screenshot({ path: shots + '/feedback.png' });
    await page.click('#fb-send'); await page.waitForTimeout(800);
  } else ok('thumbs-down button exists', false);
  // library install through the deep-link dialog
  await page.goto(APP + '/#library=deepwiki'); await page.waitForTimeout(4000);
  const btn = page.locator('.modal button', { hasText: /^Install$/ }).first();
  if (await btn.count()) { await btn.click(); await page.waitForTimeout(3000); }
  const lib = await page.evaluate(async () => (await fetch('/api/dev/library/index', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1' }, body: '{}' })).json().catch(() => ({})));
  ok('deepwiki installed from the dialog', JSON.stringify(lib).includes('deepwiki'), JSON.stringify(lib).slice(0, 300));
  ok('no page errors', errors.length === 0, errors.join(' | '));
  await browser.close();
  console.log(`chat ui: ${pass} passed, ${fail} failed`); process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });

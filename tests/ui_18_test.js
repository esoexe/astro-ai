// 1.8.0 UI check: welcome screen, Simple menu, error-report opt-in, feedback (sent to a stubbed /api/report),
// Settings rows (Natural voice, Local only, Agent sandbox, Pro), French/Arabic, phone layout. Screenshots go to OUT.
// Usage: start an engine (ASTRO_AUTH=off ... PORT=4380 ./workspace_server), then
//   NODE_PATH=~/.cache/pw/node_modules node tests/ui_18_test.js [http://127.0.0.1:4380] [out-dir]
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const BASE = process.argv[2] || 'http://127.0.0.1:4380';
const OUT = process.argv[3] || '/tmp/ui18';
fs.mkdirSync(OUT, { recursive: true });
let fails = 0;
const ok = (cond, what) => { console.log(`${cond ? 'PASS' : 'FAIL'}  ${what}`); if (!cond) fails++; };

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 1366, height: 860 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource|net::ERR/.test(m.text())) errors.push(m.text()); });
  // never send a real report from a test
  const reports = [];
  await page.route('**/api/report', async (route) => { reports.push(JSON.parse(route.request().postData() || '{}')); await route.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true,"saved":true,"shared":true}' }); });

  // ---------- first run ----------
  await page.goto(BASE + '/', { waitUntil: 'load' });
  await page.waitForSelector('#modal-title', { timeout: 8000 }).catch(() => null);
  ok((await page.textContent('#modal-title').catch(() => '')) === 'Welcome to A.S.T.R.O AI', 'welcome screen opens on first run');
  await page.screenshot({ path: `${OUT}/01-welcome.png` });
  ok(await page.evaluate(() => document.getElementById('app')?.classList.contains('simple-menu')), 'new users start with the Simple menu');
  ok(await page.evaluate(() => localStorage.getItem('astro_reports_v1') !== '1'), 'error reports are off by default');
  await page.click('#wel-reports + span').catch(() => page.check('#wel-reports'));
  ok(await page.evaluate(() => localStorage.getItem('astro_reports_v1') === '1'), 'the welcome switch turns error reports on');
  await page.click('[data-wel="ask"]');
  await page.waitForTimeout(300);
  ok(await page.evaluate(() => { const b = document.querySelector('.modal-backdrop, #modal-backdrop'); return !b || b.hidden || getComputedStyle(b).display === 'none'; }), '"Ask anything" closes the welcome screen');
  await page.reload({ waitUntil: 'load' }); await page.waitForTimeout(1500);
  ok(!(await page.isVisible('text=Welcome to A.S.T.R.O AI')), 'the welcome screen does not come back after a reload');

  // ---------- Simple menu ----------
  ok(await page.isVisible('#nav-more'), '"More" is shown in the Simple menu');
  ok(!(await page.isVisible('#nav-notes')), 'Notes is tucked under More');
  await page.click('#nav-more'); await page.waitForTimeout(250);
  ok(await page.isVisible('#nav-notes'), 'More reveals Notes, Tasks and the other tools');
  await page.screenshot({ path: `${OUT}/02-simple-menu-open.png` });

  // ---------- Settings ----------
  const openTab = async (tab) => { await page.evaluate((t) => window.AstroAgent.openSettings(t), tab); await page.waitForTimeout(900); };
  await openTab('voice');
  await page.waitForFunction(() => /Installed \(/.test(document.getElementById('nv-state')?.textContent || ''), null, { timeout: 8000 }).catch(() => null);
  const nv = await page.textContent('#nv-state').catch(() => '');
  ok(/Installed \(ar_JO, en_US, fr_FR\)|Installed \(en_US, fr_FR, ar_JO\)|Installed \(.*en_US.*\)/.test(nv), `Natural voice row shows the installed voices: "${nv.slice(0, 60)}…"`);
  await page.screenshot({ path: `${OUT}/03-settings-voice.png` });

  await openTab('data');
  await page.waitForFunction(() => /Available|On \(|Not available/.test(document.getElementById('sbx-state')?.textContent || ''), null, { timeout: 8000 }).catch(() => null);
  const sbx = await page.textContent('#sbx-state').catch(() => '');
  ok(/Available \(bubblewrap\)/.test(sbx), `Agent sandbox row: "${sbx.slice(0, 50)}…"`);
  await page.click('#set-sandbox + span').catch(() => page.check('#set-sandbox'));
  await page.waitForFunction(() => /^On \(/.test(document.getElementById('sbx-state')?.textContent || ''), null, { timeout: 5000 }).catch(() => null);
  ok(/^On \(bubblewrap\)/.test(await page.textContent('#sbx-state')), 'turning the sandbox on is saved by the engine');
  ok(await page.isVisible('#sbx-net-row'), 'the "allow internet" row appears when the sandbox is on');
  await page.screenshot({ path: `${OUT}/04-settings-data-sandbox-on.png` });
  await page.click('#set-sandbox + span').catch(() => page.uncheck('#set-sandbox'));
  await page.waitForFunction(() => /^Available/.test(document.getElementById('sbx-state')?.textContent || ''), null, { timeout: 5000 }).catch(() => null);
  const sb = await page.evaluate(async () => (await fetch('/api/sandbox')).json());
  ok(sb.on === false, 'the sandbox is off again (engine agrees)');

  await openTab('brain');
  ok(await page.isVisible('#set-localonly + span'), 'Local only switch is in Settings > Brain');
  ok(await page.isVisible('text=Local models are also the automatic fallback'), 'Local only explains the automatic local fallback');
  await page.screenshot({ path: `${OUT}/05a-settings-brain-local.png` });
  await openTab('general');
  ok(await page.isVisible('text=Send feedback'), 'Send feedback row is in Settings');
  await page.screenshot({ path: `${OUT}/05-settings-general.png`, fullPage: false });

  await openTab('account');
  ok(await page.isVisible('text=A.S.T.R.O AI Pro'), 'Pro row is in Settings > Account');
  ok(await page.isVisible('text=Not on sale yet'), 'Pro says honestly that it is not on sale');
  await page.screenshot({ path: `${OUT}/06-settings-account-pro.png` });

  // ---------- feedback ----------
  await openTab('general');
  await page.click('button:has-text("Send feedback")');
  await page.waitForSelector('#fbk-form', { timeout: 4000 }).catch(() => null);
  ok(await page.isVisible('#fbk-form'), 'feedback form opens');
  await page.click('#fbk-cat [data-v="idea"]');
  await page.fill('#fbk-msg', 'UI test: please ignore');
  await page.screenshot({ path: `${OUT}/07-feedback.png` });
  await page.click('#fbk-send');
  await page.waitForTimeout(800);
  ok(reports.length === 1 && reports[0].kind === 'feedback' && reports[0].category === 'idea' && reports[0].message === 'UI test: please ignore', 'feedback posts kind=feedback, the picked category and the message');
  ok(await page.isVisible('text=Thank you! Your feedback was sent.'), 'a thank-you toast is shown');

  // ---------- languages ----------
  for (const l of ['fr', 'ar']) {
    await page.evaluate((x) => window.AstroI18n.set(x), l);
    await openTab('data'); await page.waitForTimeout(600);
    const txt = await page.textContent('#sbx-state').catch(() => '');
    ok(l === 'fr' ? /^Disponible \(bubblewrap\)/.test(txt) : /^متاح \(bubblewrap\)/.test(txt), `${l}: sandbox status is translated ("${txt.slice(0, 40)}…")`);
    if (l === 'ar') ok(await page.evaluate(() => document.documentElement.dir === 'rtl'), 'ar: the page is right-to-left');
    await page.screenshot({ path: `${OUT}/08-settings-data-${l}.png` });
  }
  await page.evaluate(() => window.AstroI18n.set('en'));
  await page.keyboard.press('Escape');

  // ---------- phone ----------
  const phone = await browser.newContext({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true, deviceScaleFactor: 2 });
  const pp = await phone.newPage();
  pp.on('pageerror', (e) => errors.push('phone: ' + e));
  await pp.goto(BASE + '/', { waitUntil: 'load' });
  await pp.waitForSelector('#modal-title', { timeout: 8000 }).catch(() => null);
  const box = await pp.evaluate(() => { const m = document.querySelector('.modal'); if (!m) return null; const r = m.getBoundingClientRect(); return { l: r.left, r: r.right, w: innerWidth }; });
  ok(box && box.l >= 0 && box.r <= box.w + 1, 'phone: the welcome screen fits the screen width');
  await pp.screenshot({ path: `${OUT}/09-phone-welcome.png` });

  ok(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} check(s) failed` : '\nall UI checks passed');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

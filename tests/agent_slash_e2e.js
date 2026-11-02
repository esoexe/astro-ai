// The "/" menu of the agent composers (Agent page and Workspace agent mode).
// Needs an engine with the astro-seed pack installed (tests/agent_slash_e2e.sh does both) and Playwright.
//   node tests/agent_slash_e2e.js [http://127.0.0.1:4372]
// Exit 0 = all checks passed (or Playwright is missing: SKIP), 1 = a check failed.
let chromium;
try { ({ chromium } = require('playwright')); } catch { console.log('agent slash e2e: SKIP (playwright not installed)'); process.exit(0); }
const BASE = process.argv[2] || process.env.ASTRO_URL || 'http://127.0.0.1:4372';
let pass = 0; let fail = 0;
const ok = (cond, name, extra) => { if (cond) { pass++; } else { fail++; console.log(`FAIL ${name}${extra !== undefined ? `: ${JSON.stringify(extra)}` : ''}`); } };

(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1300, height: 860 } });
  const errs = []; p.on('pageerror', (e) => errs.push(e.message));
  await p.addInitScript(() => { try { localStorage.setItem('astro_welcome_v1', '1'); } catch (_e) { /* ignore */ } });
  await p.goto(`${BASE}/`, { waitUntil: 'networkidle' });
  await p.evaluate(() => window.AstroI18n?.set('en'));
  await p.evaluate(() => document.getElementById('modal-close')?.click()); await p.keyboard.press('Escape'); await p.waitForTimeout(400);
  const menu = (host) => p.evaluate((h) => {
    const box = document.querySelector(`${h} .ag-slash`);
    if (!box) return null;
    return { open: !box.hidden, items: [...box.querySelectorAll('.ag-slash-item')].map((x) => ({ cmd: x.querySelector('code').textContent, desc: x.querySelector('span').textContent, kind: x.querySelector('i').textContent, sel: x.classList.contains('sel') })) };
  }, host);
  const waitOpen = (host) => p.waitForFunction((h) => { const x = document.querySelector(`${h} .ag-slash`); return x && !x.hidden && x.children.length; }, host, { timeout: 8000 }).then(() => true, () => false);
  const val = (sel) => p.$eval(sel, (t) => t.value);

  // ---- Agent page ----
  await p.click('#nav-agent');
  await p.waitForSelector('#ag-in');
  const ta = '#ag-in'; const host = '#ag-form';
  await p.click(ta); await p.keyboard.type('/');
  ok(await waitOpen(host), 'page: "/" opens the menu');
  let m = await menu(host);
  ok(m.items[0]?.cmd === '/help' && m.items[0].sel, 'page: built-ins first, first one selected', m.items[0]);
  ok(m.items.filter((x) => x.kind === 'Command').length >= 11, 'page: all 11 built-in commands listed', m.items.length);
  ok(m.items.filter((x) => x.kind === 'Skill' && x.cmd.startsWith('/seed-')).length === 11, 'page: the 11 seed pack skills listed', m.items.filter((x) => x.kind === 'Skill').map((x) => x.cmd));
  ok(m.items.find((x) => x.cmd === '/seed-closer')?.desc.length > 10, 'page: skills show their description');

  await p.keyboard.type('seed-c');
  await p.waitForFunction(() => [...document.querySelectorAll('#ag-form .ag-slash-item code')].every((c) => c.textContent.includes('seed-c')), null, { timeout: 5000 }).catch(() => {});
  m = await menu(host);
  ok(m.open && m.items.length >= 2 && m.items.every((x) => x.cmd.includes('seed-c')), 'page: typing filters the list', m.items.map((x) => x.cmd));
  const second = m.items[1]?.cmd;
  await p.keyboard.press('ArrowDown');
  m = await menu(host);
  ok(m.items[1]?.sel, 'page: ArrowDown moves the selection');
  await p.keyboard.press('Enter');
  ok(await val(ta) === `${second} `, 'page: Enter fills in the picked skill instead of sending', await val(ta));
  m = await menu(host);
  ok(!m.open, 'page: menu closes after a pick');
  const userItems = () => p.$$eval('#ag-tl .ag-user, #ag-tl [data-kind="user"]', (x) => x.length);
  ok(await userItems() === 0, 'page: nothing was sent by the pick');

  // Tab also picks; Escape closes and keeps the text.
  await p.fill(ta, ''); await p.keyboard.type('/rev');
  await waitOpen(host);
  await p.keyboard.press('Tab');
  ok(await val(ta) === '/review ', 'page: Tab fills in', await val(ta));
  await p.fill(ta, ''); await p.keyboard.type('/');
  await waitOpen(host);
  await p.keyboard.press('Escape');
  m = await menu(host);
  ok(!m.open && await val(ta) === '/', 'page: Escape closes and keeps the text');

  // Text that is not a bare command never opens it.
  await p.fill(ta, ''); await p.keyboard.type('fix the /seed folder');
  await p.waitForTimeout(400);
  ok(!(await menu(host)).open, 'page: no menu in the middle of a sentence');
  await p.fill(ta, ''); await p.keyboard.type('/zzzz-nothing');
  await p.waitForTimeout(400);
  ok(!(await menu(host)).open, 'page: no menu when nothing matches');

  // A finished command still sends on Enter.
  await p.fill(ta, ''); await p.keyboard.type('/help');
  await waitOpen(host);
  await p.keyboard.press('Enter');
  await p.waitForFunction(() => /Commands/.test(document.querySelector('#ag-tl')?.textContent || ''), null, { timeout: 8000 }).catch(() => {});
  ok(await val(ta) === '', 'page: Enter on a complete command sends it', await val(ta));
  ok(/seed-closer/.test(await p.$eval('#ag-tl', (x) => x.textContent)), 'page: /help ran and lists the pack skills');
  await p.screenshot({ path: process.env.SHOT_DIR ? `${process.env.SHOT_DIR}/agent-slash-page.png` : '/tmp/agent-slash-page.png' }).catch(() => {});

  // French: badges and built-in descriptions are translated.
  await p.evaluate(() => window.AstroI18n?.set('fr'));
  await p.fill(ta, ''); await p.click(ta); await p.keyboard.type('/');
  await waitOpen(host); await p.waitForTimeout(500);
  m = await menu(host);
  ok(m.items[0]?.kind === 'Commande' && /commandes/i.test(m.items[0].desc), 'page: French badge and description', m.items[0]);
  await p.keyboard.press('Escape');
  await p.evaluate(() => window.AstroI18n?.set('en'));

  // ---- Workspace: only in Agent mode (the team chat has no slash commands) ----
  await p.click('#nav-workspace');
  await p.waitForSelector('#ws-team-in', { timeout: 15000 });
  const wta = '#ws-team-in'; const whost = '#ws-team-form';
  await p.click('[data-tmode="team"]').catch(() => {});
  await p.click(wta); await p.keyboard.type('/');
  await p.waitForTimeout(600);
  ok(!(await menu(whost))?.open, 'workspace: no menu in Team mode');
  await p.fill(wta, '');
  await p.click('[data-tmode="agent"]');
  await p.click(wta); await p.keyboard.type('/seed-p');
  ok(await waitOpen(whost), 'workspace: menu opens in Agent mode');
  m = await menu(whost);
  ok(m.items[0]?.cmd === '/seed-plan', 'workspace: lists pack skills', m.items.map((x) => x.cmd));
  await p.keyboard.press('Enter');
  ok(await val(wta) === '/seed-plan ', 'workspace: Enter fills in', await val(wta));
  await p.screenshot({ path: process.env.SHOT_DIR ? `${process.env.SHOT_DIR}/agent-slash-ws.png` : '/tmp/agent-slash-ws.png' }).catch(() => {});

  ok(errs.length === 0, 'no page errors', errs);
  await b.close();
  console.log(`agent slash e2e: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log(`agent slash e2e: ERROR ${e.message}`); process.exit(1); });

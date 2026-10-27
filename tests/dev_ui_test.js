// Browser test for the developer platform (web/dev.js). Usage: node tests/dev_ui_test.js [port]
// Needs Playwright (cd /tmp/pw && npm i playwright@1 && npx playwright install chromium) and a server with ASTRO_AUTH=off.
// Set AGENT=1 to also run a real agent job in Ask mode (needs at least one working model).
const path = require('path');
const fs = require('fs');
const os = require('os');
let chromium;
try { ({ chromium } = require('playwright')); } catch (_e) { ({ chromium } = require('/tmp/pw/node_modules/playwright')); }
const PORT = process.argv[2] || '4189';
const BASE = `http://localhost:${PORT}`;
const SHOTS = process.env.SHOTS || '/tmp/dev-shots';
fs.mkdirSync(SHOTS, { recursive: true });
const fixture = path.join(__dirname, 'fixtures', 'mcp_echo.js');

(async () => {
  const F = fs.mkdtempSync(path.join(os.tmpdir(), 'astro-ui-'));
  fs.mkdirSync(path.join(F, '.claude/skills/hello'), { recursive: true });
  fs.mkdirSync(path.join(F, '.claude/commands'), { recursive: true });
  fs.mkdirSync(path.join(F, 'src'), { recursive: true });
  fs.writeFileSync(path.join(F, 'AGENTS.md'), '# Rules\nUse two spaces.\n');
  fs.writeFileSync(path.join(F, '.claude/skills/hello/SKILL.md'), '---\nname: hello-skill\ndescription: Greets people politely\n---\nSay hello.\n');
  fs.writeFileSync(path.join(F, '.claude/commands/shout.md'), '---\ndescription: Shout the arguments\n---\nShout $ARGUMENTS loudly.\n');
  fs.writeFileSync(path.join(F, 'src/bad.js'), 'function f( {\n  return 1;\n}\n');
  fs.writeFileSync(path.join(F, 'src/ok.js'), 'const x = 1;\n');
  fs.writeFileSync(path.join(F, '.mcp.json'), JSON.stringify({ mcpServers: { echo: { command: 'node', args: [fixture] } } }));

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('console', (m) => { if (m.type() === 'error' && !/favicon|Failed to load resource/.test(m.text())) errors.push(m.text()); });
  let fail = 0;
  const ok = (name, cond, extra = '') => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${name}${cond ? '' : ` ${extra}`}`); if (!cond) fail = 1; };

  await page.goto(BASE);
  await page.waitForTimeout(2500);
  await page.reload();
  await page.waitForTimeout(1500);
  await page.waitForFunction(() => window.AstroDev && window.AstroAgent, null, { timeout: 15000 });
  ok('dev.js loaded and wired', await page.evaluate(() => Boolean(window.AstroDev.tools.glob && window.AstroAgent.agSend)));

  // Open folder through the project menu
  await page.evaluate(() => window.AstroAgent.switchView('workspace'));
  await page.waitForSelector('#ws-proj', { timeout: 15000 });
  await page.waitForTimeout(800);
  await page.click('#ws-proj');
  await page.click('text=Open folder…');
  await page.waitForSelector('#dev-bpath');
  await page.fill('#dev-bpath', F);
  await page.click('#dev-bgo');
  await page.waitForSelector('.dev-bdir >> text=src');
  await page.screenshot({ path: `${SHOTS}/open-folder.png` });
  await page.click('#dev-bopen');
  const name = path.basename(F);
  await page.waitForFunction((n) => document.querySelector('#ws-proj-name')?.textContent === n, name, { timeout: 15000 });
  await page.waitForSelector('#ws-tree >> text=AGENTS.md', { timeout: 10000 });
  ok('folder opened as a project', true);
  const linked = await page.evaluate((n) => window.AstroAgent.ws.projects.find((p) => p.name === n)?.linked, name);
  ok('project is marked linked', linked === F, String(linked));

  // Extensions → skills
  await page.click('[data-side="ext"]');
  await page.click('[data-xtab="skills"]');
  await page.waitForSelector('#ws-ext >> text=hello-skill', { timeout: 10000 });
  ok('skill from .claude/skills listed', true);
  await page.screenshot({ path: `${SHOTS}/ext-skills.png` });
  // MCP: untrusted → trust → running
  await page.click('[data-xtab="mcp"]');
  await page.waitForSelector('#ws-ext >> text=untrusted', { timeout: 10000 });
  await page.click('#ws-ext [data-x="mcp-trust"]');
  await page.waitForSelector('#ws-ext .mcp-running', { timeout: 30000 });
  ok('project MCP server trusted and running', await page.locator('#ws-ext >> text=2 tools').count() > 0);
  await page.screenshot({ path: `${SHOTS}/ext-mcp.png` });

  // Problems + editor squiggles
  await page.click('[data-side="problems"]');
  await page.click('[data-prob="check"]');
  await page.waitForSelector('#ws-probs .dev-prob', { timeout: 20000 });
  ok('problems found in src/bad.js', await page.locator('#ws-probs >> text=src/bad.js').count() > 0);
  await page.click('#ws-probs .dev-prob');
  await page.waitForTimeout(800);
  ok('editor shows the error squiggle', await page.locator('#ws-cm .cm-lintRange-error').count() > 0);
  await page.screenshot({ path: `${SHOTS}/problems.png` });

  // Snippets + theme from the extension store
  await page.evaluate(async () => {
    const api = window.AstroAgent.apiJson;
    await api('/api/dev/ext/write', { path: 'snippets/test.json', content: JSON.stringify({ name: 'Test', language: 'javascript', items: { Hello: { prefix: 'zzhello', body: ['console.log("${1:hi}");$0'], description: 'test snippet' } } }) });
    await api('/api/dev/ext/write', { path: 'themes/test-dark.json', content: JSON.stringify({ name: 'Test Dark', type: 'dark', colors: { 'editor.background': '#101820', 'editor.foreground': '#e0e0e0' }, tokenColors: [{ scope: 'keyword', settings: { foreground: '#ff00aa' } }, { scope: ['string'], settings: { foreground: '#00ff88' } }] }) });
    window.AstroDev.invalidate(); await window.AstroDev.applyLooks();
  });
  await page.click('[data-side="ext"]');
  await page.click('[data-xtab="themes"]');
  await page.waitForSelector('#ws-ext >> text=Test Dark', { timeout: 10000 });
  await page.click('#ws-ext [data-x="theme-use"][data-path="themes/test-dark.json"]');
  await page.waitForTimeout(600);
  const css = await page.evaluate(() => document.querySelector('style[data-dev="theme"]')?.textContent || '');
  ok('theme applied to the editor', css.includes('--cm-kw: #ff00aa') && css.includes('#101820'), css.slice(0, 200));
  await page.screenshot({ path: `${SHOTS}/theme.png` });
  await page.evaluate(() => window.AstroAgent.wsOpen('src/ok.js'));
  await page.waitForTimeout(500);
  await page.click('#ws-cm .cm-content');
  await page.keyboard.press('Control+End');
  await page.keyboard.type('\nzzhel');
  await page.keyboard.press('Control+Space');
  await page.waitForTimeout(600);
  ok('snippet offered in completions', await page.locator('.cm-tooltip-autocomplete >> text=zzhello').count() > 0);
  await page.keyboard.press('Escape');
  await page.evaluate(() => { window.AstroAgent.ws.cm.undo(); window.AstroAgent.ws.cm.undo(); });

  // Agent tools and modes (no model needed)
  const r = await page.evaluate(async (n) => {
    const s = { id: 'test', project: n, items: [], convo: [] };
    const D = window.AstroDev;
    const glob = await D.tools.glob(s, { pattern: 'src/*.js' }, {});
    const skill = await D.tools.skill(s, { name: 'hello-skill' }, {});
    const mcp = await D.tools.mcp(s, { server: 'echo', name: 'add', arguments: { a: 20, b: 22 } }, {});
    const diag = await D.tools.diagnostics(s, { paths: ['src/bad.js'] }, {});
    const plan = await D.beforeTool({ ...s, mode: 'plan' }, { tool: 'write_file', path: 'x.txt', content: 'x' }, { v: 1 }, new AbortController().signal);
    const planRead = await D.beforeTool({ ...s, mode: 'plan' }, { tool: 'bash', command: 'ls -la' }, { v: 1 }, new AbortController().signal);
    const brief = await D.brief({ ...s, mode: 'ask' });
    const cmd = await D.agentInput(s, '/shout the build is green');
    return { glob, skill, mcp, diag, plan, planRead, brief, cmd, mode: D.mode({ project: n, items: [] }) };
  }, name);
  ok('glob tool', r.glob.includes('src/bad.js') && r.glob.includes('src/ok.js'), r.glob);
  ok('skill tool', r.skill.includes('Say hello.'), r.skill);
  ok('mcp tool', r.mcp.trim() === '42', r.mcp);
  ok('diagnostics tool', /src\/bad\.js:\d+/.test(r.diag), r.diag);
  ok('plan mode blocks writes', Boolean(r.plan?.block), JSON.stringify(r.plan));
  ok('plan mode allows read-only bash', r.planRead === null, JSON.stringify(r.planRead));
  ok('brief has rules, skills, MCP tools, mode', r.brief.includes('Use two spaces') && r.brief.includes('hello-skill') && r.brief.includes('echo · add') && r.brief.includes('MODE: ASK'), r.brief.slice(0, 400));
  ok('custom slash command expands $ARGUMENTS', r.cmd === 'Shout the build is green loudly.', r.cmd);
  ok('linked folders default to Ask mode', r.mode === 'ask', r.mode);

  // Agent page: /help and /mode in the real composer
  await page.evaluate((n) => { const A = window.AstroAgent; A.AG.project = n; A.ls.set('astro_agent:project', n); A.switchView('agent'); }, name);
  await page.waitForSelector('#ag-in', { timeout: 10000 });
  await page.waitForTimeout(1200);
  await page.fill('#ag-in', '/help');
  await page.keyboard.press('Enter');
  await page.waitForSelector('.ag-dev >> text=Commands', { timeout: 10000 });
  ok('/help lists commands', await page.locator('.ag-dev >> text=/shout').count() > 0);
  await page.fill('#ag-in', '/mode plan');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(1800);
  ok('mode chip shows Plan', (await page.textContent('[data-devmode]')).includes('Plan'));
  await page.fill('#ag-in', '/mode ask');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(1800);
  await page.screenshot({ path: `${SHOTS}/agent-help.png` });

  if (process.env.AGENT === '1') {
    await page.fill('#ag-in', 'Create a file named hello.txt in the project root containing exactly the text: hi from astro. Do nothing else, then finish.');
    await page.keyboard.press('Enter');
    const appr = page.locator('.ag-approve').first();
    // Some models ask a clarifying question first: answer it with the first option, then wait for the approval card.
    const t0 = Date.now();
    while (Date.now() - t0 < 180000 && !(await appr.count())) {
      const q = page.locator('.ag-ask:not(.ag-approve) [data-agans]').first();
      if (await q.count()) { console.log('     (answered an agent question)'); await q.click(); }
      await page.waitForTimeout(1500);
    }
    try { await appr.waitFor({ timeout: 180000 }); } catch (e) { await page.screenshot({ path: SHOTS + '/agent-stuck.png' }); console.log('AGENT TIMELINE:', (await page.evaluate(() => (document.querySelector('#ag-log, .ag-log, .ag-main') || document.body).innerText)).slice(-2500)); throw e; }
    await page.screenshot({ path: `${SHOTS}/agent-approval.png` });
    ok('Ask mode shows an approval card before changing files', !fs.existsSync(path.join(F, 'hello.txt')));
    await page.click('.ag-approve [data-agans="Allow all in this run"]');
    // Wait for the file (the whole-team review after it can take minutes, so the test skips it with the stop button).
    for (let k = 0; k < 120 && !fs.existsSync(path.join(F, 'hello.txt')); k++) await page.waitForTimeout(1000);
    await page.waitForTimeout(1000);
    const txt = fs.existsSync(path.join(F, 'hello.txt')) ? fs.readFileSync(path.join(F, 'hello.txt'), 'utf8') : '';
    ok('agent wrote the file into the real folder after approval', /hi from astro/.test(txt), JSON.stringify(txt));
    await page.screenshot({ path: `${SHOTS}/agent-done.png` });
    await page.evaluate(() => { const A = window.AstroAgent; const cur = A.agCurSession?.(); if (cur) A.agStop(cur); });
  }

  // Close the folder: the files must stay
  await page.evaluate(async (n) => { await window.AstroAgent.apiJson('/api/fs/project/delete', { project: n }); }, name);
  ok('closing the folder keeps the files', fs.existsSync(path.join(F, 'src/bad.js')));
  await page.evaluate(async () => { const api = window.AstroAgent.apiJson; await api('/api/dev/ext/delete', { path: 'snippets/test.json' }); await api('/api/dev/ext/delete', { path: 'themes/test-dark.json' }); localStorage.removeItem('astro_dev:theme'); });
  ok('no page errors', errors.length === 0, errors.join(' | ').slice(0, 600));
  await browser.close();
  fs.rmSync(F, { recursive: true, force: true });
  console.log(fail ? 'Dev UI tests FAILED.' : 'Dev UI tests passed.');
  process.exit(fail);
})().catch((e) => { console.error(e); process.exit(1); });

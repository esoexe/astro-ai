// Screenshot the desktop Agent page for design checks.
//   NODE_PATH=/tmp/pw/node_modules node tests/ag_shot.js <out.png> [--url http://127.0.0.1:4173] [--light] [--w 1568 --h 907]
//                                                       [--project todo-api] [--seed] [--side session|changes|files|terminal|preview] [--narrow]
// --seed injects a realistic finished session (client-side only) so the layout can be judged without running a model.
const { chromium } = require('playwright');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const has = (k) => process.argv.includes(k);
(async () => {
  const out = process.argv[2] || 'ag.png';
  const url = arg('--url', 'http://127.0.0.1:4173');
  const W = Number(arg('--w', has('--narrow') ? 820 : 1568)); const H = Number(arg('--h', 907));
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: W, height: H }, colorScheme: has('--light') ? 'light' : 'dark', deviceScaleFactor: Number(arg('--dsf', 1)) });
  const errs = []; p.on('pageerror', (e) => errs.push(e.message));
  const project = arg('--project', 'todo-api');
  await p.addInitScript((pr) => { try { localStorage.setItem('astro_agent:project', pr); } catch (_e) { /* sandboxed frame */ } }, project);
  await p.goto(url); await p.waitForTimeout(1500); await p.goto(url); await p.waitForLoadState('networkidle').catch(() => {}); await p.waitForTimeout(1000);
  await p.evaluate(async ({ seed, side }) => {
    const A = window.AstroAgent; A.switchView('agent'); await new Promise((r) => setTimeout(r, 900));
    if (seed) {
      document.querySelector('#ag-new')?.click(); await new Promise((r) => setTimeout(r, 200));
      const s = A.agCurSession();
      s.title = 'Add a /health endpoint with a test'; s.status = 'done'; s.steps = 7; s.tools = 6; s.ms = 41300; s.files = 2; s.mode = 'ask';
      s.convo = [{ role: 'user', content: 'x'.repeat(5200) }, { role: 'assistant', content: 'y'.repeat(9800) }];
      const P = (it) => A.agPush(s, it);
      P({ kind: 'user', text: 'Add a GET /health endpoint that returns {"ok":true}, with a test for it. Run the tests.' });
      P({ kind: 'tool', tool: 'read_file', args: { path: 'src/server.js' }, status: 'ok', meta: {}, out: '' });
      P({ kind: 'tool', tool: 'read_file', args: { path: 'test/todos.test.js' }, status: 'ok', meta: {}, out: '' });
      P({ kind: 'tool', tool: 'edit_file', args: { path: 'src/server.js' }, status: 'ok', meta: { added: 1, removed: 0 }, out: "  app.use('/todos', todos);\n+ app.get('/health', (req, res) => res.json({ ok: true }));" });
      P({ kind: 'tool', tool: 'write_file', args: { path: 'test/health.test.js' }, status: 'ok', meta: { created: true, added: 14, removed: 0 }, out: "import test from 'node:test';" });
      P({ kind: 'tool', tool: 'bash', args: { command: 'npm test' }, status: 'ok', meta: { code: 0 }, out: '# pass 2\n# fail 0' });
      P({ kind: 'done', text: 'Added **GET /health** in `src/server.js`; it returns `{"ok":true}`.\n\nNew test `test/health.test.js` checks the status code and the body.', files: 2, steps: 7, ms: 41300, model: 'devstral-medium (xKiro)', check: 'pass', checkCmd: 'npm test', mode: 'ask', team: false, at: Date.now() });
      A.agSave?.(s, true);
    }
    if (side) document.querySelector(`[data-agtab="${side}"], [data-agrail="${side}"]`)?.click();
  }, { seed: has('--seed'), side: arg('--side', '') });
  await p.waitForTimeout(1500);
  await p.screenshot({ path: out });
  if (errs.length) console.log('page errors:', errs);
  await b.close();
})();

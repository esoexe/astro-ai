// Drives web/exthost-node.js over stdio with the fixture extension (no engine, no network).
const { spawn } = require('child_process');
const fs = require('fs'); const os = require('os'); const path = require('path');
const assert = (c, m) => { if (!c) { console.error('FAIL:', m); process.exitCode = 1; } else console.log('ok -', m); };
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'astro-exthost-'));
const exts = path.join(root, 'ext'); const proj = path.join(root, 'proj');
fs.mkdirSync(proj, { recursive: true }); fs.cpSync(path.join(__dirname, 'fixtures', 'vscode-hello'), path.join(exts, 'astro-test.hello'), { recursive: true });
fs.writeFileSync(path.join(proj, 'notes.txt'), 'hello world   \nTODO fix\n');
const host = spawn(process.execPath, [path.join(__dirname, '..', 'web', 'exthost-node.js'), '--extensions', exts, '--storage', path.join(root, 'st')], { cwd: proj, stdio: ['pipe', 'pipe', 'inherit'] });
let buf = Buffer.alloc(0); const pending = new Map(); const notes = []; let nextId = 1;
const send = (m) => { const b = Buffer.from(JSON.stringify(m)); host.stdin.write(`Content-Length: ${b.length}\r\n\r\n`); host.stdin.write(b); };
const answers = { 'ui/message': 0, 'ui/input': 'Ada', 'ui/quickPick': 1, 'editor/apply': true };
host.stdout.on('data', (d) => {
  buf = Buffer.concat([buf, d]);
  for (;;) {
    const he = buf.indexOf('\r\n\r\n'); if (he < 0) break; const len = Number(/content-length:\s*(\d+)/i.exec(buf.slice(0, he).toString())[1]);
    if (buf.length < he + 4 + len) break; const m = JSON.parse(buf.slice(he + 4, he + 4 + len).toString()); buf = buf.slice(he + 4 + len);
    if (m.id != null && !m.method) { const p = pending.get(m.id); pending.delete(m.id); p && p(m); }
    else if (m.id != null) { notes.push(m); send({ jsonrpc: '2.0', id: m.id, result: answers[m.method] ?? null }); }
    else notes.push(m);
  }
});
const req = (method, params) => new Promise((r) => { const id = nextId++; pending.set(id, r); send({ jsonrpc: '2.0', id, method, params }); });
const wait = (pred, ms = 5000) => new Promise((res) => { const t0 = Date.now(); const iv = setInterval(() => { const n = notes.find(pred); if (n || Date.now() - t0 > ms) { clearInterval(iv); res(n); } }, 20); });
(async () => {
  const uri = require('url').pathToFileURL(path.join(proj, 'notes.txt')).href;
  const init = await req('initialize', { settings: { 'hello.greeting': 'Howdy' } });
  assert(init.result && init.result.extensions.length === 1, 'initialize lists the extension');
  const ready = await wait((n) => n.method === 'ready');
  assert(ready && ready.params.extensions[0].activated, 'onStartupFinished activation');
  assert(ready.params.commands.some((c) => c.command === 'hello.say' && c.registered), 'contributed command registered');
  assert(notes.some((n) => n.method === 'output' && /activated in proj/.test(n.params.text)), 'output channel');
  assert(notes.some((n) => n.method === 'status' && n.params.text === '$(smiley) Hello' && n.params.visible), 'status bar item');
  assert(notes.some((n) => n.method === 'unsupported'), 'unsupported API is stubbed, not fatal');
  send({ jsonrpc: '2.0', method: 'doc/open', params: { uri, languageId: 'plaintext', version: 1, text: fs.readFileSync(path.join(proj, 'notes.txt'), 'utf8') } });
  const dg = await wait((n) => n.method === 'diagnostics' && n.params.items.length);
  assert(dg && dg.params.items[0].message === 'TODO found' && dg.params.items[0].range.start.line === 1, 'diagnostics collection');
  const say = await req('execute', { command: 'hello.say' });
  assert(say.result === 'Nice' && notes.some((n) => n.method === 'ui/message' && n.params.text === 'Howdy from an extension!'), 'command + configuration + message with items');
  send({ jsonrpc: '2.0', method: 'editor', params: { uri, selections: [{ anchor: { line: 0, character: 0 }, active: { line: 0, character: 5 } }] } });
  await new Promise((r) => setTimeout(r, 50));
  const up = await req('execute', { command: 'hello.upper' });
  const ap = notes.find((n) => n.method === 'editor/apply');
  assert(up.result === 'HELLO' && ap && ap.params.edits[0].edits[0].newText === 'HELLO', 'activeTextEditor.edit -> editor/apply');
  const ask = await req('execute', { command: 'hello.ask' });
  assert(ask.result === 'Ada:Rust:Ada', 'input box, quick pick and globalState');
  const comp = await req('complete', { uri, line: 0, character: 2 });
  assert(comp.result.items.length === 2 && comp.result.items[1].isSnippet && comp.result.items[0].kind === 14, 'completion provider (kinds mapped to LSP)');
  const hov = await req('hover', { uri, line: 0, character: 2 });
  assert(/Word: `hello`/.test(hov.result), 'hover provider');
  const fmt = await req('format', { uri });
  assert(fmt.result && fmt.result.length === 1 && fmt.result[0].range.start.character === 11, 'formatting provider');
  const bad = await req('execute', { command: 'nope.nothing' });
  assert(bad.error && /not found/.test(bad.error.message), 'unknown command errors cleanly');
  host.kill(); fs.rmSync(root, { recursive: true, force: true });
  console.log(process.exitCode ? 'exthost test FAILED' : 'exthost test passed');
})();

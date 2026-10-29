// Drives the full-screen terminal app in a real pseudo-terminal (util-linux `script`) and replays
// its output through xterm.js, so tests can read the screen and recordings can render it.
//   node tests/tui_drive.js <steps.json> [--cols 120 --rows 36 --out run.json]
// steps: [{"wait": ms} | {"keys": "text with \\r \\x1b ..."} | {"snap": "name"} | {"until": "regex", "timeout": ms}]
// Needs @xterm/headless (NODE_PATH=/tmp/pw/node_modules). Env is passed through (ASTRO_URL etc.).
const { spawn } = require('child_process');
const fs = require('fs');
const { Terminal } = require('@xterm/headless');

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const steps = JSON.parse(fs.readFileSync(args[0], 'utf8'));
const cols = Number(opt('--cols', 120)), rows = Number(opt('--rows', 36));
const outFile = opt('--out', '');
const cmd = opt('--cmd', './astro');
const cwd = opt('--cwd', process.cwd());

const term = new Terminal({ cols, rows, allowProposedApi: true, scrollback: 0 });
const chunks = []; // [ms, data] for recordings
const t0 = Date.now();
const p = spawn('script', ['-qfec', `stty rows ${rows} cols ${cols}; exec ${cmd}`, '/dev/null'], { cwd, env: { ...process.env, TERM: 'xterm-256color', COLORTERM: 'truecolor' } });
// StringDecoder keeps a multi-byte character that is split across two reads together (d.toString() would turn it into two U+FFFD).
const dec = new (require('string_decoder').StringDecoder)('utf8');
p.stdout.on('data', (d) => { const s = dec.write(d); if (!s) return; chunks.push([Date.now() - t0, s]); term.write(s); });
p.stderr.on('data', (d) => process.stderr.write(d));
let exited = false; p.on('exit', () => { exited = true; });

const screen = () => new Promise((res) => term.write('', () => {
  const b = term.buffer.active; const lines = [];
  for (let i = 0; i < rows; i++) lines.push((b.getLine(b.viewportY + i)?.translateToString(true)) || '');
  res(lines.join('\n'));
}));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const snaps = {}, snapAt = {};
(async () => {
  try {
    for (const s of steps) {
      if (s.wait) await sleep(s.wait);
      if (s.keys != null) {
        if (s.type) { for (const ch of s.keys) { p.stdin.write(ch); await sleep(s.type); } }
        else p.stdin.write(s.keys);
      }
      if (s.until) {
        const re = new RegExp(s.until); const end = Date.now() + (s.timeout || 15000); let ok = false;
        while (Date.now() < end) { if (re.test(await screen())) { ok = true; break; } await sleep(100); }
        if (!ok) { snaps['TIMEOUT:' + s.until] = await screen(); console.error('timeout waiting for ' + s.until); }
      }
      if (s.snap) { await sleep(150); snaps[s.snap] = await screen(); snapAt[s.snap] = Date.now() - t0; }
      if (exited) break;
    }
  } catch (e) { console.error(e); }
  await sleep(300);
  if (!exited) { p.kill('SIGTERM'); await sleep(200); }
  for (const [k, v] of Object.entries(snaps)) { console.log(`===== ${k}`); console.log(v); }
  if (outFile) fs.writeFileSync(outFile, JSON.stringify({ cols, rows, chunks, snaps, snapAt }));
  process.exit(0);
})();

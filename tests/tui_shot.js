// Renders a recorded terminal run (from tui_drive.js --out) with xterm.js in headless Chromium.
//   node tests/tui_shot.js run.json out.png [--at ms] [--scale 2] [--title "astro — todo-api"]
//   node tests/tui_shot.js run.json frames/ --frames 30   (one PNG per frame, for video)
//   add --light to draw a light terminal (white background) for light-theme checks
//   node tests/tui_shot.js run.json clipdir/ --clip [--from ms --to ms --clip-fps 30]   (trailer clip: a JPEG per change + frames.json)
//   --bare: no window title bar, margin or shadow (for pages that frame the screenshot themselves).
// Needs playwright + @xterm/xterm (NODE_PATH=/tmp/pw/node_modules).
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const run = JSON.parse(fs.readFileSync(args[0], 'utf8'));
const out = args[1];
const atArg = opt('--at', '1e12');
const at = /^[0-9.e]+$/.test(atArg) ? Number(atArg) : (run.snapAt || {})[atArg];
const fps = Number(opt('--frames', 0));
const scale = Number(opt('--scale', 2));
const title = opt('--title', 'astro \u2014 ' + path.basename(process.cwd()));
// xterm's canvas renderer draws at the wrong size with deviceScaleFactor > 1, so --scale is applied by scaling
// the font and every padding instead (crisp, and the whole terminal stays in frame). --dom keeps the real DPR.
const CANVAS = !args.includes('--dom');
const S = CANVAS ? scale : 1, DSF = CANVAS ? 1 : scale;
const fontSize = Number(opt('--font', 15)) * S;
const xtermDir = path.dirname(require.resolve('@xterm/xterm/package.json'));
const mono = fs.readFileSync(path.join(__dirname, '../web/vendor/jetbrains-mono.woff2')).toString('base64');

const html = `<!doctype html><meta charset="utf-8"><style>
@font-face{font-family:JBM;src:url(data:font/woff2;base64,${mono}) format('woff2')}
${fs.readFileSync(path.join(xtermDir, 'css/xterm.css'), 'utf8')}
html,body{margin:0;background:transparent}
#win{display:inline-block;margin:calc(var(--s)*28px);border-radius:calc(var(--s)*12px);overflow:hidden;background:#0d0c0e;box-shadow:0 calc(var(--s)*30px) calc(var(--s)*80px) rgba(0,0,0,.55),0 0 0 1px rgba(255,255,255,.07)}
#bar{height:calc(var(--s)*34px);display:flex;align-items:center;gap:calc(var(--s)*8px);padding:0 calc(var(--s)*14px);background:#161416;border-bottom:1px solid rgba(255,255,255,.06);font:calc(var(--s)*12.5px) Inter,system-ui,sans-serif;color:#8f8a86}
#bar i{width:calc(var(--s)*12px);height:calc(var(--s)*12px);border-radius:50%;display:inline-block}
#bar b{flex:1;text-align:center;font-weight:500;margin-right:calc(var(--s)*54px)}
#t{padding:calc(var(--s)*10px) calc(var(--s)*12px) calc(var(--s)*12px)}
.xterm-viewport{overflow:hidden!important}
</style><div id="win"><div id="bar"><i style="background:#ff5f57"></i><i style="background:#febc2e"></i><i style="background:#28c840"></i><b></b></div><div id="t"></div></div>
<script>${fs.readFileSync(path.join(xtermDir, 'lib/xterm.js'), 'utf8')}</script><script>${fs.readFileSync(require.resolve('@xterm/addon-canvas'), 'utf8')}</script>`;

(async () => {
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({ deviceScaleFactor: DSF, viewport: { width: Math.ceil(run.cols * fontSize * 0.62 + 160 * S), height: Math.ceil(run.rows * fontSize * 1.3 + 200 * S) } });
    await page.setContent(html.replace('<div id="win">', (args.includes('--dom') ? '<script>window.NO_CANVAS=1</script>' : '') + (args.includes('--bare') ? '<style>#win{margin:0;border-radius:0;box-shadow:none}#bar{display:none}#t{padding:calc(var(--s)*14px) calc(var(--s)*16px)}</style>' : '') + '<style>:root{--s:' + S + '}</style><div id="win">'));
    await page.evaluate(async ({ cols, rows, title, fontSize, LIGHT }) => {
      document.querySelector('#bar b').textContent = title;
      if (LIGHT) { document.getElementById('win').style.background = '#ffffff'; const bar = document.getElementById('bar'); bar.style.background = '#f1f1f1'; bar.style.color = '#57606a'; bar.style.borderBottomColor = 'rgba(0,0,0,.08)'; }
      await document.fonts.load(`${fontSize}px JBM`);
      window.term = new Terminal({ cols, rows, fontFamily: 'JBM, DejaVu Sans Mono, monospace', fontSize, lineHeight: 1.0, customGlyphs: true, allowProposedApi: true,
        theme: LIGHT ? { background: '#ffffff', foreground: '#24292f', cursor: '#0969da', cursorAccent: '#ffffff', selectionBackground: '#cfe3ff' } : { background: '#0d0c0e', foreground: '#e7e5e4', cursor: '#f3a679', cursorAccent: '#0d0c0e', selectionBackground: '#3a2a20' }, cursorStyle: 'bar', cursorBlink: false });
      term.open(document.getElementById('t')); if (!window.NO_CANVAS) term.loadAddon(new CanvasAddon.CanvasAddon());
      window.feed = (s) => new Promise((r) => term.write(s, r));
    }, { cols: run.cols, rows: run.rows, title, fontSize, LIGHT: args.includes('--light') });   // --light: a light terminal (white background)
    const win = await page.$('#win');
    if (args.includes('--clip')) {
      // trailer clip: a JPEG each time the screen changes (at most fps per second) + frames.json {frames:[{file,t}]} in seconds
      fs.mkdirSync(out, { recursive: true });
      const fps2 = Number(opt('--clip-fps', 30)), from = Number(opt('--from', 0)), to = Number(opt('--to', 1e12));
      const paint = () => page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));
      const frames = []; let ci = 0, n = 0, buf = '';
      while (ci < run.chunks.length && run.chunks[ci][0] <= from) buf += run.chunks[ci++][1];
      const shot = async (tm) => { if (buf) await page.evaluate((x) => feed(x), buf); buf = ''; await paint(); const file = String(n++).padStart(5, '0') + '.jpg'; await win.screenshot({ path: path.join(out, file), type: 'jpeg', quality: 92 }); frames.push({ file, t: tm / 1000 }); };
      await shot(from);
      while (ci < run.chunks.length && run.chunks[ci][0] <= to) {
        const t0 = run.chunks[ci][0];
        while (ci < run.chunks.length && run.chunks[ci][0] < t0 + 1000 / fps2 && run.chunks[ci][0] <= to) buf += run.chunks[ci++][1];
        await shot(t0);
      }
      fs.writeFileSync(path.join(out, 'frames.json'), JSON.stringify({ cols: run.cols, rows: run.rows, frames }));
      console.log(`wrote ${n} changed frames to ${out}`);
    } else if (!fps) {
      for (const [t, s] of run.chunks) { if (t > at) break; await page.evaluate((x) => feed(x), s); }
      await page.waitForTimeout(Number(opt('--settle', 400)));
      await win.screenshot({ path: out, omitBackground: true });
      console.log('wrote ' + out);
    } else {
      fs.mkdirSync(out, { recursive: true });
      const end = run.chunks.length ? run.chunks[run.chunks.length - 1][0] + 500 : 0;
      let ci = 0, n = 0;
      for (let t = 0; t <= end; t += 1000 / fps) {
        let buf = '';
        while (ci < run.chunks.length && run.chunks[ci][0] <= t) buf += run.chunks[ci++][1];
        if (buf) await page.evaluate((x) => feed(x), buf);
        await win.screenshot({ path: path.join(out, String(n++).padStart(5, '0') + '.png'), omitBackground: false });
      }
      console.log(`wrote ${n} frames to ${out}`);
    }
  } finally { await browser.close(); }
})().catch((e) => { console.error(e); process.exit(1); });

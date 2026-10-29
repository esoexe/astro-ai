#!/usr/bin/env node
// Turns REAL `astro` terminal recordings (util-linux `script -T`, see recterm.sh) into the website's
// terminal player data: website/src/static/assets/js/term-sessions.js -> window.ASTRO_TERM_SESSIONS.
// Nothing is re-typed or invented: the output bytes are the recorded ones. Long waits are shortened
// and spinner-only redraws are thinned out so a 3-minute run replays in about half a minute.
'use strict';
const fs = require('fs');
const path = require('path');
const { Screen } = require('../../website/src/static/assets/js/term.js');

const REC = path.join(__dirname, 'rec');
const OUT = path.join(__dirname, '..', '..', 'website', 'src', 'static', 'assets', 'js', 'term-sessions.js');

// id -> how it is shown. target = roughly how long the replay should take (ms, after the typed command).
const SESSIONS = [
  { id: 'one', tab: 'One task', title: 'astro -p — one task, start to finish',
    cmd: 'astro -p "Add a GET /health endpoint that returns {\\"ok\\":true}, with a test for it. Run the tests."', target: 9000 },
  { id: 'par', tab: 'Parallel agents', title: 'astro parallel — three agents, three isolated copies',
    cmd: 'astro parallel "Add PATCH /todos/:id that toggles done, plus a test" "Write a README section with curl examples for every endpoint" "Add a CONTRIBUTING.md explaining how to run and test the project" --merge', target: 26000 },
  { id: 'agents', tab: 'Agents', title: 'astro agents — every session, live or finished', cmd: 'astro agents', target: 600 },
  { id: 'models', tab: 'Models', title: 'astro models — best free coding models first', cmd: 'astro models', target: 600 },
];
const SPIN = /[⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏]/g;

function load(id) {
  const log = fs.readFileSync(path.join(REC, id + '.log'));
  const tim = fs.readFileSync(path.join(REC, id + '.tim'), 'utf8').trim().split('\n').map((l) => l.trim().split(/\s+/).map(Number));
  let off = log.indexOf(10) + 1, t = 0;
  const frames = [];
  for (const [d, n] of tim) { t += d * 1000; frames.push({ t, b: log.subarray(off, off + n) }); off += n; }
  // group bytes that arrived together (one redraw is often split over several writes), decode once per group
  const ticks = [];
  for (const f of frames) {
    const last = ticks[ticks.length - 1];
    if (last && f.t - last.t < 30) last.parts.push(f.b);
    else ticks.push({ t: f.t, parts: [f.b] });
  }
  let carry = Buffer.alloc(0);
  for (const k of ticks) {
    let buf = Buffer.concat([carry, ...k.parts]);
    // keep a split UTF-8 sequence for the next tick
    let cut = buf.length;
    for (let i = Math.max(0, buf.length - 3); i < buf.length; i++) {
      const c = buf[i];
      if (c >= 0xc0) { const need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2; if (i + need > buf.length) cut = i; }
    }
    carry = buf.subarray(cut); buf = buf.subarray(0, cut);
    k.text = buf.toString('utf8').replace(/\r?\nScript done on [^\n]*\n?$/, '');
    delete k.parts;
  }
  return ticks.filter((k) => k.text.length);
}

// A redraw that rewrites the same N lines it moves up over, and nothing else.
function redrawRows(s) {
  const m = /^\x1b\[(\d+)A/.exec(s);
  if (!m) return 0;
  const n = Number(m[1]);
  return (s.match(/\r\n/g) || []).length === n ? n : 0;
}

function build(cfg) {
  const ticks = load(cfg.id);
  const scr = new Screen(96);
  let prevSig = '';
  // pass 1: mark interesting ticks (the screen changed beyond the spinner glyphs)
  for (const k of ticks) {
    scr.write(k.text);
    const sig = scr.text().replace(SPIN, '*');
    k.interesting = sig !== prevSig;
    prevSig = sig;
  }
  // pass 2: drop spinner-only redraws that the next redraw fully replaces; keep a few for life
  const kept = [];
  let sinceKeep = 0;
  for (let i = 0; i < ticks.length; i++) {
    const k = ticks[i], next = ticks[i + 1];
    const rows = redrawRows(k.text);
    const replaceable = !k.interesting && rows && next && redrawRows(next.text) === rows;
    if (replaceable && sinceKeep < 3) { sinceKeep++; continue; }   // invisible except for the spinner
    if (!k.interesting) sinceKeep = 0;
    kept.push(k);
  }
  // pass 3: timing. Interesting changes keep (shortened) real gaps; spinner frames get a steady beat.
  let raw = 0;
  const gaps = kept.map((k, i) => {
    const dt = i ? k.t - kept[i - 1].t : 0;
    const g = k.interesting ? Math.min(Math.max(dt, 60), 1600) : Math.min(dt, 400);
    raw += g;
    return g;
  });
  const scale = Math.min(1, cfg.target / Math.max(1, raw));
  const events = kept.map((k, i) => [Math.max(k.interesting ? 70 : 90, Math.round(gaps[i] * scale)), k.text]);
  if (events.length) events[0][0] = 350;
  // merge events closer than 60 ms (one paint)
  const merged = [];
  for (const e of events) {
    const last = merged[merged.length - 1];
    if (last && e[0] < 60) last[1] += e[1]; else merged.push(e);
  }
  const total = merged.reduce((a, e) => a + e[0], 0);
  return { session: { tab: cfg.tab, title: cfg.title, cmd: cfg.cmd, prompt: '~/todo-api $ ', cols: 96, cap: 2000, speed: 1, events: merged }, total, n: merged.length };
}

const out = {};
for (const cfg of SESSIONS) {
  if (!fs.existsSync(path.join(REC, cfg.id + '.log'))) { console.error('missing recording', cfg.id); process.exit(1); }
  const { session, total, n } = build(cfg);
  out[cfg.id] = session;
  // sanity: the final screen equals the recording's final screen
  const a = new Screen(96), b = new Screen(96);
  session.events.forEach((e) => a.write(e[1]));
  load(cfg.id).forEach((k) => b.write(k.text));
  if (a.text() !== b.text()) { console.error('final screen differs for', cfg.id); process.exit(1); }
  console.log(`${cfg.id.padEnd(7)} ${String(n).padStart(4)} frames  ${(total / 1000).toFixed(1)} s replay`);
}
const js = '/* Generated by tools/term-demo/mksessions.js from real recordings of the astro terminal app. Do not edit. */\n' +
  'window.ASTRO_TERM_SESSIONS = ' + JSON.stringify(out) + ';\n';
fs.writeFileSync(OUT, js);
console.log('wrote', path.relative(process.cwd(), OUT), (js.length / 1024).toFixed(1) + ' KB');

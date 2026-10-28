#!/usr/bin/env node
// Astro AI coding benchmark runner.
//   node bench/run.js --self-test                       prove every check fails on the start files and passes on the reference
//   node bench/run.js [--models a,b] [--tasks id,id] [--repeat n] [--mode auto] [--review] [--parallel n]
// Runs each task with the terminal app (`astro -p --json`) in a fresh temp folder, then the hidden check.
// Needs a running Astro AI engine (ASTRO_URL or the desktop app) and the `astro` binary (ASTRO_BIN, default ./astro).
// Results: bench/results/<stamp>.json and bench/results/latest.md
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(`--${name}`); return i >= 0 ? (args[i + 1] && !args[i + 1].startsWith('--') ? args[i + 1] : true) : def; };
const { tasks } = JSON.parse(fs.readFileSync(path.join(__dirname, 'tasks.json'), 'utf8'));
const pick = String(opt('tasks', '')).split(',').filter(Boolean);
const selected = pick.length ? tasks.filter((t) => pick.includes(t.id)) : tasks;
const ASTRO = process.env.ASTRO_BIN || path.join(ROOT, process.platform === 'win32' ? 'astro.exe' : 'astro');

function writeFiles(dir, files) {
  for (const [rel, content] of Object.entries(files || {})) {
    const p = path.join(dir, rel); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, content);
  }
}
function check(task, dir) {
  writeFiles(dir, task.check_files);
  const r = cp.spawnSync('bash', ['-c', task.check], { cwd: dir, encoding: 'utf8', timeout: 60000 });
  return { pass: r.status === 0, out: `${r.stdout || ''}${r.stderr || ''}`.slice(-800) };
}
function tmpdir(id) { return fs.mkdtempSync(path.join(os.tmpdir(), `astro-bench-${id}-`)); }

if (opt('self-test', false)) {
  const sol = JSON.parse(fs.readFileSync(path.join(__dirname, 'solutions.json'), 'utf8'));
  let bad = 0;
  for (const t of selected) {
    const a = tmpdir(t.id); writeFiles(a, t.files); const before = check(t, a);
    const b = tmpdir(t.id); writeFiles(b, t.files); writeFiles(b, sol[t.id]); const after = check(t, b);
    const ok = !before.pass && after.pass;
    if (!ok) bad++;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${t.id}${before.pass ? '  (passes before any change)' : ''}${after.pass ? '' : `  (reference fails: ${after.out.trim().split('\n').slice(-2).join(' ')})`}`);
    fs.rmSync(a, { recursive: true, force: true }); fs.rmSync(b, { recursive: true, force: true });
  }
  console.log(`bench self-test: ${selected.length - bad}/${selected.length} tasks valid`);
  process.exit(bad ? 1 : 0);
}

const models = String(opt('models', 'auto')).split(',').filter(Boolean);
const repeat = Math.max(1, parseInt(opt('repeat', '1'), 10) || 1);
const mode = String(opt('mode', 'auto'));
const review = !!opt('review', false);
const parallel = Math.max(1, Math.min(8, parseInt(opt('parallel', '2'), 10) || 2));

function runOne(task, model) {
  return new Promise((resolve) => {
    const dir = tmpdir(task.id); writeFiles(dir, task.files);
    const a = ['-C', dir, '-p', '--json', '--mode', mode];
    if (model !== 'auto') a.push('--model', model);
    if (review) a.push('--review');
    a.push(task.prompt);
    const t0 = Date.now();
    const child = cp.spawn(ASTRO, a, { cwd: dir, env: { ...process.env, NO_COLOR: '1' } });
    let out = '';
    child.stdout.on('data', (d) => { out += d; });
    child.stderr.on('data', () => {});
    const killer = setTimeout(() => child.kill('SIGINT'), 15 * 60 * 1000);
    child.on('close', (code) => {
      clearTimeout(killer);
      const events = out.split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l); } catch (_e) { return null; } }).filter(Boolean);
      const done = events.filter((e) => e.type === 'done').pop();
      const tools = events.filter((e) => e.type === 'tool_done').length;
      const errors = events.filter((e) => e.type === 'tool_done' && !e.ok).length;
      const models = [...new Set([...events.filter((e) => e.type === 'say' && e.model).map((e) => e.model), ...(done && done.model ? [done.model] : [])])];
      const res = check(task, dir);
      fs.rmSync(dir, { recursive: true, force: true });
      resolve({ task: task.id, lang: task.lang, model, exit: code, pass: res.pass, check_out: res.pass ? '' : res.out, steps: done ? done.steps : null, tools, tool_errors: errors, ms: Date.now() - t0, models_used: models });
    });
  });
}

(async () => {
  if (!fs.existsSync(ASTRO)) { console.error(`astro not found at ${ASTRO} (build it with: make astro, or set ASTRO_BIN)`); process.exit(1); }
  const jobs = [];
  for (const m of models) for (const t of selected) for (let r = 0; r < repeat; r++) jobs.push([t, m]);
  const results = []; let next = 0;
  const worker = async () => {
    while (next < jobs.length) {
      const [t, m] = jobs[next++];
      const r = await runOne(t, m);
      results.push(r);
      console.log(`${r.pass ? 'PASS' : 'fail'}  ${t.id.padEnd(24)} ${m.padEnd(18)} ${(r.ms / 1000).toFixed(1).padStart(6)}s  steps ${r.steps ?? '-'}  ${r.models_used.join(', ')}`);
    }
  };
  await Promise.all(Array.from({ length: Math.min(parallel, jobs.length) }, worker));
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const dir = path.join(__dirname, 'results'); fs.mkdirSync(dir, { recursive: true });
  const meta = { date: new Date().toISOString(), mode, review, repeat, tasks: selected.length, astro: cp.spawnSync(ASTRO, ['--version'], { encoding: 'utf8' }).stdout.trim() };
  fs.writeFileSync(path.join(dir, `${stamp}.json`), JSON.stringify({ meta, results }, null, 2));
  let md = `# Astro AI coding benchmark\n\n${meta.date.slice(0, 16).replace('T', ' ')} UTC · ${meta.astro} · mode ${mode}${review ? ' + review' : ''} · ${selected.length} tasks × ${repeat}\n\n| Model | Passed | Rate | Avg time | Avg steps |\n|---|---|---|---|---|\n`;
  for (const m of models) {
    const rs = results.filter((r) => r.model === m); const p = rs.filter((r) => r.pass).length;
    const avg = (f) => (rs.length ? rs.reduce((s, r) => s + (f(r) || 0), 0) / rs.length : 0);
    md += `| ${m} | ${p}/${rs.length} | ${rs.length ? Math.round((100 * p) / rs.length) : 0}% | ${(avg((r) => r.ms) / 1000).toFixed(1)}s | ${avg((r) => r.steps).toFixed(1)} |\n`;
  }
  md += `\n| Task | ${models.join(' | ')} |\n|---|${models.map(() => '---').join('|')}|\n`;
  for (const t of selected) md += `| ${t.id} | ${models.map((m) => { const rs = results.filter((r) => r.task === t.id && r.model === m); return `${rs.filter((r) => r.pass).length}/${rs.length}`; }).join(' | ')} |\n`;
  md += '\nThe checks are hidden from the agent and run after it finishes. Free models change often and have rate limits, so results vary between runs. A small set like this one shows trends; it is not a leaderboard.\n';
  fs.writeFileSync(path.join(dir, 'latest.md'), md);
  console.log(`\n${md}`);
  console.log(`saved bench/results/${stamp}.json and bench/results/latest.md`);
})();

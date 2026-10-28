#!/usr/bin/env node
// A.S.T.R.O AI coding benchmark: asks real free models each task the OLD way and the NEW way, runs the code
// against hidden tests and prints the pass rate. Needs a running A.S.T.R.O AI server (ASTRO_AUTH=off).
//   ASTRO_URL=http://127.0.0.1:4192 MODELS=a,b,c node tests/coding_bench/bench.js
// OLD = general prompt, temperature 0.65, 1400-token cap (how a chat code question ran before 1.1.1).
// NEW = coding standard, low temperature, 5000-token cap (payload.code = true).
const fs = require('fs');
const os = require('os');
const path = require('path');
const TASKS = require('./tasks');

const URL_BASE = process.env.ASTRO_URL || 'http://127.0.0.1:4192';
const MODELS = (process.env.MODELS || '').split(',').filter(Boolean);
const REPEAT = Number(process.env.REPEAT || 1);
const ONLY = process.env.TASKS ? process.env.TASKS.split(',') : null;
const OUT = process.env.OUT || path.join(os.tmpdir(), 'astro-bench.json');
const CONDS = (process.env.CONDS || 'old,new').split(',');
if (!MODELS.length) { console.error('Set MODELS=provider:model,...'); process.exit(2); }

async function ask(model, prompt, cond) {
  const body = { messages: [{ role: 'user', content: prompt }], mode: 'general', language: 'auto', stage: 'expert', strict: true, model,
    max_tokens: cond === 'new' ? 5000 : 1400, code: cond === 'new' };
  const ctrl = new AbortController(); const timer = setTimeout(() => ctrl.abort(), 240000);
  const t0 = Date.now();
  try {
    const res = await fetch(`${URL_BASE}/api/chat/stream`, { method: 'POST', signal: ctrl.signal,
      headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1', Accept: 'text/event-stream' }, body: JSON.stringify(body) });
    const raw = await res.text();
    let text = ''; let error = '';
    for (const line of raw.split('\n')) {
      if (!line.startsWith('data:')) continue;
      try { const ev = JSON.parse(line.slice(5)); if (typeof ev.delta === 'string') text += ev.delta; if (ev.error) error = String(ev.error); } catch (_e) { /* skip */ }
    }
    return { text, error: text ? '' : (error || `HTTP ${res.status}`), ms: Date.now() - t0 };
  } catch (e) { return { text: '', error: e.name === 'AbortError' ? 'timeout' : e.message, ms: Date.now() - t0 }; }
  finally { clearTimeout(timer); }
}

const { grade } = require('./grade');

(async () => {
  const tasks = TASKS.filter((t) => !ONLY || ONLY.includes(t.id));
  const results = fs.existsSync(OUT) && process.env.RESUME ? JSON.parse(fs.readFileSync(OUT, 'utf8')) : [];
  const done = new Set(results.map((r) => `${r.model}|${r.task}|${r.cond}|${r.rep}`));
  // One worker per model (each provider has its own rate limit); tasks run one after another per model.
  await Promise.all(MODELS.map(async (model) => {
    for (let rep = 0; rep < REPEAT; rep++) for (const task of tasks) for (const cond of CONDS) {
      const key = `${model}|${task.id}|${cond}|${rep}`; if (done.has(key)) continue;
      let a = await ask(model, task.prompt, cond);
      // Free tiers rate-limit bursts: wait and retry (up to 4 times) instead of dropping the sample.
      for (let i = 0; i < 4 && a.error && /429|rate.?limit|busy|overloaded|timeout|temporar/i.test(a.error) && !/while thinking/.test(a.error); i++) {
        await new Promise((r) => setTimeout(r, 25000 * (i + 1))); a = await ask(model, task.prompt, cond);
      }
      // Spending the whole budget on thinking means the user got no code: that is a failure, not a provider error.
      const g = a.error && /while thinking/.test(a.error) ? { pass: false, why: 'no answer: ran out of tokens while thinking' }
        : a.error ? { pass: null, why: 'model error: ' + a.error.slice(0, 120) } : grade(task, a.text);
      const row = { model, task: task.id, cond, rep, pass: g.pass, why: g.why, ms: a.ms, chars: a.text.length, text: a.text.slice(0, 12000) };
      results.push(row); fs.writeFileSync(OUT, JSON.stringify(results, null, 1));
      console.log(`${g.pass === null ? 'ERR ' : g.pass ? 'PASS' : 'FAIL'} ${cond} ${task.id.padEnd(13)} ${model.slice(0, 40).padEnd(40)} ${(a.ms / 1000).toFixed(0)}s ${g.why}`);
    }
  }));
  summary(results);
})();

function summary(results) {
  const graded = results.filter((r) => r.pass !== null);
  // Only compare pairs where both conditions got an answer, so provider errors do not skew the result.
  const both = graded.filter((r) => graded.some((o) => o.model === r.model && o.task === r.task && o.rep === r.rep && o.cond !== r.cond));
  const rate = (rows) => rows.length ? `${rows.filter((r) => r.pass).length}/${rows.length} (${Math.round(100 * rows.filter((r) => r.pass).length / rows.length)}%)` : '-';
  console.log('\n=== Summary (pairs where both answered) ===');
  for (const cond of CONDS) console.log(`${cond.toUpperCase().padEnd(4)} ${rate(both.filter((r) => r.cond === cond))}`);
  console.log('\nPer model:');
  for (const m of [...new Set(both.map((r) => r.model))]) console.log(`  ${m.padEnd(44)} ${CONDS.map((c) => `${c} ${rate(both.filter((r) => r.model === m && r.cond === c))}`).join('   ')}`);
  console.log('\nPer task:');
  for (const t of [...new Set(both.map((r) => r.task))]) console.log(`  ${t.padEnd(14)} ${CONDS.map((c) => `${c} ${rate(both.filter((r) => r.task === t && r.cond === c))}`).join('   ')}`);
  console.log(`\nModel errors (not counted): ${results.filter((r) => r.pass === null).length}`);
}

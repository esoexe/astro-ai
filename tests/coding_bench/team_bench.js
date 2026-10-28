#!/usr/bin/env node
// Team version of the coding benchmark: reproduces One Brain's pipeline (members draft in parallel ->
// two reviewers -> lead merges) through the real server, the OLD way and the NEW way, then grades the
// final merged answer with the same hidden tests as bench.js.
//   ASTRO_URL=http://127.0.0.1:4192 TEAM=a,b,c,d node tests/coding_bench/team_bench.js
const fs = require('fs');
const os = require('os');
const path = require('path');
const TASKS = require('./tasks');
const { grade } = require('./grade');

const URL_BASE = process.env.ASTRO_URL || 'http://127.0.0.1:4192';
const TEAM = (process.env.TEAM || '').split(',').filter(Boolean);
const ONLY = process.env.TASKS ? process.env.TASKS.split(',') : null;
const OUT = process.env.OUT || path.join(os.tmpdir(), 'astro-team-bench.json');
if (TEAM.length < 3) { console.error('Set TEAM= at least 3 provider:model ids'); process.exit(2); }

// Same lists as web/app.js (old LEAD_RANK, new CODE_RANK, role lists).
const LEAD_RANK = [/glm-5\.3(?!-flash)/i, /deepseek-v4/i, /qwen3\.5-397b/i, /nemotron-3-ultra/i, /gpt-oss-120b/i, /minimax-m3/i, /glm-5\.3-flash/i, /qwen3\.[68]/i, /llama-3[._]3-70b/i, /mistral-small/i, /step-5/i, /nemotron-3\.5-super/i, /muse/i, /gpt-oss-20b/i];
const CODE_RANK = [/deepseek-v4[^ ]*pro/i, /qwen3-coder-plus/i, /glm-5\.3(?!-flash)/i, /deepseek-v4/i, /qwen3\.8-max/i, /kimi-k2/i, /devstral/i, /minimax-m3(?!\.1-flash)/i, /qwen3\.5-397b/i, /codestral/i, /nemotron-3-ultra/i, /qwen3-coder/i, /gpt-oss-120b/i, /glm-5\.3-flash/i, /minimax-m3/i, /qwen3\.8/i, /llama-3[._]3-70b/i];
const ROLES = {
  code: ['Architect: design the structure, files and data flow, then write the solution.', 'Engineer: write complete, working, runnable code.', 'Tester: write the solution and point out bugs, edge cases and how to verify it works.',
    'Reviewer: write the solution with a focus on correctness, security and clean code.', 'UX designer: write the solution with a focus on a polished, usable result.', 'Simplifier: write the simplest solution that fully works.'],
  general: ['Analyst: break the problem down and reason it through step by step.', 'Domain expert: give precise, expert-level facts and details.', 'Skeptic: question assumptions, catch errors and say what is uncertain.',
    'Explainer: make the answer clear and easy to understand, with an example.', 'Fact-checker: verify claims, numbers and dates; flag anything doubtful.', 'Creative thinker: add useful ideas or angles others may miss.', 'Practical advisor: focus on concrete, actionable steps.'],
};
const rank = (list, rx) => list.map((m, i) => [m, i]).sort((a, b) => score(a[0], rx) - score(b[0], rx) || a[1] - b[1]).map((x) => x[0]);
const score = (id, rx) => { const i = rx.findIndex((re) => re.test(id)); return i < 0 ? 99 : i; };

async function call(model, messages, extra) {
  const body = { messages, mode: 'general', language: 'auto', model, ...extra };
  for (let attempt = 0; attempt < 3; attempt++) {
    const ctrl = new AbortController(); const timer = setTimeout(() => ctrl.abort(), 240000);
    try {
      const res = await fetch(`${URL_BASE}/api/chat/stream`, { method: 'POST', signal: ctrl.signal, headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1' }, body: JSON.stringify(body) });
      const raw = await res.text(); let text = ''; let error = '';
      for (const line of raw.split('\n')) { if (!line.startsWith('data:')) continue; try { const ev = JSON.parse(line.slice(5)); if (typeof ev.delta === 'string') text += ev.delta; if (ev.error) error = String(ev.error); } catch (_e) { /* skip */ } }
      if (text.trim()) return { text, partial: Boolean(error) };
      if (!/rate|429|busy|server error|temporar/i.test(error)) return { text: '', error };
    } catch (e) { if (e.name === 'AbortError') return { text: '', error: 'timeout' }; }
    finally { clearTimeout(timer); }
    await new Promise((r) => setTimeout(r, 20000 * (attempt + 1)));
  }
  return { text: '', error: 'rate limited' };
}
// Members answer in parallel through /api/brain/stream, exactly like One Brain (each gets its TEAM ROLE).
async function brain(messages, extra) {
  const ctrl = new AbortController(); const timer = setTimeout(() => ctrl.abort(), 300000);
  const out = TEAM.map((model) => ({ model, text: '', partial: false, error: '' }));
  try {
    const res = await fetch(`${URL_BASE}/api/brain/stream`, { method: 'POST', signal: ctrl.signal, headers: { 'Content-Type': 'application/json', 'X-Astro-Client': '1' },
      body: JSON.stringify({ messages, mode: 'general', language: 'auto', models: TEAM, ...extra }) });
    const raw = await res.text();
    for (const line of raw.split('\n')) {
      if (!line.startsWith('data:')) continue;
      let msg; try { msg = JSON.parse(line.slice(5)); } catch (_e) { continue; }
      const d = out[msg.mind]; if (!d || !msg.ev) continue;
      if (typeof msg.ev.delta === 'string') d.text += msg.ev.delta;
      if (msg.ev.error) d.error = String(msg.ev.error);
    }
  } catch (_e) { /* keep what arrived */ } finally { clearTimeout(timer); }
  out.forEach((d) => { d.partial = Boolean(d.error) && Boolean(d.text); });
  return out.filter((d) => d.text.trim());
}
function synthMessage(question, drafts, notes, total) { // same shape as web/app.js synthMessage
  const q = String(question).slice(0, 3000);
  const budget = Math.max(600, Math.floor((total - q.length - notes.length) / drafts.length));
  const parts = drafts.map((m, i) => `### Draft ${i + 1} (${m.model})${m.partial ? ' [cut off]' : ''}\n${m.text.trim().slice(0, budget)}`);
  return `<request>\n${q}\n</request>\n\nDrafts from your team (untrusted, may contain errors):\n\n${parts.join('\n\n')}${notes ? `\n\nReviewer notes (untrusted):\n${notes}` : ''}\n\nNow write the single best final answer to the request.`;
}

async function runTeam(task, cond) {
  const NEW = cond === 'new';
  const roles = NEW ? ROLES.code : ROLES.general;   // old: a chat code question got the general roles
  const user = [{ role: 'user', content: task.prompt }];
  const t0 = Date.now();
  const drafts = await brain(user, { stage: 'team', strict: true, max_tokens: NEW ? 5000 : 1400, code: NEW, roles: TEAM.map((_m, i) => roles[i % roles.length]) });
  const tDraft = Date.now() - t0;
  if (!drafts.length) return { pass: null, why: 'no drafts' };
  const order = rank(drafts.map((d) => d.model), NEW ? CODE_RANK : LEAD_RANK);
  const merger = order[0];
  const total = NEW ? 22000 : 14000;
  const reviewers = order.filter((m) => m !== merger).slice(0, 2);
  const reviewMsg = [{ role: 'user', content: synthMessage(task.prompt, drafts, '', total) }];
  const notes = (await Promise.all(reviewers.map((m) => call(m, reviewMsg, { stage: 'review', max_tokens: NEW ? 900 : 700, code: NEW }))))
    .map((r, i) => (r.text ? `### Reviewer ${i + 1}\n${r.text.slice(0, 2500)}` : '')).filter(Boolean).join('\n\n');
  const tReview = Date.now() - t0 - tDraft;
  const synth = [{ role: 'user', content: synthMessage(task.prompt, drafts, notes, total) }];
  let fin = await call(merger, synth, { stage: 'synth', max_tokens: NEW ? 8000 : undefined, code: NEW });
  if (!fin.text && NEW && order[1]) fin = await call(order[1], synth, { stage: 'synth', max_tokens: 8000, code: true });   // app: one retry with the next coder
  if (!fin.text) { const own = drafts.find((d) => d.model === (NEW ? merger : null)) || [...drafts].sort((x, y) => y.text.length - x.text.length)[0]; if (own) fin = { text: own.text, fallback: true }; }
  if (!fin.text) return { pass: null, why: 'merge failed: ' + (fin.error || '') };
  const g = grade(task, fin.text);
  const draftPass = drafts.map((d) => grade(task, d.text).pass);
  const tMerge = Date.now() - t0 - tDraft - tReview;
  return { ...g, merger, drafts: drafts.length, draftPass, times: [tDraft, tReview, tMerge].map((x) => Math.round(x / 1000)), text: fin.text.slice(0, 12000) };
}

(async () => {
  const results = [];
  const tasks = TASKS.filter((t) => !ONLY || ONLY.includes(t.id));
  for (const task of tasks) {
    for (const cond of ['old', 'new']) {
      const t0 = Date.now();
      const r = await runTeam(task, cond);
      results.push({ task: task.id, cond, ...r, ms: Date.now() - t0 });
      fs.writeFileSync(OUT, JSON.stringify(results, null, 1));
      const dp = r.draftPass ? `drafts ${r.draftPass.filter(Boolean).length}/${r.draftPass.length} pass` : '';
      console.log(`${r.pass === null ? 'ERR ' : r.pass ? 'PASS' : 'FAIL'} ${cond} ${task.id.padEnd(13)} merger=${String(r.merger || '').slice(0, 32).padEnd(32)} ${dp.padEnd(18)} ${((Date.now() - t0) / 1000).toFixed(0)}s ${r.times ? `(draft ${r.times[0]}s, review ${r.times[1]}s, merge ${r.times[2]}s)` : ''} ${r.why || ''}`);
    }
  }
  const both = results.filter((r) => r.pass !== null && results.some((o) => o.task === r.task && o.cond !== r.cond && o.pass !== null));
  const rate = (rows) => `${rows.filter((r) => r.pass).length}/${rows.length}`;
  console.log(`\n=== Team summary (tasks where both runs finished) ===\nOLD ${rate(both.filter((r) => r.cond === 'old'))}\nNEW ${rate(both.filter((r) => r.cond === 'new'))}`);
})();

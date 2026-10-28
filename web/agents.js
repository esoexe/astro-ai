// A.S.T.R.O AI background & parallel agents (window.AstroAgents).
// Runs agents on the server engine (/api/agent/*): they keep working when this tab is closed, several run at once,
// and each can work in an isolated copy of the project (a git worktree or a copy) that you review, merge or discard.
// The same sessions are visible in the terminal app: `astro agents`, `astro attach <id>`, `astro merge <id>`.
(() => {
  'use strict';
  const A = () => window.AstroAgent;
  const api = (path, body) => A().apiJson(`/api/agent/${path}`, body || {});
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const icon = (n) => (A() ? A().icon(n) : '');
  const clip = (t, n) => { t = String(t ?? ''); return t.length > n ? `${t.slice(0, n)}…` : t; };
  const ago = (ts) => { const s = Math.max(0, (Date.now() - ts) / 1000); return s < 60 ? 'just now' : s < 3600 ? `${Math.floor(s / 60)}m ago` : s < 86400 ? `${Math.floor(s / 3600)}h ago` : `${Math.floor(s / 86400)}d ago`; };
  const nfiles = (n) => `${n || 0} file${n === 1 ? '' : 's'}`;
  const LIVE = new Set(['running', 'waiting', 'queued']);
  const S = { open: false, view: '', timer: 0, events: [], since: 0, polling: false, list: [] };

  function project() { return A()?.AG?.project || A()?.project || ''; }
  const $ = (sel) => document.querySelector(sel);

  function open(view = '') {
    S.open = true; S.view = view;
    A().showModal(`<h2 id="modal-title">${icon('i-sparkles')} Background &amp; parallel agents</h2>
      <p class="sub">Agents run in the A.S.T.R.O AI engine: they keep working when you close this window, several run at once, and with <b>Isolated copy</b> each works in its own copy of the project (a git worktree or a copy) that you review, then merge or discard. The terminal app shows the same agents: <code>astro agents</code>.</p>
      <div id="bg-body"></div>
      <div class="btn-row"><button type="button" class="btn ghost" id="modal-close">Close</button></div>`);
    document.getElementById('modal-content')?.classList.add('wide');
    $('#modal-close').onclick = close;
    render();
    clearInterval(S.timer);
    S.timer = setInterval(() => { if (!document.getElementById('bg-body')) { close(); return; } if (!S.view) refreshList(); }, 2500);
  }
  function close() { S.open = false; clearInterval(S.timer); S.view = ''; A().closeModal(); }

  async function refreshList() {
    const p = project();
    try { const r = await api('list', p ? { project: p } : {}); S.list = r.sessions || []; S.slots = `${r.running}/${r.max}`; } catch (_e) { S.list = []; }
    if (!S.view) paintList();
  }
  function render() { if (S.view) renderDetail(S.view); else { paintList(); refreshList(); } }

  function paintList() {
    const body = document.getElementById('bg-body'); if (!body) return;
    const p = project();
    const rows = S.list.map((s) => {
      const st = esc(s.status);
      const where = s.isolated ? `isolated copy of <b>${esc(s.base)}</b>${s.merged ? ' · merged' : ''}${s.discarded ? ' · discarded' : ''}` : esc(s.project);
      return `<li class="bg-row" data-id="${esc(s.id)}"><span class="bg-dot ${st}"></span><div class="bg-main"><b>${esc(clip(s.title, 80))}</b><small>${st} · ${where} · ${s.steps || 0} steps · ${nfiles(s.files)} · ${ago(s.updated)} · <code>${esc(s.id)}</code></small></div>
        <div class="bg-acts">${LIVE.has(s.status) ? `<button type="button" class="btn small ghost" data-act="stop">Stop</button>` : ''}<button type="button" class="btn small" data-act="view">Open</button></div></li>`;
    }).join('');
    body.innerHTML = `<form class="bg-new" id="bg-new">
        <textarea class="input" id="bg-tasks" rows="3" placeholder="${p ? `Tasks for ${esc(p)} — one per line; each line becomes its own agent` : 'Open a project in the Agent page first'}"${p ? '' : ' disabled'}></textarea>
        <div class="bg-opts">
          <label><input type="checkbox" id="bg-iso" checked> Isolated copy</label>
          <label><input type="checkbox" id="bg-review"> Second-model review</label>
          <label>Mode <select class="input small" id="bg-mode"><option value="auto">Auto</option><option value="plan">Plan (read-only)</option><option value="ask">Ask (approve here)</option></select></label>
          <span class="grow"></span><span class="hint">${S.slots ? `${esc(S.slots)} running` : ''}</span>
          <button type="submit" class="btn primary small"${p ? '' : ' disabled'}>${icon('i-send')}Start agents</button>
        </div></form>
      ${rows ? `<ul class="bg-list">${rows}</ul>` : '<p class="hint">No background agents yet.</p>'}`;
    $('#bg-new').onsubmit = async (ev) => {
      ev.preventDefault();
      const tasks = $('#bg-tasks').value.split('\n').map((t) => t.trim()).filter(Boolean);
      if (!tasks.length) return;
      const btn = ev.submitter || ev.target.querySelector('[type=submit]'); btn.disabled = true;
      const opts = { project: p, isolate: $('#bg-iso').checked, review: $('#bg-review').checked, mode: $('#bg-mode').value };
      let started = 0;
      for (const prompt of tasks) {
        try { await api('start', { ...opts, prompt }); started++; } catch (err) { A().toast(err.message || String(err)); }
      }
      A().toast(`${started} agent${started === 1 ? '' : 's'} started`);
      refreshList();
    };
    body.querySelectorAll('.bg-row').forEach((row) => {
      row.querySelector('[data-act="view"]').onclick = () => { S.view = row.dataset.id; renderDetail(row.dataset.id); };
      const stop = row.querySelector('[data-act="stop"]'); if (stop) stop.onclick = async () => { await api('stop', { id: row.dataset.id }).catch(() => {}); refreshList(); };
    });
  }

  function evHtml(e) {
    switch (e.type) {
      case 'user': return `<div class="bg-ev user">› ${esc(e.text)}</div>`;
      case 'say': return e.final ? '' : `<div class="bg-ev say">${A().renderMarkdown ? A().renderMarkdown(e.text) : esc(e.text)}</div>`;
      case 'tool_done': return `<div class="bg-ev tool ${e.ok ? 'ok' : e.blocked ? 'blocked' : 'err'}"><span>${e.ok ? '✓' : e.blocked ? '⊘' : '✗'}</span> ${esc(e.label)}${e.added !== undefined ? ` <i class="add">+${e.added}</i> <i class="del">−${e.removed}</i>` : ''}${!e.ok && e.summary ? `<small>${esc(clip(e.summary.replace(/^BLOCKED: /, ''), 240))}</small>` : ''}</div>`;
      case 'note': return `<div class="bg-ev note">· ${esc(e.text)}</div>`;
      case 'answered': return `<div class="bg-ev note">↳ ${esc(e.answer)}</div>`;
      case 'review': return `<div class="bg-ev review"><b>Review: ${esc(e.verdict)}</b>${A().renderMarkdown ? A().renderMarkdown(e.text || '') : esc(e.text)}</div>`;
      case 'todo': return `<div class="bg-ev todo">${(e.items || []).map((i) => `<div>${i.status === 'done' ? '☑' : i.status === 'doing' ? '◐' : '☐'} ${esc(i.text)}</div>`).join('')}</div>`;
      case 'done': return `<div class="bg-ev done"><b>✔ Done</b> · ${nfiles((e.files || []).length)} · ${e.steps} steps${e.text ? `<div>${A().renderMarkdown ? A().renderMarkdown(e.text) : esc(e.text)}</div>` : ''}${(e.files || []).length ? `<ul>${e.files.map((f) => `<li><code>${f.status === 'added' ? '+' : f.status === 'deleted' ? '−' : '~'} ${esc(f.path)}</code></li>`).join('')}</ul>` : ''}</div>`;
      case 'error': return `<div class="bg-ev err">✗ ${esc(e.text)}</div>`;
      default: return '';
    }
  }

  async function renderDetail(id) {
    const body = document.getElementById('bg-body'); if (!body) return;
    body.innerHTML = '<p class="hint">Loading…</p>';
    let s;
    try { s = await api('get', { id }); } catch (err) { body.innerHTML = `<p class="hint">${esc(err.message)}</p>`; return; }
    S.events = s.events || []; S.since = s.seq || 0;
    const pending = new Map();
    const paint = (v) => {
      if (S.view !== id || !document.getElementById('bg-body')) return;
      for (const e of S.events) if (e.type === 'approval' || e.type === 'question') pending.set(e.id, e);
      const wait = v.waiting ? pending.get(v.waiting) : null;
      const live = LIVE.has(v.status);
      body.innerHTML = `<div class="bg-dhead"><button type="button" class="btn small ghost" id="bg-back">← All agents</button><b>${esc(clip(v.title, 70))}</b><span class="bg-dot ${esc(v.status)}"></span><small>${esc(v.status)}${v.isolated ? ` · isolated copy of ${esc(v.base)}` : ''} · <code>${esc(v.id)}</code></small><span class="grow"></span>
          ${live ? '<button type="button" class="btn small ghost" data-d="stop">Stop</button>' : ''}
          ${v.isolated && !v.discarded ? '<button type="button" class="btn small ghost" data-d="diff">Diff</button>' : ''}
          ${v.isolated && !v.discarded && !live ? '<button type="button" class="btn small primary" data-d="merge">Merge</button><button type="button" class="btn small ghost" data-d="discard">Discard</button>' : ''}
          ${(!v.isolated && v.checkpoint) || (v.isolated && v.merged) ? '<button type="button" class="btn small ghost" data-d="undo">Undo</button>' : ''}</div>
        <div class="bg-tl" id="bg-tl">${S.events.map(evHtml).join('')}${live && v.live ? `<div class="bg-ev say live">${esc(clip(v.live, 600))}</div>` : ''}${live && !wait ? '<div class="bg-ev note">working…</div>' : ''}</div>
        ${wait ? `<div class="bg-wait"><b>${esc(wait.text || wait.question)}</b>${wait.preview ? `<pre>${esc(clip(wait.preview, 3000))}</pre>` : ''}
          <div class="btn-row">${wait.type === 'approval' ? ['Allow', 'Allow all in this run', 'Deny'].map((a) => `<button type="button" class="btn small${a === 'Allow' ? ' primary' : ''}" data-ans="${esc(a)}">${esc(a)}</button>`).join('') : (wait.options || []).map((o) => `<button type="button" class="btn small" data-ans="${esc(o)}">${esc(o)}</button>`).join('')}
          <input class="input small" id="bg-ans" placeholder="${wait.type === 'approval' ? 'Or say what to do instead' : 'Your answer'}"><button type="button" class="btn small" id="bg-ans-go">Send</button></div></div>`
          : !live && !v.discarded ? `<form class="bg-follow" id="bg-follow"><input class="input" id="bg-fin" placeholder="Follow-up for this agent…"><button class="btn small primary" type="submit">Send</button></form>` : ''}
        <div id="bg-diff"></div>`;
      const tl = $('#bg-tl'); if (tl) tl.scrollTop = tl.scrollHeight;
      $('#bg-back').onclick = () => { S.view = ''; render(); };
      body.querySelectorAll('[data-ans]').forEach((b) => { b.onclick = () => answer(id, v.waiting, b.dataset.ans); });
      const go = $('#bg-ans-go'); if (go) go.onclick = () => { const t = $('#bg-ans').value.trim(); if (t) answer(id, v.waiting, t); };
      const fol = $('#bg-follow'); if (fol) fol.onsubmit = async (ev) => { ev.preventDefault(); const t = $('#bg-fin').value.trim(); if (!t) return; await api('send', { id, prompt: t }).catch((err) => A().toast(err.message)); poll(); };
      body.querySelectorAll('[data-d]').forEach((b) => { b.onclick = () => action(id, b.dataset.d); });
    };
    let view = s;
    paint(view);
    const poll = async () => {
      if (S.polling) return; S.polling = true;
      try {
        while (S.view === id && document.getElementById('bg-body')) {
          let r;
          try { r = await api('events', { id, since: S.since, wait: 20, live: view.live || '' }); } catch (_e) { await new Promise((ok) => setTimeout(ok, 1500)); continue; }
          if (S.view !== id) break;
          for (const e of r.events || []) { S.events.push(e); S.since = Math.max(S.since, e.seq); }
          view = r; paint(view);
          if (!LIVE.has(r.status) && !r.running && !(r.events || []).length) break;
        }
      } finally { S.polling = false; }
    };
    poll();
  }
  async function answer(id, qid, text) { try { await api('answer', { id, qid, answer: text }); } catch (err) { A().toast(err.message); } }
  async function action(id, what) {
    try {
      if (what === 'stop') await api('stop', { id });
      else if (what === 'discard') { await api('discard', { id }); A().toast('Isolated copy discarded'); S.view = ''; render(); }
      else if (what === 'undo') { const r = await api('undo', { id }); A().toast(`Restored ${r.project}`); }
      else if (what === 'diff') {
        const r = await api('diff', { id });
        const html = String(r.diff || '').split('\n').map((l) => `<span class="${l.startsWith('diff --astro') ? 'h' : l.startsWith('@@') ? 'at' : l.startsWith('+') && !l.startsWith('+++') ? 'add' : l.startsWith('-') && !l.startsWith('---') ? 'del' : ''}">${esc(l.startsWith('diff --astro') ? l.slice(13) : l)}</span>`).join('\n');
        const d = $('#bg-diff'); if (d) d.innerHTML = r.diff ? `<pre class="bg-diffpre">${html}</pre>` : '<p class="hint">No changes.</p>';
      } else if (what === 'merge') {
        let r;
        try { r = await api('merge', { id, discard: true }); }
        catch (err) {
          if (!/changed in the project/.test(err.message)) throw err;
          if (!confirm(`${err.message}\n\nMerge the other files and leave the conflicting ones?`)) return;
          r = await api('merge', { id, partial: true });
        }
        A().toast(`Merged ${(r.merged || []).length} file(s)${(r.conflicts || []).length ? `, ${(r.conflicts || []).length} left` : ''}`);
        try { A().wsLoadTree?.(); } catch (_e) { /* the editor is not open */ }
        S.view = ''; render();
      }
    } catch (err) { A().toast(err.message || String(err)); }
  }

  // A "Background" chip next to the mode chip on the Agent page.
  function addChip() {
    const row = document.querySelector('#ag-form .ag-row');
    if (!row || row.querySelector('[data-bgagents]')) return;
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'ws-chip'; b.dataset.bgagents = '1';
    b.title = 'Background & parallel agents: several agents at once, each in an isolated copy of the project';
    b.innerHTML = `${icon('i-sparkles')}<span>Parallel</span>`;
    b.onclick = () => open();
    const mode = row.querySelector('[data-devmode]');
    if (mode) mode.after(b); else row.prepend(b);
  }
  let queued = false;
  const mo = new MutationObserver(() => { if (queued) return; queued = true; requestAnimationFrame(() => { queued = false; addChip(); }); });
  window.addEventListener('DOMContentLoaded', () => { mo.observe(document.body, { childList: true, subtree: true }); addChip(); });
  window.AstroAgents = { open };
})();

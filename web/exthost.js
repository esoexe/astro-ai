// A.S.T.R.O AI VS Code extension host client (window.AstroExtHost) — preview.
// Runs installed VS Code extensions (Open VSX) in web/exthost-node.js through the engine's /api/dev/lsp/* relay
// (lang "exthost"), and connects them to the app: commands in the palette, messages / quick picks / input boxes,
// output channels, status-bar items, diagnostics, editor edits, and completion / hover / definition / formatting.
(() => {
  'use strict';
  const A = () => window.AstroAgent;
  const api = (path, body) => A().apiJson(`/api/dev/${path}`, body || {});
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const SKEY = 'astro_vscode_settings';
  const X = {
    id: '', root: '', project: '', seq: 0, nextId: 1, pending: new Map(), state: 'off', error: '',
    extensions: [], commands: [], caps: { completion: [], hover: [], definition: [], formatting: [], triggers: [] },
    status: new Map(), progress: new Map(), outputs: new Map(), diags: new Map(), docs: new Map(), unsupported: new Set(),
    listeners: new Set(), installed: null, cm: null, starting: null,
  };
  const emit = () => { for (const f of X.listeners) { try { f(); } catch (_e) { /* listener */ } } };
  const settings = () => { try { return JSON.parse(localStorage.getItem(SKEY) || '{}'); } catch (_e) { return {}; } };
  const saveSetting = (k, v) => { const s = settings(); if (v === undefined) delete s[k]; else s[k] = v; try { localStorage.setItem(SKEY, JSON.stringify(s)); } catch (_e) { /* private mode */ } };
  const langId = (p) => (window.AstroLSP?.languageId?.(p)) || 'plaintext';
  const uriOf = (p) => `${X.root}/${String(p).split('/').map(encodeURIComponent).join('/')}`;
  const pathOf = (u) => { if (!u || !X.root || !u.startsWith(X.root + '/')) return null; try { return decodeURIComponent(u.slice(X.root.length + 1)); } catch (_e) { return null; } };

  async function listInstalled(force) {
    if (!force && X.installed && Date.now() - X.installed.at < 15000) return X.installed;
    const r = await api('exthost/list', {});
    X.installed = { ...r, at: Date.now() };
    return X.installed;
  }
  const enabledCount = (inst) => Object.values((inst && inst.extensions) || {}).filter((e) => e.enabled !== false).length;

  // ---------------------------------------------------------------- lifecycle
  async function ensure() {
    const project = A()?.project; if (!project) return false;
    if (X.state === 'ready' && X.project === project) return true;
    if (X.starting) return X.starting;
    X.starting = (async () => {
      try {
        const inst = await listInstalled();
        if (!enabledCount(inst)) { stop(); X.state = 'off'; emit(); return false; }
        if (!inst.ready) { X.state = 'error'; X.error = inst.why || 'Node.js is needed'; emit(); return false; }
        stop(true);
        X.state = 'starting'; X.project = project; X.error = ''; emit();
        if (X.stopping) { await X.stopping; X.stopping = null; }   // the ids repeat (project|exthost): a late stop must not hit the new host
        const st = await api('lsp/start', { project, lang: 'exthost', restart: true });
        if (st.error) throw new Error(st.error);
        X.id = st.id; X.root = st.rootUri; X.seq = st.seq || 0; X.docs.clear(); X.diags.clear(); X.status.clear(); X.outputs.clear(); X.unsupported.clear();
        pump(X.id);
        const init = await request('initialize', { settings: settings() }, 60000);
        X.extensions = init.extensions || [];
        X.state = 'ready'; emit();
        syncActive(true);
        return true;
      } catch (e) { X.state = 'error'; X.error = String(e.message || e); emit(); return false; }
      finally { X.starting = null; }
    })();
    return X.starting;
  }
  function stop(silent) {
    const id = X.id; X.id = '';
    for (const [, p] of X.pending) p.reject(new Error('stopped')); X.pending.clear();
    if (id) X.stopping = api('lsp/stop', { id }).catch(() => {});
    X.status.clear(); X.progress.clear();
    const paths = new Set(); for (const owner of X.diags.values()) for (const p of owner.keys()) paths.add(p);
    X.diags.clear(); for (const p of paths) X.cm?.setExtProblems?.(p, []);
    if (!silent) { X.state = 'off'; emit(); }
  }
  async function restart() { stop(true); X.state = 'off'; X.installed = null; await ensure(); }
  async function pump(id) {
    while (X.id === id) {
      let r;
      try { r = await api('lsp/recv', { id, after: X.seq, wait: 20000 }); } catch (_e) { await sleep(1500); continue; }
      if (X.id !== id) return;
      if (typeof r.seq === 'number') X.seq = r.seq;
      for (const m of r.messages || []) onMessage(m);
      if (r.alive === false) { X.state = 'error'; X.error = r.error || 'The extension host stopped.'; X.id = ''; emit(); return; }
    }
  }
  // One ordered outbox. The server handles each HTTP request on its own thread, so separate fetches could reach the
  // host out of order (e.g. "execute" before the "editor" selection it depends on). Messages queued while a send is in
  // flight go out together, in order, in the next request.
  let outbox = [], flushing = null;
  function send(m) {
    if (!X.id) return Promise.resolve();
    outbox.push([X.id, m]);
    if (!flushing) flushing = (async () => {
      try {
        while (outbox.length) {
          const id = outbox[0][0]; const batch = [];
          while (outbox.length && outbox[0][0] === id) batch.push(outbox.shift()[1]);
          if (id === X.id) await api('lsp/send', { id, messages: batch }).catch(() => {});
        }
      } finally { flushing = null; }
    })();
    return flushing;
  }
  const notify = (method, params) => send({ jsonrpc: '2.0', method, params });
  function request(method, params, timeout = 20000) {
    const id = X.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { if (X.pending.delete(id)) reject(new Error('The extension took too long')); }, timeout);
      X.pending.set(id, { resolve: (v) => { clearTimeout(timer); resolve(v); }, reject: (e) => { clearTimeout(timer); reject(e); } });
      send({ jsonrpc: '2.0', id, method, params });
    });
  }

  // ---------------------------------------------------------------- host -> app
  async function onMessage(m) {
    if (m.id != null && !m.method) { const p = X.pending.get(m.id); if (!p) return; X.pending.delete(m.id); if (m.error) p.reject(new Error(m.error.message)); else p.resolve(m.result); return; }
    if (m.method && m.id != null) { let result = null; try { result = await answer(m.method, m.params || {}); } catch (e) { console.warn(e); } send({ jsonrpc: '2.0', id: m.id, result: result === undefined ? null : result }); return; }
    const p = m.params || {};
    switch (m.method) {
      case 'ready': case 'ext/state': X.extensions = p.extensions || X.extensions; if (p.commands) X.commands = p.commands; if (p.capabilities) X.caps = p.capabilities; emit(); break;
      case 'caps': X.caps = p; emit(); break;
      case 'commands/changed': clearTimeout(X.cmdT); X.cmdT = setTimeout(async () => { try { X.commands = await request('commands', {}); X.caps = await request('capabilities', {}); emit(); } catch (_e) { /* stopped */ } }, 150); break;
      case 'ext': { const e = X.extensions.find((x) => x.id === p.id); if (e) { e.activated = p.activated; e.error = p.error; } if (p.error) A()?.toast?.(`Extension ${p.id} failed to start`, 'error'); emit(); break; }
      case 'output': {
        const ch = X.outputs.get(p.channel) || { text: '' };
        if (p.clear) ch.text = ''; if (p.text) ch.text = (ch.text + p.text).slice(-200000);
        X.outputs.set(p.channel, ch); if (p.show) { X.showOutput = p.channel; window.AstroDev?.openVscode?.(); }
        emit(); break;
      }
      case 'status': { if (p.visible === false) X.status.delete(p.id); else X.status.set(p.id, { ...(X.status.get(p.id) || {}), ...p }); emit(); break; }
      case 'progress': { if (p.state === 'end') X.progress.delete(p.id); else X.progress.set(p.id, p.message ? `${p.title} ${p.message}`.trim() : p.title); emit(); break; }
      case 'diagnostics': {
        const owner = X.diags.get(p.owner) || new Map(); X.diags.set(p.owner, owner);
        const path = pathOf(p.uri); if (path == null) break;
        const sev = ['error', 'warning', 'info', 'info'];
        owner.set(path, (p.items || []).map((d) => ({ line: d.range.start.line + 1, col: d.range.start.character + 1, endLine: d.range.end.line + 1, endCol: d.range.end.character + 1, severity: sev[d.severity] || 'error', message: d.message, source: d.source || p.owner })));
        pushProblems(path); break;
      }
      case 'config/update': saveSetting(p.key, p.value); break;
      case 'unsupported': X.unsupported.add(p.api); emit(); break;
      case 'fs/changed': clearTimeout(X.fsT); X.fsT = setTimeout(() => A()?.wsLoadTree?.(), 400); break;
      case 'log': if (p.level === 'error') console.warn('[extension]', p.text); { const ch = X.outputs.get('Extension Host') || { text: '' }; ch.text = (ch.text + `[${p.level}] ${p.text}\n`).slice(-100000); X.outputs.set('Extension Host', ch); } break;
      default: break;
    }
  }
  function pushProblems(only) {
    const cm = X.cm; if (!cm || !cm.setExtProblems) return;
    const paths = new Set(); for (const owner of X.diags.values()) for (const p of owner.keys()) paths.add(p);
    if (only) paths.add(only);
    for (const path of paths) { const all = []; for (const owner of X.diags.values()) all.push(...(owner.get(path) || [])); cm.setExtProblems(path, all); }
    emit();
  }
  const stripSnippet = (t) => String(t).replace(/\$\{\d+:([^}]*)\}/g, '$1').replace(/\$\{\d+\|([^,|]*)[^}]*\}/g, '$1').replace(/\$\{?\d+\}?/g, '').replace(/\\([$}\\])/g, '$1');
  async function answer(method, p) {
    const a = A();
    switch (method) {
      case 'ui/message': {
        if (!p.items || !p.items.length) { if (p.modal) await pick({ title: p.text, items: [], buttons: ['OK'] }); else a.toast(p.text, p.severity === 'error' ? 'error' : undefined); return null; }
        return pick({ title: p.text, buttons: p.items, severity: p.severity });
      }
      case 'ui/quickPick': return pick({ title: p.placeHolder || 'Select', items: p.items || [], many: p.canPickMany });
      case 'ui/input': { const v = await a.wsDialog({ title: p.prompt || 'Input', value: p.value || '', placeholder: p.placeHolder || '', ok: 'OK' }); return v == null ? null : v; }
      case 'editor/apply': {
        let ok = true;
        for (const fe of p.edits || []) {
          const path = pathOf(fe.uri); if (path == null) { ok = false; continue; }
          const edits = fe.edits.map((e) => ({ ...e, newText: p.snippet ? stripSnippet(e.newText) : e.newText }));
          if (X.cm && X.cm.current() === path) { X.cm.applyEdits(edits, 'input.extension'); continue; }
          try {
            const open = a.ws.open.find((o) => o.path === path);
            const text = open ? open.content : (await a.wsApi('/api/fs/read', { path })).content || '';
            const next = window.AstroLSP._applyTextEdits(text, edits);
            await a.wsApi('/api/fs/write', { path, content: next });
            if (open) { open.content = open.saved = next; X.cm?.forget?.(path); }
          } catch (_e) { ok = false; }
        }
        return ok;
      }
      case 'editor/save': { const path = pathOf(p.uri); const f = a.ws.open.find((o) => o.path === path); if (f) await a.wsSaveAll(); return true; }
      case 'editor/saveAll': await a.wsSaveAll(); return true;
      case 'editor/format': X.cm?.view && window.AstroLSP && X.cm.view.contentDOM.dispatchEvent(new KeyboardEvent('keydown', { key: 'F', code: 'KeyF', shiftKey: true, altKey: true, bubbles: true })); return true;
      case 'editor/select': { const s = (p.selections || [])[0]; if (s && X.cm && pathOf(p.uri) === X.cm.current()) X.cm.gotoLine(s.start.line + 1, s.start.character + 1, s.end.line === s.start.line ? s.end.character - s.start.character : 0); return true; }
      case 'window/open': { const path = pathOf(p.uri); if (path == null) { a.toast('The extension tried to open a file outside this project'); return false; } const r = p.selection && p.selection.start; await a.wsGoto(path, r ? r.line + 1 : 1, r ? r.character + 1 : 1, 0); return true; }
      case 'clipboard/read': try { return await navigator.clipboard.readText(); } catch (_e) { return ''; }
      case 'clipboard/write': try { await navigator.clipboard.writeText(p.text || ''); } catch (_e) { /* denied */ } return true;
      case 'env/openExternal': { const u = String(p.uri || ''); if (!/^https?:\/\//.test(u)) return false; if (!(await a.wsDialog({ title: 'Open link?', text: `An extension wants to open ${u}`, ok: 'Open', input: false }))) return false; window.open(u, '_blank', 'noopener'); return true; }
      default: return null;
    }
  }
  // Small picker: buttons (message with actions) or a filterable list (quick pick). Resolves to an index, [indices] or null.
  function pick({ title, items = [], buttons = [], many = false, severity }) {
    return new Promise((resolve) => {
      const esc = A().esc;
      const back = document.createElement('div'); back.className = 'ws-dialog-back';
      const list = items.length ? `<input type="text" class="xh-filter" placeholder="Type to filter" spellcheck="false"><div class="xh-list" role="listbox">${items.map((it, i) => it.kind === -1 ? `<div class="xh-sep">${esc(it.label)}</div>` : `<label class="xh-item" data-i="${i}">${many ? `<input type="checkbox"${it.picked ? ' checked' : ''}>` : ''}<span><b>${esc(String(it.label).replace(/\$\([\w-]+\)\s*/g, ''))}</b>${it.description ? ` <em>${esc(it.description)}</em>` : ''}${it.detail ? `<small>${esc(it.detail)}</small>` : ''}</span></label>`).join('')}</div>` : '';
      back.innerHTML = `<div class="ws-dialog xh-pick${severity ? ` sev-${esc(severity)}` : ''}" role="dialog" aria-modal="true"><h3>${esc(title)}</h3>${list}<div class="ws-dialog-row">${buttons.map((b, i) => `<button type="button" class="ws-btn${i === 0 ? ' primary' : ''}" data-b="${i}">${esc(b)}</button>`).join('')}${many ? '<button type="button" class="ws-btn primary" data-ok>OK</button>' : ''}<button type="button" class="ws-btn" data-x>${buttons.length ? 'Close' : 'Cancel'}</button></div></div>`;
      document.body.append(back);
      const done = (v) => { back.remove(); resolve(v); };
      back.addEventListener('mousedown', (e) => { if (e.target === back) done(null); });
      back.addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.stopPropagation(); done(null); } if (e.key === 'Enter' && items.length && !many) { const f = back.querySelector('.xh-item:not([hidden])'); if (f) { e.preventDefault(); done(Number(f.dataset.i)); } } });
      back.querySelector('[data-x]').onclick = () => done(null);
      back.querySelectorAll('[data-b]').forEach((b) => { b.onclick = () => done(Number(b.dataset.b)); });
      const ok = back.querySelector('[data-ok]'); if (ok) ok.onclick = () => done([...back.querySelectorAll('.xh-item')].filter((l) => l.querySelector('input').checked).map((l) => Number(l.dataset.i)));
      if (!many) back.querySelectorAll('.xh-item').forEach((l) => { l.onclick = () => done(Number(l.dataset.i)); });
      const f = back.querySelector('.xh-filter');
      if (f) { f.oninput = () => { const q = f.value.toLowerCase(); back.querySelectorAll('.xh-item').forEach((l) => { l.hidden = q && !l.textContent.toLowerCase().includes(q); }); }; setTimeout(() => f.focus(), 20); }
      else setTimeout(() => back.querySelector('button')?.focus(), 20);
    });
  }

  // ---------------------------------------------------------------- app -> host: documents
  function docText(path) { if (X.cm && X.cm.current() === path) return X.cm.getDoc(); const f = A()?.ws.open.find((o) => o.path === path); return f ? f.content || '' : null; }
  function openDoc(path) {
    if (X.state !== 'ready' || !path) return null;
    const text = docText(path); if (text == null) return null;
    const uri = uriOf(path); const d = X.docs.get(path);
    if (!d) { X.docs.set(path, { text, sent: text, version: 1 }); notify('doc/open', { uri, languageId: langId(path), version: 1, text }); }
    else if (d.sent !== text) { d.text = d.sent = text; d.version++; clearTimeout(d.t); notify('doc/change', { uri, version: d.version, text }); }
    return uri;
  }
  function editorState() {
    const path = X.cm && X.cm.current(); if (!path) return;
    const uri = openDoc(path); if (!uri) return;
    const s = X.cm.getSelection(); const a = X.cm.lcAt(s.from), b = X.cm.lcAt(s.to);
    notify('editor', { uri, selections: [{ anchor: a, active: b }] });
  }
  let lastActive = '';
  function syncActive(force) {
    const path = A()?.ws.active;
    if (X.state !== 'ready') return;
    if (path && (force || path !== lastActive)) { lastActive = path; editorState(); }
  }
  function changed(path, text) {
    const d = X.docs.get(path); if (!d || X.state !== 'ready') return;
    d.text = text; clearTimeout(d.t);
    d.t = setTimeout(() => { if (d.sent === d.text) return; d.sent = d.text; d.version++; notify('doc/change', { uri: uriOf(path), version: d.version, text: d.text }); }, 200);
  }
  function saved(path) { if (X.docs.has(path)) notify('doc/save', { uri: uriOf(path) }); }

  // ---------------------------------------------------------------- language features (used by lsp.js)
  const covers = (list, path) => list.includes('*') || list.includes(langId(path));
  const handles = (path) => X.state === 'ready' && path && ['completion', 'hover', 'definition', 'formatting'].some((k) => covers(X.caps[k] || [], path));
  async function complete(q) {
    if (X.state !== 'ready' || !covers(X.caps.completion || [], q.key)) return null;
    if (q.trigger && !(X.caps.triggers || []).includes(q.trigger) && !q.explicit) return null;
    const uri = openDoc(q.key); const r = await request('complete', { uri, line: q.line, character: q.character, trigger: q.trigger, explicit: q.explicit }, 8000);
    return r && r.items && r.items.length ? r : null;
  }
  async function hover(q) { if (X.state !== 'ready' || !covers(X.caps.hover || [], q.key)) return null; return request('hover', { uri: openDoc(q.key), line: q.line, character: q.character }, 8000); }
  async function definition(q) {
    if (X.state !== 'ready' || !covers(X.caps.definition || [], q.key)) return false;
    const r = await request('definition', { uri: openDoc(q.key), line: q.line, character: q.character }, 10000);
    const l = r && r[0]; if (!l) return false;
    const path = pathOf(l.uri); if (path == null) return false;
    A().wsGoto(path, l.range.start.line + 1, l.range.start.character + 1, 0); return true;
  }
  async function format(q) {
    if (X.state !== 'ready' || !covers(X.caps.formatting || [], q.key)) return null;
    return request('format', { uri: openDoc(q.key), tabSize: q.tabSize, insertSpaces: q.insertSpaces }, 20000);
  }

  // ---------------------------------------------------------------- commands + UI bits
  async function execute(command, args) {
    if (!(await ensure())) { A().toast(X.error || 'No VS Code extensions are installed', 'error'); return; }
    editorState();
    try { return await request('execute', { command, args: args || [] }, 120000); }
    catch (e) { A().toast(`${command}: ${e.message}`, 'error'); return undefined; }
  }
  function paletteItems() {
    if (X.state !== 'ready') return [];
    return X.commands.map((c) => ({ label: `${c.category ? `${c.category}: ` : ''}${c.title || c.command}`, icon: 'i-puzzle', sub: `${c.ext || 'Extension'} · ${c.command}`, run: () => execute(c.command) }));
  }
  function chips() {
    if (X.state !== 'ready') return '';
    const esc = A().esc; let html = '';
    const items = [...X.status.values()].filter((s) => s.text).sort((a, b) => (a.alignment - b.alignment) || (b.priority - a.priority)).slice(0, 4);
    for (const s of items) html += `<button type="button" data-st="ext" data-cmd="${esc(s.command || '')}" title="${esc(s.tooltip || s.text)}">${esc(String(s.text).replace(/\$\([\w~-]+\)\s*/g, '').slice(0, 40))}</button>`;
    const prog = [...X.progress.values()][0]; if (prog) html += `<span class="st-ext-prog">${esc(String(prog).slice(0, 40))}…</span>`;
    return html;
  }
  function onStatusClick(btn) { const cmd = btn.dataset.cmd; if (cmd) execute(cmd); else window.AstroDev?.openVscode?.(); }

  // ---------------------------------------------------------------- wiring
  function attach(cm) {
    X.cm = cm;
    if (!X.timer) X.timer = setInterval(() => { if (X.state === 'ready' && A()?.project !== X.project) { stop(true); X.state = 'off'; ensure(); } syncActive(false); }, 800);
    ensure();
  }
  function projectChanged() { lastActive = ''; if (X.project && X.project !== A()?.project) { stop(true); X.state = 'off'; } ensure(); }

  window.AstroExtHost = {
    attach, ensure, restart, stop, execute, changed, saved, projectChanged, paletteItems, chips, onStatusClick, listInstalled,
    handles, complete, hover, definition, format,
    get state() { return X.state; }, get error() { return X.error; }, get extensions() { return X.extensions; }, get commands() { return X.commands; },
    get outputs() { return X.outputs; }, get unsupported() { return [...X.unsupported]; }, get showOutput() { return X.showOutput; }, set showOutput(v) { X.showOutput = v; },
    onChange(f) { X.listeners.add(f); return () => X.listeners.delete(f); },
    problems() { const out = []; for (const owner of X.diags.values()) for (const [path, list] of owner) for (const d of list) out.push({ path, ...d }); return out; },
  };
})();

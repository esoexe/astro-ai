// A.S.T.R.O AI language-server client (window.AstroLSP).
// Talks to real language servers (clangd, Pyright, TypeScript, rust-analyzer, gopls, ...) through the engine's
// /api/dev/lsp/* relay and feeds the editor: completion, hover, go-to-definition, formatting, rename and live errors.
// Servers are installed portably from Extensions > Tools. Loaded before app.js; dev.js calls attach() when the editor is ready.
(() => {
  'use strict';
  const A = () => window.AstroAgent;
  const api = (path, body) => A().apiJson(`/api/dev/${path}`, body || {});
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const L = {
    status: null, statusProject: '', statusAt: 0,   // /lsp/status cache
    clients: new Map(),                               // project|lang -> Client
    cm: null, ide: null, enabled: true, listeners: new Set(),
  };
  try { L.enabled = localStorage.getItem('astro_lsp:off') !== '1'; } catch (_e) { /* private mode */ }

  const extOf = (p) => { const m = /\.([^./\\]+)$/.exec(String(p || '')); return m ? m[1].toLowerCase() : ''; };
  function langFor(ext) {
    if (!L.status) return null;
    for (const s of L.status.servers || []) if (s.exts.split(' ').includes(ext)) return s;
    return null;
  }
  const LANG_ID = { js: 'javascript', mjs: 'javascript', cjs: 'javascript', jsx: 'javascriptreact', ts: 'typescript', mts: 'typescript', cts: 'typescript', tsx: 'typescriptreact',
    c: 'c', h: 'cpp', cpp: 'cpp', cc: 'cpp', cxx: 'cpp', hpp: 'cpp', hh: 'cpp', hxx: 'cpp', ino: 'cpp', py: 'python', pyi: 'python', html: 'html', htm: 'html', css: 'css', scss: 'scss', less: 'less',
    json: 'json', jsonc: 'jsonc', rs: 'rust', go: 'go', java: 'java', sh: 'shellscript', bash: 'shellscript', zsh: 'shellscript', yaml: 'yaml', yml: 'yaml', lua: 'lua', php: 'php' };

  async function refreshStatus(force) {
    const project = A()?.project || '';
    if (!force && L.status && L.statusProject === project && Date.now() - L.statusAt < 60000) return L.status;
    try { L.status = await api('lsp/status', { project }); L.statusProject = project; L.statusAt = Date.now(); } catch (_e) { /* engine offline */ }
    emit();
    return L.status;
  }
  function emit() { for (const f of L.listeners) { try { f(); } catch (_e) { /* listener */ } } }

  // ---------------------------------------------------------------- one language server
  class Client {
    constructor(project, lang) {
      this.project = project; this.lang = lang; this.key = `${project}|${lang}`;
      this.id = ''; this.root = ''; this.seq = 0; this.nextId = 1; this.pending = new Map();
      this.docs = new Map();      // path -> { version, text, timer }
      this.caps = {}; this.state = 'starting'; this.error = ''; this.progress = new Map(); this.diags = new Map();
      this.ready = this.start();
    }
    async start(restart = true) {
      try {
        const st = await api('lsp/start', { project: this.project, lang: this.lang, restart });
        if (st.error) throw new Error(st.error);
        this.id = st.id; this.root = st.rootUri; this.seq = st.seq || 0;
        this.pump();
        const init = await this.request('initialize', {
          processId: null, rootUri: this.root, rootPath: st.rootPath, workspaceFolders: [{ uri: this.root, name: this.project }],
          initializationOptions: st.initializationOptions,
          clientInfo: { name: 'A.S.T.R.O AI', version: '1.8.1' },
          capabilities: {
            general: { positionEncodings: ['utf-16'] },
            workspace: { configuration: true, workspaceFolders: true, applyEdit: false, workspaceEdit: { documentChanges: true } },
            window: { workDoneProgress: true, showMessage: {} },
            textDocument: {
              synchronization: { didSave: true, dynamicRegistration: false },
              completion: { completionItem: { snippetSupport: true, documentationFormat: ['markdown', 'plaintext'], insertReplaceSupport: false, labelDetailsSupport: true }, contextSupport: true },
              hover: { contentFormat: ['markdown', 'plaintext'] },
              definition: { linkSupport: true }, formatting: {}, rename: { prepareSupport: false },
              publishDiagnostics: { relatedInformation: false, versionSupport: false },
              diagnostic: { dynamicRegistration: false, relatedDocumentSupport: false },
              signatureHelp: { signatureInformation: { documentationFormat: ['markdown', 'plaintext'] } },
            },
          },
        }, 90000);
        this.caps = (init && init.capabilities) || {};
        this.notify('initialized', {});
        this.state = 'ready'; emit();
        return true;
      } catch (e) {
        this.state = 'error'; this.error = String(e.message || e); emit();
        return false;
      }
    }
    async pump() {
      const id = this.id;
      while (this.id === id && this.state !== 'stopped') {
        let r;
        try { r = await api('lsp/recv', { id, after: this.seq, wait: 20000 }); } catch (_e) { await sleep(1500); continue; }
        if (this.id !== id) return;
        if (typeof r.seq === 'number') this.seq = r.seq;
        for (const m of r.messages || []) this.onMessage(m);
        if (r.alive === false || r.error) {
          this.state = 'error'; this.error = r.error || 'The language server stopped.';
          for (const [, p] of this.pending) p.reject(new Error(this.error));
          this.pending.clear(); emit(); return;
        }
      }
    }
    onMessage(m) {
      if (m.id != null && !m.method) {
        const p = this.pending.get(m.id); if (!p) return; this.pending.delete(m.id); clearTimeout(p.timer);
        if (m.error) p.reject(new Error(m.error.message || 'Request failed')); else p.resolve(m.result);
        return;
      }
      if (m.method && m.id != null) { this.answer(m); return; }
      switch (m.method) {
        case 'textDocument/publishDiagnostics': this.setDiags(m.params.uri, m.params.diagnostics || []); break;
        case 'window/showMessage': if (m.params && m.params.type === 1) A()?.toast?.(`${this.lang}: ${String(m.params.message).slice(0, 200)}`, 'error'); break;
        case '$/progress': {
          const v = m.params.value || {};
          if (v.kind === 'end') this.progress.delete(m.params.token); else this.progress.set(m.params.token, v.title || v.message || this.progress.get(m.params.token) || 'Working');
          emit(); break;
        }
        default: break;
      }
    }
    answer(m) {   // requests from the server to us
      let result = null;
      if (m.method === 'workspace/configuration') result = ((m.params && m.params.items) || []).map(() => null);
      else if (m.method === 'workspace/workspaceFolders') result = [{ uri: this.root, name: this.project }];
      this.send({ jsonrpc: '2.0', id: m.id, result });
    }
    // Ordered: separate fetches may be handled by different server threads, so a didChange could land after the
    // completion request that needs it. Messages queued while a send is in flight go out together, in order.
    send(msg) {
      (this.outbox || (this.outbox = [])).push(msg);
      if (!this.flushing) this.flushing = (async () => {
        try { while (this.outbox.length) { const batch = this.outbox.splice(0); await api('lsp/send', { id: this.id, messages: batch }).catch(() => {}); } }
        finally { this.flushing = null; }
      })();
      return this.flushing;
    }
    notify(method, params) { return this.send({ jsonrpc: '2.0', method, params }); }
    request(method, params, timeout = 15000) {
      const id = this.nextId++;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { if (this.pending.delete(id)) { this.send({ jsonrpc: '2.0', method: '$/cancelRequest', params: { id } }); reject(new Error('timeout')); } }, timeout);
        this.pending.set(id, { resolve, reject, timer });
        this.send({ jsonrpc: '2.0', id, method, params });
      });
    }
    uri(path) { return `${this.root}/${String(path).split('/').map(encodeURIComponent).join('/')}`; }
    pathOf(uri) {
      if (!uri || !uri.startsWith(this.root + '/')) return null;
      try { return decodeURIComponent(uri.slice(this.root.length + 1)); } catch (_e) { return null; }
    }
    // Makes sure the server has the current text of path (didOpen / pending didChange).
    sync(path, text, now) {
      let d = this.docs.get(path);
      if (!d) {
        d = { version: 1, text };
        this.docs.set(path, d);
        this.notify('textDocument/didOpen', { textDocument: { uri: this.uri(path), languageId: LANG_ID[extOf(path)] || this.lang, version: 1, text } });
        this.pull(path);
        return;
      }
      const flush = () => { d.timer = 0; d.version++; this.notify('textDocument/didChange', { textDocument: { uri: this.uri(path), version: d.version }, contentChanges: [{ text: d.text }] }); this.pull(path); };
      // A debounced change that has not gone out yet must be SENT (not dropped) when a request needs the current text,
      // or the server answers for stale text (completion ranges land on the wrong characters).
      if (d.text === text) { if (now && d.timer) { clearTimeout(d.timer); flush(); } return; }
      d.text = text;
      clearTimeout(d.timer);
      if (now) flush(); else d.timer = setTimeout(flush, 150);
    }
    saved(path) { if (this.docs.has(path)) this.notify('textDocument/didSave', { textDocument: { uri: this.uri(path) } }); }
    // Servers with pull diagnostics (TypeScript 7, newer servers) are asked after each change.
    pull(path) {
      if (!this.caps.diagnosticProvider) return;
      const d = this.docs.get(path); if (!d) return;
      clearTimeout(d.pullTimer);
      d.pullTimer = setTimeout(async () => {
        try { const r = await this.request('textDocument/diagnostic', { textDocument: { uri: this.uri(path) } }, 20000); if (r && r.kind !== 'unchanged') this.setDiags(this.uri(path), r.items || []); } catch (_e) { /* not supported */ }
      }, 350);
    }
    setDiags(uri, list) {
      const path = this.pathOf(uri); if (path == null) return;
      const sev = { 1: 'error', 2: 'warning', 3: 'info', 4: 'info' };
      const items = list.map((d) => ({ line: d.range.start.line + 1, col: d.range.start.character + 1, endLine: d.range.end.line + 1, endCol: d.range.end.character + 1,
        severity: sev[d.severity || 1], message: String(d.message || '').trim(), source: d.source || this.lang, code: d.code }));
      this.diags.set(path, items);
      L.cm?.setLspProblems?.(path, items);
      emit();
    }
    stop() { this.state = 'stopped'; const id = this.id; this.id = ''; if (id) api('lsp/stop', { id }).catch(() => {}); for (const d of this.docs.values()) clearTimeout(d.timer); }
  }

  function fileText(path) {
    const f = A()?.ws.open.find((o) => o.path === path);
    if (L.cm && L.cm.current() === path) return L.cm.getDoc();
    return f ? f.content || '' : null;
  }
  // Client for a file, starting its server if needed. Returns null when there is no server for this language.
  async function clientFor(path, start = true) {
    if (!L.enabled) return null;
    const project = A()?.project; if (!project) return null;
    await refreshStatus();
    const s = langFor(extOf(path)); if (!s || !s.available) return null;
    const key = `${project}|${s.lang}`;
    let c = L.clients.get(key);
    if (c && c.state === 'error' && Date.now() - (c.failedAt || 0) > 30000 && start) { c.stop(); L.clients.delete(key); c = null; }
    if (!c) {
      if (!start) return null;
      const live = [...L.clients.values()].filter((x) => x.state !== 'stopped');
      if (live.length >= 4) { live[0].stop(); L.clients.delete(live[0].key); }   // keep browser connections free
      c = new Client(project, s.lang); L.clients.set(key, c); emit();
    }
    if (!(await c.ready) || c.state !== 'ready') { c.failedAt = c.failedAt || Date.now(); return null; }
    const text = fileText(path); if (text == null) return null;
    c.sync(path, text, true);
    return c;
  }

  // ---------------------------------------------------------------- the editor's language service
  const docText = (v) => (typeof v === 'string' ? v : v && typeof v.value === 'string' ? v.value : '');
  function markdownOf(contents) {
    if (!contents) return '';
    if (Array.isArray(contents)) return contents.map(markdownOf).filter(Boolean).join('\n\n---\n\n');
    if (typeof contents === 'string') return contents;
    if (contents.kind) return contents.kind === 'markdown' ? contents.value : '```\n' + contents.value + '\n```';
    if (contents.language) return '```' + contents.language + '\n' + contents.value + '\n```';
    return '';
  }
  const lspService = {
    handles(key, ext) { return L.enabled && Boolean(A()?.project) && Boolean(L.status) && Boolean(langFor(ext)?.available); },
    async complete(q) {
      const c = await clientFor(q.key); if (!c || !c.caps.completionProvider) return null;
      const trig = c.caps.completionProvider.triggerCharacters || [];
      const context = q.trigger && trig.includes(q.trigger) ? { triggerKind: 2, triggerCharacter: q.trigger } : { triggerKind: 1 };
      if (q.trigger && !trig.includes(q.trigger) && !q.explicit) return null;
      const r = await c.request('textDocument/completion', { textDocument: { uri: c.uri(q.key) }, position: { line: q.line, character: q.character }, context }, 8000);
      const items = Array.isArray(r) ? r : (r && r.items) || [];
      items.sort((a, b) => String(a.sortText || a.label).localeCompare(String(b.sortText || b.label)));
      return {
        items: items.map((it) => ({
          label: it.label, kind: it.kind, filterText: it.filterText,
          detail: it.detail || (it.labelDetails && (it.labelDetails.detail || it.labelDetails.description)) || '',
          documentation: docText(it.documentation),
          insertText: it.textEdit ? it.textEdit.newText : it.insertText != null ? it.insertText : it.label,
          isSnippet: it.insertTextFormat === 2,
        })),
      };
    },
    async hover(q) {
      const c = await clientFor(q.key); if (!c || !c.caps.hoverProvider) return null;
      const r = await c.request('textDocument/hover', { textDocument: { uri: c.uri(q.key) }, position: { line: q.line, character: q.character } }, 8000);
      return r ? markdownOf(r.contents) : null;
    },
    async definition(q) {
      const c = await clientFor(q.key); if (!c || !c.caps.definitionProvider) { A()?.toast?.('No language server for this file. Install one in Extensions › Tools.'); return; }
      let r = await c.request('textDocument/definition', { textDocument: { uri: c.uri(q.key) }, position: { line: q.line, character: q.character } }, 15000);
      if (Array.isArray(r)) r = r[0];
      if (!r) { A()?.toast?.('No definition found'); return; }
      const uri = r.targetUri || r.uri; const range = r.targetSelectionRange || r.range || { start: { line: 0, character: 0 } };
      const path = c.pathOf(uri);
      if (path == null) { A()?.toast?.(`Defined outside this project: ${decodeURIComponent(String(uri).replace(/^file:\/\//, ''))}:${range.start.line + 1}`); return; }
      A().wsGoto(path, range.start.line + 1, range.start.character + 1, 0);
    },
    async format(q) {
      const c = await clientFor(q.key); if (!c || !c.caps.documentFormattingProvider) { A()?.toast?.('This language server cannot format files'); return; }
      const ext = extOf(q.key); const spaces = ext === 'go' ? 0 : ['py', 'java', 'rs', 'php', 'cs'].includes(ext) ? 4 : 2;
      const before = L.cm.getDoc();
      const edits = await c.request('textDocument/formatting', { textDocument: { uri: c.uri(q.key) }, options: { tabSize: spaces || 4, insertSpaces: spaces > 0 } }, 20000);
      if (L.cm.current() !== q.key || L.cm.getDoc() !== before) return;
      const n = L.cm.applyEdits(edits || [], 'input.format');
      A()?.toast?.(n ? 'Formatted' : 'Already formatted');
    },
    async rename(q) {
      const c = await clientFor(q.key); if (!c || !c.caps.renameProvider) { A()?.toast?.('This language server cannot rename'); return; }
      const old = L.cm.wordAt(L.cm.posAt(q.line, q.character));
      const name = await A().wsDialog({ title: 'Rename symbol', text: `Rename “${old}” everywhere it is used.`, value: old, ok: 'Rename' });
      if (!name || name === old) return;
      const we = await c.request('textDocument/rename', { textDocument: { uri: c.uri(q.key) }, position: { line: q.line, character: q.character }, newName: name }, 30000);
      await applyWorkspaceEdit(c, we);
    },
  };
  // Language servers + VS Code extensions (AstroExtHost) behind one editor service.
  const X = () => (window.AstroExtHost && window.AstroExtHost.state === 'ready' ? window.AstroExtHost : null);
  const lspHas = async (key, cap) => { if (!lspService.handles(key, extOf(key))) return false; const c = await clientFor(key).catch(() => null); return Boolean(c && c.caps[cap]); };
  const service = {
    handles(key, ext) { return lspService.handles(key, ext) || Boolean(X()?.handles(key)); },
    async complete(q) {
      const [a, b] = await Promise.all([lspService.handles(q.key, extOf(q.key)) ? lspService.complete(q).catch(() => null) : null, X() ? X().complete(q).catch(() => null) : null]);
      if (!a && !b) return null;
      return { items: [...((a && a.items) || []), ...((b && b.items) || [])] };
    },
    async hover(q) {
      const [a, b] = await Promise.all([lspService.handles(q.key, extOf(q.key)) ? lspService.hover(q).catch(() => null) : null, X() ? X().hover(q).catch(() => null) : null]);
      return [a, b].filter(Boolean).join('\n\n---\n\n') || null;
    },
    async definition(q) {
      if (!(await lspHas(q.key, 'definitionProvider')) && X() && (await X().definition(q).catch(() => false))) return;
      return lspService.definition(q);
    },
    async format(q) {
      if (!(await lspHas(q.key, 'documentFormattingProvider')) && X()) {
        const ext = extOf(q.key); const spaces = ext === 'go' ? 0 : ['py', 'java', 'rs', 'php', 'cs'].includes(ext) ? 4 : 2;
        const before = L.cm.getDoc();
        const edits = await X().format({ ...q, tabSize: spaces || 4, insertSpaces: spaces > 0 }).catch(() => null);
        if (edits) { if (L.cm.current() !== q.key || L.cm.getDoc() !== before) return; const n = L.cm.applyEdits(edits, 'input.format'); A()?.toast?.(n ? 'Formatted' : 'Already formatted'); return; }
      }
      return lspService.format(q);
    },
    rename(q) { return lspService.rename(q); },
  };
  // Applies a WorkspaceEdit: the open file through the editor (undoable), other files on disk.
  async function applyWorkspaceEdit(c, we) {
    if (!we) { A()?.toast?.('Nothing to rename'); return; }
    const byUri = new Map();
    if (we.changes) for (const [u, e] of Object.entries(we.changes)) byUri.set(u, e);
    if (we.documentChanges) for (const dc of we.documentChanges) if (dc.textDocument && dc.edits) byUri.set(dc.textDocument.uri, dc.edits);
    let files = 0, outside = 0;
    for (const [u, edits] of byUri) {
      const path = c.pathOf(u); if (path == null) { outside++; continue; }
      if (L.cm.current() === path) { L.cm.applyEdits(edits, 'input.rename'); files++; continue; }
      try {
        const open = A().ws.open.find((o) => o.path === path);
        const text = open ? open.content : (await A().wsApi('/api/fs/read', { path })).content || '';
        const next = applyTextEdits(text, edits);
        await A().wsApi('/api/fs/write', { path, content: next });
        if (open) { open.content = open.saved = next; L.cm.forget?.(path); }
        c.sync(path, next, true); files++;
      } catch (_e) { /* skip unreadable */ }
    }
    A()?.toast?.(`Renamed in ${files} file${files === 1 ? '' : 's'}${outside ? ` (${outside} outside the project left unchanged)` : ''}`);
  }
  function applyTextEdits(text, edits) {
    const starts = [0]; for (let i = 0; i < text.length; i++) if (text[i] === '\n') starts.push(i + 1);
    const off = (p) => Math.min(text.length, (starts[Math.min(p.line, starts.length - 1)] ?? text.length) + p.character);
    const list = edits.map((e) => ({ from: off(e.range.start), to: off(e.range.end), t: e.newText || '' })).sort((a, b) => b.from - a.from);
    for (const e of list) text = text.slice(0, e.from) + e.t + text.slice(e.to);
    return text;
  }

  // ---------------------------------------------------------------- wiring
  let lastActive = '';
  function tick() {
    const a = A(); if (!a || !L.cm || !L.enabled) return;
    const path = a.ws.active;
    if (path && path !== lastActive) {
      lastActive = path;
      const ext = extOf(path);
      refreshStatus().then(() => { const s = langFor(ext); if (s && s.available) clientFor(path).catch(() => {}); else emit(); });
    }
  }
  function attach(cm, ide) {
    L.cm = cm; L.ide = ide || window.AstroIDE;
    if (L.ide && L.ide.setLanguageService) L.ide.setLanguageService(service);
    refreshStatus(true);
    if (!L.timer) L.timer = setInterval(tick, 700);
  }
  // app.js: the open file changed (typing, AI edit) / was saved
  function changed(path, text) {
    const s = langFor(extOf(path)); const project = A()?.project; if (!s || !project) return;
    const c = L.clients.get(`${project}|${s.lang}`); if (c && c.state === 'ready') c.sync(path, text, false);
  }
  function saved(path) { const s = langFor(extOf(path)); const c = s && L.clients.get(`${A()?.project}|${s.lang}`); if (c && c.state === 'ready') c.saved(path); }
  function projectChanged() { L.statusAt = 0; lastActive = ''; refreshStatus(true); }
  // Status-bar chip for the active file.
  function chip(path) {
    if (!L.status || !path) return '';
    const s = langFor(extOf(path)); if (!s) return '';
    const esc = A().esc;
    if (!L.enabled) return `<button type="button" data-st="lsp" class="st-lsp off" title="Language servers are off">${esc(s.lang)} · off</button>`;
    if (!s.available) return `<button type="button" data-st="lsp" class="st-lsp need" title="${esc(s.why || '')}">Install ${esc(s.tool)} for ${esc(s.lang)}</button>`;
    const c = L.clients.get(`${A().project}|${s.lang}`);
    const st = !c ? 'idle' : c.state;
    const busy = c && c.progress.size ? [...c.progress.values()][0] : '';
    const label = st === 'ready' ? (busy ? `${busy}…` : 'ready') : st === 'starting' ? 'starting…' : st === 'error' ? 'stopped' : 'idle';
    const n = c ? (c.diags.get(path) || []).filter((d) => d.severity === 'error').length : 0;
    return `<button type="button" data-st="lsp" class="st-lsp ${st}" title="${esc(st === 'error' ? c.error : `Language server: ${s.command || s.tool}`)}"><i class="st-dot"></i>${esc(s.lang)} · ${esc(label)}${n ? ` · ${n} error${n === 1 ? '' : 's'}` : ''}</button>`;
  }
  function setEnabled(on) {
    L.enabled = Boolean(on);
    try { localStorage.setItem('astro_lsp:off', on ? '0' : '1'); } catch (_e) { /* private mode */ }
    if (!on) { for (const c of L.clients.values()) c.stop(); L.clients.clear(); L.cm?.clearProblems && A()?.ws.open.forEach((o) => L.cm.setLspProblems?.(o.path, [])); }
    lastActive = ''; emit();
  }
  async function restart(lang) {
    for (const [k, c] of L.clients) if (!lang || c.lang === lang) { c.stop(); L.clients.delete(k); }
    lastActive = ''; await refreshStatus(true); tick();
  }
  function problems() {   // every language-server problem in this project: [{path, line, col, severity, message, source}]
    const out = []; const project = A()?.project;
    for (const c of L.clients.values()) if (c.project === project) for (const [path, list] of c.diags) for (const d of list) out.push({ path, ...d });
    return out;
  }
  function servers() { return [...L.clients.values()].map((c) => ({ project: c.project, lang: c.lang, state: c.state, error: c.error, busy: [...c.progress.values()][0] || '' })); }

  window.AstroLSP = {
    attach, changed, saved, projectChanged, chip, refreshStatus, setEnabled, restart, problems, servers,
    get enabled() { return L.enabled; }, get status() { return L.status; },
    onChange(f) { L.listeners.add(f); return () => L.listeners.delete(f); },
    _applyTextEdits: applyTextEdits,
    languageId: (p) => LANG_ID[extOf(p)] || ({ md: 'markdown', markdown: 'markdown', txt: 'plaintext', xml: 'xml', sql: 'sql', rb: 'ruby', cs: 'csharp', kt: 'kotlin', swift: 'swift', dart: 'dart', toml: 'toml', vue: 'vue', svelte: 'svelte' })[extOf(p)] || 'plaintext',
  };
})();

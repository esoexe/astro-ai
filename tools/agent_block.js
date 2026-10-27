  // ===========================================================================================
  // Agent mode: an autonomous, tool-using agent (team plan → act → observe → repeat → team review),
  // shown OpenCode-style: a live timeline of every step, plus the files it changed (diffs), the
  // project files, a terminal transcript and a live preview. Used by the Agent page and by the
  // Workspace team panel ("Agent" mode). Sessions are saved on the server per project.
  // ===========================================================================================
  const AG = {
    sessions: new Map(), list: [], cur: '', project: '', runs: new Map(), waiters: new Map(), saveT: new Map(),
    side: 'changes', sideOpen: true, changes: null, tree: null, view: null, prev: '', termExtra: [], mounted: false,
    team: ls.get('astro_agent:team') !== '0', tick: 0,
  };
  const AG_MAX = 40;
  const AG_TOOLS = {
    bash: ['i-terminal', 'Ran'], read_file: ['i-file', 'Read'], write_file: ['i-new-file', 'Wrote'], edit_file: ['i-pen', 'Edited'],
    delete_file: ['i-trash', 'Deleted'], list_files: ['i-folder', 'Listed files'], search: ['i-search', 'Searched code'],
    web_search: ['i-globe', 'Searched the web'], fetch_page: ['i-read', 'Read page'], start_server: ['i-server', 'Started server'],
    server_logs: ['i-server', 'Server logs'], stop_server: ['i-stop', 'Stopped server'], ask_user: ['i-hand', 'Asked you'],
    remember: ['i-memory', 'Remembered'], present: ['i-eye', 'Presented'], _bad: ['i-x', 'Unreadable tool call'],
  };
  const AG_RUNNING = { bash: 'Running', read_file: 'Reading', write_file: 'Writing', edit_file: 'Editing', delete_file: 'Deleting', list_files: 'Listing files', search: 'Searching code', web_search: 'Searching the web', fetch_page: 'Reading page', start_server: 'Starting server', server_logs: 'Reading logs', stop_server: 'Stopping server', ask_user: 'Question for you', remember: 'Remembering', present: 'Presenting' };
  const AG_ALIAS = {
    shell: 'bash', run: 'bash', exec: 'bash', terminal: 'bash', command: 'bash', sh: 'bash', run_command: 'bash', execute: 'bash',
    create_file: 'write_file', write: 'write_file', save_file: 'write_file', read: 'read_file', cat: 'read_file', open_file: 'read_file',
    ls: 'list_files', list: 'list_files', list_dir: 'list_files', grep: 'search', search_code: 'search', search_files: 'search',
    replace: 'edit_file', str_replace: 'edit_file', edit: 'edit_file', patch: 'edit_file', remove_file: 'delete_file', rm: 'delete_file',
    read_url: 'fetch_page', open_url: 'fetch_page', fetch: 'fetch_page', browse: 'fetch_page', search_web: 'web_search', google: 'web_search',
    start_process: 'start_server', serve: 'start_server', stop_process: 'stop_server', logs: 'server_logs', process_output: 'server_logs',
    ask: 'ask_user', question: 'ask_user', memory: 'remember', show: 'present', present_file: 'present', open: 'present',
  };
  let agSeq = 0;
  const agId = () => `${Date.now().toString(36)}${(agSeq++).toString(36)}`;
  const agApi = (s, path, body) => apiJson(path, { project: s.project, ...body });
  const agBusy = (s) => Boolean(s && AG.runs.has(s.id));
  const agClip = (t, n) => { t = String(t ?? ''); return t.length > n ? `${t.slice(0, n)}…` : t; };
  const agHeadTail = (t, head, tail) => { t = String(t ?? ''); return t.length > head + tail + 80 ? `${t.slice(0, head)}\n… [${t.length - head - tail} characters trimmed] …\n${t.slice(-tail)}` : t; };
  const agWsKey = (p) => `astro_agent:ws:${p}`;
  const agAgo = (ts) => { const s = Math.max(0, (Date.now() - ts) / 1000); return s < 60 ? 'just now' : s < 3600 ? `${Math.floor(s / 60)}m ago` : s < 86400 ? `${Math.floor(s / 3600)}h ago` : `${Math.floor(s / 86400)}d ago`; };

  // ---------- sessions ----------
  function agNew(project, host = 'page') {
    const s = { id: `s${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`, project, host, title: 'New session', created: Date.now(), updated: Date.now(),
      status: 'idle', team: AG.team, checkpoint: '', items: [], convo: [], steps: 0, tools: 0, ms: 0, files: 0, pref: 0, fixRound: 0 };
    AG.sessions.set(s.id, s);
    return s;
  }
  function agMeta(s) { return { id: s.id, title: s.title, status: s.status, created: s.created, updated: s.updated, steps: s.steps, tools: s.tools, files: s.files, host: s.host, ms: s.ms }; }
  function agSave(s, now = false) {
    clearTimeout(AG.saveT.get(s.id));
    const run = () => {
      AG.saveT.delete(s.id);
      s.updated = Date.now();
      const data = JSON.stringify(s, (k, v) => (k.startsWith('_') ? undefined : v));
      agApi(s, '/api/ide/agent/save', { id: s.id, data, meta: agMeta(s) }).catch(() => { /* retried on the next save */ });
      if (s.project === AG.project) { const i = AG.list.findIndex((m) => m.id === s.id); const m = agMeta(s); if (i >= 0) AG.list[i] = m; else AG.list.unshift(m); AG.list.sort((a, b) => b.updated - a.updated); agRenderSessions(); }
    };
    if (now) run(); else AG.saveT.set(s.id, setTimeout(run, 700));
  }
  async function agLoadList(project) {
    try { const r = await apiJson('/api/ide/agent/list', { project }); if (project === AG.project) AG.list = r.sessions || []; } catch (_e) { if (project === AG.project) AG.list = []; }
    agRenderSessions();
  }
  async function agGet(project, id) {
    if (!id) return null;
    const have = AG.sessions.get(id); if (have) return have;
    try {
      const r = await apiJson('/api/ide/agent/load', { project, id });
      const s = JSON.parse(r.data);
      if (!s || s.id !== id) return null;
      s.project = project;
      if ((s.status === 'running' || s.status === 'waiting') && !AG.runs.has(s.id)) s.status = 'interrupted';
      for (const it of s.items || []) if (it.status === 'run' || it.status === 'running') it.status = it.kind === 'tool' ? 'err' : 'done';
      AG.sessions.set(id, s);
      return s;
    } catch (_e) { return null; }
  }

  // ---------- parsing the model's reply ----------
  function agJson(body) {
    const src = String(body || '').trim().replace(/^json\s*\n/i, '');
    try { return JSON.parse(src); } catch (_e) { /* repair below */ }
    // Repair raw newlines / tabs inside strings and trailing commas (common with free models).
    let out = ''; let inStr = false; let escp = false;
    for (const ch of src) {
      if (inStr) {
        if (escp) { out += ch; escp = false; continue; }
        if (ch === '\\') { out += ch; escp = true; continue; }
        if (ch === '"') { inStr = false; out += ch; continue; }
        if (ch === '\n') { out += '\\n'; continue; }
        if (ch === '\r') continue;
        if (ch === '\t') { out += '\\t'; continue; }
        out += ch;
      } else { if (ch === '"') inStr = true; out += ch; }
    }
    out = out.replace(/,\s*([}\]])/g, '$1');
    try { return JSON.parse(out); } catch (_e) { /* give up */ }
    const m = out.match(/\{[\s\S]*\}/); if (m && m[0] !== out) { try { return JSON.parse(m[0]); } catch (_e) { /* no */ } }
    return null;
  }
  function agNorm(c) {
    const o = { ...c }; let t = String(o.tool || o.name || o.action || '').trim().toLowerCase().replace(/[\s-]+/g, '_');
    if (o.arguments && typeof o.arguments === 'object') Object.assign(o, o.arguments);
    if (o.args && typeof o.args === 'object' && !Array.isArray(o.args)) Object.assign(o, o.args);
    t = AG_ALIAS[t] || t; o.tool = t;
    if (t === 'bash') o.command = o.command ?? o.cmd ?? o.script ?? o.code ?? '';
    if (['read_file', 'write_file', 'edit_file', 'delete_file', 'present', 'list_files'].includes(t)) o.path = cleanPath(o.path ?? o.file ?? o.filename ?? o.file_path ?? o.target ?? '');
    if (t === 'write_file') o.content = o.content ?? o.text ?? o.data ?? o.code ?? '';
    if (t === 'edit_file') { o.old_text = o.old_text ?? o.old_str ?? o.old ?? o.search ?? o.find ?? ''; o.new_text = o.new_text ?? o.new_str ?? o.new ?? o.replace ?? o.replacement ?? ''; }
    if (t === 'search' || t === 'web_search') o.query = o.query ?? o.q ?? o.pattern ?? o.text ?? '';
    if (t === 'fetch_page') o.url = o.url ?? o.link ?? o.href ?? '';
    if (t === 'start_server') o.command = o.command ?? o.cmd ?? '';
    if (t === 'ask_user') { o.question = o.question ?? o.text ?? o.prompt ?? ''; if (!Array.isArray(o.options)) o.options = []; }
    if (t === 'remember') o.fact = o.fact ?? o.text ?? o.memory ?? '';
    return o;
  }
  function agParse(text) {
    const calls = []; let done = null; let narrative = ''; let last = 0; let m;
    const re = /```([^\n`]*)\n([\s\S]*?)(```|$)/g;
    while ((m = re.exec(text))) {
      const info = m[1].trim(); const body = m[2]; const closed = m[3] === '```'; const low = info.toLowerCase();
      narrative += text.slice(last, m.index); last = re.lastIndex;
      if (low === 'tool' || low === 'tool_call' || low === 'json tool' || low.startsWith('tool ')) {
        const c = agJson(body);
        calls.push(c && (c.tool || c.name) ? agNorm(c) : { tool: '_bad', raw: body.slice(0, 600), cut: !closed });
      } else if (low === 'done' || low.startsWith('done ')) {
        done = agJson(body) || { summary: body.trim().slice(0, 300) };
      } else {
        const path = (info.match(/(?:path|file|filename)\s*[=:]\s*["']?([^\s"']+)/i) || [])[1];
        if (path) calls.push(closed ? { tool: 'write_file', path: cleanPath(path), content: body } : { tool: '_bad', raw: `file block for ${path}`, cut: true });
        else if ((low === 'json' || low === '') && /"tool"\s*:/.test(body)) { const c = agJson(body); calls.push(c && c.tool ? agNorm(c) : { tool: '_bad', raw: body.slice(0, 600), cut: !closed }); }
        else narrative += m[0];
      }
      if (!closed) break;
    }
    if (last < text.length) narrative += text.slice(last);
    if (!calls.length && !done) {
      const mm = text.match(/\{\s*"tool"\s*:\s*"[a-z_]+"[\s\S]*?\}\s*(?=$|\n)/);
      if (mm) { const c = agJson(mm[0]); if (c?.tool) { calls.push(agNorm(c)); narrative = narrative.replace(mm[0], ''); } }
    }
    return { calls, done, text: narrative.replace(/\n{3,}/g, '\n\n').trim() };
  }
  // Narrative shown while a reply streams: hide half-written tool/file blocks.
  function agLiveText(text) {
    const parsed = agParse(text);
    const open = (text.match(/```/g) || []).length % 2 === 1;
    return { text: parsed.text.replace(/```[^\n`]*\n[\s\S]*$/, '').trim(), writing: open, calls: parsed.calls.length };
  }

  // ---------- one model turn (best model first, automatic fallback) ----------
  function agCands() {
    let ready = [];
    try { ready = pickMinds(Infinity).filter((m) => !m.blocked && m.kind !== 'local'); } catch (_e) { /* models not loaded */ }
    return rankLead(ready).slice(0, 6);
  }
  function agCompact(s) {
    const c = s.convo; if (!c.length) return [];
    const KEEP = 12; // the last 6 exchanges stay complete
    let tailStart = Math.max(1, c.length - KEEP);
    if (c[tailStart]?.role !== 'assistant') tailStart = Math.min(c.length - 1, tailStart + 1);
    const older = c.slice(1, tailStart);
    let head = c[0].content;
    if (older.length) {
      const lines = [];
      for (let i = 0; i < older.length; i++) {
        const msg = older[i];
        if (msg.role === 'assistant') { const p = agParse(msg.content); lines.push(`- You: ${agClip(p.text.replace(/\s+/g, ' '), 220)}${p.calls.length ? ` → ${p.calls.map(agCallShort).join('; ')}` : ''}`); }
        else lines.push(`  Results: ${agClip(msg.content.replace(/^TOOL RESULTS:\s*/, '').replace(/\s+/g, ' '), 260)}`);
      }
      head += `\n\nEARLIER STEPS IN THIS JOB (summary; the files on disk are the source of truth):\n${agClip(lines.join('\n'), 9000)}`;
    }
    const out = [{ role: 'user', content: agClip(head, 22000) }];
    for (const msg of c.slice(tailStart)) out.push({ role: msg.role, content: agClip(msg.content, 20000) });
    return out;
  }
  async function agModelTurn(s, item, signal) {
    const cands = agCands();
    const order = cands.length ? [...cands.slice(s.pref % cands.length), ...cands.slice(0, s.pref % cands.length)].slice(0, 4) : [null];
    let lastErr = '';
    for (let k = 0; k < order.length; k++) {
      if (signal.aborted) return null;
      const m = order[k];
      const d = { id: makeId(), content: '', thinking: '', notice: '', status: 'streaming', requested: m ? (m.route || m.id || m.model) : 'auto', started: performance.now() };
      let last = 0;
      const onUpdate = () => { const now = performance.now(); if (now - last < 160) return; last = now; const lt = agLiveText(d.content); item.text = lt.text; item.writing = lt.writing; item.v++; agPaint(s); };
      try { await streamDraft(d, { messages: agCompact(s), mode: 'coding', language: state.language, agentloop: true, max_tokens: 8000 }, signal, { onUpdate }); } catch (err) { d.error = err.message; }
      if (d.content && d.content.trim().length > 2 && d.status !== 'error') {
        if (k > 0) s.pref = (s.pref + k) % Math.max(1, cands.length);
        item.model = d.model || m?.name || m?.label || ''; item.ms = Math.round(performance.now() - d.started);
        return d.content;
      }
      lastErr = d.error || 'empty answer';
      if (signal.aborted) return null;
    }
    throw new Error(`No model could answer (${lastErr}).`);
  }
  function agCallShort(c) {
    const t = c.tool;
    if (t === 'bash') return `bash \`${agClip(String(c.command).split('\n')[0], 80)}\``;
    if (t === 'write_file' || t === 'edit_file' || t === 'read_file' || t === 'delete_file' || t === 'present') return `${t} ${c.path}`;
    if (t === 'search' || t === 'web_search') return `${t} "${agClip(c.query, 60)}"`;
    if (t === 'fetch_page') return `fetch ${agClip(c.url, 80)}`;
    if (t === 'start_server') return `start_server \`${agClip(c.command, 60)}\``;
    return t;
  }

  // ---------- tools ----------
  async function agRead(s, path) { const r = await agApi(s, '/api/fs/read', { path }); if (r.binary) throw new Error(`${path} is a binary file.`); return r.content ?? ''; }
  async function agReadOrNull(s, path) { try { return await agRead(s, path); } catch (_e) { return null; } }
  function agStats(a, b) {
    if (ws.ide?.diffStats) { try { return ws.ide.diffStats(a || '', b || ''); } catch (_e) { /* fallback */ } }
    const A = new Set(String(a || '').split('\n')); const B = new Set(String(b || '').split('\n'));
    return { added: [...B].filter((l) => !A.has(l)).length, removed: [...A].filter((l) => !B.has(l)).length };
  }
  function agFindLoose(text, needle) {
    const i = text.indexOf(needle); if (i >= 0) return [i, needle.length];
    const t = needle.trim(); if (!t) return null;
    const parts = t.split(/\s+/).map((p) => p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    try { const m = new RegExp(parts.join('\\s+')).exec(text); if (m) return [m.index, m[0].length]; } catch (_e) { /* too long */ }
    return null;
  }
  async function agExec(s, call, item, signal) {
    const t = call.tool; const meta = item.meta = {};
    const need = (v, name) => { if (v === undefined || v === null || String(v).trim() === '') throw new Error(`${t} needs "${name}".`); return v; };
    const pathOk = (p) => { need(p, 'path'); if (!okPath(p)) throw new Error(`Invalid path "${p}" (use a relative path inside the project).`); return p; };
    switch (t) {
      case 'bash': {
        need(call.command, 'command');
        const r = await agApi(s, '/api/ide/exec', { command: String(call.command), timeout: Number(call.timeout) || 120, cwd: call.cwd || '' });
        Object.assign(meta, { code: r.code, ms: r.ms, timedOut: r.timed_out });
        item.out = agClip(r.out, 40000);
        item.ok = r.code === 0;
        agSyncWs(s, null);
        return `exit code ${r.code}${r.timed_out ? ` (stopped: timed out after ${r.timeout}s — use start_server for long-running processes)` : ''} · ${r.ms} ms\n${agHeadTail(r.out || '(no output)', 1500, 5000)}`;
      }
      case 'read_file': {
        const p = pathOk(call.path);
        const text = await agRead(s, p);
        const lines = text.split('\n'); const off = Math.max(1, Number(call.offset) || 1); const lim = Math.min(2000, Math.max(1, Number(call.limit) || 400));
        const slice = lines.slice(off - 1, off - 1 + lim);
        meta.lines = lines.length; meta.path = p;
        item.out = slice.slice(0, 80).join('\n');
        const body = slice.map((l, i) => `${String(off + i).padStart(4)}| ${l}`).join('\n');
        return `${p} (${lines.length} lines${off > 1 || lines.length > off - 1 + lim ? `, showing ${off}-${Math.min(lines.length, off - 1 + lim)}` : ''})\n${agClip(body, 14000)}`;
      }
      case 'write_file': {
        const p = pathOk(call.path);
        const content = String(call.content ?? '').replace(/\n?$/, '\n');
        const old = await agReadOrNull(s, p);
        await agApi(s, '/api/fs/write', { path: p, content });
        const st = agStats(old ?? '', content);
        Object.assign(meta, { path: p, created: old === null, added: st.added, removed: st.removed, lines: content.split('\n').length - 1 });
        item.out = agClip(content, 30000);
        agSyncWs(s, [p]);
        return `${old === null ? 'created' : 'rewrote'} ${p} (${meta.lines} lines, +${st.added} −${st.removed})`;
      }
      case 'edit_file': {
        const p = pathOk(call.path); need(call.old_text, 'old_text');
        const text = await agRead(s, p);
        const hit = agFindLoose(text, String(call.old_text));
        if (!hit) throw new Error(`old_text was not found in ${p}. Use read_file to see the current text and copy it exactly (or rewrite the whole file).`);
        const next = text.slice(0, hit[0]) + String(call.new_text ?? '') + text.slice(hit[0] + hit[1]);
        await agApi(s, '/api/fs/write', { path: p, content: next });
        const st = agStats(text, next);
        Object.assign(meta, { path: p, added: st.added, removed: st.removed });
        item.out = `- ${String(call.old_text).split('\n').join('\n- ')}\n+ ${String(call.new_text ?? '').split('\n').join('\n+ ')}`;
        agSyncWs(s, [p]);
        const line = text.slice(0, hit[0]).split('\n').length;
        return `edited ${p} at line ${line} (+${st.added} −${st.removed})`;
      }
      case 'delete_file': {
        const p = pathOk(call.path);
        await agApi(s, '/api/fs/delete', { path: p }); meta.path = p; agSyncWs(s, [p]);
        return `deleted ${p}`;
      }
      case 'list_files': {
        const r = await agApi(s, '/api/fs/tree', {});
        const pre = call.path && call.path !== '.' ? `${String(call.path).replace(/\/+$/, '')}/` : '';
        const files = (r.files || []).filter((f) => !pre || f.path.startsWith(pre));
        meta.count = files.length;
        item.out = files.slice(0, 300).map((f) => `${f.path}  (${fmtBytes(f.size || 0)})`).join('\n');
        return files.length ? `${files.length} files${files.length > 400 ? ' (first 400)' : ''}:\n${files.slice(0, 400).map((f) => `${f.path} (${fmtBytes(f.size || 0)})`).join('\n')}` : (pre ? `No files under ${pre}` : 'The project is empty.');
      }
      case 'search': {
        need(call.query, 'query');
        const r = await agApi(s, '/api/ide/search', { query: String(call.query), regex: Boolean(call.regex) });
        const lines = []; for (const f of r.files || []) for (const h of f.hits || []) lines.push(`${f.path}:${h.line}: ${agClip(String(h.text).trim(), 200)}`);
        meta.count = r.matches || lines.length; item.out = lines.slice(0, 120).join('\n');
        return lines.length ? `${meta.count} matches${r.truncated ? ' (truncated)' : ''}:\n${lines.slice(0, 80).join('\n')}` : 'No matches.';
      }
      case 'web_search': {
        need(call.query, 'query');
        const r = await apiJson('/api/web/search', { q: String(call.query) });
        const res = (r.results || []).slice(0, 8);
        meta.count = res.length; meta.results = res.map((x) => ({ title: x.title, url: x.url }));
        item.out = res.map((x, i) => `${i + 1}. ${x.title}\n   ${x.url}\n   ${x.snippet || ''}`).join('\n');
        return res.length ? res.map((x, i) => `${i + 1}. ${x.title} — ${x.url}\n   ${agClip(x.snippet || '', 300)}`).join('\n') : 'No results.';
      }
      case 'fetch_page': {
        need(call.url, 'url');
        const r = await apiJson('/api/web/read', { url: String(call.url) });
        meta.title = r.title; meta.url = r.url || call.url;
        item.out = agClip(r.text, 4000);
        return `${r.title || ''} — ${r.url || call.url}\n${agClip(r.text || '', 9000)}`;
      }
      case 'start_server': {
        need(call.command, 'command');
        await agApi(s, '/api/ide/proc/start', { cmd: String(call.command) });
        let st = null;
        for (let i = 0; i < 24 && !signal.aborted; i++) { await wsSleep(500); st = await agApi(s, '/api/ide/proc/tail', { bytes: 4000 }).catch(() => null); if (st && (!st.running || st.listening > 0) && i >= 3) break; }
        st = st || {};
        Object.assign(meta, { url: st.url || '', port: st.listening || st.port || 0, running: Boolean(st.running) });
        item.out = st.log || '';
        item.ok = Boolean(st.running);
        if (st.running && meta.url && s.project === AG.project && AG.cur === s.id) { AG.prev = meta.url; if (AG.side === 'preview') agRenderSide(); }
        agSyncWs(s, null);
        if (s.project === ws.project && $('#ws-view')?._mounted) wsProcRefresh().catch(() => {});
        if (!st.running) return `The server exited (code ${st.exit ?? '?'}${st.note ? `, ${st.note}` : ''}).\nLog:\n${agHeadTail(st.log || '(no output)', 1000, 3000)}`;
        return `running: ${call.command}\n${st.listening ? `listening on port ${st.listening} — verify with: curl -s http://127.0.0.1:${st.listening}/` : `not listening yet (expected port ${st.port}); check server_logs`}\npreview: ${st.url}\nLog:\n${agHeadTail(st.log || '(no output yet)', 800, 2500)}`;
      }
      case 'server_logs': {
        const st = await agApi(s, '/api/ide/proc/tail', { bytes: 8000 });
        item.out = st.log || ''; meta.running = Boolean(st.running);
        return `${st.running ? `running (port ${st.listening || st.port})` : 'no server running'}\n${agHeadTail(st.log || '(no output)', 1000, 5000)}`;
      }
      case 'stop_server': { await agApi(s, '/api/ide/proc/stop', {}); return 'server stopped'; }
      case 'ask_user': {
        need(call.question, 'question');
        item.question = String(call.question); item.options = (call.options || []).map(String).slice(0, 6);
        s.status = 'waiting'; item.v++; agPaint(s); agSave(s);
        const answer = await new Promise((resolve) => { AG.waiters.set(item.id, resolve); signal.addEventListener('abort', () => resolve(null), { once: true }); });
        AG.waiters.delete(item.id);
        s.status = 'running';
        if (answer === null) throw new Error('Stopped while waiting for the answer.');
        item.answer = answer;
        return `The user answered: ${answer}`;
      }
      case 'remember': {
        need(call.fact, 'fact');
        const fact = String(call.fact).replace(/\s+/g, ' ').trim().slice(0, 300);
        let mem = await agReadOrNull(s, '.workspace/memory.md');
        if (mem === null) mem = '# Project memory\n\nArchitecture decisions, conventions and preferences for this project. The AI team reads this before every task and adds lines it learns.\n';
        await agApi(s, '/api/fs/write', { path: '.workspace/memory.md', content: `${mem.replace(/\n*$/, '\n')}- ${fact} (${todayIso()})\n` });
        meta.fact = fact; agSyncWs(s, ['.workspace/memory.md']);
        return `saved to project memory: ${fact}`;
      }
      case 'present': {
        const p = pathOk(call.path);
        await agApi(s, '/api/fs/read', { path: p });
        meta.path = p;
        if (s.project === AG.project && AG.cur === s.id) { if (WEB_EXT.has(extOf(p))) { AG.prev = previewUrlFor(s.project, p); agSetSide('preview'); } else agViewFile(p); }
        return `shown to the user: ${p}`;
      }
      case '_bad':
        throw new Error(call.cut ? 'Your reply was cut off in the middle of a tool call or file (too long). Write big files in smaller parts (several files, or edit_file), then continue.'
          : `Could not read this tool call as JSON: ${agClip(call.raw, 200)}. Put exactly one JSON object in each \`\`\`tool block.`);
      default:
        throw new Error(`Unknown tool "${t}". Tools: ${Object.keys(AG_TOOLS).filter((k) => k !== '_bad').join(', ')}.`);
    }
  }
  const previewUrlFor = (project, path) => `/preview/${ws.key}/${encodeURIComponent(project)}/${String(path).split('/').map(encodeURIComponent).join('/')}`;

  // Keep the Workspace editor and file tree in sync with what the agent changes.
  let agSyncT = 0; let agSyncPaths = new Set(); let agSyncAll = false;
  function agSyncWs(s, paths) {
    if (s.project !== ws.project || !$('#ws-view')?._mounted) return;
    if (paths) for (const p of paths) agSyncPaths.add(p); else agSyncAll = true;
    clearTimeout(agSyncT);
    agSyncT = setTimeout(async () => {
      const all = agSyncAll; const set = agSyncPaths; agSyncAll = false; agSyncPaths = new Set();
      await wsLoadTree();
      for (const o of ws.open) {
        if (o.binary || o.image || o.content !== o.saved || (!all && !set.has(o.path))) continue;
        const r = await wsApi('/api/fs/read', { path: o.path }).catch(() => null);
        if (!r || r.binary || r.content === undefined || r.content === o.saved) continue;
        o.content = r.content; o.saved = r.content;
        if (ws.cm?.current() === o.path) ws.cm.setDoc(r.content);
      }
      if (ws.side === 'git') wsRenderSide();
    }, 400);
  }

  // ---------- the agent loop ----------
  async function agProjectBrief(s) {
    let files = [];
    try { files = (await agApi(s, '/api/fs/tree', {})).files || []; } catch (_e) { /* empty */ }
    const mem = await agReadOrNull(s, '.workspace/memory.md');
    const list = files.slice(0, 120).map((f) => `${f.path} (${fmtBytes(f.size || 0)})`).join('\n');
    return { count: files.length, text: `PROJECT "${s.project}" — ${files.length ? `${files.length} files${files.length > 120 ? ' (first 120)' : ''}:\n${list}` : 'empty folder'}${mem ? `\n\nPROJECT MEMORY (obey):\n${agClip(mem, 3000)}` : ''}` };
  }
  async function agTeamDraft(s, item, content, signal) {
    const d = { id: makeId(), content: '', thinking: '', notice: '', status: 'streaming', requested: 'auto', started: performance.now() };
    item._d = d; item.status = 'running'; item.v++; agPaint(s);
    try { await runBrainDraft(d, { messages: [{ role: 'user', content }], mode: 'coding', language: state.language }, signal); } catch (err) { d.error = err.message; }
    item._d = null;
    item.models = d.brain && !d.brain.single ? d.brain.minds.filter((m) => m.status === 'done' || m.status === 'partial').length : (d.content ? 1 : 0);
    item.ms = Math.round(performance.now() - d.started);
    return stripConfidence(d.content || '').trim();
  }
  function agPush(s, it) { it.id = it.id || agId(); it.v = 1; s.items.push(it); agPaint(s); return it; }

  async function agSend(s, text) {
    if (agBusy(s)) { toast('The agent is still working'); return; }
    text = String(text || '').trim(); if (!text) return;
    if (!s.items.length) s.title = agClip(text.replace(/\s+/g, ' '), 70);
    agPush(s, { kind: 'user', text });
    await agRun(s, text);
  }
  async function agRun(s, text, opts = {}) {
    const ctrl = new AbortController(); AG.runs.set(s.id, ctrl);
    const signal = ctrl.signal;
    s.status = 'running'; s._started = performance.now(); agSave(s, true); agPaint(s); agTicker();
    let finished = false; let report = '';
    try {
      if (!s.checkpoint) {
        try { const r = await agApi(s, '/api/ide/history/create', { label: `Before agent: ${agClip(text.replace(/\s+/g, ' '), 90)}`, kind: 'ai' }); s.checkpoint = r.id || ''; } catch (_e) { /* no undo point */ }
      }
      if (!opts.resume) {
        const brief = await agProjectBrief(s);
        let plan = '';
        if (s.team && agCands().length >= 2) {
          const pi = agPush(s, { kind: 'plan', text: '', status: 'running' });
          plan = await agTeamDraft(s, pi, `You are planning a job for Astro AI's autonomous agent. The agent works inside the project folder with tools: bash, read/write/edit files, list and search files, web search, fetch pages, start a server, ask the user, present a result. Write the plan it will follow:\n1. Goal and acceptance criteria: what "done" means and exactly how to verify it.\n2. Numbered steps (3 to 8), each concrete: what to inspect, create, change, install, run or test.\n3. Pitfalls to avoid.\nAt most 250 words, no code.\n\nJOB: ${text}\n\n${brief.text}`, signal);
          pi.text = plan || (signal.aborted ? '' : 'The team could not write a plan, so the agent plans as it goes.');
          pi.status = plan ? 'done' : 'skipped'; pi.v++; agPaint(s); agSave(s);
        }
        if (signal.aborted) throw new DOMException('stopped', 'AbortError');
        const prior = s.convo.length ? `\n\nThis continues an earlier job in the same session; the earlier steps are summarised below.` : '';
        const job = `JOB: ${text}${prior}\n\n${brief.text}${plan ? `\n\nTEAM PLAN (written by Astro AI's team of models):\n${plan}` : ''}\n\nStart now: act with tools, verify, and finish with the done block.`;
        if (s.convo.length) s.convo.push({ role: 'user', content: job }); else s.convo = [{ role: 'user', content: job }];
      }
      const limit = opts.limit || AG_MAX;
      let noCall = 0; let fails = new Map();
      for (let step = 0; step < limit; step++) {
        if (signal.aborted) throw new DOMException('stopped', 'AbortError');
        const say = agPush(s, { kind: 'say', text: '', live: true, step: s.steps + 1 });
        let reply;
        try { reply = await agModelTurn(s, say, signal); } catch (err) { say.live = false; s.items.splice(s.items.indexOf(say), 1); throw err; }
        if (reply === null) throw new DOMException('stopped', 'AbortError');
        s.steps++;
        const parsed = agParse(reply);
        say.text = parsed.text; say.live = false; say.writing = false; say.v++;
        if (!say.text && parsed.calls.length) { s.items.splice(s.items.indexOf(say), 1); }
        s.convo.push({ role: 'assistant', content: reply });
        if (!parsed.calls.length) {
          if (parsed.done || ++noCall >= 2) { finished = true; report = parsed.text || parsed.done?.summary || ''; if (say.text && parsed.done) say.final = true; break; }
          s.convo.push({ role: 'user', content: 'TOOL RESULTS: (none — your reply had no tool call.) Continue the job with tool calls in ```tool blocks, or, if the job is completely done and verified, reply with the report and the ```done block.' });
          agPaint(s); continue;
        }
        noCall = 0;
        const results = [];
        for (const [i, call] of parsed.calls.slice(0, 6).entries()) {
          if (signal.aborted) break;
          const ti = agPush(s, { kind: 'tool', tool: call.tool, args: agArgsForView(call), status: 'run', open: false });
          const t0 = performance.now();
          let res = '';
          try {
            res = await agExec(s, call, ti, signal);
            ti.status = ti.ok === false ? 'err' : 'ok';
          } catch (err) {
            if (err?.name === 'AbortError' || signal.aborted) { ti.status = 'err'; ti.error = 'Stopped'; ti.v++; break; }
            ti.status = 'err'; ti.error = err.message || String(err); res = `ERROR: ${ti.error}`;
          }
          ti.ms = Math.round(performance.now() - t0);
          if (ti.status === 'err' && (call.tool === 'bash' || ti.error)) ti.open = true;
          if (call.tool === 'ask_user' || call.tool === 'present') ti.open = true;
          ti.v++; s.tools++;
          if (['write_file', 'edit_file', 'delete_file', 'bash', 'remember'].includes(call.tool)) agChangesDirty(s);
          const sig = `${call.tool}|${call.command || call.path || ''}`;
          if (ti.status === 'err') { const n = (fails.get(sig) || 0) + 1; fails.set(sig, n); if (n >= 3) res += '\nNOTE: this exact action has now failed 3 times. Stop repeating it: read the error, change the approach.'; }
          results.push(`### [${i + 1}] ${agCallShort(call)}\n${res}`);
          agPaint(s); agSave(s);
        }
        if (parsed.calls.length > 6) results.push(`(Only the first 6 tool calls ran; send the rest in the next reply.)`);
        s.convo.push({ role: 'user', content: `TOOL RESULTS:\n${results.join('\n\n')}` });
        if (parsed.done && !signal.aborted) { finished = true; report = parsed.text; if (say.text) say.final = true; break; }
      }
      if (!finished && !signal.aborted) {
        agPush(s, { kind: 'note', text: `Reached the limit of ${limit} steps for one run. Press Continue to let the agent keep going.`, cont: true });
        s.status = 'paused';
      }
      // Team review of the finished job (once per run, plus one fix round if the team finds problems).
      if (finished && s.team && !opts.noReview && agCands().length >= 2 && !signal.aborted) {
        const changes = await agChangeList(s);
        if (changes.length) {
          const ri = agPush(s, { kind: 'review', text: '', status: 'running' });
          const prompt = await agReviewPrompt(s, text, report, changes);
          const review = await agTeamDraft(s, ri, prompt, signal);
          const verdict = (review.match(/VERDICT\s*:\s*\**\s*(PASS|FIX)/i) || [])[1]?.toUpperCase() || (review ? 'PASS' : '');
          ri.text = review.replace(/^.*VERDICT\s*:\s*\**\s*(PASS|FIX)\**\s*$/im, '').trim() || (signal.aborted ? '' : 'The team could not review this run.');
          ri.verdict = verdict; ri.status = review ? 'done' : 'skipped'; ri.v++; agPaint(s); agSave(s);
          if (verdict === 'FIX' && s.fixRound < 1 && !signal.aborted) {
            s.fixRound++;
            s.convo.push({ role: 'user', content: `TEAM REVIEW — Astro AI's team of models reviewed your work and found problems:\n${agClip(ri.text, 4000)}\n\nFix every real problem (verify each fix by running or testing), then finish again with the report and the done block. If a point is wrong, say why in the report.` });
            s.ms += Math.round(performance.now() - s._started);
            AG.runs.delete(s.id);
            return agRun(s, text, { resume: true, limit: 16, noReview: true });
          }
        }
      }
      if (finished) {
        const changes = await agChangeList(s);
        s.files = changes.length;
        agPush(s, { kind: 'done', text: report, files: changes.length, steps: s.steps, ms: Math.round(s.ms + performance.now() - s._started) });
        s.status = 'done';
      }
    } catch (err) {
      if (err?.name === 'AbortError' || signal.aborted) { agPush(s, { kind: 'note', text: 'Stopped by you.', cont: true }); s.status = 'stopped'; }
      else { agPush(s, { kind: 'error', text: err.message || String(err), cont: true }); s.status = 'error'; }
    } finally {
      if (AG.runs.get(s.id) === ctrl) {
        AG.runs.delete(s.id);
        s.ms += Math.round(performance.now() - (s._started || performance.now()));
        for (const it of s.items) if (it.live) { it.live = false; it.v++; }
        agSave(s, true); agPaint(s); agChangesDirty(s);
        if (s.status === 'done') toast('Agent finished');
      }
    }
  }
  function agArgsForView(c) {
    const o = {};
    for (const k of ['command', 'path', 'query', 'url', 'question', 'fact', 'timeout', 'cwd', 'offset', 'limit', 'regex']) if (c[k] !== undefined && c[k] !== '') o[k] = typeof c[k] === 'string' ? agClip(c[k], 4000) : c[k];
    if (c.tool === 'edit_file') { o.old_text = agClip(c.old_text, 4000); o.new_text = agClip(c.new_text, 4000); }
    if (c.tool === '_bad') o.raw = c.raw;
    return o;
  }
  async function agChangeList(s) {
    if (!s.checkpoint) return [];
    try { return (await agApi(s, '/api/ide/history/changes', { id: s.checkpoint })).changes || []; } catch (_e) { return []; }
  }
  async function agReviewPrompt(s, job, report, changes) {
    const actions = s.items.filter((it) => it.kind === 'tool').slice(-40).map((it) => `- ${agCallShort({ tool: it.tool, ...it.args })} → ${it.status === 'err' ? `FAILED: ${agClip(it.error || `exit ${it.meta?.code}`, 120)}` : it.tool === 'bash' ? `exit ${it.meta?.code}` : 'ok'}`).join('\n');
    let contents = ''; let budget = 12000;
    for (const c of changes) {
      if (c.status === 'deleted' || budget <= 0 || !/\.(c|cc|cpp|h|hpp|js|mjs|cjs|ts|py|java|go|rs|rb|php|sh|html|css|json|md|txt|yml|yaml|toml|sql)$/i.test(c.path) || c.path.startsWith('.workspace/')) continue;
      const t = await agReadOrNull(s, c.path); if (t === null) continue;
      const chunk = agClip(t, Math.min(3500, budget)); budget -= chunk.length;
      contents += `\n--- ${c.path} (${c.status}) ---\n${chunk}\n`;
    }
    const plan = s.items.filter((it) => it.kind === 'plan' && it.text).map((it) => it.text).pop() || '';
    return `Review the work of Astro AI's autonomous agent and decide whether the job is fully and correctly done.\n\nJOB: ${job}\n${plan ? `\nTEAM PLAN:\n${agClip(plan, 2000)}\n` : ''}\nAGENT REPORT:\n${agClip(report || '(none)', 2500)}\n\nACTIONS (latest last):\n${actions}\n\nFILES CHANGED (${changes.length}): ${changes.map((c) => `${c.path} [${c.status}]`).join(', ')}\n\nCURRENT CONTENT OF CHANGED FILES:${contents || ' (none readable)'}\n\nReply with the first line exactly "VERDICT: PASS" or "VERDICT: FIX". Then at most 8 short bullets: real problems (bugs, missing requirements, claims that were not verified, broken run commands) and how to fix each. Use FIX only for problems that matter; for PASS you may add brief suggestions.`;
  }
  function agStop(s) { const c = AG.runs.get(s?.id); if (c) c.abort(); }
  async function agContinue(s) {
    if (agBusy(s)) return;
    if (!s.convo.length) { toast('Nothing to continue'); return; }
    const lastUser = [...s.items].reverse().find((it) => it.kind === 'user')?.text || s.title;
    s.convo.push({ role: 'user', content: 'Continue the job from where you stopped: check the current state with tools, finish the remaining work, verify it, then end with the report and the done block.' });
    await agRun(s, lastUser, { resume: true });
  }
  function agTicker() {
    if (AG.tick) return;
    AG.tick = setInterval(() => {
      if (!AG.runs.size) { clearInterval(AG.tick); AG.tick = 0; agRenderHead(); return; }
      for (const id of AG.runs.keys()) {
        const s = AG.sessions.get(id); if (!s) continue;
        for (const it of s.items) if (it._d) for (const box of agHosts(s)) { const el = box.querySelector(`[data-brain="${it.id}"]`); if (el) paintBrain(el, it._d); }
      }
      agRenderHead(); agWsHint();
    }, 400);
  }

  // ---------- timeline rendering (shared by the Agent page and the Workspace panel) ----------
  function agHosts(s) {
    const out = [];
    const page = $('#ag-tl'); if (page && AG.cur === s.id && AG.project === s.project && state.view === 'agent') out.push(page);
    const wbox = $('#ws-team-msgs'); if (wbox && ws.agentMode && ws.project === s.project && ls.get(agWsKey(ws.project)) === s.id) out.push(wbox);
    return out;
  }
  function agPaint(s) {
    for (const box of agHosts(s)) agRenderTl(box, s);
    if (AG.cur === s.id) { agRenderHead(); if (AG.side === 'terminal') agRenderSide(); }
    agWsHint();
  }
  function agToolSummary(it) {
    const a = it.args || {};
    if (it.tool === 'bash') return `$ ${String(a.command || '').split('\n')[0]}`;
    if (it.tool === 'start_server') return a.command || '';
    if (a.path) return a.path;
    if (a.query) return `“${a.query}”`;
    if (a.url) return hostOf(a.url) + agClip(String(a.url).replace(/^https?:\/\/[^/]+/, ''), 40);
    if (a.question) return a.question;
    if (a.fact) return a.fact;
    return '';
  }
  function agToolStat(it) {
    const m = it.meta || {};
    if (it.status === 'run') return '';
    if (it.tool === 'write_file' || it.tool === 'edit_file') return `${m.created ? '<span class="ag-new">new</span>' : ''}${m.added !== undefined ? `<span class="dstat add">+${m.added}</span><span class="dstat del">−${m.removed}</span>` : ''}`;
    if (it.tool === 'bash' && m.code !== undefined) return `<span class="ag-code-x${m.code === 0 ? '' : ' bad'}">${m.timedOut ? 'timeout' : `exit ${m.code}`}</span>`;
    if (it.tool === 'read_file' && m.lines) return `<span>${m.lines} lines</span>`;
    if ((it.tool === 'search' || it.tool === 'web_search' || it.tool === 'list_files') && m.count !== undefined) return `<span>${m.count}</span>`;
    return '';
  }
  function agToolBody(s, it) {
    const a = it.args || {}; const m = it.meta || {}; let h = '';
    if (it.error) h += `<div class="ag-err">${esc(it.error)}</div>`;
    if (it.tool === 'bash') h += `<pre class="ag-pre"><span class="ag-ps">$ ${esc(a.command || '')}</span>\n${esc(it.out || (it.status === 'run' ? 'running…' : ''))}</pre>`;
    else if (it.tool === 'write_file' && it.out) h += `<pre class="ag-pre code">${HL(agClip(it.out, 12000), extOf(a.path))}</pre>`;
    else if (it.tool === 'edit_file' && it.out) h += `<pre class="ag-pre diff">${esc(it.out).split('\n').map((l) => `<span class="${l.startsWith('+') ? 'l-add' : l.startsWith('-') ? 'l-del' : ''}">${l}</span>`).join('\n')}</pre>`;
    else if (it.tool === 'web_search' && m.results) h += `<ol class="ag-links">${m.results.map((r) => `<li><a href="${esc(r.url)}" target="_blank" rel="noopener noreferrer">${esc(r.title || r.url)}</a><small>${esc(hostOf(r.url))}</small></li>`).join('')}</ol>`;
    else if (it.tool === 'ask_user') {
      if (it.answer !== undefined) h += `<div class="ag-answer">${icon('i-check')}<span>${esc(it.answer)}</span></div>`;
      else if (it.status === 'run') h += `<div class="ag-ask"><div class="ag-q">${esc(it.question || a.question || '')}</div>${(it.options || []).length ? `<div class="ag-opts">${it.options.map((o) => `<button type="button" class="ws-chip" data-agans="${esc(o)}" data-ag-item="${it.id}">${esc(o)}</button>`).join('')}</div>` : ''}<form class="ag-ans-form" data-ag-item="${it.id}"><input type="text" placeholder="Type your answer…" aria-label="Your answer"><button type="submit" class="ws-chip">Send</button></form></div>`;
    } else if (it.out) h += `<pre class="ag-pre">${esc(agClip(it.out, 12000))}</pre>`;
    const acts = [];
    if ((it.tool === 'write_file' || it.tool === 'edit_file') && it.status === 'ok') acts.push(`<button type="button" class="ag-act" data-agdiff="${esc(a.path)}">${icon('i-diff')}View diff</button>`, `<button type="button" class="ag-act" data-agopen="${esc(a.path)}">${icon('i-file')}Open file</button>`);
    if (it.tool === 'present' && m.path) acts.push(`<button type="button" class="ag-act" data-agopen="${esc(m.path)}">${icon('i-eye')}Open ${esc(baseName(m.path))}</button>`);
    if (it.tool === 'start_server' && m.url) acts.push(`<button type="button" class="ag-act" data-agprev="${esc(m.url)}">${icon('i-eye')}Open preview</button>`);
    if (acts.length) h += `<div class="ag-acts">${acts.join('')}</div>`;
    return h;
  }
  function agItemHtml(s, it) {
    if (it.kind === 'user') return `<div class="ag-it ag-user" data-agi="${it.id}"><div class="ag-bubble" dir="auto">${esc(it.text)}</div></div>`;
    if (it.kind === 'say') {
      const md = it.text ? `<div class="md ag-md" dir="auto">${renderMarkdown(it.text)}</div>` : '';
      const live = it.live ? `<div class="ag-thinking"><span class="ag-spin"></span>${it.writing ? 'Writing a file or tool call…' : it.text ? 'Thinking…' : 'Thinking about the next step…'}</div>` : '';
      return `<div class="ag-it ag-say${it.final ? ' final' : ''}" data-agi="${it.id}">${md}${live}${!it.live && it.model ? `<div class="ag-via" title="Model that wrote this step">${esc(it.model)}${it.ms ? ` · ${fmtSecs(it.ms)}` : ''}</div>` : ''}</div>`;
    }
    if (it.kind === 'tool') {
      const [ic, verb] = AG_TOOLS[it.tool] || ['i-bolt', it.tool];
      const st = it.status === 'run' ? '<span class="ag-spin"></span>' : it.status === 'ok' ? `<span class="ag-ok">${icon('i-check')}</span>` : `<span class="ag-bad">${icon('i-x')}</span>`;
      const open = it.open || (it.tool === 'ask_user' && it.status === 'run');
      return `<div class="ag-it ag-tool st-${it.status}${open ? ' open' : ''}" data-agi="${it.id}"><button type="button" class="ag-th" data-agtoggle="${it.id}" aria-expanded="${open}"><span class="ag-ti">${icon(ic)}</span><b>${esc(it.status === 'run' ? AG_RUNNING[it.tool] || verb : verb)}</b><code dir="auto">${esc(agClip(agToolSummary(it), 160))}</code><span class="ag-stat">${agToolStat(it)}${it.ms && it.status !== 'run' && it.tool !== 'ask_user' ? `<span class="ag-ms">${fmtSecs(it.ms)}</span>` : ''}</span>${st}<span class="ag-chev">${icon('i-chev')}</span></button>${open ? `<div class="ag-tb">${agToolBody(s, it)}</div>` : ''}</div>`;
    }
    if (it.kind === 'plan' || it.kind === 'review') {
      const title = it.kind === 'plan' ? 'Team plan' : 'Team review';
      const chip = it.kind === 'review' && it.verdict ? `<span class="ag-verdict ${it.verdict === 'PASS' ? 'ok' : 'fix'}">${it.verdict === 'PASS' ? 'Approved' : 'Fixes needed'}</span>` : '';
      const sub = it.status === 'running' ? '' : it.models ? `${it.models} model${it.models === 1 ? '' : 's'} · ${fmtSecs(it.ms || 0)}` : '';
      return `<div class="ag-it ag-card ag-${it.kind}${it.open === false ? ' shut' : ''}" data-agi="${it.id}"><button type="button" class="ag-ch" data-agcard="${it.id}"><span class="ag-ci">${icon(it.kind === 'plan' ? 'i-plan' : 'i-check')}</span><b>${title}</b>${chip}<span class="ag-sub">${esc(sub)}</span><span class="ag-chev">${icon('i-chev')}</span></button>${it.status === 'running' ? `<div class="ag-brain" data-brain="${it.id}"></div>` : it.text ? `<div class="md ag-md ag-cb" dir="auto">${renderMarkdown(it.text)}</div>` : ''}</div>`;
    }
    if (it.kind === 'done') return `<div class="ag-it ag-done" data-agi="${it.id}"><div class="ag-done-h"><span class="ag-ok">${icon('i-check')}</span><b>Done</b><span class="ag-sub">${it.steps} steps · ${fmtSecs(it.ms || 0)}</span>${it.files ? `<button type="button" class="ag-act" data-agchanges>${icon('i-diff')}${it.files} file${it.files === 1 ? '' : 's'} changed</button>` : ''}</div>${it.text ? `<div class="md ag-md" dir="auto">${renderMarkdown(it.text)}</div>` : ''}</div>`;
    if (it.kind === 'error') return `<div class="ag-it ag-note bad" data-agi="${it.id}">${icon('i-x')}<span>${esc(it.text)}</span></div>`;
    return `<div class="ag-it ag-note" data-agi="${it.id}">${icon('i-hand')}<span>${esc(it.text)}</span></div>`;
  }
  function agFootHtml(s) {
    if (agBusy(s)) return '';
    const canCont = ['stopped', 'error', 'paused', 'interrupted'].includes(s.status) && s.convo.length;
    const undo = s.checkpoint && s.items.some((it) => it.kind === 'tool' && ['write_file', 'edit_file', 'delete_file', 'bash'].includes(it.tool));
    if (!canCont && !undo) return '';
    return `<div class="ag-foot">${s.status === 'interrupted' ? '<span>This run was interrupted (the page was closed or reloaded).</span>' : ''}${canCont ? `<button type="button" class="ws-chip" data-agcont>${icon('i-play')}Continue</button>` : ''}${undo ? `<button type="button" class="ws-chip ghost" data-agundo>${icon('i-undo')}Undo all changes</button>` : ''}</div>`;
  }
  function agEmptyHtml(where) {
    const sugg = [
      ['Build a to-do web app with a Node/Express API and a polished page, then start it and test the API with curl', 'i-server'],
      ['Research the latest stable three.js release on the web, then build a rotating 3D galaxy page with it', 'i-globe'],
      ['Write a C++20 program that benchmarks 3 sorting algorithms on 1M ints and prints a table, compile with -O2 and run it', 'i-bolt'],
      ['Inspect this project, find bugs, fix them and verify by running it', 'i-check'],
    ];
    return `<div class="ag-empty"><div class="ag-orb" aria-hidden="true"><i></i><i></i><i></i></div><h3>Astro AI Agent</h3><p>Give it a job. The agent plans with your team of models, then works on its own: it runs commands, writes and edits files, searches the web, starts servers, checks the results and fixes its own errors until the job is done. Every step and every file it touches is shown here${where === 'ws' ? '' : ' and in the side panel'}.</p><div class="ag-sugg">${sugg.map(([t, i]) => `<button type="button" data-agsuggest="${esc(t)}">${icon(i)}<span>${esc(t)}</span></button>`).join('')}</div></div>`;
  }
  function agRenderTl(box, s) {
    if (!s || !s.items.length) { box.innerHTML = agEmptyHtml(box.id === 'ws-team-msgs' ? 'ws' : 'page'); box._sid = ''; box._nodes = null; return; }
    if (box._sid !== s.id || !box._nodes || [...box._nodes.values()].some((n) => n.parentNode !== box)) { box.innerHTML = ''; box._sid = s.id; box._nodes = new Map(); }
    const near = box.scrollHeight - box.scrollTop - box.clientHeight < 180;
    const nodes = box._nodes; const alive = new Set();
    let prev = null;
    for (const it of s.items) {
      alive.add(it.id);
      let n = nodes.get(it.id);
      const key = `${it.v}|${it.open ? 1 : 0}`;
      if (!n || n._key !== key) {
        const tpl = document.createElement('template'); tpl.innerHTML = agItemHtml(s, it).trim();
        const fresh = tpl.content.firstElementChild; fresh._key = key;
        if (n) n.replaceWith(fresh); else if (prev) prev.after(fresh); else box.prepend(fresh);
        nodes.set(it.id, fresh); n = fresh;
        if (it._d) { const el = fresh.querySelector('[data-brain]'); if (el) paintBrain(el, it._d); }
      }
      prev = n;
    }
    for (const [id, n] of nodes) if (!alive.has(id)) { n.remove(); nodes.delete(id); }
    let foot = box.querySelector(':scope > .ag-foot-wrap');
    if (!foot) { foot = document.createElement('div'); foot.className = 'ag-foot-wrap'; }
    const fh = agFootHtml(s); if (foot._h !== fh) { foot.innerHTML = fh; foot._h = fh; }
    box.append(foot);
    if (near) box.scrollTop = box.scrollHeight;
  }
  function agFindItem(s, id) { return s?.items.find((it) => it.id === id); }
  // Click handling shared by both hosts. `host` is 'page' or 'ws'.
  function agOnClick(e, host) {
    const s = host === 'page' ? AG.sessions.get(AG.cur) : AG.sessions.get(ls.get(agWsKey(ws.project)) || '');
    const sug = e.target.closest('[data-agsuggest]');
    if (sug) { if (host === 'page') agPageSend(sug.dataset.agsuggest); else agWsSend(sug.dataset.agsuggest); return true; }
    if (!s) return false;
    const tg = e.target.closest('[data-agtoggle]');
    if (tg) { const it = agFindItem(s, tg.dataset.agtoggle); if (it) { it.open = !it.open; agPaint(s); } return true; }
    const cd = e.target.closest('[data-agcard]');
    if (cd) { const it = agFindItem(s, cd.dataset.agcard); if (it) { it.open = it.open === false; it.v++; agPaint(s); } return true; }
    const an = e.target.closest('[data-agans]');
    if (an) { const w = AG.waiters.get(an.dataset.agItem); if (w) w(an.dataset.agans); return true; }
    const df = e.target.closest('[data-agdiff]');
    if (df) { const p = df.dataset.agdiff; if (host === 'ws' && s.checkpoint) wsShowDiff({ id: s.checkpoint, path: p, label: 'before the agent' }); else agShowDiff(s, p); return true; }
    const op = e.target.closest('[data-agopen]');
    if (op) { const p = op.dataset.agopen; if (host === 'ws') wsOpen(p); else if (WEB_EXT.has(extOf(p))) { AG.prev = previewUrlFor(s.project, p); agSetSide('preview'); } else agViewFile(p); return true; }
    const pv = e.target.closest('[data-agprev]');
    if (pv) { if (host === 'ws') { wsProcRefresh().catch(() => {}).then(() => { ws.previewMode = 'app'; wsPanel('preview'); }); } else { AG.prev = pv.dataset.agprev; agSetSide('preview'); } return true; }
    if (e.target.closest('[data-agcont]')) { agContinue(s); return true; }
    if (e.target.closest('[data-agundo]')) { agUndoAll(s); return true; }
    if (e.target.closest('[data-agchanges]')) { if (host === 'ws') { const c = [...s.items].reverse().find((it) => it.kind === 'tool' && it.meta?.path && ['write_file', 'edit_file'].includes(it.tool)); if (c && s.checkpoint) wsShowDiff({ id: s.checkpoint, path: c.meta.path, label: 'before the agent' }); else wsSide('history'); } else agSetSide('changes'); return true; }
    return false;
  }
  function agOnSubmit(e) {
    const f = e.target.closest('.ag-ans-form'); if (!f) return false;
    e.preventDefault();
    const v = $('input', f).value.trim(); if (!v) return true;
    const w = AG.waiters.get(f.dataset.agItem); if (w) w(v);
    return true;
  }
  async function agUndoAll(s) {
    if (!s.checkpoint) return;
    if (agBusy(s)) { toast('Stop the agent first'); return; }
    const ok = await wsDialog({ title: 'Undo all changes?', text: 'Every file in the project goes back to how it was before this agent session started. The current state is saved as a version first, so this can be undone from History.', ok: 'Undo all', danger: true });
    if (!ok) return;
    try {
      await agApi(s, '/api/ide/history/restore', { id: s.checkpoint });
      agPush(s, { kind: 'note', text: 'All changes from this session were undone (the project is back to how it was before). The previous state is saved in History.' });
      agSave(s); agChangesDirty(s); agSyncWs(s, null);
      if (s.project === ws.project && typeof wsAfterRestore === 'function') wsAfterRestore();
      toast('Changes undone');
    } catch (err) { toast(err.message, 'error'); }
  }

  // ---------- Agent page ----------
  function agMount() {
    const v = $('#agent-view'); if (!v || v._mounted) return;
    v._mounted = true;
    v.innerHTML = `
      <div class="ag" id="ag">
        <aside class="ag-sessions" id="ag-sessions">
          <button type="button" class="ag-proj" id="ag-proj" title="Project the agent works in">${icon('i-folder')}<b id="ag-proj-name">Project</b>${icon('i-chev')}</button>
          <button type="button" class="ag-newbtn" id="ag-new">${icon('i-plus')}New session</button>
          <div class="ag-slabel">Sessions</div>
          <div class="ag-slist" id="ag-slist"></div>
        </aside>
        <section class="ag-main">
          <header class="ag-head">
            <button type="button" class="ws-icon sm ag-only-narrow" id="ag-show-sessions" title="Sessions">${icon('i-history')}</button>
            <div class="ag-titles"><b id="ag-title">New session</b><span class="ag-status" id="ag-status"></span></div>
            <span class="grow"></span>
            <button type="button" class="ws-chip ghost" id="ag-open-ws" title="Open this project in the Workspace editor">${icon('i-code')}<span>Workspace</span></button>
            <button type="button" class="ws-icon sm" id="ag-side-btn" title="Show or hide the files panel">${icon('i-panel')}</button>
          </header>
          <div class="ag-tl" id="ag-tl" aria-live="polite"></div>
          <form class="ag-comp" id="ag-form">
            <textarea id="ag-in" rows="1" placeholder="Give the agent a job: build, fix, research, test…" aria-label="Message the agent"></textarea>
            <div class="ag-row">
              <button type="button" class="ws-chip ag-team-chip" id="ag-team" title="The whole team of models writes the plan first and reviews the result at the end (slower, stronger). Steps in between run on the best fast model.">${icon('i-brain')}<span>Team plan &amp; review</span></button>
              <span class="ag-hint" id="ag-hint"></span><span class="grow"></span>
              <button type="submit" class="ws-send" id="ag-send" title="Send (Enter)">${icon('i-send')}</button>
            </div>
          </form>
        </section>
        <aside class="ag-side" id="ag-side">
          <div class="ag-tabs" role="tablist">
            <button type="button" data-agtab="changes">${icon('i-diff')}<span>Changes</span><i id="ag-ccount"></i></button>
            <button type="button" data-agtab="files">${icon('i-folder')}<span>Files</span></button>
            <button type="button" data-agtab="terminal">${icon('i-terminal')}<span>Terminal</span></button>
            <button type="button" data-agtab="preview">${icon('i-eye')}<span>Preview</span></button>
          </div>
          <div class="ag-pane" id="ag-pane"></div>
        </aside>
      </div>`;
    $('#ag-new').onclick = () => { const s = agNew(AG.project); AG.cur = s.id; agRenderAll(); $('#ag-in').focus(); };
    $('#ag-proj').onclick = (e) => agProjectMenu(e.currentTarget);
    $('#ag-open-ws').onclick = () => agOpenInWorkspace();
    $('#ag-side-btn').onclick = () => { AG.sideOpen = !AG.sideOpen; $('#ag').classList.toggle('side-off', !AG.sideOpen); };
    $('#ag-show-sessions').onclick = () => $('#ag').classList.toggle('sess-on');
    $('#ag-team').onclick = () => { const s = agCurSession(); AG.team = !AG.team; if (s && !agBusy(s)) s.team = AG.team; ls.set('astro_agent:team', AG.team ? '1' : '0'); agRenderHead(); };
    $$('[data-agtab]', v).forEach((b) => { b.onclick = () => agSetSide(b.dataset.agtab); });
    const tin = $('#ag-in');
    tin.addEventListener('input', () => { tin.style.height = 'auto'; tin.style.height = `${Math.min(200, tin.scrollHeight)}px`; });
    tin.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); $('#ag-form').requestSubmit(); } });
    $('#ag-form').onsubmit = (e) => {
      e.preventDefault();
      const s = agCurSession();
      if (agBusy(s)) { agStop(s); return; }
      const text = tin.value.trim(); if (!text) return;
      tin.value = ''; tin.style.height = 'auto';
      agPageSend(text);
    };
    const tl = $('#ag-tl');
    tl.addEventListener('click', (e) => agOnClick(e, 'page'));
    tl.addEventListener('submit', (e) => agOnSubmit(e));
    $('#ag-slist').addEventListener('click', async (e) => {
      const del = e.target.closest('[data-agdel]');
      if (del) {
        e.stopPropagation();
        const id = del.dataset.agdel; const s = AG.sessions.get(id);
        if (s && agBusy(s)) { toast('Stop the agent first'); return; }
        if (!(await wsDialog({ title: 'Delete this session?', text: 'The session log is deleted. Files the agent created stay in the project.', ok: 'Delete', danger: true }))) return;
        await apiJson('/api/ide/agent/delete', { project: AG.project, id }).catch(() => {});
        AG.sessions.delete(id); AG.list = AG.list.filter((m) => m.id !== id);
        if (AG.cur === id) AG.cur = '';
        agRenderAll(); return;
      }
      const row = e.target.closest('[data-agsess]'); if (!row) return;
      await agSelect(row.dataset.agsess);
      $('#ag').classList.remove('sess-on');
    });
    $('#ag-pane').addEventListener('click', (e) => agSideClick(e));
    $('#ag-pane').addEventListener('submit', (e) => agSideSubmit(e));
  }
  function agCurSession() { return AG.sessions.get(AG.cur) || null; }
  async function agShow() {
    agMount();
    wsMount();
    if (!ws.loaded) { try { await wsLoad(); } catch (_e) { /* shown in the workspace */ } }
    wsEnsureIDE().catch(() => {});
    if (!AG.project) AG.project = ls.get('astro_agent:project') || ws.project || ws.projects?.[0]?.name || '';
    if (AG.project && ws.projects && !ws.projects.some((p) => p.name === AG.project)) AG.project = ws.project || ws.projects[0]?.name || '';
    if (!AG.project) { await agCreateProject('agent-lab'); }
    await agLoadList(AG.project);
    if (!AG.cur || !AG.sessions.has(AG.cur)) {
      const first = AG.list[0];
      if (first) await agSelect(first.id, true); else { const s = agNew(AG.project); AG.cur = s.id; }
    }
    agRenderAll();
    setTimeout(() => $('#ag-in')?.focus(), 50);
  }
  async function agCreateProject(name) {
    let n = name; let i = 2;
    while (ws.projects?.some((p) => p.name === n)) n = `${name}-${i++}`;
    try { await apiJson('/api/fs/project/create', { project: n, starter: false }); ws.projects = [{ name: n, mtime: Date.now() / 1000 }, ...(ws.projects || [])]; AG.project = n; ls.set('astro_agent:project', n); return n; } catch (err) { toast(err.message, 'error'); return ''; }
  }
  async function agSelect(id, quiet = false) {
    const s = await agGet(AG.project, id);
    if (!s) { if (!quiet) toast('That session could not be opened', 'error'); return; }
    AG.cur = s.id; AG.changes = null; AG.view = null;
    agRenderAll();
  }
  function agProjectMenu(anchor) {
    const items = [{ title: 'Project for the agent' },
      ...(ws.projects || []).slice(0, 30).map((p) => ({ label: p.name, icon: p.name === AG.project ? 'i-check' : 'i-folder', onClick: () => agSwitchProject(p.name) })),
      'sep', { label: 'New project…', icon: 'i-plus', onClick: async () => { const name = await wsDialog({ title: 'New project', text: 'Letters, numbers, - _ . only.', placeholder: 'my-app', ok: 'Create' }); if (name && name.trim()) { const n = await agCreateProject(name.trim().replace(/[^\w.-]+/g, '-')); if (n) agSwitchProject(n); } } }];
    openMenu(anchor, items, { width: 260 });
  }
  async function agSwitchProject(name) {
    if (name === AG.project) return;
    AG.project = name; ls.set('astro_agent:project', name);
    AG.cur = ''; AG.list = []; AG.changes = null; AG.tree = null; AG.view = null; AG.prev = '';
    await agLoadList(name);
    const first = AG.list[0];
    if (first) await agSelect(first.id, true); else { const s = agNew(name); AG.cur = s.id; }
    agRenderAll();
  }
  function agPageSend(text) {
    let s = agCurSession();
    if (!s || s.project !== AG.project) { s = agNew(AG.project); AG.cur = s.id; }
    s.team = AG.team;
    agSend(s, text).then(() => agRenderAll());
    agRenderAll();
  }
  async function agOpenInWorkspace() {
    const s = agCurSession();
    switchView('workspace');
    if (ws.loadingAll) await ws.loadingAll;
    if (ws.project !== AG.project) await wsSwitch(AG.project);
    if (s && s.items.length) { ls.set(agWsKey(AG.project), s.id); ws.agentMode = true; ls.set(`${WS}:agent`, '1'); }
    wsRenderTeam();
  }
  function agRenderAll() {
    if (!$('#agent-view')?._mounted) return;
    $('#ag-proj-name').textContent = AG.project || 'Project';
    agRenderSessions();
    const s = agCurSession();
    const tl = $('#ag-tl');
    if (s) agRenderTl(tl, s); else tl.innerHTML = agEmptyHtml('page');
    agRenderHead();
    agRenderSide();
  }
  function agRenderSessions() {
    const box = $('#ag-slist'); if (!box) return;
    const cur = agCurSession();
    const list = [...AG.list];
    if (cur && cur.project === AG.project && !list.some((m) => m.id === cur.id) && cur.items.length) list.unshift(agMeta(cur));
    if (!list.length) { box.innerHTML = '<div class="ag-sempty">No sessions yet. Your agent jobs appear here.</div>'; return; }
    box.innerHTML = list.map((m) => {
      const live = AG.sessions.get(m.id); const st = live ? (agBusy(live) ? (live.status === 'waiting' ? 'waiting' : 'running') : live.status) : m.status;
      return `<div class="ag-srow${m.id === AG.cur ? ' on' : ''}" data-agsess="${esc(m.id)}" title="${esc(m.title)}"><i class="ag-sdot st-${esc(st)}"></i><div class="ag-stext"><b>${esc(m.title || 'Session')}</b><small>${esc(agAgo(m.updated || m.created))}${m.steps ? ` · ${m.steps} steps` : ''}${m.files ? ` · ${m.files} files` : ''}</small></div><button type="button" class="ag-sdel" data-agdel="${esc(m.id)}" title="Delete session">${icon('i-trash')}</button></div>`;
    }).join('');
  }
  function agStatusText(s) {
    if (!s) return '';
    const live = agBusy(s);
    const secs = live ? s.ms + (performance.now() - (s._started || performance.now())) : s.ms;
    const map = { idle: '', running: 'Working', waiting: 'Waiting for your answer', done: 'Done', stopped: 'Stopped', error: 'Error', paused: 'Paused at the step limit', interrupted: 'Interrupted' };
    const label = live ? (s.status === 'waiting' ? map.waiting : `Working · step ${s.steps + 1}`) : map[s.status] ?? '';
    return label ? `${label}${secs > 1000 ? ` · ${fmtSecs(secs)}` : ''}${s.tools ? ` · ${s.tools} actions` : ''}` : '';
  }
  function agRenderHead() {
    if (!$('#agent-view')?._mounted) return;
    const s = agCurSession();
    $('#ag-title').textContent = s?.title || 'New session';
    const st = $('#ag-status'); st.textContent = agStatusText(s); st.className = `ag-status st-${s ? (agBusy(s) ? (s.status === 'waiting' ? 'waiting' : 'running') : s.status) : 'idle'}`;
    const busy = agBusy(s);
    $('#ag-send').innerHTML = icon(busy ? 'i-stop' : 'i-send'); $('#ag-send').classList.toggle('stop', busy); $('#ag-send').title = busy ? 'Stop the agent' : 'Send (Enter)';
    $('#ag-team').classList.toggle('on', AG.team);
    const n = agCands().length; let all = 0; try { all = pickMinds(Infinity).length; } catch (_e) { /* not loaded */ }
    $('#ag-hint').textContent = busy ? 'Working on its own… press ■ to stop' : AG.team ? `Team of ${all} models plans & reviews · best of ${n} fast models acts` : `Best of ${n} fast models · automatic fallback`;
    $('#ag-ccount').textContent = AG.changes && AG.changes.sid === AG.cur && AG.changes.list.length ? AG.changes.list.length : '';
    if (AG.list.length) { const row = $(`#ag-slist [data-agsess="${CSS.escape(AG.cur)}"] .ag-sdot`); if (row && s) row.className = `ag-sdot st-${busy ? (s.status === 'waiting' ? 'waiting' : 'running') : s.status}`; }
  }

  // ---------- side panel: changes / files / terminal / preview ----------
  function agSetSide(tab) {
    AG.side = tab; AG.sideOpen = true; $('#ag')?.classList.remove('side-off');
    if (tab !== 'changes' && tab !== 'files') AG.view = null;
    agRenderSide();
  }
  let agChangesT = 0;
  function agChangesDirty(s) {
    if (!s || s.id !== AG.cur) return;
    clearTimeout(agChangesT);
    agChangesT = setTimeout(async () => {
      AG.changes = { sid: s.id, list: await agChangeList(s) };
      AG.tree = null;
      agRenderHead();
      if (AG.side === 'changes' && !AG.view) agRenderSide();
      if (AG.side === 'files' && !AG.view) agRenderSide();
    }, 500);
  }
  async function agRenderSide() {
    const pane = $('#ag-pane'); if (!pane) return;
    $$('[data-agtab]').forEach((b) => b.classList.toggle('on', b.dataset.agtab === AG.side));
    const s = agCurSession();
    AG.ide = ws.ide;
    if (AG.view && (AG.side === 'changes' || AG.side === 'files')) return agRenderView(pane);
    if (AG.side === 'changes') {
      if (!s || !s.checkpoint) { pane.innerHTML = `<div class="ag-pempty">${icon('i-diff')}<p>Files the agent creates, edits or deletes in this session appear here, with a diff for each one.</p></div>`; return; }
      if (!AG.changes || AG.changes.sid !== s.id) { pane.innerHTML = '<div class="ws-skel"><i></i><i></i><i></i></div>'; AG.changes = { sid: s.id, list: await agChangeList(s) }; agRenderHead(); if (AG.side !== 'changes' || AG.view) return; }
      const list = AG.changes.list;
      const label = { added: ['A', 'Created'], modified: ['M', 'Modified'], deleted: ['D', 'Deleted'] };
      pane.innerHTML = `<div class="ag-ph"><b>${list.length} file${list.length === 1 ? '' : 's'} changed</b><span class="ag-sub">since this session started</span><span class="grow"></span><button type="button" class="ws-icon sm" data-agside="refresh" title="Refresh">${icon('i-refresh')}</button>${list.length && !agBusy(s) ? `<button type="button" class="ws-chip ghost sm" data-agundo>${icon('i-undo')}Undo all</button>` : ''}</div>
        ${list.length ? `<div class="ag-clist">${list.map((c) => `<button type="button" class="ag-crow" data-agcdiff="${esc(c.path)}" title="${esc(label[c.status]?.[1] || c.status)}: ${esc(c.path)}"><span class="ag-cst st-${esc(c.status)}">${label[c.status]?.[0] || '?'}</span>${fileBadge(c.path)}<span class="ag-cname">${esc(baseName(c.path))}</span><span class="ag-cdir">${esc(c.path.includes('/') ? c.path.slice(0, c.path.lastIndexOf('/')) : '')}</span></button>`).join('')}</div>` : `<div class="ag-pempty"><p>No file changes yet.</p></div>`}`;
      return;
    }
    if (AG.side === 'files') {
      if (!AG.tree || AG.tree.project !== AG.project) {
        pane.innerHTML = '<div class="ws-skel"><i></i><i></i><i></i></div>';
        try { const r = await apiJson('/api/fs/tree', { project: AG.project }); AG.tree = { project: AG.project, files: r.files || [] }; } catch (err) { pane.innerHTML = `<div class="ws-note error">${esc(err.message)}</div>`; return; }
        if (AG.side !== 'files' || AG.view) return;
      }
      const changed = new Map((AG.changes?.sid === AG.cur ? AG.changes.list : []).map((c) => [c.path, c.status]));
      const files = [...AG.tree.files].sort((a, b) => a.path.localeCompare(b.path));
      let lastDir = null; let h = '';
      for (const f of files) {
        const dir = f.path.includes('/') ? f.path.slice(0, f.path.lastIndexOf('/')) : '';
        if (dir !== lastDir) { if (dir) h += `<div class="ag-fdir">${icon('i-folder')}${esc(dir)}/</div>`; lastDir = dir; }
        const st = changed.get(f.path);
        h += `<button type="button" class="ag-frow${dir ? ' in' : ''}" data-agfile="${esc(f.path)}">${fileBadge(f.path)}<span>${esc(baseName(f.path))}</span>${st ? `<i class="ag-cst st-${st}">${st === 'added' ? 'A' : 'M'}</i>` : ''}<small>${fmtBytes(f.size || 0)}</small></button>`;
      }
      pane.innerHTML = `<div class="ag-ph"><b>${files.length} files</b><span class="ag-sub">${esc(AG.project)}</span><span class="grow"></span><button type="button" class="ws-icon sm" data-agside="refresh-tree" title="Refresh">${icon('i-refresh')}</button></div><div class="ag-flist">${h || '<div class="ag-pempty"><p>The project is empty.</p></div>'}</div>`;
      return;
    }
    if (AG.side === 'terminal') {
      const items = s ? s.items.filter((it) => it.kind === 'tool' && (it.tool === 'bash' || it.tool === 'start_server' || it.tool === 'server_logs')) : [];
      const body = items.map((it) => `<div class="ag-tcmd${it.status === 'err' ? ' bad' : ''}"><span class="ag-ps">$ ${esc(it.tool === 'bash' ? it.args?.command || '' : it.tool === 'start_server' ? `${it.args?.command || ''}   # app server` : '# server logs')}</span>${it.status === 'run' ? '\n<span class="ag-spin"></span> running…' : `\n${esc(it.out || '')}${it.meta?.code !== undefined ? `\n<span class="ag-exit${it.meta.code === 0 ? '' : ' bad'}">[exit ${it.meta.code} · ${fmtSecs(it.meta.ms || it.ms || 0)}]</span>` : ''}`}</div>`).join('') + AG.termExtra.filter((x) => x.sid === AG.cur).map((x) => `<div class="ag-tcmd mine"><span class="ag-ps">you $ ${esc(x.cmd)}</span>\n${x.out === null ? '<span class="ag-spin"></span> running…' : `${esc(x.out)}\n<span class="ag-exit${x.code === 0 ? '' : ' bad'}">[exit ${x.code}]</span>`}</div>`).join('');
      const keep = $('.ag-term', pane); const atEnd = !keep || keep.scrollHeight - keep.scrollTop - keep.clientHeight < 60;
      const val = $('#ag-term-in')?.value || '';
      pane.innerHTML = `<div class="ag-ph"><b>Terminal</b><span class="ag-sub">every command the agent ran, in order</span></div><pre class="ag-term">${body || '<span class="ag-dim">No commands yet.</span>'}</pre><form class="ag-term-form" id="ag-term-form"><span class="ag-ps">$</span><input id="ag-term-in" type="text" spellcheck="false" autocomplete="off" placeholder="Run your own command in ${esc(AG.project)}…"></form>`;
      const t = $('.ag-term', pane); if (atEnd) t.scrollTop = t.scrollHeight;
      if (val) $('#ag-term-in').value = val;
      return;
    }
    if (AG.side === 'preview') {
      let url = AG.prev;
      if (!url) {
        const st = await apiJson('/api/ide/proc/status', { project: AG.project }).catch(() => null);
        if (st?.running && st.url) url = st.url;
        else { const files = AG.tree?.project === AG.project ? AG.tree.files : ((await apiJson('/api/fs/tree', { project: AG.project }).catch(() => ({}))).files || []); const html = files.find((f) => f.path === 'index.html') || files.find((f) => /\.html?$/i.test(f.path)); if (html) url = previewUrlFor(AG.project, html.path); }
        AG.prev = url || '';
      }
      if (AG.side !== 'preview') return;
      pane.innerHTML = `<div class="ag-ph"><span class="ag-url" title="${esc(url || '')}">${esc(url ? url.replace(/^\/(preview|app)\/[^/]+\//, '') : 'Nothing to preview yet')}</span><span class="grow"></span><button type="button" class="ws-icon sm" data-agside="reload" title="Reload">${icon('i-refresh')}</button>${url ? `<a class="ws-icon sm" href="${esc(url)}" target="_blank" rel="noopener" title="Open in a new tab">${icon('i-ext')}</a>` : ''}</div>${url ? `<iframe class="ag-frame" src="${esc(url)}" title="Preview" sandbox="allow-scripts allow-forms allow-modals allow-popups allow-downloads allow-pointer-lock"></iframe>` : `<div class="ag-pempty">${icon('i-eye')}<p>Web pages the agent builds and servers it starts show here live.</p></div>`}`;
    }
  }
  async function agSideClick(e) {
    const s = agCurSession();
    const b = e.target.closest('[data-agside]');
    if (b) {
      const a = b.dataset.agside;
      if (a === 'refresh') { AG.changes = null; agRenderSide(); }
      if (a === 'refresh-tree') { AG.tree = null; agRenderSide(); }
      if (a === 'reload') { const f = $('.ag-frame'); if (f) f.src = f.src; else { AG.prev = ''; agRenderSide(); } }
      if (a === 'back') { AG.view = null; agRenderSide(); }
      if (a === 'open-ws') { const p = AG.view?.path; await agOpenInWorkspace(); if (p) wsOpen(p); }
      return;
    }
    if (e.target.closest('[data-agundo]')) { if (s) agUndoAll(s); return; }
    const c = e.target.closest('[data-agcdiff]'); if (c && s) { agShowDiff(s, c.dataset.agcdiff); return; }
    const f = e.target.closest('[data-agfile]'); if (f) { agViewFile(f.dataset.agfile); }
  }
  async function agSideSubmit(e) {
    if (e.target.id !== 'ag-term-form') return;
    e.preventDefault();
    const input = $('#ag-term-in'); const cmd = input.value.trim(); if (!cmd) return;
    input.value = '';
    const x = { sid: AG.cur, cmd, out: null, code: 0 }; AG.termExtra.push(x); agRenderSide();
    try { const r = await apiJson('/api/ide/exec', { project: AG.project, command: cmd, timeout: 120 }); x.out = r.out; x.code = r.code; } catch (err) { x.out = err.message; x.code = -1; }
    if (AG.side === 'terminal') agRenderSide();
    const s = agCurSession(); if (s) { agChangesDirty(s); agSyncWs(s, null); }
    setTimeout(() => $('#ag-term-in')?.focus(), 0);
  }
  async function agShowDiff(s, path) {
    if (AG.side !== 'changes') { AG.side = 'changes'; }
    AG.view = { kind: 'diff', path, loading: true };
    agRenderSide();
    const [a, b] = await Promise.all([
      s.checkpoint ? agApi(s, '/api/ide/history/read', { id: s.checkpoint, path }).catch((err) => ({ error: err.message })) : Promise.resolve({ missing: true }),
      agApi(s, '/api/fs/read', { path }).catch((err) => (err.status === 404 ? { missing: true } : { error: err.message })),
    ]);
    if (AG.view?.path !== path) return;
    Object.assign(AG.view, { loading: false, a, b });
    agRenderSide();
  }
  async function agViewFile(path) {
    AG.side = 'files'; AG.sideOpen = true; $('#ag')?.classList.remove('side-off');
    AG.view = { kind: 'file', path, loading: true };
    agRenderSide();
    const r = await apiJson('/api/fs/read', { project: AG.project, path }).catch((err) => ({ error: err.message }));
    if (AG.view?.path !== path) return;
    Object.assign(AG.view, { loading: false, r });
    agRenderSide();
  }
  function agRenderView(pane) {
    const v = AG.view;
    AG.diffView?.destroy?.(); AG.diffView = null;
    const head = `<div class="ag-ph"><button type="button" class="ws-icon sm" data-agside="back" title="Back">${icon('i-left')}</button>${fileBadge(v.path)}<b class="ag-vpath" title="${esc(v.path)}">${esc(v.path)}</b><span class="grow"></span><span id="ag-vstat"></span><button type="button" class="ws-chip ghost sm" data-agside="open-ws">${icon('i-code')}Edit</button></div>`;
    if (v.loading) { pane.innerHTML = `${head}<div class="ws-skel"><i></i><i></i><i></i></div>`; return; }
    if (v.kind === 'file') {
      const r = v.r || {};
      if (r.error) { pane.innerHTML = `${head}<div class="ws-note error">${esc(r.error)}</div>`; return; }
      if (r.binary) { pane.innerHTML = `${head}<div class="ag-pempty"><p>Binary file.</p></div>`; return; }
      const text = String(r.content ?? ''); const lines = text.split('\n'); const shown = lines.slice(0, 4000);
      pane.innerHTML = `${head}<div class="ag-code"><pre class="ag-gut">${shown.map((_l, i) => i + 1).join('\n')}</pre><pre class="ag-src">${HL(shown.join('\n'), extOf(v.path))}</pre></div>${lines.length > 4000 ? '<div class="ag-dim">Showing the first 4000 lines.</div>' : ''}`;
      return;
    }
    const A = v.a || {}; const B = v.b || {};
    if (A.error || B.error) { pane.innerHTML = `${head}<div class="ws-note error">${esc(A.error || B.error)}</div>`; return; }
    if (A.binary || B.binary) { pane.innerHTML = `${head}<div class="ag-pempty"><p>Binary file: no text diff.</p></div>`; return; }
    const aText = A.missing ? '' : A.content ?? ''; const bText = B.missing ? '' : B.content ?? '';
    pane.innerHTML = `${head}<div class="ag-dnote">${A.missing ? 'New file created by the agent' : B.missing ? 'Deleted by the agent' : 'Before the session → now'}</div><div class="ag-diff" id="ag-diff-host"></div>`;
    const st = agStats(aText, bText); $('#ag-vstat').innerHTML = `<span class="dstat add">+${st.added}</span><span class="dstat del">−${st.removed}</span>`;
    const host = $('#ag-diff-host');
    if (ws.ide?.createDiff) { try { AG.diffView = ws.ide.createDiff(host, { a: aText, b: bText, ext: extOf(v.path), unified: true }); return; } catch (_e) { /* fallback below */ } }
    host.innerHTML = `<pre class="ag-pre code">${HL(bText, extOf(v.path))}</pre>`;
  }

  // ---------- Workspace "Agent" mode ----------
  async function agWsSession(create) {
    if (!ws.project) return null;
    const id = ls.get(agWsKey(ws.project));
    let s = id ? await agGet(ws.project, id) : null;
    if (!s && create) { s = agNew(ws.project, 'ws'); ls.set(agWsKey(ws.project), s.id); }
    return s;
  }
  async function agWsRender() {
    const box = $('#ws-team-msgs'); if (!box) return;
    const want = ws.project;
    const s = await agWsSession(false);
    if (!ws.agentMode || ws.project !== want) return;
    if (s) agRenderTl(box, s); else { box.innerHTML = agEmptyHtml('ws'); box._sid = ''; box._nodes = null; }
    agWsHint();
  }
  function agWsHint() {
    if (!ws.agentMode) return;
    const hint = $('#ws-team-hint'); const send = $('#ws-team-send'); if (!hint || !send) return;
    const s = AG.sessions.get(ls.get(agWsKey(ws.project)) || '');
    const busy = agBusy(s);
    let all = 0; try { all = pickMinds(Infinity).length; } catch (_e) { /* not loaded */ }
    hint.textContent = busy ? `${agStatusText(s)} · press ■ to stop` : `Agent · ${AG.team ? `team of ${all} plans & reviews, ` : ''}acts on its own with tools`;
    send.innerHTML = icon(busy ? 'i-stop' : 'i-send'); send.classList.toggle('stop', busy);
  }
  async function agWsSend(text) {
    const s = await agWsSession(true); if (!s) { toast('Create a project first', 'error'); return; }
    if (agBusy(s)) { toast('The agent is still working'); return; }
    s.team = AG.team;
    if (wsIsNarrow()) { ws.mtab = 'team'; wsLayout(); }
    await wsSaveAll();
    agWsRender();
    await agSend(s, text);
  }
  function agWsStop() { const s = AG.sessions.get(ls.get(agWsKey(ws.project)) || ''); if (s) agStop(s); }
  function agWsBusy() { return agBusy(AG.sessions.get(ls.get(agWsKey(ws.project)) || '')); }
  function agWsNew() { if (agWsBusy()) { toast('Stop the agent first'); return; } ls.del(agWsKey(ws.project)); agWsRender(); }
  function agWsFull() { AG.project = ws.project; ls.set('astro_agent:project', ws.project); const id = ls.get(agWsKey(ws.project)); if (id) AG.cur = id; AG.list = []; switchView('agent'); }

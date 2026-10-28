// Astro AI developer platform (window.AstroDev): the features of Claude Code, Codex, OpenCode, Cursor,
// VS Code, Antigravity and DeepSeek Harness, inside Astro AI's Workspace and Agent.
//   - Open folder: work on a real folder on this computer, in place (Plan / Ask / Auto permission modes, checkpoints)
//   - Rules (AGENTS.md, CLAUDE.md, .cursorrules...), skills (SKILL.md), slash commands, custom subagents, hooks
//   - MCP servers (stdio + HTTP), the MCP registry, plugins (Claude Code plugin format, from GitHub)
//   - Open VSX themes and snippets, a Problems panel, AI ghost text and Ctrl/Cmd+I inline edit
//   - New agent tools: glob, diagnostics, todo, skill, mcp, task (subagents and installed coding CLIs)
// Loaded before app.js; app.js exposes its internals as window.AstroAgent and calls the hooks below.
(() => {
  'use strict';
  const A = () => window.AstroAgent;
  const dev = (path, body) => A().apiJson(`/api/dev/${path}`, body || {});
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const icon = (n) => (A() ? A().icon(n) : '');
  const clip = (t, n) => { t = String(t ?? ''); return t.length > n ? `${t.slice(0, n)}…` : t; };
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const LS = 'astro_dev';
  const lsGet = (k, d = '') => { try { const v = localStorage.getItem(`${LS}:${k}`); return v === null ? d : v; } catch (_e) { return d; } };
  const lsSet = (k, v) => { try { if (v === null || v === undefined) localStorage.removeItem(`${LS}:${k}`); else localStorage.setItem(`${LS}:${k}`, v); } catch (_e) { /* private mode */ } };
  const D = { scan: new Map(), mcp: new Map(), problems: [], probProject: '', probBusy: false, extTab: lsGet('ext-tab', 'skills'), pluginTools: {}, pluginCommands: [], vsx: null, reg: null, styleNodes: new Map() };

  // ---------- small parsers ----------
  function frontmatter(text) {
    const t = String(text || '');
    const m = t.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n?/);
    if (!m) return { meta: {}, body: t };
    const meta = {};
    for (const line of m[1].split(/\r?\n/)) { const kv = line.match(/^([A-Za-z0-9_-]+)\s*:\s*(.*)$/); if (kv) meta[kv[1].toLowerCase()] = kv[2].replace(/^["']|["']$/g, '').trim(); }
    return { meta, body: t.slice(m[0].length) };
  }
  function tomlPrompt(text) {   // Codex / Gemini CLI prompt files: description = "...", prompt = """..."""
    const d = (text.match(/^\s*description\s*=\s*"([^"]*)"/m) || [])[1] || '';
    const p = (text.match(/prompt\s*=\s*"""([\s\S]*?)"""/) || text.match(/prompt\s*=\s*"([^"]*)"/) || [])[1] || text;
    return { meta: { description: d }, body: p };
  }
  function jsonc(text) {   // JSON with comments and trailing commas (VS Code files)
    let out = ''; let i = 0; let inStr = false;
    const s = String(text || '');
    while (i < s.length) {
      const c = s[i];
      if (inStr) { out += c; if (c === '\\') { out += s[i + 1] || ''; i += 2; continue; } if (c === '"') inStr = false; i++; continue; }
      if (c === '"') { inStr = true; out += c; i++; continue; }
      if (c === '/' && s[i + 1] === '/') { while (i < s.length && s[i] !== '\n') i++; continue; }
      if (c === '/' && s[i + 1] === '*') { i += 2; while (i < s.length && !(s[i] === '*' && s[i + 1] === '/')) i++; i += 2; continue; }
      out += c; i++;
    }
    return JSON.parse(out.replace(/,\s*([}\]])/g, '$1'));
  }
  const baseName = (p) => String(p).split('/').pop();
  const stem = (p) => baseName(p).replace(/\.(md|mdc|toml)$/i, '');

  // ---------- scan (rules, skills, commands, agents, hooks, plugins, themes, snippets) ----------
  async function getScan(project, force = false) {
    const key = project || '*';
    const have = D.scan.get(key);
    if (!force && have && Date.now() - have.at < 20000) return have.data;
    let raw = { files: [], agents_cli: [] };
    try { raw = await dev('scan', { project: project || '' }); } catch (_e) { /* offline */ }
    const data = digest(raw);
    D.scan.set(key, { at: Date.now(), data });
    return data;
  }
  function digest(raw) {
    const enabled = pluginState(raw);
    const live = (f) => !f.owner || enabled[f.owner] !== false;
    const out = { rules: [], skills: [], commands: [], agents: [], hooks: [], plugins: [], themes: [], snippets: [], scripts: [], styles: [], settings: {}, cli: raw.agents_cli || [], extRoot: raw.ext_root || '', enabled };
    for (const f of raw.files || []) {
      if (f.kind === 'rule') out.rules.push(f);
      else if (f.kind === 'skill' && live(f)) {
        const { meta, body } = frontmatter(f.content);
        const dir = f.path.split('/').slice(-2, -1)[0] || stem(f.path);
        out.skills.push({ ...f, name: meta.name || dir, description: meta.description || clip(body.replace(/\s+/g, ' '), 160), body });
      } else if (f.kind === 'command' && live(f)) {
        const { meta, body } = /\.toml$/i.test(f.path) ? tomlPrompt(f.content) : frontmatter(f.content);
        const rel = f.path.replace(/^.*?(commands|command|prompts)\//, '');
        const name = rel.replace(/\.(md|mdc|toml)$/i, '').split('/').join(':');
        out.commands.push({ ...f, name: (f.owner ? `${f.owner}:` : '') + name, short: name, description: meta.description || clip(body.replace(/\s+/g, ' '), 100), meta, body });
      } else if (f.kind === 'agent' && live(f)) {
        const { meta, body } = frontmatter(f.content);
        out.agents.push({ ...f, name: meta.name || stem(f.path), description: meta.description || clip(body.replace(/\s+/g, ' '), 140), meta, body });
      } else if (f.kind === 'hooks' && live(f)) {
        try { const j = JSON.parse(f.content); const h = j.hooks || j; if (h && typeof h === 'object') out.hooks.push({ ...f, hooks: h }); } catch (_e) { /* not JSON */ }
      } else if (f.kind === 'plugin') {
        let m = {}; try { m = JSON.parse(f.content); } catch (_e) { /* bad manifest */ }
        out.plugins.push({ ...f, name: f.owner, manifest: m, enabled: enabled[f.owner] !== false });
      } else if (f.kind === 'plugin-script' && live(f)) out.scripts.push(f);
      else if (f.kind === 'plugin-style' && live(f)) out.styles.push(f);
      else if (f.kind === 'theme' && live(f)) { try { out.themes.push({ ...f, theme: JSON.parse(f.content) }); } catch (_e) { /* bad theme */ } }
      else if (f.kind === 'snippets' && live(f)) { try { out.snippets.push({ ...f, data: JSON.parse(f.content) }); } catch (_e) { /* bad snippets */ } }
      else if (f.kind === 'settings') { try { out.settings = JSON.parse(f.content) || {}; } catch (_e) { /* bad */ } }
    }
    // the same skill / command in several places: project wins over global over user
    const rank = { project: 0, global: 1, user: 2 };
    const uniq = (arr) => { const seen = new Map(); for (const x of arr.sort((a, b) => rank[a.scope] - rank[b.scope])) if (!seen.has(x.name.toLowerCase())) seen.set(x.name.toLowerCase(), x); return [...seen.values()]; };
    out.skills = uniq(out.skills); out.commands = uniq(out.commands); out.agents = uniq(out.agents);
    return out;
  }
  function pluginState(raw) { const f = (raw.files || []).find((x) => x.kind === 'plugin-state'); try { return f ? JSON.parse(f.content) || {} : {}; } catch (_e) { return {}; } }
  function invalidate() { D.scan.clear(); }

  // ---------- permission modes ----------
  const MODES = {
    plan: { label: 'Plan', icon: 'i-plan', sub: 'Read-only: the agent inspects and writes a plan, then you run it' },
    ask: { label: 'Ask', icon: 'i-hand', sub: 'You approve every change and command before it runs' },
    auto: { label: 'Auto', icon: 'i-bolt', sub: 'The agent works on its own (a checkpoint is saved first)' },
  };
  const isLinked = (project) => Boolean(A()?.ws.projects?.find((p) => p.name === project)?.linked);
  function modeOf(s) {
    if (!s) return 'auto';
    if (s.mode && MODES[s.mode]) return s.mode;
    const saved = lsGet(`mode:${s.project}`);
    return MODES[saved] ? saved : isLinked(s.project) ? 'ask' : 'auto';
  }
  function setMode(project, mode, s) {
    lsSet(`mode:${project}`, mode);
    if (s) s.mode = mode;
    for (const x of A().AG.sessions.values()) if (x.project === project && !A().AG.runs.has(x.id)) x.mode = mode;
    paintModeChips();
  }
  function paintModeChips() {
    if (!A()) return;
    const s = A().agCurSession?.();
    const project = s?.project || A().AG.project;
    const m = modeOf(s || { project });
    document.querySelectorAll('[data-devmode]').forEach((b) => {
      const label = `${MODES[m].label} mode`;
      if (b.dataset.m !== m) { b.dataset.m = m; b.innerHTML = `${icon(MODES[m].icon)}<span>${label}</span>`; b.classList.toggle('on', m !== 'auto'); }
    });
  }
  function modeMenu(anchor) {
    const s = A().agCurSession?.();
    const project = s?.project || A().AG.project;
    const cur = modeOf(s || { project });
    A().openMenu(anchor, [{ title: 'Permission mode' }, ...Object.entries(MODES).map(([id, m]) => ({ label: m.label, icon: m.icon, sub: m.sub, tick: id === cur, onClick: () => { setMode(project, id, s); A().toast(`${m.label} mode`); } }))], { width: 320 });
  }
  const READONLY_BASH = /^\s*(ls|cat|head|tail|wc|pwd|echo|find|grep|rg|tree|file|stat|du|which|type|env|printenv|node\s+(-v|--version)|python3?\s+(-V|--version)|git\s+(status|log|diff|show|branch|remote|blame|ls-files|rev-parse))\b[^;&|><`$]*$/;
  const MUTATING = new Set(['write_file', 'edit_file', 'delete_file', 'bash', 'start_server', 'stop_server', 'remember', 'mcp']);
  function isMutating(call) {
    if (call.tool === 'task') return Boolean(call.agent && cliFor(call.agent));
    if (call.tool === 'bash') return !READONLY_BASH.test(String(call.command || ''));
    return MUTATING.has(call.tool);
  }
  function approvalText(call) {
    switch (call.tool) {
      case 'bash': return 'Run this command?';
      case 'write_file': return `Write ${call.path}?`;
      case 'edit_file': return `Edit ${call.path}?`;
      case 'delete_file': return `Delete ${call.path}?`;
      case 'start_server': return 'Start this server?';
      case 'stop_server': return 'Stop the app server?';
      case 'mcp': return `Call the MCP tool ${call.server} · ${call.name}?`;
      case 'task': return `Let ${call.agent} work on the project?`;
      case 'remember': return 'Save this to the project memory?';
      default: return `Allow ${call.tool}?`;
    }
  }
  function approvalPreview(call) {
    if (call.tool === 'bash' || call.tool === 'start_server') return `$ ${call.command}`;
    if (call.tool === 'write_file') return clip(call.content, 3000);
    if (call.tool === 'edit_file') return `- ${String(call.old_text).split('\n').join('\n- ')}\n+ ${String(call.new_text ?? '').split('\n').join('\n+ ')}`;
    if (call.tool === 'mcp') return JSON.stringify(call.arguments || {}, null, 2);
    if (call.tool === 'task') return call.prompt;
    if (call.tool === 'remember') return call.fact;
    return '';
  }

  // ---------- hooks (Claude Code format: PreToolUse, PostToolUse, Stop, UserPromptSubmit) ----------
  const HOOK_NAME = { bash: 'Bash', write_file: 'Write', edit_file: 'Edit', read_file: 'Read', delete_file: 'Delete', list_files: 'LS', glob: 'Glob', search: 'Grep', web_search: 'WebSearch', fetch_page: 'WebFetch', task: 'Task', todo: 'TodoWrite', start_server: 'StartServer', stop_server: 'StopServer', skill: 'Skill' };
  function hookToolName(call) { return call.tool === 'mcp' ? `mcp__${call.server}__${call.name}` : HOOK_NAME[call.tool] || call.tool; }
  function hookInput(call) {
    if (call.tool === 'bash') return { command: call.command };
    if (call.tool === 'write_file') return { file_path: call.path, content: call.content };
    if (call.tool === 'edit_file') return { file_path: call.path, old_string: call.old_text, new_string: call.new_text };
    if (['read_file', 'delete_file'].includes(call.tool)) return { file_path: call.path };
    const o = { ...call }; delete o.tool; return o;
  }
  const hash = (s) => { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return (h >>> 0).toString(36); };
  function projectHooksTrusted(project, scan) {
    const ph = scan.hooks.filter((h) => h.scope === 'project');
    if (!ph.length) return true;
    return lsGet(`hooks-trust:${project}`) === hash(JSON.stringify(ph.map((h) => h.hooks)));
  }
  function trustProjectHooks(project, scan, on) {
    const ph = scan.hooks.filter((h) => h.scope === 'project');
    lsSet(`hooks-trust:${project}`, on ? hash(JSON.stringify(ph.map((h) => h.hooks))) : null);
  }
  async function hooksFor(s, event, toolName) {
    const scan = await getScan(s.project);
    const trusted = projectHooksTrusted(s.project, scan);
    const out = [];
    for (const src of scan.hooks) {
      if (src.scope === 'project' && !trusted) continue;
      for (const group of src.hooks[event] || []) {
        const matcher = group.matcher || '';
        let ok = !toolName || !matcher || matcher === '*';
        if (!ok) { try { ok = new RegExp(`^(?:${matcher})$`).test(toolName); } catch (_e) { ok = matcher === toolName; } }
        if (!ok) continue;
        for (const h of group.hooks || []) if (h.type === 'command' && h.command) out.push({ command: h.command, timeout: Math.min(300, Number(h.timeout) || 60), source: src.path });
      }
    }
    return out;
  }
  async function runHooks(s, event, payload, toolName) {
    const hooks = await hooksFor(s, event, toolName);
    const res = { block: '', context: [], allow: false };
    for (const h of hooks) {
      const json = JSON.stringify({ session_id: s.id, hook_event_name: event, cwd: '.', ...payload });
      const b64 = btoa(unescape(encodeURIComponent(json)));
      const command = `export CLAUDE_PROJECT_DIR="$PWD" ASTRO_PROJECT_DIR="$PWD"; printf %s '${b64}' | base64 -d | ( ${h.command} )`;
      let r;
      try { r = await A().agApi(s, '/api/ide/exec', { command, timeout: h.timeout }); } catch (err) { res.context.push(`[hook ${h.source}] failed to run: ${err.message}`); continue; }
      const outText = String(r.out || '').trim();
      let j = null; try { j = JSON.parse(outText); } catch (_e) { /* plain text */ }
      const decision = j?.hookSpecificOutput?.permissionDecision || j?.decision;
      if (r.code === 2 || decision === 'deny' || decision === 'block') { res.block = (j?.hookSpecificOutput?.permissionDecisionReason || j?.reason || outText || `blocked by a ${event} hook`).slice(0, 2000); break; }
      if (decision === 'allow' || decision === 'approve') res.allow = true;
      const extra = j?.hookSpecificOutput?.additionalContext || (event === 'UserPromptSubmit' && r.code === 0 && !j ? outText : '');
      if (extra) res.context.push(String(extra).slice(0, 3000));
    }
    return res;
  }

  // ---------- agent hooks called by app.js ----------
  async function beforeTool(s, call, ti, signal) {
    const mode = modeOf(s);
    if (mode === 'plan' && isMutating(call)) return { block: 'Plan mode is read-only: inspect with read_file, list_files, glob, search, diagnostics or read-only bash (ls, cat, grep, git diff...), then write the plan and finish with the done block. Do not change anything.' };
    const pre = await runHooks(s, 'PreToolUse', { tool_name: hookToolName(call), tool_input: hookInput(call) }, hookToolName(call)).catch(() => ({}));
    if (pre.block) return { block: `A PreToolUse hook blocked this: ${pre.block}` };
    if (mode === 'ask' && isMutating(call) && !pre.allow) {
      const run = A().AG.runs.get(s.id);
      if (run && s._allowAll === run) return null;
      ti.approve = approvalText(call); ti.preview = approvalPreview(call); ti.v++; s.status = 'waiting'; A().agPaint(s); A().agSave(s);
      const answer = await new Promise((resolve) => { A().AG.waiters.set(ti.id, resolve); signal.addEventListener('abort', () => resolve(null), { once: true }); });
      A().AG.waiters.delete(ti.id);
      delete ti.approve; delete ti.preview; ti.v++; s.status = 'running';
      if (answer === null) throw new DOMException('stopped', 'AbortError');
      if (answer === 'Allow all in this run') { s._allowAll = run; return null; }
      if (answer === 'Allow') return null;
      if (answer === 'Deny') return { block: 'The user denied this action. Choose another approach, or ask_user what they want.' };
      return { block: `The user denied this action and said: ${answer}` };
    }
    return null;
  }
  const CODE_EXT = /\.(js|mjs|cjs|jsx|ts|tsx|mts|py|c|cc|cpp|cxx|h|hpp|sh|bash|php|go|rs|json)$/i;
  async function afterTool(s, call, ti, res) {
    const notes = [];
    if ((call.tool === 'write_file' || call.tool === 'edit_file') && call.path && CODE_EXT.test(call.path) && lsGet('agent-lint', '1') === '1') {
      try {
        const r = await dev('diagnostics', { project: s.project, paths: [call.path] });
        const errs = (r.problems || []).filter((p) => p.severity === 'error');
        if (errs.length) notes.push(`PROBLEMS in ${call.path} after this change (fix them):\n${errs.slice(0, 12).map((p) => `${p.path}:${p.line}:${p.col} ${p.message}`).join('\n')}`);
        if (s.project === A().ws.project) mergeProblems([call.path], r.problems || []);
      } catch (_e) { /* no checker */ }
    }
    const post = await runHooks(s, 'PostToolUse', { tool_name: hookToolName(call), tool_input: hookInput(call), tool_response: clip(res, 4000) }, hookToolName(call)).catch(() => ({}));
    if (post.block) notes.push(`PostToolUse hook feedback: ${post.block}`);
    if (post.context?.length) notes.push(...post.context);
    if (call.tool === 'write_file' || call.tool === 'edit_file' || call.tool === 'delete_file') { if (/^\.(astro|claude|cursor|opencode|codex)\/|^(AGENTS|CLAUDE|GEMINI)\.md$|\.mcp\.json$/.test(call.path || '')) invalidate(); }
    return notes.join('\n');
  }
  async function onStop(s, report) {
    const r = await runHooks(s, 'Stop', { stop_hook_active: Boolean(s._stopRounds), last_message: clip(report, 4000) });
    return r.block ? { continue: r.block } : null;
  }

  // ---------- context for the agent (rules, skills, MCP tools, agents, mode) ----------
  async function brief(s) {
    const scan = await getScan(s.project);
    const parts = [];
    const mode = modeOf(s);
    parts.push(`MODE: ${mode.toUpperCase()} — ${mode === 'plan' ? 'read-only: inspect, then reply with a numbered implementation plan and the done block. Do not change files or run commands that change anything.' : mode === 'ask' ? 'Astro automatically shows the user an approval prompt before each change or command runs, so just call the tools — do not use ask_user to request permission. If a call is denied, choose another approach.' : 'you act on your own; a checkpoint was saved so the user can undo everything.'}${isLinked(s.project) ? ' This project is a real folder on the user\'s computer: be careful, never delete or rewrite files you were not asked to touch.' : ''}`);
    if (s.compactSummary) parts.push(`EARLIER IN THIS SESSION (compacted):\n${clip(s.compactSummary, 4000)}`);
    let budget = 12000; const rules = [];
    for (const r of [...scan.rules.filter((x) => x.scope === 'project'), ...scan.rules.filter((x) => x.scope === 'global'), ...scan.rules.filter((x) => x.scope === 'user')]) {
      if (budget <= 0) break;
      const body = clip(frontmatter(r.content).body.trim(), Math.min(5000, budget)); budget -= body.length;
      if (body) rules.push(`--- ${r.scope === 'project' ? r.path : `${r.scope}: ${r.path}`} ---\n${body}`);
    }
    if (rules.length) parts.push(`PROJECT RULES (obey them; project rules win over global ones):\n${rules.join('\n')}`);
    if (scan.skills.length) parts.push(`SKILLS (load one with {"tool":"skill","name":"..."} when the job matches its description):\n${scan.skills.slice(0, 60).map((k) => `- ${k.name}: ${clip(k.description, 200)}`).join('\n')}`);
    const agents = [...scan.agents.map((a) => `- ${a.name}: ${clip(a.description, 160)} (custom subagent)`), ...scan.cli.map((c) => `- ${c.bin}: ${c.name} installed on this computer (runs headless in the project folder)`)];
    if (agents.length) parts.push(`AGENTS for the task tool:\n${agents.join('\n')}`);
    const tools = await mcpTools(s.project).catch(() => []);
    if (tools.length) {
      let tb = 6000; const lines = [];
      for (const t of tools) {
        const props = t.inputSchema?.properties ? Object.entries(t.inputSchema.properties).map(([k, v]) => `${k}${(t.inputSchema.required || []).includes(k) ? '' : '?'}:${v.type || 'any'}`).join(', ') : '';
        const line = `- ${t.server} · ${t.name}(${props}): ${clip(t.description || '', 160)}`;
        tb -= line.length; if (tb < 0) { lines.push(`… and ${tools.length - lines.length} more`); break; }
        lines.push(line);
      }
      parts.push(`MCP TOOLS (call with {"tool":"mcp","server":"...","name":"...","arguments":{...}}):\n${lines.join('\n')}`);
    }
    return parts.join('\n\n');
  }
  async function mcpTools(project) {
    const r = await Promise.race([dev('mcp/list', { project, start: true }), sleep(25000).then(() => dev('mcp/list', { project }))]);
    D.mcp.set(project, r.servers || []);
    const out = [];
    for (const sv of r.servers || []) if (sv.status === 'running') for (const t of sv.tools || []) out.push({ ...t, server: sv.name });
    return out;
  }

  // ---------- new agent tools ----------
  function globRe(pattern) {
    let p = String(pattern || '').trim().replace(/^\.\//, '');
    let re = ''; let i = 0;
    while (i < p.length) {
      const c = p[i];
      if (c === '*' && p[i + 1] === '*') { if (p[i + 2] === '/') { re += '(?:.*/)?'; i += 3; } else { re += '.*'; i += 2; } continue; }
      if (c === '*') { re += '[^/]*'; i++; continue; }
      if (c === '?') { re += '[^/]'; i++; continue; }
      if (c === '{') { const j = p.indexOf('}', i); if (j > i) { re += `(?:${p.slice(i + 1, j).split(',').map((x) => x.replace(/[.+^$()|[\]\\]/g, '\\$&').replace(/\*/g, '[^/]*')).join('|')})`; i = j + 1; continue; } }
      re += c.replace(/[.+^$()|[\]\\]/g, '\\$&'); i++;
    }
    return new RegExp(`^${re}$`);
  }
  function cliFor(name) {
    const n = String(name || '').toLowerCase().trim();
    const map = { 'claude code': 'claude', claude: 'claude', codex: 'codex', 'codex cli': 'codex', opencode: 'opencode', gemini: 'gemini', 'gemini cli': 'gemini', dsh: 'dsh', 'deepseek harness': 'dsh', deepseek: 'dsh', 'cursor-agent': 'cursor-agent', cursor: 'cursor-agent', qwen: 'qwen', aider: 'aider' };
    const bin = map[n] || n;
    for (const v of D.scan.values()) { const c = v.data.cli.find((x) => x.bin === bin); if (c) return c; }
    return null;
  }
  const shq = (s) => `'${String(s).replace(/'/g, `'\\''`)}'`;
  function cliCommand(bin, prompt, mode) {
    const p = shq(prompt);
    const auto = mode !== 'plan';
    switch (bin) {
      case 'claude': return `claude -p ${p} --output-format text${auto ? ' --permission-mode acceptEdits' : ' --permission-mode plan'}`;
      case 'codex': return `codex exec ${auto ? '--full-auto' : '--sandbox read-only'} ${p}`;
      case 'opencode': return `opencode run ${p}`;
      case 'gemini': return `gemini -p ${p}${auto ? ' --yolo' : ''}`;
      case 'cursor-agent': return `cursor-agent -p ${p} --output-format text`;
      case 'qwen': return `qwen -p ${p}${auto ? ' --yolo' : ''}`;
      case 'aider': return `aider --message ${p}${auto ? ' --yes-always' : ' --dry-run'} --no-pretty`;
      case 'dsh': return `dsh run ${p}`;
      default: return '';
    }
  }
  async function oneShot(prompt, signal, maxTokens = 3000) {
    const d = { id: A().makeId(), content: '', thinking: '', notice: '', status: 'streaming', requested: 'auto', started: performance.now() };
    const cands = A().agCands();
    if (cands[0]) d.requested = cands[0].route || cands[0].id || cands[0].model;
    await A().streamDraft(d, { messages: [{ role: 'user', content: prompt }], mode: 'coding', language: A().state.language, max_tokens: maxTokens }, signal || new AbortController().signal, { onUpdate: () => {} });
    if (d.status === 'error' && !d.content) throw new Error(d.error || 'no model answered');
    return String(d.content || '').replace(/<think>[\s\S]*?<\/think>/g, '').replace(/\n*\[\[\s*confidence[^\]]*\]\]\s*$/i, '').trim();
  }
  function mcpResultText(r) {
    const parts = [];
    for (const c of r.content || []) {
      if (c.type === 'text') parts.push(c.text);
      else if (c.type === 'image') parts.push(`[image ${c.mimeType || ''}]`);
      else if (c.type === 'resource') parts.push(c.resource?.text || `[resource ${c.resource?.uri || ''}]`);
      else parts.push(JSON.stringify(c));
    }
    if (!parts.length && r.structuredContent) parts.push(JSON.stringify(r.structuredContent, null, 2));
    return parts.join('\n');
  }
  const tools = {
    async glob(s, call, item) {
      const pattern = String(call.pattern || call.glob || call.query || call.path || '').trim();
      if (!pattern) throw new Error('glob needs "pattern".');
      const r = await A().agApi(s, '/api/fs/tree', {});
      const re = globRe(pattern); const bare = !pattern.includes('/');
      const hits = (r.files || []).filter((f) => re.test(f.path) || (bare && re.test(baseName(f.path))));
      item.meta = { count: hits.length };
      item.out = hits.slice(0, 300).map((f) => f.path).join('\n');
      return hits.length ? `${hits.length} files match ${pattern}${hits.length > 300 ? ' (first 300)' : ''}:\n${hits.slice(0, 300).map((f) => f.path).join('\n')}` : `No files match ${pattern}.`;
    },
    async diagnostics(s, call, item) {
      const paths = Array.isArray(call.paths) ? call.paths.map(String) : call.path ? [String(call.path)] : [];
      const r = await dev('diagnostics', { project: s.project, paths });
      const list = r.problems || [];
      item.meta = { count: list.length };
      if (s.project === A().ws.project) { if (paths.length) mergeProblems(paths, list); else setProblems(s.project, list); }
      const lines = list.slice(0, 80).map((p) => `${p.path}:${p.line}:${p.col} ${p.severity}: ${p.message} (${p.source})`);
      item.out = lines.join('\n');
      return `Checked with: ${(r.checkers || []).join(', ') || 'no checker found for these files'}.\n${list.length ? `${list.length} problem(s):\n${lines.join('\n')}` : 'No problems found.'}`;
    },
    async todo(s, call) {
      let items = call.items || call.todos || call.list || [];
      if (typeof items === 'string') items = items.split('\n').filter(Boolean);
      items = (Array.isArray(items) ? items : []).slice(0, 30).map((x) => (typeof x === 'string' ? { text: x.replace(/^[-*\d.\s[\]x]+/i, ''), status: /\[x\]/i.test(x) ? 'done' : 'todo' } : { text: String(x.text || x.content || x.task || x.title || ''), status: ['done', 'completed', 'complete'].includes(x.status) ? 'done' : ['doing', 'in_progress', 'active'].includes(x.status) ? 'doing' : 'todo' })).filter((x) => x.text);
      if (!items.length) throw new Error('todo needs "items": [{"text":"...","status":"todo|doing|done"}].');
      let it = s.items.find((x) => x.id === s._todoId);
      if (it) { it.items = items; it.v++; } else { it = A().agPush(s, { kind: 'todo', items }); s._todoId = it.id; }
      s.todos = items;
      A().agPaint(s);
      return `To-do list updated (${items.filter((x) => x.status === 'done').length}/${items.length} done).`;
    },
    async skill(s, call, item) {
      const name = String(call.name || call.skill || '').trim();
      const scan = await getScan(s.project);
      const k = scan.skills.find((x) => x.name.toLowerCase() === name.toLowerCase()) || scan.skills.find((x) => x.path.toLowerCase().includes(`/${name.toLowerCase()}/`));
      if (!k) throw new Error(`No skill named "${name}". Skills: ${scan.skills.map((x) => x.name).join(', ') || 'none installed'}.`);
      item.meta = { path: k.path };
      if (call.file) {
        const r = await dev('ext/read', { scope: k.scope, project: s.project, path: String(call.file) });
        return `${call.file}:\n${clip(r.content, 14000)}`;
      }
      item.out = clip(k.body, 4000);
      const extra = (k.files || []).length ? `\nThe skill also has these files (read one with {"tool":"skill","name":"${k.name}","file":"<path>"}):\n${k.files.join('\n')}` : '';
      return `SKILL "${k.name}" (${k.scope}) — follow these instructions:\n${clip(k.body, 14000)}${extra}`;
    },
    async mcp(s, call, item) {
      const server = String(call.server || '').trim(); const name = String(call.name || call.tool_name || call.method || '').trim();
      if (!server || !name) throw new Error('mcp needs "server" and "name" (see MCP TOOLS in the job).');
      const r = await dev('mcp/call', { project: s.project, server, tool: name, arguments: call.arguments && typeof call.arguments === 'object' ? call.arguments : call.input || {} });
      const text = mcpResultText(r);
      item.out = clip(text, 12000);
      if (r.isError) throw new Error(clip(text || 'the MCP tool reported an error', 2000));
      return clip(text || '(empty result)', 20000);
    },
    async task(s, call, item, signal) {
      const prompt = String(call.prompt || call.description || call.task || '').trim();
      if (!prompt) throw new Error('task needs "prompt".');
      const agentName = String(call.agent || call.subagent_type || '').trim();
      const scan = await getScan(s.project);
      const cli = agentName ? cliFor(agentName) : null;
      if (cli) {
        const command = cliCommand(cli.bin, prompt, modeOf(s));
        const r = await A().agApi(s, '/api/ide/exec', { command, timeout: 900 });
        item.meta = { code: r.code, agent: cli.name };
        item.out = clip(r.out, 30000);
        item.ok = r.code === 0;
        return `${cli.name} finished with exit code ${r.code}${r.timed_out ? ' (timed out)' : ''}:\n${clip(r.out || '(no output)', 12000)}`;
      }
      const custom = agentName ? scan.agents.find((a) => a.name.toLowerCase() === agentName.toLowerCase()) : null;
      if (agentName && !custom) throw new Error(`No agent named "${agentName}". Agents: ${[...scan.agents.map((a) => a.name), ...scan.cli.map((c) => c.bin)].join(', ') || 'none (omit "agent" for a general subagent)'}.`);
      // A focused subagent: one model call with the project context and the files the prompt mentions.
      const tree = await A().agApi(s, '/api/fs/tree', {}).catch(() => ({ files: [] }));
      const files = tree.files || [];
      const mentioned = files.filter((f) => prompt.includes(f.path) || prompt.includes(baseName(f.path))).slice(0, 8);
      let ctx = ''; let budget = 30000;
      for (const f of mentioned) { const t = await A().agReadOrNull(s, f.path); if (t === null) continue; const c = clip(t, Math.min(9000, budget)); budget -= c.length; ctx += `\n--- ${f.path} ---\n${c}\n`; if (budget <= 0) break; }
      const head = custom ? `${custom.body.trim()}\n\n` : 'You are a focused subagent of Astro AI\'s coding agent. ';
      const text = await oneShot(`${head}Do the sub-task below and reply with a concise, factual report the main agent can act on (findings, exact file paths and line numbers, recommended changes as code snippets). You cannot run tools; say what should be verified.\n\nSUB-TASK: ${prompt}\n\nPROJECT FILES (${files.length}):\n${files.slice(0, 200).map((f) => f.path).join('\n')}${ctx ? `\n\nFILE CONTENTS:${ctx}` : ''}`, signal, 3000);
      item.meta = { agent: custom ? custom.name : 'subagent' };
      item.out = clip(text, 12000);
      return `${custom ? custom.name : 'Subagent'} report:\n${clip(text, 12000)}`;
    },
  };

  // ---------- slash commands ----------
  const BUILTIN = [
    ['/help', 'List commands, skills and agents'],
    ['/init', 'Write or improve AGENTS.md for this project'],
    ['/plan', 'Plan only (read-only), then run the plan: /plan <job>'],
    ['/mode', 'Permission mode: /mode plan | ask | auto'],
    ['/review', 'Review the current changes: /review [focus]'],
    ['/test', 'Run the tests and fix failures'],
    ['/commit', 'Write a commit message and commit (no push)'],
    ['/compact', 'Summarise this session to free up context'],
    ['/skills', 'Show installed skills'],
    ['/mcp', 'Show MCP servers and their tools'],
    ['/agents', 'Show subagents and installed coding CLIs'],
  ];
  function expandArgs(body, args) {
    const list = args.match(/"[^"]*"|\S+/g) || [];
    return body.replace(/\$ARGUMENTS|\{\{args\}\}/g, args).replace(/\$(\d)/g, (_m, n) => (list[Number(n) - 1] || '').replace(/^"|"$/g, ''));
  }
  function devNote(s, text, ic) { A().agPush(s, { kind: 'dev', text, icon: ic }); }
  async function agentInput(s, text) {
    paintModeChips();
    if (!text.startsWith('/')) {
      const r = await runHooks(s, 'UserPromptSubmit', { prompt: text }).catch(() => ({}));
      if (r.block) { devNote(s, `A UserPromptSubmit hook blocked this message: ${r.block}`, 'i-x'); return null; }
      return r.context?.length ? `${text}\n\nCONTEXT FROM HOOKS:\n${r.context.join('\n')}` : text;
    }
    const m = text.match(/^\/([\w:.-]+)\s*([\s\S]*)$/);
    if (!m) return text;
    const cmd = m[1].toLowerCase(); const args = m[2].trim();
    const scan = await getScan(s.project, true);
    switch (cmd) {
      case 'help': {
        const custom = scan.commands.map((c) => `| \`/${c.name}\` | ${esc(clip(c.description, 90))} (${c.scope}) |`).join('\n');
        devNote(s, `**Commands**\n\n| Command | What it does |\n|---|---|\n${BUILTIN.map(([c, d]) => `| \`${c}\` | ${d} |`).join('\n')}${custom ? `\n${custom}` : ''}\n\nSkills: ${scan.skills.map((k) => `\`/${k.name}\``).join(', ') || 'none yet (add some in Workspace → Extensions)'}.\n\nMode: **${MODES[modeOf(s)].label}**. Change it with \`/mode plan\`, \`/mode ask\` or \`/mode auto\`.`, 'i-keyboard');
        return null;
      }
      case 'mode': {
        const want = args.toLowerCase();
        if (!MODES[want]) { devNote(s, `Mode is **${MODES[modeOf(s)].label}**. Use \`/mode plan\`, \`/mode ask\` or \`/mode auto\`.`, 'i-hand'); return null; }
        setMode(s.project, want, s); devNote(s, `Mode set to **${MODES[want].label}**: ${MODES[want].sub}.`, MODES[want].icon); return null;
      }
      case 'plan': s.mode = 'plan'; paintModeChips(); return args ? `Make a plan for this job (Plan mode: inspect only, change nothing): ${args}` : 'Inspect this project and propose a plan for the most valuable next improvements (Plan mode: change nothing).';
      case 'init': return 'Analyse this project (structure, languages, how to build, run and test it, conventions you can see in the code) and create or improve AGENTS.md at the project root: a short, accurate guide for coding agents with sections Overview, Commands (build/run/test/lint, verified by running them), Code style, Project structure, and Gotchas. Keep any useful existing content.';
      case 'review': return `Review the current changes in this project${args ? ` with a focus on: ${args}` : ''}. Use git diff (or, without git, the most recently changed files) and read the code. Do not change files. Report findings grouped by severity (bugs, security, correctness, performance, style) with file:line references and concrete fixes, then finish.`;
      case 'test': return `Find out how this project is tested, run the tests${args ? ` (${args})` : ''}, fix every failure at its root cause, and re-run until they pass. If there are no tests, add a small meaningful test suite first.`;
      case 'commit': return `Look at git status and git diff, then stage the relevant changes and commit them with a clear conventional commit message${args ? ` (hint: ${args})` : ''}. Do not push. If this is not a git repository, run git init first.`;
      case 'compact': {
        const lines = s.items.filter((it) => ['user', 'done', 'say', 'review'].includes(it.kind) && it.text).slice(-24).map((it) => `${it.kind === 'user' ? 'User' : it.kind === 'done' ? 'Result' : 'Agent'}: ${clip(String(it.text).replace(/\s+/g, ' '), 300)}`);
        s.compactSummary = clip(`${s.compactSummary ? `${s.compactSummary}\n` : ''}${lines.join('\n')}`, 5000);
        s.convo = [];
        devNote(s, `Compacted: the conversation history was replaced by a short summary (${lines.length} points). The files on disk are unchanged.`, 'i-memory');
        return null;
      }
      case 'skills': devNote(s, scan.skills.length ? `**Skills**\n\n${scan.skills.map((k) => `- **${esc(k.name)}** (${k.scope}${k.owner ? `, ${esc(k.owner)}` : ''}): ${esc(clip(k.description, 160))}`).join('\n')}` : 'No skills yet. Add one in Workspace → Extensions → Skills, or put a SKILL.md in `.astro/skills/<name>/` (Claude Code `.claude/skills` and Codex `.codex/skills` folders work too).', 'i-spark'); return null;
      case 'agents': devNote(s, `**Subagents**\n\n${scan.agents.map((a) => `- **${esc(a.name)}** (${a.scope}): ${esc(clip(a.description, 140))}`).join('\n') || '- none (add `.astro/agents/<name>.md`)'}\n\n**Coding CLIs on this computer**\n\n${scan.cli.map((c) => `- \`${c.bin}\`: ${c.name}`).join('\n') || '- none found (Claude Code, Codex, OpenCode, Gemini CLI, DeepSeek Harness, Cursor CLI, Qwen Code, Aider)'}`, 'i-sparkles'); return null;
      case 'mcp': {
        const r = await dev('mcp/list', { project: s.project }).catch((e) => ({ error: e.message, servers: [] }));
        devNote(s, (r.servers || []).length ? `**MCP servers**\n\n${r.servers.map((x) => `- **${esc(x.name)}** · ${x.status} · ${(x.tools || []).length} tools · _${esc(x.source)}_${x.error ? ` · ${esc(clip(x.error, 120))}` : ''}`).join('\n')}\n\nManage them in Workspace → Extensions → MCP.` : 'No MCP servers configured. Add one in Workspace → Extensions → MCP, or put a `.mcp.json` in the project.', 'i-puzzle');
        return null;
      }
      default: break;
    }
    const custom = scan.commands.find((c) => c.name.toLowerCase() === cmd || c.short.toLowerCase() === cmd);
    if (custom) return expandArgs(custom.body.trim(), args) + (custom.body.includes('$ARGUMENTS') || !args ? '' : `\n\n${args}`);
    const skill = scan.skills.find((k) => k.name.toLowerCase() === cmd);
    if (skill) return `Use the skill "${skill.name}" (load it with the skill tool first)${args ? ` for: ${args}` : '.'}`;
    return text;
  }

  // ---------- Open folder ----------
  async function openFolder() {
    const st = { path: lsGet('last-folder', ''), data: null, hidden: false };
    A().showModal(`<h2 id="modal-title">Open folder</h2><p class="sub">Astro AI works on the folder in place: the editor, terminal, Git and the agent use your real files. New sessions in a folder start in <b>Ask</b> mode (you approve each change), and a checkpoint is saved before every agent run.</p>
      <div class="dev-browse"><div class="dev-bpath"><input class="input" id="dev-bpath" spellcheck="false" aria-label="Folder path" placeholder="/home/you/projects/app"><button type="button" class="btn small" id="dev-bgo">Go</button></div>
      <div class="dev-broots" id="dev-broots"></div><div class="dev-blist" id="dev-blist"><p class="hint">Loading…</p></div>
      <label class="dev-bhid"><input type="checkbox" id="dev-bhidden"> Show hidden folders</label></div>
      <div class="btn-row"><button type="button" class="btn primary" id="dev-bopen">${icon('i-folder-open')}Open this folder</button><button type="button" class="btn ghost" id="modal-close">Cancel</button></div>`);
    const $ = (sel) => document.querySelector(sel);
    $('#modal-close').onclick = () => A().closeModal();
    const load = async (path) => {
      $('#dev-blist').innerHTML = '<p class="hint">Loading…</p>';
      try {
        const r = await dev('fs/browse', { path, hidden: st.hidden });
        st.data = r; st.path = r.path; $('#dev-bpath').value = r.path;
        $('#dev-broots').innerHTML = [r.home ? `<button type="button" class="chip" data-bgo="${esc(r.home)}">${icon('i-folder')}Home</button>` : '', ...(r.roots || []).map((x) => `<button type="button" class="chip" data-bgo="${esc(x)}">${esc(x)}</button>`), r.parent ? `<button type="button" class="chip" data-bgo="${esc(r.parent)}">↑ Up</button>` : ''].join('');
        $('#dev-blist').innerHTML = (r.dirs || []).length ? r.dirs.map((d) => `<button type="button" class="dev-bdir" data-bdir="${esc(d.name)}">${icon('i-folder')}<span>${esc(d.name)}</span>${d.git ? '<em>git</em>' : ''}</button>`).join('') : '<p class="hint">No folders here.</p>';
        $('#dev-bopen').innerHTML = `${icon('i-folder-open')}Open “${esc(baseName(r.path) || r.path)}”`;
      } catch (err) { $('#dev-blist').innerHTML = `<p class="hint">${esc(err.message)}</p>`; }
    };
    const join = (a, b) => (a.endsWith('/') || a.endsWith('\\') ? a + b : `${a}${st.data?.sep || '/'}${b}`);
    document.querySelector('.dev-browse').addEventListener('click', (e) => {
      const g = e.target.closest('[data-bgo]'); if (g) { load(g.dataset.bgo); return; }
      const d = e.target.closest('[data-bdir]'); if (d) load(join(st.path, d.dataset.bdir));
    });
    $('#dev-bgo').onclick = () => load($('#dev-bpath').value.trim());
    $('#dev-bpath').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); load(e.target.value.trim()); } });
    $('#dev-bhidden').onchange = (e) => { st.hidden = e.target.checked; load(st.path); };
    $('#dev-bopen').onclick = async () => {
      const path = $('#dev-bpath').value.trim() || st.path;
      try {
        const r = await dev('fs/open', { path });
        lsSet('last-folder', st.data?.parent || path);
        A().closeModal();
        await showProject(r.name);
        A().toast(r.existing ? `“${r.name}” was already open` : `Opened ${path}`);
      } catch (err) { A().toast(err.message, 'error'); }
    };
    load(st.path);
  }
  async function showProject(name) {
    const a = A();
    a.switchView('workspace');
    if (a.ws.loadingAll) await a.ws.loadingAll;
    await a.wsShow();
    try {
      const r = await a.apiJson('/api/fs/projects', null, 'GET');
      a.ws.projects = (r.projects || []).sort((x, y) => (y.mtime || 0) - (x.mtime || 0));
    } catch (_e) { /* keep the old list */ }
    if (!a.ws.projects.some((p) => p.name === name)) a.ws.projects.unshift({ name, mtime: Date.now() / 1000, linked: true });
    if (a.ws.project !== name) await a.wsSwitch(name);
    else await a.wsLoadTree();
    a.ls.set('astro_ws_v1:project', name);
  }

  // ---------- Problems panel ----------
  function setProblems(project, list) {
    D.probProject = project; D.problems = list || [];
    pushProblemsToEditor(); renderProblems(); paintProbCount();
  }
  function mergeProblems(paths, list) {
    if (D.probProject !== A().ws.project) { D.problems = []; D.probProject = A().ws.project; }
    const set = new Set(paths);
    D.problems = [...D.problems.filter((p) => !set.has(p.path)), ...(list || []).filter((p) => set.has(p.path))];
    pushProblemsToEditor(paths); renderProblems(); paintProbCount();
  }
  function pushProblemsToEditor(paths) {
    const cm = A()?.ws.cm; if (!cm?.setProblems) return;
    const by = new Map();
    for (const p of D.problems) { if (!by.has(p.path)) by.set(p.path, []); by.get(p.path).push(p); }
    if (!paths) { cm.clearProblems(); for (const [k, v] of by) cm.setProblems(k, v); } else for (const k of paths) cm.setProblems(k, by.get(k) || []);
  }
  function paintProbCount() {
    const n = D.problems.filter((p) => p.severity === 'error').length; const b = document.getElementById('ws-prob-count');
    if (b) { b.hidden = !D.problems.length; b.textContent = String(D.problems.length); b.classList.toggle('bad', n > 0); }
  }
  async function checkProblems(paths) {
    const project = A().ws.project; if (!project || D.probBusy) return;
    D.probBusy = true; renderProblems();
    try {
      await A().wsSaveAll();
      const r = await dev('diagnostics', { project, paths: paths || [] });
      D.checkers = r.checkers || [];
      if (paths && paths.length) mergeProblems(paths, r.problems || []); else setProblems(project, r.problems || []);
    } catch (err) { A().toast(err.message, 'error'); }
    D.probBusy = false; renderProblems();
  }
  function renderProblems() {
    const box = document.getElementById('ws-probs'); if (!box || box.closest('[hidden]')) return;
    const by = new Map();
    for (const p of D.problems) { if (!by.has(p.path)) by.set(p.path, []); by.get(p.path).push(p); }
    const auto = lsGet('prob-auto', '1') === '1';
    box.innerHTML = `<div class="ws-head"><span class="ws-head-title">Problems</span><span class="grow"></span><button type="button" class="ws-btn sm" data-prob="check">${D.probBusy ? '<span class="ag-spin"></span>' : icon('i-refresh')}<span>Check project</span></button></div>
      <label class="dev-toggle"><input type="checkbox" data-prob="auto"${auto ? ' checked' : ''}> Check the open file as you type</label>
      ${D.checkers?.length ? `<div class="dev-hint">Checked with ${esc(D.checkers.join(', '))}</div>` : '<div class="dev-hint">Uses the compilers and linters on this computer: node, tsc, python, gcc/g++, bash, php, go vet, cargo and JSON.</div>'}
      ${by.size ? [...by].map(([path, list]) => `<div class="dev-pfile"><b>${esc(path)}</b><i>${list.length}</i></div>${list.map((p) => `<button type="button" class="dev-prob sev-${esc(p.severity)}" data-pgo="${esc(JSON.stringify([p.path, p.line, p.col]))}"><span class="dot"></span><span class="msg">${esc(p.message)}</span><em>${esc(p.source || '')} ${p.line}:${p.col}</em></button>`).join('')}`).join('') : `<div class="git-empty">${D.probBusy ? 'Checking…' : D.probProject === A().ws.project && D.checkers ? 'No problems found.' : 'Press “Check project” to find errors.'}</div>`}`;
  }

  // ---------- Astro AI Library (official MCP servers, skills and plugins) ----------
  // The server downloads and checks everything (SHA-256 against the catalog); this panel only browses,
  // shows exactly what an item will run or write, and asks for the values it needs.
  const LIB_KINDS = [['all', 'All'], ['mcp', 'MCP'], ['skill', 'Skills'], ['plugin', 'Plugins']];
  const LIB_PERMS = { network: 'Talks to an online service', 'local-process': 'Runs a program on this computer', filesystem: 'Reads and writes files in a folder you choose', browser: 'Opens and controls a browser', account: 'Uses your account with a token you give it' };
  async function libLoad(force) {
    if (!force && D.lib?.data && Date.now() - D.lib.at < 60000) return D.lib.data;
    const data = await dev('library/index', { refresh: Boolean(force) });
    D.lib = { ...(D.lib || {}), data, at: Date.now() };
    return data;
  }
  function libCmd(it) {
    const m = it.mcp || {};
    return m.type === 'http' ? m.url : [m.command, ...(m.args || [])].join(' ');
  }
  function libMatches(it, q, kind) {
    if (kind && kind !== 'all' && it.kind !== kind) return false;
    if (!q) return true;
    return `${it.id} ${it.name} ${it.summary} ${(it.tags || []).join(' ')} ${it.publisher || ''}`.toLowerCase().includes(q);
  }
  async function libBody(force) {
    let data;
    try { data = await libLoad(force); } catch (err) { return `<div class="ws-note error">${esc(err.message)}</div>`; }
    const kind = lsGet('lib-kind', 'all'); const q = (D.lib?.q || '').toLowerCase();
    const inst = data.installed || {};
    const items = (data.items || []);
    const card = (it) => {
      const have = inst[it.id]; const upd = have && have.version !== it.version;
      return `<div class="dev-item lib-card" data-lib-card="${esc(it.id)}" data-lib-text="${esc(`${it.id} ${it.name} ${it.summary} ${(it.tags || []).join(' ')} ${it.publisher || ''}`.toLowerCase())}" data-lib-kind="${esc(it.kind)}">
        <div class="dev-it-h"><b>${esc(it.name)}</b><span class="dev-scope">${esc(it.kind === 'mcp' ? 'MCP' : it.kind)}</span>${it.runs === 'local' ? '<span class="dev-scope s-project">runs locally</span>' : ''}${have ? `<span class="dev-scope s-global">${upd ? 'update available' : 'installed'}</span>` : ''}</div>
        <p>${esc(clip(it.summary, 200))}</p>
        <div class="dev-it-a"><button type="button" class="ag-act" data-x="lib-open" data-id="${esc(it.id)}">${icon('i-eye')}Details</button>${have ? `${upd ? `<button type="button" class="ag-act" data-x="lib-open" data-id="${esc(it.id)}">${icon('i-refresh')}Update</button>` : ''}<button type="button" class="ag-act" data-x="lib-remove" data-id="${esc(it.id)}">${icon('i-trash')}Remove</button>` : `<button type="button" class="ag-act" data-x="lib-open" data-id="${esc(it.id)}">${icon('i-download')}Install</button>`}<span class="grow"></span><small>${esc(it.publisher || '')}</small></div></div>`;
    };
    const shown = items.filter((it) => libMatches(it, q, kind));
    return `<div class="dev-inst lib-search"><input class="input" id="lib-q" placeholder="Search ${items.length} servers, skills and plugins" value="${esc(D.lib?.q || '')}" spellcheck="false" aria-label="Search the library"><button type="button" class="ws-btn sm" data-x="lib-refresh" title="Refresh">${icon('i-refresh')}</button></div>
      <div class="dev-acts lib-kinds">${LIB_KINDS.map(([k, l]) => `<button type="button" class="ws-btn sm${k === kind ? ' primary' : ''}" data-x="lib-kind" data-kind="${k}">${l} <small>${k === 'all' ? items.length : items.filter((x) => x.kind === k).length}</small></button>`).join('')}</div>
      <div class="dev-hint">The official Astro AI Library. Every file is checked against its SHA-256 fingerprint before it is saved, skills and plugins are text only (no scripts or hooks), and MCP packages are pinned to exact versions. Tokens you enter stay in this computer's private <code>mcp.json</code>.${data.error ? '' : ` <a href="https://esoexe.github.io/astro-ai/account.html#library" target="_blank" rel="noopener">Open on the website</a>`}</div>
      ${data.error ? `<div class="ws-note error">${esc(data.error)}</div>` : ''}
      ${!data.node && items.some((x) => x.runs === 'local') ? '<div class="ws-note">Servers marked “runs locally” need Node.js (npx). Install it from nodejs.org; remote servers, skills and plugins work without it.</div>' : ''}
      <div id="lib-list">${shown.map(card).join('') || '<div class="git-empty">Nothing matches.</div>'}</div>`;
  }
  function libFilter() {
    const q = (D.lib?.q || '').toLowerCase(); const kind = lsGet('lib-kind', 'all');
    let n = 0;
    document.querySelectorAll('#lib-list [data-lib-card]').forEach((c) => { const on = (!q || c.dataset.libText.includes(q)) && (kind === 'all' || c.dataset.libKind === kind); c.hidden = !on; if (on) n++; });
    return n;
  }
  async function libOpen(id) {
    const data = await libLoad();
    const it = (data.items || []).find((x) => x.id === id);
    if (!it) throw new Error('That item is not in the library any more.');
    const have = (data.installed || {})[id];
    const perms = (it.permissions || []).map((p) => `<li>${esc(LIB_PERMS[p] || p)}</li>`).join('') || '<li>Nothing: it only adds instructions for the agent</li>';
    const what = it.kind === 'mcp'
      ? `<div class="dev-sec">${it.mcp?.type === 'http' ? 'Connects to' : 'Runs this command'}</div><code class="dev-cmd">${esc(libCmd(it))}</code>`
      : `<div class="dev-sec">Files it adds (${(it.files || []).length})</div><div class="lib-files">${(it.files || []).map((f) => `<div><code>${esc(f.path)}</code><small title="SHA-256 ${esc(f.sha256)}">${esc(f.sha256.slice(0, 12))}… · ${Number(f.size || 0).toLocaleString()} B</small></div>`).join('')}</div>`;
    const inputs = (it.inputs || []).map((inp) => `<label class="lib-in"><span>${esc(inp.label || inp.name)}${inp.required ? ' *' : ''}</span><input class="input" data-lib-in="${esc(inp.name)}" type="${inp.secret ? 'password' : 'text'}" autocomplete="off" spellcheck="false" placeholder="${esc(inp.kind === 'arg' && inp.name === 'FOLDER' ? (A().ws.projectPath || 'Full path, e.g. /home/you/project') : inp.secret ? 'Paste here (stored only on this computer)' : '')}">${inp.help ? `<small><a href="${esc(inp.help)}" target="_blank" rel="noopener">Where do I get this?</a></small>` : ''}</label>`).join('');
    A().showModal(`<h2 id="modal-title">${esc(it.name)}</h2>
      <p class="hint">${esc(it.summary)}</p>
      <p class="hint">${esc(it.publisher || '')}${it.license ? ` · ${esc(it.license)}` : ''}${it.homepage ? ` · <a href="${esc(it.homepage)}" target="_blank" rel="noopener">Homepage</a>` : ''} · version ${esc(it.version || '')}</p>
      <div class="dev-sec">What it can do</div><ul class="lib-perms">${perms}</ul>
      ${what}
      ${inputs ? `<div class="dev-sec">Settings</div>${inputs}` : ''}
      ${it.runs === 'local' ? '<p class="hint">This server runs on your computer with your user rights. Only install it if you trust the publisher.</p>' : ''}
      <div class="btn-row"><button type="button" class="btn primary" id="lib-go">${have ? (have.version !== it.version ? 'Update' : 'Reinstall') : 'Install'}</button><button type="button" class="btn ghost" id="modal-close">Cancel</button></div>`);
    document.getElementById('modal-content')?.classList.add('wide');
    document.getElementById('modal-close').onclick = () => A().closeModal();
    document.getElementById('lib-go').onclick = async (ev) => {
      const btn = ev.currentTarget; btn.disabled = true; btn.textContent = 'Installing…';
      const vals = {}; document.querySelectorAll('[data-lib-in]').forEach((i) => { if (i.value.trim()) vals[i.dataset.libIn] = i.value.trim(); });
      try {
        let r;
        try { r = await dev('library/install', { id, inputs: vals }); } catch (err) {
          if (!/did not come from the library/.test(err.message)) throw err;
          const ok = await A().wsDialog({ title: 'Replace your own item?', text: `${err.message} Replace it with the library version?`, ok: 'Replace', danger: true, input: false });
          if (!ok) { btn.disabled = false; btn.textContent = 'Install'; return; }
          r = await dev('library/install', { id, inputs: vals, replace: true });
        }
        A().closeModal(); invalidate(); D.lib.at = 0;
        A().toast(`${it.name} ${r.updated ? 'updated' : 'installed'}${it.kind === 'mcp' ? ' · find it under MCP' : it.kind === 'plugin' ? ' · its commands and skills are ready' : ' · the agent can use it now'}`);
        if (it.kind === 'mcp' && it.runs !== 'local') dev('mcp/restart', { project: A().ws.project, server: id }).catch(() => {});
        renderExt(true);
      } catch (err) { btn.disabled = false; btn.textContent = 'Install'; A().toast(err.message, 'error'); }
    };
  }
  async function openLibrary(id) {
    D.extTab = 'library'; lsSet('ext-tab', 'library');
    await showSide('ext');
    if (id && /^[a-z0-9][a-z0-9-]{0,47}$/.test(id)) libOpen(id).catch((err) => A().toast(err.message, 'error'));
  }
  function libFromHash() {
    const m = String(location.hash || '').match(/^#library(?:=([a-z0-9-]{1,48}))?$/);
    if (!m) return;
    history.replaceState(null, '', location.pathname + location.search);
    setTimeout(() => openLibrary(m[1]).catch(() => {}), 400);
  }

  // ---------- Extensions panel ----------
  const EXT_TABS = [['library', 'Library', 'i-download'], ['skills', 'Skills', 'i-spark'], ['mcp', 'MCP', 'i-puzzle'], ['plugins', 'Plugins', 'i-puzzle'], ['themes', 'Themes', 'i-moon'], ['snippets', 'Snippets', 'i-code'], ['commands', 'Commands', 'i-keyboard'], ['agents', 'Agents', 'i-sparkles'], ['rules', 'Rules & hooks', 'i-read']];
  async function renderExt(force) {
    const box = document.getElementById('ws-ext'); if (!box || box.closest('[hidden]')) return;
    const project = A().ws.project;
    const scan = await getScan(project, force);
    const tab = D.extTab;
    let body = '';
    const scopeBadge = (x) => `<span class="dev-scope s-${esc(x.scope)}">${x.scope === 'user' ? 'your tools' : esc(x.scope)}${x.owner ? ` · ${esc(x.owner)}` : ''}</span>`;
    if (tab === 'library') {
      body = await libBody(force);
    } else if (tab === 'skills') {
      body = `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="new-skill">${icon('i-plus')}<span>New skill</span></button></div>
        <div class="dev-hint">Skills are folders with a SKILL.md (name, description, instructions). The agent sees every skill's description and loads one when a job matches. Astro reads <code>.astro/skills</code>, Claude Code <code>.claude/skills</code>, Codex <code>.codex/skills</code>, OpenCode and Cursor skill folders, in the project, in Extensions, and in your home folder.</div>
        ${scan.skills.map((k) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(k.name)}</b>${scopeBadge(k)}</div><p>${esc(clip(k.description, 220))}</p><div class="dev-it-a"><button type="button" class="ag-act" data-x="view" data-scope="${esc(k.scope)}" data-path="${esc(k.path)}">${icon('i-eye')}View</button>${k.scope !== 'user' && !k.owner ? `<button type="button" class="ag-act" data-x="edit" data-scope="${esc(k.scope)}" data-path="${esc(k.path)}">${icon('i-edit')}Edit</button><button type="button" class="ag-act" data-x="del" data-scope="${esc(k.scope)}" data-path="${esc(k.path.replace(/\/SKILL\.md$|\/skill\.md$/i, ''))}">${icon('i-trash')}Delete</button>` : ''}</div></div>`).join('') || '<div class="git-empty">No skills yet.</div>'}`;
    } else if (tab === 'commands') {
      body = `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="new-cmd">${icon('i-plus')}<span>New command</span></button></div>
        <div class="dev-hint">Type <code>/name</code> in the Agent to run a saved prompt; <code>$ARGUMENTS</code> and <code>$1</code>… are replaced by what you type after it. Built in: ${BUILTIN.map(([c]) => `<code>${c}</code>`).join(' ')}</div>
        ${scan.commands.map((c) => `<div class="dev-item"><div class="dev-it-h"><b>/${esc(c.name)}</b>${scopeBadge(c)}</div><p>${esc(clip(c.description, 200))}</p><div class="dev-it-a"><button type="button" class="ag-act" data-x="view" data-scope="${esc(c.scope)}" data-path="${esc(c.path)}">${icon('i-eye')}View</button>${c.scope !== 'user' && !c.owner ? `<button type="button" class="ag-act" data-x="edit" data-scope="${esc(c.scope)}" data-path="${esc(c.path)}">${icon('i-edit')}Edit</button><button type="button" class="ag-act" data-x="del" data-scope="${esc(c.scope)}" data-path="${esc(c.path)}">${icon('i-trash')}Delete</button>` : ''}</div></div>`).join('') || '<div class="git-empty">No custom commands yet.</div>'}`;
    } else if (tab === 'agents') {
      body = `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="new-agent">${icon('i-plus')}<span>New subagent</span></button><button type="button" class="ws-btn sm" data-x="manager">${icon('i-sparkles')}<span>Agent Manager</span></button></div>
        <div class="dev-hint">The agent hands focused sub-tasks to these with the <code>task</code> tool. Coding CLIs installed on this computer run headless in the project folder (Plan mode runs them read-only).</div>
        <div class="dev-sec">Subagents</div>${scan.agents.map((a) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(a.name)}</b>${scopeBadge(a)}</div><p>${esc(clip(a.description, 200))}</p><div class="dev-it-a"><button type="button" class="ag-act" data-x="view" data-scope="${esc(a.scope)}" data-path="${esc(a.path)}">${icon('i-eye')}View</button>${a.scope !== 'user' && !a.owner ? `<button type="button" class="ag-act" data-x="edit" data-scope="${esc(a.scope)}" data-path="${esc(a.path)}">${icon('i-edit')}Edit</button><button type="button" class="ag-act" data-x="del" data-scope="${esc(a.scope)}" data-path="${esc(a.path)}">${icon('i-trash')}Delete</button>` : ''}</div></div>`).join('') || '<div class="git-empty">No custom subagents.</div>'}
        <div class="dev-sec">Coding CLIs on this computer</div>${scan.cli.map((c) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(c.name)}</b><code>${esc(c.bin)}</code></div></div>`).join('') || '<div class="git-empty">None found. Install Claude Code, Codex, OpenCode, Gemini CLI, DeepSeek Harness (dsh), Cursor CLI, Qwen Code or Aider and they show up here.</div>'}`;
    } else if (tab === 'rules') {
      const trusted = projectHooksTrusted(project, scan);
      const ph = scan.hooks.filter((h) => h.scope === 'project');
      body = `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="edit-agents-md">${icon('i-edit')}<span>Edit AGENTS.md</span></button><button type="button" class="ws-btn sm" data-x="edit-global-rules">${icon('i-edit')}<span>Global rules</span></button><button type="button" class="ws-btn sm" data-x="edit-hooks">${icon('i-edit')}<span>Global hooks</span></button></div>
        <div class="dev-hint">Rules are always given to the agent: AGENTS.md, CLAUDE.md, GEMINI.md, .cursorrules, .cursor/rules, .github/copilot-instructions.md, plus global rules and your ~/.claude, ~/.codex and OpenCode rules.</div>
        <div class="dev-sec">Rules</div>${scan.rules.map((r) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(r.path)}</b>${scopeBadge(r)}</div><div class="dev-it-a"><button type="button" class="ag-act" data-x="view" data-scope="${esc(r.scope)}" data-path="${esc(r.path)}">${icon('i-eye')}View</button></div></div>`).join('') || '<div class="git-empty">No rules. Use /init in the Agent to write AGENTS.md.</div>'}
        <div class="dev-sec">Hooks <small>(Claude Code format: PreToolUse, PostToolUse, Stop, UserPromptSubmit)</small></div>
        ${ph.length ? `<div class="dev-trust ${trusted ? 'ok' : ''}">${trusted ? `${icon('i-check')}<span>This project's hooks are trusted and run automatically.</span><button type="button" class="ag-act" data-x="untrust-hooks">Turn off</button>` : `${icon('i-hand')}<span>This project has ${ph.length} hook file(s). They run shell commands, so they stay off until you trust them.</span><button type="button" class="ws-chip" data-x="trust-hooks">Trust and enable</button>`}</div>` : ''}
        ${scan.hooks.map((h) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(h.path)}</b>${scopeBadge(h)}</div><p>${esc(Object.entries(h.hooks).map(([ev, g]) => `${ev}: ${(Array.isArray(g) ? g : []).length}`).join(' · '))}</p><div class="dev-it-a"><button type="button" class="ag-act" data-x="view" data-scope="${esc(h.scope)}" data-path="${esc(h.path)}">${icon('i-eye')}View</button></div></div>`).join('') || '<div class="git-empty">No hooks.</div>'}
        <div class="dev-sec">Agent checks</div><label class="dev-toggle"><input type="checkbox" data-x="agent-lint"${lsGet('agent-lint', '1') === '1' ? ' checked' : ''}> After each file edit, check it for errors and tell the agent</label>`;
    } else if (tab === 'mcp') {
      let r = { servers: D.mcp.get(project) || [] };
      try { r = await dev('mcp/list', { project }); D.mcp.set(project, r.servers || []); } catch (err) { r.error = err.message; }
      body = `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="mcp-add">${icon('i-plus')}<span>Add server</span></button><button type="button" class="ws-btn sm" data-x="mcp-registry">${icon('i-globe')}<span>Browse registry</span></button><button type="button" class="ws-btn sm" data-x="mcp-config">${icon('i-edit')}<span>Edit mcp.json</span></button></div>
        <div class="dev-hint">MCP servers give the agent new tools (GitHub, databases, browsers, docs…). Astro reads its global <code>mcp.json</code>, plugins, and the project's <code>.mcp.json</code>, <code>.cursor/mcp.json</code>, <code>.vscode/mcp.json</code> and <code>opencode.json</code>. Project servers run commands, so they start only after you trust them.</div>
        ${r.error ? `<div class="ws-note error">${esc(r.error)}</div>` : ''}
        ${(r.servers || []).map((sv) => `<div class="dev-item mcp-${esc(sv.status)}"><div class="dev-it-h"><span class="dev-dot"></span><b>${esc(sv.name)}</b><span class="dev-scope">${esc(sv.status)}</span><span class="dev-scope s-${esc(sv.scope)}">${esc(sv.source)}</span></div><code class="dev-cmd">${esc(clip(sv.command, 200))}</code>${sv.error ? `<p class="dev-err">${esc(clip(sv.error, 400))}</p>` : ''}${(sv.tools || []).length ? `<details><summary>${sv.tools.length} tools</summary><ul class="dev-tools">${sv.tools.map((t) => `<li><b>${esc(t.name)}</b> ${esc(clip(t.description || '', 160))}</li>`).join('')}</ul></details>` : ''}<div class="dev-it-a">${sv.status === 'untrusted' ? `<button type="button" class="ws-chip" data-x="mcp-trust" data-name="${esc(sv.name)}">${icon('i-check')}Trust and start</button>` : sv.status === 'disabled' ? '' : `<button type="button" class="ag-act" data-x="mcp-restart" data-name="${esc(sv.name)}">${icon('i-refresh')}${sv.status === 'running' ? 'Restart' : 'Start'}</button>${sv.status === 'running' ? `<button type="button" class="ag-act" data-x="mcp-stop" data-name="${esc(sv.name)}">${icon('i-stop')}Stop</button>` : ''}${sv.scope === 'project' ? `<button type="button" class="ag-act" data-x="mcp-untrust" data-name="${esc(sv.name)}">Untrust</button>` : ''}`}</div></div>`).join('') || '<div class="git-empty">No MCP servers yet.</div>'}`;
    } else if (tab === 'plugins') {
      body = `<form class="dev-inst" data-x="plugin-install"><input class="input" name="repo" placeholder="GitHub repo: owner/name or URL" spellcheck="false" aria-label="GitHub repository"><button type="submit" class="ws-btn sm primary">${icon('i-download')}<span>Install</span></button></form>
        <div class="dev-hint">Plugins bundle skills, commands, subagents, hooks, MCP servers, themes and snippets. Astro installs Claude Code plugins and plugin marketplaces from GitHub, and Astro plugins (plugin.json + optional main.js / style.css). Scripts in a plugin run only if you allow them.</div>
        ${scan.plugins.map((p) => { const m = p.manifest || {}; const parts = [scan.skills.filter((x) => x.owner === p.name).length && `${scan.skills.filter((x) => x.owner === p.name).length} skills`, scan.commands.filter((x) => x.owner === p.name).length && `${scan.commands.filter((x) => x.owner === p.name).length} commands`, scan.agents.filter((x) => x.owner === p.name).length && `${scan.agents.filter((x) => x.owner === p.name).length} agents`, scan.hooks.some((x) => x.owner === p.name) && 'hooks', scan.scripts.some((x) => x.owner === p.name) && 'script', scan.themes.some((x) => x.owner === p.name) && 'themes'].filter(Boolean); const hasScript = (D.lastRawScripts || []).includes(p.name) || scan.scripts.some((x) => x.owner === p.name); return `<div class="dev-item${p.enabled ? '' : ' off'}"><div class="dev-it-h"><b>${esc(m.displayName || m.name || p.name)}</b>${m.version ? `<span class="dev-scope">v${esc(m.version)}</span>` : ''}<span class="grow"></span><label class="switch sm"><input type="checkbox" data-x="plugin-toggle" data-name="${esc(p.name)}"${p.enabled ? ' checked' : ''}><span></span></label></div><p>${esc(clip(m.description || '', 220))}</p><small>${esc(parts.join(' · ') || 'no parts found')}</small><div class="dev-it-a">${hasScript ? `<label class="dev-toggle"><input type="checkbox" data-x="plugin-script" data-name="${esc(p.name)}"${lsGet(`script:${p.name}`) === '1' ? ' checked' : ''}> Allow its script (full access to Astro AI)</label>` : ''}<button type="button" class="ag-act" data-x="del" data-scope="global" data-path="plugins/${esc(p.name)}">${icon('i-trash')}Remove</button></div></div>`; }).join('') || '<div class="git-empty">No plugins yet. Try anthropics/claude-code (its plugins folder is a marketplace).</div>'}`;
    } else if (tab === 'themes' || tab === 'snippets') {
      const isTheme = tab === 'themes';
      const active = lsGet('theme', '');
      const installed = isTheme ? scan.themes : scan.snippets;
      body = `<form class="dev-inst" data-x="vsx-search" data-cat="${isTheme ? 'Themes' : 'Snippets'}"><input class="input" name="q" placeholder="Search Open VSX ${isTheme ? 'themes' : 'snippets'}" value="${esc(D.vsx?.cat === tab ? D.vsx.q : '')}" aria-label="Search"><button type="submit" class="ws-btn sm">${icon('i-search')}<span>Search</span></button></form>
        <div class="dev-hint">${isTheme ? 'Colour themes from VS Code extensions on Open VSX, applied to the editor.' : 'Snippets from VS Code extensions on Open VSX, or your own: type the prefix and pick it from the completion list.'} Extensions that run code are not supported.</div>
        ${D.vsx?.cat === tab ? `<div class="dev-sec">Results</div>${D.vsx.busy ? '<div class="git-empty">Searching…</div>' : D.vsx.error ? `<div class="ws-note error">${esc(D.vsx.error)}</div>` : (D.vsx.items || []).map((x) => `<div class="dev-item"><div class="dev-it-h"><b>${esc(x.displayName || x.name)}</b><span class="dev-scope">${esc(x.namespace)}</span><span class="dev-scope">${Number(x.downloadCount || 0).toLocaleString()} ↓</span></div><p>${esc(clip(x.description || '', 200))}</p><div class="dev-it-a"><button type="button" class="ws-chip" data-x="vsx-install" data-ns="${esc(x.namespace)}" data-name="${esc(x.name)}">${icon('i-download')}Install</button></div></div>`).join('') || '<div class="git-empty">Nothing found.</div>'}` : ''}
        <div class="dev-sec">Installed</div>
        ${isTheme ? `<button type="button" class="dev-item dev-theme${active ? '' : ' on'}" data-x="theme-use" data-path="">${icon('i-moon')}<b>Astro (default)</b></button>` : `<div class="dev-acts"><button type="button" class="ws-btn sm" data-x="new-snippets">${icon('i-plus')}<span>My snippets</span></button></div>`}
        ${installed.map((t) => isTheme ? `<div class="dev-item dev-theme${active === t.path ? ' on' : ''}"><button type="button" class="dev-tbtn" data-x="theme-use" data-path="${esc(t.path)}"><span class="dev-swatch" style="background:${esc(t.theme.colors?.['editor.background'] || '#222')};color:${esc(t.theme.colors?.['editor.foreground'] || '#ddd')}">Aa</span><b>${esc(t.theme.name || baseName(t.path))}</b><span class="dev-scope">${esc(t.theme.type || '')}</span></button><button type="button" class="ag-act" data-x="del" data-scope="global" data-path="${esc(t.path)}">${icon('i-trash')}</button></div>`
          : `<div class="dev-item"><div class="dev-it-h"><b>${esc(t.data.name || baseName(t.path))}</b><span class="dev-scope">${esc((t.data.language || '*'))}</span><span class="dev-scope">${Object.keys(t.data.items || {}).length} snippets</span></div><div class="dev-it-a"><button type="button" class="ag-act" data-x="edit" data-scope="global" data-path="${esc(t.path)}">${icon('i-edit')}Edit</button><button type="button" class="ag-act" data-x="del" data-scope="global" data-path="${esc(t.path)}">${icon('i-trash')}Remove</button></div></div>`).join('') || (isTheme ? '' : '<div class="git-empty">No snippets yet.</div>')}`;
    }
    box.innerHTML = `<div class="dev-tabs" role="tablist">${EXT_TABS.map(([id, label, ic]) => `<button type="button" class="${id === tab ? 'on' : ''}" data-xtab="${id}" title="${label}">${icon(ic)}<span>${label}</span></button>`).join('')}</div><div class="dev-body">${body}</div>`;
    const lq = box.querySelector('#lib-q');
    if (lq) lq.addEventListener('input', () => { D.lib = { ...(D.lib || {}), q: lq.value }; libFilter(); });
  }
  function onExtClick(e) {
    const t = e.target.closest('[data-xtab]');
    if (t) { D.extTab = t.dataset.xtab; lsSet('ext-tab', D.extTab); renderExt(); return; }
    const b = e.target.closest('[data-x]'); if (!b || b.tagName === 'FORM' || b.tagName === 'INPUT') return;
    extAction(b.dataset.x, b.dataset).catch((err) => A().toast(err.message, 'error'));
  }
  function onExtChange(e) {
    const b = e.target.closest('input[data-x]'); if (!b) return;
    if (b.dataset.x === 'plugin-toggle') extAction('plugin-toggle', { name: b.dataset.name, on: b.checked }).catch((err) => A().toast(err.message, 'error'));
    if (b.dataset.x === 'plugin-script') { lsSet(`script:${b.dataset.name}`, b.checked ? '1' : null); A().toast(b.checked ? 'The plugin script runs after a reload' : 'Plugin script turned off (reload to unload it)'); }
    if (b.dataset.x === 'agent-lint') lsSet('agent-lint', b.checked ? '1' : '0');
  }
  function onExtSubmit(e) {
    const f = e.target.closest('form[data-x]'); if (!f) return;
    e.preventDefault();
    const fd = Object.fromEntries(new FormData(f));
    extAction(f.dataset.x, { ...f.dataset, ...fd }).catch((err) => A().toast(err.message, 'error'));
  }
  async function readExt(scope, path) { return (await dev('ext/read', { scope, project: A().ws.project, path })).content; }
  async function writeExt(scope, path, content, base64) { await dev('ext/write', { scope, project: A().ws.project, path, content, base64: Boolean(base64) }); invalidate(); }
  function editorModal(title, text, onSave, readOnly) {
    A().showModal(`<h2 id="modal-title">${esc(title)}</h2><textarea class="input dev-ta" id="dev-ta" spellcheck="false"${readOnly ? ' readonly' : ''}>${esc(text)}</textarea><div class="btn-row">${readOnly ? '' : '<button type="button" class="btn primary" id="dev-save">Save</button>'}<button type="button" class="btn ghost" id="modal-close">${readOnly ? 'Close' : 'Cancel'}</button></div>`);
    document.getElementById('modal-content')?.classList.add('wide');
    document.getElementById('modal-close').onclick = () => A().closeModal();
    if (!readOnly) document.getElementById('dev-save').onclick = async () => { try { await onSave(document.getElementById('dev-ta').value); A().closeModal(); A().toast('Saved'); renderExt(true); } catch (err) { A().toast(err.message, 'error'); } };
  }
  const slug = (s) => String(s || '').trim().toLowerCase().replace(/[^a-z0-9._-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 60);
  async function askScope() {
    const project = A().ws.project;
    if (!project) return 'global';
    return new Promise((resolve) => {
      A().openMenu(document.activeElement || document.body, [{ title: 'Save where?' }, { label: 'This project', icon: 'i-folder', sub: `${project} (.astro/…, shared with the repo)`, onClick: () => resolve('project') }, { label: 'Everywhere', icon: 'i-globe', sub: 'Astro Extensions (all projects)', onClick: () => resolve('global') }], { width: 300, onClose: () => resolve(null) });
    });
  }
  async function extAction(act, d) {
    const a = A(); const project = a.ws.project;
    switch (act) {
      case 'view': editorModal(d.path, await readExt(d.scope, d.path), null, true); return;
      case 'lib-open': await libOpen(d.id); return;
      case 'lib-kind': lsSet('lib-kind', d.kind); renderExt(); return;
      case 'lib-refresh': await libLoad(true); renderExt(); return;
      case 'lib-remove': {
        const ok = await a.wsDialog({ title: `Remove ${d.id}?`, text: 'Only the files the library installed are removed; anything you added stays.', ok: 'Remove', danger: true, input: false });
        if (!ok) return;
        await dev('library/uninstall', { id: d.id }); invalidate(); if (D.lib) D.lib.at = 0; A().toast('Removed'); renderExt(true); return;
      }
      case 'edit': editorModal(d.path, await readExt(d.scope, d.path), (t) => writeExt(d.scope, d.path, t)); return;
      case 'del': {
        const ok = await a.wsDialog({ title: `Delete ${baseName(d.path)}?`, text: d.path, ok: 'Delete', danger: true, input: false });
        if (!ok) return;
        await dev('ext/delete', { scope: d.scope, project, path: d.path }); invalidate(); renderExt(true); applyLooks(); return;
      }
      case 'new-skill': {
        const name = slug(await a.wsDialog({ title: 'New skill', text: 'A short name, like pdf-forms or release-notes.', placeholder: 'my-skill', ok: 'Next' }));
        if (!name) return;
        const scope = await askScope(); if (!scope) return;
        const path = scope === 'project' ? `.astro/skills/${name}/SKILL.md` : `skills/${name}/SKILL.md`;
        editorModal(path, `---\nname: ${name}\ndescription: What this skill does and when the agent should use it.\n---\n\n# ${name}\n\nStep-by-step instructions, conventions and examples for the agent.\n`, (t) => writeExt(scope, path, t));
        return;
      }
      case 'new-cmd': {
        const name = slug(await a.wsDialog({ title: 'New command', text: 'You run it by typing /name in the Agent.', placeholder: 'fix-issue', ok: 'Next' }));
        if (!name) return;
        const scope = await askScope(); if (!scope) return;
        const path = scope === 'project' ? `.astro/commands/${name}.md` : `commands/${name}.md`;
        editorModal(path, `---\ndescription: One line shown in /help\n---\n\nDo this for $ARGUMENTS: …\n`, (t) => writeExt(scope, path, t));
        return;
      }
      case 'new-agent': {
        const name = slug(await a.wsDialog({ title: 'New subagent', text: 'For example code-reviewer, test-writer, security-auditor.', placeholder: 'code-reviewer', ok: 'Next' }));
        if (!name) return;
        const scope = await askScope(); if (!scope) return;
        const path = scope === 'project' ? `.astro/agents/${name}.md` : `agents/${name}.md`;
        editorModal(path, `---\nname: ${name}\ndescription: When the main agent should hand work to this subagent.\n---\n\nYou are a ${name.replace(/-/g, ' ')}. Instructions…\n`, (t) => writeExt(scope, path, t));
        return;
      }
      case 'new-snippets': {
        const path = 'snippets/my-snippets.json';
        let text = ''; try { text = await readExt('global', path); } catch (_e) { text = JSON.stringify({ name: 'My snippets', language: '*', items: { 'Log a value': { prefix: 'log', body: ['console.log(${1:value});$0'], description: 'console.log' } } }, null, 2); }
        editorModal(path, text, (t) => { JSON.parse(t); return writeExt('global', path, t); });
        return;
      }
      case 'edit-agents-md': {
        if (!project) throw new Error('Open a project first');
        let text = ''; try { text = await readExt('project', 'AGENTS.md'); } catch (_e) { text = '# AGENTS.md\n\nGuidance for coding agents working in this project.\n\n## Commands\n\n## Code style\n'; }
        editorModal('AGENTS.md', text, (t) => writeExt('project', 'AGENTS.md', t));
        return;
      }
      case 'edit-global-rules': { let text = ''; try { text = await readExt('global', 'AGENTS.md'); } catch (_e) { text = '# Global rules\n\nRules the agent follows in every project.\n'; } editorModal('Global rules (all projects)', text, (t) => writeExt('global', 'AGENTS.md', t)); return; }
      case 'edit-hooks': {
        let text = ''; try { text = await readExt('global', 'hooks.json'); } catch (_e) { text = JSON.stringify({ hooks: { PostToolUse: [{ matcher: 'Write|Edit', hooks: [{ type: 'command', command: 'cat >/dev/null; echo formatted' }] }] } }, null, 2); }
        editorModal('Global hooks (hooks.json)', text, (t) => { JSON.parse(t); return writeExt('global', 'hooks.json', t); });
        return;
      }
      case 'trust-hooks': case 'untrust-hooks': trustProjectHooks(project, await getScan(project), act === 'trust-hooks'); renderExt(); return;
      case 'mcp-trust': await dev('mcp/trust', { project, server: d.name }); A().toast(`Starting ${d.name}…`); await dev('mcp/restart', { project, server: d.name }).catch((err) => A().toast(err.message, 'error')); renderExt(); return;
      case 'mcp-untrust': await dev('mcp/stop', { project, server: d.name }).catch(() => {}); await dev('mcp/trust', { project, server: d.name, trust: false }); renderExt(); return;
      case 'mcp-restart': { A().toast(`Starting ${d.name}…`); const r = await dev('mcp/restart', { project, server: d.name }); A().toast(r.status === 'running' ? `${d.name}: ${(r.tools || []).length} tools` : `${d.name}: ${clip(r.error, 120)}`, r.status === 'running' ? '' : 'error'); renderExt(); return; }
      case 'mcp-stop': await dev('mcp/stop', { project, server: d.name }); renderExt(); return;
      case 'mcp-config': { const r = await dev('mcp/config_get', {}); editorModal('Global MCP servers (mcp.json)', r.text, async (t) => { await dev('mcp/config_set', { text: t }); }); return; }
      case 'mcp-add': return mcpAddDialog();
      case 'mcp-registry': return mcpRegistry();
      case 'plugin-install': return installPlugin(String(d.repo || ''));
      case 'plugin-toggle': {
        let st = {}; try { st = JSON.parse(await readExt('global', 'plugins.json')) || {}; } catch (_e) { /* none yet */ }
        st[d.name] = Boolean(d.on);
        await writeExt('global', 'plugins.json', JSON.stringify(st, null, 2)); renderExt(true); applyLooks(); return;
      }
      case 'vsx-search': return vsxSearch(String(d.q || ''), d.cat);
      case 'vsx-install': return vsxInstall(d.ns, d.name);
      case 'theme-use': lsSet('theme', d.path || null); applyLooks(); renderExt(); return;
      case 'manager': return agentManager();
      default: break;
    }
  }

  // ---------- MCP: add + registry ----------
  async function mcpMerge(name, spec) {
    const r = await dev('mcp/config_get', {});
    let j = {}; try { j = JSON.parse(r.text) || {}; } catch (_e) { j = {}; }
    j.mcpServers = j.mcpServers || {}; j.mcpServers[name] = spec;
    await dev('mcp/config_set', { text: JSON.stringify(j, null, 2) });
  }
  function mcpAddDialog(pre = {}) {
    A().showModal(`<h2 id="modal-title">Add an MCP server</h2><p class="sub">Saved to Astro's global mcp.json (all projects). Use a command for local servers (stdio) or a URL for remote ones (Streamable HTTP).</p>
      <div class="dev-form"><label>Name<input class="input" id="mcp-name" value="${esc(pre.name || '')}" placeholder="github"></label>
      <label>Command or URL<input class="input" id="mcp-cmd" value="${esc(pre.cmd || '')}" placeholder="npx -y @modelcontextprotocol/server-filesystem ."></label>
      <label>Environment / headers <small>(one KEY=value per line; values stay on your Astro AI server)</small><textarea class="input" id="mcp-env" rows="3" placeholder="GITHUB_TOKEN=…">${esc(pre.env || '')}</textarea></label></div>
      <div class="btn-row"><button type="button" class="btn primary" id="mcp-save">Add and start</button><button type="button" class="btn ghost" id="modal-close">Cancel</button></div>`);
    document.getElementById('modal-close').onclick = () => A().closeModal();
    document.getElementById('mcp-save').onclick = async () => {
      const name = slug(document.getElementById('mcp-name').value); const cmd = document.getElementById('mcp-cmd').value.trim();
      if (!name || !cmd) { A().toast('Name and command or URL are needed', 'error'); return; }
      const kv = Object.fromEntries(document.getElementById('mcp-env').value.split('\n').map((l) => l.trim()).filter((l) => l.includes('=')).map((l) => [l.slice(0, l.indexOf('=')).trim(), l.slice(l.indexOf('=') + 1).trim()]));
      let spec;
      if (/^https?:\/\//.test(cmd)) spec = { type: 'http', url: cmd, ...(Object.keys(kv).length ? { headers: kv } : {}) };
      else { const parts = cmd.match(/"[^"]*"|'[^']*'|\S+/g).map((x) => x.replace(/^["']|["']$/g, '')); spec = { command: parts[0], args: parts.slice(1), ...(Object.keys(kv).length ? { env: kv } : {}) }; }
      try {
        await mcpMerge(name, spec); A().closeModal(); A().toast(`Starting ${name}…`);
        const r = await dev('mcp/restart', { project: A().ws.project, server: name });
        A().toast(r.status === 'running' ? `${name} is running: ${(r.tools || []).length} tools` : `${name} did not start: ${clip(r.error, 140)}`, r.status === 'running' ? '' : 'error');
      } catch (err) { A().toast(err.message, 'error'); }
      D.extTab = 'mcp'; renderExt();
    };
  }
  async function mcpRegistry(q = '') {
    A().showModal(`<h2 id="modal-title">MCP registry</h2><p class="sub">Servers from the official Model Context Protocol registry. Check the publisher before adding one: a server can run code on this computer.</p>
      <form class="dev-inst" id="reg-form"><input class="input" name="q" value="${esc(q)}" placeholder="Search (github, postgres, browser, docs…)" aria-label="Search"><button type="submit" class="ws-btn sm">${icon('i-search')}<span>Search</span></button></form><div class="dev-reg" id="reg-list"><p class="hint">Searching…</p></div>
      <div class="btn-row"><button type="button" class="btn ghost" id="modal-close">Close</button></div>`);
    document.getElementById('modal-content')?.classList.add('wide');
    document.getElementById('modal-close').onclick = () => A().closeModal();
    document.getElementById('reg-form').onsubmit = (e) => { e.preventDefault(); mcpRegistry(new FormData(e.target).get('q')); };
    try {
      const r = await dev('fetch', { url: `https://registry.modelcontextprotocol.io/v0/servers?limit=40${q ? `&search=${encodeURIComponent(q)}` : ''}` });
      const j = JSON.parse(r.text);
      const list = (j.servers || []).map((x) => x.server || x).filter((x) => x && x.name);
      D.reg = list;
      document.getElementById('reg-list').innerHTML = list.map((x, i) => {
        const pkg = (x.packages || [])[0]; const rem = (x.remotes || [])[0];
        const how = pkg ? `${pkg.registryType || pkg.registry_type || 'package'} · ${pkg.identifier || pkg.name}` : rem ? `remote · ${rem.url}` : 'no install info';
        return `<div class="dev-item"><div class="dev-it-h"><b>${esc(x.title || x.name)}</b><span class="dev-scope">v${esc(x.version || '?')}</span></div><p>${esc(clip(x.description || '', 220))}</p><small>${esc(how)}</small><div class="dev-it-a">${pkg || rem ? `<button type="button" class="ws-chip" data-reg="${i}">${icon('i-plus')}Add</button>` : ''}${x.repository?.url ? `<a class="ag-act" href="${esc(x.repository.url)}" target="_blank" rel="noopener noreferrer">${icon('i-git')}Source</a>` : ''}</div></div>`;
      }).join('') || '<p class="hint">Nothing found.</p>';
      document.getElementById('reg-list').onclick = (e) => {
        const b = e.target.closest('[data-reg]'); if (!b) return;
        const x = D.reg[Number(b.dataset.reg)]; const pkg = (x.packages || [])[0]; const rem = (x.remotes || [])[0];
        const name = slug(String(x.name).split('/').pop());
        let cmd = ''; let env = '';
        if (pkg) {
          const id = pkg.identifier || pkg.name; const type = pkg.registryType || pkg.registry_type;
          cmd = type === 'pypi' ? `uvx ${id}` : type === 'oci' ? `docker run -i --rm ${id}` : `npx -y ${id}${pkg.version ? `@${pkg.version}` : ''}`;
          env = (pkg.environmentVariables || pkg.environment_variables || []).map((v) => `${v.name}=`).join('\n');
        } else if (rem) { cmd = rem.url; env = (rem.headers || []).map((h) => `${h.name}=${h.value || ''}`).join('\n'); }
        mcpAddDialog({ name, cmd, env });
      };
    } catch (err) { document.getElementById('reg-list').innerHTML = `<p class="hint">${esc(err.message)}</p>`; }
  }

  // ---------- zip reader (plugins from GitHub, .vsix from Open VSX) ----------
  function b64ToBytes(b64) { const bin = atob(b64); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u; }
  function bytesToB64(u) { let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); }
  async function inflateRaw(data) {
    if (typeof DecompressionStream === 'undefined') throw new Error('This browser cannot unzip files (no DecompressionStream).');
    const ds = new DecompressionStream('deflate-raw');
    const out = new Response(new Blob([data]).stream().pipeThrough(ds));
    return new Uint8Array(await out.arrayBuffer());
  }
  function unzipIndex(u) {
    const dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
    let eocd = -1;
    for (let i = u.length - 22; i >= Math.max(0, u.length - 65557); i--) if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw new Error('Not a zip file.');
    const count = dv.getUint16(eocd + 10, true); let off = dv.getUint32(eocd + 16, true);
    const dec = new TextDecoder(); const entries = [];
    for (let n = 0; n < count; n++) {
      if (dv.getUint32(off, true) !== 0x02014b50) break;
      const method = dv.getUint16(off + 10, true); const csize = dv.getUint32(off + 20, true); const size = dv.getUint32(off + 24, true);
      const nl = dv.getUint16(off + 28, true); const xl = dv.getUint16(off + 30, true); const cl = dv.getUint16(off + 32, true); const lho = dv.getUint32(off + 42, true);
      const name = dec.decode(u.subarray(off + 46, off + 46 + nl));
      entries.push({ name, method, csize, size, lho });
      off += 46 + nl + xl + cl;
    }
    return entries;
  }
  async function unzipRead(u, e) {
    const dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
    const nl = dv.getUint16(e.lho + 26, true); const xl = dv.getUint16(e.lho + 28, true);
    const start = e.lho + 30 + nl + xl; const raw = u.subarray(start, start + e.csize);
    if (e.method === 0) return raw;
    if (e.method === 8) return inflateRaw(raw);
    throw new Error(`Unsupported zip compression (${e.method}).`);
  }
  const unzipText = async (u, e) => new TextDecoder().decode(await unzipRead(u, e));

  // ---------- plugins from GitHub ----------
  async function installPlugin(input) {
    const m = String(input).trim().match(/^(?:https?:\/\/github\.com\/)?([\w.-]+)\/([\w.-]+?)(?:\.git)?(?:\/tree\/([\w./-]+?))?\/?$/);
    if (!m) throw new Error('Use owner/name or a github.com URL.');
    const [, owner, repo, treeRef] = m;
    A().toast(`Downloading ${owner}/${repo}…`);
    let ref = treeRef ? treeRef.split('/')[0] : '';
    let sub = treeRef ? treeRef.split('/').slice(1).join('/') : '';
    if (!ref) { const info = JSON.parse((await dev('fetch', { url: `https://api.github.com/repos/${owner}/${repo}` })).text); if (info.message && !info.default_branch) throw new Error(`GitHub: ${info.message}`); ref = info.default_branch || 'main'; }
    const z = await dev('fetch', { url: `https://codeload.github.com/${owner}/${repo}/zip/refs/heads/${ref}`, binary: true });
    if (z.status !== 200) throw new Error(`GitHub download failed (HTTP ${z.status}).`);
    const u = b64ToBytes(z.base64);
    const entries = unzipIndex(u).filter((e) => !e.name.endsWith('/'));
    const top = (entries[0]?.name.split('/')[0] || '') + '/';
    const rel = (e) => e.name.slice(top.length);
    const find = (p) => entries.find((e) => rel(e) === p);
    // A marketplace lists several plugins: let the user pick.
    const mk = find(`${sub ? `${sub}/` : ''}.claude-plugin/marketplace.json`);
    if (mk && !find(`${sub ? `${sub}/` : ''}.claude-plugin/plugin.json`)) {
      const j = JSON.parse(await unzipText(u, mk));
      const plugins = (j.plugins || []).filter((p) => typeof p.source === 'string' && p.source.startsWith('./'));
      if (!plugins.length) throw new Error('This marketplace has no plugins stored in the repository.');
      const pick = await new Promise((resolve) => {
        A().showModal(`<h2 id="modal-title">${esc(j.name || `${owner}/${repo}`)}</h2><p class="sub">This repository is a plugin marketplace. Pick the plugins to install.</p><div class="dev-pick">${plugins.map((p, i) => `<label class="dev-item"><input type="checkbox" value="${i}"> <b>${esc(p.name)}</b><p>${esc(clip(p.description || '', 200))}</p></label>`).join('')}</div><div class="btn-row"><button type="button" class="btn primary" id="pk-ok">Install selected</button><button type="button" class="btn ghost" id="modal-close">Cancel</button></div>`);
        document.getElementById('modal-close').onclick = () => { A().closeModal(); resolve([]); };
        document.getElementById('pk-ok').onclick = () => { const sel = [...document.querySelectorAll('.dev-pick input:checked')].map((x) => plugins[Number(x.value)]); A().closeModal(); resolve(sel); };
      });
      for (const p of pick) await writePluginFiles(u, entries, rel, `${sub ? `${sub}/` : ''}${p.source.replace(/^\.\//, '').replace(/\/$/, '')}`, slug(p.name));
      if (pick.length) A().toast(`Installed ${pick.length} plugin(s)`);
    } else {
      await writePluginFiles(u, entries, rel, sub, slug(sub ? sub.split('/').pop() : repo));
      A().toast(`Installed ${repo}`);
    }
    invalidate(); D.extTab = 'plugins'; renderExt(true); applyLooks();
  }
  async function writePluginFiles(u, entries, rel, sub, name) {
    const pre = sub ? `${sub.replace(/\/$/, '')}/` : '';
    const files = entries.filter((e) => rel(e).startsWith(pre) && !/(^|\/)(\.git|node_modules)\//.test(rel(e)));
    if (!files.length) throw new Error(`Nothing found in ${sub || 'the repository'}.`);
    if (files.length > 1500) throw new Error('That plugin is too big (over 1,500 files).');
    let total = 0;
    for (const e of files) {
      total += e.size; if (total > 25e6) throw new Error('That plugin is too big (over 25 MB).');
      const data = await unzipRead(u, e);
      await dev('ext/write', { scope: 'global', path: `plugins/${name}/${rel(e).slice(pre.length)}`, content: bytesToB64(data), base64: true });
    }
  }

  // ---------- Open VSX themes + snippets ----------
  async function vsxSearch(q, cat) {
    D.vsx = { q, cat: cat === 'Themes' ? 'themes' : 'snippets', busy: true }; renderExt();
    try {
      const r = await dev('fetch', { url: `https://open-vsx.org/api/-/search?size=24&sortBy=downloadCount&sortOrder=desc&category=${encodeURIComponent(cat)}${q ? `&query=${encodeURIComponent(q)}` : ''}` });
      D.vsx.items = JSON.parse(r.text).extensions || [];
    } catch (err) { D.vsx.error = err.message; }
    D.vsx.busy = false; renderExt();
  }
  const VSC_LANG = { javascript: ['js', 'mjs', 'cjs'], javascriptreact: ['jsx'], typescript: ['ts', 'mts', 'cts'], typescriptreact: ['tsx'], python: ['py'], cpp: ['cpp', 'cc', 'cxx', 'hpp', 'hh', 'h'], c: ['c', 'h'], html: ['html', 'htm'], css: ['css'], scss: ['scss'], less: ['less'], json: ['json'], jsonc: ['jsonc', 'json'], markdown: ['md'], go: ['go'], rust: ['rs'], java: ['java'], php: ['php'], ruby: ['rb'], shellscript: ['sh', 'bash', 'zsh'], yaml: ['yml', 'yaml'], vue: ['vue'], svelte: ['svelte'], sql: ['sql'], lua: ['lua'], csharp: ['cs'], kotlin: ['kt'], dart: ['dart'], swift: ['swift'], xml: ['xml', 'svg'], dockerfile: ['dockerfile'], toml: ['toml'] };
  async function vsxInstall(ns, name) {
    A().toast(`Downloading ${ns}.${name}…`);
    const meta = JSON.parse((await dev('fetch', { url: `https://open-vsx.org/api/${encodeURIComponent(ns)}/${encodeURIComponent(name)}` })).text);
    if (!meta.files?.download) throw new Error('This extension has no download.');
    const z = await dev('fetch', { url: meta.files.download, binary: true });
    const u = b64ToBytes(z.base64);
    const entries = unzipIndex(u);
    const get = (p) => entries.find((e) => e.name === p);
    const pkgE = get('extension/package.json'); if (!pkgE) throw new Error('Not a VS Code extension.');
    const pkg = jsonc(await unzipText(u, pkgE));
    // VS Code extensions keep display names in package.nls.json ("%themeLabel%" → "Night Owl").
    let nls = {}; const nlsE = get('extension/package.nls.json');
    if (nlsE) { try { nls = jsonc(await unzipText(u, nlsE)) || {}; } catch (_e) { nls = {}; } }
    const L = (s) => { if (typeof s !== 'string') return s; const m = s.match(/^%(.+)%$/); if (!m) return s; const v = nls[m[1]]; return typeof v === 'string' ? v : (v && typeof v.message === 'string' ? v.message : s.slice(1, -1)); };
    pkg.displayName = L(pkg.displayName); pkg.description = L(pkg.description);
    const c = pkg.contributes || {};
    for (const t of c.themes || []) t.label = L(t.label);
    const norm = (base, p) => { const parts = `${base}/${String(p).replace(/^\.\//, '')}`.split('/'); const out = []; for (const x of parts) { if (x === '..') out.pop(); else if (x && x !== '.') out.push(x); } return out.join('/'); };
    let n = 0;
    for (const t of c.themes || []) {
      const loadTheme = async (path, depth = 0) => {
        const e = get(path); if (!e || depth > 3) return {};
        const j = jsonc(await unzipText(u, e));
        if (j.include) { const parent = await loadTheme(norm(path.split('/').slice(0, -1).join('/'), j.include), depth + 1); return { ...parent, ...j, colors: { ...(parent.colors || {}), ...(j.colors || {}) }, tokenColors: [...(parent.tokenColors || []), ...(Array.isArray(j.tokenColors) ? j.tokenColors : [])] }; }
        return j;
      };
      const th = await loadTheme(norm('extension', t.path));
      if (!th.colors && !th.tokenColors) continue;
      const out = { name: t.label || th.name || pkg.displayName, type: /light/i.test(t.uiTheme || th.type || '') && !/dark/i.test(t.uiTheme || '') ? 'light' : 'dark', colors: th.colors || {}, tokenColors: Array.isArray(th.tokenColors) ? th.tokenColors : [], source: `${ns}.${name}@${meta.version}` };
      await dev('ext/write', { scope: 'global', path: `themes/${slug(`${ns}.${name}-${t.label || th.name || n}`)}.json`, content: JSON.stringify(out) }); n++;
    }
    for (const sn of c.snippets || []) {
      const e = get(norm('extension', sn.path)); if (!e) continue;
      let items = {}; try { items = jsonc(await unzipText(u, e)); } catch (_e) { continue; }
      await dev('ext/write', { scope: 'global', path: `snippets/${slug(`${ns}.${name}-${sn.language}`)}.json`, content: JSON.stringify({ name: `${pkg.displayName || name} (${sn.language})`, language: sn.language, items, source: `${ns}.${name}@${meta.version}` }) }); n++;
    }
    invalidate();
    if (!n) throw new Error('This extension has no themes or snippets Astro can use.');
    A().toast(`Installed ${n} item(s) from ${pkg.displayName || name}`);
    renderExt(true); applyLooks();
  }

  // ---------- applying themes, snippets, plugin styles + scripts ----------
  const TOKEN_MAP = {
    kw: ['keyword.control', 'keyword', 'storage.type', 'storage.modifier', 'storage'], str: ['string.quoted', 'string'], num: ['constant.numeric', 'constant.language', 'constant'],
    com: ['comment.line', 'comment'], fn: ['entity.name.function', 'support.function', 'meta.function-call'], type: ['entity.name.type', 'entity.name.class', 'support.type', 'support.class', 'entity.name'],
    prop: ['variable.other.property', 'support.type.property-name', 'meta.object-literal.key', 'variable.other.object.property'], tag: ['entity.name.tag'], op: ['keyword.operator', 'punctuation'],
    meta: ['meta.preprocessor', 'keyword.control.directive', 'meta.decorator', 'entity.name.function.decorator'], def: ['variable.other', 'variable', 'variable.parameter'],
  };
  function tokenColor(theme, want) {
    let best = null; let bestLen = -1;
    for (const rule of theme.tokenColors || []) {
      const fg = rule.settings?.foreground; if (!fg) continue;
      const scopes = Array.isArray(rule.scope) ? rule.scope : String(rule.scope || '').split(',');
      for (let sc of scopes) { sc = sc.trim(); if (!sc || sc.includes(' ')) continue; if ((want === sc || want.startsWith(`${sc}.`)) && sc.length > bestLen) { best = fg; bestLen = sc.length; } }
    }
    return best;
  }
  function themeCss(t) {
    const c = t.colors || {}; const v = {};
    const set = (k, val) => { if (val) v[k] = val; };
    set('--cm-fg', c['editor.foreground']); set('--cm-gutter', c['editorLineNumber.foreground']); set('--cm-sel', c['editor.selectionBackground']);
    set('--cm-active', c['editor.lineHighlightBackground']); set('--cm-caret', c['editorCursor.foreground']); set('--cm-pop', c['editorWidget.background'] || c['editorSuggestWidget.background']);
    set('--cm-pop-sel', c['list.activeSelectionBackground'] || c['editorSuggestWidget.selectedBackground']); set('--cm-err', c['editorError.foreground']); set('--cm-match', c['editor.findMatchHighlightBackground']);
    set('--cm-match-sel', c['editor.findMatchBackground']); set('--cm-selmatch', c['editor.selectionHighlightBackground']); set('--cm-line', c['editorWidget.border'] || c['panel.border']);
    set('--cm-bracket', c['editorBracketMatch.background']);
    for (const [k, scopes] of Object.entries(TOKEN_MAP)) { for (const s of scopes) { const col = tokenColor(t, s); if (col) { v[`--cm-${k}`] = col; break; } } }
    const bg = c['editor.background'];
    return `#ws-cm .cm-editor, .ws-diff .cm-editor { ${Object.entries(v).map(([k, val]) => `${k}: ${val};`).join(' ')} }${bg ? ` #ws-cm, #ws-cm .cm-gutters { background: ${bg} !important; } #ws-cm .cm-editor { color: var(--cm-fg); }` : ''}`;
  }
  function styleNode(id) { let n = D.styleNodes.get(id); if (!n) { n = document.createElement('style'); n.dataset.dev = id; document.head.append(n); D.styleNodes.set(id, n); } return n; }
  async function applyLooks() {
    if (!A()) return;
    const scan = await getScan(A().ws.project || '');
    const want = lsGet('theme', '');
    const th = scan.themes.find((t) => t.path === want);
    styleNode('theme').textContent = th ? themeCss(th.theme) : '';
    styleNode('plugins').textContent = scan.styles.map((s) => `/* ${s.owner} */\n${s.content}`).join('\n');
    const sets = scan.snippets.map((s) => ({ langs: !s.data.language || s.data.language === '*' ? ['*'] : (VSC_LANG[s.data.language] || [s.data.language]), items: s.data.items || {} }));
    A().ws.cm?.setSnippets?.(sets);
    for (const sc of scan.scripts) runPluginScript(sc);
  }
  const ranScripts = new Set();
  function runPluginScript(sc) {
    if (ranScripts.has(sc.owner) || lsGet(`script:${sc.owner}`) !== '1') return;
    ranScripts.add(sc.owner);
    const api = {
      version: 1, toast: (...x) => A().toast(...x), apiJson: (...x) => A().apiJson(...x), project: () => A().ws.project,
      registerCommand: (label, run, sub) => { D.pluginCommands.push({ label: String(label), sub: sub || `Plugin: ${sc.owner}`, icon: 'i-puzzle', run }); },
      registerTool: (name, fn) => { const n = String(name).toLowerCase().replace(/[^a-z0-9_]/g, '_'); if (!tools[n]) { tools[n] = async (s, call, item) => String(await fn(call, { project: s.project })); D.pluginTools[n] = sc.owner; } },
    };
    try { new Function('astro', `"use strict";\n${sc.content}\n//# sourceURL=astro-plugin-${sc.owner}.js`)(api); } catch (err) { console.error(err); A().toast(`Plugin ${sc.owner}: ${err.message}`, 'error'); }
  }

  // ---------- editor: AI ghost text + inline edit ----------
  function stripFences(t) { const m = String(t || '').match(/```[^\n]*\n([\s\S]*?)```/); return (m ? m[1] : String(t || '')).replace(/\n$/, ''); }
  let ghostCtl = null;
  async function ghostSuggest({ prefix, suffix, path }) {
    if (ghostCtl) ghostCtl.abort();
    const ctl = new AbortController(); ghostCtl = ctl;
    const text = await oneShot(`You are a code completion engine. Continue the code at <CURSOR> in ${path || 'the file'}. Reply with ONLY the text to insert at the cursor (at most 4 lines, no explanation, no code fences, do not repeat text before the cursor). If nothing useful fits, reply with nothing.\n\n${prefix.slice(-2500)}<CURSOR>${suffix.slice(0, 600)}`, ctl.signal, 160);
    if (ctl.signal.aborted) return '';
    return stripFences(text).replace(/^\s*<CURSOR>/, '').slice(0, 600);
  }
  function setGhost(on) { lsSet('ghost', on ? '1' : '0'); A()?.ws.cm?.setInlineSuggest?.(on ? ghostSuggest : null); A()?.toast(on ? 'AI inline suggestions on (Tab accepts)' : 'AI inline suggestions off'); }
  async function inlineEdit(sel) {
    const a = A(); const cm = a.ws.cm; if (!cm) return;
    const doc = cm.getDoc(); let { from, to } = sel;
    if (from === to) { from = doc.lastIndexOf('\n', from - 1) + 1; const e = doc.indexOf('\n', to); to = e < 0 ? doc.length : e; }
    const orig = doc.slice(from, to);
    const instr = await a.wsDialog({ title: 'Edit with AI', text: orig.trim() ? `Lines ${sel.line}${sel.endLine !== sel.line ? `–${sel.endLine}` : ''} of ${a.ws.active}. Describe the change.` : `Line ${sel.line} of ${a.ws.active}. Describe the code to write.`, placeholder: 'e.g. add error handling, convert to async/await, add JSDoc', ok: 'Edit' });
    if (!instr) return;
    a.toast('Editing…');
    try {
      const out = await oneShot(`Rewrite the SELECTED code in ${a.ws.active} according to the instruction. Reply with ONLY the replacement code for the selection (keep the indentation style, no explanation, no code fences).\n\nINSTRUCTION: ${instr}\n\nCODE BEFORE THE SELECTION:\n${doc.slice(Math.max(0, from - 3000), from)}\n<<<SELECTED>>>\n${orig}\n<<<END SELECTED>>>\nCODE AFTER THE SELECTION:\n${doc.slice(to, to + 1500)}`, null, 4000);
      const code = stripFences(out);
      if (!code.trim() && orig.trim()) { a.toast('The AI returned nothing', 'error'); return; }
      if (cm.current() !== a.ws.active || cm.getDoc() !== doc) { a.toast('The file changed while the AI was working; edit not applied', 'error'); return; }
      cm.replaceRange(from, to, code);
      a.toast('Edited (Ctrl+Z to undo)');
    } catch (err) { a.toast(err.message, 'error'); }
  }

  // ---------- Agent Manager (Antigravity-style: many agents, one view) ----------
  function agentManager() {
    const a = A();
    const all = [...a.AG.sessions.values()].sort((x, y) => (a.AG.runs.has(y.id) - a.AG.runs.has(x.id)) || (y.updated - x.updated)).slice(0, 40);
    a.showModal(`<h2 id="modal-title">Agent Manager</h2><p class="sub">Every agent session opened in this window. Sessions run in parallel: start several jobs in different projects or sessions and watch them here.</p>
      <div class="dev-mgr">${all.map((s) => `<div class="dev-item"><div class="dev-it-h"><span class="dev-dot ${a.AG.runs.has(s.id) ? 'run' : s.status === 'done' ? 'ok' : s.status === 'error' ? 'bad' : ''}"></span><b>${esc(clip(s.title, 80))}</b><span class="dev-scope">${esc(s.project)}</span><span class="dev-scope">${esc(s.status)}</span><span class="dev-scope">${esc(MODES[modeOf(s)].label)}</span></div><small>${s.steps || 0} steps · ${s.tools || 0} tool calls${s.todos?.length ? ` · to-dos ${s.todos.filter((t) => t.status === 'done').length}/${s.todos.length}` : ''}</small><div class="dev-it-a"><button type="button" class="ag-act" data-mgr-open="${esc(s.id)}">${icon('i-eye')}Open</button>${a.AG.runs.has(s.id) ? `<button type="button" class="ag-act" data-mgr-stop="${esc(s.id)}">${icon('i-stop')}Stop</button>` : ''}</div></div>`).join('') || '<p class="hint">No agent sessions yet.</p>'}</div>
      <div class="btn-row"><button type="button" class="btn ghost" id="modal-close">Close</button></div>`);
    document.getElementById('modal-close').onclick = () => a.closeModal();
    document.querySelector('.dev-mgr').onclick = (e) => {
      const o = e.target.closest('[data-mgr-open]'); const st = e.target.closest('[data-mgr-stop]');
      if (st) { a.agStop(a.AG.sessions.get(st.dataset.mgrStop)); setTimeout(agentManager, 300); return; }
      if (o) { const s = a.AG.sessions.get(o.dataset.mgrOpen); a.closeModal(); a.AG.project = s.project; a.ls.set('astro_agent:project', s.project); a.AG.cur = s.id; a.switchView('agent'); }
    };
  }

  // ---------- palette ----------
  function paletteItems() {
    const ghostOn = lsGet('ghost') === '1';
    return [
      { label: 'Open folder…', icon: 'i-folder-open', sub: 'Work on a folder on this computer', run: openFolder },
      { label: 'Extensions: skills, MCP, plugins, themes', icon: 'i-puzzle', run: () => showSide('ext') },
      { label: 'Astro AI Library: install MCP servers, skills and plugins', icon: 'i-download', sub: 'Official, checked, one click', run: () => openLibrary() },
      { label: 'Problems: check the project for errors', icon: 'i-alert', run: () => { showSide('problems'); checkProblems(); } },
      { label: 'MCP servers', icon: 'i-puzzle', run: () => { D.extTab = 'mcp'; showSide('ext'); } },
      { label: 'Browse the MCP registry', icon: 'i-globe', run: () => mcpRegistry() },
      { label: 'Install a plugin from GitHub', icon: 'i-download', run: () => { D.extTab = 'plugins'; showSide('ext'); } },
      { label: 'Color theme (Open VSX)', icon: 'i-moon', run: () => { D.extTab = 'themes'; showSide('ext'); } },
      { label: 'Agent Manager', icon: 'i-sparkles', sub: 'All agent sessions at once', run: agentManager },
      { label: `${ghostOn ? 'Turn off' : 'Turn on'} AI inline suggestions`, icon: 'i-spark', sub: 'Ghost text in the editor; Tab accepts', run: () => setGhost(!ghostOn) },
      ...Object.entries(MODES).map(([id, m]) => ({ label: `Agent mode: ${m.label}`, icon: m.icon, sub: m.sub, run: () => { const s = A().agCurSession?.(); setMode(s?.project || A().AG.project || A().ws.project, id, s); A().toast(`${m.label} mode`); } })),
      ...D.pluginCommands,
    ];
  }
  async function showSide(name) {
    const a = A();
    a.switchView('workspace');
    if (a.ws.loadingAll) await a.ws.loadingAll;
    for (let i = 0; i < 40 && !document.querySelector(`[data-side="${name}"]`); i++) await sleep(50);
    a.wsSide(name);
  }

  // ---------- hooks for app.js ----------
  function side(name) { if (name === 'ext') renderExt(); else { if (D.probProject !== A().ws.project) { D.problems = []; D.checkers = null; } renderProblems(); } }
  function projectChanged(name) {
    if (D.probProject !== name) { D.problems = []; D.checkers = null; D.probProject = name; A()?.ws.cm?.clearProblems?.(); paintProbCount(); }
    applyLooks(); paintModeChips();
  }
  function editorReady(cm) {
    if (lsGet('ghost') === '1') cm.setInlineSuggest?.(ghostSuggest);
    applyLooks();
    const host = document.getElementById('ws-cm');
    if (host && !host._devProb) {
      host._devProb = true;
      let t = 0;
      host.addEventListener('keyup', () => {
        if (lsGet('prob-auto', '1') !== '1') return;
        clearTimeout(t);
        t = setTimeout(() => { const p = A().ws.active; if (p && CODE_EXT.test(p)) checkProblems([p]); }, 2600);
      });
    }
  }
  function init() {
    document.addEventListener('click', (e) => {
      const m = e.target.closest('[data-devmode]'); if (m) { modeMenu(m); return; }
      const rp = e.target.closest('[data-devrunplan]');
      if (rp) {
        const s = A().AG.sessions.get(rp.dataset.devrunplan); if (!s) return;
        const next = lsGet(`mode:${s.project}`) && lsGet(`mode:${s.project}`) !== 'plan' ? lsGet(`mode:${s.project}`) : isLinked(s.project) ? 'ask' : 'auto';
        s.mode = next; paintModeChips();
        A().agSend(s, 'Implement the plan above now, step by step. Verify each step (run, compile or test), then finish with the report and the done block.');
        return;
      }
      if (e.target.closest('#ws-ext')) onExtClick(e);
      if (e.target.closest('#ws-probs')) {
        const g = e.target.closest('[data-pgo]'); if (g) { const [p, l, c] = JSON.parse(g.dataset.pgo); A().wsGoto(p, l, c, 0); return; }
        if (e.target.closest('[data-prob="check"]')) checkProblems();
      }
    });
    document.addEventListener('change', (e) => {
      if (e.target.closest('#ws-ext')) onExtChange(e);
      const pa = e.target.closest('[data-prob="auto"]'); if (pa) lsSet('prob-auto', pa.checked ? '1' : '0');
    });
    document.addEventListener('submit', (e) => { if (e.target.closest('#ws-ext')) onExtSubmit(e); });
    setInterval(paintModeChips, 1500);
    paintModeChips();
    applyLooks();
    libFromHash();
    window.addEventListener('hashchange', libFromHash);
  }

  window.AstroDev = {
    init, tools, beforeTool, afterTool, onStop, brief, agentInput, mode: modeOf, setMode, side, projectChanged, editorReady, inlineEdit,
    openFolder, openLibrary, paletteItems, checkProblems, agentManager, mcpRegistry, installPlugin, vsxInstall, getScan, globRe, frontmatter, jsonc, unzipIndex, invalidate, applyLooks, renderExt,
  };
})();

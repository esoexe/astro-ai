// Astro AI for VS Code: runs the Astro AI agent on the open folder through the local Astro AI engine (/api/agent/*).
// No dependencies; plain Node + the VS Code API. Works with any editor that runs VS Code extensions (VSCodium, Cursor, Windsurf...).
'use strict';
const vscode = require('vscode');
const cp = require('child_process');

const LIVE = new Set(['running', 'waiting', 'queued']);
let out;            // output channel
let status;         // status bar item
let baseUrl = '';   // resolved engine URL
const followers = new Map();   // session id -> AbortController

function cfg() { return vscode.workspace.getConfiguration('astro'); }

// ---------- engine connection ----------
async function probe(url) {
  try {
    const ctl = new AbortController(); const t = setTimeout(() => ctl.abort(), 2500);
    const r = await fetch(`${url}/api/status`, { signal: ctl.signal, headers: { 'X-Astro-Client': '1' } });
    clearTimeout(t);
    return r.status === 200 || r.status === 401;
  } catch (_e) { return false; }
}
async function findServer() {
  const set = String(cfg().get('serverUrl') || process.env.ASTRO_URL || '').replace(/\/+$/, '');
  if (set) return (await probe(set)) ? set : '';
  for (const port of [47612, 47611, 4173]) { const u = `http://127.0.0.1:${port}`; if (await probe(u)) return u; }
  return '';
}
function astroBin() {
  const set = String(cfg().get('cliPath') || '');
  if (set) return set;
  return process.platform === 'win32' ? 'astro.exe' : 'astro';
}
async function ensureServer() {
  if (baseUrl && (await probe(baseUrl))) return baseUrl;
  baseUrl = await findServer();
  if (baseUrl) return baseUrl;
  // Start the engine that ships with the desktop app (`astro server start`) if the terminal app is installed.
  const ok = await new Promise((resolve) => {
    cp.execFile(astroBin(), ['server', 'start'], { timeout: 40000, windowsHide: true }, (err) => resolve(!err));
  });
  if (ok) baseUrl = await findServer();
  if (!baseUrl) {
    const pick = await vscode.window.showErrorMessage('Astro AI engine not found. Open the Astro AI desktop app (or install it so the `astro` command exists), or set "astro.serverUrl".', 'Get Astro AI', 'Settings');
    if (pick === 'Get Astro AI') vscode.env.openExternal(vscode.Uri.parse('https://esoexe.github.io/astro-ai/download.html'));
    if (pick === 'Settings') vscode.commands.executeCommand('workbench.action.openSettings', 'astro.');
    throw new Error('no engine');
  }
  return baseUrl;
}
async function api(route, body = {}, signal) {
  const url = await ensureServer();
  const headers = { 'Content-Type': 'application/json', 'X-Astro-Client': '1' };
  const token = String(cfg().get('token') || process.env.ASTRO_TOKEN || '');
  if (token) headers['X-Astro-Token'] = token;
  const r = await fetch(`${url}/api/${route}`, { method: 'POST', headers, body: JSON.stringify(body), signal });
  let j = {};
  try { j = await r.json(); } catch (_e) { /* empty body */ }
  if (!r.ok) { const e = new Error(j.error || `HTTP ${r.status}`); e.data = j; throw e; }
  return j;
}
const agent = (action, body, signal) => api(`agent/${action}`, body, signal);

// The Astro AI project for the open folder (opened in place, like the desktop app's Open folder).
async function projectFor(folder) {
  const fixed = String(cfg().get('project') || '');
  if (fixed) return fixed;
  const r = await api('dev/fs/open', { path: folder });
  return r.name;
}
function workspaceFolder() {
  const ed = vscode.window.activeTextEditor;
  const f = (ed && vscode.workspace.getWorkspaceFolder(ed.document.uri)) || (vscode.workspace.workspaceFolders || [])[0];
  if (!f) { vscode.window.showWarningMessage('Open a folder first: Astro AI works on the folder in place.'); return null; }
  return f.uri.fsPath;
}

// ---------- rendering ----------
function log(id, line) { out.appendLine(`[${id.slice(0, 6)}] ${line}`); }
function render(id, e) {
  switch (e.type) {
    case 'user': out.appendLine(''); log(id, `› ${e.text}`); break;
    case 'say': if (!e.final) log(id, `● ${String(e.text || '').trim()}`); break;
    case 'tool_done': log(id, `  ${e.ok ? '✓' : e.blocked ? '⊘' : '✗'} ${e.label}${e.added !== undefined ? ` +${e.added} −${e.removed}` : ''}${!e.ok && e.summary ? `\n      ${String(e.summary).replace(/^BLOCKED: /, '').slice(0, 400)}` : ''}`); break;
    case 'note': log(id, `  · ${e.text}`); break;
    case 'todo': (e.items || []).forEach((i) => log(id, `  ${i.status === 'done' ? '☑' : i.status === 'doing' ? '◐' : '☐'} ${i.text}`)); break;
    case 'review': log(id, `  Review: ${e.verdict}\n${e.text || ''}`); break;
    case 'answered': log(id, `  ↳ ${e.answer}`); break;
    case 'done': log(id, `✔ Done · ${(e.files || []).length} file(s) · ${e.steps} steps${e.model ? ` · ${e.model}` : ''}\n${e.text || ''}`); (e.files || []).forEach((f) => log(id, `  ${f.status === 'added' ? '+' : f.status === 'deleted' ? '−' : '~'} ${f.path}`)); break;
    case 'error': log(id, `✗ ${e.text}`); break;
    default: break;
  }
}

// ---------- following a session ----------
async function follow(id, opts = {}) {
  if (followers.has(id)) return;
  const ctl = new AbortController(); followers.set(id, ctl);
  let since = opts.since || 0; let live = ''; const asked = new Set();
  const pending = new Map();
  updateStatus();
  try {
    for (;;) {
      let r;
      try { r = await agent('events', { id, since, wait: 25, live }, ctl.signal); }
      catch (err) { if (ctl.signal.aborted) return; await new Promise((ok) => setTimeout(ok, 1500)); continue; }
      for (const e of r.events || []) {
        since = Math.max(since, e.seq);
        if (e.type === 'approval' || e.type === 'question') pending.set(e.id, e);
        render(id, e);
      }
      live = r.live || '';
      if (r.waiting && !asked.has(r.waiting) && pending.has(r.waiting)) {
        asked.add(r.waiting);
        answerPrompt(id, pending.get(r.waiting));
      }
      if (!LIVE.has(r.status) && !r.running) { finished(id, r); return; }
    }
  } finally { followers.delete(id); updateStatus(); }
}
async function answerPrompt(id, e) {
  let answer;
  if (e.type === 'approval') {
    if (e.preview) { out.appendLine(`[${id.slice(0, 6)}]   ┌ waiting for your approval\n${String(e.preview).split('\n').map((l) => `      │ ${l}`).join('\n')}`); out.show(true); }
    const pick = await vscode.window.showWarningMessage(`Astro AI wants to: ${e.text}`, { modal: false, detail: e.preview || '' }, 'Allow', 'Allow all in this run', 'Deny', 'Something else…');
    answer = pick === 'Something else…' ? await vscode.window.showInputBox({ prompt: 'What should the agent do instead?' }) : pick;
  } else {
    const opts = (e.options || []).map(String);
    if (opts.length) {
      const pick = await vscode.window.showQuickPick([...opts, 'Type an answer…'], { placeHolder: e.question, ignoreFocusOut: true });
      answer = pick === 'Type an answer…' ? await vscode.window.showInputBox({ prompt: e.question, ignoreFocusOut: true }) : pick;
    } else answer = await vscode.window.showInputBox({ prompt: e.question, ignoreFocusOut: true });
  }
  if (!answer) answer = e.type === 'approval' ? 'Deny' : 'No answer: use your best judgement.';
  try { await agent('answer', { id, qid: e.id, answer }); } catch (err) { vscode.window.showErrorMessage(err.message); }
}
async function finished(id, view) {
  let s = view;
  try { s = await agent('get', { id, since: 1e12 }); } catch (_e) { /* keep the view */ }
  const st = s.status;
  if (s.isolated && !s.discarded && st === 'done') {
    const pick = await vscode.window.showInformationMessage(`Astro AI agent ${id.slice(0, 6)} finished in an isolated copy (${s.files || 0} file(s)).`, 'Show diff', 'Merge', 'Discard');
    if (pick === 'Show diff') showDiff(id);
    if (pick === 'Merge') merge(id);
    if (pick === 'Discard') discard(id);
  } else if (st === 'done') {
    const pick = await vscode.window.showInformationMessage(`Astro AI: done (${s.files || 0} file(s) changed).`, 'Show log', 'Undo');
    if (pick === 'Show log') out.show(true);
    if (pick === 'Undo') undo(id);
  } else if (st === 'paused') {
    const pick = await vscode.window.showInformationMessage('Astro AI paused at the step limit.', 'Continue');
    if (pick === 'Continue') { await agent('send', { id, prompt: 'Continue.' }); follow(id); }
  } else if (st === 'error') vscode.window.showErrorMessage('The Astro AI agent stopped with an error. See the log.', 'Show log').then((p) => p && out.show(true));
}
function updateStatus() {
  const n = followers.size;
  status.text = n ? `$(sync~spin) Astro AI: ${n} agent${n === 1 ? '' : 's'}` : '$(sparkle) Astro AI';
  status.tooltip = n ? 'Agents working: click to see them' : 'Run the Astro AI agent';
  status.command = n ? 'astro.agents' : 'astro.run';
}

// ---------- commands ----------
async function startAgent(prompt, extra = {}) {
  const folder = workspaceFolder(); if (!folder) return null;
  const project = await projectFor(folder);
  const body = { project, prompt, mode: cfg().get('mode') || 'auto', review: !!cfg().get('review'), model: cfg().get('model') || '', ...extra };
  const r = await agent('start', body);
  out.show(true);
  follow(r.id);
  return r.id;
}
async function cmdRun(withSelection) {
  let context = '';
  const ed = vscode.window.activeTextEditor;
  if (withSelection && ed && !ed.selection.isEmpty) {
    const rel = vscode.workspace.asRelativePath(ed.document.uri, false);
    context = `\n\nContext: ${rel}, lines ${ed.selection.start.line + 1}-${ed.selection.end.line + 1}:\n\`\`\`\n${ed.document.getText(ed.selection).slice(0, 12000)}\n\`\`\``;
  }
  const task = await vscode.window.showInputBox({ prompt: withSelection ? 'What should Astro AI do with the selection?' : 'Give the Astro AI agent a job', placeHolder: 'e.g. add input validation and a test for it', ignoreFocusOut: true });
  if (!task) return;
  await vscode.workspace.saveAll(false);
  await startAgent(task + context, { isolate: !!cfg().get('isolate') });
}
async function cmdParallel() {
  const raw = await vscode.window.showInputBox({ prompt: 'Tasks for parallel agents, separated by |  (each runs in its own isolated copy)', placeHolder: 'add dark mode | write tests for utils | update the README', ignoreFocusOut: true });
  if (!raw) return;
  const tasks = raw.split('|').map((t) => t.trim()).filter(Boolean);
  await vscode.workspace.saveAll(false);
  for (const t of tasks) { try { await startAgent(t, { isolate: true, interactive: false }); } catch (err) { vscode.window.showErrorMessage(err.message); } }
  vscode.window.showInformationMessage(`${tasks.length} Astro AI agent(s) started. Each works in an isolated copy; you review and merge them when they finish.`);
}
async function pickSession(placeHolder, filter = () => true) {
  const folder = workspaceFolder(); if (!folder) return null;
  const project = await projectFor(folder);
  const r = await agent('list', { project });
  const items = (r.sessions || []).filter(filter).map((s) => ({
    label: `${LIVE.has(s.status) ? '$(sync~spin)' : s.status === 'done' ? '$(check)' : '$(circle-slash)'} ${s.title}`,
    description: `${s.status}${s.isolated ? ` · isolated${s.merged ? ' · merged' : ''}` : ''} · ${s.files || 0} file(s)`,
    detail: s.id, s,
  }));
  if (!items.length) { vscode.window.showInformationMessage('No Astro AI agents for this folder yet.'); return null; }
  const pick = await vscode.window.showQuickPick(items, { placeHolder, matchOnDescription: true });
  return pick ? pick.s : null;
}
async function cmdAgents() {
  const s = await pickSession('Astro AI agents for this folder'); if (!s) return;
  const acts = [];
  if (LIVE.has(s.status)) acts.push('Follow', 'Stop');
  if (s.isolated && !s.discarded) acts.push('Show diff', ...(LIVE.has(s.status) ? [] : ['Merge', 'Discard']));
  if (!LIVE.has(s.status)) acts.push('Show log', 'Follow-up…');
  if ((!s.isolated && s.checkpoint) || (s.isolated && s.merged)) acts.push('Undo');
  const a = await vscode.window.showQuickPick(acts, { placeHolder: s.title });
  if (a === 'Follow') { out.show(true); follow(s.id, { since: 0 }); }
  if (a === 'Stop') agent('stop', { id: s.id });
  if (a === 'Show diff') showDiff(s.id);
  if (a === 'Merge') merge(s.id);
  if (a === 'Discard') discard(s.id);
  if (a === 'Undo') undo(s.id);
  if (a === 'Show log') { const g = await agent('get', { id: s.id }); out.show(true); (g.events || []).forEach((e) => render(s.id, e)); }
  if (a === 'Follow-up…') { const t = await vscode.window.showInputBox({ prompt: 'Follow-up for this agent' }); if (t) { const g = await agent('get', { id: s.id, since: 1e12 }); await agent('send', { id: s.id, prompt: t }); out.show(true); follow(s.id, { since: g.seq || 0 }); } }
}
async function showDiff(id) {
  const r = await agent('diff', { id });
  const doc = await vscode.workspace.openTextDocument({ content: String(r.diff || 'No changes.').replace(/^diff --astro /gm, 'diff --git '), language: 'diff' });
  vscode.window.showTextDocument(doc, { preview: true });
}
async function merge(id) {
  try {
    const r = await agent('merge', { id, discard: true });
    vscode.window.showInformationMessage(`Merged ${(r.merged || []).length} file(s). The isolated copy was removed; "Astro AI: Agents" → Undo reverts it.`);
  } catch (err) {
    const c = (err.data && err.data.conflicts) || [];
    if (!c.length) { vscode.window.showErrorMessage(err.message); return; }
    const pick = await vscode.window.showWarningMessage(`${c.length} file(s) also changed in your folder since the agent started: ${c.map((x) => x.path || x).slice(0, 5).join(', ')}`, 'Merge the rest', 'Use the agent\'s version', 'Show diff');
    if (pick === 'Show diff') return showDiff(id);
    if (!pick) return;
    const r = await agent('merge', pick === 'Merge the rest' ? { id, partial: true } : { id, force: true, discard: true });
    vscode.window.showInformationMessage(`Merged ${(r.merged || []).length} file(s)${(r.conflicts || []).length ? `; left ${(r.conflicts || []).length} unchanged` : ''}.`);
  }
}
async function discard(id) { await agent('discard', { id }); vscode.window.showInformationMessage('Isolated copy discarded.'); }
async function undo(id) { const r = await agent('undo', { id }); vscode.window.showInformationMessage(`Restored ${r.project || 'the folder'} to the checkpoint.`); }
async function cmdStop() {
  const s = await pickSession('Stop which agent?', (x) => LIVE.has(x.status)); if (!s) return;
  await agent('stop', { id: s.id });
}
async function cmdOpenTerminal() {
  const folder = workspaceFolder();
  const t = vscode.window.createTerminal({ name: 'Astro AI', cwd: folder || undefined });
  t.show(); t.sendText(astroBin().includes(' ') ? `"${astroBin()}"` : astroBin());
}

function wrap(fn) { return async (...a) => { try { await fn(...a); } catch (err) { if (err.message !== 'no engine') vscode.window.showErrorMessage(`Astro AI: ${err.message}`); } }; }

function activate(context) {
  out = vscode.window.createOutputChannel('Astro AI Agent');
  status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  updateStatus(); status.show();
  context.subscriptions.push(out, status,
    vscode.commands.registerCommand('astro.run', wrap(() => cmdRun(false))),
    vscode.commands.registerCommand('astro.runSelection', wrap(() => cmdRun(true))),
    vscode.commands.registerCommand('astro.parallel', wrap(cmdParallel)),
    vscode.commands.registerCommand('astro.agents', wrap(cmdAgents)),
    vscode.commands.registerCommand('astro.stop', wrap(cmdStop)),
    vscode.commands.registerCommand('astro.terminal', wrap(cmdOpenTerminal)),
    { dispose: () => { for (const c of followers.values()) c.abort(); } });
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration('astro.serverUrl')) baseUrl = ''; }));
}
function deactivate() { for (const c of followers.values()) c.abort(); }
module.exports = { activate, deactivate, _test: { render, findServer } };

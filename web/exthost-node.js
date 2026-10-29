#!/usr/bin/env node
// A.S.T.R.O AI extension host (preview). Runs VS Code extensions from Open VSX with a compatible subset of the
// `vscode` API, in a separate Node process started by the engine. Messages are JSON-RPC with Content-Length framing
// over stdio, relayed to the browser (web/exthost.js) by the engine's /api/dev/lsp/* bridge.
//
//   Supported: commands, window messages / quick picks / input boxes / output channels / status bar items / progress,
//   the active text editor (selections, edit, insertSnippet), workspace folders / configuration / fs / documents /
//   findFiles / applyEdit, languages (completion, hover, definition, formatting, diagnostics), Memento state,
//   secrets, env, extensions, and the common classes (Uri, Position, Range, TextEdit, WorkspaceEdit, ...).
//   Not supported (stubbed, logged once): webviews, tree views, debuggers, tasks, terminals, custom editors,
//   notebooks, authentication, SCM, language-client based servers that need the full LSP glue.
'use strict';
const fs = require('fs');
const path = require('path');
const Module = require('module');
const { pathToFileURL, fileURLToPath } = require('url');

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const EXT_DIR = arg('--extensions', path.join(process.cwd(), '.vscode-ext'));
const STORAGE = arg('--storage', path.join(EXT_DIR, '..', 'vscode-storage'));
const ROOT = process.cwd();
const VERSION = '1.95.0';

// ------------------------------------------------------------------ stdio framing (stdout is ours only)
const realWrite = process.stdout.write.bind(process.stdout);
function send(msg) { const b = Buffer.from(JSON.stringify(msg), 'utf8'); realWrite(`Content-Length: ${b.length}\r\n\r\n`); realWrite(b); }
const logLine = (level, args) => { try { send({ jsonrpc: '2.0', method: 'log', params: { level, text: args.map((a) => (typeof a === 'string' ? a : a instanceof Error ? a.stack || a.message : safeJson(a))).join(' ').slice(0, 4000) } }); } catch (_e) { /* closed */ } };
function safeJson(v) { try { return JSON.stringify(v); } catch (_e) { return String(v); } }
console.log = (...a) => logLine('info', a); console.info = console.log; console.debug = console.log;
console.warn = (...a) => logLine('warn', a); console.error = (...a) => logLine('error', a);
process.stdout.write = (chunk) => { logLine('info', [String(chunk)]); return true; };
process.on('uncaughtException', (e) => logLine('error', ['Uncaught:', e]));
process.on('unhandledRejection', (e) => logLine('error', ['Unhandled rejection:', e]));

let inBuf = Buffer.alloc(0);
process.stdin.on('data', (d) => {
  inBuf = Buffer.concat([inBuf, d]);
  for (;;) {
    const he = inBuf.indexOf('\r\n\r\n'); if (he < 0) break;
    const m = /content-length:\s*(\d+)/i.exec(inBuf.slice(0, he).toString('ascii'));
    if (!m) { inBuf = inBuf.slice(he + 4); continue; }
    const len = Number(m[1]); if (inBuf.length < he + 4 + len) break;
    const body = inBuf.slice(he + 4, he + 4 + len).toString('utf8'); inBuf = inBuf.slice(he + 4 + len);
    let msg; try { msg = JSON.parse(body); } catch (_e) { continue; }
    onMessage(msg);
  }
});
process.stdin.on('end', () => process.exit(0));

let reqSeq = 0; const pending = new Map();
function request(method, params) { const id = `h${++reqSeq}`; return new Promise((resolve) => { pending.set(id, resolve); send({ jsonrpc: '2.0', id, method, params }); }); }
const notify = (method, params) => send({ jsonrpc: '2.0', method, params });

// ------------------------------------------------------------------ basic types
class Disposable { constructor(fn) { this._fn = fn; } static from(...d) { return new Disposable(() => d.forEach((x) => x && x.dispose && x.dispose())); } dispose() { if (this._fn) { const f = this._fn; this._fn = null; f(); } } }
class EventEmitter {
  constructor() { this._l = new Set(); this.event = (fn, thisArg, disposables) => { const h = thisArg ? fn.bind(thisArg) : fn; this._l.add(h); const d = new Disposable(() => this._l.delete(h)); if (Array.isArray(disposables)) disposables.push(d); return d; }; }
  fire(e) { for (const l of [...this._l]) { try { l(e); } catch (err) { console.error(err); } } }
  dispose() { this._l.clear(); }
}
class CancellationTokenSource { constructor() { const em = new EventEmitter(); this.token = { isCancellationRequested: false, onCancellationRequested: em.event }; this._em = em; } cancel() { this.token.isCancellationRequested = true; this._em.fire(); } dispose() {} }
const noToken = new CancellationTokenSource().token;

class Uri {
  constructor(scheme, authority, p, query, fragment) { this.scheme = scheme || 'file'; this.authority = authority || ''; this.path = p || '/'; this.query = query || ''; this.fragment = fragment || ''; }
  static file(p) { const abs = path.resolve(p); let up = abs.replace(/\\/g, '/'); if (!up.startsWith('/')) up = '/' + up; return new Uri('file', '', up); }
  static parse(s) {
    const m = /^([a-zA-Z][\w+.-]*):(?:\/\/([^/?#]*))?([^?#]*)(?:\?([^#]*))?(?:#(.*))?$/.exec(String(s));
    if (!m) return Uri.file(s);
    return new Uri(m[1], m[2], decodeURIComponent(m[3] || '/'), m[4], m[5]);
  }
  static joinPath(base, ...parts) { return base.with({ path: path.posix.join(base.path, ...parts) }); }
  static from(c) { return new Uri(c.scheme, c.authority, c.path, c.query, c.fragment); }
  get fsPath() { let p = this.path; if (/^\/[a-zA-Z]:/.test(p)) p = p.slice(1); return process.platform === 'win32' ? p.replace(/\//g, '\\') : p; }
  with(c) { return new Uri(c.scheme ?? this.scheme, c.authority ?? this.authority, c.path ?? this.path, c.query ?? this.query, c.fragment ?? this.fragment); }
  toString() { if (this.scheme === 'file') return pathToFileURL(this.fsPath).href; return `${this.scheme}:${this.authority ? '//' + this.authority : ''}${this.path}${this.query ? '?' + this.query : ''}${this.fragment ? '#' + this.fragment : ''}`; }
  toJSON() { return { scheme: this.scheme, path: this.path, fsPath: this.fsPath, external: this.toString() }; }
}
const toUri = (u) => (u instanceof Uri ? u : typeof u === 'string' ? (/^[a-z][\w+.-]*:/i.test(u) && !/^[a-zA-Z]:[\\/]/.test(u) ? Uri.parse(u) : Uri.file(u)) : u && u.path ? Uri.from(u) : Uri.file(String(u)));
const uriKey = (u) => toUri(u).toString();

class Position {
  constructor(line, character) { this.line = Math.max(0, line | 0); this.character = Math.max(0, character | 0); }
  isBefore(o) { return this.line < o.line || (this.line === o.line && this.character < o.character); }
  isBeforeOrEqual(o) { return this.isBefore(o) || this.isEqual(o); }
  isAfter(o) { return !this.isBeforeOrEqual(o); }
  isAfterOrEqual(o) { return !this.isBefore(o); }
  isEqual(o) { return this.line === o.line && this.character === o.character; }
  compareTo(o) { return this.isBefore(o) ? -1 : this.isEqual(o) ? 0 : 1; }
  translate(a, b) { if (typeof a === 'object' && a) return new Position(this.line + (a.lineDelta || 0), this.character + (a.characterDelta || 0)); return new Position(this.line + (a || 0), this.character + (b || 0)); }
  with(a, b) { if (typeof a === 'object' && a) return new Position(a.line ?? this.line, a.character ?? this.character); return new Position(a ?? this.line, b ?? this.character); }
  toJSON() { return { line: this.line, character: this.character }; }
}
class Range {
  constructor(a, b, c, d) {
    if (typeof a === 'number') { a = new Position(a, b); b = new Position(c, d); }
    if (b.isBefore(a)) [a, b] = [b, a];
    this.start = a; this.end = b;
  }
  get isEmpty() { return this.start.isEqual(this.end); }
  get isSingleLine() { return this.start.line === this.end.line; }
  contains(x) { if (x instanceof Range) return this.contains(x.start) && this.contains(x.end); return x.isAfterOrEqual(this.start) && x.isBeforeOrEqual(this.end); }
  isEqual(o) { return this.start.isEqual(o.start) && this.end.isEqual(o.end); }
  intersection(o) { const s = this.start.isAfter(o.start) ? this.start : o.start; const e = this.end.isBefore(o.end) ? this.end : o.end; return s.isAfter(e) ? undefined : new Range(s, e); }
  union(o) { return new Range(this.start.isBefore(o.start) ? this.start : o.start, this.end.isAfter(o.end) ? this.end : o.end); }
  with(a, b) { if (a && !(a instanceof Position)) return new Range(a.start ?? this.start, a.end ?? this.end); return new Range(a ?? this.start, b ?? this.end); }
  toJSON() { return { start: this.start.toJSON(), end: this.end.toJSON() }; }
}
class Selection extends Range {
  constructor(a, b, c, d) { if (typeof a === 'number') { a = new Position(a, b); b = new Position(c, d); } super(a, b); this.anchor = a; this.active = b; }
  get isReversed() { return this.active.isBefore(this.anchor); }
}
class Location { constructor(uri, rangeOrPos) { this.uri = uri; this.range = rangeOrPos instanceof Position ? new Range(rangeOrPos, rangeOrPos) : rangeOrPos; } }
class TextEdit {
  constructor(range, newText) { this.range = range; this.newText = newText; }
  static replace(r, t) { return new TextEdit(r, t); } static insert(p, t) { return new TextEdit(new Range(p, p), t); } static delete(r) { return new TextEdit(r, ''); }
  static setEndOfLine() { return new TextEdit(new Range(0, 0, 0, 0), ''); }
}
class WorkspaceEdit {
  constructor() { this._m = new Map(); this._ops = []; }
  _list(uri) { const k = uriKey(uri); if (!this._m.has(k)) this._m.set(k, { uri: toUri(uri), edits: [] }); return this._m.get(k).edits; }
  replace(uri, range, text) { this._list(uri).push(new TextEdit(range, text)); }
  insert(uri, pos, text) { this._list(uri).push(TextEdit.insert(pos, text)); }
  delete(uri, range) { this._list(uri).push(TextEdit.delete(range)); }
  set(uri, edits) { const l = this._list(uri); l.length = 0; for (const e of edits || []) l.push(Array.isArray(e) ? e[0] : e); }
  get(uri) { return (this._m.get(uriKey(uri)) || { edits: [] }).edits; }
  has(uri) { return this._m.has(uriKey(uri)); }
  entries() { return [...this._m.values()].map((x) => [x.uri, x.edits]); }
  createFile(uri, opts) { this._ops.push({ op: 'create', uri: toUri(uri), opts }); }
  deleteFile(uri, opts) { this._ops.push({ op: 'delete', uri: toUri(uri), opts }); }
  renameFile(a, b, opts) { this._ops.push({ op: 'rename', uri: toUri(a), to: toUri(b), opts }); }
  get size() { return this._m.size; }
}
class SnippetString {
  constructor(v) { this.value = v || ''; }
  appendText(s) { this.value += String(s).replace(/[$}\\]/g, '\\$&'); return this; }
  appendTabstop(n) { this.value += `$${n ?? 0}`; return this; }
  appendPlaceholder(v, n) { this.value += `\${${n ?? 1}:${typeof v === 'string' ? v : ''}}`; return this; }
  appendChoice(vals, n) { this.value += `\${${n ?? 1}|${vals.join(',')}|}`; return this; }
  appendVariable(name, def) { this.value += `\${${name}${def ? ':' + def : ''}}`; return this; }
}
class MarkdownString {
  constructor(v, supportThemeIcons) { this.value = v || ''; this.isTrusted = false; this.supportThemeIcons = supportThemeIcons; this.supportHtml = false; }
  appendText(s) { this.value += String(s).replace(/[\\`*_{}[\]()#+\-.!]/g, '\\$&'); return this; }
  appendMarkdown(s) { this.value += s; return this; }
  appendCodeblock(code, lang) { this.value += `\n\`\`\`${lang || ''}\n${code}\n\`\`\`\n`; return this; }
}
class Hover { constructor(contents, range) { this.contents = Array.isArray(contents) ? contents : [contents]; this.range = range; } }
class CompletionItem { constructor(label, kind) { this.label = label; this.kind = kind; } }
class CompletionList { constructor(items, isIncomplete) { this.items = items || []; this.isIncomplete = Boolean(isIncomplete); } }
class Diagnostic { constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity ?? 0; } }
class DiagnosticRelatedInformation { constructor(location, message) { this.location = location; this.message = message; } }
class ThemeColor { constructor(id) { this.id = id; } }
class ThemeIcon { constructor(id, color) { this.id = id; this.color = color; } }
ThemeIcon.File = new ThemeIcon('file'); ThemeIcon.Folder = new ThemeIcon('folder');
class CodeAction { constructor(title, kind) { this.title = title; this.kind = kind; } }
class CodeActionKind { constructor(v) { this.value = v; } append(p) { return new CodeActionKind(this.value ? `${this.value}.${p}` : p); } contains(o) { return o.value === this.value || o.value.startsWith(this.value + '.'); } intersects(o) { return this.contains(o) || o.contains(this); } }
for (const [k, v] of Object.entries({ Empty: '', QuickFix: 'quickfix', Refactor: 'refactor', RefactorExtract: 'refactor.extract', RefactorInline: 'refactor.inline', RefactorRewrite: 'refactor.rewrite', Source: 'source', SourceOrganizeImports: 'source.organizeImports', SourceFixAll: 'source.fixAll' })) CodeActionKind[k] = new CodeActionKind(v);
class SymbolInformation { constructor(name, kind, a, b) { this.name = name; this.kind = kind; this.location = b instanceof Location ? b : a instanceof Location ? a : new Location(b, a); } }
class DocumentSymbol { constructor(name, detail, kind, range, selectionRange) { Object.assign(this, { name, detail, kind, range, selectionRange, children: [] }); } }
class FoldingRange { constructor(start, end, kind) { Object.assign(this, { start, end, kind }); } }
class SignatureHelp { constructor() { this.signatures = []; this.activeSignature = 0; this.activeParameter = 0; } }
class SignatureInformation { constructor(label, documentation) { this.label = label; this.documentation = documentation; this.parameters = []; } }
class ParameterInformation { constructor(label, documentation) { this.label = label; this.documentation = documentation; } }
class DocumentLink { constructor(range, target) { this.range = range; this.target = target; } }
class Color { constructor(r, g, b, a) { Object.assign(this, { red: r, green: g, blue: b, alpha: a }); } }
class InlayHint { constructor(position, label, kind) { Object.assign(this, { position, label, kind }); } }
class CodeLens { constructor(range, command) { this.range = range; this.command = command; } get isResolved() { return Boolean(this.command); } }
class RelativePattern { constructor(base, pattern) { this.baseUri = typeof base === 'string' ? Uri.file(base) : base.uri || base; this.base = this.baseUri.fsPath; this.pattern = pattern; } }
class TreeItem { constructor(label, state) { this.label = label; this.collapsibleState = state; } }
class FileSystemError extends Error { static FileNotFound(m) { const e = new FileSystemError(String(m || 'File not found')); e.code = 'FileNotFound'; return e; } static FileExists(m) { const e = new FileSystemError(String(m || 'File exists')); e.code = 'FileExists'; return e; } static NoPermissions(m) { const e = new FileSystemError(String(m || 'No permissions')); e.code = 'NoPermissions'; return e; } static Unavailable(m) { const e = new FileSystemError(String(m || 'Unavailable')); e.code = 'Unavailable'; return e; } static FileIsADirectory(m) { return new FileSystemError(String(m || 'Is a directory')); } static FileNotADirectory(m) { return new FileSystemError(String(m || 'Not a directory')); } }
const enums = {
  CompletionItemKind: ['Text', 'Method', 'Function', 'Constructor', 'Field', 'Variable', 'Class', 'Interface', 'Module', 'Property', 'Unit', 'Value', 'Enum', 'Keyword', 'Snippet', 'Color', 'File', 'Reference', 'Folder', 'EnumMember', 'Constant', 'Struct', 'Event', 'Operator', 'TypeParameter', 'User', 'Issue'],
  SymbolKind: ['File', 'Module', 'Namespace', 'Package', 'Class', 'Method', 'Property', 'Field', 'Constructor', 'Enum', 'Interface', 'Function', 'Variable', 'Constant', 'String', 'Number', 'Boolean', 'Array', 'Object', 'Key', 'Null', 'EnumMember', 'Struct', 'Event', 'Operator', 'TypeParameter'],
  DiagnosticSeverity: ['Error', 'Warning', 'Information', 'Hint'],
};
const mkEnum = (names, base = 0) => { const e = {}; names.forEach((n, i) => { e[n] = i + base; e[i + base] = n; }); return e; };
const E = (o) => Object.freeze(o);

// ------------------------------------------------------------------ documents + editor
const docs = new Map();   // uri string -> TextDocument
class TextLine { constructor(n, text, range, rangeNL) { this.lineNumber = n; this.text = text; this.range = range; this.rangeIncludingLineBreak = rangeNL; const m = /^\s*/.exec(text); this.firstNonWhitespaceCharacterIndex = m[0].length; this.isEmptyOrWhitespace = m[0].length === text.length; } }
class TextDocument {
  constructor(uri, languageId, version, text) { this.uri = uri; this.languageId = languageId; this.version = version; this._set(text); this.isDirty = false; this.isClosed = false; this.isUntitled = uri.scheme === 'untitled'; this.eol = 1; this.encoding = 'utf8'; }
  _set(t) { this._text = String(t ?? ''); this._lines = this._text.split('\n'); this._starts = []; let o = 0; for (const l of this._lines) { this._starts.push(o); o += l.length + 1; } }
  get fileName() { return this.uri.fsPath; }
  get lineCount() { return this._lines.length; }
  getText(r) { if (!r) return this._text; return this._text.slice(this.offsetAt(r.start), this.offsetAt(r.end)); }
  lineAt(x) { const n = typeof x === 'number' ? x : x.line; if (n < 0 || n >= this._lines.length) throw new Error('Illegal value for `line`'); const t = this._lines[n].replace(/\r$/, ''); return new TextLine(n, t, new Range(n, 0, n, t.length), n + 1 < this._lines.length ? new Range(n, 0, n + 1, 0) : new Range(n, 0, n, t.length)); }
  offsetAt(p) { const l = Math.min(Math.max(0, p.line), this._lines.length - 1); return this._starts[l] + Math.min(p.character, this._lines[l].length); }
  positionAt(off) { off = Math.max(0, Math.min(this._text.length, off | 0)); let lo = 0, hi = this._starts.length - 1; while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (this._starts[mid] <= off) lo = mid; else hi = mid - 1; } return new Position(lo, off - this._starts[lo]); }
  validatePosition(p) { return this.positionAt(this.offsetAt(p)); }
  validateRange(r) { return new Range(this.validatePosition(r.start), this.validatePosition(r.end)); }
  getWordRangeAtPosition(p, re) {
    const line = this._lines[p.line] || ''; const rx = re ? new RegExp(re.source, re.flags.includes('g') ? re.flags : re.flags + 'g') : /[\w$]+|[^\s\w$]+/g; rx.lastIndex = 0; let m;
    while ((m = rx.exec(line))) { if (m.index <= p.character && m.index + m[0].length >= p.character && m[0].length) return new Range(p.line, m.index, p.line, m.index + m[0].length); if (!m[0].length) rx.lastIndex++; }
    return undefined;
  }
  save() { return request('editor/save', { uri: this.uri.toString() }).then(Boolean); }
}
const ev = { open: new EventEmitter(), change: new EventEmitter(), save: new EventEmitter(), close: new EventEmitter(), willSave: new EventEmitter(),
  active: new EventEmitter(), selection: new EventEmitter(), visible: new EventEmitter(), config: new EventEmitter(), folders: new EventEmitter(), diag: new EventEmitter() };
let activeEditor;
function makeEditor(doc, selections) {
  const sels = (selections && selections.length ? selections : [{ anchor: { line: 0, character: 0 }, active: { line: 0, character: 0 } }]).map((s) => new Selection(new Position(s.anchor.line, s.anchor.character), new Position(s.active.line, s.active.character)));
  const ed = {
    document: doc, selections: sels, options: { tabSize: 4, insertSpaces: true }, viewColumn: 1, visibleRanges: [new Range(0, 0, doc.lineCount, 0)],
    get selection() { return this.selections[0]; }, set selection(s) { this.selections = [s]; request('editor/select', { uri: doc.uri.toString(), selections: [s] }); },
    async edit(cb) {
      const list = []; const b = {
        replace: (loc, t) => list.push({ range: loc instanceof Position ? new Range(loc, loc) : loc, newText: t }),
        insert: (p, t) => list.push({ range: new Range(p, p), newText: t }), delete: (r) => list.push({ range: r, newText: '' }), setEndOfLine() {},
      };
      cb(b);
      return applyEdits([{ uri: doc.uri, edits: list }]);
    },
    async insertSnippet(sn, where) {
      const text = typeof sn === 'string' ? sn : sn.value;
      const ranges = !where ? this.selections : Array.isArray(where) ? where : [where];
      return request('editor/apply', { edits: [{ uri: doc.uri.toString(), edits: ranges.map((r) => ({ range: (r instanceof Position ? new Range(r, r) : r).toJSON(), newText: text })) }], snippet: true }).then(Boolean);
    },
    setDecorations() {}, revealRange() {}, show() {}, hide() {},
  };
  return ed;
}
async function applyEdits(list) {
  const out = list.filter((x) => x.edits.length).map((x) => ({ uri: toUri(x.uri).toString(), edits: x.edits.map((e) => ({ range: (e.range.toJSON ? e.range.toJSON() : e.range), newText: e.newText ?? '' })) }));
  if (!out.length) return true;
  return Boolean(await request('editor/apply', { edits: out }));
}

// ------------------------------------------------------------------ configuration + state
let settings = {};   // flat "section.key" -> value (user settings from the browser)
const defaults = {};
function readDefaults(pkg) {
  let conf = pkg.contributes && pkg.contributes.configuration; if (!conf) return;
  if (!Array.isArray(conf)) conf = [conf];
  for (const c of conf) for (const [k, v] of Object.entries(c.properties || {})) if (v && 'default' in v) defaults[k] = v.default;
}
function getConfiguration(section) {
  const full = (k) => (section ? `${section}.${k}` : k);
  const lookup = (k) => {
    const f = full(k);
    if (f in settings) return settings[f];
    if (f in defaults) return defaults[f];
    // nested object: collect "f.*"
    const pre = f + '.'; let obj; for (const src of [defaults, settings]) for (const [kk, vv] of Object.entries(src)) if (kk.startsWith(pre)) { obj = obj || {}; setDeep(obj, kk.slice(pre.length).split('.'), vv); }
    return obj;
  };
  const cfg = {
    get: (k, d) => { const v = lookup(k); return v === undefined ? d : v; },
    has: (k) => lookup(k) !== undefined,
    inspect: (k) => ({ key: full(k), defaultValue: defaults[full(k)], globalValue: settings[full(k)] }),
    update: async (k, v) => { settings[full(k)] = v; notify('config/update', { key: full(k), value: v }); ev.config.fire({ affectsConfiguration: (s) => full(k).startsWith(s) }); },
  };
  return new Proxy(cfg, { get(t, p) { if (p in t) return t[p]; if (typeof p === 'string') return lookup(p); return undefined; } });
}
function setDeep(o, keys, v) { let c = o; keys.slice(0, -1).forEach((k) => { c[k] = c[k] && typeof c[k] === 'object' ? c[k] : {}; c = c[k]; }); c[keys[keys.length - 1]] = v; }
function memento(file) {
  let data = {}; try { data = JSON.parse(fs.readFileSync(file, 'utf8')); } catch (_e) { /* new */ }
  let t = 0; const save = () => { clearTimeout(t); t = setTimeout(() => { try { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, JSON.stringify(data)); } catch (_e) { /* read-only */ } }, 200); };
  return { get: (k, d) => (k in data ? data[k] : d), update: async (k, v) => { if (v === undefined) delete data[k]; else data[k] = v; save(); }, keys: () => Object.keys(data), setKeysForSync() {} };
}

// ------------------------------------------------------------------ commands, providers, diagnostics, UI
const commands = new Map();   // id -> { fn, thisArg, ext }
const providers = { completion: [], hover: [], definition: [], formatting: [], rangeFormatting: [], codeActions: [], symbols: [], signature: [] };
const diagCollections = new Set();
const warned = new Set();
const warnOnce = (what) => { if (warned.has(what)) return; warned.add(what); console.warn(`[A.S.T.R.O extension host] ${what} is not supported yet (ignored).`); notify('unsupported', { api: what }); };
const stub = (name) => { const f = function () { warnOnce(name); return new Disposable(() => {}); }; return f; };
const ns = (obj, name) => new Proxy(obj, { get(t, p) { if (p in t) return t[p]; if (typeof p === 'symbol' || p === 'then' || p === 'toJSON') return undefined; return stub(`${name}.${String(p)}`); } });
function globToRe(g) {
  let re = ''; let i = 0; g = String(g);
  while (i < g.length) {
    const c = g[i];
    if (c === '*') { if (g[i + 1] === '*') { re += '.*'; i += 2; if (g[i] === '/') i++; continue; } re += '[^/]*'; }
    else if (c === '?') re += '[^/]';
    else if (c === '{') { const j = g.indexOf('}', i); if (j > i) { re += '(?:' + g.slice(i + 1, j).split(',').map((x) => x.replace(/[.+^$()|[\]\\]/g, '\\$&').replace(/\*/g, '[^/]*')).join('|') + ')'; i = j + 1; continue; } re += '\\{'; }
    else re += c.replace(/[.+^$()|[\]\\]/g, '\\$&');
    i++;
  }
  return new RegExp('^' + re + '$');
}
function matchSelector(sel, doc) {
  if (!sel) return 0;
  if (Array.isArray(sel)) return Math.max(0, ...sel.map((s) => matchSelector(s, doc)));
  if (typeof sel === 'string') return sel === '*' ? 5 : sel === doc.languageId ? 10 : 0;
  if (sel.language && sel.language !== '*' && sel.language !== doc.languageId) return 0;
  if (sel.scheme && sel.scheme !== '*' && sel.scheme !== doc.uri.scheme) return 0;
  if (sel.pattern) { const pat = typeof sel.pattern === 'string' ? sel.pattern : sel.pattern.pattern; const rel = path.relative(ROOT, doc.uri.fsPath).replace(/\\/g, '/'); if (!globToRe(pat).test(rel) && !globToRe(pat).test(doc.uri.path)) return 0; }
  return sel.language ? 10 : 5;
}
const register = (kind, selector, provider, extra) => { const e = { selector, provider, extra }; providers[kind].push(e); return new Disposable(() => { const i = providers[kind].indexOf(e); if (i >= 0) providers[kind].splice(i, 1); }); };
const serRange = (r) => (r ? (r.toJSON ? r.toJSON() : r.start ? { start: r.start, end: r.end } : r) : null);
function serDiag(d) { return { range: serRange(d.range), message: typeof d.message === 'string' ? d.message : d.message && d.message.value, severity: d.severity ?? 0, source: d.source, code: typeof d.code === 'object' && d.code ? d.code.value : d.code }; }
function createDiagnosticCollection(name) {
  const map = new Map(); const owner = name || `ext${diagCollections.size}`;
  const push = (u) => notify('diagnostics', { owner, uri: u, items: (map.get(u) || []).map(serDiag) });
  const c = {
    name: owner,
    set(uri, list) { if (Array.isArray(uri)) { for (const [u, l] of uri) c.set(u, l); return; } const k = uriKey(uri); if (!list || !list.length) map.delete(k); else map.set(k, [...list]); push(k); ev.diag.fire({ uris: [toUri(uri)] }); },
    delete(uri) { const k = uriKey(uri); map.delete(k); push(k); },
    clear() { const ks = [...map.keys()]; map.clear(); ks.forEach(push); },
    get: (uri) => map.get(uriKey(uri)) || [], has: (uri) => map.has(uriKey(uri)),
    forEach(cb) { for (const [k, v] of map) cb(Uri.parse(k), v, c); },
    dispose() { c.clear(); diagCollections.delete(c); },
    [Symbol.iterator]: function* () { for (const [k, v] of map) yield [Uri.parse(k), v]; },
  };
  diagCollections.add(c); return c;
}
const outputs = new Map();
function createOutputChannel(name, opts) {
  const ch = {
    name, append: (t) => notify('output', { channel: name, text: String(t) }), appendLine: (t) => notify('output', { channel: name, text: String(t) + '\n' }),
    replace: (t) => { notify('output', { channel: name, clear: true }); notify('output', { channel: name, text: String(t) }); },
    clear: () => notify('output', { channel: name, clear: true }), show: () => notify('output', { channel: name, show: true }), hide() {}, dispose() { outputs.delete(name); },
    logLevel: 2, onDidChangeLogLevel: new EventEmitter().event,
  };
  if (opts && (opts === true || opts.log)) for (const [lvl, tag] of [['trace', 'trace'], ['debug', 'debug'], ['info', 'info'], ['warn', 'warning'], ['error', 'error']]) ch[lvl] = (m, ...a) => ch.appendLine(`${new Date().toISOString()} [${tag}] ${m instanceof Error ? m.message : m}${a.length ? ' ' + a.map(safeJson).join(' ') : ''}`);
  outputs.set(name, ch); return ch;
}
let statusSeq = 0;
function createStatusBarItem(a, b, c) {
  let id = `sb${++statusSeq}`; let alignment = 1; let priority = 0;
  if (typeof a === 'string') { id = a; alignment = b ?? 1; priority = c ?? 0; } else { alignment = a ?? 1; priority = b ?? 0; }
  const st = { text: '', tooltip: '', command: undefined, visible: false, name: '' };
  const push = () => notify('status', { id, alignment, priority, text: st.text, tooltip: typeof st.tooltip === 'string' ? st.tooltip : st.tooltip && st.tooltip.value, command: typeof st.command === 'string' ? st.command : st.command && st.command.command, args: st.command && st.command.arguments, visible: st.visible });
  const item = { id, alignment, priority, show() { st.visible = true; push(); }, hide() { st.visible = false; push(); }, dispose() { st.visible = false; push(); } };
  for (const k of ['text', 'tooltip', 'command', 'name', 'color', 'backgroundColor', 'accessibilityInformation']) Object.defineProperty(item, k, { get: () => st[k], set: (v) => { st[k] = v; if (st.visible) push(); } });
  return item;
}
async function showMessage(severity, message, ...rest) {
  let modal = false; let items = rest;
  if (rest[0] && typeof rest[0] === 'object' && !('title' in rest[0]) && !Array.isArray(rest[0])) { modal = Boolean(rest[0].modal); items = rest.slice(1); }
  if (Array.isArray(items[0])) items = items[0];
  const labels = items.map((x) => (typeof x === 'string' ? x : x.title));
  const i = await request('ui/message', { severity, text: String(message), items: labels, modal });
  return typeof i === 'number' && i >= 0 ? items[i] : undefined;
}
async function showQuickPick(items, opts = {}) {
  const list = await Promise.resolve(items);
  const norm = (list || []).map((x) => (typeof x === 'string' ? { label: x } : { label: x.label, description: x.description, detail: x.detail, picked: x.picked, kind: x.kind }));
  const r = await request('ui/quickPick', { items: norm, placeHolder: opts.placeHolder || opts.title, canPickMany: Boolean(opts.canPickMany) });
  if (opts.canPickMany) return Array.isArray(r) ? r.map((i) => list[i]) : undefined;
  return typeof r === 'number' && r >= 0 ? list[r] : undefined;
}
async function showInputBox(opts = {}) {
  for (;;) {
    const v = await request('ui/input', { prompt: opts.prompt || opts.title, value: opts.value, placeHolder: opts.placeHolder, password: Boolean(opts.password) });
    if (v == null) return undefined;
    if (opts.validateInput) { const err = await opts.validateInput(v); if (err) { await request('ui/message', { severity: 'error', text: typeof err === 'string' ? err : err.message, items: [] }); opts = { ...opts, value: v }; continue; } }
    return v;
  }
}
async function withProgress(opts, task) {
  const id = `p${++statusSeq}`; notify('progress', { id, title: opts.title || '', state: 'start' });
  try { return await task({ report: (v) => notify('progress', { id, title: opts.title || '', message: v && v.message, increment: v && v.increment, state: 'report' }) }, noToken); }
  finally { notify('progress', { id, state: 'end' }); }
}
function walk(dir, out, max, depth = 0) {
  if (out.length >= max || depth > 12) return;
  let ents = []; try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch (_e) { return; }
  for (const e of ents) {
    if (out.length >= max) return;
    if (e.name === 'node_modules' || e.name === '.git' || e.name.startsWith('.astro')) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out, max, depth + 1); else out.push(p);
  }
}
async function findFiles(include, exclude, max = 5000) {
  const inc = include instanceof RelativePattern ? include.pattern : String(include || '**/*');
  const base = include instanceof RelativePattern ? include.base : ROOT;
  const exc = exclude ? globToRe(exclude instanceof RelativePattern ? exclude.pattern : String(exclude)) : null;
  const re = globToRe(inc); const all = []; walk(base, all, 20000);
  return all.map((p) => path.relative(base, p).replace(/\\/g, '/')).filter((r) => re.test(r) && !(exc && exc.test(r))).slice(0, max).map((r) => Uri.file(path.join(base, r)));
}
const fsApi = {
  stat: async (u) => { const s = fs.statSync(toUri(u).fsPath); return { type: s.isDirectory() ? 2 : 1, ctime: s.ctimeMs, mtime: s.mtimeMs, size: s.size }; },
  readFile: async (u) => { try { return new Uint8Array(fs.readFileSync(toUri(u).fsPath)); } catch (e) { throw FileSystemError.FileNotFound(u); } },
  writeFile: async (u, data) => { const p = toUri(u).fsPath; fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, Buffer.from(data)); notify('fs/changed', { uri: toUri(u).toString() }); },
  readDirectory: async (u) => fs.readdirSync(toUri(u).fsPath, { withFileTypes: true }).map((e) => [e.name, e.isDirectory() ? 2 : 1]),
  createDirectory: async (u) => { fs.mkdirSync(toUri(u).fsPath, { recursive: true }); },
  delete: async (u, o) => { fs.rmSync(toUri(u).fsPath, { recursive: Boolean(o && o.recursive), force: true }); notify('fs/changed', { uri: toUri(u).toString() }); },
  rename: async (a, b) => { fs.renameSync(toUri(a).fsPath, toUri(b).fsPath); notify('fs/changed', { uri: toUri(b).toString() }); },
  copy: async (a, b) => { fs.cpSync(toUri(a).fsPath, toUri(b).fsPath, { recursive: true }); },
  isWritableFileSystem: () => true,
};
async function openTextDocument(x) {
  if (x && typeof x === 'object' && !(x instanceof Uri) && !x.scheme && ('content' in x || 'language' in x)) {
    const u = new Uri('untitled', '', `/Untitled-${++statusSeq}`); const d = new TextDocument(u, x.language || 'plaintext', 1, x.content || ''); docs.set(u.toString(), d); return d;
  }
  const u = toUri(x); const k = u.toString();
  if (docs.has(k)) return docs.get(k);
  const text = fs.readFileSync(u.fsPath, 'utf8');
  const d = new TextDocument(u, langOfPath(u.fsPath), 1, text); docs.set(k, d); return d;
}
const LANG_BY_EXT = { js: 'javascript', mjs: 'javascript', cjs: 'javascript', jsx: 'javascriptreact', ts: 'typescript', tsx: 'typescriptreact', py: 'python', c: 'c', h: 'c', cpp: 'cpp', cc: 'cpp', hpp: 'cpp', rs: 'rust', go: 'go', java: 'java', json: 'json', md: 'markdown', html: 'html', css: 'css', scss: 'scss', yaml: 'yaml', yml: 'yaml', sh: 'shellscript', php: 'php', rb: 'ruby', lua: 'lua', txt: 'plaintext', xml: 'xml', sql: 'sql', cs: 'csharp', kt: 'kotlin', swift: 'swift', toml: 'toml', vue: 'vue', svelte: 'svelte' };
const langOfPath = (p) => LANG_BY_EXT[(/\.([^./\\]+)$/.exec(p) || [])[1]?.toLowerCase()] || 'plaintext';

// ------------------------------------------------------------------ the vscode module
const extensionsById = new Map();
const vscode = {
  version: VERSION,
  Disposable, EventEmitter, CancellationTokenSource, Uri, Position, Range, Selection, Location, TextEdit, WorkspaceEdit, SnippetString, MarkdownString, Hover,
  CompletionItem, CompletionList, Diagnostic, DiagnosticRelatedInformation, ThemeColor, ThemeIcon, CodeAction, CodeActionKind, SymbolInformation, DocumentSymbol, FoldingRange,
  SignatureHelp, SignatureInformation, ParameterInformation, DocumentLink, Color, InlayHint, CodeLens, RelativePattern, TreeItem, FileSystemError,
  CancellationError: class CancellationError extends Error { constructor() { super('Canceled'); } },
  CompletionItemKind: mkEnum(enums.CompletionItemKind), SymbolKind: mkEnum(enums.SymbolKind), DiagnosticSeverity: mkEnum(enums.DiagnosticSeverity),
  CompletionTriggerKind: E({ Invoke: 0, TriggerCharacter: 1, TriggerForIncompleteCompletions: 2 }), CompletionItemTag: E({ Deprecated: 1 }), DiagnosticTag: E({ Unnecessary: 1, Deprecated: 2 }),
  StatusBarAlignment: E({ Left: 1, Right: 2 }), ViewColumn: E({ Active: -1, Beside: -2, One: 1, Two: 2, Three: 3 }), ConfigurationTarget: E({ Global: 1, Workspace: 2, WorkspaceFolder: 3 }),
  ProgressLocation: E({ SourceControl: 1, Window: 10, Notification: 15 }), EndOfLine: E({ LF: 1, CRLF: 2 }), TextEditorRevealType: E({ Default: 0, InCenter: 1, InCenterIfOutsideViewport: 2, AtTop: 3 }),
  FileType: E({ Unknown: 0, File: 1, Directory: 2, SymbolicLink: 64 }), ExtensionMode: E({ Production: 1, Development: 2, Test: 3 }), ExtensionKind: E({ UI: 1, Workspace: 2 }),
  TreeItemCollapsibleState: E({ None: 0, Collapsed: 1, Expanded: 2 }), OverviewRulerLane: E({ Left: 1, Center: 2, Right: 4, Full: 7 }), DecorationRangeBehavior: E({ OpenOpen: 0, ClosedClosed: 1, OpenClosed: 2, ClosedOpen: 3 }),
  TextDocumentSaveReason: E({ Manual: 1, AfterDelay: 2, FocusOut: 3 }), QuickPickItemKind: E({ Separator: -1, Default: 0 }), UIKind: E({ Desktop: 1, Web: 2 }), LogLevel: E({ Off: 0, Trace: 1, Debug: 2, Info: 3, Warning: 4, Error: 5 }),
  ColorThemeKind: E({ Light: 1, Dark: 2, HighContrast: 3, HighContrastLight: 4 }), SignatureHelpTriggerKind: E({ Invoke: 1, TriggerCharacter: 2, ContentChange: 3 }), InlayHintKind: E({ Type: 1, Parameter: 2 }),
  FoldingRangeKind: E({ Comment: 1, Imports: 2, Region: 3 }), IndentAction: E({ None: 0, Indent: 1, IndentOutdent: 2, Outdent: 3 }), TextEditorCursorStyle: E({ Line: 1, Block: 2, Underline: 3 }),
  commands: ns({
    registerCommand(id, fn, thisArg) { commands.set(id, { fn, thisArg }); notify('commands/changed', {}); return new Disposable(() => commands.delete(id)); },
    registerTextEditorCommand(id, fn, thisArg) { commands.set(id, { fn: (...a) => activeEditor && fn.call(thisArg, activeEditor, { replace() {}, insert() {}, delete() {} }, ...a) }); return new Disposable(() => commands.delete(id)); },
    executeCommand: (id, ...a) => executeCommand(id, a),
    getCommands: async () => [...commands.keys()],
  }, 'commands'),
  window: ns({
    get activeTextEditor() { return activeEditor; }, get visibleTextEditors() { return activeEditor ? [activeEditor] : []; },
    onDidChangeActiveTextEditor: ev.active.event, onDidChangeTextEditorSelection: ev.selection.event, onDidChangeVisibleTextEditors: ev.visible.event,
    onDidChangeTextEditorVisibleRanges: new EventEmitter().event, onDidChangeTextEditorOptions: new EventEmitter().event, onDidChangeWindowState: new EventEmitter().event, onDidChangeActiveColorTheme: new EventEmitter().event,
    state: { focused: true, active: true }, activeColorTheme: { kind: 2 }, tabGroups: { all: [], activeTabGroup: { tabs: [] }, onDidChangeTabs: new EventEmitter().event, onDidChangeTabGroups: new EventEmitter().event },
    showInformationMessage: (m, ...r) => showMessage('info', m, ...r), showWarningMessage: (m, ...r) => showMessage('warning', m, ...r), showErrorMessage: (m, ...r) => showMessage('error', m, ...r),
    showQuickPick, showInputBox, createOutputChannel, createStatusBarItem, withProgress,
    setStatusBarMessage: (t, ms) => { notify('status', { id: '_msg', text: t, visible: true }); const d = new Disposable(() => notify('status', { id: '_msg', visible: false })); if (typeof ms === 'number') setTimeout(() => d.dispose(), ms); return d; },
    async showTextDocument(x, opts) { const d = x && x.getText ? x : await openTextDocument(x); const sel = opts && opts.selection; await request('window/open', { uri: d.uri.toString(), selection: sel ? serRange(sel) : null }); return activeEditor && activeEditor.document.uri.toString() === d.uri.toString() ? activeEditor : makeEditor(d); },
    createTextEditorDecorationType: () => ({ key: `deco${++statusSeq}`, dispose() {} }),
    showOpenDialog: async () => { warnOnce('window.showOpenDialog'); return undefined; }, showSaveDialog: async () => { warnOnce('window.showSaveDialog'); return undefined; },
    showWorkspaceFolderPick: async () => vscode.workspace.workspaceFolders[0],
  }, 'window'),
  workspace: ns({
    get workspaceFolders() { return [{ uri: Uri.file(ROOT), name: path.basename(ROOT), index: 0 }]; }, get rootPath() { return ROOT; }, get name() { return path.basename(ROOT); }, workspaceFile: undefined,
    getWorkspaceFolder: (u) => (toUri(u).fsPath.startsWith(ROOT) ? vscode.workspace.workspaceFolders[0] : undefined),
    asRelativePath: (p) => { const f = typeof p === 'string' ? p : toUri(p).fsPath; return path.relative(ROOT, f).replace(/\\/g, '/') || f; },
    getConfiguration, onDidChangeConfiguration: ev.config.event, onDidChangeWorkspaceFolders: ev.folders.event,
    get textDocuments() { return [...docs.values()]; }, openTextDocument, findFiles, fs: fsApi,
    onDidOpenTextDocument: ev.open.event, onDidChangeTextDocument: ev.change.event, onDidSaveTextDocument: ev.save.event, onDidCloseTextDocument: ev.close.event, onWillSaveTextDocument: ev.willSave.event,
    onDidCreateFiles: new EventEmitter().event, onDidDeleteFiles: new EventEmitter().event, onDidRenameFiles: new EventEmitter().event, onWillCreateFiles: new EventEmitter().event, onWillDeleteFiles: new EventEmitter().event, onWillRenameFiles: new EventEmitter().event,
    onDidGrantWorkspaceTrust: new EventEmitter().event, isTrusted: true,
    async applyEdit(we) {
      for (const op of we._ops || []) { try { if (op.op === 'create') await fsApi.writeFile(op.uri, new Uint8Array()); else if (op.op === 'delete') await fsApi.delete(op.uri, { recursive: true }); else if (op.op === 'rename') await fsApi.rename(op.uri, op.to); } catch (e) { console.error(e); } }
      return applyEdits(we.entries().map(([uri, edits]) => ({ uri, edits })));
    },
    createFileSystemWatcher: () => ({ onDidCreate: new EventEmitter().event, onDidChange: new EventEmitter().event, onDidDelete: new EventEmitter().event, dispose() {}, ignoreChangeEvents: false, ignoreCreateEvents: false, ignoreDeleteEvents: false }),
    registerTextDocumentContentProvider: () => new Disposable(() => {}),
    saveAll: async () => Boolean(await request('editor/saveAll', {})),
  }, 'workspace'),
  languages: ns({
    registerCompletionItemProvider: (sel, p, ...trig) => register('completion', sel, p, { triggers: trig }),
    registerHoverProvider: (sel, p) => register('hover', sel, p), registerDefinitionProvider: (sel, p) => register('definition', sel, p),
    registerDocumentFormattingEditProvider: (sel, p) => register('formatting', sel, p), registerDocumentRangeFormattingEditProvider: (sel, p) => register('rangeFormatting', sel, p),
    registerCodeActionsProvider: (sel, p) => register('codeActions', sel, p), registerDocumentSymbolProvider: (sel, p) => register('symbols', sel, p),
    registerSignatureHelpProvider: (sel, p) => register('signature', sel, p),
    createDiagnosticCollection, getDiagnostics: (u) => { const out = []; for (const c of diagCollections) if (u) out.push(...c.get(u)); return out; }, onDidChangeDiagnostics: ev.diag.event,
    getLanguages: async () => [...new Set(Object.values(LANG_BY_EXT))], match: (sel, doc) => matchSelector(sel, doc),
    setTextDocumentLanguage: async (doc, id) => { doc.languageId = id; return doc; }, setLanguageConfiguration: () => new Disposable(() => {}),
  }, 'languages'),
  env: ns({
    appName: 'A.S.T.R.O AI', appRoot: path.dirname(__dirname), appHost: 'desktop', uriScheme: 'astro', language: 'en', machineId: 'astro-' + require('os').hostname().length, sessionId: String(Date.now()), shell: process.env.SHELL || '', uiKind: 1, remoteName: undefined, isNewAppInstall: false, isTelemetryEnabled: false,
    onDidChangeTelemetryEnabled: new EventEmitter().event, logLevel: 3, onDidChangeLogLevel: new EventEmitter().event,
    clipboard: { readText: async () => (await request('clipboard/read', {})) || '', writeText: async (t) => { await request('clipboard/write', { text: String(t) }); } },
    openExternal: async (u) => Boolean(await request('env/openExternal', { uri: toUri(u).toString() })), asExternalUri: async (u) => u,
  }, 'env'),
  extensions: ns({
    getExtension: (id) => { const e = extensionsById.get(String(id).toLowerCase()); return e ? e.api : undefined; },
    get all() { return [...extensionsById.values()].map((e) => e.api); }, onDidChange: new EventEmitter().event,
  }, 'extensions'),
  authentication: ns({}, 'authentication'), debug: ns({ activeDebugSession: undefined, breakpoints: [], onDidStartDebugSession: new EventEmitter().event, onDidTerminateDebugSession: new EventEmitter().event }, 'debug'),
  tasks: ns({ taskExecutions: [], onDidStartTask: new EventEmitter().event, onDidEndTask: new EventEmitter().event }, 'tasks'), scm: ns({}, 'scm'), comments: ns({}, 'comments'), notebooks: ns({}, 'notebooks'), tests: ns({}, 'tests'), l10n: ns({ t: (m, ...a) => { const s = typeof m === 'string' ? m : m.message; const args = Array.isArray(a[0]) ? a[0] : typeof a[0] === 'object' && a[0] ? a[0] : a; return s.replace(/\{(\w+)\}/g, (x, k) => (args[k] !== undefined ? String(args[k]) : x)); }, bundle: undefined, uri: undefined }, 'l10n'),
  chat: ns({}, 'chat'), lm: ns({}, 'lm'),
};
vscode.window.createWebviewPanel = stub('window.createWebviewPanel'); vscode.window.registerWebviewViewProvider = stub('window.registerWebviewViewProvider');
vscode.window.createTreeView = stub('window.createTreeView'); vscode.window.registerTreeDataProvider = stub('window.registerTreeDataProvider'); vscode.window.createTerminal = stub('window.createTerminal');
const vscodeProxy = ns(vscode, 'vscode');

const origLoad = Module._load;
Module._load = function (request, parent, isMain) { if (request === 'vscode') return vscodeProxy; return origLoad.apply(this, arguments); };

// ------------------------------------------------------------------ extensions + activation
async function executeCommand(id, args) {
  if (!commands.has(id)) await activateBy(`onCommand:${id}`, id);
  const c = commands.get(id);
  if (c) return c.fn.apply(c.thisArg, args || []);
  // a few built-ins extensions commonly call
  if (id === 'setContext') return undefined;
  if (id === 'vscode.open') return vscode.window.showTextDocument(args[0]);
  if (id === 'workbench.action.files.save' || id === 'workbench.action.files.saveAll') return request('editor/saveAll', {});
  if (id === 'editor.action.formatDocument') return request('editor/format', {});
  if (id === 'vscode.executeFormatDocumentProvider') return provideFormatting({ uri: uriKey(args[0]) }).then((x) => x);
  if (id.startsWith('workbench.') || id.startsWith('editor.') || id.startsWith('vscode.')) { warnOnce(`command ${id}`); return undefined; }
  throw new Error(`command '${id}' not found`);
}
function loadExtensions() {
  let dirs = []; try { dirs = fs.readdirSync(EXT_DIR, { withFileTypes: true }).filter((d) => d.isDirectory()).map((d) => path.join(EXT_DIR, d.name)); } catch (_e) { /* none */ }
  let enabled = null; try { const m = JSON.parse(fs.readFileSync(path.join(EXT_DIR, 'installed.json'), 'utf8')); enabled = new Set(Object.entries(m).filter(([, v]) => v.enabled !== false).map(([, v]) => path.basename(v.dir))); } catch (_e) { /* all */ }
  for (const dir of dirs) {
    if (enabled && !enabled.has(path.basename(dir))) continue;
    let pkg; try { pkg = JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8')); } catch (_e) { continue; }
    let nls = {}; try { nls = JSON.parse(fs.readFileSync(path.join(dir, 'package.nls.json'), 'utf8')); } catch (_e) { /* none */ }
    const loc = (s) => (typeof s === 'string' ? s.replace(/^%(.+)%$/, (m, k) => (nls[k] !== undefined ? (typeof nls[k] === 'string' ? nls[k] : nls[k].message) : m)) : s);
    const id = `${pkg.publisher}.${pkg.name}`.toLowerCase();
    readDefaults(pkg);
    const contributed = ((pkg.contributes && pkg.contributes.commands) || []).map((c) => ({ command: c.command, title: loc(c.title), category: loc(c.category) || '' }));
    const ext = { id, dir, pkg, name: loc(pkg.displayName) || pkg.name, main: pkg.main, activated: false, error: '', exports: undefined, contributed, events: pkg.activationEvents || [], subscriptions: [] };
    ext.api = { id: `${pkg.publisher}.${pkg.name}`, extensionPath: dir, extensionUri: Uri.file(dir), packageJSON: pkg, extensionKind: 2, get isActive() { return ext.activated; }, get exports() { return ext.exports; }, activate: () => activate(ext).then(() => ext.exports) };
    extensionsById.set(id, ext);
  }
}
const activating = new Map();
function activate(ext) {
  if (ext.activated || !ext.main) return Promise.resolve();
  if (activating.has(ext.id)) return activating.get(ext.id);
  const p = (async () => {
    const storageDir = path.join(STORAGE, ext.id);
    const ctx = {
      subscriptions: ext.subscriptions, extensionPath: ext.dir, extensionUri: Uri.file(ext.dir), extension: ext.api, extensionMode: 1,
      globalState: memento(path.join(storageDir, 'global.json')), workspaceState: memento(path.join(storageDir, 'ws-' + Buffer.from(ROOT).toString('base64url').slice(0, 40) + '.json')),
      secrets: (() => { const m = memento(path.join(storageDir, 'secrets.json')); const e = new EventEmitter(); return { get: async (k) => m.get(k), store: async (k, v) => { await m.update(k, v); e.fire({ key: k }); }, delete: async (k) => { await m.update(k, undefined); e.fire({ key: k }); }, onDidChange: e.event }; })(),
      storagePath: path.join(storageDir, 'ws'), globalStoragePath: path.join(storageDir, 'global'), logPath: path.join(storageDir, 'logs'),
      storageUri: Uri.file(path.join(storageDir, 'ws')), globalStorageUri: Uri.file(path.join(storageDir, 'global')), logUri: Uri.file(path.join(storageDir, 'logs')),
      asAbsolutePath: (rel) => path.join(ext.dir, rel), environmentVariableCollection: { replace() {}, append() {}, prepend() {}, get() {}, forEach() {}, delete() {}, clear() {}, persistent: false, description: '' },
      languageModelAccessInformation: { canSendRequest: () => undefined, onDidChange: new EventEmitter().event },
    };
    try { fs.mkdirSync(path.join(storageDir, 'global'), { recursive: true }); } catch (_e) { /* read-only */ }
    try {
      const mainPath = path.resolve(ext.dir, ext.main);
      const mod = require(mainPath);
      if (typeof mod.activate === 'function') ext.exports = await mod.activate(ctx);
      ext.module = mod; ext.activated = true;
    } catch (e) { ext.error = String((e && e.stack) || e).split('\n').slice(0, 4).join('\n'); console.error(`Activating ${ext.id} failed:`, e); }
    notify('ext', { id: ext.id, activated: ext.activated, error: ext.error });
    notify('commands/changed', {});
  })();
  activating.set(ext.id, p);
  return p;
}
async function activateBy(event, commandId) {
  const tasks = [];
  for (const ext of extensionsById.values()) {
    if (ext.activated) continue;
    const evs = ext.events;
    const implicitCmd = commandId && ext.contributed.some((c) => c.command === commandId);
    const lang = event.startsWith('onLanguage:') ? event.slice(11) : '';
    const implicitLang = lang && ((ext.pkg.contributes && ext.pkg.contributes.languages) || []).some((l) => l.id === lang);
    if (evs.includes(event) || implicitCmd || implicitLang || (lang && evs.includes('onLanguage'))) tasks.push(activate(ext));
  }
  await Promise.all(tasks);
}
async function startup() {
  const tasks = [];
  let files = null;
  for (const ext of extensionsById.values()) {
    const evs = ext.events;
    if (evs.includes('*') || evs.includes('onStartupFinished')) { tasks.push(activate(ext)); continue; }
    const wc = evs.filter((e) => e.startsWith('workspaceContains:')).map((e) => e.slice(18));
    if (wc.length) {
      if (!files) { files = []; walk(ROOT, files, 5000); files = files.map((p) => path.relative(ROOT, p).replace(/\\/g, '/')); }
      if (wc.some((g) => { const re = globToRe(g); return files.some((f) => re.test(f)); })) tasks.push(activate(ext));
    }
  }
  await Promise.all(tasks);
}

// ------------------------------------------------------------------ language feature requests from the editor
const docOf = (uri) => docs.get(uri);
const kindOut = (k) => (typeof k === 'number' ? k + 1 : undefined);   // vscode enum is 0-based, LSP 1-based
const mdOut = (c) => (c == null ? '' : typeof c === 'string' ? c : c.value !== undefined ? (c.language ? '```' + c.language + '\n' + c.value + '\n```' : c.value) : '');
async function provideCompletion(q) {
  const doc = docOf(q.uri); if (!doc) return { items: [] };
  await activateBy(`onLanguage:${doc.languageId}`);
  const pos = new Position(q.line, q.character); const items = [];
  for (const p of providers.completion) {
    if (!matchSelector(p.selector, doc)) continue;
    if (q.trigger && !(p.extra.triggers || []).includes(q.trigger) && !q.explicit) continue;
    try {
      let r = await p.provider.provideCompletionItems(doc, pos, noToken, { triggerKind: q.trigger ? 1 : 0, triggerCharacter: q.trigger });
      if (!r) continue; const list = Array.isArray(r) ? r : r.items || [];
      for (const it of list.slice(0, 500)) {
        const label = typeof it.label === 'string' ? it.label : it.label && it.label.label;
        const ins = it.insertText instanceof SnippetString ? it.insertText.value : typeof it.insertText === 'string' ? it.insertText : it.textEdit ? it.textEdit.newText : label;
        items.push({ label, kind: kindOut(it.kind), detail: it.detail || (typeof it.label === 'object' && it.label.description) || '', documentation: mdOut(it.documentation), insertText: ins, isSnippet: it.insertText instanceof SnippetString, filterText: it.filterText, sortText: it.sortText });
      }
    } catch (e) { console.error(e); }
  }
  return { items };
}
async function provideHover(q) {
  const doc = docOf(q.uri); if (!doc) return null;
  await activateBy(`onLanguage:${doc.languageId}`);
  const parts = [];
  for (const p of providers.hover) { if (!matchSelector(p.selector, doc)) continue; try { const h = await p.provider.provideHover(doc, new Position(q.line, q.character), noToken); if (h) for (const c of h.contents) parts.push(mdOut(c)); } catch (e) { console.error(e); } }
  return parts.filter(Boolean).join('\n\n---\n\n') || null;
}
async function provideDefinition(q) {
  const doc = docOf(q.uri); if (!doc) return [];
  const out = [];
  for (const p of providers.definition) { if (!matchSelector(p.selector, doc)) continue; try { let r = await p.provider.provideDefinition(doc, new Position(q.line, q.character), noToken); if (!r) continue; if (!Array.isArray(r)) r = [r]; for (const l of r) out.push({ uri: toUri(l.targetUri || l.uri).toString(), range: serRange(l.targetSelectionRange || l.targetRange || l.range) }); } catch (e) { console.error(e); } }
  return out;
}
async function provideFormatting(q) {
  const doc = docOf(q.uri); if (!doc) return null;
  await activateBy(`onLanguage:${doc.languageId}`);
  for (const p of providers.formatting) { if (!matchSelector(p.selector, doc)) continue; try { const r = await p.provider.provideDocumentFormattingEdits(doc, { tabSize: q.tabSize || 2, insertSpaces: q.insertSpaces !== false }, noToken); if (r) return r.map((e) => ({ range: serRange(e.range), newText: e.newText })); } catch (e) { console.error(e); } }
  return null;
}
function capabilities() {
  const langs = (kind) => { const s = new Set(); for (const p of providers[kind]) { const add = (x) => { if (typeof x === 'string') s.add(x); else if (Array.isArray(x)) x.forEach(add); else if (x && x.language) s.add(x.language); else if (x) s.add('*'); }; add(p.selector); } return [...s]; };
  return { completion: langs('completion'), hover: langs('hover'), definition: langs('definition'), formatting: langs('formatting'), triggers: [...new Set(providers.completion.flatMap((p) => p.extra.triggers || []))] };
}
function listExtensions() {
  return [...extensionsById.values()].map((e) => ({ id: e.id, name: e.name, version: e.pkg.version, activated: e.activated, error: e.error, hasCode: Boolean(e.main), commands: e.contributed, events: e.events.slice(0, 12) }));
}
function listCommands() {
  const out = []; const seen = new Set();
  for (const e of extensionsById.values()) for (const c of e.contributed) { seen.add(c.command); out.push({ ...c, ext: e.name, registered: commands.has(c.command) }); }
  for (const id of commands.keys()) if (!seen.has(id) && !id.startsWith('_')) out.push({ command: id, title: id, category: '', ext: '', registered: true });
  return out;
}

// ------------------------------------------------------------------ dispatch
async function onMessage(m) {
  if (m.id != null && !m.method) { const r = pending.get(m.id); if (r) { pending.delete(m.id); r(m.result); } return; }
  const p = m.params || {};
  const reply = (result) => { if (m.id != null) send({ jsonrpc: '2.0', id: m.id, result }); };
  const fail = (e) => { if (m.id != null) send({ jsonrpc: '2.0', id: m.id, error: { code: -32000, message: String((e && e.message) || e) } }); };
  try {
    switch (m.method) {
      case 'initialize': {
        settings = p.settings || {};
        loadExtensions();
        reply({ host: 'astro-exthost', version: VERSION, node: process.version, root: ROOT, extensions: listExtensions() });
        startup().then(() => notify('ready', { extensions: listExtensions(), commands: listCommands(), capabilities: capabilities() }));
        return;
      }
      case 'shutdown': reply(null); return;
      case 'exit': for (const e of extensionsById.values()) { try { if (e.module && e.module.deactivate) await e.module.deactivate(); } catch (_e) { /* ignore */ } for (const d of e.subscriptions) { try { d.dispose(); } catch (_e) { /* ignore */ } } } process.exit(0); return;
      case 'extensions': reply(listExtensions()); return;
      case 'commands': reply(listCommands()); return;
      case 'capabilities': reply(capabilities()); return;
      case 'execute': reply(await executeCommand(p.command, p.args || [])); notify('ext/state', { extensions: listExtensions(), capabilities: capabilities() }); return;
      case 'activate': { const e = extensionsById.get(String(p.id).toLowerCase()); if (e) await activate(e); reply(listExtensions()); notify('ready', { extensions: listExtensions(), commands: listCommands(), capabilities: capabilities() }); return; }
      case 'complete': reply(await provideCompletion(p)); return;
      case 'hover': reply(await provideHover(p)); return;
      case 'definition': reply(await provideDefinition(p)); return;
      case 'format': reply(await provideFormatting(p)); return;
      case 'settings': settings = p.settings || {}; ev.config.fire({ affectsConfiguration: () => true }); return;
      case 'doc/open': {
        const u = Uri.parse(p.uri); let d = docs.get(p.uri);
        if (!d) { d = new TextDocument(u, p.languageId || langOfPath(u.fsPath), p.version || 1, p.text); docs.set(p.uri, d); await activateBy(`onLanguage:${d.languageId}`); ev.open.fire(d); notify('caps', capabilities()); }
        else if (d._text !== p.text) { d._set(p.text); d.version++; ev.change.fire({ document: d, contentChanges: [{ range: new Range(0, 0, d.lineCount, 0), rangeOffset: 0, rangeLength: 0, text: p.text }], reason: undefined }); }
        return;
      }
      case 'doc/change': { const d = docs.get(p.uri); if (!d || d._text === p.text) return; d._set(p.text); d.version = p.version || d.version + 1; d.isDirty = true; ev.change.fire({ document: d, contentChanges: [{ range: new Range(0, 0, d.lineCount, 0), rangeOffset: 0, rangeLength: d._text.length, text: p.text }], reason: undefined }); return; }
      case 'doc/save': { const d = docs.get(p.uri); if (d) { d.isDirty = false; ev.save.fire(d); } return; }
      case 'doc/close': { const d = docs.get(p.uri); if (d) { docs.delete(p.uri); d.isClosed = true; ev.close.fire(d); } return; }
      case 'editor': {
        const d = p.uri ? docs.get(p.uri) : null;
        if (!d) { if (activeEditor) { activeEditor = undefined; ev.active.fire(undefined); } return; }
        if (!activeEditor || activeEditor.document !== d) { activeEditor = makeEditor(d, p.selections); ev.active.fire(activeEditor); ev.visible.fire([activeEditor]); }
        else if (p.selections) { activeEditor.selections = makeEditor(d, p.selections).selections; ev.selection.fire({ textEditor: activeEditor, selections: activeEditor.selections, kind: 1 }); }
        return;
      }
      default: if (m.id != null) fail(new Error(`Unknown method ${m.method}`));
    }
  } catch (e) { console.error(e); fail(e); }
}

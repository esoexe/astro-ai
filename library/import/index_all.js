#!/usr/bin/env node
// Builds the community catalog of the Astro AI Library (schema 2): every MCP server in the official MCP Registry,
// MCP servers published on npm and as Hugging Face Spaces, MCP projects tagged on GitHub, Docker's mcp/ images,
// Agent Skills and Claude Code plugins found in public GitHub repos, Open VSX editor extensions, installable AI
// command-line tools and a directory of AI apps. The website serves the result as static files and Astro AI Desktop
// installs from it. Only public APIs are used, robots.txt is respected (PyPI's JSON and simple APIs are disallowed
// for crawlers, so PyPI is not bulk-indexed) and every count is real: nothing is padded or duplicated.
//
//   node library/import/index_all.js <out-dir> [--reviewed <index.json>] [--max-repos N]
//        [--only mcp,npm,hf,docker,openvsx,skills,ghmcp,tools,apps] [--budget-min M] [--search-min M] [--npm-min M]
//
// Output (<out-dir> is normally dist/library/v2):
//   manifest.json                  counts, the shard files of each kind and where the data came from
//   list/<kind>-<n>.json           compact rows for browsing and search: [id, name, summary, publisher, flags, tags, extra]
//   detail/<hh>.json               everything needed to install, keyed by id (hh = first 2 hex of sha256(id))
//
// Integrity: MCP packages are pinned to the exact version the registry lists; skills and plugins are pinned to a git
// commit and every file has a SHA-256, so the desktop installer downloads exactly what was indexed. Hooks, binaries
// and plugin UI scripts are never listed. If a source can't be reached, the last published data is reused.
//
// Schema 3 (--format v3, published at https://esoexe.github.io/astro-library/v3/ by that repository's workflow) stores
// the same rows and details as gzip files in smaller shards, with a search index (q/<xx>.json.gz), so apps never
// download the whole catalog. --extra adds GitHub code search for SKILL.md files (resumed from build to build).
// --prev2 <v2 url> lets the first schema 3 build start from the schema 2 build. The reader is web/lib3core.js.
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto'), os = require('os');
const { execFile } = require('child_process');

const argv = process.argv.slice(2);
// A broken pipe or reset connection to one child process or server must not throw away hours of indexing.
process.on('uncaughtException', (e) => { if (e && /^(EPIPE|ECONNRESET)$/.test(e.code)) { console.log('[library] ignored ' + e.code + ' (' + (e.syscall || '') + ')'); return; } console.error(e); process.exit(1); });
const opt = (name, def) => { const i = argv.indexOf('--' + name); return i >= 0 ? argv[i + 1] : def; };
const OUT = argv[0] && !argv[0].startsWith('--') ? path.resolve(argv[0]) : path.resolve('library-v2');
const SOURCES = JSON.parse(fs.readFileSync(path.join(__dirname, 'sources.json'), 'utf8'));
const CLI = JSON.parse(fs.readFileSync(path.join(__dirname, 'cli_tools.json'), 'utf8'));
const MAX_REPOS = Number(opt('max-repos', SOURCES.max_repos || 2500));
const ONLY = new Set((opt('only', 'mcp,npm,hf,docker,openvsx,skills,ghmcp,tools,apps')).split(','));
const T_START = Date.now();
const BUDGET_END = T_START + Number(opt('budget-min', 300)) * 60000;
// Service bases (overridable so the tests can point them at a local fake server).
const NPM = process.env.ASTRO_IDX_NPM || 'https://registry.npmjs.org';
const HF = process.env.ASTRO_IDX_HF || 'https://huggingface.co';
const OVSX = process.env.ASTRO_IDX_OVSX || 'https://open-vsx.org';
const DHUB = process.env.ASTRO_IDX_DOCKER || 'https://hub.docker.com';
const GHAPI = process.env.ASTRO_IDX_GHAPI || 'https://api.github.com';
const DELAY = process.env.ASTRO_IDX_DELAY_SCALE != null ? Number(process.env.ASTRO_IDX_DELAY_SCALE) : 1;   // tests set 0
const pause = (ms) => sleep(ms * DELAY);
const LIVE = opt('live', 'https://esoexe.github.io/astro-ai/library/v2/');
const CACHE = path.resolve(opt('cache', path.join(os.tmpdir(), 'astro-lib-git')));
const CONC = Number(opt('jobs', 8));
const UA = 'AstroAI-Library-Indexer/2 (+https://github.com/esoexe/astro-ai)';
const SHARD = 5000;
const FORMAT = opt('format', 'v2') === 'v3' ? 'v3' : 'v2';
const EXTRA = argv.includes('--extra');

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
const log = (...a) => console.log('[library]', ...a);
const clip = (s, n) => { s = String(s || '').replace(/\s+/g, ' ').trim(); return s.length > n ? s.slice(0, n - 1).trimEnd() + '…' : s; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function get(url, { json = true, tries = 4, timeout = 30000, headers = {}, meta = null } = {}) {
  let last;
  for (let i = 0; i < tries; i++) {
    try {
      const r = await fetch(url, { headers: { 'User-Agent': UA, Accept: json ? 'application/json' : '*/*', ...headers }, signal: AbortSignal.timeout(timeout) });
      if (r.status === 404) return null;
      if (r.status === 429) { await sleep(Math.min(120000, (Number(r.headers.get('retry-after')) || 20) * 1000)); throw new Error('HTTP 429'); }
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      if (meta) meta.headers = r.headers;
      return json ? await r.json() : await r.text();
    } catch (e) { last = e; await sleep(800 * (i + 1)); }
  }
  throw new Error(`${url}: ${last && last.message}`);
}
// "Safe and clean": adult content, jailbreaks and malware never enter the catalog, whatever the source.
const UNSAFE = /\b(nsfw|porn\w*|hentai|nud(?:e|es|ity|ify)|undress\w*|deep-?nude|erotic\w*|onlyfans|sex|sexy|sexual|xxx|uncensored|jailbreak\w*|keylogger|info-?stealer|stealer|ransomware|botnet|ddos|carding)\b/i;
const safeText = (...t) => !UNSAFE.test(t.filter(Boolean).join(' ').replace(/[-_]+/g, ' '));
const ghKey = (u) => { const m = /github\.com[/:]([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.-]+)/.exec(String(u || '')); return m ? `${m[1]}/${m[2].replace(/\.git$/, '')}`.toLowerCase() : ''; };
const COVERED = new Set();   // github owner/repo (lower) of MCP servers already listed from a richer source
async function pool(items, n, fn) {
  let i = 0; const out = new Array(items.length);
  await Promise.all(Array.from({ length: Math.min(n, items.length) }, async () => { while (i < items.length) { const k = i++; out[k] = await fn(items[k], k); } }));
  return out;
}

// ------------------------------------------------------------------ ids
const IDS = new Set();
function slug(s, n = 48) { return String(s || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, n).replace(/-+$/, ''); }
function makeId(pref, key) {
  let base = slug(pref) || 'item';
  if (!/^[a-z0-9]/.test(base)) base = 'x' + base;
  let id = base.slice(0, 48).replace(/-+$/, '');
  if (!IDS.has(id) && id.length >= 2) { IDS.add(id); return id; }
  for (let k = 0; k < 50; k++) {
    const h = sha256(key + '#' + k).slice(0, 6);
    id = base.slice(0, 41).replace(/-+$/, '') + '-' + h;
    if (!IDS.has(id)) { IDS.add(id); return id; }
  }
  throw new Error('id space exhausted for ' + key);
}

// rows[kind] = [[id, name, summary, publisher, flags, tags, extra]], details[id] = {...}
const rows = { mcp: [], skill: [], plugin: [], tool: [] };
const details = new Map();
const sourcesUsed = [];
const STATE = { repos: {}, ids: {} };
let blocked = 0;
const add = (kind, row, det) => {
  if (!det.reviewed && !safeText(row[0], row[1], row[2])) { blocked++; return false; }
  rows[kind].push(row); details.set(row[0], det); return true;
};

// ------------------------------------------------------------------ reviewed items (schema 1 index)
function addReviewed(indexPath) {
  if (!indexPath || !fs.existsSync(indexPath)) return 0;
  const idx = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
  for (const it of idx.items || []) {
    IDS.add(it.id);
    const kind = it.kind === 'mcp' || it.kind === 'skill' || it.kind === 'plugin' ? it.kind : 'tool';
    const flags = 'r' + (it.kind === 'mcp' ? (it.mcp && it.mcp.type === 'http' ? 'h' : 'n') : 'g');
    rows[kind].push([it.id, it.name, clip(it.summary, 140), it.publisher || 'Astro AI', flags, (it.tags || []).slice(0, 4).join(','), it.version || '']);
    details.set(it.id, { k: kind, reviewed: 1 });   // installed from the reviewed index (schema 1)
  }
  return (idx.items || []).length;
}

// ------------------------------------------------------------------ MCP registry
const ARG_BAD = /[&|<>^%"`$;\r\n\0]/;
function inputName(s) { return String(s || '').toUpperCase().replace(/[^A-Z0-9_]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40) || 'VALUE'; }
function mcpPackageSpec(p, inputs) {
  const env = [];
  for (const e of p.environmentVariables || []) {
    if (!/^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(e.name || '')) continue;
    env.push({ name: e.name, label: clip(e.description || e.name, 160), secret: e.isSecret ? 1 : 0, req: e.isRequired ? 1 : 0, def: e.default && !e.isSecret ? String(e.default).slice(0, 200) : undefined });
  }
  const conv = (list) => {
    const out = [];
    for (const a of list || []) {
      let v = a.value != null ? String(a.value) : a.default != null ? String(a.default) : '';
      if (!v && !a.isRequired) continue;
      if (!v) { const n = inputName(a.valueHint || a.name); inputs.push({ name: n, label: clip(a.description || a.name || n, 160), req: 1 }); v = '{' + n + '}'; }
      v = v.replace(/\{([A-Za-z0-9_.-]+)\}/g, (_, n) => { const N = inputName(n); if (!inputs.some((i) => i.name === N)) { const d = a.variables && a.variables[n]; inputs.push({ name: N, label: clip((d && d.description) || n, 160), req: d && d.isRequired === false ? 0 : 1, secret: d && d.isSecret ? 1 : 0 }); } return '{' + N + '}'; });
      if (ARG_BAD.test(v.replace(/\{[A-Z0-9_]+\}/g, '')) || v.length > 300) return null;
      if (a.type === 'named' && a.name) { if (ARG_BAD.test(a.name)) return null; if (v === 'true' && a.format === 'boolean') out.push(a.name); else out.push(a.name, v); }
      else out.push(v);
    }
    return out;
  };
  const pargs = conv(p.packageArguments); if (pargs === null) return null;
  const transport = (p.transport && p.transport.type) || 'stdio';
  if (transport !== 'stdio') return null;   // package-run http servers need a port and a URL; not one-click
  if (p.registryType === 'npm') {
    const pkg = `${p.identifier}@${p.version}`;
    if (!/^(?:@[a-z0-9][a-z0-9._-]*\/)?[a-z0-9][a-z0-9._-]*@\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(pkg)) return null;
    return { t: 'stdio', cmd: 'npx', args: ['-y', pkg, ...pargs], env };
  }
  if (p.registryType === 'pypi') {
    const pkg = `${p.identifier}==${p.version}`;
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*==[0-9][0-9A-Za-z.+!-]*$/.test(pkg)) return null;
    return { t: 'stdio', cmd: 'uvx', args: [pkg, ...pargs], env };
  }
  if (p.registryType === 'oci') {
    let img = String(p.identifier || '');
    if (p.version && !/[:@]/.test(img.split('/').pop())) img += ':' + p.version;
    if (!/^[a-z0-9][a-z0-9._\/-]*(?::[A-Za-z0-9._-]{1,128}|@sha256:[a-f0-9]{64})$/.test(img) || /:latest$/.test(img)) return null;
    const rargs = conv(p.runtimeArguments); if (rargs === null) return null;
    if (rargs.some((a) => /^--?(privileged|cap-add|pid|net(work)?=host|device|security-opt|ipc|userns|uts)\b/.test(a))) return null;
    return { t: 'stdio', cmd: 'docker', args: ['run', '-i', '--rm', ...rargs, ...env.flatMap((e) => ['-e', e.name]), img, ...pargs], env };
  }
  return null;
}
function mcpRemoteSpec(r, inputs) {
  let url = String(r.url || '');
  if (!/^https:\/\/[A-Za-z0-9.-]+(:\d+)?\/?[^\s"'<>]*$/.test(url)) return null;
  const vars = r.variables || {};
  url = url.replace(/\{([A-Za-z0-9_.-]+)\}/g, (_, n) => { const N = inputName(n); const d = vars[n] || {}; if (!inputs.some((i) => i.name === N)) inputs.push({ name: N, label: clip(d.description || n, 160), req: d.isRequired === false ? 0 : 1, def: d.default, url: 1 }); return '{' + N + '}'; });
  const headers = [];
  for (const h of r.headers || []) {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(h.name || '')) continue;
    const fixed = h.value && !/\{/.test(h.value) && !h.isSecret ? String(h.value) : '';
    let prefix = '';
    if (h.value && /^Bearer\s+\{/.test(h.value)) prefix = 'Bearer ';
    else if (/^authorization$/i.test(h.name) && /bearer/i.test(h.description || '')) prefix = 'Bearer ';
    headers.push({ h: h.name, label: clip(h.description || h.name, 160), secret: h.isSecret ? 1 : 0, req: h.isRequired ? 1 : 0, prefix: prefix || undefined, value: fixed || undefined });
  }
  return { t: r.type === 'sse' ? 'sse' : 'http', url, headers };
}
const REG_PKGS = new Set();   // "npm:<name>", "oci:<image>" already listed by the registry
async function importMcp() {
  const base = SOURCES.mcp_registry;
  const all = new Map();
  let cursor = '', pages = 0;
  if (opt('registry-file')) { const j = JSON.parse(fs.readFileSync(opt('registry-file'), 'utf8')); for (const e of Array.isArray(j) ? j : j.servers) all.set((e.server || e).name, e); }
  else for (;;) {
    const j = await get(`${base}?limit=100&version=latest${cursor ? '&cursor=' + encodeURIComponent(cursor) : ''}`);
    pages++;
    for (const e of (j && j.servers) || []) { const s = e.server || e; all.set(s.name, e); }
    cursor = j && j.metadata && j.metadata.nextCursor;
    if (!cursor || !(j.servers || []).length) break;
    if (pages % 50 === 0) log(`MCP registry: ${pages} pages, ${all.size} servers`);
  }
  log(`MCP registry: ${all.size} servers from ${pages} pages`);
  const list = [...all.values()].sort((a, b) => String((a.server || a).name).localeCompare(String((b.server || b).name)));
  let n = 0;
  for (const e of list) {
    const s = e.server || e, meta = (e._meta && e._meta['io.modelcontextprotocol.registry/official']) || {};
    if (meta.status === 'deleted') continue;
    const name = String(s.name || '');
    const [ns, short] = name.includes('/') ? name.split('/', 2) : ['', name];
    const nsParts = ns.split('.').filter(Boolean);
    const owner = nsParts[0] === 'io' && nsParts[1] === 'github' ? nsParts.slice(2).join('-') : nsParts.length > 1 ? nsParts[nsParts.length - 1] === 'com' ? nsParts[0] : nsParts.slice(1).join('-') : ns;
    let spec = null; const inputs = [];
    const remotes = s.remotes || [];
    const http = remotes.find((r) => r.type === 'streamable-http'), sse = remotes.find((r) => r.type === 'sse');
    if (http) spec = mcpRemoteSpec(http, inputs);
    if (!spec) for (const type of ['npm', 'pypi', 'oci']) { const p = (s.packages || []).find((x) => x.registryType === type); if (p) { inputs.length = 0; spec = mcpPackageSpec(p, inputs); if (spec) break; } }
    if (!spec && sse) { inputs.length = 0; spec = mcpRemoteSpec(sse, inputs); }
    const repo = s.repository && s.repository.url;
    const web = s.websiteUrl || repo || '';
    if (ghKey(repo)) COVERED.add(ghKey(repo));
    for (const p of s.packages || []) if (p.identifier) REG_PKGS.add(p.registryType + ':' + String(p.identifier).toLowerCase().replace(/^docker\.io\//, ''));
    const id = makeId(short && owner && !short.toLowerCase().includes(owner.toLowerCase()) ? `${owner}-${short}` : short || owner, 'mcp:' + name);
    const flags = (spec ? spec.t === 'http' ? 'h' : spec.t === 'sse' ? 'e' : spec.cmd === 'npx' ? 'n' : spec.cmd === 'uvx' ? 'p' : 'd' : 'l')
      + (meta.status === 'deprecated' ? 'x' : '')
      + ((spec && ((spec.env || []).some((x) => x.secret && x.req) || (spec.headers || []).some((x) => x.secret))) ? 's' : '');
    const title = s.title || short || name;
    if (add('mcp', [id, clip(title, 70), clip(s.description, 140), owner || ns, flags, '', s.version || ''], {
      k: 'mcp', n: clip(title, 90), d: clip(s.description, 600), v: s.version || '', reg: name, web, repo: repo || undefined,
      st: meta.status && meta.status !== 'active' ? meta.status : undefined, i: spec || undefined, in: inputs.length ? inputs : undefined,
    })) n++;
  }
  sourcesUsed.push({ kind: 'mcp', name: 'Official MCP Registry', url: 'https://registry.modelcontextprotocol.io', count: n });
}

// ------------------------------------------------------------------ git repos (skills + plugins)
const TEXT_EXT = new Set(('md mdx markdown txt rst json jsonc json5 yaml yml toml ini cfg conf csv tsv xml html htm css scss js mjs cjs ts tsx jsx mts cts py pyi sh bash zsh fish ps1 psm1 bat cmd rb go rs java kt kts swift c h cc cpp hpp cs php sql r lua pl pm ex exs erl hs ml scala clj dart vue svelte astro graphql gql proto tf hcl nix dockerfile makefile mk gradle properties env-example template tpl j2 jinja hbs mustache ejs liquid tex bib sty cls typ ipynb').split(' '));
const NAME_OK = new Set(['license', 'licence', 'notice', 'makefile', 'dockerfile', 'readme', 'changelog', 'authors', 'contributing', '.gitignore', '.env.example']);
const SKIP_DIR = /(^|\/)(\.git|node_modules|__pycache__|\.venv|venv|dist|build|target|\.next|coverage|\.cache|hooks)(\/|$)/;
const MAX_FILE = 1024 * 1024, MAX_SKILL = 5 * 1024 * 1024, MAX_PLUGIN = 8 * 1024 * 1024, MAX_SKILL_FILES = 200, MAX_PLUGIN_FILES = 400;
function fileAllowed(rel, size, kind) {
  if (size > MAX_FILE || rel.length > 240) return false;
  if (SKIP_DIR.test(rel)) return false;
  const base = rel.split('/').pop(), low = base.toLowerCase();
  if (low === 'hooks.json') return false;
  if (kind === 'plugin' && (rel === 'main.js' || rel === 'style.css')) return false;   // Astro plugin UI code is never installed from the community
  if (/[\\:*?"<>|\0]/.test(rel) || rel.split('/').some((p) => p === '..' || p === '.' || p === '')) return false;
  const ext = low.includes('.') ? low.split('.').pop() : '';
  return TEXT_EXT.has(ext) || NAME_OK.has(low) || NAME_OK.has(low.replace(/\.(md|txt)$/, ''));
}
function run(cmd, args, { cwd, input, timeout = 120000, maxBuffer = 256 * 1024 * 1024, encoding = 'utf8' } = {}) {
  return new Promise((resolve, reject) => {
    const p = execFile(cmd, args, { cwd, timeout, maxBuffer, encoding, env: { ...process.env, GIT_TERMINAL_PROMPT: '0', GIT_LFS_SKIP_SMUDGE: '1', GCM_INTERACTIVE: 'never' } }, (err, stdout, stderr) => err ? reject(new Error((stderr || err.message).toString().slice(0, 300))) : resolve(stdout));
    // git can exit (timeout, bad repo) before reading all of its input: that EPIPE must not end the whole build
    if (input != null) { p.stdin.on('error', () => {}); p.stdin.end(input); }
  });
}
function frontmatter(text) {
  const m = /^\uFEFF?---\r?\n([\s\S]*?)\r?\n---/.exec(text);
  const out = {};
  if (!m) return out;
  const lines = m[1].split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const km = /^([A-Za-z_][A-Za-z0-9_-]*):\s*(.*)$/.exec(lines[i]);
    if (!km) continue;
    let v = km[2].trim();
    if (v === '|' || v === '>' || v === '|-' || v === '>-' || v === '') {
      const buf = [];
      while (i + 1 < lines.length && (/^\s+/.test(lines[i + 1]) || lines[i + 1] === '')) buf.push(lines[++i].trim());
      v = buf.join(' ').trim();
    }
    v = v.replace(/^(['"])([\s\S]*)\1$/, '$2');
    out[km[1].toLowerCase()] = v;
  }
  return out;
}
function licenseOf(text) {
  const t = String(text || '').slice(0, 3000);
  if (/MIT License|Permission is hereby granted, free of charge/i.test(t)) return 'MIT';
  if (/Apache License,?\s+Version 2\.0/i.test(t)) return 'Apache-2.0';
  if (/GNU AFFERO GENERAL PUBLIC LICENSE/i.test(t)) return 'AGPL-3.0';
  if (/GNU LESSER GENERAL PUBLIC LICENSE/i.test(t)) return 'LGPL';
  if (/GNU GENERAL PUBLIC LICENSE/i.test(t)) return /Version 2/i.test(t) ? 'GPL-2.0' : 'GPL-3.0';
  if (/Mozilla Public License/i.test(t)) return 'MPL-2.0';
  if (/BSD 3-Clause|Redistribution and use in source and binary forms/i.test(t)) return 'BSD';
  if (/CC0|Creative Commons Zero|No Copyright/i.test(t)) return 'CC0-1.0';
  if (/This is free and unencumbered software released into the public domain/i.test(t)) return 'Unlicense';
  if (/Creative Commons Attribution/i.test(t)) return 'CC-BY';
  return 'see repo';
}
// A repo produces candidates; ids and duplicates are resolved afterwards in priority order, so the result doesn't
// depend on which clone finished first. cand = { kind, key, fp, row, det } (row[0] / id assigned later)
const extraRepos = [];
async function indexRepo(slugRepo, stats, info) {
  const [owner, name] = slugRepo.split('/');
  const dir = path.join(CACHE, `${owner}__${name}`.replace(/[^A-Za-z0-9_.-]/g, '_'));
  fs.rmSync(dir, { recursive: true, force: true });
  const cands = [];
  try {
    await run('git', ['clone', '--quiet', '--depth', '1', '--filter=blob:none', '--no-checkout', '--single-branch', `https://github.com/${owner}/${name}.git`, dir], { timeout: 120000 });
    const commit = (await run('git', ['rev-parse', 'HEAD'], { cwd: dir })).trim();
    info.c = commit;
    const tree = (await run('git', ['ls-tree', '-r', '--full-tree', 'HEAD'], { cwd: dir })).split('\n').filter(Boolean).map((l) => {
      const tab = l.indexOf('\t'); const [mode, type, sha] = l.slice(0, tab).split(/\s+/);
      return { mode, type, sha, size: 0, path: l.slice(tab + 1) };   // sizes are checked after download (asking git for them would fetch every blob)
    }).filter((e) => e.type === 'blob' && e.mode !== '120000' && !e.path.startsWith('"'));
    if (tree.length > 400000) throw new Error('repository too large');
    const skills = [], pluginDirs = [], markets = [];
    for (const e of tree) {
      if (/(^|\/)SKILL\.md$/i.test(e.path) && !/(^|\/)(node_modules|\.git|test-fixtures)\//i.test(e.path)) skills.push(e.path.includes('/') ? e.path.slice(0, e.path.lastIndexOf('/')) : '');
      if (/(^|\/)\.claude-plugin\/plugin\.json$/.test(e.path)) pluginDirs.push(e.path.replace(/\/?\.claude-plugin\/plugin\.json$/, ''));
      if (/(^|\/)\.claude-plugin\/marketplace\.json$/.test(e.path)) markets.push(e.path);
    }
    // index the tree by directory prefix once (repos with thousands of skills would be slow otherwise)
    const byDirCache = new Map();
    const byDir = (d) => {
      if (d === '') return tree;
      if (!byDirCache.has(d)) byDirCache.set(d, tree.filter((e) => e.path.startsWith(d + '/')));
      return byDirCache.get(d);
    };
    const want = new Set();
    const skillDirs = skills.filter((d) => d !== '' || tree.length < 400).slice(0, 3000);
    const plugs = pluginDirs.slice(0, 1500);
    for (const d of skillDirs) for (const e of byDir(d)) if (fileAllowed(d ? e.path.slice(d.length + 1) : e.path, 0, 'skill')) want.add(e.path);
    for (const d of plugs) for (const e of byDir(d)) if (fileAllowed(d ? e.path.slice(d.length + 1) : e.path, 0, 'plugin')) want.add(e.path);
    for (const m of markets) want.add(m);
    const lic = tree.find((e) => /^(LICENSE|LICENCE|COPYING)(\.md|\.txt)?$/i.test(e.path));
    if (lic) want.add(lic.path);
    if (!skillDirs.length && !plugs.length && !markets.length) { stats.empty++; return cands; }
    // One batched download of every blob we need (sparse checkout), then read the exact blob bytes with cat-file.
    const wantList = [...want];
    if (wantList.length > 60000) throw new Error('too many files');
    await run('git', ['sparse-checkout', 'set', '--no-cone', '--stdin'], { cwd: dir, input: wantList.map((p) => '/' + p.replace(/([*?[\]\\!#])/g, '\\$1')).join('\n') + '\n', timeout: 60000 });
    await run('git', ['-c', 'core.autocrlf=false', 'checkout', '--quiet', 'HEAD'], { cwd: dir, timeout: 300000 });
    const shaOf = new Map(tree.map((e) => [e.path, e.sha]));
    const blobs = new Map();
    const shas = [...new Set(wantList.map((p) => shaOf.get(p)).filter(Boolean))];
    const raw = await run('git', ['cat-file', '--batch'], { cwd: dir, input: shas.join('\n') + '\n', encoding: 'buffer', timeout: 300000, maxBuffer: 1024 * 1024 * 1024 });
    let off = 0;
    while (off < raw.length) {
      const nl = raw.indexOf(10, off); if (nl < 0) break;
      const head = raw.slice(off, nl).toString().split(' ');
      if (head[1] === 'missing') { off = nl + 1; continue; }
      const sz = Number(head[2]); if (sz <= MAX_FILE) blobs.set(head[0], raw.slice(nl + 1, nl + 1 + sz)); off = nl + 1 + sz + 1;
    }
    for (const e of tree) { const b = blobs.get(e.sha); e.size = b ? b.length : (want.has(e.path) ? MAX_FILE + 1 : 0); }
    const content = (p) => blobs.get(shaOf.get(p));
    const license = lic ? licenseOf(content(lic.path)) : 'see repo';
    const fileList = (d, kind, maxFiles, maxBytes) => {
      const files = []; let total = 0;
      for (const e of byDir(d)) {
        const rel = d ? e.path.slice(d.length + 1) : e.path;
        if (!fileAllowed(rel, e.size, kind)) continue;
        const b = content(e.path); if (!b) continue;
        if (b.includes(0)) continue;   // binary despite the extension
        total += b.length; files.push([rel, sha256(b), b.length]);
        if (files.length > maxFiles || total > maxBytes) return null;
      }
      return files;
    };
    const ghTree = (d) => `https://github.com/${owner}/${name}/tree/${commit}${d ? '/' + d.split('/').map(encodeURIComponent).join('/') : ''}`;
    const stars = info.stars || 0;
    for (const d of skillDirs) {
      const md = content((d ? d + '/' : '') + 'SKILL.md') || content((d ? d + '/' : '') + 'skill.md');
      if (!md) continue;
      const text = md.toString('utf8');
      const files = fileList(d, 'skill', MAX_SKILL_FILES, MAX_SKILL);
      if (!files || !files.some((f) => /^skill\.md$/i.test(f[0]))) { stats.toobig++; continue; }
      const fm = frontmatter(text);
      const sname = clip(fm.name || (d ? d.split('/').pop() : name), 64);
      const desc = fm.description || (text.replace(/^---[\s\S]*?---/, '').split('\n').find((l) => l.trim() && !l.startsWith('#')) || '');
      const inPlugin = plugs.find((p) => p === '' || d.startsWith(p + '/'));
      cands.push({
        kind: 'skill', key: `skill:${slugRepo}:${d}`, fp: sha256(text.replace(/\r\n/g, '\n').trim()).slice(0, 16),
        idBase: sname.toLowerCase().includes(owner.toLowerCase()) ? sname : `${sname}-${owner}`,
        row: [null, sname, clip(desc, 140), owner, 'g' + (files.some((f) => /\.(py|sh|js|mjs|ts|ps1|rb|bat|cmd)$/i.test(f[0])) ? 'c' : ''), '', `${owner}/${name}`, stars],
        det: { k: 'skill', n: sname, d: clip(desc, 600), repo: `${owner}/${name}`, c: commit, dir: d, f: files, lic: license, web: ghTree(d), pl: inPlugin != null ? inPlugin : undefined, stars: stars || undefined },
      });
    }
    for (const d of plugs) {
      const pj = content((d ? d + '/' : '') + '.claude-plugin/plugin.json');
      if (!pj) continue;
      let meta = {}; try { meta = JSON.parse(pj.toString('utf8')); } catch { continue; }
      const files = fileList(d, 'plugin', MAX_PLUGIN_FILES, MAX_PLUGIN);
      if (!files || !files.length) { stats.toobig++; continue; }
      const pname = clip(meta.name || (d ? d.split('/').pop() : name), 64);
      const author = (meta.author && (meta.author.name || meta.author)) || owner;
      const parts = [];
      const has = (re) => files.some((f) => re.test(f[0]));
      if (has(/^commands\//)) parts.push('commands'); if (has(/^agents\//)) parts.push('agents'); if (has(/(^|\/)SKILL\.md$/i)) parts.push('skills'); if (has(/^\.mcp\.json$/)) parts.push('mcp');
      const fp = sha256(files.map((f) => f[0] + f[1]).join('|')).slice(0, 16);
      cands.push({
        kind: 'plugin', key: `plugin:${slugRepo}:${d}`, fp,
        idBase: pname.toLowerCase().includes(owner.toLowerCase()) ? `${pname}-plugin` : `${pname}-${owner}-plugin`,
        row: [null, pname, clip(meta.description, 140), clip(typeof author === 'string' ? author : owner, 40), 'g', parts.join(','), `${owner}/${name}`, stars],
        det: { k: 'plugin', n: pname, d: clip(meta.description, 600), v: meta.version ? String(meta.version).slice(0, 30) : undefined, repo: `${owner}/${name}`, c: commit, dir: d, f: files, lic: license, web: ghTree(d),
          kw: Array.isArray(meta.keywords) ? meta.keywords.slice(0, 8).map((x) => clip(x, 30)) : undefined, stars: stars || undefined },
      });
    }
    // Marketplaces can point at plugins in other repos; queue those repos too.
    for (const m of markets) {
      try {
        const mj = JSON.parse(content(m).toString('utf8'));
        for (const p of mj.plugins || []) {
          const s = p.source; let r = '';
          if (s && typeof s === 'object' && s.source === 'github' && s.repo) r = s.repo;
          else if (s && typeof s === 'object' && s.url) { const mm = /github\.com\/([^/]+\/[^/.]+)/.exec(s.url); if (mm) r = mm[1]; }
          if (r && /^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(r)) extraRepos.push(r);
        }
      } catch {}
    }
    stats.ok++;
  } catch (e) {
    stats.failed++; info.failed = true;
    if (stats.failed <= 30) log(`  skip ${slugRepo}: ${String(e.message).split('\n')[0].slice(0, 140)}`);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
  return cands;
}
function reposFromMarkdown(md) {
  const out = [];
  const re = /github\.com\/([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.-]+)/g;
  let m;
  while ((m = re.exec(md))) {
    const o = m[1], r = m[2].replace(/\.git$/, '').replace(/[.]+$/, '');
    if (/^(topics|orgs|sponsors|features|marketplace|apps|settings|login|about|pricing|collections|trending|search|users|enterprise|site|security|readme|github|user-attachments)$/i.test(o)) continue;
    if (!r || /^(issues|pulls|blob|tree|wiki)$/i.test(r)) continue;
    out.push(`${o}/${r}`);
  }
  return out;
}

// GitHub repository search by topic (needs a token: GITHUB_TOKEN in Actions). Search returns at most 1000 results per
// query, so big topics are split by date until each slice fits. Results are kept between builds (state-gh.json):
// every build asks only for repos pushed since the last build, then continues a full sweep of all topics where the
// previous build stopped, so the whole of GitHub's topic lists is re-read every few days within the rate limit.
// Repos not seen by any sweep for 45 days (deleted, made private, topic removed) are dropped.
const TOPICS = SOURCES.skill_topics || ['claude-skills', 'agent-skills', 'claude-code-plugin'];
const MCP_TOPICS = SOURCES.mcp_topics || [];
const GH = { items: {}, npm: {}, sweep: null, inc: null, code: null, found: {} };   // state-gh.json (code/found: SKILL.md code search)
const today = () => new Date().toISOString().slice(0, 10);
async function loadGhState() {
  if (argv.includes('--fresh')) return;
  let j = null;
  try {
    j = await readCatalogFile(opt('prev', LIVE), FORMAT === 'v3' ? 'state-gh.json.gz' : 'state-gh.json');
    if (!j && FORMAT === 'v3' && opt('prev2')) { j = await readCatalogFile(opt('prev2'), 'state-gh.json'); if (j) log('GitHub state taken from the schema 2 build'); }
  } catch (e) { log('previous GitHub state unavailable: ' + e.message); }
  if (j && j.schema === 1) { GH.items = j.items || {}; GH.npm = j.npm || {}; GH.sweep = j.sweep || null; GH.inc = j.inc || null; GH.code = j.code || null; GH.found = j.found || {}; }
  log(`GitHub state: ${Object.keys(GH.items).length} repos, ${Object.keys(GH.npm).length} npm packages`);
}
function ghTake(it, group) {
  if (!it || !it.full_name || it.fork || it.archived || it.private) return;
  const k = it.full_name.toLowerCase();
  const e = GH.items[k] || (GH.items[k] = { g: 0 });
  e.r = it.full_name; e.s = it.stargazers_count || 0; e.p = it.pushed_at || e.p; e.g |= group; e.seen = today();
  e.d = clip(it.description || '', 200) || undefined;
  e.h = /^https:\/\/[^\s"'<>]+$/.test(it.homepage || '') ? String(it.homepage).slice(0, 200) : undefined;
  e.l = it.license && it.license.spdx_id && it.license.spdx_id !== 'NOASSERTION' ? it.license.spdx_id : undefined;
  const tp = (it.topics || []).filter((t) => !/^(mcp|mcp-server|model-context-protocol|ai|claude|llm)$/.test(t)).slice(0, 4);
  e.t = tp.length ? tp.join(',') : undefined;
}
async function searchTopics(token, searchEnd) {
  let last = 0, calls = 0;
  const call = async (q, page) => {
    const wait = last + 2100 * DELAY - Date.now(); if (wait > 0) await sleep(wait);
    last = Date.now(); calls++;
    for (let attempt = 0; attempt < 5; attempt++) {
      const r = await fetch(`${GHAPI}/search/repositories?q=${encodeURIComponent(q)}&per_page=100&page=${page}`, { headers: { Authorization: `Bearer ${token}`, Accept: 'application/vnd.github+json', 'User-Agent': UA, 'X-GitHub-Api-Version': '2022-11-28' }, signal: AbortSignal.timeout(30000) }).catch((e) => ({ ok: false, status: 0, e }));
      if (r.ok) return r.json();
      if (r.status === 403 || r.status === 429) { const reset = Number((r.headers && r.headers.get('x-ratelimit-reset')) || 0) * 1000; await sleep(Math.max(5000, Math.min(65000, reset - Date.now() + 1000))); continue; }
      if (r.status === 422) return { total_count: 0, items: [] };
      await sleep(3000);
    }
    throw new Error('search failed: ' + q);
  };
  const iso = (ms) => new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z');
  // Walks [from, to] in ascending order, splitting until every slice has at most 1000 results. Returns false when the
  // time budget ran out; onLeaf(to) is called after each finished slice so the sweep can resume from there.
  const walk = async (topic, group, field, from, to, onLeaf) => {
    if (Date.now() > searchEnd) return false;
    const q = `topic:${topic} ${field}:${iso(from)}..${iso(to)}`;
    const first = await call(q, 1);
    if (first.total_count > 1000 && to - from > 3600000) {
      const mid = from + Math.floor((to - from) / 2);
      if (!(await walk(topic, group, field, from, mid, onLeaf))) return false;
      return walk(topic, group, field, mid + 1000, to, onLeaf);
    }
    for (const it of first.items || []) ghTake(it, group);
    const pages = Math.min(10, Math.ceil(first.total_count / 100));
    for (let p = 2; p <= pages; p++) { const j = await call(q, p); for (const it of j.items || []) ghTake(it, group); }
    if (onLeaf) onLeaf(to);
    return true;
  };
  const all = [...TOPICS.map((t) => [t, 1]), ...MCP_TOPICS.map((t) => [t, 2])];
  const now = Date.now(), before = Object.keys(GH.items).length;
  // 1. repos pushed since the previous build (new repos and updates)
  if (GH.inc) {
    const since = Date.parse(GH.inc) - 6 * 3600000;
    for (const [t, g] of all) { try { await walk(t, g, 'pushed', since, now + 3600000, null); } catch (e) { log(`  topic ${t} (recent): ${e.message}`); } }
    log(`  recent pushes: ${calls} searches`);
  }
  GH.inc = new Date(now).toISOString();
  // 2. continue the full sweep (by creation date) where the last build stopped
  const START = Date.parse('2008-01-01T00:00:00Z');
  let cur = GH.sweep && all.some(([t]) => t === GH.sweep.topic) ? GH.sweep : { topic: all[0][0], from: START, cycle: today() };
  let guard = 0;
  while (Date.now() < searchEnd && guard++ < all.length + 1) {
    const ti = all.findIndex(([t]) => t === cur.topic), [t, g] = all[ti];
    const c0 = calls, n0 = Object.keys(GH.items).length;
    let ok = false;
    try { ok = await walk(t, g, 'created', cur.from || START, now + 86400000, (to) => { cur.from = to + 1000; }); } catch (e) { log(`  topic ${t}: ${e.message}`); }
    log(`  topic ${t}: ${ok ? 'swept' : 'paused'} (+${Object.keys(GH.items).length - n0} new, ${calls - c0} searches)`);
    if (!ok) break;
    if (ti + 1 < all.length) cur = { topic: all[ti + 1][0], from: START, cycle: cur.cycle };
    else { log(`  full sweep finished (started ${cur.cycle})`); cur = { topic: all[0][0], from: START, cycle: today() }; GH.lastCycle = today(); }
  }
  GH.sweep = cur;
  // 3. drop repos no sweep has seen for 45 days
  const cutoff = new Date(Date.now() - 45 * 86400000).toISOString().slice(0, 10);
  let dropped = 0;
  for (const [k, e] of Object.entries(GH.items)) if (!e.seen || e.seen < cutoff) { delete GH.items[k]; dropped++; }
  log(`GitHub topics: ${Object.keys(GH.items).length} repos (${Object.keys(GH.items).length - before + dropped} new, ${dropped} dropped) with ${calls} searches`);
}

// The previous build (published on the website): lets unchanged repos be reused instead of cloned again.
// A schema 3 build can also start from a schema 2 build (--prev2 <url>), e.g. the first time it runs.
async function loadPrevious() {
  const first = await loadPreviousFrom(opt('prev', LIVE), FORMAT === 'v3');
  if (first || FORMAT !== 'v3' || !opt('prev2')) return first;
  return loadPreviousFrom(opt('prev2'), false);
}
async function loadPreviousFrom(src, v3) {
  const read = (f) => readCatalogFile(src, f);
  const state = await read(v3 ? 'state.json.gz' : 'state.json');
  const man = state && await read('manifest.json');
  if (!state || !man || man.schema !== (v3 ? 3 : 2)) return null;
  const rowsById = new Map(), det = new Map();
  const shardFiles = (kind) => v3 ? Array.from({ length: (man.kinds[kind] || {}).shards || 0 }, (_, i) => `list/${kind}-${i}.json.gz`) : ((man.kinds[kind] || {}).shards || []);
  await pool(['skill', 'plugin'].flatMap(shardFiles), 16, async (f) => { const j = await read(f); for (const r of (j && j.rows) || []) rowsById.set(r[0], r); });
  const detFiles = v3 ? Array.from({ length: 4096 }, (_, i) => 'detail/' + hex(i, 3) + '.json.gz') : Array.from({ length: 256 }, (_, i) => 'detail/' + hex(i, 2) + '.json');
  await pool(detFiles, 16, async (f) => {
    const j = await read(f); if (!j) return;
    for (const [id, d] of Object.entries(j)) if (d.k === 'skill' || d.k === 'plugin') det.set(id, d);
  });
  log(`previous build (schema ${v3 ? 3 : 2}): ${Object.keys(state.repos || {}).length} repos, ${det.size} skills and plugins (${state.generated})`);
  return { state, rowsById, det };
}

async function importRepos() {
  fs.mkdirSync(CACHE, { recursive: true });
  const t0 = Date.now(), budgetEnd = BUDGET_END;
  const prio = new Map();   // lower = more trusted (official first)
  const meta = new Map();   // repo(lower) -> {repo, stars, pushed}
  const push = (r, p, m) => { const k = r.toLowerCase(); if (!prio.has(k)) { prio.set(k, p); meta.set(k, { repo: r, stars: 0, ...(m || {}) }); } else if (m) Object.assign(meta.get(k), m); };
  SOURCES.repos.forEach((r, i) => push(r, i));
  for (const l of SOURCES.skill_lists) {
    try {
      const md = await get(`https://raw.githubusercontent.com/${l.repo}/HEAD/${l.file}`, { json: false });
      if (md) { const found = reposFromMarkdown(md); found.forEach((r) => push(r, 1000)); log(`list ${l.repo}: ${found.length} links`); }
    } catch (e) { log(`list ${l.repo} failed: ${e.message}`); }
  }
  for (const e of Object.values(GH.items)) if (e.g & 1) push(e.r, 2000, { stars: e.s, pushed: e.p });
  for (const r of Object.values(GH.found || {})) push(r, 2600);   // repos with a SKILL.md from code search (--extra)
  const prev = argv.includes('--fresh') ? null : await loadPrevious();
  if (prev) {   // repos indexed before stay in the queue even if this build's sweep hasn't reached them yet
    const names = new Map();
    for (const d of prev.det.values()) if (d.repo) names.set(d.repo.toLowerCase(), d.repo);
    for (const k of Object.keys(prev.state.repos || {})) push(names.get(k) || k, 2500);
  }
  // order: trusted sources first, then by stars
  let queue = [...prio.keys()].sort((a, b) => (prio.get(a) - prio.get(b)) || ((meta.get(b).stars || 0) - (meta.get(a).stars || 0))).slice(0, MAX_REPOS);
  const stats = { ok: 0, failed: 0, empty: 0, reused: 0, skipped: 0, toobig: 0 };
  const results = new Map();   // repo(lower) -> { info, cands }
  const reuse = (k) => {
    const ps = prev && prev.state.repos[k];
    if (!ps) return false;
    const cands = [];
    for (const [key, id] of Object.entries(ps.items || {})) {
      const d = prev.det.get(id), r = prev.rowsById.get(id);
      if (!d || !r) return false;   // incomplete: index it again
      const st = meta.get(k).stars || d.stars || 0;
      cands.push({ kind: d.k, key, fp: d.fp, idBase: null, prevId: id, row: [null, ...r.slice(1, 7), st], det: { ...d, stars: st || undefined } });
    }
    results.set(k, { info: { c: ps.c, reused: true }, cands });
    stats.reused++;
    return true;
  };
  log(`git: ${queue.length} repositories (${CONC} at a time)…`);
  let done = 0;
  const work = async (k) => {
    const m = meta.get(k);
    const ps = prev && prev.state.repos[k];
    if (ps && m.pushed && prev.state.generated && m.pushed < prev.state.generated && reuse(k)) return;
    if (ps && !m.pushed) {   // not from search: ask git whether HEAD moved
      try { const head = (await run('git', ['ls-remote', `https://github.com/${m.repo}.git`, 'HEAD'], { timeout: 30000 })).split(/\s/)[0]; if (head && head === ps.c && reuse(k)) return; } catch {}
    }
    if (Date.now() > budgetEnd) { if (!reuse(k)) stats.skipped++; return; }
    const info = { stars: m.stars };
    const cands = await indexRepo(m.repo, stats, info);
    if (info.failed && ps && reuse(k)) return;
    results.set(k, { info, cands });
  };
  await pool(queue, CONC, async (k) => { await work(k); if (++done % 250 === 0) log(`  ${done}/${queue.length} repos · ${JSON.stringify(stats)} · ${Math.round((Date.now() - t0) / 1000)}s`); });
  const more = [...new Set(extraRepos.map((r) => r.toLowerCase()))].filter((k) => !prio.has(k)).slice(0, Math.max(0, MAX_REPOS - queue.length));
  if (more.length) {
    log(`git: ${more.length} more repos from marketplaces`);
    more.forEach((k) => { const r = extraRepos.find((x) => x.toLowerCase() === k); push(r, 1500); });
    await pool(more, CONC, work);
    queue = queue.concat(more);
  }
  // Resolve duplicates (same SKILL.md or same plugin files) and ids, most trusted repo first.
  const prevIds = (prev && prev.state.ids) || {};
  const seenFp = new Set();
  const repoState = {};
  const idsOut = {};
  let skills = 0, plugins = 0, dupes = 0;
  const order = [...results.keys()].sort((a, b) => (prio.get(a) - prio.get(b)) || ((meta.get(b).stars || 0) - (meta.get(a).stars || 0)) || a.localeCompare(b));
  // reserve previous ids first so items keep their id from build to build
  for (const k of order) for (const c of results.get(k).cands) { const pid = c.prevId || prevIds[c.key]; if (pid && !IDS.has(pid)) { IDS.add(pid); c.id = pid; } }
  for (const k of order) {
    const { info, cands } = results.get(k);
    const items = {};
    for (const c of cands) {
      const fpKey = c.kind + ':' + c.fp;
      if (c.fp && seenFp.has(fpKey)) { dupes++; if (c.id) IDS.delete(c.id); continue; }
      if (c.fp) seenFp.add(fpKey);
      const id = c.id || makeId(c.idBase || c.row[1], c.key);
      c.row[0] = id; c.det.fp = c.fp;
      if (!add(c.kind, c.row, c.det)) continue;
      items[c.key] = id; idsOut[c.key] = id;
      if (c.kind === 'skill') skills++; else plugins++;
    }
    if (info.c) repoState[k] = { c: info.c, items };
  }
  STATE.repos = repoState; STATE.ids = idsOut;
  log(`git: ${JSON.stringify(stats)} → ${skills} skills, ${plugins} plugins (${dupes} duplicates dropped) in ${Math.round((Date.now() - t0) / 1000)}s`);
  sourcesUsed.push({ kind: 'skill', name: 'Public GitHub repositories (Agent Skills)', url: 'https://agentskills.io', count: skills, repos: stats.ok + stats.reused });
  sourcesUsed.push({ kind: 'plugin', name: 'Public GitHub repositories (Claude Code plugins)', url: 'https://docs.claude.com/en/docs/claude-code/plugins', count: plugins });
  if (stats.ok + stats.reused === 0) throw new Error('no repository could be read');
}

// ------------------------------------------------------------------ AI tools: installable CLIs
async function importCli() {
  let n = 0;
  await pool(CLI.tools, 6, async (t) => {
    try {
      let version = '', bins = t.bins || [];
      if (t.registry === 'npm') {
        const j = await get(`https://registry.npmjs.org/${t.pkg.replace('/', '%2F')}`);
        if (!j) throw new Error('not on npm');
        version = j['dist-tags'] && j['dist-tags'].latest;
        const v = j.versions && j.versions[version];
        if (!v) throw new Error('no latest version');
        if (v.deprecated) throw new Error('deprecated');
        bins = typeof v.bin === 'string' ? [t.pkg.split('/').pop()] : Object.keys(v.bin || {});
        if (!bins.length) throw new Error('no command');
      } else {
        const j = await get(`https://pypi.org/pypi/${encodeURIComponent(t.pkg)}/json`);
        if (!j) throw new Error('not on PyPI');
        version = j.info.version;
      }
      if (!version) throw new Error('no version');
      IDS.add(t.id);
      const pin = t.registry === 'npm' ? `${t.pkg}@${version}` : `${t.pkg}==${version}`;
      add('tool', [t.id, t.name, clip(t.summary, 140), t.registry === 'npm' ? 'npm' : 'PyPI', 'c' + (t.registry === 'npm' ? 'n' : 'p'), (t.tags || []).join(','), version], {
        k: 'tool', n: t.name, d: clip(t.summary, 600), t: t.registry, pkg: pin, bins: bins.slice(0, 8), web: t.url, v: version,
      });
      n++;
    } catch (e) { log(`  cli ${t.id} skipped: ${e.message}`); }
  });
  sourcesUsed.push({ kind: 'tool', name: 'npm and PyPI (AI command-line tools)', url: 'https://www.npmjs.com', count: n });
}

// ------------------------------------------------------------------ AI tools: app directory (awesome lists)
function parseList(md, listRepo) {
  const out = [];
  let cat = '';
  const lines = md.split(/\r?\n/);
  let inCode = false;
  for (const line of lines) {
    if (/^\s*```/.test(line)) { inCode = !inCode; continue; }
    if (inCode) continue;
    const h = /^#{2,4}\s+(.+?)\s*#*$/.exec(line);
    if (h) { cat = h[1].replace(/\[([^\]]*)\]\([^)]*\)/g, '$1').replace(/[*_`:]/g, '').replace(/[^\p{L}\p{N}&/ ,.+-]/gu, '').trim().slice(0, 40); continue; }
    if (/^(contents|table of contents|contributing|license|footnotes|related|other awesome lists?|resources|community|sponsors?)$/i.test(cat)) continue;
    let m = /^\s*[-*+]\s+(?:\*\*)?\[([^\]]{1,80})\]\((https?:\/\/[^)\s]+)\)(?:\*\*)?\s*(?:[-–—:|]\s*)?(.*)$/.exec(line);
    if (!m) { const t = /^\s*\|\s*\[([^\]]{1,80})\]\((https?:\/\/[^)\s]+)\)\s*\|\s*([^|]*)\|/.exec(line); if (t) m = t; }
    if (!m) continue;
    const name = m[1].replace(/[*_`]/g, '').replace(/!\[[^\]]*\]\([^)]*\)/g, '').trim();
    let url = m[2].replace(/[.,;]+$/, '');
    if (!name || /^(back to top|top|↑|⬆)/i.test(name) || /shields\.io|badge|#/.test(url.slice(0, 40)) || /\.(png|jpg|gif|svg)$/i.test(url)) continue;
    if (/github\.com\/[^/]+\/[^/]+\/(blob|tree)\/.+\.(md|txt)/.test(url)) continue;
    let desc = (m[3] || '').replace(/!\[[^\]]*\]\([^)]*\)/g, '').replace(/\[([^\]]*)\]\([^)]*\)/g, '$1').replace(/<[^>]+>/g, '').replace(/[*_`]/g, '').trim();
    desc = desc.replace(/^[-–—:|]\s*/, '');
    out.push({ name: clip(name, 70), url, desc: clip(desc, 300), cat: clip(cat, 40), list: listRepo });
  }
  return out;
}
async function importApps() {
  const byUrl = new Map();
  let lists = 0;
  for (const l of SOURCES.app_lists) {
    try {
      const md = await get(`https://raw.githubusercontent.com/${l.repo}/HEAD/${l.file}`, { json: false });
      if (!md) { log(`apps ${l.repo}: not found`); continue; }
      const found = parseList(md, l.repo);
      lists++;
      for (const it of found) {
        let key; try { const u = new URL(it.url); key = (u.hostname.replace(/^www\./, '') + u.pathname.replace(/\/+$/, '')).toLowerCase(); } catch { continue; }
        if (!byUrl.has(key)) byUrl.set(key, it); else if (!byUrl.get(key).desc && it.desc) byUrl.get(key).desc = it.desc;
      }
      log(`apps ${l.repo}: ${found.length} entries`);
    } catch (e) { log(`apps ${l.repo} failed: ${e.message}`); }
  }
  let n = 0;
  for (const [key, it] of byUrl) {
    const host = (() => { try { return new URL(it.url).hostname.replace(/^www\./, ''); } catch { return ''; } })();
    const gh = /^github\.com\/([^/]+)\//.exec(key);
    const id = makeId(`${it.name}`, 'app:' + key);
    add('tool', [id, it.name, clip(it.desc, 140), gh ? gh[1] : host, 'l', it.cat, host], { k: 'tool', t: 'link', n: it.name, d: it.desc, url: it.url, cat: it.cat, src: it.list });
    n++;
  }
  sourcesUsed.push({ kind: 'tool', name: `Open "awesome" lists of AI tools (${lists} lists)`, url: 'https://github.com/sindresorhus/awesome', count: n });
  if (!n) throw new Error('no app list could be read');
}

// ------------------------------------------------------------------ MCP servers on npm
// npm's search API returns at most 10,000 results per query, so the "mcp" keyword is read through several narrower
// queries (keyword intersections). Only packages that install a command are listed (libraries such as SDKs are
// not servers); they run at the exact version with npx, like registry entries. Results are cached between builds so
// a package's metadata is fetched again only when its version changes.
const NPM_PKG = /^(?:@[a-z0-9][a-z0-9._-]*\/)?[a-z0-9][a-z0-9._-]*$/;
async function importNpm() {
  const queries = opt('npm-queries') ? opt('npm-queries').split('|') : SOURCES.npm_queries || [];
  const end = Math.min(BUDGET_END, Date.now() + Number(opt('npm-min', 25)) * 60000);
  let calls = 0, last = 0, seen = 0;
  const found = new Map();
  for (const q of queries) {
    if (Date.now() > end) { log('npm: time budget used, keeping the cached packages for the rest'); break; }
    let firstName = '', prevName = '';
    for (let from = 0; from < 10000; from += 250) {
      if (Date.now() > end) break;
      const wait = last + 1200 * DELAY - Date.now(); if (wait > 0) await sleep(wait);
      last = Date.now(); calls++;
      let j;
      try { j = await get(`${NPM}/-/v1/search?text=${encodeURIComponent(q)}&size=250&from=${from}`, { tries: 5 }); } catch (e) { log(`npm "${q}": ${e.message}`); break; }
      const objs = (j && j.objects) || [];
      if (!objs.length) break;
      const n0 = objs[0].package && objs[0].package.name;
      if (from === 0) firstName = n0; else if (n0 === firstName || n0 === prevName) break;   // past the result window (about 5,500): npm repeats a page
      prevName = n0;
      for (const o of objs) { const p = o.package || {}; if (p.name && NPM_PKG.test(p.name) && !found.has(p.name)) found.set(p.name, p); }
      if (objs.length < 250) break;
    }
  }
  log(`npm: ${found.size} packages from ${calls} searches`);
  // which of them install a command (cached per version)
  const todo = [...found.values()].filter((p) => !(GH.npm[p.name] && GH.npm[p.name].v === p.version));
  let fetched = 0;
  await pool(todo, 12, async (p) => {
    if (Date.now() > BUDGET_END) return;
    try {
      const doc = await get(`${NPM}/${p.name.replace('/', '%2F')}`, { headers: { Accept: 'application/vnd.npm.install-v1+json' }, tries: 3 });
      const v = doc && doc.versions && doc.versions[p.version];
      if (!v) return;
      const bin = typeof v.bin === 'string' ? 1 : Object.keys(v.bin || {}).length ? 1 : 0;
      GH.npm[p.name] = { v: p.version, b: bin, x: v.deprecated ? 1 : 0 };
      fetched++;
    } catch {}
  });
  for (const p of found.values()) {
    const e = GH.npm[p.name]; if (!e || e.v !== p.version) continue;
    seen++;
    e.d = clip(p.description || '', 200) || undefined;
    e.r = (p.links && p.links.repository) || undefined;
    e.w = (p.links && p.links.homepage) || undefined;
    e.u = (p.publisher && p.publisher.username) || undefined;
    e.k = (p.keywords || []).filter((k) => typeof k === 'string' && !/^(mcp|mcp-server|ai|model-context-protocol|modelcontextprotocol)$/i.test(k)).slice(0, 4).map((k) => clip(k, 24)).join(',') || undefined;
    e.seen = today();
  }
  const cutoff = new Date(Date.now() - 45 * 86400000).toISOString().slice(0, 10);
  for (const [k, e] of Object.entries(GH.npm)) if (!e.seen || e.seen < cutoff) delete GH.npm[k];
  let n = 0;
  for (const name of Object.keys(GH.npm).sort()) {
    const e = GH.npm[name];
    if (!e.b || REG_PKGS.has('npm:' + name.toLowerCase())) continue;
    const pkg = `${name}@${e.v}`;
    if (!/^(?:@[a-z0-9][a-z0-9._-]*\/)?[a-z0-9][a-z0-9._-]*@\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(pkg)) continue;
    const scope = name.startsWith('@') ? name.slice(1, name.indexOf('/')) : '';
    const short = name.split('/').pop();
    const id = makeId(scope && !short.includes(scope) ? `${scope}-${short}` : short, 'npm:' + name);
    const repo = /^https:\/\/[^\s"'<>]+$/.test(e.r || '') ? e.r : undefined;
    if (add('mcp', [id, clip(short, 70), clip(e.d || 'MCP server on npm', 140), e.u || scope || 'npm', 'n' + (e.x ? 'x' : ''), e.k || '', e.v], {
      k: 'mcp', n: clip(name, 90), d: clip(e.d || '', 600), v: e.v, npm: name, web: (/^https:\/\//.test(e.w || '') && e.w) || repo || `https://www.npmjs.com/package/${name}`, repo, src: 'npm',
      st: e.x ? 'deprecated' : undefined, i: { t: 'stdio', cmd: 'npx', args: ['-y', pkg], env: [] },
    })) { n++; if (ghKey(repo)) COVERED.add(ghKey(repo)); }
  }
  log(`npm: ${n} MCP servers that install a command (${fetched} package versions checked)`);
  sourcesUsed.push({ kind: 'mcp', name: 'npm (packages tagged mcp that install a command)', url: 'https://www.npmjs.com/search?q=keywords:mcp', count: n });
}

// ------------------------------------------------------------------ MCP servers as Hugging Face Spaces
// Gradio Spaces started with mcp_server=True are tagged "mcp-server" and serve MCP over SSE at a fixed path.
async function importHf() {
  let url = `${HF}/api/spaces?filter=mcp-server&limit=1000`, pages = 0, n = 0;
  const seenIds = new Set();
  while (url && pages < 200) {
    const meta = {};
    const j = await get(url, { meta, timeout: 60000 });
    pages++;
    for (const sp of j || []) {
      if (!sp || !sp.id || sp.private || seenIds.has(sp.id)) continue;
      seenIds.add(sp.id);
      const [owner, nm] = sp.id.split('/');
      const card = sp.cardData || {};
      const title = clip(card.title || nm, 70), desc = clip(card.short_description || '', 300);
      const sub = String(sp.subdomain || sp.id.replace('/', '-').replace(/[_.]+/g, '-').toLowerCase());
      const gradio = sp.sdk === 'gradio' && /^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$/.test(sub) && (sp.subdomain || sub.length <= 56);   // longer names get a hashed subdomain
      const id = makeId(`${nm}-${owner}`, 'hf:' + sp.id);
      const tags = (sp.tags || []).filter((t) => !/^(gradio|mcp-server|region:|docker|static|streamlit)/.test(t)).slice(0, 4).join(',');
      if (add('mcp', [id, title, desc || `Hugging Face Space by ${owner}`, owner, gradio ? 'e' : 'l', tags, 'Space', sp.likes || 0], {
        k: 'mcp', n: title, d: desc, web: `https://huggingface.co/spaces/${sp.id}`, src: 'hf', lic: card.license || undefined,
        i: gradio ? { t: 'sse', url: `https://${sub}.hf.space/gradio_api/mcp/sse`, headers: [] } : undefined,
        note: gradio ? 'Free Spaces sleep when unused and can take a minute to wake up; some need a Hugging Face token or have usage limits.' : undefined,
      })) n++;
    }
    const link = meta.headers && meta.headers.get('link');
    const m = link && /<([^>]+)>;\s*rel="next"/.exec(link);
    url = m ? m[1] : '';
    if (url && !url.startsWith(HF)) url = '';
  }
  log(`Hugging Face: ${n} MCP Spaces from ${pages} pages`);
  sourcesUsed.push({ kind: 'mcp', name: 'Hugging Face Spaces tagged mcp-server', url: 'https://huggingface.co/spaces?filter=mcp-server', count: n });
}

// ------------------------------------------------------------------ Docker's MCP catalog (docker/mcp-registry, MIT)
// Docker Hub's API no longer pages anonymously, so the catalog is read from its source repo with a shallow git clone.
async function importDocker() {
  const repo = SOURCES.docker_mcp_repo || 'docker/mcp-registry';
  const dir = path.join(CACHE, 'docker_mcp_registry');
  fs.mkdirSync(CACHE, { recursive: true });
  fs.rmSync(dir, { recursive: true, force: true });
  let n = 0;
  try {
    await run('git', ['clone', '--quiet', '--depth', '1', '--filter=blob:none', '--no-checkout', '--single-branch', (process.env.ASTRO_IDX_GIT || 'https://github.com/') + repo + '.git', dir], { timeout: 120000 });
    const tree = (await run('git', ['ls-tree', '-r', 'HEAD', 'servers'], { cwd: dir })).split('\n').filter(Boolean).map((l) => { const t = l.indexOf('\t'); return { sha: l.slice(0, t).split(/\s+/)[2], path: l.slice(t + 1) }; }).filter((e) => /^servers\/[a-z0-9][a-z0-9._-]*\/server\.yaml$/.test(e.path));
    const raw = await run('git', ['cat-file', '--batch'], { cwd: dir, input: tree.map((e) => e.sha).join('\n') + '\n', encoding: 'buffer', timeout: 300000 });
    const blobs = new Map(); let off = 0;
    while (off < raw.length) { const nl = raw.indexOf(10, off); if (nl < 0) break; const h = raw.slice(off, nl).toString().split(' '); if (h[1] === 'missing') { off = nl + 1; continue; } const sz = Number(h[2]); blobs.set(h[0], raw.slice(nl + 1, nl + 1 + sz).toString('utf8')); off = nl + 1 + sz + 1; }
    for (const e of tree) {
      const y = blobs.get(e.sha) || '';
      // the few fields we need from a flat YAML file (no YAML library: top-level and one level of nesting)
      const top = (k) => { const m = new RegExp('^' + k + ':\\s*(.+)$', 'm').exec(y); return m ? m[1].trim().replace(/^(['"])(.*)\1$/, '$2') : ''; };
      const sub = (sec, k) => { const m = new RegExp('^' + sec + ':\\s*\\n((?:[ \\t]+.*\\n?)*)', 'm').exec(y); if (!m) return ''; const mm = new RegExp('^[ \\t]+' + k + ':\\s*(.+)$', 'm').exec(m[1]); return mm ? mm[1].trim().replace(/^(['"])(.*)\1$/, '$2') : ''; };
      const name = top('name') || e.path.split('/')[1];
      const image = top('image');
      if (!/^[a-z0-9][a-z0-9._-]*$/.test(name) || top('type') === 'poci') continue;
      if (image && REG_PKGS.has('oci:' + image.toLowerCase())) continue;
      const title = sub('about', 'title') || name, desc = sub('about', 'description');
      const project = sub('source', 'project');
      const remote = !image;   // remote servers have no image
      const id = makeId(`${name}-docker`, 'docker:' + name);
      const web = image ? `https://hub.docker.com/r/${image}` : `https://github.com/${repo}/tree/main/servers/${name}`;
      if (add('mcp', [id, clip(title, 70), clip(desc || 'MCP server in Docker\'s MCP catalog', 140), 'Docker MCP catalog', 'l', clip(sub('meta', 'category'), 24), image || 'remote'], {
        k: 'mcp', n: clip(title, 90), d: clip(desc, 600), web, repo: /^https:\/\/github\.com\//.test(project) ? project : undefined, src: 'docker', img: image || undefined,
        note: remote ? undefined : 'Runs with Docker (docker run -i --rm ' + image + '); see its page for the settings it needs.',
      })) { n++; if (ghKey(project)) COVERED.add(ghKey(project)); }
    }
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
  if (!n) throw new Error('no servers read from ' + repo);
  log(`Docker MCP catalog: ${n} servers`);
  sourcesUsed.push({ kind: 'mcp', name: "Docker's MCP catalog (docker/mcp-registry)", url: 'https://hub.docker.com/mcp', count: n });
}

// ------------------------------------------------------------------ Open VSX editor extensions
// The search API stops at offset 10,000, so it is read in both date orders and per category (robots.txt asks for
// 2 seconds between requests).
async function importOpenVsx() {
  const byKey = new Map();
  const cats = SOURCES.openvsx_categories || [];
  const runs = [['', 'asc'], ['', 'desc'], ...cats.flatMap((c) => [[c, 'asc'], [c, 'desc']])];
  let calls = 0, total = 0;
  for (const [cat, order] of runs) {
    if (Date.now() > BUDGET_END) break;
    let catTotal = Infinity;
    for (let off = 0; off < 10000 && off < catTotal; off += 1000) {
      await pause(2000); calls++;
      const j = await get(`${OVSX}/api/-/search?size=1000&offset=${off}&sortBy=timestamp&sortOrder=${order}${cat ? '&category=' + encodeURIComponent(cat) : ''}`, { timeout: 60000 });
      const ex = (j && j.extensions) || [];
      catTotal = (j && j.totalSize) || 0; if (!cat) total = Math.max(total, catTotal);
      for (const e of ex) if (e.namespace && e.name) byKey.set(`${e.namespace}.${e.name}`.toLowerCase(), e);
      if (ex.length < 1000) break;
    }
    if (cat && catTotal <= 10000 && order === 'asc') runs.splice(runs.findIndex(([c, o]) => c === cat && o === 'desc'), 1);   // one direction was enough
  }
  let n = 0;
  for (const [key, e] of [...byKey.entries()].sort()) {
    if (e.deprecated) continue;
    const name = clip(e.displayName || e.name, 70);
    const id = makeId(`${e.name}-${e.namespace}-vsx`, 'ovsx:' + key);
    if (add('plugin', [id, name, clip(e.description || '', 140), clip(e.namespace, 40), 'lv', 'open-vsx,editor', e.version || '', 0], {
      k: 'plugin', t: 'vsx', n: name, d: clip(e.description || '', 600), v: e.version || undefined, vsx: `${e.namespace}.${e.name}`,
      web: `https://open-vsx.org/extension/${encodeURIComponent(e.namespace)}/${encodeURIComponent(e.name)}`, src: 'open-vsx', verified: e.verified ? 1 : undefined,
    })) n++;
  }
  log(`Open VSX: ${n} extensions (${byKey.size} read of ${total}, ${calls} requests)`);
  sourcesUsed.push({ kind: 'plugin', name: 'Open VSX editor extensions (themes, languages, snippets and more; install them in the Code tool)', url: 'https://open-vsx.org', count: n });
}

// ------------------------------------------------------------------ MCP projects tagged on GitHub
// Repos with an MCP topic that no richer source already lists. These are links: read the repo to set them up.
function importGhMcp() {
  let n = 0;
  for (const e of Object.values(GH.items)) {
    if (!(e.g & 2) || !e.r) continue;
    const k = e.r.toLowerCase();
    if (COVERED.has(k)) continue;
    if (!e.d && !(e.s > 0)) continue;   // no description and no stars: usually an empty experiment
    const [owner, name] = e.r.split('/');
    const id = makeId(name.toLowerCase().includes(owner.toLowerCase()) ? name : `${name}-${owner}`, 'ghmcp:' + k);
    if (add('mcp', [id, clip(name, 70), clip(e.d || 'MCP project on GitHub', 140), owner, 'l', e.t || '', 'GitHub', e.s || 0], {
      k: 'mcp', n: clip(e.r, 90), d: clip(e.d || '', 600), web: e.h || `https://github.com/${e.r}`, repo: `https://github.com/${e.r}`, lic: e.l, src: 'github', stars: e.s || undefined,
    })) n++;
  }
  log(`GitHub: ${n} more MCP projects from topics`);
  sourcesUsed.push({ kind: 'mcp', name: 'GitHub repositories with an MCP topic (links)', url: 'https://github.com/topics/mcp-server', count: n });
}

// Repositories that contain a SKILL.md, found with GitHub code search in file-size windows. The cursor is saved, so
// every build continues where the last one stopped.
let codeNext = 0;
async function ghCode(token, q, page) {   // code search allows 10 requests a minute
  const at = Math.max(Date.now(), codeNext); codeNext = at + 6600 * DELAY;
  if (at > Date.now()) await sleep(at - Date.now());
  for (let attempt = 0; attempt < 6; attempt++) {
    const r = await fetch(`${GHAPI}/search/code?q=${encodeURIComponent(q)}&per_page=100&page=${page}`, { headers: { Authorization: `Bearer ${token}`, Accept: 'application/vnd.github+json', 'User-Agent': UA, 'X-GitHub-Api-Version': '2022-11-28' }, signal: AbortSignal.timeout(30000) }).catch(() => ({ ok: false, status: 0, headers: null }));
    if (r.ok) return r.json();
    if (r.status === 403 || r.status === 429) {
      const reset = Number((r.headers && r.headers.get('x-ratelimit-reset')) || 0) * 1000;
      const wait = Math.max(10000, Math.min(70000, reset - Date.now() + 1000)) * DELAY;
      codeNext = Date.now() + wait; await sleep(wait); continue;
    }
    if (r.status === 422) return { total_count: 0, items: [] };
    await sleep(3000 * (attempt + 1) * DELAY);
  }
  throw new Error('code search failed: ' + q);
}
async function searchCodeSkills(token, budgetEnd) {
  const prev = GH.code || {};
  const cur = { s: prev.s || 0, w: prev.w || 64, pass: prev.pass || 0 };
  const repos = GH.found || (GH.found = {});
  let calls = 0; const before = Object.keys(repos).length;
  const MAX = 200000;
  while (Date.now() < budgetEnd) {
    const q = `filename:SKILL.md size:${cur.s}..${cur.s + cur.w - 1}`;
    let first;
    try { first = await ghCode(token, q, 1); calls++; } catch (e) { log(`  code search: ${e.message}`); break; }
    if (first.total_count > 1000 && cur.w > 1) { cur.w = Math.max(1, Math.floor(cur.w / 2)); continue; }
    const take = (j) => { for (const it of j.items || []) { const r = it.repository; if (r && !r.fork && !r.private && r.full_name && safeText(r.full_name, r.description)) repos[r.full_name.toLowerCase()] = r.full_name; } };
    take(first);
    const pages = Math.min(10, Math.ceil(first.total_count / 100));
    for (let p = 2; p <= pages && Date.now() < budgetEnd; p++) { try { take(await ghCode(token, q, p)); calls++; } catch { break; } }
    cur.s += cur.w;
    if (first.total_count < 300) cur.w = Math.min(4096, cur.w * 2);
    if (cur.s > MAX) { cur.s = 0; cur.w = 64; cur.pass++; break; }   // one full pass per build at most
  }
  GH.code = cur;
  log(`code search: ${calls} searches, ${Object.keys(repos).length - before} new repos with a SKILL.md (${Object.keys(repos).length} known), next size ${cur.s}`);
}

// ------------------------------------------------------------------ schema 3 (gzip files, search index)
const zlib = require('zlib');
const LIB3 = require(path.join(__dirname, '..', '..', 'web', 'lib3core.js'));
const SHARD3 = Math.max(1, Number(opt('shard3', 2000)));
async function getBuf(url, tries = 4) {
  let last;
  for (let i = 0; i < tries; i++) {
    try {
      const r = await fetch(url, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(60000) });
      if (r.status === 404) return null;
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return Buffer.from(await r.arrayBuffer());
    } catch (e) { last = e; await sleep(800 * (i + 1)); }
  }
  throw new Error(`${url}: ${last && last.message}`);
}
// Reads a catalog file from a URL or a directory; .gz files are unpacked.
async function readCatalogFile(src, f) {
  let buf;
  if (/^https?:/.test(src)) buf = await getBuf(src + f, 2).catch(() => null);
  else { const p = path.join(src, f); buf = fs.existsSync(p) ? fs.readFileSync(p) : null; }
  if (!buf) return null;
  try { return JSON.parse((f.endsWith('.gz') ? zlib.gunzipSync(buf) : buf).toString('utf8')); } catch { return null; }
}
const hex = (i, w) => i.toString(16).padStart(w, '0');
function writeV3() {
  fs.rmSync(OUT, { recursive: true, force: true });
  for (const d of ['list', 'detail', 'q']) fs.mkdirSync(path.join(OUT, d), { recursive: true });
  const bytes = { list: 0, detail: 0, q: 0, state: 0 };
  const gz = (f, obj, part) => { const b = zlib.gzipSync(JSON.stringify(obj), { level: 9 }); fs.writeFileSync(path.join(OUT, f), b); bytes[part] += b.length; };
  const kinds = {}; let start = 0;
  const post = new Map();
  for (const kind of LIB3.KINDS) {
    const list = rows[kind];
    sortRows(list);
    const shards = Math.max(1, Math.ceil(list.length / SHARD3));
    for (let i = 0; i < shards; i++) gz(`list/${kind}-${i}.json.gz`, { kind, rows: list.slice(i * SHARD3, (i + 1) * SHARD3) }, 'list');
    list.forEach((row, i) => { for (const t of LIB3.rowTokens(row)) { let a = post.get(t); if (!a) post.set(t, (a = [])); a.push(start + i); } });
    kinds[kind] = { count: list.length, start, shards };
    start += list.length;
  }
  const byPrefix = new Map();
  for (const [t, a] of post) { const p = t.slice(0, 2); if (!byPrefix.has(p)) byPrefix.set(p, {}); byPrefix.get(p)[t] = LIB3.encodePostings(a); }
  post.clear();
  const prefixes = [...byPrefix.keys()].sort();
  for (const p of prefixes) gz(`q/${p}.json.gz`, byPrefix.get(p), 'q');
  const buckets = Array.from({ length: 4096 }, () => ({}));
  for (const [id, d] of details) { if (d.d && d.d.length > 300) d.d = clip(d.d, 300); buckets[parseInt(sha256(id).slice(0, 3), 16)][id] = d; }
  buckets.forEach((b, i) => gz(`detail/${hex(i, 3)}.json.gz`, b, 'detail'));
  const man = {
    schema: 3, generated: new Date().toISOString(), total: start, shard: SHARD3, order: LIB3.KINDS, kinds, sources: sourcesUsed,
    row: ['id', 'name', 'summary', 'publisher', 'flags', 'tags', 'extra', 'stars'], flags: FLAGS,
    detail: 'detail/{hhh}.json.gz where hhh is the first three hex characters of sha256(id)',
    search: { files: 'q/{xx}.json.gz (xx = first two characters of a word)', tokenizer: 'web/lib3core.js', prefixes },
    filtered: blocked,
  };
  gz('state.json.gz', { generated: man.generated, repos: STATE.repos, ids: STATE.ids }, 'state');
  gz('state-gh.json.gz', { schema: 1, generated: man.generated, inc: GH.inc, sweep: GH.sweep, lastCycle: GH.lastCycle, items: GH.items, npm: GH.npm, code: GH.code, found: GH.found }, 'state');
  man.bytes = bytes;
  fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(man, null, 1));
  const total = Object.values(bytes).reduce((s, b) => s + b, 0);
  log(`wrote ${start} items to ${OUT} (${Object.entries(kinds).map(([k, v]) => `${k} ${v.count}`).join(', ')}; ${(total / 1e6).toFixed(1)} MB: ${Object.entries(bytes).map(([k, v]) => `${k} ${(v / 1e6).toFixed(1)}`).join(', ')}; ${blocked} unsafe entries left out)`);
  if (total > 980e6) throw new Error('the catalog is larger than a GitHub Pages site may be (1 GB)');
}
async function reusePublishedV3(reason, stale = true) {
  log(`reusing the published catalog (${reason})`);
  const man = await get(LIVE + 'manifest.json');
  if (!man || man.schema !== 3) throw new Error('no published schema 3 catalog to fall back to');
  fs.rmSync(OUT, { recursive: true, force: true });
  for (const d of ['list', 'detail', 'q']) fs.mkdirSync(path.join(OUT, d), { recursive: true });
  const files = [];
  for (const [k, v] of Object.entries(man.kinds)) for (let i = 0; i < v.shards; i++) files.push(`list/${k}-${i}.json.gz`);
  for (let i = 0; i < 4096; i++) files.push(`detail/${hex(i, 3)}.json.gz`);
  for (const p of (man.search && man.search.prefixes) || []) files.push(`q/${p}.json.gz`);
  let missing = 0;
  await pool(files.concat('state.json.gz', 'state-gh.json.gz'), 16, async (f) => { const b = await getBuf(LIVE + f); if (b) fs.writeFileSync(path.join(OUT, f), b); else if (!/^state/.test(f)) missing++; });
  if (missing) throw new Error(`${missing} published files are missing`);
  if (stale) man.stale = reason;
  fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(man, null, 1));
}

// ------------------------------------------------------------------ fallback: reuse the last published catalog
async function reusePublished(reason, stale = true) {
  if (FORMAT === 'v3') return reusePublishedV3(reason, stale);
  log(`reusing the published catalog (${reason})`);
  const man = await get(LIVE + 'manifest.json');
  if (!man || man.schema !== 2) throw new Error('no published catalog to fall back to');
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(path.join(OUT, 'list'), { recursive: true }); fs.mkdirSync(path.join(OUT, 'detail'), { recursive: true });
  const files = [...Object.values(man.kinds).flatMap((k) => k.shards), ...Array.from({ length: 256 }, (_, i) => 'detail/' + i.toString(16).padStart(2, '0') + '.json')];
  files.push('state.json', 'state-gh.json');
  let missing = 0;
  await pool(files, 8, async (f) => { const t = await get(LIVE + f, { json: false, timeout: 120000 }).catch((e) => { if (/^state/.test(f)) return null; throw e; }); if (t != null) fs.writeFileSync(path.join(OUT, f), t); else if (!/^state/.test(f)) missing++; });
  if (missing) throw new Error(`${missing} published files are missing`);
  if (stale) man.stale = reason;
  fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(man));
}

// ------------------------------------------------------------------ write
const nameKey = (s) => String(s || '').replace(/^[^\p{L}\p{N}]+/u, '') || String(s || '');
// reviewed first, then active before deprecated, installable before link-only, then by stars and name
const sortRows = (list) => list.sort((a, b) => (b[4].includes('r') - a[4].includes('r')) || (a[4].includes('x') - b[4].includes('x')) || (a[4].includes('l') - b[4].includes('l')) || ((b[7] || 0) - (a[7] || 0)) || nameKey(a[1]).localeCompare(nameKey(b[1]), 'en', { sensitivity: 'base' }));
const FLAGS = { r: 'reviewed by Astro AI', h: 'remote MCP (https)', e: 'remote MCP (SSE)', n: 'npm package', p: 'PyPI package', d: 'Docker image', g: 'files from GitHub (pinned commit)', c: 'includes scripts or installs a command', l: 'link only', v: 'Open VSX editor extension (installs in the Code tool)', s: 'needs a key or token', x: 'deprecated' };
function write() {
  if (FORMAT === 'v3') return writeV3();
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(path.join(OUT, 'list'), { recursive: true }); fs.mkdirSync(path.join(OUT, 'detail'), { recursive: true });
  const kinds = {};
  for (const [kind, list] of Object.entries(rows)) {
    sortRows(list);
    const shards = [];
    for (let i = 0; i * SHARD < list.length || i === 0; i++) {
      const f = `list/${kind}-${i}.json`;
      fs.writeFileSync(path.join(OUT, f), JSON.stringify({ kind, rows: list.slice(i * SHARD, (i + 1) * SHARD) }));
      shards.push(f);
      if ((i + 1) * SHARD >= list.length) break;
    }
    kinds[kind] = { count: list.length, shards };
  }
  const buckets = Array.from({ length: 256 }, () => ({}));
  for (const [id, d] of details) buckets[parseInt(sha256(id).slice(0, 2), 16)][id] = d;
  let bytes = 0;
  buckets.forEach((b, i) => { const t = JSON.stringify(b); bytes += t.length; fs.writeFileSync(path.join(OUT, 'detail', i.toString(16).padStart(2, '0') + '.json'), t); });
  const total = Object.values(kinds).reduce((s, k) => s + k.count, 0);
  const man = {
    schema: 2, generated: new Date().toISOString(), total, kinds, sources: sourcesUsed,
    row: ['id', 'name', 'summary', 'publisher', 'flags', 'tags', 'extra', 'stars'],
    flags: FLAGS,
    filtered: blocked,
    detail: 'detail/{hh}.json where hh is the first two hex characters of sha256(id)',
  };
  fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(man, null, 1));
  fs.writeFileSync(path.join(OUT, 'state.json'), JSON.stringify({ generated: man.generated, repos: STATE.repos, ids: STATE.ids }));
  fs.writeFileSync(path.join(OUT, 'state-gh.json'), JSON.stringify({ schema: 1, generated: man.generated, inc: GH.inc, sweep: GH.sweep, lastCycle: GH.lastCycle, items: GH.items, npm: GH.npm, code: GH.code, found: GH.found }));
  log(`wrote ${total} items to ${OUT} (${Object.entries(kinds).map(([k, v]) => `${k} ${v.count}`).join(', ')}; details ${(bytes / 1e6).toFixed(1)} MB; ${blocked} unsafe entries left out)`);
}

(async () => {
  const t0 = Date.now();
  if (opt('mode') === 'reuse') {   // normal pushes: publish the last full build instead of indexing again (that runs daily)
    try { await reusePublished('reuse', false); log('reused the published catalog'); return; } catch (e) { log(`nothing to reuse (${e.message}); building it now`); }
  }
  addReviewed(opt('reviewed', ''));
  if (opt('fixture')) {   // tests: rows and details from a JSON file instead of the network
    const fx = JSON.parse(fs.readFileSync(opt('fixture'), 'utf8'));
    for (const [kind, list] of Object.entries(fx.rows || {})) for (const row of list) { IDS.add(row[0]); add(kind, row, (fx.details || {})[row[0]] || { k: kind }); }
    sourcesUsed.push({ kind: 'mcp', name: 'test fixture', url: 'https://example.com', count: Object.values(fx.rows || {}).reduce((n, l) => n + l.length, 0) });
    write(); return;
  }
  await loadGhState();
  const token = process.env.GITHUB_TOKEN || process.env.GH_TOKEN || (opt('token-file') ? fs.readFileSync(opt('token-file'), 'utf8').trim() : '');
  const search = async () => {
    if (!token || argv.includes('--no-search')) { log('GitHub topic search skipped (no token); using the saved results'); return; }
    const mins = Math.min(Number(opt('search-min', 90)), Math.max(5, (BUDGET_END - Date.now()) / 60000 / 2));
    log(`GitHub topic search (up to ${Math.round(mins)} min)…`);
    const end = Date.now() + mins * 60000;
    await Promise.all([searchTopics(token, end), EXTRA ? searchCodeSkills(token, end).catch((e) => log('code search failed: ' + e.message)) : null]);
  };
  const jobs = [];
  if (ONLY.has('tools')) jobs.push(['tools', importCli]);
  if (ONLY.has('mcp')) jobs.push(['mcp', importMcp]);
  if (ONLY.has('npm')) jobs.push(['npm', importNpm]);
  if (ONLY.has('hf')) jobs.push(['hf', importHf]);
  if (ONLY.has('docker')) jobs.push(['docker', importDocker]);
  if (ONLY.has('openvsx')) jobs.push(['openvsx', importOpenVsx]);
  if (ONLY.has('apps')) jobs.push(['apps', importApps]);
  if (ONLY.has('skills') || ONLY.has('ghmcp')) jobs.push(['search', search]);
  if (ONLY.has('skills')) jobs.push(['skills', importRepos]);
  if (ONLY.has('ghmcp')) jobs.push(['ghmcp', importGhMcp]);
  const failed = [];
  for (const [name, fn] of jobs) { try { await fn(); } catch (e) { failed.push(name); log(`${name} failed: ${e.message}`); } }
  if (failed.length && failed.length === jobs.length) { await reusePublished(`all sources failed: ${failed.join(', ')}`); return; }
  if (failed.includes('mcp') || failed.includes('skills')) {
    try { await reusePublished(`${failed.join(', ')} unavailable`); return; } catch (e) { log(`fallback failed (${e.message}); publishing what was indexed`); }
  }
  write();
  log(`done in ${Math.round((Date.now() - t0) / 1000)}s`);
})().catch((e) => { console.error(e); process.exit(1); });

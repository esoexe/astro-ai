#!/usr/bin/env node
// Builds the Astro AI Library into a static folder that the website serves and Astro AI Desktop installs from.
//   node library/build_library.js <out-dir>        (the website build calls this with dist/library)
//   node library/build_library.js --check          (validate only, used by CI)
//
// Output:
//   <out>/index.json                 every item, with the SHA-256 and size of each file it installs
//   <out>/files/<kind>s/<id>/...      the files themselves (skills and plugins)
//   <out>/files/<kind>s/<id>.zip      the same files as one download (for other tools or manual installs)
//
// Safety rules enforced here AND again by the desktop installer (astro_library.inc):
//   * skills and plugins are text only (.md, .json, .txt); no scripts, no hooks, no styles
//   * a plugin's .mcp.json may only list remote https servers
//   * MCP packages are pinned to an exact version and started with `npx -y <pkg>@<x.y.z>`
//   * no file may look like it contains a secret
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto'), zlib = require('zlib');

const SRC = __dirname;
const ID_RE = /^[a-z0-9][a-z0-9-]{0,47}$/;
const FILE_RE = /^(?:[A-Za-z0-9_][A-Za-z0-9_.-]*\/)*[A-Za-z0-9_][A-Za-z0-9_.-]*\.(?:md|json|txt)$/;
const DOT_OK = new Set(['.claude-plugin/plugin.json', '.mcp.json']);
const MAX_FILE = 256 * 1024, MAX_ITEM = 2 * 1024 * 1024, MAX_FILES = 80;
const PKG_RE = /^(?:@[a-z0-9][a-z0-9._-]*\/)?[a-z0-9][a-z0-9._-]*@\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;
const PERMS = new Set(['network', 'files', 'browser', 'account']);
const SECRET_RE = [
  /\bsk-[A-Za-z0-9_-]{20,}/, /\bsk-ant-[A-Za-z0-9_-]{20,}/, /\bgh[pousr]_[A-Za-z0-9]{30,}/, /\bgithub_pat_[A-Za-z0-9_]{30,}/,
  /\bgsk_[A-Za-z0-9]{30,}/, /\bAIza[0-9A-Za-z_-]{30,}/, /\bxox[abpr]-[A-Za-z0-9-]{10,}/, /\bhf_[A-Za-z0-9]{30,}/,
  /\bnvapi-[A-Za-z0-9_-]{30,}/, /\bsb_secret_[A-Za-z0-9_-]{20,}/, /-----BEGIN [A-Z ]*PRIVATE KEY-----/, /\beyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\./,
];

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');
function walk(dir, base = dir) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = path.join(dir, e.name);
    if (e.isSymbolicLink()) throw new Error(`symlinks are not allowed: ${p}`);
    return e.isDirectory() ? walk(p, base) : [path.relative(base, p).split(path.sep).join('/')];
  }).sort();
}
function frontmatter(text) {
  const m = /^---\r?\n([\s\S]*?)\r?\n---/.exec(text);
  if (!m) return null;
  const o = {};
  for (const line of m[1].split(/\r?\n/)) { const i = line.indexOf(':'); if (i > 0) o[line.slice(0, i).trim()] = line.slice(i + 1).trim(); }
  return o;
}

// ---------- tiny zip writer (deflate) ----------
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc32 = (b) => { let c = 0xffffffff; for (const x of b) c = CRC[(c ^ x) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function zip(entries) {   // entries: [{ name, data }]
  const parts = [], central = []; let off = 0;
  const dosTime = (0 << 11) | (0 << 5), dosDate = ((2026 - 1980) << 9) | (1 << 5) | 1;   // fixed date: reproducible zips
  for (const { name, data } of entries) {
    const nb = Buffer.from(name, 'utf8'), comp = zlib.deflateRawSync(data, { level: 9 }), crc = crc32(data);
    const lh = Buffer.alloc(30); lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(20, 4); lh.writeUInt16LE(0x0800, 6); lh.writeUInt16LE(8, 8);
    lh.writeUInt16LE(dosTime, 10); lh.writeUInt16LE(dosDate, 12); lh.writeUInt32LE(crc, 14); lh.writeUInt32LE(comp.length, 18); lh.writeUInt32LE(data.length, 22); lh.writeUInt16LE(nb.length, 26);
    parts.push(lh, nb, comp);
    const ch = Buffer.alloc(46); ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(20, 4); ch.writeUInt16LE(20, 6); ch.writeUInt16LE(0x0800, 8); ch.writeUInt16LE(8, 10);
    ch.writeUInt16LE(dosTime, 12); ch.writeUInt16LE(dosDate, 14); ch.writeUInt32LE(crc, 16); ch.writeUInt32LE(comp.length, 20); ch.writeUInt32LE(data.length, 24); ch.writeUInt16LE(nb.length, 28); ch.writeUInt32LE(off, 42);
    central.push(ch, nb);
    off += lh.length + nb.length + comp.length;
  }
  const cd = Buffer.concat(central), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10); end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(off, 16);
  return Buffer.concat([...parts, cd, end]);
}

// ---------- validation ----------
function checkText(rel, buf, errs) {
  if (buf.length > MAX_FILE) errs.push(`${rel}: larger than ${MAX_FILE} bytes`);
  if (buf.includes(0)) errs.push(`${rel}: binary content`);
  const t = buf.toString('utf8');
  for (const re of SECRET_RE) if (re.test(t)) errs.push(`${rel}: looks like it contains a secret (${re.source.slice(0, 18)}…)`);
  return t;
}
function checkMcpSpec(where, m, errs) {
  if (!m || typeof m !== 'object') return errs.push(`${where}: missing mcp spec`);
  if (m.type === 'http') {
    if (!/^https:\/\/[A-Za-z0-9.-]+(?::\d+)?\/[^\s"'<>]*$/.test(m.url || '')) errs.push(`${where}: http servers need an https URL`);
  } else if (m.type === 'stdio') {
    if (m.command !== 'npx') errs.push(`${where}: local servers must start with npx`);
    const a = m.args || [];
    if (a[0] !== '-y' || !PKG_RE.test(a[1] || '')) errs.push(`${where}: args must be ["-y", "<package>@<exact version>", ...]`);
    for (const x of a.slice(2)) if (!/^(?:--?[A-Za-z0-9][A-Za-z0-9-]*(?:=[A-Za-z0-9._:/-]*)?|\{[A-Z][A-Z0-9_]*\})$/.test(x)) errs.push(`${where}: unsafe argument ${JSON.stringify(x)}`);
  } else errs.push(`${where}: type must be http or stdio`);
}

function build(out) {
  const cat = JSON.parse(fs.readFileSync(path.join(SRC, 'catalog.json'), 'utf8'));
  const errs = [], seen = new Set(), items = [];
  for (const it of cat.items) {
    const where = `item ${it.id}`;
    if (!ID_RE.test(it.id || '')) { errs.push(`${where}: bad id`); continue; }
    if (seen.has(it.id)) errs.push(`${where}: duplicate id`); seen.add(it.id);
    if (!['mcp', 'skill', 'plugin'].includes(it.kind)) { errs.push(`${where}: kind must be mcp, skill or plugin`); continue; }
    if (!it.name || !it.summary) errs.push(`${where}: name and summary are required`);
    for (const p of it.permissions || []) if (!PERMS.has(p)) errs.push(`${where}: unknown permission ${p}`);
    const entry = {
      id: it.id, kind: it.kind, name: it.name, summary: it.summary,
      publisher: it.publisher || 'The Astro AI team', homepage: it.homepage || 'https://github.com/esoexe/astro-ai/tree/main/library',
      license: it.license || 'MIT', tags: it.tags || [], verified: true,
      permissions: it.permissions || (it.kind === 'mcp' ? ['network'] : []), runs: it.runs || (it.kind === 'mcp' ? 'remote' : 'none'),
    };
    let fingerprint = '';
    if (it.kind === 'mcp') {
      checkMcpSpec(where, it.mcp, errs);
      const names = new Set();
      for (const inp of it.inputs || []) {
        if (!/^[A-Z][A-Z0-9_]{1,63}$/.test(inp.name || '')) errs.push(`${where}: bad input name ${inp.name}`);
        if (!['env', 'header', 'arg'].includes(inp.kind)) errs.push(`${where}: input ${inp.name} kind must be env, header or arg`);
        if (inp.kind === 'header' && !/^[A-Za-z0-9_-]{1,64}$/.test(inp.header || '')) errs.push(`${where}: input ${inp.name} needs a header name`);
        if (inp.kind === 'arg' && !(it.mcp.args || []).includes(`{${inp.name}}`)) errs.push(`${where}: arg input ${inp.name} is not used in args`);
        if (inp.kind !== 'header' && it.mcp.type === 'http') errs.push(`${where}: remote servers only take header inputs`);
        names.add(inp.name);
      }
      for (const a of it.mcp.args || []) { const m = /^\{([A-Z0-9_]+)\}$/.exec(a); if (m && !names.has(m[1])) errs.push(`${where}: placeholder ${a} has no input`); }
      entry.mcp = it.mcp; entry.inputs = it.inputs || []; entry.requires = it.requires || [];
      if (it.mcp.type === 'stdio') entry.package = it.mcp.args[1];
      fingerprint = JSON.stringify([it.mcp, entry.inputs]);
      entry.files = [];
    } else {
      const dir = path.join(SRC, it.kind === 'skill' ? 'skills' : 'plugins', it.id);
      if (!fs.existsSync(dir)) { errs.push(`${where}: missing folder ${path.relative(SRC, dir)}`); continue; }
      const rels = walk(dir);
      if (!rels.length || rels.length > MAX_FILES) errs.push(`${where}: needs 1-${MAX_FILES} files`);
      let total = 0; const files = [];
      for (const rel of rels) {
        const isDot = rel.split('/').some((s) => s.startsWith('.'));
        if ((isDot && !DOT_OK.has(rel)) || (!isDot && !FILE_RE.test(rel))) { errs.push(`${where}: file not allowed: ${rel} (text only: .md .json .txt)`); continue; }
        if (/(^|\/)hooks(\/|\.json$)/.test(rel)) { errs.push(`${where}: hooks are not allowed in the library: ${rel}`); continue; }
        const buf = fs.readFileSync(path.join(dir, rel)); total += buf.length;
        const text = checkText(`${it.id}/${rel}`, buf, errs);
        if (rel.endsWith('.json')) { try { JSON.parse(text); } catch (e) { errs.push(`${where}: ${rel} is not valid JSON`); } }
        if (rel === '.mcp.json') {
          const j = JSON.parse(text); const servers = j.mcpServers || {};
          for (const [n, s] of Object.entries(servers)) { if (s.type !== 'http') errs.push(`${where}: plugin MCP server ${n} must be remote (type http)`); else checkMcpSpec(`${where} .mcp.json ${n}`, s, errs); }
        }
        files.push({ path: rel, sha256: sha256(buf), size: buf.length, buf });
      }
      if (total > MAX_ITEM) errs.push(`${where}: larger than ${MAX_ITEM} bytes`);
      if (it.kind === 'skill') {
        const sk = files.find((f) => f.path === 'SKILL.md');
        const fm = sk && frontmatter(sk.buf.toString('utf8'));
        if (!fm || fm.name !== it.id || !fm.description) errs.push(`${where}: SKILL.md needs frontmatter with name: ${it.id} and a description`);
      } else if (!files.some((f) => f.path === '.claude-plugin/plugin.json' || f.path === 'plugin.json')) errs.push(`${where}: plugins need .claude-plugin/plugin.json`);
      entry.files = files.map(({ path: p, sha256: s, size }) => ({ path: p, sha256: s, size }));
      entry.size = total;
      entry.contents = {
        skills: files.filter((f) => /(^|\/)SKILL\.md$/.test(f.path)).length,
        commands: files.filter((f) => /^commands\/[^/]+\.md$/.test(f.path)).length,
        agents: files.filter((f) => /^agents\/[^/]+\.md$/.test(f.path)).length,
        mcp: files.some((f) => f.path === '.mcp.json') ? Object.keys(JSON.parse(files.find((f) => f.path === '.mcp.json').buf.toString('utf8')).mcpServers || {}).length : 0,
      };
      fingerprint = files.map((f) => `${f.path}:${f.sha256}`).join('\n');
      entry._files = files;
    }
    entry.version = sha256(fingerprint).slice(0, 12);
    items.push(entry);
  }
  if (errs.length) { console.error('Library check failed:\n  ' + errs.join('\n  ')); process.exit(1); }
  if (!out) { console.log(`Library OK: ${items.length} items (${items.filter((i) => i.kind === 'mcp').length} MCP, ${items.filter((i) => i.kind === 'skill').length} skills, ${items.filter((i) => i.kind === 'plugin').length} plugins)`); return; }

  fs.rmSync(out, { recursive: true, force: true });
  for (const it of items) {
    if (!it._files) continue;
    const base = path.join(out, 'files', `${it.kind}s`, it.id);
    for (const f of it._files) { fs.mkdirSync(path.dirname(path.join(base, f.path)), { recursive: true }); fs.writeFileSync(path.join(base, f.path), f.buf); }
    const z = zip(it._files.map((f) => ({ name: `${it.id}/${f.path}`, data: f.buf })));
    fs.writeFileSync(`${base}.zip`, z);
    it.zip = { path: `files/${it.kind}s/${it.id}.zip`, sha256: sha256(z), size: z.length };
    delete it._files;
  }
  const index = { schema: 1, name: cat.name, description: cat.description, generated: new Date().toISOString().slice(0, 10), files_base: 'files/', items };
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, 'index.json'), JSON.stringify(index, null, 1) + '\n');
  console.log(`Library: ${items.length} items -> ${path.relative(process.cwd(), out) || out}`);
  return index;
}

if (require.main === module) {
  const arg = process.argv[2];
  if (!arg) { console.error('usage: node library/build_library.js <out-dir> | --check'); process.exit(2); }
  build(arg === '--check' ? null : path.resolve(arg));
}
module.exports = { build };

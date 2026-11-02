#!/usr/bin/env node
// Builds the Astro Packs (library/plugins/astro-*) from Jake Schincariol's MIT skill repos.
//   node tools/port_packs.js <folder with the extracted repos>      (one sub-folder per repo, named like the repo)
// What it does, per pack in library/packs.json:
//   * copies each skill's SKILL.md and data files, adapted for A.S.T.R.O AI (names, paths, helper commands, brand)
//   * keeps the hand-written Node.js ports (*.mjs) already in the pack folder; Python files are never copied
//   * writes plugin.json, README.md and NOTICE.md (the original MIT notice + what was changed)
//   * adds or updates the pack's entry in library/catalog.json
//   * lists any Python helper that has no .mjs port yet (exit code 3 if --strict)
'use strict';
const fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const SRC = process.argv[2];
const STRICT = process.argv.includes('--strict');
if (!SRC || !fs.existsSync(SRC)) { console.error('usage: node tools/port_packs.js <folder with the extracted repos> [--strict]'); process.exit(2); }
const CFG = JSON.parse(fs.readFileSync(path.join(ROOT, 'library', 'packs.json'), 'utf8'));
const DATA_EXT = new Set(['.md', '.json', '.txt', '.csv', '.tsv']);
// data files whose names collide with A.S.T.R.O AI's "no hooks" rule for library items
// Helpers built into the A.S.T.R.O AI engine (C++, packtools/*.inc): PACKTOOL("<skill>/<helper>", ...)
const BUILTIN = new Set();
for (const n of fs.existsSync(path.join(ROOT, 'packtools')) ? fs.readdirSync(path.join(ROOT, 'packtools')) : []) if (n.endsWith('.inc'))
  for (const m of fs.readFileSync(path.join(ROOT, 'packtools', n), 'utf8').matchAll(/^PACKTOOL\("([\w.-]+\/[\w.-]+)"/gm)) BUILTIN.add(m[1]);
const FILE_RENAMES = { 'hooks.json': 'hook-formulas.json', 'claude_desktop_config.example.json': 'alpaca.mcp.example.json' };
const renameFile = (rel) => rel.split('/').map((p) => FILE_RENAMES[p] || p).join('/');
const esc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

function walk(dir, base = dir) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    if (e.name.startsWith('.') || e.name === '__pycache__' || e.name === 'tests' || e.name === 'node_modules') return [];
    const p = path.join(dir, e.name);
    return e.isDirectory() ? walk(p, base) : [path.relative(base, p).split(path.sep).join('/')];
  }).sort();
}
function frontmatterName(t) { const m = /^---\r?\n([\s\S]*?)\r?\n---/.exec(t); const n = m && /^name:\s*(.+)$/m.exec(m[1]); return n ? n[1].trim().replace(/^["']|["']$/g, '') : ''; }

// ---------- the skills of one pack: [{ orig, name, dir }] ----------
function skillsOf(pack, src) {
  const out = [];
  if (pack.layout === 'single') { const t = fs.readFileSync(path.join(src, 'SKILL.md'), 'utf8'); const n = frontmatterName(t) || pack.id; out.push({ orig: n, dir: src, single: true }); }
  else {
    const base = pack.layout === 'skills' ? path.join(src, 'skills') : src;
    for (const e of fs.readdirSync(base, { withFileTypes: true })) if (e.isDirectory() && !e.name.startsWith('.') && fs.existsSync(path.join(base, e.name, 'SKILL.md'))) out.push({ orig: e.name, dir: path.join(base, e.name) });
  }
  for (const s of out) {
    const r = pack.rename || {};
    s.name = r.map && r.map[s.orig] ? r.map[s.orig] : r.prefix && s.orig.startsWith(r.prefix[0]) ? r.prefix[1] + s.orig.slice(r.prefix[0].length) : s.orig;
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}


// phrase -> regex that tolerates any line wrapping between words
// built-in helpers: what the original says about its Python tools, said for the C++ ones (ws() tolerates line breaks)
const BUILTIN_PROSE = [
  ['small local tools (Node.js standard library + ffmpeg)', 'small local tools (built into A.S.T.R.O AI, plus ffmpeg)'],
  ['Tools live in `tools/` next to this file. Below, `T` means that folder: `${ASTRO_TOOL}', 'The tools are built into A.S.T.R.O AI, so there is nothing to install for them: `${ASTRO_TOOL}'],
  ['transcribes (faster-whisper, optional)', 'transcribes (whisper.cpp, optional)'],
  ['encoders, faster-whisper, and Node', 'encoders, whisper.cpp (and its model folder), and Node'],
  ['No faster-whisper? Exit 3 with install steps.', 'No whisper.cpp or no model? Exit 3 with install steps (whisper.cpp from its releases page, a ggml model in `~/.cache/whisper.cpp/`).'],
  ['No faster-whisper: use `--words` or the venv install it prints.', 'No whisper.cpp: use `--words` or the install steps it prints.'],
];
const ws = (p) => new RegExp(p.trim().split(/\s+/).map(esc).join('\\s+'), 'g');
const MONEY_CARD = "A.S.T.R.O AI's Money action card for that one call (it appears for every order tool call in every mode, and \"Allow all\" never covers it)";
const OS_SCHED = 'your operating system\'s scheduler (Task Scheduler on Windows, cron or launchd on macOS and Linux; A.S.T.R.O AI has no built-in scheduler)';
const HONEST_RULES = [
  ['user clicked Allow once on Astro\'s own card because the order tool is set to "Needs approval" (Customize > Connectors > tool permissions). Never set an order tool to "Always allow".', 'user approved ' + MONEY_CARD + '.'],
  ['AND the brokerage tool is set to "Needs approval" in Astro (Customize > Connectors > tool permissions) so Astro\'s own "Allow once / Deny" card appears too. Never "Always allow" an order tool.', 'AND the user approves ' + MONEY_CARD + '.'],
  ['Broker keys live in environment variables or the Astro Desktop config, never in the chat.', 'Broker keys live in environment variables or A.S.T.R.O AI\'s MCP settings, never in the chat.'],
  ['To run it on a schedule, use `/loop 30m /trader-news` during market hours or a scheduled task each morning.', 'To repeat it, ask Astro to run `/trader-news` again during market hours, or run the helper from ' + OS_SCHED + '.'],
  ['run it with `/loop 1h` or a scheduled task.', 'ask Astro to check again, or run the helper hourly from ' + OS_SCHED + '.'],
  ['During the session: `/loop 15m /daytrade-news`, or a scheduled task each morning before the open.', 'During the session, ask Astro to run `/daytrade-news` again, or run the helper each morning before the open from ' + OS_SCHED + '.'],
  ['or `entry.mjs check SYMBOL --write` on a `/loop` every 5 minutes.', 'or run `entry.mjs check SYMBOL --write` again every 5 minutes when the user asks.'],
  ['every few minutes from a Astro scheduled task or `/loop`.', 'every few minutes, each time the user asks, or from ' + OS_SCHED + '.'],
  ['in the background or on a `/loop` until a setup triggers', 'in the background until a setup triggers'],
  ['(a Astro scheduled task or cron, every 15 minutes in market hours is plenty)', '(from ' + OS_SCHED + '; every 15 minutes in market hours is plenty)'],
  // closer-skill (sales-coach, record-keeper, scheduler, follow-up): A.S.T.R.O AI has no Chrome extension, no built-in
  // Gmail or calendar connector and no scheduler. Say what really works: MCP servers the user adds, and files.
  ['With Astro in Chrome, read the captions area every few seconds (`get_page_text` or `read_page` on the Meet tab)', 'With a browser tool that can see the Meet tab (such as the Playwright Browser MCP server from the Library, with the user in the meeting in the window it opens), read the captions area every few seconds'],
  ['read off the page by Astro in Chrome', 'read off the page by a browser tool'],
  ['(Astro in Chrome or the', '(the Playwright Browser MCP server from the Library or the'],
  ['(Astro in Chrome, the', '(the Playwright Browser MCP server from the Library, the'],
  ['Astro in Chrome', 'a browser tool (such as the Playwright Browser MCP server from the Library)'],
  ['No Gmail connector?', 'No Gmail MCP server?'],
  ['(the Gmail connector)', "(through a Gmail MCP server the user added in A.S.T.R.O AI's MCP settings)"],
  ['the Gmail connector', "a Gmail MCP server the user added in A.S.T.R.O AI's MCP settings"],
  ['No calendar connector:', 'No calendar MCP server:'],
  ['the calendar connector', "a calendar MCP server the user added in A.S.T.R.O AI's MCP settings"],
  ['A scheduled morning run may list and draft.', 'A morning run started from ' + OS_SCHED + ' may list and draft.'],
  // replica-deploy: install paths of the Python helpers do not apply to built-in tools
  ['(Paths are relative to wherever the pack is installed. Under `~/.astro-packs/skills/` that is `~/.astro-packs/skills/replica-diff/parity.mjs` and so on.)', '(These tools are built into A.S.T.R.O AI, so the commands work from any folder.)'],
];
// ---------- text adaptation ----------
function makeTx(pack, skills, pyNames) {
  const renames = skills.filter((s) => s.orig !== s.name);
  const origSet = new Map(skills.map((s) => [s.orig, s.name]));
  const helperPath = (p, hadDir) => {
    if (hadDir || !p.includes('/') || p.startsWith('../') || p.startsWith('./')) return `\${ASTRO_SKILL_DIR}/${p.replace(/^\.\//, '')}`;
    const segs = p.split('/');
    if (segs[0] === 'skills' && origSet.has(segs[1])) return `\${ASTRO_PACK_DIR}/skills/${origSet.get(segs[1])}/${segs.slice(2).join('/')}`;
    if (origSet.has(segs[0])) return `\${ASTRO_PACK_DIR}/skills/${origSet.get(segs[0])}/${segs.slice(1).join('/')}`;
    return `\${ASTRO_SKILL_DIR}/${p}`;
  };
  return (t, where) => {
    let s = t.replace(/\r\n/g, '\n');
    // test and install lines from the original repo make no sense inside A.S.T.R.O AI
    s = s.split('\n').filter((l) => !/python3?\s+-m\s+(unittest|pytest)|pip3?\s+install|\/plugin\s+(install|marketplace)/.test(l)).join('\n');
    // master-skills: "Tools live in `tools/` next to this file. Below, `T` means that folder" + python3 "$T/x.py"
    s = s.replace(/\bpython3?\s+"\$T\/([A-Za-z0-9_.-]+?)\.py"/g, (_m, p) => `node "\${ASTRO_SKILL_DIR}/tools/${p}.mjs"`);
    // python3 "${CLAUDE_SKILL_DIR}/x.py" | python3 x.py | python3 ../y/x.py  ->  node "<dir>/x.mjs"
    // ...except scripts of other projects the docs tell the user to run in that project's checkout (not ported, not shipped)
    s = s.replace(/\bpython3?\s+("?)(?!scripts\/serve\.py)(\$\{CLAUDE_SKILL_DIR\}\/|\$CLAUDE_SKILL_DIR\/)?([A-Za-z0-9_./-]+?)\.py\1(?=[\s`)'"]|$)/gm, (_m, _q, dir, p) => `node "${helperPath(p, !!dir)}.mjs"`);
    // bare mentions of the helpers: deadair.py -> deadair.mjs
    // (refs/x.py is the author's own reference script from his video, not shipped and not ported: left as it is)
    for (const n of pyNames) s = s.replace(new RegExp(`(?<![\\w-]|refs/)${esc(n)}\\.py\\b`, 'g'), `${n}.mjs`);
    for (const [a, b] of Object.entries(FILE_RENAMES)) s = s.replace(new RegExp(`(?<![\\w-])${esc(a)}`, 'g'), b);
    s = s.replace(/\$\{CLAUDE_SKILL_DIR\}|\$CLAUDE_SKILL_DIR\b/g, '${ASTRO_SKILL_DIR}').replace(/\$\{CLAUDE_PLUGIN_ROOT\}/g, '${ASTRO_PACK_DIR}');
    s = s.replace(/~\/\.claude\/skills\//g, '~/.astro-packs/skills/').replace(/~\/\.claude\//g, '~/.astro-packs/');
    // skill renames: hyphenated names everywhere, one-word names only where they are clearly the skill
    for (const r of renames) {
      if (/-/.test(r.orig)) s = s.replace(new RegExp(`(?<![\\w-])${esc(r.orig)}(?![\\w-])`, 'g'), r.name);
      else {
        s = s.replace(new RegExp(`(^|[\\s(\`"'])/${esc(r.orig)}(?![\\w-])`, 'gm'), `$1/${r.name}`);
        s = s.replace(new RegExp(`\`${esc(r.orig)}\``, 'g'), `\`${r.name}\``);
        s = s.replace(new RegExp(`skills/${esc(r.orig)}(?=/|\\b)`, 'g'), `skills/${r.name}`);
        s = s.replace(new RegExp(`^name:\\s*${esc(r.orig)}\\s*$`, 'm'), `name: ${r.name}`);
      }
    }
    // brand: the agent is A.S.T.R.O AI (spoken "Astro"); keep repo names, URLs and file paths untouched
    s = s.replace(/(?<![\w./-])Claude Code(?![\w-])/g, 'A.S.T.R.O AI');
    s = s.replace(/(?<![\w./-])Claude(?=['’]s\b)/g, 'Astro');
    s = s.replace(/(?<![\w./@-])Claude(?!\.(?:ai|com|json)\b)(?![\w/-])/g, 'Astro');
    // the helpers are Node.js now
    s = s.replace(/standard[- ]library Python/gi, 'standard-library Node.js').replace(/wherever Python 3 runs/g, 'wherever Node.js runs').replace(/there is no Python/g, 'there is no Node.js')
      .replace(/\bpure Python\b/g, 'plain JavaScript').replace(/\bplain Python\b/g, 'plain Node.js').replace(/Python's `random\.Random\(seed\)`/g, 'a seeded random generator (mulberry32)')
      .replace(/a small Python script/g, 'a small Node.js script').replace(/Python 3 standard library/g, 'Node.js standard library').replace(/\(Python 3\b/g, '(Node.js').replace(/Python `zlib\.decompress`/g, 'Node `zlib.inflateSync`').replace(/\bpython3? -m yt_dlp\b/g, 'yt-dlp');
    // Claude Desktop's permission model and schedulers do not exist in A.S.T.R.O AI: say what really happens
    for (const [a, b] of HONEST_RULES) s = s.replace(ws(a), b);
    s = s.replace(/(^|[.!?]\s+)a browser tool \(such as/gm, '$1A browser tool (such as');   // a rewrite that starts a sentence
    s = s.replace(/\bthe Task tool\b/gi, 'the task tool').replace(/\bTask tool\b/g, 'task tool');
    s = s.replace(/\bWebFetch\b/g, 'fetch_page').replace(/\bWebSearch\b/g, 'web_search').replace(/\bAskUserQuestion\b/g, 'ask_user');
    if (where === 'SKILL.md') s = foldFrontmatter(s);
    if (where === 'SKILL.md' && !/^---\n(?:[\s\S]*?\n)?name:/.test(s)) throw new Error('SKILL.md without frontmatter name');
    return s;
  };
}

// "description: >-" + indented lines  ->  one line (A.S.T.R.O AI and most tools read one-line values)
function foldFrontmatter(t) {
  const m = /^---\n([\s\S]*?)\n---\n/.exec(t); if (!m) return t;
  const lines = m[1].split('\n'); const out = [];
  for (let i = 0; i < lines.length; i++) {
    const k = /^([A-Za-z0-9_-]+):\s*([>|][-+]?)\s*$/.exec(lines[i]);
    if (!k) { out.push(lines[i]); continue; }
    const vals = []; while (i + 1 < lines.length && (/^\s+\S/.test(lines[i + 1]) || lines[i + 1].trim() === '')) vals.push(lines[++i].trim());
    const v = vals.join(' ').replace(/\s+/g, ' ').trim();
    out.push(`${k[1]}: ${/[:#"'\[\]{}]|^[>|&*!%@`]/.test(v) ? `"${v.replace(/"/g, "'")}"` : v}`);
  }
  return `---\n${out.join('\n')}\n---\n` + t.slice(m[0].length);
}

// The style-editor skills: same tools, honest framing.
const STYLE_NOTE = '\n> **A.S.T.R.O AI note.** This is a style editor: it makes a draft read naturally, in the user\'s own voice, and removes clichés and hidden Unicode characters. It is not a way to hide that AI helped. Where a platform, school or employer asks you to disclose AI use, disclose it.\n';
const TRADING_NOTE = (c) => `\n> **A.S.T.R.O AI safety rule.** ${c}\n`;

function readLicense(src) {
  for (const n of ['LICENSE', 'LICENSE.md', 'LICENSE.txt']) if (fs.existsSync(path.join(src, n))) return fs.readFileSync(path.join(src, n), 'utf8').trim();
  throw new Error(`no LICENSE in ${src}`);
}

// ---------- one pack ----------
function buildPack(pack) {
  const src = path.join(SRC, pack.repo);
  if (!fs.existsSync(src)) throw new Error(`missing source folder ${src}`);
  const out = path.join(ROOT, 'library', 'plugins', pack.id);
  // keep the hand-written ports, regenerate everything else
  const keep = new Map();
  for (const rel of walk(out)) if (rel.endsWith('.mjs')) keep.set(rel, fs.readFileSync(path.join(out, rel)));
  fs.rmSync(out, { recursive: true, force: true });
  for (const [rel, buf] of keep) { fs.mkdirSync(path.dirname(path.join(out, rel)), { recursive: true }); fs.writeFileSync(path.join(out, rel), buf); }

  const skills = skillsOf(pack, src);
  const pyFiles = []; const pyNames = new Set();
  for (const sk of skills) for (const rel of (sk.single ? [] : walk(sk.dir))) if (rel.endsWith('.py') && !/(^|\/)test_[^/]*\.py$|(^|\/)_load\.py$/.test(rel)) { pyFiles.push({ sk, rel }); pyNames.add(path.basename(rel, '.py')); }
  const tx = makeTx(pack, skills, pyNames);
  const notes = []; const missing = []; let ruled = 0;
  const put = (rel, text) => { const p = path.join(out, rel); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, text); };

  for (const sk of skills) {
    const files = sk.single ? ['SKILL.md', ...fs.readdirSync(sk.dir).filter((f) => f !== 'SKILL.md' && f !== 'README.md' && DATA_EXT.has(path.extname(f)) && f !== 'LICENSE' && fs.statSync(path.join(sk.dir, f)).isFile())] : walk(sk.dir);
    for (const rel of files) {
      const ext = path.extname(rel);
      const dest = `skills/${sk.name}/${renameFile(rel)}`;
      if (ext === '.py') continue;
      if (/(^|\/)README\.md$/.test(rel) && rel !== 'SKILL.md' && sk.single) continue;
      let buf = fs.readFileSync(path.join(sk.dir, rel));
      if (!DATA_EXT.has(ext) && !buf.includes(0) && buf.length < 200000) { put(`${dest}.txt`, buf); notes.push(`${sk.name}/${rel} is kept as an example text file (${rel}.txt)`); continue; }
      if (!DATA_EXT.has(ext)) { notes.push(`skipped ${sk.orig}/${rel} (binary or unsupported type)`); continue; }
      if (ext === '.md') {
        let t = tx(buf.toString('utf8'), path.basename(rel));
        if (rel === 'SKILL.md') {
          if (/(^|-)human$/.test(sk.orig)) t = t.replace(/\n---\n/, () => `\n---\n${STYLE_NOTE}`);
          if (pack.caveats && /trader|daytrade|console|replica/.test(sk.name)) { t = t.replace(/\n---\n/, () => `\n---\n${TRADING_NOTE(pack.caveats)}`); ruled++; }
        }
        buf = Buffer.from(t);
      } else if (ext === '.json') {   // notes inside data files name the helpers too
        let t = buf.toString('utf8').replace(/\bpython3? +([\w./-]+)\.py\b/g, (m, f) => `node ${f}.mjs`).replace(/\b([\w-]+)\.py\b/g, (m, f) => `${f}.mjs`);
        for (const [k, v] of Object.entries(FILE_RENAMES)) t = t.split(k).join(v);
        JSON.parse(t); buf = Buffer.from(t);
      }
      put(dest, buf);
    }
  }
  // pack-level templates (voice.md etc.)
  for (const rel of walk(path.join(src, 'templates'))) if (DATA_EXT.has(path.extname(rel))) put(`templates/${rel}`, path.extname(rel) === '.md' ? tx(fs.readFileSync(path.join(src, 'templates', rel), 'utf8'), rel) : fs.readFileSync(path.join(src, 'templates', rel)));
  if (fs.existsSync(path.join(out, 'templates'))) for (const sk of skills) { const p = path.join(out, 'skills', sk.name, 'SKILL.md'); fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace(/(?<![\w/.$}-])templates\//g, '${ASTRO_PACK_DIR}/templates/')); }

  // hand-written replacements for files that needed more than mechanical changes
  const ov = path.join(ROOT, 'library', 'pack-overrides', pack.id);
  for (const rel of walk(ov)) { fs.mkdirSync(path.dirname(path.join(out, rel)), { recursive: true }); fs.copyFileSync(path.join(ov, rel), path.join(out, rel)); }
  // helpers built into the engine: ${ASTRO_TOOL} <skill>/<helper> replaces node "<dir>/<helper>.mjs", and the .mjs goes
  let builtin = 0;
  // a bare `x.mjs` mention: this skill's own built-in helper -> `x`; another skill's (exactly one in this pack) -> `skill/x`
  const names = new Set(skills.map((k) => k.name));
  const bare = (own, h, m) => { if (BUILTIN.has(`${own}/${h}`)) return h; const hits = [...BUILTIN].filter((id) => id.endsWith(`/${h}`) && names.has(id.split('/')[0])); return hits.length === 1 ? hits[0] : m; };
  for (const sk of skills) {
    const dir = path.join(out, 'skills', sk.name);
    for (const rel of walk(dir)) {
      if (rel.endsWith('.mjs') && BUILTIN.has(`${sk.name}/${path.basename(rel, '.mjs')}`)) { fs.rmSync(path.join(dir, rel)); continue; }
      if (!/\.(md|json)$/.test(rel)) continue;
      const p = path.join(dir, rel); const t0 = fs.readFileSync(p, 'utf8');
      const t = t0
        // a sibling skill's helper (founder-pricing runs ../founder-cfo/unit_economics)
        .replace(/node "\$\{ASTRO_SKILL_DIR\}\/\.\.\/([\w-]+)\/(?:[\w.-]+\/)*([\w.-]+)\.mjs"/g, (m, k, h) => (BUILTIN.has(`${k}/${h}`) ? `\${ASTRO_TOOL} ${k}/${h}` : m))
        // helpers may sit in a sub-folder of the skill (master-skills keeps them in tools/): the id uses the file name
        .replace(/node "\$\{ASTRO_SKILL_DIR\}\/(?:[\w.-]+\/)*([\w.-]+)\.mjs"/g, (m, h) => (BUILTIN.has(`${sk.name}/${h}`) ? `\${ASTRO_TOOL} ${sk.name}/${h}` : m))
        .replace(/node "\$\{ASTRO_PACK_DIR\}\/skills\/([\w-]+)\/(?:[\w.-]+\/)*([\w.-]+)\.mjs"/g, (m, k, h) => (BUILTIN.has(`${k}/${h}`) ? `\${ASTRO_TOOL} ${k}/${h}` : m))
        .replace(/(?<![\w\/.$}-])(?:[\w.-]+\/)*([\w-]+)\.mjs\b/g, (m, h) => bare(sk.name, h, m));
      // a built-in helper is not a file in the folder, and it is C++, not Node.js
      const t2 = /\$\{ASTRO_TOOL\}/.test(t) ? t
        .replace(/\b(Tools?) in this folder \(standard-library Node\.js\)/g, '$1 built into A.S.T.R.O AI')
        .replace(/\b(Tools?) in this folder \(standard-library Node\.js[,;]\s+/g, '$1 built into A.S.T.R.O AI (') : t;
      const t3 = /\$\{ASTRO_TOOL\}/.test(t2) ? BUILTIN_PROSE.reduce((acc, [a, b]) => acc.replace(ws(a), () => b), t2) : t2;
      if (t3 !== t0) fs.writeFileSync(p, t3);
      if (/\$\{ASTRO_TOOL\}/.test(t)) builtin++;
    }
  }
  // pack-level templates name helpers too (instagram's voice.md: "beats.py --wpm uses this"): the unique built-in id
  for (const rel of walk(path.join(out, 'templates'))) {
    if (!rel.endsWith('.md')) continue;
    const p = path.join(out, 'templates', rel); const t0 = fs.readFileSync(p, 'utf8');
    const t = t0.replace(/(?<![\w\/.$}-])(?:[\w.-]+\/)*([\w-]+)\.mjs\b/g, (m, h) => bare('', h, m));
    if (t !== t0) fs.writeFileSync(p, t);
  }
  // a module with no __main__ (a shared library such as master-skills' mediakit.py) is built in when a built-in helper imports it
  const libCovered = (sk, rel, h) => {
    if (/__name__\s*==\s*['"]__main__['"]/.test(fs.readFileSync(path.join(sk.dir, rel), 'utf8'))) return false;
    const imp = new RegExp(`^\\s*(?:import|from)\\s+${esc(h)}\\b`, 'm');
    return pyFiles.some((o) => o.sk === sk && o.rel !== rel && BUILTIN.has(`${sk.name}/${path.basename(o.rel, '.py')}`) && imp.test(fs.readFileSync(path.join(sk.dir, o.rel), 'utf8')));
  };
  for (const { sk, rel } of pyFiles) { const h = path.basename(rel, '.py'); const want = `skills/${sk.name}/${rel.replace(/\.py$/, '.mjs')}`; if (!BUILTIN.has(`${sk.name}/${h}`) && !libCovered(sk, rel, h) && !fs.existsSync(path.join(out, want))) missing.push(want); }
  return { skills, notes, missing, out, scripts: walk(out).some((r) => r.endsWith('.mjs')), builtin: builtin > 0, ruled: ruled > 0 };
}

function writeMeta(pack, r) {
  const src = path.join(SRC, pack.repo);
  const repoUrl = `https://github.com/Jakeschincariol/${pack.repo}`;
  const commitFile = path.join(src, '.astro-commit');
  const commit = fs.existsSync(commitFile) ? fs.readFileSync(commitFile, 'utf8').trim() : '';
  const plugin = { name: pack.id, displayName: pack.name, version: '1.0.0', description: pack.summary, author: { name: 'The Astro AI team' }, license: 'MIT',
    source: { repo: repoUrl, author: CFG.author, license: 'MIT', ...(commit ? { commit } : {}) }, skills: r.skills.map((s) => s.name) };
  fs.writeFileSync(path.join(r.out, 'plugin.json'), JSON.stringify(plugin, null, 2) + '\n');
  const renamed = r.skills.filter((s) => s.orig !== s.name);
  fs.writeFileSync(path.join(r.out, 'NOTICE.md'), `# Notice

**${pack.name}** is an Astro Pack: an adaptation for A.S.T.R.O AI of **${pack.repo}** by ${CFG.author}
(${repoUrl}${commit ? `, commit ${commit.slice(0, 12)}` : ''}), used under the MIT License below.

Changes made for A.S.T.R.O AI:
${r.builtin ? '- Python helpers were re-written in C++ and built into A.S.T.R.O AI itself (run with \`\${ASTRO_TOOL}\`, A.S.T.R.O AI ' + (pack.min_version || '1.9.0') + ' or later); they print what the originals printed. The Python files are not included.\n' : ''}${r.scripts ? '- Python helpers were re-written as Node.js scripts (\`.mjs\`, Node standard library only) that do the same job; the Python files are not included.\n' : ''}- Commands, paths and the assistant's name were adapted (\`\${ASTRO_SKILL_DIR}\`, \`\${ASTRO_PACK_DIR}\`, \`~/.astro-packs/\`).
${renamed.length ? `- Skills were renamed: ${renamed.map((s) => `\`${s.orig}\` → \`${s.name}\``).join(', ')}.\n` : ''}${pack.caveats ? `- ${r.ruled ? 'A safety rule was added to the skills' : 'A note was added to the README and the Library listing'}: ${pack.caveats}\n` : ''}- Tests, install instructions for other tools and repository files were left out.

This pack is not made or endorsed by ${CFG.author}. Report problems with the A.S.T.R.O AI version to https://github.com/esoexe/astro-ai/issues.

---

${readLicense(src)}
`);
  fs.writeFileSync(path.join(r.out, 'README.md'), `# ${pack.name}

${pack.summary}

${pack.caveats ? `> **Read first.** ${pack.caveats}\n\n` : ''}Adapted from [${pack.repo}](${repoUrl}) by ${CFG.author} (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
${r.skills.map((s) => { const t = fs.readFileSync(path.join(r.out, 'skills', s.name, 'SKILL.md'), 'utf8'); const m = /^description:\s*(?:>-?\s*\n)?([\s\S]*?)\n(?:[a-z_-]+:|---)/m.exec(t); let d = (m ? m[1] : '').replace(/\s+/g, ' ').trim().replace(/^"([\s\S]*)"$/, '$1'); if (d.length > 220) d = d.slice(0, 220).replace(/\s+\S*$/, '') + '…'; return `| \`${s.name}\` | ${d.replace(/\|/g, '/')} |`; }).join('\n')}

## Use

Ask the agent for the job in plain words ("write chapters for this transcript"), or name the skill.
${r.builtin ? `Some skills run helper tools that are built into A.S.T.R.O AI (${pack.min_version || '1.9.0'} or later); nothing extra to install.\n` : ''}${r.scripts ? `Some skills run small helper scripts with Node.js. If Node.js is missing, install it in Settings > Tools.\n` : ''}${pack.network ? 'Some helpers read public web data (they say which site in their help text).\n' : ''}${pack.programs ? `Some helpers call ${pack.programs.join(', ')} if it is installed.\n` : ''}`);
  return { repoUrl, commit };
}

function upsertCatalog(pack, r, meta) {
  const file = path.join(ROOT, 'library', 'catalog.json');
  const cat = JSON.parse(fs.readFileSync(file, 'utf8'));
  const item = { id: pack.id, kind: 'plugin', name: `${pack.name} (Astro Pack)`, summary: pack.summary,
    publisher: `Community: adapted from ${CFG.author} (MIT)`, homepage: meta.repoUrl, license: 'MIT', tags: ['astro-pack', ...pack.tags],
    runs: r.scripts || r.builtin ? 'local' : 'none', permissions: [...(r.scripts || r.builtin ? ['scripts'] : []), ...(pack.network ? ['network'] : []), 'files'],
    pack: true, source: { repo: meta.repoUrl, license: 'MIT', author: CFG.author, ...(meta.commit ? { commit: meta.commit } : {}) } };
  if (r.scripts) item.scripts = 'node'; else if (r.builtin) item.scripts = 'builtin';
  if (r.builtin) item.min_version = pack.min_version || '1.9.0';   // packs.json min_version: the release that first built its tools in
  if (pack.network) item.network = true;
  if (pack.programs) item.programs = pack.programs;
  if (pack.caveats) item.caveats = pack.caveats;
  const i = cat.items.findIndex((x) => x.id === pack.id);
  if (i >= 0) cat.items[i] = item; else cat.items.push(item);
  fs.writeFileSync(file, JSON.stringify(cat, null, 2) + '\n');
}

let allMissing = 0;
const only = process.argv.slice(3).filter((a) => !a.startsWith('--'));
for (const pack of CFG.packs) {
  if (only.length && !only.includes(pack.id)) continue;
  const r = buildPack(pack);
  const meta = writeMeta(pack, r);
  upsertCatalog(pack, r, meta);
  allMissing += r.missing.length;
  console.log(`${pack.id}: ${r.skills.length} skills${r.builtin ? ', built-in tools' : ''}${r.scripts ? ', scripts' : ''}${r.missing.length ? `, ${r.missing.length} helper(s) not ported yet` : ''}`);
  for (const n of r.notes) console.log(`  note: ${n}`);
  if (process.argv.includes('--missing')) for (const m of r.missing) console.log(`  todo: ${m}`);
}
if (STRICT && allMissing) process.exit(3);

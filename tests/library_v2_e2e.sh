#!/usr/bin/env bash
# End-to-end test of community (schema 2) installs: a small catalog and fake GitHub raw files are served on
# 127.0.0.1, then the backend installs, refuses and uninstalls items from it.
# Usage: bash tests/library_v2_e2e.sh      (needs node and a built ./workspace_server)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TMP="$(mktemp -d /tmp/astro-lib2-e2e.XXXXXX)"
LIBPORT=4193 APIPORT=4198
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }

node library/build_library.js "$TMP/site/library" >/dev/null || { echo "library build failed"; exit 1; }
node - "$TMP/site" <<'EOF'
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const site = process.argv[2], v2 = path.join(site, 'library/v2'), raw = path.join(site, 'raw');
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
const C = 'a'.repeat(40);
const put = (repo, dir, rel, text) => { const p = path.join(raw, repo, C, dir, rel); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, text); return [rel, sha(Buffer.from(text)), Buffer.byteLength(text)]; };
const skillFiles = [put('acme/skills', 'skills/hello', 'SKILL.md', '---\nname: hello\ndescription: Says hello\n---\nSay hello.\n'), put('acme/skills', 'skills/hello', 'scripts/run.sh', 'echo hi\n')];
const tamperFiles = [put('acme/skills', 'skills/bad', 'SKILL.md', '---\nname: bad\n---\nok\n')];
fs.appendFileSync(path.join(raw, 'acme/skills', C, 'skills/bad/SKILL.md'), 'evil');
const plugFiles = [
  put('acme/plugins', 'p1', '.claude-plugin/plugin.json', JSON.stringify({ name: 'p1', hooks: { PreToolUse: [] } })),
  put('acme/plugins', 'p1', '.mcp.json', JSON.stringify({ mcpServers: { local: { command: 'node', args: ['x.js'] }, remote: { type: 'http', url: 'https://example.com/mcp' } } })),
  put('acme/plugins', 'p1', 'commands/go.md', '# go\n'),
];
const det = {
  'remote-one': { k: 'mcp', n: 'Remote one', i: { t: 'http', url: 'https://{TENANT}.example.com/mcp', headers: [{ h: 'Authorization', label: 'Token', secret: 1, req: 1, prefix: 'Bearer ' }, { h: 'X-Fixed', value: 'yes' }] }, in: [{ name: 'TENANT', label: 'Tenant', req: 1, url: 1 }] },
  'npx-one': { k: 'mcp', n: 'Npx one', i: { t: 'stdio', cmd: 'npx', args: ['-y', 'some-mcp@1.2.3', '--root', '{ROOT}'], env: [{ name: 'API_KEY', label: 'Key', secret: 1, req: 1 }, { name: 'MODE', def: 'fast' }] }, in: [{ name: 'ROOT', label: 'Root', req: 1 }] },
  'npx-unpinned': { k: 'mcp', n: 'Unpinned', i: { t: 'stdio', cmd: 'npx', args: ['-y', 'some-mcp'], env: [] } },
  'docker-bad': { k: 'mcp', n: 'Docker bad', i: { t: 'stdio', cmd: 'docker', args: ['run', '-i', '--rm', '--privileged', 'ghcr.io/acme/x:1.0'], env: [] } },
  'uvx-one': { k: 'mcp', n: 'Uvx one', i: { t: 'stdio', cmd: 'uvx', args: ['some-mcp==0.4.1'], env: [] } },
  'no-setup': { k: 'mcp', n: 'No setup' },
  'hello-acme': { k: 'skill', n: 'hello', repo: 'acme/skills', c: C, dir: 'skills/hello', f: skillFiles },
  'bad-acme': { k: 'skill', n: 'bad', repo: 'acme/skills', c: C, dir: 'skills/bad', f: tamperFiles },
  'evil-path': { k: 'skill', n: 'evil', repo: 'acme/skills', c: C, dir: '../x', f: tamperFiles },
  'evil-file': { k: 'skill', n: 'evil', repo: 'acme/skills', c: C, dir: 'skills/hello', f: [['SKILL.md', skillFiles[0][1], 10], ['hooks/hooks.json', 'b'.repeat(64), 2]] },
  'p1-acme-plugin': { k: 'plugin', n: 'p1', repo: 'acme/plugins', c: C, dir: 'p1', f: plugFiles },
  'some-app': { k: 'tool', t: 'link', n: 'Some app', url: 'https://example.com' },
  'cli-unpinned': { k: 'tool', t: 'npm', n: 'cli', pkg: 'left-pad', bins: ['x'] },
};
const buckets = {};
for (const [id, d] of Object.entries(det)) { const hh = sha(id).slice(0, 2); (buckets[hh] = buckets[hh] || {})[id] = d; }
fs.mkdirSync(path.join(v2, 'detail'), { recursive: true }); fs.mkdirSync(path.join(v2, 'list'), { recursive: true });
for (const [hh, b] of Object.entries(buckets)) fs.writeFileSync(path.join(v2, 'detail', hh + '.json'), JSON.stringify(b));
fs.writeFileSync(path.join(v2, 'list', 'mcp-0.json'), JSON.stringify({ kind: 'mcp', rows: [['remote-one', 'Remote one', '', 'acme', 'hs', '', '1']] }));
fs.writeFileSync(path.join(v2, 'manifest.json'), JSON.stringify({ schema: 2, total: 1, kinds: { mcp: { count: 1, shards: ['list/mcp-0.json'] } } }));
EOF

cat > "$TMP/serve.js" <<'EOF'
const http = require('http'), fs = require('fs'), path = require('path');
const root = process.argv[2];
http.createServer((q, s) => {
  const p = path.join(root, decodeURIComponent(q.url.split('?')[0]));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { s.writeHead(404); return s.end('no'); }
  s.writeHead(200); s.end(fs.readFileSync(p));
}).listen(+process.argv[3], '127.0.0.1');
EOF
node "$TMP/serve.js" "$TMP/site" $LIBPORT & SRV=$!
mkdir -p "$TMP/data"
ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$APIPORT ASTRO_LIBRARY_URL="http://127.0.0.1:$LIBPORT/library/index.json" ASTRO_LIBRARY_RAW_BASE="http://127.0.0.1:$LIBPORT/raw/" \
  "$ROOT/workspace_server" >"$TMP/server.log" 2>&1 & API=$!
cleanup() { kill $SRV $API 2>/dev/null; wait $SRV $API 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$APIPORT/" && curl -s -o /dev/null "http://127.0.0.1:$LIBPORT/library/v2/manifest.json" && break; sleep 0.2; done

call() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$APIPORT/api/dev/library/$1" -d "$2"; }
has()  { echo "$1" | grep -q -- "$2"; }
EXT="$TMP/data/ext"

r=$(call v2 '{"file":"manifest.json"}');                  has "$r" '"schema":2' && ok "manifest served through the backend" || bad "manifest: $r"
r=$(call v2 '{"file":"../index.json"}');                   has "$r" 'Unknown catalog file' && ok "catalog file names are checked" || bad "v2 file check: $r"
r=$(call detail '{"id":"hello-acme"}');                    has "$r" '"repo":"acme/skills"' && ok "detail lookup by id" || bad "detail: $r"
r=$(call install '{"id":"remote-one"}');                   has "$r" 'required' && ok "required URL value enforced" || bad "remote required: $r"
r=$(call install '{"id":"remote-one","inputs":{"TENANT":"a/b","Authorization":"t"}}'); has "$r" 'letters, numbers' && ok "URL value checked" || bad "url var: $r"
r=$(call install '{"id":"remote-one","inputs":{"TENANT":"team1","Authorization":"tok123"}}'); has "$r" '"ok":true' && grep -q 'https://team1.example.com/mcp' "$EXT/mcp.json" && grep -q 'Bearer tok123' "$EXT/mcp.json" && grep -q '"X-Fixed": *"yes"' "$EXT/mcp.json" && ok "remote MCP with URL value, prefixed and fixed headers" || bad "remote: $r"
r=$(call install '{"id":"npx-one","inputs":{"ROOT":"/tmp"}}'); has "$r" 'API_KEY is required' && ok "required env enforced" || bad "env required: $r"
r=$(call install '{"id":"npx-one","inputs":{"ROOT":"/tmp; rm -rf /","API_KEY":"k"}}'); has "$r" 'not allowed' && ok "shell syntax in arg refused" || bad "arg: $r"
r=$(call install '{"id":"npx-one","inputs":{"ROOT":"/tmp","API_KEY":"k1"}}'); has "$r" '"ok":true' && grep -q 'some-mcp@1.2.3' "$EXT/mcp.json" && grep -q '"MODE": *"fast"' "$EXT/mcp.json" && grep -q '"API_KEY": *"k1"' "$EXT/mcp.json" && ok "pinned npx MCP with env and defaults" || bad "npx: $r"
r=$(call install '{"id":"npx-unpinned"}');                 has "$r" 'not pinned' && ok "unpinned package refused" || bad "unpinned: $r"
r=$(call install '{"id":"docker-bad"}');                   has "$r" 'not pinned' && ok "privileged docker refused" || bad "docker: $r"
r=$(call install '{"id":"uvx-one"}');                      has "$r" '"ok":true' && grep -q 'some-mcp==0.4.1' "$EXT/mcp.json" && ok "pinned uvx MCP" || bad "uvx: $r"
r=$(call install '{"id":"no-setup"}');                     has "$r" 'no one-click setup' && ok "no-setup server explained" || bad "no-setup: $r"
r=$(call install '{"id":"hello-acme"}');                   has "$r" '"ok":true' && [ -f "$EXT/skills/hello-acme/SKILL.md" ] && [ -f "$EXT/skills/hello-acme/scripts/run.sh" ] && ok "community skill installed from pinned commit" || bad "skill: $r"
grep -q '"commit": *"aaaa' "$EXT/library.json" && ok "receipt records the commit" || bad "receipt commit"
r=$(call install '{"id":"bad-acme"}');                     has "$r" 'Checksum mismatch' && [ ! -e "$EXT/skills/bad-acme" ] && ok "tampered file refused, nothing written" || bad "tamper: $r"
r=$(call install '{"id":"evil-path"}');                    has "$r" 'invalid source' && ok "path traversal in source refused" || bad "evil path: $r"
r=$(call install '{"id":"evil-file"}');                    has "$r" 'not allowed' && ok "hook file refused" || bad "evil file: $r"
r=$(call install '{"id":"p1-acme-plugin"}');               has "$r" '"ok":true' && ok "community plugin installed" || bad "plugin: $r"
P="$EXT/plugins/p1-acme-plugin"
grep -q 'example.com/mcp' "$P/.mcp.json" && ! grep -q '"command"' "$P/.mcp.json" && ok "plugin local MCP server left out, remote kept" || bad "plugin mcp: $(cat "$P/.mcp.json" 2>/dev/null)"
! grep -q hooks "$P/.claude-plugin/plugin.json" && ok "plugin hooks stripped" || bad "plugin hooks"
r=$(call install '{"id":"some-app"}');                     has "$r" 'not something to install' && ok "app links are not installable" || bad "app: $r"
r=$(call install '{"id":"cli-unpinned"}');                 has "$r" 'pinned' && ok "unpinned CLI refused" || bad "cli: $r"
r=$(call install '{"id":"nothing-here"}');                 has "$r" 'not in the Astro AI Library' && ok "unknown id refused" || bad "unknown: $r"
r=$(call uninstall '{"id":"hello-acme"}');                 has "$r" '"ok":true' && [ ! -e "$EXT/skills/hello-acme" ] && ok "community skill uninstalled" || bad "uninstall: $r"
r=$(call uninstall '{"id":"remote-one"}');                 has "$r" '"ok":true' && ! grep -q 'remote-one' "$EXT/mcp.json" && ok "community MCP uninstalled" || bad "uninstall mcp: $r"

echo "library v2 e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]

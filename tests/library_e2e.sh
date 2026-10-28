#!/usr/bin/env bash
# End-to-end test of the Astro AI Library installer:
#   build the library -> serve it on 127.0.0.1 -> start the backend against it -> install / tamper / uninstall.
# Usage: bash tests/library_e2e.sh      (needs node and a built ./workspace_server)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TMP="$(mktemp -d /tmp/astro-lib-e2e.XXXXXX)"
LIBPORT=4192 APIPORT=4197
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }

node library/build_library.js "$TMP/site/library" >/dev/null || { echo "library build failed"; exit 1; }

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
ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$APIPORT ASTRO_LIBRARY_URL="http://127.0.0.1:$LIBPORT/library/index.json" \
  "$ROOT/workspace_server" >"$TMP/server.log" 2>&1 & API=$!
cleanup() { kill $SRV $API 2>/dev/null; wait $SRV $API 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$APIPORT/" && curl -s -o /dev/null "http://127.0.0.1:$LIBPORT/library/index.json" && break; sleep 0.2; done

call() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$APIPORT/api/dev/library/$1" -d "$2"; }
has()  { echo "$1" | grep -q -- "$2"; }
EXT="$TMP/data/ext"

r=$(call index '{}');                                   has "$r" '"id":"code-review"' && ok "index lists the catalog" || bad "index: $r"
r=$(call install '{"id":"code-review"}');               has "$r" '"ok":true' && ok "install skill" || bad "install skill: $r"
[ -f "$EXT/skills/code-review/SKILL.md" ] && ok "skill files written" || bad "skill files missing"
r=$(call install '{"id":"git-flow"}');                  has "$r" '"ok":true' && ok "install plugin" || bad "install plugin: $r"
[ -f "$EXT/plugins/git-flow/.claude-plugin/plugin.json" ] && grep -q '"git-flow":true' "$EXT/plugins.json" && ok "plugin written + enabled" || bad "plugin files/state"
r=$(call install '{"id":"deepwiki"}');                  has "$r" '"ok":true' && grep -q 'mcp.deepwiki.com' "$EXT/mcp.json" && ok "install remote MCP" || bad "remote MCP: $r"
r=$(call install '{"id":"github"}');                    has "$r" 'required' && ok "required token enforced" || bad "required token: $r"
r=$(call install '{"id":"github","inputs":{"GITHUB_TOKEN":"ghp_test123"}}'); has "$r" '"ok":true' && grep -q 'Bearer ghp_test123' "$EXT/mcp.json" && ok "header input stored locally" || bad "header input: $r"
r=$(call install '{"id":"filesystem","inputs":{"FOLDER":"/etc & rm -rf /"}}'); has "$r" 'not allowed' && ok "shell syntax in arg refused" || bad "arg injection: $r"
r=$(call install '{"id":"filesystem","inputs":{"FOLDER":"'"$TMP"'/nope"}}'); has "$r" 'existing folder' && ok "missing folder refused" || bad "folder check: $r"
mkdir -p "$TMP/proj"
r=$(call install '{"id":"filesystem","inputs":{"FOLDER":"'"$TMP"'/proj"}}'); has "$r" '"ok":true' && grep -q '"server-filesystem@\|@modelcontextprotocol/server-filesystem@' "$EXT/mcp.json" && ok "pinned stdio MCP with folder" || bad "filesystem: $r"
r=$(call install '{"id":"../../etc"}');                 has "$r" 'Unknown library item' && ok "bad id refused" || bad "bad id: $r"
r=$(call install '{"id":"not-there"}');                 has "$r" 'not in the Astro AI Library' && ok "unknown id refused" || bad "unknown id: $r"
# A user-made skill with the same name is not overwritten silently.
mkdir -p "$EXT/skills/write-tests"; echo mine > "$EXT/skills/write-tests/SKILL.md"
r=$(call install '{"id":"write-tests"}');               has "$r" '"conflict":true' && [ "$(cat "$EXT/skills/write-tests/SKILL.md")" = mine ] && ok "user skill protected" || bad "conflict: $r"
# Tampered download: change a served file after the catalog was built.
echo "evil" >> "$TMP/site/library/files/skills/debug-detective/SKILL.md"
r=$(call install '{"id":"debug-detective"}');           has "$r" 'Checksum mismatch' && [ ! -e "$EXT/skills/debug-detective" ] && ok "tampered file refused, nothing written" || bad "tamper: $r"
ls "$EXT/.library-staging" 2>/dev/null | grep -q . && bad "staging left behind" || ok "no staging leftovers"
# Broken mcp.json is never overwritten.
cp "$EXT/mcp.json" "$TMP/mcp.bak"; echo "{ broken" > "$EXT/mcp.json"
r=$(call install '{"id":"memory"}');                    has "$r" 'mcp.json has an error' && [ "$(cat "$EXT/mcp.json")" = "{ broken" ] && ok "broken mcp.json left alone" || bad "broken mcp.json: $r"
cp "$TMP/mcp.bak" "$EXT/mcp.json"
r=$(call installed '{}');                               has "$r" '"git-flow"' && ok "receipts listed" || bad "installed: $r"
# Uninstall removes only our files.
echo "my notes" > "$EXT/plugins/git-flow/NOTES.md"
r=$(call uninstall '{"id":"git-flow"}');                has "$r" '"ok":true' && [ -f "$EXT/plugins/git-flow/NOTES.md" ] && [ ! -e "$EXT/plugins/git-flow/.claude-plugin" ] && ok "uninstall keeps user files" || bad "uninstall plugin: $r"
r=$(call uninstall '{"id":"code-review"}');             [ ! -e "$EXT/skills/code-review" ] && ok "uninstall skill" || bad "uninstall skill: $r"
r=$(call uninstall '{"id":"github"}');                  ! grep -q 'ghp_test123' "$EXT/mcp.json" && ok "uninstall MCP removes token" || bad "uninstall MCP: $r"
r=$(call uninstall '{"id":"write-tests"}');             has "$r" 'not installed from the library' && [ -f "$EXT/skills/write-tests/SKILL.md" ] && ok "cannot uninstall user items" || bad "uninstall user item: $r"
if [ "$(uname)" != "Darwin" ]; then
  perm=$(stat -c %a "$EXT/mcp.json"); [ "$perm" = 600 ] && ok "mcp.json is private (600)" || bad "mcp.json mode $perm"
fi

echo "library e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]

#!/usr/bin/env bash
# Checks the developer platform API (/api/dev/*): open folder, linked projects, scan, extensions, MCP, diagnostics.
# Usage: tests/dev_api_test.sh [port]   (server started with ASTRO_AUTH=off)
set -u
P=${1:-4189}
H=(-H Content-Type:application/json -H x-astro-client:1)
api(){ curl -s -m 120 -X POST "localhost:$P/api/$1" "${H[@]}" -d "$2"; }
fail=0
check(){ if echo "$2" | grep -q -- "$3"; then echo "ok   $1"; else echo "FAIL $1: $(echo "$2" | head -c 300)"; fail=1; fi; }
HERE="$(cd "$(dirname "$0")" && pwd)"
F=$(mktemp -d /tmp/astro-folder-XXXX)
mkdir -p "$F/.claude/skills/hello" "$F/.claude/commands" "$F/src"
printf '# Rules\nUse tabs.\n' > "$F/AGENTS.md"
printf -- '---\nname: hello\ndescription: Says hello\n---\nSay hello.\n' > "$F/.claude/skills/hello/SKILL.md"
printf 'Review $ARGUMENTS carefully.\n' > "$F/.claude/commands/review2.md"
printf 'function f( {\n' > "$F/src/bad.js"
printf 'def ok():\n  return 1\ndef bad(:\n  pass\n' > "$F/src/bad.py"
printf '{"a": 1,}' > "$F/broken.json"
printf 'keep me\n' > "$F/keep.txt"
cat > "$F/.mcp.json" <<JSON
{ "mcpServers": { "echo": { "command": "node", "args": ["$HERE/fixtures/mcp_echo.js"] } } }
JSON

r=$(api dev/fs/browse "{\"path\":\"$F\"}"); check "browse" "$r" '"name":"src"'
r=$(api dev/fs/open '{"path":"/"}'); check "refuse drive root" "$r" 'not the whole drive'
r=$(api dev/fs/open "{\"path\":\"$F\",\"name\":\"linked-test\"}"); check "open folder" "$r" '"name":"linked-test"'
r=$(api dev/fs/open "{\"path\":\"$F\"}"); check "reopen reuses" "$r" '"existing":true'
r=$(curl -s "localhost:$P/api/fs/projects" -H x-astro-client:1); check "projects list shows linked" "$r" "\"linked\":\"$F\""
r=$(api fs/tree '{"project":"linked-test"}'); check "tree of linked folder" "$r" 'keep.txt'
r=$(api fs/write '{"project":"linked-test","path":"src/new.txt","content":"from astro"}'); check "write into folder" "$r" '"ok":true'
[ "$(cat "$F/src/new.txt" 2>/dev/null)" = "from astro" ] && echo "ok   file landed in the real folder" || { echo "FAIL real folder write"; fail=1; }
r=$(api fs/read '{"project":"linked-test","path":"../../etc/passwd"}'); check "no escape" "$r" 'error'

r=$(api dev/scan '{"project":"linked-test","no_user":true}')
check "scan rule" "$r" '"path":"AGENTS.md"'
check "scan skill" "$r" '"kind":"skill"'
check "scan command" "$r" 'review2.md'

r=$(api dev/ext/write '{"path":"skills/demo/SKILL.md","content":"---\nname: demo\ndescription: d\n---\nbody"}'); check "ext write" "$r" '"ok":true'
r=$(api dev/ext/read '{"path":"skills/demo/SKILL.md"}'); check "ext read" "$r" 'name: demo'
r=$(api dev/ext/read '{"path":"../.astro_auth.json"}'); check "ext no escape" "$r" 'error'
r=$(api dev/ext/read '{"scope":"user","path":".ssh/id_rsa"}'); check "user scope limited" "$r" 'can be read'
r=$(api dev/scan '{"no_user":true}'); check "scan global skill" "$r" 'skills/demo/SKILL.md'
r=$(api dev/ext/delete '{"path":"skills/demo"}'); check "ext delete" "$r" '"ok":true'

r=$(api dev/mcp/list '{"project":"linked-test"}'); check "mcp listed untrusted" "$r" '"status":"untrusted"'
r=$(api dev/mcp/call '{"project":"linked-test","server":"echo","tool":"echo","arguments":{"text":"hi"}}'); check "untrusted call refused" "$r" 'not trusted'
r=$(api dev/mcp/trust '{"project":"linked-test","server":"echo"}'); check "trust" "$r" '"ok":true'
r=$(api dev/mcp/list '{"project":"linked-test","start":true}'); check "mcp running + tools" "$r" '"name":"add"'
r=$(api dev/mcp/call '{"project":"linked-test","server":"echo","tool":"echo","arguments":{"text":"hi"}}'); check "mcp call echo" "$r" 'echo: hi'
r=$(api dev/mcp/call '{"project":"linked-test","server":"echo","tool":"add","arguments":{"a":2,"b":40}}'); check "mcp call add" "$r" '"text":"42"'
r=$(api dev/mcp/stop '{"project":"linked-test","server":"echo"}'); check "mcp stop" "$r" '"status":"stopped"'
r=$(api dev/mcp/config_set '{"text":"{\"mcpServers\":{\"g\":{\"url\":\"https://example.com/mcp\"}}}"}'); check "global mcp config" "$r" '"ok":true'
r=$(api dev/mcp/list '{}'); check "global server listed" "$r" '"name":"g"'
api dev/mcp/config_set '{"text":"{\"mcpServers\":{}}"}' >/dev/null
r=$(api dev/mcp/config_set '{"text":"{nope"}'); check "bad config rejected" "$r" 'not valid JSON'

r=$(api dev/diagnostics '{"project":"linked-test"}')
check "diag js" "$r" '"path":"src/bad.js"'
check "diag py line 3" "$r" '"line":3,"message":"invalid syntax","path":"src/bad.py"'
check "diag json" "$r" 'Invalid JSON'

r=$(api dev/fetch '{"url":"https://evil.example.com/x"}'); check "fetch allowlist" "$r" 'limited to'

r=$(api fs/project/delete '{"project":"linked-test"}'); check "close linked project" "$r" '"ok":true'
[ -f "$F/keep.txt" ] && [ -f "$F/src/new.txt" ] && echo "ok   real folder untouched after close" || { echo "FAIL real folder was deleted"; fail=1; }
rm -rf "$F"
[ $fail = 0 ] && echo "Dev API tests passed." || { echo "Dev API tests FAILED."; exit 1; }

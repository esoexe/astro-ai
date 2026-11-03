#!/usr/bin/env bash
# Local model engines that share a port: Colibri and vLLM both default to 127.0.0.1:8000.
# Fake /v1/models servers stand in for them; the backend must give each provider only its own models
# (by "owned_by"), drop Colibri's helper models, send COLI_API_KEY when set, and explain what it found.
# Usage: bash tests/local_engines_e2e.sh      (needs node, jq and a built ./workspace_server)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TMP="$(mktemp -d /tmp/astro-local-e2e.XXXXXX)"
SHARED=4281 VONLY=4282 AUTH=4283 CLOSED=4284 API=4287
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }

cat > "$TMP/fake.js" <<'EOF'
// node fake.js <port> <mode>: shared = Colibri and vLLM models on one port, vonly = vLLM only, auth = Colibri behind a key
const http = require('http');
const [port, mode] = [+process.argv[2], process.argv[3]];
const coli = [{ id: 'deepseek-v4-flash-colibri', object: 'model', owned_by: 'colibri' },
              { id: 'laya-colibri', object: 'model', owned_by: 'colibri' },
              { id: 'gliner2.5-decide', object: 'model', owned_by: 'colibri' }];
const vllm = [{ id: 'Qwen/Qwen3-8B', object: 'model', owned_by: 'vllm' }];
http.createServer((q, s) => {
  if (q.url !== '/v1/models') { s.writeHead(404); return s.end('{}'); }
  if (mode === 'auth' && q.headers.authorization !== 'Bearer k-coli-test') { s.writeHead(401); return s.end('{"error":"key"}'); }
  const data = mode === 'shared' ? [...coli, ...vllm] : mode === 'vonly' ? vllm : coli;
  s.writeHead(200, { 'content-type': 'application/json' });
  s.end(JSON.stringify({ object: 'list', data, models: data.map((m) => m.id) }));
}).listen(port, '127.0.0.1');
EOF
node "$TMP/fake.js" $SHARED shared & F1=$!
node "$TMP/fake.js" $VONLY vonly & F2=$!
node "$TMP/fake.js" $AUTH auth & F3=$!

# The real Colibri and vLLM entries from providers.json, pointed at the fakes (plus copies for the other cases).
jq --arg s "http://127.0.0.1:$SHARED/v1" --arg v "http://127.0.0.1:$VONLY/v1" --arg a "http://127.0.0.1:$AUTH/v1" --arg c "http://127.0.0.1:$CLOSED/v1" '
  [.providers[] | select(.id == "colibri")][0] as $coli | [.providers[] | select(.id == "vllm")][0] as $vllm |
  { providers: [
      ($coli + { base_url: $s }), ($vllm + { base_url: $s }),
      ($coli + { id: "colibri-v", base_url: $v, base_url_key: "COLIBRI_V_URL" }),
      ($coli + { id: "colibri-auth", base_url: $a, base_url_key: "COLIBRI_A_URL" }),
      ($coli + { id: "colibri-off", base_url: $c, base_url_key: "COLIBRI_O_URL" }) ] }' providers.json > "$TMP/providers.json" \
  || { echo "could not build the test providers file"; exit 1; }
[ "$(jq '.providers | length' "$TMP/providers.json")" = 5 ] && ok "colibri and vllm entries exist in providers.json" || bad "providers.json entries"

mkdir -p "$TMP/data" "$TMP/home" "$TMP/cwd"
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$SHARED/v1/models" && curl -s -o /dev/null "http://127.0.0.1:$AUTH/v1/models" && break; sleep 0.1; done
(cd "$TMP/cwd" && HOME="$TMP/home" ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$API PROVIDERS_FILE="$TMP/providers.json" COLI_API_KEY=k-coli-test \
  exec "$ROOT/workspace_server") >"$TMP/server.log" 2>&1 & SP=$!
cleanup() { kill $F1 $F2 $F3 $SP 2>/dev/null; wait $F1 $F2 $F3 $SP 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 100); do curl -s -o /dev/null "http://127.0.0.1:$API/api/status" && break; sleep 0.2; done
S=$(curl -s -H 'X-Astro-Client: 1' "http://127.0.0.1:$API/api/status")
models() { echo "$S" | jq -c --arg p "$1" '[.models[] | select(.provider == $p) | .id]'; }
status() { echo "$S" | jq -r --arg p "$1" '.providers[] | select(.id == $p) | .status'; }

[ "$(models colibri)" = '["colibri:deepseek-v4-flash-colibri"]' ] && ok "Colibri keeps only its own chat model" || bad "colibri models: $(models colibri)"
[ "$(models vllm)" = '["vllm:Qwen/Qwen3-8B"]' ] && ok "vLLM on the same port skips Colibri's models" || bad "vllm models: $(models vllm)"
echo "$S" | jq -e '.models[] | select(.provider == "colibri") | select(.local == true and .parallel == 1)' >/dev/null && ok "Colibri route is local, one request at a time" || bad "colibri route flags"
[ "$(models colibri-v)" = '[]' ] && status colibri-v | grep -q "Another local engine is using http://127.0.0.1:$VONLY/v1 (set COLIBRI_V_URL" && ok "vLLM-only port: Colibri says another engine is there" || bad "colibri-v: $(models colibri-v) / $(status colibri-v)"
[ "$(models colibri-auth)" = '["colibri-auth:deepseek-v4-flash-colibri"]' ] && ok "COLI_API_KEY is sent as a Bearer token" || bad "colibri-auth: $(models colibri-auth) / $(status colibri-auth)"
[ "$(models colibri-off)" = '[]' ] && status colibri-off | grep -q "Not running on this computer" && ok "Colibri not running is explained" || bad "colibri-off: $(status colibri-off)"
status colibri | grep -q "Connected: 1 model" && ok "Colibri status: connected" || bad "colibri status: $(status colibri)"
echo "$S" | jq -r '.providers[] | select(.id == "colibri") | .privacy' | grep -q "Stays on your computer" && ok "privacy label" || bad "privacy: $(echo "$S" | jq -c '.providers[] | select(.id == "colibri")')"

echo "local engines e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]

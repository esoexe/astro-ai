#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-4181}"
BASE="http://127.0.0.1:${PORT}"
cd "$ROOT"
DATA_TEST="$(mktemp -d)"
unset LLM_BASE_URL LLM_API_KEY LLM_MODEL LLM_CREDENTIALS_URL FALLBACK_BASE_URL FALLBACK_MODEL OPENCODE_URL GROQ_API_KEY OPENROUTER_API_KEY || true
export PROVIDERS_FILE="$ROOT/tests/providers_test.json"
ASTRO_AUTH=off ASTRO_DATA_DIR="$DATA_TEST" PORT="$PORT" ./workspace_server >/tmp/workspace-cpp-test.log 2>&1 &
SERVER_PID=$!
AUTH_PID=""
cleanup() { kill "$SERVER_PID" $AUTH_PID 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$DATA_TEST"; }
trap cleanup EXIT
for _ in $(seq 1 40); do
  if curl -fsS "$BASE/api/status" >/dev/null 2>&1; then break; fi
  sleep 0.1
done

curl -fsS "$BASE/" | grep -q 'Ask Astro'
curl -fsS "$BASE/api/status" | grep -q '"mode":"demo"'
# Brain registry: every platform from providers.json is listed, none connected without keys.
curl -fsS "$BASE/api/status" | grep -q '"secret":"GROQ_API_KEY"'
curl -fsS "$BASE/api/status" | grep -q '"status":"Add secret OPENROUTER_API_KEY"'
curl -fsS -X POST -H 'Content-Type: application/json' \
  -d '{"expression":"(125 * 1.08) / 4"}' "$BASE/api/calc" | grep -q '33.75'
if [ "$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
  -d '{"expression":"__import__(1)"}' "$BASE/api/calc")" != "400" ]; then
  echo 'Unsafe calculator input was not rejected' >&2; exit 1
fi
curl -fsS -X POST -H 'Content-Type: application/json' \
  -d '{"messages":[{"role":"user","content":"hello"}],"mode":"general","language":"English"}' \
  "$BASE/api/chat" | grep -q 'Astro AI is in demo mode'

TMPDIR_TEST="$(mktemp -d)"
mkdir -p "$TMPDIR_TEST/word"
printf '%s' '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Native C++ DOCX test</w:t></w:r></w:p></w:body></w:document>' > "$TMPDIR_TEST/word/document.xml"
(cd "$TMPDIR_TEST" && zip -q -r document.docx word/document.xml)
DOCX_B64="$(base64 -w0 "$TMPDIR_TEST/document.docx")"
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data "{\"filename\":\"document.docx\",\"data_base64\":\"$DOCX_B64\"}" \
  "$BASE/api/extract" | grep -q 'Native C++ DOCX test'
BRAIN_OUT=$(curl -fsS -N -X POST -H 'Content-Type: application/json' --data '{"stage":"expert","models":["a","b"],"messages":[{"role":"user","content":"hi"}]}' "$BASE/api/brain/stream")
echo "$BRAIN_OUT" | grep -q '"mind":1,"ev":{"delta"'
echo "$BRAIN_OUT" | grep -q '"all_done":true'
# Astro AI extensions
curl -fsS "$BASE/api/auth/state" | grep -q '"enabled":false'
curl -fsS "$BASE/api/keys" | grep -q '"key":"GROQ_API_KEY"'
curl -fsS "$BASE/api/usage" | grep -q '"providers"'
code=$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"url":"http://127.0.0.1:4097/doc"}' "$BASE/api/web/read")
[ "$code" = "422" ] || { echo "Private address was not blocked ($code)" >&2; exit 1; }
curl -fsS -X POST -H 'Content-Type: application/json' -d '{"updated":5,"data":{"notes":[1]}}' "$BASE/api/sync" | grep -q '"ok":true'
curl -fsS "$BASE/api/sync" | grep -q '"notes":\[1\]'
code=$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"updated":1,"data":{}}' "$BASE/api/sync")
[ "$code" = "409" ] || { echo "Older sync data was not rejected ($code)" >&2; exit 1; }
curl -fsS "$BASE/manifest.webmanifest" | grep -q 'Astro AI'
[ "$(curl -sS -o /dev/null -w '%{http_code}' "$BASE/.astro_keys.env")" = "404" ]
# Login: a second server with the password lock on.
AUTH_DATA="$DATA_TEST/auth"; mkdir -p "$AUTH_DATA"
APORT=$((PORT + 1)); ABASE="http://127.0.0.1:$APORT"
ASTRO_DATA_DIR="$AUTH_DATA" PORT="$APORT" ./workspace_server >/tmp/workspace-cpp-auth.log 2>&1 &
AUTH_PID=$!
for _ in $(seq 1 40); do curl -fsS "$ABASE/api/auth/state" >/dev/null 2>&1 && break; sleep 0.1; done
curl -fsS "$ABASE/api/auth/state" | grep -q '"setup":true'
[ "$(curl -sS -o /dev/null -w '%{http_code}' "$ABASE/api/status")" = "401" ]
TOKEN=$(curl -fsS -X POST -H 'Content-Type: application/json' -d '{"password":"secret-test-1"}' "$ABASE/api/auth/setup" | sed -E 's/.*"token":"([0-9a-f]+)".*/\1/')
[ ${#TOKEN} -eq 64 ] || { echo "No session token" >&2; exit 1; }
curl -fsS -H "Authorization: Bearer $TOKEN" "$ABASE/api/status" | grep -q '"providers"'
# Proxy-proof sessions: a foreign Bearer value (proxy token) must not hide x-astro-token, and the cookie alone works.
curl -fsS -H "Authorization: Bearer proxy-replaced-0123456789abcdef0123456789abcdef" -H "X-Astro-Token: $TOKEN" "$ABASE/api/status" | grep -q '"providers"'
curl -fsS -H "Cookie: theme=dark; astro_session=$TOKEN" "$ABASE/api/status" | grep -q '"providers"'
curl -fsS -H "X-Astro-Token: $TOKEN" "$ABASE/api/auth/state" | grep -q '"ok":true'
curl -fsS -D - -o /dev/null -X POST -H 'Content-Type: application/json' -d '{"password":"secret-test-1"}' "$ABASE/api/auth/login" | grep -qi '^set-cookie: astro_session=[0-9a-f]\{64\}; Path=/; Max-Age=2592000; HttpOnly'
[ "$(curl -sS -o /dev/null -w '%{http_code}' -H "Cookie: astro_session=0000000000000000000000000000000000000000000000000000000000000000" "$ABASE/api/status")" = "401" ]
[ "$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' -d '{"password":"nope"}' "$ABASE/api/auth/login")" = "401" ]
curl -fsS -X POST -H 'Content-Type: application/json' -d '{"password":"secret-test-1"}' "$ABASE/api/auth/login" | grep -q '"token"'
[ "$(stat -c %a "$AUTH_DATA/.astro_auth.json")" = "600" ]
rm -rf "$TMPDIR_TEST"
echo 'Astro AI smoke tests passed (core, extensions, login).'

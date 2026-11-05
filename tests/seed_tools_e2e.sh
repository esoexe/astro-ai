#!/usr/bin/env bash
# Bundled tools (the personal build's seed-tools/, e.g. the natural voice): installed once on first start, usable at
# once, removable, and not reinstalled after the user removes them.
#   bash tests/seed_tools_e2e.sh [SEED_DIR]     default .cache/seed-tools/linux-x64 (skipped when missing)
# Make one with: node tools/fetch_seed_tools.js linux-x64 .cache/seed-tools/linux-x64 piper piper-voice-fr
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"; SEED="$(realpath "${1:-.cache/seed-tools/linux-x64}" 2>/dev/null)"
if [ ! -f "$SEED/seed.json" ]; then echo "seed tools: skipped (no seed at ${1:-.cache/seed-tools/linux-x64})"; exit 0; fi
PORT=4294; TMP=$(mktemp -d); PASS=0; FAIL=0; SRV=""
ok() { PASS=$((PASS + 1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL $1"; }
cleanup() { [ -n "$SRV" ] && kill "$SRV" 2>/dev/null; wait 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
start() {
    ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$PORT ASTRO_SEED_TOOLS_DIR="$SEED" "$ROOT/workspace_server" >>"$TMP/server.log" 2>&1 & SRV=$!
    for _ in $(seq 1 100); do curl -s -o /dev/null "http://127.0.0.1:$PORT/" && return 0; sleep 0.2; done
    echo "server did not start"; tail -20 "$TMP/server.log"; exit 1
}
stop() { kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null; SRV=""; }
api() { curl -s -H 'x-astro-client: 1' -H 'Content-Type: application/json' -X POST "http://127.0.0.1:$PORT$1" -d "${2:-{\}}" "${@:3}"; }
mkdir -p "$TMP/data"

start
M="$TMP/data/tools/installed.json"
[ "$(jq -r '.piper.bundled' "$M" 2>/dev/null)" = true ] && ok "first start installs the bundled voice" || bad "piper not recorded: $(cat "$M" 2>/dev/null | head -c 300)"
[ -x "$(jq -r '.piper.bindirs[0]' "$M")/piper" ] && ok "piper is in its tools folder" || bad "piper binary missing"
[ "$(jq -r '.piper.verified' "$M")" = sha256 ] && ok "recorded as checksum-verified" || bad "verified field"
for id in $(jq -r '.tools | keys[]' "$SEED/seed.json"); do
    [ -d "$TMP/data/tools/$id" ] && ok "$id copied" || bad "$id not copied"
done
ST=$(api /api/dev/tts/status)
[ "$(jq -r .available <<<"$ST")" = true ] && ok "natural voice available at once" || bad "tts status: $ST"
jq -e '.voices[] | select(.name == "en_US-ljspeech-medium")' <<<"$ST" >/dev/null && ok "English voice listed" || bad "no English voice: $ST"
if jq -e '.tools["piper-voice-fr"]' "$SEED/seed.json" >/dev/null; then
    jq -e '.voices[] | select(.name == "fr_FR-siwis-medium")' <<<"$ST" >/dev/null && ok "French voice listed" || bad "no French voice: $ST"
fi
api /api/dev/tts/speak '{"text":"Hello, this is Astro A.I. speaking offline.","lang":"en"}' -o "$TMP/hello.wav"
[ "$(head -c 4 "$TMP/hello.wav")" = RIFF ] && [ "$(wc -c < "$TMP/hello.wav")" -gt 20000 ] && ok "speaks: a WAV of $(wc -c < "$TMP/hello.wav") bytes" || bad "speak: $(head -c 200 "$TMP/hello.wav")"
L=$(api /api/dev/tools/list)
[ "$(jq -r '[.. | objects | select(.id? == "piper")][0].installed // empty' <<<"$L")" != "" ] && ok "Tools list knows piper" || bad "tools list: $(head -c 300 <<<"$L")"
stop

# A restart changes nothing; a removed tool stays removed.
start
[ "$(grep -c . "$TMP/data/tools/.seeded")" = "$(jq '.tools | length' "$SEED/seed.json")" ] && ok "each tool seeded once" || bad ".seeded: $(cat "$TMP/data/tools/.seeded")"
R=$(api /api/dev/tools/remove '{"id":"piper"}')
[ ! -d "$TMP/data/tools/piper" ] && ok "removable like any tool" || bad "remove: $R"
stop
start
[ ! -d "$TMP/data/tools/piper" ] && [ "$(jq -r '.piper // "gone"' "$M")" = gone ] && ok "not reinstalled after removal" || bad "piper came back"
stop

# A tool the user installed already is left alone.
rm -rf "$TMP/data"; mkdir -p "$TMP/data/tools/piper"; echo '{"piper":{"version":"mine","dir":"x","bindirs":[]}}' > "$TMP/data/tools/installed.json"
start
[ "$(jq -r .piper.version "$TMP/data/tools/installed.json")" = mine ] && ok "an existing install is kept" || bad "existing install overwritten"
stop

echo "seed tools: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]

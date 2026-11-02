#!/usr/bin/env bash
# Built-in pack tools end to end: the library installs an Astro Pack whose helpers are C++ inside the engine
# (scripts: "builtin"), the scan tells the browser agent how to call them, and the installed skill's helper runs
# through "<engine> tool --skill-dir <installed skill>" with its bundled default files.
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TMP="$(mktemp -d /tmp/astro-pt-e2e.XXXXXX)"
LIBPORT=4193 APIPORT=4198
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }
has() { echo "$1" | grep -q -- "$2"; }

node library/build_library.js "$TMP/site/library" >/dev/null || { echo "library build failed"; exit 1; }
cat > "$TMP/serve.js" <<'JS'
const http = require('http'), fs = require('fs'), path = require('path');
const root = process.argv[2];
http.createServer((q, s) => {
  const p = path.join(root, decodeURIComponent(q.url.split('?')[0]));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { s.writeHead(404); return s.end('no'); }
  s.writeHead(200); s.end(fs.readFileSync(p));
}).listen(+process.argv[3], '127.0.0.1');
JS
node "$TMP/serve.js" "$TMP/site" $LIBPORT & SRV=$!
mkdir -p "$TMP/data"
ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$APIPORT ASTRO_LIBRARY_URL="http://127.0.0.1:$LIBPORT/library/index.json" \
  "$ROOT/workspace_server" >"$TMP/server.log" 2>&1 & API=$!
cleanup() { kill $SRV $API 2>/dev/null; wait $SRV $API 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$APIPORT/" && curl -s -o /dev/null "http://127.0.0.1:$LIBPORT/library/index.json" && break; sleep 0.2; done
post() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$APIPORT$1" -d "$2"; }

idx=$(cat "$TMP/site/library/index.json")
[ "$(echo "$idx" | jq -r '.items[] | select(.id=="astro-trader") | .scripts')" = builtin ] && ok "catalog: astro-trader scripts=builtin" || bad "catalog scripts"
[ "$(echo "$idx" | jq -r '.items[] | select(.id=="astro-trader") | .min_version')" = 1.9.0 ] && ok "catalog: min_version 1.9.0" || bad "catalog min_version"
echo "$idx" | jq -r '.items[] | select(.id=="astro-trader") | .files[].path' | grep -q 'trader-screener/screen.mjs' && bad "screen.mjs still shipped" || ok "no .mjs for the C++ helper"

ver=$(sed -n 's/^#define ASTRO_APP_VERSION "\(.*\)"/\1/p' platform.h)
r=$(post /api/dev/library/install '{"id":"astro-trader"}')
if [ "$(printf '%s\n1.9.0\n' "$ver" | sort -V | head -1)" != 1.9.0 ]; then
  has "$r" "needs A.S.T.R.O AI 1.9.0" && ok "older engine ($ver) refuses the pack" || bad "version gate: $r"
  echo "packtools e2e: $pass passed, $fail failed (engine $ver: install part skipped)"; [ $fail -eq 0 ]; exit
fi
has "$r" '"ok":true' && ok "install astro-trader" || { bad "install: $r"; tail -5 "$TMP/server.log"; }
SK="$TMP/data/ext/plugins/astro-trader/skills/trader-screener"
[ -f "$SK/SKILL.md" ] && [ -f "$SK/rules.example.json" ] && ok "skill + bundled rules installed" || bad "installed files: $(ls "$SK" 2>&1)"
grep -q '${ASTRO_TOOL} trader-screener/screen' "$SK/SKILL.md" && ok "SKILL.md calls \${ASTRO_TOOL}" || bad "SKILL.md command"

scan=$(post /api/dev/scan '{"project":""}')
tool=$(echo "$scan" | jq -r '.tool // empty')
[ "$tool" = "\"$ROOT/workspace_server\" tool" ] && ok "scan reports the tool command" || bad "scan tool: [$tool]"
echo "$scan" | jq -r '.files[].path' | grep -q 'plugins/astro-trader/skills/trader-screener/SKILL.md' && ok "scan finds the installed skill" || bad "scan skills: $(echo "$scan" | jq -c '[.files[].path] | .[:5]')"

# what the agent runs after ${ASTRO_TOOL} expansion (default rules come from the installed skill folder)
F="$ROOT/packtools/tests/fixtures/trader-screener"
out=$(cd "$TMP" && eval "$tool --skill-dir \"$SK\" trader-screener/screen \"$F/metrics.json\" --prices \"$F/prices.json\" --json"); rc=$?
[ $rc -eq 0 ] && [ "$(echo "$out" | jq -c '[.passed[].ticker]')" = '["GOOGL","KO"]' ] && ok "installed helper runs (GOOGL, KO)" || bad "helper run rc=$rc: $out"
# the Job Hunter Pack: Job Scout reads the boards.json installed with the skill, and searches the upstream feeds offline
r=$(post /api/dev/library/install '{"id":"astro-hunter"}')
has "$r" '"ok":true' && ok "install astro-hunter" || bad "install hunter: $r"
HS="$TMP/data/ext/plugins/astro-hunter/skills/hunter-scout"
grep -q '${ASTRO_TOOL} hunter-scout/jobs search' "$HS/SKILL.md" && [ -f "$HS/boards.json" ] && ok "hunter-scout installed with boards.json" || bad "hunter-scout files: $(ls "$HS" 2>&1)"
out=$(cd "$TMP" && eval "$tool --skill-dir \"$HS\" hunter-scout/jobs boards"); rc=$?
[ $rc -eq 0 ] && has "$out" "auto    NVIDIA      workday    nvidia" && ok "installed scout lists the bundled boards" || bad "scout boards rc=$rc: $(echo "$out" | tail -2)"
HF="$ROOT/packtools/tests/fixtures/hunter-scout"
out=$(cd "$TMP" && eval "$tool --skill-dir \"$HS\" hunter-scout/jobs search --offline \"$HF\" --profile \"$ROOT/packtools/tests/fixtures/hunter-apply/profile.example.json\" --today 2026-10-15 --company Discord --out \"\" --json"); rc=$?
[ $rc -eq 0 ] && [ "$(echo "$out" | jq -r '.jobs[0].id')" = discord-senior-software-engineer-ads-8697883002 ] && ok "installed scout ranks Discord offline" || bad "scout search rc=$rc: $out"
# the Master Pack: helpers live in a tools/ sub-folder upstream; the converted docs must call the built-in ids
r=$(post /api/dev/library/install '{"id":"astro-master"}')
has "$r" '"ok":true' && ok "install astro-master" || bad "install master: $r"
MP="$TMP/data/ext/plugins/astro-master/skills"
grep -q '${ASTRO_TOOL} master-build/sitecheck site/' "$MP/master-build/SKILL.md" && grep -q '${ASTRO_TOOL} master-virality/hookscore' "$MP/master-virality/SKILL.md" \
  && ok "master docs call \${ASTRO_TOOL} master-*/<helper>" || bad "master docs: $(grep -n 'sitecheck' "$MP/master-build/SKILL.md" | head -2)"
left=$(grep -rnE '[A-Za-z_-]+\.(py|mjs)\b' "$MP/master-build" "$MP/master-virality" "$MP/master-edit" --include=*.md | grep -v 'logic.test.mjs' | head -3)
[ -z "$left" ] && ok "no .py/.mjs helper references left in build/virality/edit" || bad "leftover refs: $left"
grep -q '${ASTRO_TOOL} master-edit/captions take.mov --words-only' "$MP/master-edit/SKILL.md" && ! grep -qiE 'python|faster-whisper|\$T/' "$MP/master-edit/SKILL.md" \
  && ok "master-edit docs: built-in tools, whisper.cpp, no Python" || bad "master-edit docs: $(grep -niE 'python|faster-whisper|\$T/' "$MP/master-edit/SKILL.md" | head -2)"
out=$(cd "$TMP" && eval "$tool master-edit/probe --doctor --json"); rc=$?
[ $rc -le 3 ] && [ "$(echo "$out" | jq -r 'has("engine") and has("whisper_cpp")')" = true ] && ok "master-edit/probe --doctor answers (rc $rc)" || bad "doctor rc=$rc: $(echo "$out" | head -c 200)"
if command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null; then
  ffmpeg -nostdin -v error -y -f lavfi -i "aevalsrc='0.3*sin(2*PI*220*t)*(between(t,0.5,1.5)+between(t,2.7,3.5))':s=48000:d=4" "$TMP/talk.wav"
  out=$(cd "$TMP" && eval "$tool master-edit/deadspace talk.wav -o \"$TMP/talk.cut.wav\" --json"); rc=$?
  [ $rc -eq 0 ] && [ -s "$TMP/talk.cut.wav" ] && [ "$(echo "$out" | jq '.cuts == 3 and .output_s < 2.5')" = true ] && ok "master-edit/deadspace cuts the pauses" || bad "deadspace rc=$rc: $(echo "$out" | head -c 300)"
else ok "master-edit render skipped (no ffmpeg)"; fi
mkdir -p "$TMP/web1"
cat > "$TMP/web1/index.html" <<'HTML'
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clean page</title><meta name="description" content="A small clean page."><meta property="og:image" content="https://example.org/share.png">
<link rel="icon" href="data:,"></head><body><h1>Hello</h1><p>All good here.</p><a href="#top" id="top">Top</a></body></html>
HTML
out=$(cd "$TMP" && eval "$tool master-build/sitecheck \"$TMP/web1\""); rc=$?
[ $rc -eq 0 ] && has "$out" "PASS: 0 errors, 0 warnings" && ok "sitecheck: clean folder passes" || bad "sitecheck clean rc=$rc: $(echo "$out" | tail -3)"
printf '<!doctype html><html><head><title>x</title></head><body><a href="gone.html">x</a></body></html>' > "$TMP/web1/index.html"
out=$(cd "$TMP" && eval "$tool master-build/sitecheck \"$TMP/web1\" --json"); rc=$?
[ $rc -eq 1 ] && [ "$(echo "$out" | jq -r '[.findings[] | select(.rule=="broken-link")] | length')" = 1 ] && ok "sitecheck: broken link found (exit 1)" || bad "sitecheck broken rc=$rc: $(echo "$out" | head -c 300)"
printf 'We delve into a rich tapestry of ideas \xe2\x80\x94 truly.\n' > "$TMP/draft.txt"
out=$(cd "$TMP" && eval "$tool --skill-dir \"$MP/master-virality\" master-virality/humanize draft.txt"); rc=$?
[ $rc -eq 0 ] && ! has "$out" "tapestry" && ! has "$out" $'\xe2\x80\x94' && ok "installed humanize uses its bundled lexicon" || bad "humanize rc=$rc: $out"
# the Closer Pack (1.9.1): installs with its example board, and its C++ helpers read the installed files
[ "$(echo "$idx" | jq -r '.items[] | select(.id=="astro-closer") | .min_version')" = 1.9.1 ] && ok "catalog: astro-closer min_version 1.9.1" || bad "closer min_version"
r=$(post /api/dev/library/install '{"id":"astro-closer"}')
if [ "$(printf '%s\n1.9.1\n' "$ver" | sort -V | head -1)" != 1.9.1 ]; then
  has "$r" "needs A.S.T.R.O AI 1.9.1" && ok "older engine ($ver) refuses the Closer Pack" || bad "closer version gate: $r"
else
  has "$r" '"ok":true' && ok "install astro-closer" || bad "install closer: $r"
  CP="$TMP/data/ext/plugins/astro-closer/skills"
  grep -q '${ASTRO_TOOL} pipeline/deals' "$CP/pipeline/SKILL.md" && [ -f "$CP/pipeline/pipeline.example.json" ] && ok "pipeline skill installed with its example board" || bad "closer files: $(ls "$CP/pipeline" 2>&1)"
  out=$(cd "$TMP" && eval "$tool --skill-dir \"$CP/pipeline\" pipeline/deals --file \"$CP/pipeline/pipeline.example.json\" board --today 2026-10-29"); rc=$?
  [ $rc -eq 0 ] && has "$out" 'Open: $19,364 across 5 deals. Won to date: $2,500.' && ok "installed pipeline/deals reads the board" || bad "closer board rc=$rc: $(echo "$out" | tail -2)"
  out=$(cd "$TMP" && eval "$tool --skill-dir \"$CP/sales-coach\" sales-coach/coach card --objection price"); rc=$?
  [ $rc -eq 0 ] && has "$out" "Objection: price" && ok "installed sales-coach uses its bundled objections" || bad "coach card rc=$rc: $(echo "$out" | head -3)"
fi
out=$("$ROOT/workspace_server" tool nope 2>&1); rc=$?
[ $rc -eq 2 ] && has "$out" "unknown tool 'nope'" && ok "unknown tool exit 2" || bad "unknown tool: $rc $out"
echo "packtools e2e: $pass passed, $fail failed"; [ $fail -eq 0 ]

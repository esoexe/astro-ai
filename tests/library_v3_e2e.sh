#!/usr/bin/env bash
# End-to-end test of the schema 3 community catalog (the separate library site): the real writer
# (library/import/index_all.js --format v3 --fixture) builds a small catalog, the shared reader (web/lib3core.js)
# searches it, and the backend serves, unpacks and installs from it, falling back to schema 2 for older ids.
# Usage: bash tests/library_v3_e2e.sh      (needs node and a built ./workspace_server)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TMP="$(mktemp -d /tmp/astro-lib3-e2e.XXXXXX)"
LIBPORT=4291 APIPORT=4296
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }

node library/build_library.js "$TMP/site/library" >/dev/null || { echo "library build failed"; exit 1; }
node - "$TMP" <<'EOF'
const fs = require('fs'), path = require('path');
const tmp = process.argv[2];
const rows = { mcp: [], skill: [], plugin: [], tool: [] }, details = {};
const put = (kind, row, det) => { rows[kind].push(row); details[row[0]] = det; };
put('mcp', ['weather-now', 'Weather Now', 'Current weather and forecasts for any city', 'acme', 'n', 'weather,forecast', '1.0.0', 50],
  { k: 'mcp', n: 'Weather Now', i: { t: 'stdio', cmd: 'npx', args: ['-y', 'weather-now-mcp@1.0.0'], env: [] }, src: 'npm' });
put('mcp', ['github-tools', 'GitHub Tools', 'Issues and pull requests on GitHub', 'octo', 'h', 'git', '2.0.0', 900],
  { k: 'mcp', n: 'GitHub Tools', i: { t: 'http', url: 'https://example.com/mcp', headers: [] } });
put('mcp', ['weather-link', 'weather-station', 'Home weather station readings', 'diy', 'l', '', '', 3],
  { k: 'mcp', n: 'weather-station', web: 'https://github.com/diy/weather-station', src: 'github' });
put('mcp', ['spicy-thing', 'NSFW image maker', 'not allowed', 'x', 'e', '', '', 99999], { k: 'mcp', n: 'x' });
for (let i = 0; i < 7; i++) put('skill', [`skill-${i}`, `Skill number ${i}`, i === 3 ? 'Writes weather reports' : 'Does a thing', 'pub', 'g', '', '', i],
  { k: 'skill', n: `skill-${i}`, repo: 'acme/skills', c: 'a'.repeat(40), dir: `s${i}`, f: [['SKILL.md', 'b'.repeat(64), 10]] });
put('tool', ['some-app', 'Some App', 'An AI app', 'example.com', 'l', 'apps', 'example.com', 0], { k: 'tool', t: 'link', n: 'Some App', url: 'https://example.com' });
fs.writeFileSync(path.join(tmp, 'fixture.json'), JSON.stringify({ rows, details }));
// a schema 2 catalog with one id that the schema 3 catalog doesn't have (older builds, fallback)
const crypto = require('crypto'); const sha = (s) => crypto.createHash('sha256').update(s).digest('hex');
const v2 = path.join(tmp, 'site/library/v2'); fs.mkdirSync(path.join(v2, 'detail'), { recursive: true });
fs.writeFileSync(path.join(v2, 'detail', sha('only-in-v2').slice(0, 2) + '.json'), JSON.stringify({ 'only-in-v2': { k: 'mcp', n: 'Old', i: { t: 'http', url: 'https://old.example.com/mcp', headers: [] } } }));
fs.writeFileSync(path.join(v2, 'manifest.json'), JSON.stringify({ schema: 2, total: 0, kinds: {} }));
EOF
out=$(node library/import/index_all.js "$TMP/site/library/v3" --format v3 --shard3 3 --fixture "$TMP/fixture.json" --reviewed "$TMP/site/library/index.json" 2>&1) || { echo "$out"; echo "writer failed"; exit 1; }
V3="$TMP/site/library/v3"
[ -f "$V3/manifest.json" ] && [ -f "$V3/list/skill-2.json.gz" ] && [ -f "$V3/state.json.gz" ] && ok "writer made gzip shards of 3 rows" || bad "writer output: $(ls -R "$V3" | head -20)"
[ "$(ls "$V3/detail" | wc -l)" -eq 4096 ] && ok "4096 detail buckets" || bad "detail buckets: $(ls "$V3/detail" | wc -l)"
echo "$out" | grep -q '; 1 unsafe entries left out' && ok "unsafe item left out" || bad "unsafe filter: $out"

# the shared reader, straight from the files
node - "$V3" "$ROOT/web/lib3core.js" <<'EOF' > "$TMP/reader.txt" 2>&1
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const [dir, core] = process.argv.slice(2);
const L = require(core);
const read = async (f) => { const b = fs.readFileSync(path.join(dir, f)); return JSON.parse((f.endsWith('.gz') ? zlib.gunzipSync(b) : b).toString()); };
(async () => {
  const r = new L.Reader(read); const m = await r.open();
  const say = (k, v) => console.log(k + '=' + JSON.stringify(v));
  const nr = (row) => !row[4].includes('r');   // the reviewed items are in the catalog too; leave them out here
  say('schema', m.schema); say('mcp', r.count('mcp')); say('skill', r.count('skill'));
  say('reviewedFirst', (await r.browse('mcp', 1))[0].row[4].includes('r'));
  const s1 = await r.search('weather', 'all', 10, nr); say('weather', s1.items.map((x) => x.row[0]).sort());
  const s2 = await r.search('weat', 'mcp', 10, nr); say('prefix', s2.items.map((x) => x.row[0]).sort());
  const s3 = await r.search('weather forecasts', 'all', 10, nr); say('two', s3.items.map((x) => x.row[0]));
  const s4 = await r.search('weather', 'skill', 10, nr); say('kind', s4.items.map((x) => x.row[0]));
  const s5 = await r.search('zzzqqq', 'all', 10); say('none', s5.total);
  const b = await r.browse('skill', 5, nr); say('browse', b.map((x) => x.row[0]));
  const n = await r.search('weather', 'mcp', 10, (row) => nr(row) && !row[4].includes('l')); say('filter', n.items.map((x) => x.row[0]));
  say('nsfw', (await r.search('nsfw', 'all', 5)).total);
  say('codec', L.decodePostings(L.encodePostings([0, 1, 5, 40, 41, 1000])));
})().catch((e) => { console.log('ERR ' + e.stack); });
EOF
R=$(cat "$TMP/reader.txt")
g() { echo "$R" | grep "^$1=" | cut -d= -f2-; }
[ "$(g schema)" = "3" ] && ok "reader opens the manifest" || bad "reader: $R"
[ "$(g mcp)" -ge 3 ] && [ "$(g skill)" -ge 7 ] && ok "kind counts" || bad "counts: mcp=$(g mcp) skill=$(g skill)"
[ "$(g reviewedFirst)" = "true" ] && ok "reviewed items rank first" || bad "rank: $(g reviewedFirst)"
[ "$(g weather)" = '["skill-3","weather-link","weather-now"]' ] && ok "search across kinds" || bad "search: $(g weather)"
[ "$(g prefix)" = '["weather-link","weather-now"]' ] && ok "prefix search" || bad "prefix: $(g prefix)"
[ "$(g two)" = '["weather-now"]' ] && ok "every word must match" || bad "two words: $(g two)"
[ "$(g kind)" = '["skill-3"]' ] && ok "kind filter" || bad "kind: $(g kind)"
[ "$(g none)" = "0" ] && ok "no match is empty" || bad "none: $(g none)"
[ "$(g browse)" = '["skill-6","skill-5","skill-4","skill-3","skill-2"]' ] && ok "browse in rank order across shards" || bad "browse: $(g browse)"
[ "$(g filter)" = '["weather-now"]' ] && ok "row filter" || bad "filter: $(g filter)"
[ "$(g nsfw)" = "0" ] && ok "left-out item is not searchable" || bad "nsfw: $(g nsfw)"
[ "$(g codec)" = '[0,1,5,40,41,1000]' ] && ok "posting codec round trip" || bad "codec: $(g codec)"

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
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$APIPORT/" && curl -s -o /dev/null "http://127.0.0.1:$LIBPORT/library/v3/manifest.json" && break; sleep 0.2; done

call() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$APIPORT/api/dev/library/$1" -d "$2"; }
has()  { echo "$1" | grep -q -- "$2"; }
EXT="$TMP/data/ext"

r=$(call v3 '{"file":"manifest.json"}');             has "$r" '"schema": *3' && ok "v3 manifest served through the backend" || bad "manifest: $r"
r=$(call v3 '{"file":"list/mcp-0.json.gz"}');        has "$r" '"rows"' && ok "gzip shard unpacked by the backend" || bad "shard: $(echo "$r" | head -c 200)"
r=$(call v3 '{"file":"q/we.json.gz"}');              has "$r" '"weather"' && ok "search index file served" || bad "q file: $(echo "$r" | head -c 200)"
r=$(call v3 '{"file":"list/mcp-0.json"}');           has "$r" 'Unknown catalog file' && ok "v3 file names are checked" || bad "name check: $r"
r=$(call v3 '{"file":"../state.json.gz"}');          has "$r" 'Unknown catalog file' && ok "no path tricks" || bad "path: $r"
r=$(call v3 '{"file":"state.json.gz"}');             has "$r" 'Unknown catalog file' && ok "indexer state is not served" || bad "state: $r"
r=$(call detail '{"id":"weather-now"}');             has "$r" 'weather-now-mcp@1.0.0' && ok "detail from the v3 catalog" || bad "detail v3: $r"
r=$(call detail '{"id":"only-in-v2"}');              has "$r" 'old.example.com' && ok "falls back to the v2 catalog" || bad "detail v2: $r"
r=$(call install '{"id":"weather-now"}');            has "$r" '"ok":true' && grep -q 'weather-now-mcp@1.0.0' "$EXT/mcp.json" && ok "pinned npx MCP installed from v3" || bad "install: $r"
r=$(call install '{"id":"github-tools"}');           has "$r" '"ok":true' && grep -q 'https://example.com/mcp' "$EXT/mcp.json" && ok "remote MCP installed from v3" || bad "remote: $r"
r=$(call install '{"id":"weather-link"}');           has "$r" 'no one-click setup\|not something to install' && ok "link-only MCP is not installable" || bad "link: $r"
r=$(call install '{"id":"spicy-thing"}');            has "$r" 'not in the A.S.T.R.O AI Library' && ok "left-out item can't be installed" || bad "unsafe install: $r"
r=$(call uninstall '{"id":"weather-now"}');          has "$r" '"ok":true' && ! grep -q 'weather-now' "$EXT/mcp.json" && ok "uninstalled" || bad "uninstall: $r"
ls "$TMP/data/.astro_library_cache/" 2>/dev/null | grep -q '^v3_list_mcp-0.json.gz$' && ok "v3 files cached on disk" || bad "cache: $(ls "$TMP/data/.astro_library_cache/" 2>/dev/null | head)"

echo "library v3 e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]

#!/usr/bin/env bash
# Offline test of the community-catalog indexer's extra sources (npm, Hugging Face Spaces, Docker's MCP catalog,
# Open VSX, GitHub topic search): fake APIs on 127.0.0.1 and a local git repo stand in for the real services.
# Usage: bash tests/indexer_sources_e2e.sh      (needs node and git)
set -u
cd "$(dirname "$0")/.."
TMP="$(mktemp -d /tmp/astro-idx-e2e.XXXXXX)"
PORT=4291
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }
cleanup() { [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT

# Docker's catalog as a local git repo
mkdir -p "$TMP/src/servers/weather" "$TMP/src/servers/remote-thing" "$TMP/src/servers/dupe"
printf 'name: weather\nimage: mcp/weather\ntype: server\nmeta:\n  category: data\nabout:\n  title: Weather (Docker)\n  description: Forecasts in a container\nsource:\n  project: https://github.com/dock/weather\n' > "$TMP/src/servers/weather/server.yaml"
printf 'name: remote-thing\ntype: remote\nabout:\n  title: Remote Thing\n  description: A hosted server\n' > "$TMP/src/servers/remote-thing/server.yaml"
printf 'name: dupe\nimage: mcp/dupe\nabout:\n  title: Dupe\n  description: Listed by the registry already\n' > "$TMP/src/servers/dupe/server.yaml"
( cd "$TMP/src" && git init -q && git add -A && git -c user.name=t -c user.email=t@t commit -qm init ) || { echo "git setup failed"; exit 1; }
mkdir -p "$TMP/git/docker" && git clone -q --bare "$TMP/src" "$TMP/git/docker/mcp-registry.git"

# Official registry: one npm server, one oci server
cat > "$TMP/registry.json" <<'EOF'
{ "servers": [
  { "server": { "name": "io.github.official/registry-server", "description": "From the registry", "version": "1.0.0",
    "repository": { "url": "https://github.com/official/registry-server" },
    "packages": [ { "registryType": "npm", "identifier": "registry-dup-mcp", "version": "1.0.0", "transport": { "type": "stdio" } } ] } },
  { "server": { "name": "io.github.dock/dupe", "description": "Docker one", "version": "2.0.0",
    "packages": [ { "registryType": "oci", "identifier": "docker.io/mcp/dupe", "version": "2.0.0", "transport": { "type": "stdio" } } ] } }
] }
EOF
echo "test-token" > "$TMP/token"
: > "$TMP/hits.log"
node tests/fixtures/indexer_fake.js $PORT "$TMP/hits.log" > "$TMP/fake.out" 2>&1 & SRV=$!
for _ in $(seq 1 50); do grep -q ready "$TMP/fake.out" 2>/dev/null && break; sleep 0.1; done

export ASTRO_IDX_DELAY_SCALE=0 ASTRO_IDX_NPM="http://127.0.0.1:$PORT/npm" ASTRO_IDX_HF="http://127.0.0.1:$PORT/hf" ASTRO_IDX_OVSX="http://127.0.0.1:$PORT/ovsx" ASTRO_IDX_GHAPI="http://127.0.0.1:$PORT/ghapi" ASTRO_IDX_GIT="file://$TMP/git/"
mkdir -p "$TMP/empty"
run_idx() {   # $1 = out dir, $2 = previous build dir
  node library/import/index_all.js "$1" --only mcp,npm,hf,docker,openvsx,ghmcp --registry-file "$TMP/registry.json" --prev "$2" \
    --token-file "$TMP/token" --search-min 1 --npm-queries 'keywords:mcp' --live "http://127.0.0.1:$PORT/none/" --cache "$TMP/cache" > "$1.log" 2>&1
}
run_idx "$TMP/out1" "$TMP/empty"; rc=$?
[ $rc = 0 ] && [ -f "$TMP/out1/manifest.json" ] && ok "indexer runs offline against the fake services" || { bad "indexer exit $rc"; tail -20 "$TMP/out1.log"; }

# Query helpers over the output
row()  { node -e 'const fs=require("fs"),p=process.argv[1];for(const f of fs.readdirSync(p+"/list")){for(const r of JSON.parse(fs.readFileSync(p+"/list/"+f)).rows) if(r[0]===process.argv[2]){console.log(JSON.stringify(r));process.exit(0)}}' "$1" "$2"; }
det()  { node -e 'const c=require("crypto"),id=process.argv[2];const h=c.createHash("sha256").update(id).digest("hex").slice(0,2);const d=require(process.argv[1]+"/detail/"+h+".json")[id];console.log(d?JSON.stringify(d):"")' "$1" "$2"; }
names() { node -e 'const fs=require("fs"),p=process.argv[1];const o=[];for(const f of fs.readdirSync(p+"/list"))for(const r of JSON.parse(fs.readFileSync(p+"/list/"+f)).rows)o.push(r[0]);console.log(o.join("\n"))' "$1"; }
O="$TMP/out1"; ALL="$(names "$O")"

# npm
r=$(row "$O" weather-mcp); d=$(det "$O" weather-mcp)
case "$r" in *'"n"'*'1.2.3'*) ok "npm server with a command listed, one-click";; *) bad "weather-mcp row: $r";; esac
case "$d" in *'"args":["-y","weather-mcp@1.2.3"]'*) ok "npm server pinned to its exact version";; *) bad "weather-mcp detail: $d";; esac
r=$(row "$O" acme-db-mcp); case "$r" in *'"n"'*) ok "scoped npm package with a string bin listed";; *) bad "acme-db-mcp: $r";; esac
echo "$ALL" | grep -q '^mcp-sdk-helpers$' && bad "library without a command was listed" || ok "npm library without a command left out"
echo "$ALL" | grep -q 'registry-dup' && bad "npm package already in the registry listed again" || { echo "$ALL" | grep -q '^official-registry-server$' && ok "npm package already in the registry listed once (from the registry)" || bad "registry entry missing"; }
echo "$ALL" | grep -q 'nsfw' && bad "unsafe npm package listed" || ok "unsafe npm package left out"

# Hugging Face
d=$(det "$O" weather-space-alice)
case "$d" in *'"t":"sse","url":"https://alice-weather-space.hf.space/gradio_api/mcp/sse"'*) ok "Gradio Space listed as a remote SSE server";; *) bad "weather-space: $d";; esac
r=$(row "$O" echo-tool-bob); d=$(det "$O" echo-tool-bob)
case "$d" in *'bob-echo-tool.hf.space'*) ok "second page followed; underscores mapped in the subdomain";; *) bad "echo-tool: $r $d";; esac
r=$(row "$O" static-page-alice); case "$r" in *'"l"'*) ok "non-Gradio Space is link only";; *) bad "static-page: $r";; esac
r=$(row "$O" "$(echo "$ALL" | grep '^a-very-long' | head -1)"); case "$r" in *'"l"'*) ok "long Space name (hashed subdomain) is link only";; *) bad "long space: $r";; esac
echo "$ALL" | grep -q 'undress' && bad "unsafe Space listed" || ok "unsafe Space left out"

# Docker catalog
r=$(row "$O" weather-docker); d=$(det "$O" weather-docker)
case "$r$d" in *'Weather (Docker)'*'hub.docker.com/r/mcp/weather'*) ok "Docker catalog server read from git";; *) bad "docker weather: $r $d";; esac
r=$(row "$O" remote-thing-docker); case "$r" in *'Remote Thing'*) ok "remote server from the Docker catalog";; *) bad "remote-thing: $r";; esac
echo "$ALL" | grep -q '^dupe-docker$' && bad "Docker image already in the registry listed again" || ok "Docker image in the registry not listed twice"

# Open VSX
r=$(row "$O" lint-all-acme-vsx); d=$(det "$O" lint-all-acme-vsx)
case "$r" in *'"lv"'*) ok "Open VSX extension flagged as an editor extension";; *) bad "lint-all row: $r";; esac
case "$d" in *'"t":"vsx"'*'"vsx":"acme.lint-all"'*) ok "extension detail has its Open VSX id";; *) bad "lint-all detail: $d";; esac
r=$(row "$O" peach-theme-peach-vsx); case "$r" in *Peach*) ok "category read (Themes)";; *) bad "peach theme: $r";; esac
echo "$ALL" | grep -q 'gone-old' && bad "deprecated extension listed" || ok "deprecated extension left out"

# GitHub topics
r=$(row "$O" tiny-mcp-zed); case "$r" in *'"l"'*'"GitHub",7'*) ok "MCP repo from topic search listed as a link with stars";; *) bad "tiny-mcp: $r";; esac
echo "$ALL" | grep -q '^weather-mcp-acme$' && bad "repo already covered by npm listed again" || ok "repo covered by an npm package not listed twice"
echo "$ALL" | grep -q 'registry-server-official' && bad "repo covered by the registry listed again" || ok "repo covered by the registry not listed twice"
echo "$ALL" | grep -q 'empty-zed\|forked' && bad "empty or forked repo listed" || ok "empty and forked repos left out"
grep -q 'topic%3Amcp-server' "$TMP/hits.log" && grep -q 'topic%3Aclaude-skills' "$TMP/hits.log" && ok "both topic groups searched" || bad "topic searches missing"

# Manifest and state
m=$(cat "$O/manifest.json")
case "$m" in *'"npm (packages tagged mcp that install a command)"'*'"Open VSX'*) ok "manifest lists the new sources";; *) bad "manifest sources";; esac
case "$m" in *'"filtered": 2'*) ok "manifest counts the unsafe entries left out";; *) bad "filtered count: $(grep -o '"filtered": [0-9]*' "$O/manifest.json")";; esac
node -e 'const j=require(process.argv[1]);process.exit(j.schema===1&&j.items["zed/tiny-mcp"]&&j.items["sk/skills-repo"]&&j.npm["weather-mcp"]&&j.npm["weather-mcp"].b===1&&j.sweep&&j.inc?0:1)' "$O/state-gh.json" && ok "state-gh.json keeps repos, npm cache and the sweep cursor" || bad "state-gh.json"

# Second build from the first: unchanged npm versions are not fetched again; the recent-push search runs.
: > "$TMP/hits.log"
run_idx "$TMP/out2" "$TMP/out1"; rc=$?
[ $rc = 0 ] && ok "second build from the saved state" || bad "second build exit $rc"
grep -q '^/npm/weather-mcp' "$TMP/hits.log" && bad "unchanged npm package fetched again" || ok "npm metadata cached by version"
grep -q 'pushed%3A' "$TMP/hits.log" && ok "recent-push search used after the first build" || bad "no recent-push search"
[ "$(row "$TMP/out2" tiny-mcp-zed)" = "$(row "$O" tiny-mcp-zed)" ] && ok "same row in both builds" || bad "rows differ between builds"

echo "indexer sources e2e: $pass passed, $fail failed"
[ $fail = 0 ]

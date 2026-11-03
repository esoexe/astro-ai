#!/usr/bin/env bash
# Creates or updates the esoexe/astro-library repository (the schema 3 community catalog site) from library/site/,
# turns on GitHub Pages (built by Actions) and, with --run, starts the indexing workflow.
#   tools/setup_library_site.sh [--run] [--fresh]
# Token: $GH_TOKEN_FILE (default ~/.config/astro-ai/gh_token). Never printed or stored in .git/config.
set -euo pipefail
cd "$(dirname "$0")/.."
RUN=0 FRESH=false
for a in "$@"; do case "$a" in --run) RUN=1;; --fresh) FRESH=true;; esac; done
REPO="${LIB_REPO:-astro-library}"
TOKEN_FILE="${GH_TOKEN_FILE:-$HOME/.config/astro-ai/gh_token}"
WORK="$HOME/.cache/gh"; mkdir -p "$WORK"; chmod 700 "$WORK"; umask 077
[ -s "$TOKEN_FILE" ] || { echo "missing token file $TOKEN_FILE"; exit 1; }
HDR="$WORK/auth.hdr"; printf 'Authorization: Bearer %s\n' "$(tr -d ' \r\n' < "$TOKEN_FILE")" > "$HDR"
ASK="$WORK/askpass.sh"
cleanup() { rm -f "$HDR" "$ASK"; }
trap cleanup EXIT
api() { curl -sS -H @"$HDR" -H 'Accept: application/vnd.github+json' -H 'X-GitHub-Api-Version: 2022-11-28' "$@"; }
LOGIN=$(api https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s);if(!j.login){console.error("token rejected: "+(j.message||""));process.exit(1)}console.log(j.login)})')
UID_=$(api https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>console.log(JSON.parse(s).id))')
echo "signed in as $LOGIN"

code=$(api -o /dev/null -w '%{http_code}' "https://api.github.com/repos/$LOGIN/$REPO")
if [ "$code" = 404 ]; then
  api -X POST https://api.github.com/user/repos -d "{\"name\":\"$REPO\",\"description\":\"Community catalog data for the A.S.T.R.O AI Library (MCP servers, skills, plugins, AI tools), rebuilt daily\",\"homepage\":\"https://$LOGIN.github.io/$REPO/\",\"has_issues\":false,\"has_wiki\":false,\"has_projects\":false,\"auto_init\":true}" > /dev/null
  echo "created $LOGIN/$REPO"; sleep 3
fi

printf '#!/bin/sh\ncase "$1" in Username*) echo x-access-token;; *) tr -d " \\r\\n" < "%s";; esac\n' "$TOKEN_FILE" > "$ASK"; chmod 700 "$ASK"
export GIT_ASKPASS="$ASK" GIT_TERMINAL_PROMPT=0
CLONE="$WORK/libsite"; rm -rf "$CLONE"
git clone -q "https://github.com/$LOGIN/$REPO.git" "$CLONE"
find "$CLONE" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -a library/site/. "$CLONE"/
cd "$CLONE"
git config user.name "$LOGIN"; git config user.email "$UID_+$LOGIN@users.noreply.github.com"
git add -A
if git diff --cached --quiet; then echo "repository already up to date"; else git commit -q -m "Update the library site from esoexe/astro-ai" && git push -q origin HEAD:main && echo "pushed $(git rev-parse --short HEAD)"; fi
cd - > /dev/null

# GitHub Pages, deployed by Actions
pages=$(api -o /dev/null -w '%{http_code}' "https://api.github.com/repos/$LOGIN/$REPO/pages")
if [ "$pages" = 404 ]; then api -X POST "https://api.github.com/repos/$LOGIN/$REPO/pages" -d '{"build_type":"workflow"}' > /dev/null && echo "Pages turned on"; else api -X PUT "https://api.github.com/repos/$LOGIN/$REPO/pages" -d '{"build_type":"workflow"}' > /dev/null || true; fi

if [ "$RUN" = 1 ]; then
  sleep 3
  r=$(api -o /dev/null -w '%{http_code}' -X POST "https://api.github.com/repos/$LOGIN/$REPO/actions/workflows/index.yml/dispatches" -d "{\"ref\":\"main\",\"inputs\":{\"fresh\":$FRESH}}")
  [ "$r" = 204 ] && echo "indexing started: https://github.com/$LOGIN/$REPO/actions" || echo "could not start the workflow (HTTP $r)"
fi

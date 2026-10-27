#!/usr/bin/env bash
# Pushes the current secret-free export as a new commit on top of the existing GitHub repo.
#   tools/update_github.sh "commit message"
# Token: $GH_TOKEN_FILE (default ~/.cache/gh/token, mode 600). Never printed or stored in .git/config.
set -euo pipefail
cd "$(dirname "$0")/.."
MSG="${1:-Update Astro AI}"
REPO="${REPO:-astro-ai}"
TOKEN_FILE="${GH_TOKEN_FILE:-$HOME/.cache/gh/token}"
WORK="$HOME/.cache/gh"; mkdir -p "$WORK"; chmod 700 "$WORK"; umask 077
[ -s "$TOKEN_FILE" ] || { echo "missing token file $TOKEN_FILE"; exit 1; }
HDR="$WORK/auth.hdr"; printf 'Authorization: Bearer %s\n' "$(tr -d ' \r\n' < "$TOKEN_FILE")" > "$HDR"
LOGIN=$(curl -sS -H @"$HDR" https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s);if(!j.login){console.error("token rejected: "+(j.message||""));process.exit(1)}console.log(j.login)})')
UID_=$(curl -sS -H @"$HDR" https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>console.log(JSON.parse(s).id))')
echo "signed in as $LOGIN"

STAGE="$WORK/export"; CLONE="$WORK/clone"
tools/export_repo.sh "$STAGE" "$LOGIN" | tail -1
node tools/scan_secrets.js "$STAGE" > /dev/null || { echo "secret scan failed; NOT pushing"; exit 1; }

ASK="$WORK/askpass.sh"; printf '#!/bin/sh\ncase "$1" in Username*) echo x-access-token;; *) tr -d " \\r\\n" < "%s";; esac\n' "$TOKEN_FILE" > "$ASK"; chmod 700 "$ASK"
export GIT_ASKPASS="$ASK" GIT_TERMINAL_PROMPT=0
rm -rf "$CLONE"; git clone -q "https://github.com/$LOGIN/$REPO.git" "$CLONE"
# mirror the export into the clone (deletions included), keeping .git
find "$CLONE" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -a "$STAGE"/. "$CLONE"/
cd "$CLONE"
git config user.name "$LOGIN"; git config user.email "$UID_+$LOGIN@users.noreply.github.com"
git add -A
if git diff --cached --quiet; then echo "nothing changed"; exit 0; fi
git diff --cached --stat | tail -15
git commit -q -m "$MSG"
git push -q origin HEAD:main
echo "pushed: https://github.com/$LOGIN/$REPO/commit/$(git rev-parse HEAD)"
rm -f "$HDR" "$ASK"

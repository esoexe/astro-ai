#!/usr/bin/env bash
# Pushes the current secret-free export as a new commit on top of the existing GitHub repo.
#   tools/update_github.sh "commit message"
# Token: $GH_TOKEN_FILE (default ~/.config/astro-ai/gh_token, mode 600; kept across sandbox resets). Never printed or stored in .git/config.
set -euo pipefail
cd "$(dirname "$0")/.."
MSG="${1:-Update A.S.T.R.O AI}"
REPO="${REPO:-astro-ai}"
TOKEN_FILE="${GH_TOKEN_FILE:-$HOME/.config/astro-ai/gh_token}"
WORK="$HOME/.cache/gh"; mkdir -p "$WORK"; chmod 700 "$WORK"; umask 077
[ -s "$TOKEN_FILE" ] || { echo "missing token file $TOKEN_FILE"; exit 1; }
HDR="$WORK/auth.hdr"; printf 'Authorization: Bearer %s\n' "$(tr -d ' \r\n' < "$TOKEN_FILE")" > "$HDR"
LOGIN=$(curl -sS -H @"$HDR" https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s);if(!j.login){console.error("token rejected: "+(j.message||""));process.exit(1)}console.log(j.login)})')
UID_=$(curl -sS -H @"$HDR" https://api.github.com/user | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>console.log(JSON.parse(s).id))')
echo "signed in as $LOGIN"

STAGE="$WORK/export"; CLONE="$WORK/clone"
tools/export_repo.sh "$STAGE" "$LOGIN" | tail -1
node tools/scan_secrets.js "$STAGE" > /dev/null || { echo "secret scan failed; NOT pushing"; exit 1; }
# Guard: after a workspace restore website/downloads can be empty, which would wipe every installer from the site.
NDL=$(find "$STAGE/website/downloads" -type f 2>/dev/null | wc -l)
if [ "$NDL" -lt 5 ] && [ "${ALLOW_NO_DOWNLOADS:-0}" != 1 ]; then
  echo "refusing to push: only $NDL installer(s) in website/downloads (restore them, or set ALLOW_NO_DOWNLOADS=1)"; exit 1
fi

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
# Guard: a workspace restore can silently drop big files (videos, images). Old installers in website/downloads
# are replaced every release; any other deletion must be confirmed with ALLOW_DELETE=1.
DEL=$(git diff --cached --name-only --diff-filter=D | grep -v '^website/downloads/' || true)
if [ -n "$DEL" ] && [ "${ALLOW_DELETE:-0}" != 1 ]; then
  echo "refusing to push: these files would be DELETED from the repo (set ALLOW_DELETE=1 if intended):"; echo "$DEL" | head -30; exit 1
fi
git diff --cached --stat | tail -15
git commit -q -m "$MSG"
git push -q origin HEAD:main
echo "pushed: https://github.com/$LOGIN/$REPO/commit/$(git rev-parse HEAD)"
rm -f "$HDR" "$ASK"

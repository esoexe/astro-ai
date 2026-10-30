#!/usr/bin/env bash
# Opens a winget-pkgs pull request for one version: forks microsoft/winget-pkgs (once), makes a branch from
# upstream master, uploads packaging/winget/manifests/e/esoexe/AstroAI/<version>/*.yaml and opens the PR.
# Usage: bash tools/winget_submit.sh 1.7.1        (token: ~/.config/astro-ai/gh_token or $GH_TOKEN)
# Run `node tools/package_managers.js <version> <dir-with-SHA256SUMS>` first so the manifests use the release hashes.
# The first PR from an account needs the Microsoft CLA signed (the bot posts the link on the PR).
set -euo pipefail
cd "$(dirname "$0")/.."
V="${1:?version, e.g. 1.7.1}"
T="${GH_TOKEN:-$(cat ~/.config/astro-ai/gh_token)}"
DIR="packaging/winget/manifests/e/esoexe/AstroAI/$V"
[ -d "$DIR" ] || { echo "missing $DIR (run tools/package_managers.js first)"; exit 1; }
API=https://api.github.com; H=(-H "Authorization: Bearer $T" -H "Accept: application/vnd.github+json")
json() { node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s);console.log(process.argv[1].split(".").reduce((o,k)=>o&&o[k],j)??"")})' "$1"; }
OWNER=$(curl -fsS "${H[@]}" $API/user | json login)
curl -fsS -o /dev/null -X POST "${H[@]}" $API/repos/microsoft/winget-pkgs/forks -d '{"default_branch_only":true}' || true
for _ in $(seq 1 30); do [ "$(curl -s -o /dev/null -w '%{http_code}' "${H[@]}" "$API/repos/$OWNER/winget-pkgs/branches/master")" = 200 ] && break; sleep 4; done
SHA=$(curl -fsS "${H[@]}" $API/repos/microsoft/winget-pkgs/git/ref/heads/master | json object.sha)
BR="esoexe.AstroAI-$V"
curl -sS -o /dev/null -X POST "${H[@]}" "$API/repos/$OWNER/winget-pkgs/git/refs" -d "{\"ref\":\"refs/heads/$BR\",\"sha\":\"$SHA\"}"
for f in "$DIR"/*.yaml; do
  rel=${f#packaging/winget/}
  old=$(curl -s "${H[@]}" "$API/repos/$OWNER/winget-pkgs/contents/$rel?ref=$BR" | json sha)
  body="{\"message\":\"New version: esoexe.AstroAI version $V\",\"content\":\"$(base64 -w0 "$f")\",\"branch\":\"$BR\"${old:+,\"sha\":\"$old\"}}"
  printf '%s ' "$rel"; curl -sS -o /dev/null -w '%{http_code}\n' -X PUT "${H[@]}" "$API/repos/$OWNER/winget-pkgs/contents/$rel" -d "$body"
done
KIND="New version"; curl -s -o /dev/null -w '%{http_code}' "$API/repos/microsoft/winget-pkgs/contents/manifests/e/esoexe/AstroAI" | grep -q 200 || KIND="New package"
PR=$(curl -sS -X POST "${H[@]}" $API/repos/microsoft/winget-pkgs/pulls -d "{\"title\":\"$KIND: esoexe.AstroAI version $V\",\"head\":\"$OWNER:$BR\",\"base\":\"master\",\"maintainer_can_modify\":true,\"body\":\"$KIND: A.S.T.R.O AI $V. Per-user exe, silent with /S; InstallerSha256 matches SHA256SUMS.txt of the GitHub release v$V.\"}")
URL=$(echo "$PR" | json html_url)
if [ -n "$URL" ]; then echo "PR: $URL"; else echo "Could not open the PR automatically ($(echo "$PR" | json message)). Open it here:"; echo "https://github.com/microsoft/winget-pkgs/compare/master...$OWNER:winget-pkgs:$BR?expand=1"; fi

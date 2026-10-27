#!/usr/bin/env bash
# Publishes the secret-free export to a new public GitHub repo and turns on GitHub Pages.
#   tools/publish_github.sh            (token is read from $GH_TOKEN_FILE, default ~/.cache/gh/token, mode 600)
# The token is never printed, never put on a command line, and never written into .git/config.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="${REPO:-astro-ai}"
TOKEN_FILE="${GH_TOKEN_FILE:-$HOME/.cache/gh/token}"
WORK="$HOME/.cache/gh"
STAGE="$WORK/$REPO"
[ -s "$TOKEN_FILE" ] || { echo "missing token file $TOKEN_FILE"; exit 1; }
chmod 600 "$TOKEN_FILE"
HDR="$WORK/auth.hdr"; umask 077
printf 'Authorization: Bearer %s\n' "$(tr -d ' \r\n' < "$TOKEN_FILE")" > "$HDR"

api() { # api METHOD PATH [json-body]  -> body on stdout, HTTP code in $WORK/code
  local m="$1" p="$2" d="${3:-}"
  curl -sS -X "$m" -H @"$HDR" -H 'Accept: application/vnd.github+json' -H 'X-GitHub-Api-Version: 2022-11-28' \
       ${d:+-H 'Content-Type: application/json' --data "$d"} -o "$WORK/body" -w '%{http_code}' "https://api.github.com$p" > "$WORK/code"
  cat "$WORK/body"
}
code() { cat "$WORK/code"; }
jget() { node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const j=JSON.parse(s||"{}");const v=process.argv[1].split(".").reduce((o,k)=>o==null?o:o[k],j);console.log(v==null?"":v)})' "$1"; }

echo "1/6  checking the token"
api GET /user > /dev/null; [ "$(code)" = 200 ] || { echo "token rejected (HTTP $(code))"; exit 1; }
LOGIN=$(jget login < "$WORK/body"); UID_=$(jget id < "$WORK/body"); NAME=$(jget name < "$WORK/body")
SCOPES=$(curl -sS -I -H @"$HDR" https://api.github.com/user | tr -d '\r' | awk -F': ' 'tolower($1)=="x-oauth-scopes"{print $2}')
echo "     signed in as $LOGIN; scopes: ${SCOPES:-<fine-grained token>}"
case ",${SCOPES// /}," in *,repo,*) ;; *) [ -n "$SCOPES" ] && { echo "token needs the 'repo' scope"; exit 1; } ;; esac
case ",${SCOPES// /}," in *,workflow,*) ;; *) [ -n "$SCOPES" ] && { echo "token needs the 'workflow' scope (to push .github/workflows)"; exit 1; } ;; esac

echo "2/6  creating $LOGIN/$REPO"
api GET "/repos/$LOGIN/$REPO" > /dev/null
if [ "$(code)" = 200 ]; then
  [ "${ALLOW_EXISTING:-0}" = 1 ] || { echo "     $LOGIN/$REPO already exists; stopping so nothing is overwritten (ALLOW_EXISTING=1 to push to it)"; exit 1; }
else
  api POST /user/repos "{\"name\":\"$REPO\",\"private\":false,\"description\":\"Astro AI: one brain, many minds. A free desktop app where a team of free AI models answers as one.\",\"homepage\":\"https://${LOGIN,,}.github.io/$REPO/\",\"has_wiki\":false,\"has_projects\":false}" > /dev/null
  [ "$(code)" = 201 ] || { echo "     could not create the repo (HTTP $(code)): $(jget message < "$WORK/body")"; exit 1; }
fi

# Turn Pages on before the first push when GitHub allows it, so the push-triggered run deploys cleanly
PAGES_EARLY=0
api POST "/repos/$LOGIN/$REPO/pages" '{"build_type":"workflow"}' > /dev/null || true
case "$(code)" in 201|409) PAGES_EARLY=1 ;; esac

echo "3/6  exporting (allowlist + secret scan)"
tools/export_repo.sh "$STAGE" "$LOGIN" | tail -2
node tools/scan_secrets.js "$STAGE" > /dev/null || { echo "secret scan failed; NOT pushing"; exit 1; }

echo "4/6  committing and pushing"
cd "$STAGE"
git init -q -b main
git config user.name "${NAME:-$LOGIN}"; git config user.email "$UID_+$LOGIN@users.noreply.github.com"
git add -A
git commit -q -m "Astro AI 1.0.0: source, desktop app, installer and website" -m "Clean edition only: no API keys, env files or personal data are included."
ASK="$WORK/askpass.sh"; printf '#!/bin/sh\ncase "$1" in Username*) echo x-access-token;; *) tr -d " \\r\\n" < "%s";; esac\n' "$TOKEN_FILE" > "$ASK"; chmod 700 "$ASK"
GIT_ASKPASS="$ASK" GIT_TERMINAL_PROMPT=0 git push -q "https://github.com/$LOGIN/$REPO.git" main 2>&1 | sed 's/x-access-token:[^@]*@//'
git remote add origin "https://github.com/$LOGIN/$REPO.git"
cd - > /dev/null

echo "5/6  GitHub Pages (built by Actions)"
if [ "$PAGES_EARLY" = 1 ]; then echo "     Pages was enabled before the push"; else
  api POST "/repos/$LOGIN/$REPO/pages" '{"build_type":"workflow"}' > /dev/null
  case "$(code)" in 201|409) ;; *) api PUT "/repos/$LOGIN/$REPO/pages" '{"build_type":"workflow"}' > /dev/null ;; esac
  echo "     Pages enabled after the push: HTTP $(code)"
fi
api PUT "/repos/$LOGIN/$REPO/topics" '{"names":["ai","llm","desktop-app","cpp","free-ai","multi-model","webview","github-pages"]}' > /dev/null
if [ "$PAGES_EARLY" != 1 ]; then
  sleep 3
  api POST "/repos/$LOGIN/$REPO/actions/workflows/pages.yml/dispatches" '{"ref":"main"}' > /dev/null
  echo "     re-ran the website workflow: HTTP $(code)"
fi

echo "6/6  done"
echo "REPO=https://github.com/$LOGIN/$REPO"
echo "SITE=https://${LOGIN,,}.github.io/$REPO/"
echo "$LOGIN" > "$WORK/login"

#!/usr/bin/env bash
# Starts a throwaway engine, installs the Seed and Closer packs into its temp data dir and runs the
# agent composer "/" menu test (tests/agent_slash_e2e.js). Skips (exit 0) when Playwright is not installed.
# Point NODE_PATH or PLAYWRIGHT_DIR at an install, e.g.
#   (mkdir -p /tmp/pw && cd /tmp/pw && npm i playwright && npx playwright install chromium)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
export NODE_PATH="${NODE_PATH:-${PLAYWRIGHT_DIR:-/tmp/pw}/node_modules}"
if ! node -e "require('playwright')" 2>/dev/null; then echo "agent slash e2e: SKIP (playwright not installed)"; exit 0; fi
TMP="$(mktemp -d /tmp/astro-slash.XXXXXX)"; PORT_SLASH="${PORT_SLASH:-4372}"
(ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP" ASTRO_UPDATE_URL=http://127.0.0.1:9/none PORT=$PORT_SLASH exec "$ROOT/workspace_server") >"$TMP/server.log" 2>&1 & SP=$!
cleanup() { kill $SP 2>/dev/null; wait $SP 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 100); do curl -s -o /dev/null "http://127.0.0.1:$PORT_SLASH/api/status" && break; sleep 0.2; done
for P in astro-seed astro-closer; do   # the Closer's skills share no name prefix: the pack chips must group by pack
  R="$(curl -s -H 'x-astro-client: 1' -H 'content-type: application/json' -d "{\"id\":\"$P\",\"inputs\":{}}" "http://127.0.0.1:$PORT_SLASH/api/dev/library/install")"
  case "$R" in *'"ok":true'*) ;; *) echo "agent slash e2e: FAIL (could not install $P: $R)"; exit 1 ;; esac
done
node tests/agent_slash_e2e.js "http://127.0.0.1:$PORT_SLASH"

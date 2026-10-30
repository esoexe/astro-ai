#!/usr/bin/env bash
# Starts a throwaway engine and runs the UI translation coverage test (tests/i18n_coverage.js).
# Skips (exit 0) when Playwright is not installed. Point NODE_PATH or PLAYWRIGHT_DIR at an install, e.g.
#   (mkdir -p /tmp/pw && cd /tmp/pw && npm i playwright && npx playwright install chromium)
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
export NODE_PATH="${NODE_PATH:-${PLAYWRIGHT_DIR:-/tmp/pw}/node_modules}"
if ! node -e "require('playwright')" 2>/dev/null; then echo "i18n coverage: SKIP (playwright not installed)"; exit 0; fi
TMP="$(mktemp -d /tmp/astro-i18n.XXXXXX)"; PORT_I18N="${PORT_I18N:-4371}"
(ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP" ASTRO_UPDATE_URL=http://127.0.0.1:9/none PORT=$PORT_I18N exec "$ROOT/workspace_server") >"$TMP/server.log" 2>&1 & SP=$!
cleanup() { kill $SP 2>/dev/null; wait $SP 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 100); do curl -s -o /dev/null "http://127.0.0.1:$PORT_I18N/api/status" && break; sleep 0.2; done
sleep 2   # let the first provider check finish so the model lists are filled in
node tests/i18n_coverage.js "http://127.0.0.1:$PORT_I18N"

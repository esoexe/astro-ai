#!/usr/bin/env bash
# Starts the OpenCode layer for Workspace: the official `opencode serve` on 127.0.0.1:${OPENCODE_PORT:-4097}.
# - Installs OpenCode into .cache/opencode if missing (not saved in workspace snapshots).
# - Fetches OPENCODE_API_KEY from the Supabase gateway (/credentials) into memory only.
# - Runs with all tools disabled (opencode_layer/opencode.json): no file, shell, or web access.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
OC_HOME="$ROOT/.cache/opencode"
OC_VERSION="${OPENCODE_VERSION:-1.19.4}"
if [ ! -x "$OC_HOME/node_modules/.bin/opencode" ]; then
  echo "Installing OpenCode $OC_VERSION into $OC_HOME ..."
  mkdir -p "$OC_HOME" && npm install --no-audit --no-fund --prefix "$OC_HOME" "opencode-ai@$OC_VERSION" >/dev/null
fi
set -a; . ./.workspace.env; set +a
if [ -n "${LLM_BASE_URL:-}" ] && [ -n "${LLM_API_KEY:-}" ]; then
  KEY=$(curl -fsS -m 30 -X POST -H "Authorization: Bearer $LLM_API_KEY" "${LLM_BASE_URL%/}/credentials" \
        | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{process.stdout.write(JSON.parse(s).opencode_api_key||"")}catch{}})' || true)
  if [ -n "$KEY" ]; then export OPENCODE_API_KEY="$KEY"; echo "OpenCode layer: API key loaded from Supabase (memory only)"; else echo "OpenCode layer: no API key; free models only"; fi
  unset KEY
fi
unset LLM_API_KEY NVIDIA_API_KEY || true
export OPENCODE_SERVER_PASSWORD
export XDG_DATA_HOME="$OC_HOME/xdg/data" XDG_CONFIG_HOME="$OC_HOME/xdg/config" XDG_STATE_HOME="$OC_HOME/xdg/state" XDG_CACHE_HOME="$OC_HOME/xdg/cache"
cd "$ROOT/opencode_layer"
exec "$OC_HOME/node_modules/.bin/opencode" serve --hostname 127.0.0.1 --port "${OPENCODE_PORT:-4097}" --print-logs --log-level WARN

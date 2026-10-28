#!/usr/bin/env bash
# Starts A.S.T.R.O AI on PORT (default 8080). Data (password hash, sessions, saved keys, sync copy) goes to
# ASTRO_DATA_DIR (default: this folder; files .astro_auth.json, .astro_sessions, .astro_keys.env are mode 600). Keyless providers work with no setup.
# Hosted mode (.workspace.env present): fetches every provider key (NVIDIA, Groq, Gemini, OpenRouter, ...)
# from the Supabase gateway once at startup (memory only) and calls the providers directly, using the
# registry in providers.json. If the OpenCode layer (./start_opencode.sh) is running, its models are tried first.
# If the local model on 127.0.0.1:8081 is running, it is used as the last automatic fallback.
# Local mode: USE_LOCAL_MODEL=1 ./start_workspace.sh
set -euo pipefail
cd "$(dirname "$0")"
export PORT="${PORT:-8080}"
# Login data lives next to .workspace.env (owner-only files) so it survives sandbox resets.
export ASTRO_DATA_DIR="${ASTRO_DATA_DIR:-$PWD}"
LOCAL_URL="http://127.0.0.1:${LLAMA_PORT:-8081}/v1"
if [ "${USE_LOCAL_MODEL:-0}" != "1" ] && [ -f .workspace.env ]; then
  set -a; . ./.workspace.env; set +a
  export LLM_CREDENTIALS_URL="${LLM_CREDENTIALS_URL:-${LLM_BASE_URL%/}/credentials}"
  export LLM_WAIT_SECONDS="${LLM_WAIT_SECONDS:-60}"
  # Every free platform and model lives in providers.json (the "brain" registry). Provider keys come
  # from Supabase secrets via the gateway (memory only) or from env vars with the same names.
  if curl -fsS -m 3 "${LOCAL_URL%/v1}/health" >/dev/null 2>&1; then
    export FALLBACK_BASE_URL="${FALLBACK_BASE_URL:-$LOCAL_URL}" FALLBACK_MODEL="${FALLBACK_MODEL:-qwen2.5-1.5b-instruct}"
    echo "A.S.T.R.O AI: local fallback model available at $FALLBACK_BASE_URL"
  else
    echo "A.S.T.R.O AI: no local fallback (start ./start_local_ai.sh to enable one)"
  fi
  OC_URL="http://127.0.0.1:${OPENCODE_PORT:-4097}"
  if [ -n "${OPENCODE_SERVER_PASSWORD:-}" ] && curl -fsS -m 3 -u "opencode:$OPENCODE_SERVER_PASSWORD" "$OC_URL/global/health" >/dev/null 2>&1; then
    export OPENCODE_URL="$OC_URL"
    echo "A.S.T.R.O AI: OpenCode layer at $OPENCODE_URL"
  else
    echo "A.S.T.R.O AI: OpenCode layer not running (start ./start_opencode.sh to enable it)"
  fi
  echo "A.S.T.R.O AI: loading provider keys from ${LLM_BASE_URL%%/functions/*} (memory only)"
else
  export LLM_BASE_URL="$LOCAL_URL" LLM_MODEL="qwen2.5-1.5b-instruct" LLM_MAX_TOKENS="${LLM_MAX_TOKENS:-768}"
  unset LLM_API_KEY LLM_CREDENTIALS_URL || true
  echo "A.S.T.R.O AI: using local model (start it with ./start_local_ai.sh)"
fi
make -s
exec ./workspace_server

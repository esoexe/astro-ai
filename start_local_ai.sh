#!/usr/bin/env bash
# Starts a free, local open-weight model (llama.cpp + Qwen2.5 1.5B Instruct) on 127.0.0.1:${LLAMA_PORT:-8081}.
# Workspace then talks to it through the OpenAI-compatible API. No API key, no cost.
set -euo pipefail

LLAMA_BUILD="${LLAMA_BUILD:-b11899}"
CACHE="${CACHE:-$HOME/.cache}"
LLAMA_DIR="$CACHE/llama/llama-$LLAMA_BUILD"
MODEL_DIR="$CACHE/models"
MODEL_FILE="${MODEL_FILE:-qwen2.5-1.5b-instruct-q4_k_m.gguf}"
MODEL_URL="${MODEL_URL:-https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/$MODEL_FILE}"
THREADS="${THREADS:-$(nproc)}"
LLAMA_PORT="${LLAMA_PORT:-8081}"  # internal only; keep 8080 free for the Workspace app

if [ ! -x "$LLAMA_DIR/llama-server" ]; then
  echo "Downloading llama.cpp $LLAMA_BUILD (prebuilt Linux x64)..."
  mkdir -p "$CACHE/llama"
  curl -fL --retry 3 -o "$CACHE/llama/llama.tar.gz" \
    "https://github.com/ggml-org/llama.cpp/releases/download/$LLAMA_BUILD/llama-$LLAMA_BUILD-bin-ubuntu-x64.tar.gz"
  tar -xzf "$CACHE/llama/llama.tar.gz" -C "$CACHE/llama" && rm "$CACHE/llama/llama.tar.gz"
fi

if [ ! -f "$MODEL_DIR/$MODEL_FILE" ]; then
  echo "Downloading model $MODEL_FILE (~1.1 GB)..."
  mkdir -p "$MODEL_DIR"
  curl -fL --retry 3 -o "$MODEL_DIR/$MODEL_FILE.part" "$MODEL_URL"
  mv "$MODEL_DIR/$MODEL_FILE.part" "$MODEL_DIR/$MODEL_FILE"
fi

exec "$LLAMA_DIR/llama-server" -m "$MODEL_DIR/$MODEL_FILE" \
  --host 127.0.0.1 --port "$LLAMA_PORT" -c 4096 -t "$THREADS" -np 1 \
  --alias qwen2.5-1.5b-instruct

#!/usr/bin/env bash
# Runs the Android engine (static test build) on a Linux PC under qemu-aarch64 and checks what the Android app
# relies on: start-up, app-token protection, the web app, shell commands (/system/bin/sh on Android), the
# payload layout, and a clean stop when the app closes the engine's stdin.
#
#   ASTRO_ANDROID_STATIC_TEST=1 bash android/build_engine.sh    # builds work/workspace_server-static
#   bash android/test_engine_qemu.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${ASTRO_ANDROID_WORK:-$HOME/.cache/android/work}"
BIN="$WORK/workspace_server-static"
QEMU="$(command -v qemu-aarch64-static || command -v qemu-aarch64 || true)"
PORT="${PORT:-4391}"
[ -x "$BIN" ] || { echo "Missing $BIN (run: ASTRO_ANDROID_STATIC_TEST=1 bash android/build_engine.sh)"; exit 2; }
[ -n "$QEMU" ] || { echo "qemu-aarch64 not installed (apt install qemu-user-static)"; exit 2; }
if [ ! -e /system/bin/sh ]; then
    # Android's shell path. On this PC it points at the normal shell so commands can run.
    sudo -n mkdir -p /system/bin 2>/dev/null && sudo -n ln -sf /bin/sh /system/bin/sh 2>/dev/null || echo "note: /system/bin/sh missing; the command test will fail"
fi

T="$(mktemp -d "$HOME/.cache/android/qemu-test.XXXXXX")"
trap 'exec 9>&- 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/app" "$T/data" "$T/tmp"
cp -r "$ROOT/web" "$ROOT/providers.json" "$T/app/"
cp "$ROOT/android/app/src/main/assets/cacert.pem" "$T/app/" 2>/dev/null || true
TOKEN="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
mkfifo "$T/stdin"

fail=0
ok()  { echo "PASS  $*"; }
bad() { echo "FAIL  $*"; fail=1; }

(
    cd "$T/app" && env -i PATH=/usr/bin:/bin PORT="$PORT" ASTRO_BIND=127.0.0.1 ASTRO_AUTH=off ASTRO_APP_TOKEN="$TOKEN" \
        ASTRO_DATA_DIR="$T/data" ASTRO_SUPERVISED=1 ASTRO_EXIT_ON_STDIN_EOF=1 ASTRO_SYNC_MIRROR=off ASTRO_ANDROID=1 \
        HOME="$T" TMPDIR="$T/tmp" SSL_CERT_FILE="$T/app/cacert.pem" ASTRO_UPDATE_URL=http://127.0.0.1:9/none \
        "$QEMU" "$BIN" < "$T/stdin" > "$T/engine.log" 2>&1
    echo "$?" > "$T/exit"
) &
exec 9> "$T/stdin"      # hold the engine's stdin open, like the app does

up=0
for _ in $(seq 1 150); do
    if curl -s -o /dev/null -H 'X-Astro-Client: 1' "http://127.0.0.1:$PORT/api/auth/state"; then up=1; break; fi
    sleep 0.2
done
[ "$up" = 1 ] && ok "engine started under qemu (aarch64)" || { bad "engine did not start"; tail -20 "$T/engine.log"; exit 1; }

H=(-H 'X-Astro-Client: 1')
c() { curl -s -o /dev/null -w '%{http_code}' "${H[@]}" "$@"; }
[ "$(c "http://127.0.0.1:$PORT/api/status")" = 401 ] && ok "no token -> 401" || bad "no token was not refused"
[ "$(c -H "Cookie: astro_session=$(printf 'a%.0s' $(seq 64))" "http://127.0.0.1:$PORT/api/status")" = 401 ] && ok "wrong token -> 401" || bad "wrong token was not refused"
[ "$(c -H "Cookie: astro_session=$TOKEN" "http://127.0.0.1:$PORT/api/status")" = 200 ] && ok "app token cookie -> 200" || bad "app token refused"
[ "$(c -X POST -H 'Content-Type: application/json' -d '{"password":"abcdefgh"}' "http://127.0.0.1:$PORT/api/auth/setup")" = 403 ] && ok "password setup refused in app mode" || bad "password setup allowed"
curl -s "http://127.0.0.1:$PORT/" | grep -q 'id="message-input"' && ok "web app served from the payload" || bad "web app not served"
curl -s "http://127.0.0.1:$PORT/android.js" | grep -q 'AstroAndroid' && ok "android.js served" || bad "android.js missing"

A=(-H "Cookie: astro_session=$TOKEN" -H 'Content-Type: application/json')
proj=qemu-test; mkdir -p "$T/data/projects/$proj"    # a project is a folder under <data>/projects
out="$(curl -s "${H[@]}" "${A[@]}" -X POST -d "{\"project\":\"$proj\",\"command\":\"echo shell-ok-\$((6*7))\"}" "http://127.0.0.1:$PORT/api/ide/exec")"
echo "$out" | grep -q 'shell-ok-42' && ok "shell command runs (/bin/bash -> /system/bin/sh)" || bad "shell command failed: $(echo "$out" | head -c 300)"

exec 9>&-               # the app goes away
for _ in $(seq 1 50); do [ -f "$T/exit" ] && break; sleep 0.2; done
if [ -f "$T/exit" ]; then ok "engine stopped by itself when stdin closed (exit $(cat "$T/exit"))"; else bad "engine kept running after stdin closed"; pkill -f "$BIN" ; fi
[ "$fail" = 0 ] && echo "all Android engine checks passed" || { echo "--- engine log ---"; tail -30 "$T/engine.log"; }
exit "$fail"

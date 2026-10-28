#!/usr/bin/env bash
# Background code check, model health and feedback endpoints (astro_quality.inc).
set -u
cd "$(dirname "$0")/.."
TMP="$(mktemp -d /tmp/astro-quality.XXXXXX)"; PORT=4198
ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP" PORT=$PORT ./workspace_server >"$TMP/log" 2>&1 & PID=$!
trap 'kill $PID 2>/dev/null; wait $PID 2>/dev/null; rm -rf "$TMP"' EXIT
for i in $(seq 1 50); do curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break; sleep 0.2; done
pass=0; fail=0
c() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$PORT/api/$1" -d "$2"; }
t() { if echo "$2" | grep -q -- "$3"; then pass=$((pass+1)); echo "  ok   $1"; else fail=$((fail+1)); echo "  FAIL $1: $2"; fi; }
t "js syntax error"        "$(c check/code '{"lang":"js","code":"function f( {"}')" '"stage":"syntax"'
t "js runtime error"       "$(c check/code '{"lang":"js","code":"const a = null; console.log(a.x);"}')" '"stage":"run"'
t "js browser code skipped" "$(c check/code '{"lang":"js","code":"document.querySelector(\"#a\").textContent = 1;"}')" '"ran":false'
t "esm syntax ok"          "$(c check/code '{"lang":"js","code":"export function add(a, b) { return a + b; }"}')" '"ok":true'
t "python ok + output"     "$(c check/code '{"lang":"python","code":"def f(n):\n    return n * 2\nprint(f(21))"}')" '"stdout":"42'
t "python os not run"      "$(c check/code '{"lang":"python","code":"import os\nprint(os.getcwd())"}')" '"ran":false'
t "python numpy not run"   "$(c check/code '{"lang":"python","code":"import numpy as np\nprint(np.zeros(3))"}')" 'uses the package numpy'
t "temp path hidden"       "$(c check/code '{"lang":"python","code":"def f(:\n  pass"}')" 'main.py'
r=$(c check/code '{"lang":"python","code":"def f(:\n  pass"}'); if echo "$r" | grep -q astro_checks; then fail=$((fail+1)); echo "  FAIL temp folder leaked"; else pass=$((pass+1)); echo "  ok   temp folder not shown"; fi
t "cpp runs"               "$(c check/code '{"lang":"cpp","code":"#include <iostream>\nint main(){ std::cout << 6*7 << std::endl; }"}')" '"stdout":"42'
t "cpp without main"       "$(c check/code '{"lang":"cpp","code":"int add(int a, int b) { return a + b; }"}')" 'no main'
t "c endless loop stopped" "$(c check/code '{"lang":"c","code":"int main(void){ for(;;){} }"}')" '"timed_out":true'
t "json error"             "$(c check/code '{"lang":"json","code":"{\"a\": }"}')" '"ok":false'
t "unknown language"       "$(c check/code '{"lang":"cobol","code":"DISPLAY HELLO"}')" '"checked":false'
t "feedback saved"         "$(c feedback '{"kind":"bad_answer","question":"q","answer":"a"}')" '"saved":true'
t "model health"           "$(curl -s -H 'X-Astro-Client: 1' "http://127.0.0.1:$PORT/api/models/health")" '"models"'
t "no client header"       "$(curl -s -X POST "http://127.0.0.1:$PORT/api/feedback" -d '{}')" 'Missing client header'
echo "quality: $pass passed, $fail failed"
[ $fail -eq 0 ]

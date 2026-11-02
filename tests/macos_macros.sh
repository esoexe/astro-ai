#!/usr/bin/env bash
# macOS <math.h> still defines the old SVID error constants as macros (DOMAIN, SING, OVERFLOW, UNDERFLOW, TLOSS,
# PLOSS), so a variable with one of those names breaks only the macOS build (1.9.0's first tag did: hunter_emails.inc
# had a regex called DOMAIN). Linux and Windows don't define them, so this check defines them and syntax-checks every
# translation unit of the engine, which catches the clash here instead of on the Mac runner.
set -u
cd "$(dirname "$0")/.."
MAC=(-DDOMAIN=1 -DSING=2 -DOVERFLOW=3 -DUNDERFLOW=4 -DTLOSS=5 -DPLOSS=6)
CURL_CFLAGS=$(pkg-config --cflags libcurl 2>/dev/null || true)
fail=0; n=0
for tu in workspace_server.cpp packtools/packtools*.cpp; do
  n=$((n + 1))
  # shellcheck disable=SC2086
  if ! ${CXX:-g++} -std=c++20 -fsyntax-only -I. $CURL_CFLAGS "${MAC[@]}" --param ggc-min-expand=20 --param ggc-min-heapsize=65536 "$tu" 2>/tmp/macos_macros.$$; then
    echo "  FAIL $tu (a name clashes with a macOS <math.h> macro):"; grep -m5 "error" /tmp/macos_macros.$$ | sed 's/^/    /'; fail=$((fail + 1))
  fi
done
rm -f /tmp/macos_macros.$$
echo "macOS macro check: $((n - fail)) passed, $fail failed"
[ "$fail" -eq 0 ]

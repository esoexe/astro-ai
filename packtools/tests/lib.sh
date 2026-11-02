# lib.sh - tiny assertion helpers for the pack tool tests (bash + jq). Fixtures under tests/fixtures come from
# Jake Schincariol's MIT-licensed skill repos; the assertions mirror those repos' own tests.
T="${T:-./workspace_server tool}"
# suites cd into temp folders: make a relative binary path absolute
read -r T_BIN T_REST <<<"$T"
if [[ "$T_BIN" == */* && "$T_BIN" != /* ]]; then T="$(cd "$(dirname "$T_BIN")" && pwd)/$(basename "$T_BIN") $T_REST"; fi
unset T_BIN T_REST
export ASTRO_TOOL_OFFLINE=1   # never call live services from the suites
FIX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fixtures"
PASS=0; FAILN=0
ok()   { PASS=$((PASS+1)); }
bad()  { FAILN=$((FAILN+1)); echo "  FAIL: $*"; }
eq()   { [ "$1" == "$2" ] && ok || bad "$3: expected [$2], got [$1]"; }
has()  { [[ "$1" == *"$2"* ]] && ok || bad "$3: [$2] not in output"; }
hasnt(){ [[ "$1" != *"$2"* ]] && ok || bad "$3: [$2] should not be in output"; }
run()  { OUT=$($T "$@" 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err); }
done_() { echo "$1: $PASS passed, $FAILN failed"; [ "$FAILN" -eq 0 ]; }

#!/usr/bin/env bash
# End-to-end test of the agent engine (/api/agent/*) and the terminal app (astro), with a scripted mock model.
# Covers: a full tool loop, print mode (text + JSON), Plan blocking, Ask approvals, ask_user answers, stop,
# parallel isolated agents (copy and git worktree), diff, merge conflicts, partial/force merge, undo, discard.
# Usage: bash tests/engine_e2e.sh       (needs node, git, a built ./workspace_server and ./astro)
#        SERVER_BIN=... ASTRO_BIN=... bash tests/engine_e2e.sh
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
SERVER_BIN="${SERVER_BIN:-$ROOT/workspace_server}"
ASTRO_BIN="${ASTRO_BIN:-$ROOT/astro}"
TMP="$(mktemp -d /tmp/astro-engine-e2e.XXXXXX)"
MOCK=4196 API=4195
fail=0; pass=0
ok()  { pass=$((pass+1)); echo "  ok   $1"; }
bad() { fail=$((fail+1)); echo "  FAIL $1"; }

mkdir -p "$TMP/data/projects/demo" "$TMP/cwd" "$TMP/home"
echo "seed" > "$TMP/data/projects/demo/README.md"
cat > "$TMP/providers.json" <<EOF
{"providers":[{"id":"mock","name":"Mock","kind":"openai","keyless":true,"base_url":"http://127.0.0.1:$MOCK/v1","models":["mock-coder","mock-reviewer"],"auto_models":2}]}
EOF
node tests/mock_llm.js $MOCK >"$TMP/mock.log" 2>&1 & MP=$!
(cd "$TMP/cwd" && HOME="$TMP/home" ASTRO_AUTH=off ASTRO_DATA_DIR="$TMP/data" PORT=$API PROVIDERS_FILE="$TMP/providers.json" exec "$SERVER_BIN") >"$TMP/server.log" 2>&1 & SP=$!
cleanup() { kill $MP $SP 2>/dev/null; wait $MP $SP 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
for i in $(seq 1 60); do curl -s -o /dev/null "http://127.0.0.1:$API/api/status" && curl -s -o /dev/null "http://127.0.0.1:$MOCK/v1/models" && break; sleep 0.2; done

call() { curl -s -X POST -H 'Content-Type: application/json' -H 'X-Astro-Client: 1' "http://127.0.0.1:$API/api/agent/$1" -d "$2"; }
has()  { echo "$1" | grep -q -- "$2"; }
jget() { node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{const j=JSON.parse(s);const v=process.argv[1].split(".").reduce((o,k)=>o==null?o:o[k],j);console.log(typeof v==="object"?JSON.stringify(v):v)}catch(e){console.log("")}})' "$1"; }
wait_status() {  # id, wanted-regex, seconds
  for i in $(seq 1 $(( $3 * 5 ))); do st=$(call get "{\"id\":\"$1\"}" | jget status); echo "$st" | grep -Eq "$2" && return 0; sleep 0.2; done; return 1
}
export ASTRO_URL="http://127.0.0.1:$API" NO_COLOR=1 HOME="$TMP/home"
D="$TMP/data/projects"

# --- full loop ---
r=$(call start '{"project":"demo","prompt":"create file hello.txt containing hello engine","review":true}'); id=$(echo "$r" | jget id)
[ -n "$id" ] && ok "session started" || bad "start: $r"
wait_status "$id" '^done$' 20 && ok "agent finished" || bad "status: $(call get "{\"id\":\"$id\"}" | head -c 400)"
[ "$(cat "$D/demo/hello.txt" 2>/dev/null)" = "hello engine" ] && ok "file written by the tool loop" || bad "hello.txt missing"
g=$(call get "{\"id\":\"$id\"}")
has "$g" '"type":"review"' && has "$g" 'PASS' && ok "second-model review ran" || bad "review: $(echo "$g" | head -c 300)"
has "$g" '"path":"hello.txt","status":"added"' && ok "done event lists the changed file" || bad "files: $(echo "$g" | head -c 300)"
[ -n "$(echo "$g" | jget checkpoint)" ] && ok "checkpoint created before the run" || bad "no checkpoint"
r=$(call undo "{\"id\":\"$id\"}"); [ ! -e "$D/demo/hello.txt" ] && ok "undo restores the checkpoint" || bad "undo: $r"

# --- follow-up in the same session ---
r=$(call send "{\"id\":\"$id\",\"prompt\":\"create file second.txt containing two\"}"); wait_status "$id" '^done$' 20
[ "$(cat "$D/demo/second.txt" 2>/dev/null)" = "two" ] && ok "follow-up continues the session" || bad "follow-up: $r"

# --- plan mode ---
id=$(call start '{"project":"demo","prompt":"plan only","mode":"plan"}' | jget id); wait_status "$id" '^done$' 20
[ ! -e "$D/demo/should_not_exist.txt" ] && has "$(call get "{\"id\":\"$id\"}")" 'Plan mode is read-only' && ok "plan mode blocks changes" || bad "plan mode"

# --- ask_user ---
id=$(call start '{"project":"demo","prompt":"ask me a question"}' | jget id); wait_status "$id" '^waiting$' 10 && ok "agent waits for the answer" || bad "no question"
call answer "{\"id\":\"$id\",\"answer\":\"blue\"}" >/dev/null; wait_status "$id" '^done$' 20
has "$(call get "{\"id\":\"$id\"}")" 'You chose blue' && ok "answer reaches the agent" || bad "answer"

# --- ask mode approvals ---
id=$(call start '{"project":"demo","prompt":"approve this","mode":"ask"}' | jget id); wait_status "$id" '^waiting$' 10
call answer "{\"id\":\"$id\",\"answer\":\"Deny\"}" >/dev/null; sleep 1; wait_status "$id" '^waiting$' 10
[ ! -e "$D/demo/approved.txt" ] && ok "denied command did not run" || bad "deny ignored"
call answer "{\"id\":\"$id\",\"answer\":\"Allow\"}" >/dev/null; wait_status "$id" '^done$' 20 && ok "allowed command ran, agent finished" || bad "allow"
r=$(call start '{"project":"demo","prompt":"approve this","mode":"ask","interactive":false}'); id=$(echo "$r" | jget id); wait_status "$id" '^done$' 20
has "$(call get "{\"id\":\"$id\"}")" 'nobody is watching' && ok "ask mode without a user blocks instead of hanging" || bad "non-interactive ask"

# --- stop ---
id=$(call start '{"project":"demo","prompt":"slow job"}' | jget id); sleep 1.5; call stop "{\"id\":\"$id\"}" >/dev/null
wait_status "$id" '^stopped$' 5 && ok "stop ends a running command" || bad "stop"

# --- terminal app: print mode ---
mkdir -p "$TMP/work"; echo keep > "$TMP/work/keep.txt"
out=$(cd "$TMP/work" && "$ASTRO_BIN" -p "create file cli.txt containing from the cli" 2>"$TMP/cli.err"); rc=$?
[ $rc -eq 0 ] && [ "$(cat "$TMP/work/cli.txt" 2>/dev/null)" = "from the cli" ] && ok "astro -p works on the current folder" || bad "astro -p rc=$rc $(cat "$TMP/cli.err")"
has "$out" "Created cli.txt" && ok "print mode puts the report on stdout" || bad "stdout: $out"
j=$(cd "$TMP/work" && "$ASTRO_BIN" -p --json "create file j.txt containing json")
echo "$j" | tail -n 3 | grep -q '"type":"done"' && echo "$j" | head -1 | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>JSON.parse(s))' && ok "--json emits JSON lines" || bad "json: $(echo "$j" | head -c 300)"
v=$("$ASTRO_BIN" --version); has "$v" "astro " && ok "astro --version" || bad "version"
"$ASTRO_BIN" models | grep -q "mock-coder" && ok "astro models" || bad "models"
"$ASTRO_BIN" agents | grep -q "create file cli.txt" && ok "astro agents lists sessions" || bad "agents"

# --- parallel agents (copy mode) + merge ---
out=$(cd "$TMP/work" && "$ASTRO_BIN" parallel "create file p1.txt containing one" "create file p2.txt containing two" --merge 2>&1); rc=$?
[ $rc -eq 0 ] && [ "$(cat "$TMP/work/p1.txt" 2>/dev/null)" = "one" ] && [ "$(cat "$TMP/work/p2.txt" 2>/dev/null)" = "two" ] && ok "parallel agents in isolated copies, merged" || bad "parallel rc=$rc: $(echo "$out" | tail -5)"
ls "$D" | grep -q "agent-" && bad "isolated copies left behind: $(ls "$D")" || ok "isolated copies removed after merge"

# --- isolation details (git worktree) ---
R="$D/repo"; mkdir -p "$R"; (cd "$R" && git init -q && git config user.email t@t && git config user.name t && echo one > f.txt && git add . && git commit -qm init && echo dirty >> f.txt && echo new > untracked.txt)
id=$(call start '{"project":"repo","prompt":"edit f.txt from one to ONE","isolate":true,"interactive":false}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}"); W="$D/$(echo "$g" | jget project)"
[ "$(echo "$g" | jget git)" = "true" ] && [ -f "$W/untracked.txt" ] && grep -q dirty "$W/f.txt" && ok "git worktree with uncommitted and untracked files" || bad "worktree: $(echo "$g" | head -c 300)"
[ "$(head -1 "$R/f.txt")" = "one" ] && ok "the project is untouched while the agent works" || bad "base changed early"
d=$(call diff "{\"id\":\"$id\"}"); has "$d" '+ONE' && has "$d" '\-one' && ok "diff of the isolated copy" || bad "diff: $d"
echo "two" > "$R/f.txt"   # the user edits the same file meanwhile
r=$(call merge "{\"id\":\"$id\"}"); has "$r" 'conflicts' && [ "$(head -1 "$R/f.txt")" = "two" ] && ok "merge refuses to overwrite a file changed meanwhile" || bad "conflict: $r"
r=$(call merge "{\"id\":\"$id\",\"force\":true,\"discard\":true}"); [ "$(head -1 "$R/f.txt")" = "ONE" ] && ok "force merge applies the agent's version" || bad "force: $r"
r=$(call undo "{\"id\":\"$id\"}"); [ "$(head -1 "$R/f.txt")" = "two" ] && ok "undo reverts the merge" || bad "undo merge: $r"
[ ! -e "$W" ] && ! (cd "$R" && git worktree list | grep -q agent-) && ok "worktree removed" || bad "worktree left: $(cd "$R" && git worktree list)"

# --- partial merge + discard (copy mode) ---
id1=$(call start '{"project":"demo","prompt":"create file NOTES.md containing agent version","isolate":true,"interactive":false}' | jget id)
wait_status "$id1" '^done$' 20; echo "user version" > "$D/demo/NOTES.md"
r=$(call merge "{\"id\":\"$id1\",\"partial\":true}"); has "$r" '"conflicts":\[{' && [ "$(cat "$D/demo/NOTES.md")" = "user version" ] && ok "partial merge leaves conflicting files" || bad "partial: $r"
W1="$D/$(call get "{\"id\":\"$id1\"}" | jget project)"
call discard "{\"id\":\"$id1\"}" >/dev/null; [ ! -e "$W1" ] && ok "discard removes the copy" || bad "discard"

# --- 3-way merge: the agent and the user changed different lines of the same file ---
printf 'alpha\nb\nc\nd\ne\nf\nomega\n' > "$D/demo/three.txt"
id=$(call start '{"project":"demo","prompt":"edit three.txt from omega to OMEGA","isolate":true,"interactive":false}' | jget id); wait_status "$id" '^done$' 20
sed -i 's/^alpha$/ALPHA/' "$D/demo/three.txt"
r=$(call merge "{\"id\":\"$id\",\"discard\":true}"); [ "$(head -1 "$D/demo/three.txt")" = "ALPHA" ] && [ "$(tail -1 "$D/demo/three.txt")" = "OMEGA" ] && has "$r" '"combined":true' && ok "3-way merge keeps both changes to one file" || bad "3-way: $r $(cat "$D/demo/three.txt")"

# --- project tools (.astro/tools/*.json) ---
mkdir -p "$D/demo/.astro/tools"
echo '{"name":"greet","description":"Write a greeting to greet.txt","command":"echo hello {{who}} > greet.txt","args":{"who":"who to greet"}}' > "$D/demo/.astro/tools/greet.json"
id=$(call start '{"project":"demo","prompt":"use tool greet with who=x; touch pwned"}' | jget id); wait_status "$id" '^done$' 20
[ "$(cat "$D/demo/greet.txt" 2>/dev/null)" = "hello x; touch pwned" ] && [ ! -e "$D/demo/pwned" ] && ok "project tool runs with shell-quoted arguments" || bad "custom tool: $(cat "$D/demo/greet.txt" 2>&1) $(ls "$D/demo")"
rm -f "$D/demo/greet.txt"
id=$(call start '{"project":"demo","prompt":"use tool greet with who=plan","mode":"plan"}' | jget id); wait_status "$id" '^done$' 20
[ ! -e "$D/demo/greet.txt" ] && ok "Plan mode blocks a project tool that is not read-only" || bad "plan custom tool"

# --- a tool block missing its closing brace still runs; cwd=<project> works inside an isolated copy ---
id=$(call start '{"project":"demo","prompt":"missing brace"}' | jget id); wait_status "$id" '^done$' 20
[ -f "$D/demo/brace.txt" ] && ok "tool JSON missing a closing brace is repaired" || bad "brace: $(call get "{\"id\":\"$id\"}" | head -c 300)"
id=$(call start '{"project":"demo","prompt":"fork cwd","isolate":true}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}")
echo "$g" | grep -q '"tool":"bash","type":"tool_done"' && ! echo "$g" | grep -q 'No folder' && ok "cwd=<project name> works in an isolated copy" || bad "fork cwd: $(echo "$g" | grep -o '"summary":"[^"]*"' | head -3)"
call discard "{\"id\":\"$id\"}" > /dev/null

# --- native tool-call markup (GLM <arg_key> style and Qwen JSON style) runs and is hidden ---
id=$(call start '{"project":"demo","prompt":"native markup"}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}")
[ -f "$D/demo/glm.txt" ] && [ -f "$D/demo/q.txt" ] && ! echo "$g" | grep -q 'say[^}]*tool_call' && ok "native tool-call markup runs (GLM + JSON) and is hidden" || bad "native: $(ls "$D/demo") $(echo "$g" | head -c 400)"

id=$(call start '{"project":"demo","prompt":"multi open"}' | jget id); wait_status "$id" '^done$' 20
[ -f "$D/demo/m1.txt" ] && [ -f "$D/demo/m2.txt" ] && [ -f "$D/demo/m3.txt" ] && ok "several <tool_call> openers, name {args} and name(args=) forms" || bad "multi: $(ls "$D/demo") $(call get "{\"id\":\"$id\"}" | grep -o '"summary":"[^"]*"' | head -3)"

id=$(call start '{"project":"demo","prompt":"fence in json"}' | jget id); wait_status "$id" '^done$' 20
grep -q 'curl http://localhost:3000/todos' "$D/demo/DOCS.md" 2>/dev/null && grep -c '```' "$D/demo/DOCS.md" | grep -q '^2$' && ok "a code fence inside a JSON string does not end the tool block" || bad "fence in json: $(cat "$D/demo/DOCS.md" 2>&1 | head -5)"

echo original > "$D/demo/keep.txt"
id=$(call start '{"project":"demo","prompt":"blind rewrite"}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}"); has "$g" 'which you have not read' && [ "$(cat "$D/demo/keep.txt")" = "replaced" ] && ok "write_file must read an existing file before replacing it" || bad "blind rewrite: $(echo "$g" | grep -o '"summary":"[^"]*"' | head -3)"
id=$(call start '{"project":"demo","prompt":"failing check"}' | jget id); wait_status "$id" '^done$' 30
g=$(call get "{\"id\":\"$id\"}"); has "$g" '"failing":"bash' && has "$g" 'Needs attention' && ok "a failing test command is reported on the done event" || bad "failing: $(echo "$g" | head -c 600)"

# --- repeated calls run once; cwd = project name means the root; thinking-out-loud text is hidden ---
id=$(call start '{"project":"demo","prompt":"dup and think"}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}")
[ "$(wc -l < "$D/demo/dup.txt" 2>/dev/null)" = "1" ] && ! echo "$g" | grep -q 'We need to run' && ok "duplicate call runs once, cwd=project works, reasoning hidden" || bad "dup: $(cat "$D/demo/dup.txt" 2>&1 | head -3) $(echo "$g" | head -c 300)"

# --- tolerant parsing: a JSON tool call inside a ```bash fence, plus an empty fence ---
id=$(call start '{"project":"demo","prompt":"fenced bash call"}' | jget id); wait_status "$id" '^done$' 20
g=$(call get "{\"id\":\"$id\"}")
[ -f "$D/demo/fenced.txt" ] && ! echo "$g" | grep -q 'Running it.\\n```' && ok "JSON call in a bash fence runs; empty fences are hidden" || bad "fenced: $(echo "$g" | head -c 400)"

# --- server stop guard ---
r=$(call server-stop '{}'); has "$r" 'belongs to the A.S.T.R.O AI app' && ok "server-stop refused for servers the CLI did not start" || bad "server-stop: $r"

# --- VS Code extension (fake vscode module) ---
if node tests/vscode_ext_test.js "$TMP/vsfolder" > "$TMP/vs.log" 2>&1; then ok "VS Code extension: run, parallel + merge, undo"; else bad "VS Code extension: $(tail -5 "$TMP/vs.log")"; fi

# --- JavaScript SDK ---
if node tests/sdk_test.mjs "$TMP/sdkfolder" > "$TMP/sdk.log" 2>&1; then ok "JS SDK: stream, prompts, parallel, diff, merge, undo"; else bad "JS SDK: $(tail -5 "$TMP/sdk.log")"; fi

echo "engine e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]

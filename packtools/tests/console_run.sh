#!/usr/bin/env bash
# console-tester/run: the TestRun and TestClassify cases of console-skill/tests/test_tester.py, with the fake
# ps2EntryRunner written in sh (mode = the ELF's base name) instead of Python, plus exact summary keys, text output,
# environment and cwd handling, start errors, SIGKILL after an ignored SIGTERM, CRLF and invalid UTF-8 output, `last`.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
RN=(console-tester/run)
mkdir -p bin game inputs
cat > bin/ps2EntryRunner <<'EOF'
#!/bin/sh
mode=$(basename "$1"); mode=${mode%%.*}
echo "Using argv boot path"
echo "ELF file loaded successfully. Entry point: 0x100008"
case "$mode" in
  pass) echo "[title] reached the title screen"; sleep 30 ;;
  hang) sleep 30 ;;
  quick) echo "[title] reached the title screen"; exit 0 ;;
  early) exit 0 ;;
  crash) echo 'Warning: Unimplemented PS2 stub called. name=sceCdInit PC=0x1000, RA=0x2000, Stub# guess (from $v0)=0x0' >&2
         echo "Error during program execution: Unimplemented PS2 stub called: sceCdInit" >&2; exit 0 ;;
  missing) echo "Error: No exact recompiled function for guest PC 0x123456 tableBase=0x0 tableEnd=0x1000000 codeRegion=yes trace=" >&2; exit 0 ;;
  branch) echo "[guest-branch:missing-target] kind=Call op=jal source=0x1 target=0x2 pc=0x2 ra=0x3"; exit 0 ;;
  code) echo boom >&2; exit 3 ;;
  signal) kill -TERM $$; sleep 5 ;;
  segv) kill -SEGV $$; sleep 5 ;;
  flood) awk 'BEGIN { for (i = 0; i < 5000; i++) print "line " i }'; echo "[title] after the flood"; sleep 30 ;;
  child) sleep 30 & echo $! > "$1.pid"; echo "[child] started"; sleep 30 ;;
  replay) [ -n "$PS2X_INPUT_SCRIPT" ] && echo "[pad-replay] loaded 2 lines from $PS2X_INPUT_SCRIPT" >&2
          echo "[title] reached the title screen"; exit 0 ;;
  badreplay) echo "[pad-replay] /x/s.txt line 3 is bad; replay disabled  "; exit 0 ;;
  env) echo "script=${PS2X_INPUT_SCRIPT-unset} record=${PS2X_INPUT_RECORD-unset} pwd=$(pwd)"; shift; echo "args=$*"; exit 0 ;;
  stubborn) trap '' TERM; echo "ignoring TERM"; sleep 30 ;;
  bytes) printf 'a\r\n\377b\r\r\nno newline at the end'; exit 0 ;;
  issues) echo "Warning: Unimplemented PS2 syscall called. PC=0x1"; echo "[IOP/RPC trace:unhandled] sid=0x1"; echo "Warning: Unimplemented PS2 stub called. name=x"; exit 0 ;;
esac
EOF
chmod +x bin/ps2EntryRunner
for m in pass hang quick early crash missing branch code signal segv flood child replay badreplay env stubborn bytes issues; do : > game/$m.elf; done
R=$TMP/bin/ps2EntryRunner OUTD=$TMP/console/runs
boot() { local m=$1; shift; run "${RN[@]}" boot --runner "$R" --elf "game/$m.elf" --out "$OUTD" --json "$@"; }
j() { echo "$OUT" | jq -c "$1"; }

# ---- test_pass_with_expect_until
S0=$(date +%s)
boot pass --seconds 10 --expect '\[title\]' --until-expect
eq "$CODE" 0 "pass exit 0"
eq "$(j '[.result, .stopped_on_pass, .timed_out, .exit_code, .signal, .expects[0].line, .expects[0].text]')" '["PASS",true,false,null,null,3,"[title] reached the title screen"]' "pass: stopped on pass at line 3"
eq "$(( $(date +%s) - S0 < 8 ))" 1 "stopped long before the timeout"
eq "$(jq -r .result "$OUTD/latest.json")" PASS "latest.json written"
eq "$(cat "$OUTD/latest.log")" "$(cat "$(echo "$OUT" | jq -r .log)")" "latest.log is a copy of the run log"
eq "$(j 'keys_unsorted')" '["result","reason","label","stamp","runner","elf","args","seconds","elapsed","timed_out","stopped_on_pass","exit_code","signal","expects","first_fatal","first_issue","counts","input","replay","replay_problem","record","log","lines","omitted","silent_for","notes"]' "summary key order"
eq "$(j '[.runner, .elf, .seconds, .label, .args, .counts, .lines, .omitted, .notes, .reason]')" "[\"$R\",\"$TMP/game/pass.elf\",10.0,null,[],{\"stub\":0,\"syscall\":0,\"missing\":0,\"missing_target\":0,\"iop\":0},3,0,[],\"every expected line appeared\"]" "summary values"
has "$(j .stamp)" "Z\"" "UTC stamp"
eq "$(jq -c 'keys_unsorted | length' "$OUTD/latest.json")" 26 "latest.json has the same keys"

# ---- test_pass_without_expect_runs_whole_timeout
boot pass --seconds 0.4
eq "$(j '[.result, .timed_out, .exit_code, .reason, (.silent_for != null), .seconds]')" '["PASS",true,null,"ran the whole timeout without a crash",true,0.4]' "no expects: the whole timeout is a pass"
eq "$CODE" 0 "pass exit 0 again"

# ---- test_hang
boot hang --seconds 0.4 --expect title --expect title --expect zzz
eq "$CODE" 1 "hang exit 1"
eq "$(j '[.result, .reason, (.silent_for | type), (.expects | length)]')" '["HANG","timeout with no crash; never saw: title, zzz","number",3]' "hang: repeats listed once in the reason, kept in expects"

# ---- test_crash_by_fatal_line_even_with_exit_zero
boot crash --seconds 5
eq "$CODE" 1 "crash exit 1"
eq "$(j '[.result, .first_fatal, .counts.stub, .exit_code, .first_issue.kind, .first_issue.line]')" '["CRASH",{"line":4,"text":"Error during program execution: Unimplemented PS2 stub called: sceCdInit"},1,0,"stub",3]' "crash by a fatal line with exit 0"
eq "$(j .reason)" '"fatal line 4: Error during program execution: Unimplemented PS2 stub called: sceCdInit"' "crash reason"

# ---- test_crash_by_exit_code_and_signal
boot code --seconds 5
eq "$(j '[.result, .reason, .exit_code, .signal]')" '["CRASH","exited with code 3",3,null]' "exit code 3"
boot signal --seconds 5
eq "$(j '[.result, .reason, .exit_code, .signal]')" '["CRASH","killed by SIGTERM",-15,"SIGTERM"]' "killed by SIGTERM"
boot segv --seconds 5
eq "$(j '[.reason, .signal]')" '["killed by SIGSEGV","SIGSEGV"]' "killed by SIGSEGV"

# ---- test_crash_after_missing_function
boot missing --seconds 5
eq "$(j '[.result, .reason, .counts.missing]')" '["CRASH","stopped after jumping to a function the recompiler never emitted",1]' "missing function"
boot branch --seconds 5
eq "$(j '[.result, .counts.missing_target, .first_issue.kind]')" '["CRASH",1,"missing_target"]' "missing branch target"

# ---- test_fail_when_exiting_early
boot early --seconds 5
eq "$(j '[.result, .reason]')" '["FAIL","exited by itself (code 0) before the timeout"]' "early exit"
boot early --seconds 5 --expect title
eq "$(j '[.result, .reason]')" '["FAIL","exited by itself (code 0) without: title"]' "early exit without the expected line"
boot quick --seconds 5 --expect title
eq "$(j .result)" '"PASS"' "quick exit with the expected line"

# ---- test_huge_log_keeps_head_and_tail
boot flood --seconds 10 --keep 100 --expect "after the flood" --until-expect
eq "$(j '[.result, .lines, .omitted]')" '["PASS",5003,4803]' "flood: expect matched past the kept head"
LOGF=$(echo "$OUT" | jq -r .log)
eq "$(wc -l < "$LOGF" | tr -d ' ')" 201 "201 lines kept"
eq "$(sed -n 101p "$LOGF")" "[run.py] ... 4803 lines omitted (kept the first 100 and the last 100) ..." "omitted marker"
eq "$(tail -n 1 "$LOGF")" "[title] after the flood" "last line kept"
eq "$(sed -n 102p "$LOGF")" "line 4901" "tail starts where it should"

# ---- test_kills_the_whole_tree
boot child --seconds 0.7 --expect never
eq "$(j .result)" '"HANG"' "child: hang"
CPID=$(cat game/child.elf.pid)
alive=1; for _ in $(seq 60); do kill -0 "$CPID" 2>/dev/null || { alive=0; break; }; sleep 0.05; done
eq "$alive" 0 "the runner's child did not survive the timeout"
S0=$(date +%s%N)
boot stubborn --seconds 0.3
eq "$(j '[.result, .timed_out]')" '["PASS",true]' "a runner that ignores SIGTERM"
EL=$(( ($(date +%s%N) - S0) / 1000000 ))
eq "$(( EL >= 2000 && EL < 6000 ))" 1 "SIGKILL after the 2 s grace ($EL ms)"

# ---- test_input_sets_replay_env
printf '0 -\n10 START\n' > inputs/menu.txt
boot replay --seconds 5 --input inputs/menu.txt --expect title
eq "$(j '[.result, .replay, .notes, .input]')" "[\"PASS\",\"[pad-replay] loaded 2 lines from $TMP/inputs/menu.txt\",[],\"$TMP/inputs/menu.txt\"]" "replay confirmed"
boot quick --seconds 5 --input inputs/menu.txt --expect title
has "$(echo "$OUT" | jq -r ".notes[0]")" "padmap.py gen --replay" "replay never confirmed"
boot badreplay --seconds 5
eq "$(j '[.replay_problem, .notes]')" '["[pad-replay] /x/s.txt line 3 is bad; replay disabled",["[pad-replay] /x/s.txt line 3 is bad; replay disabled"]]' "replay problem noted (stripped)"
PS2X_INPUT_SCRIPT=/leak PS2X_INPUT_RECORD=/leak2 bash -c "cd '$TMP' && $T ${RN[*]} boot --runner '$R' --elf game/env.elf --out '$OUTD' --json --record rec.txt --cwd '$TMP/game' --arg=-x --arg 'two words'" > env.json
eq "$(jq -r '.replay' env.json)" null "env: no replay"
eq "$(head -3 "$(jq -r .log env.json)" | tail -1)" "script=unset record=$TMP/rec.txt pwd=$TMP/game" "inherited replay variables dropped, --record set, --cwd used"
eq "$(sed -n 4p "$(jq -r .log env.json)")" "args=-x two words" "--arg passed through"
eq "$(jq -c '[.args, .record, .input]' env.json)" "[[\"-x\",\"two words\"],\"$TMP/rec.txt\",null]" "args and record in the summary"

# ---- output bytes
boot bytes --seconds 5
LOGF=$(echo "$OUT" | jq -r .log)
eq "$(od -An -c "$LOGF" | tr -s ' ' | tr -d '\n')" "$(printf 'Using argv boot path\nELF file loaded successfully. Entry point: 0x100008\na\n\357\277\275b\nno newline at the end\n' | od -An -c | tr -s ' ' | tr -d '\n')" "CR stripped at line ends, bad bytes replaced, last partial line kept"
eq "$(j .lines)" 5 "five lines"
boot issues --seconds 5
eq "$(j '[.counts, .first_issue.kind, .result]')" '[{"stub":1,"syscall":1,"missing":0,"missing_target":0,"iop":1},"syscall","FAIL"]' "issue counts"

# ---- test_text_output_and_last
run "${RN[@]}" boot --runner "$R" --elf game/crash.elf --out "$OUTD" --seconds 5 --tail-chars 200
eq "$CODE" 1 "text crash exit 1"
eq "$(echo "$OUT" | sed -E 's/after [0-9]+\.[0-9] s/after T s/; s#[0-9]{8}T[0-9]{6}Z(-[0-9]+)?#STAMP#')" "CRASH  fatal line 4: Error during program execution: Unimplemented PS2 stub called: sceCdInit
  runner  $R $TMP/game/crash.elf
  run     exit code 0 after T s
  issues  stubs 1, syscalls 0, missing functions 0, IOP 0
  fatal   line 4: Error during program execution: Unimplemented PS2 stub called: sceCdInit
  log     $OUTD/STAMP.log (4 lines)
  ---- last 200 characters ----
Warning: Unimplemented PS2 stub called. name=sceCdInit PC=0x1000, RA=0x2000, Stub# guess (from \$v0)=0x0
Error during program execution: Unimplemented PS2 stub called: sceCdInit" "boot text, the tail cut at a line start"
run "${RN[@]}" last --out "$OUTD"
eq "$CODE" 1 "last crash exit 1"
eq "$(echo "$OUT" | head -1)" "CRASH  fatal line 4: Error during program execution: Unimplemented PS2 stub called: sceCdInit" "last text"
nolast() { echo "$OUT" | grep -c -- "----"; }
eq "$(nolast)" 0 "last prints no tail"
run "${RN[@]}" last --out "$OUTD" --json
eq "$(j '.result')" '"CRASH"' "last json"
run "${RN[@]}" last --out "$TMP/none"
eq "$CODE" 2 "last without a run exit 2"
eq "$ERR" "error: no $TMP/none/latest.json yet: run boot first" "last missing message"
mkdir -p d1/latest.json d2 d3 d4
run "${RN[@]}" last --out d1
eq "$ERR" "error: cannot read d1/latest.json: [Errno 21] Is a directory: 'd1/latest.json'" "latest.json is a folder"
echo '{"result": ' > d2/latest.json
run "${RN[@]}" last --out d2
eq "$ERR" "error: cannot read d2/latest.json: Expecting value: line 2 column 1 (char 12)" "bad json"
printf '{"a": "\xe2\x82"}' > d3/latest.json
run "${RN[@]}" last --out d3
eq "$ERR" "error: cannot read d3/latest.json: 'utf-8' codec can't decode bytes in position 7-8: invalid continuation byte" "not UTF-8"
echo '[1]' > d4/latest.json
run "${RN[@]}" last --out d4
eq "$CODE" 1 "a list is a runtime error"
boot quick --seconds 5 --expect title --expect 'reached (the)'
run "${RN[@]}" last --out "$OUTD"
eq "$CODE" 0 "last pass exit 0"
has "$OUT" "  expect  yes  title  (line 3)
  expect  yes  reached (the)  (line 3)" "expect lines"
boot hang --seconds 0.3 --expect zz
run "${RN[@]}" last --out "$OUTD"
has "$OUT" "  run     killed by run.py after 0." "killed text"
has "$OUT" "  expect  NO   zz" "NO padded"
boot signal --seconds 5
run "${RN[@]}" last --out "$OUTD"
has "$OUT" "  run     exit code SIGTERM after" "signal name in the run line"

# ---- test_soak
run "${RN[@]}" soak --runner "$R" --elf game/quick.elf --out "$OUTD" --runs 3 --seconds 5 --expect title --json
eq "$CODE" 0 "soak pass exit 0"
eq "$(j '[.runs, .requested, .totals, .flaky, .fatal_lines, (.results | length), (.results | map(.log) | unique | length), (.results[0] | keys_unsorted)]')" '[3,3,{"PASS":3,"HANG":0,"CRASH":0,"FAIL":0},false,[],3,3,["stamp","result","reason","elapsed","log"]]' "soak json: one log per run"
F=$(echo "$OUT" | jq -r .file)
eq "$(jq -c 'keys_unsorted' "$F")" '["runs","requested","totals","flaky","elapsed_min","elapsed_max","fatal_lines","results"]' "soak file (no file key)"
eq "$(jq -r .label "$OUTD/latest.json")" "soak 3/3" "soak labels each run"
run "${RN[@]}" soak --runner "$R" --elf game/crash.elf --out "$OUTD" --runs 5 --seconds 5 --stop-on-crash
eq "$CODE" 1 "soak crash exit 1"
has "$OUT" "CRASH 1" "stopped on the first crash"
has "$OUT" "x1  Error during program execution" "fatal lines"
eq "$(echo "$OUT" | head -1 | sed -E 's/[0-9]+\.[0-9] s/T s/')" "run   1/5  CRASH   T s  fatal line 4: Error during program execution: Unimplemented PS2 stub called: sceCdInit" "soak run line"
eq "$(echo "$OUT" | sed -n 3p)" "soak: 1 run(s): PASS 0, HANG 0, CRASH 1, FAIL 0" "soak totals line"
has "$OUT" "It does not play the game for you." "soak closing line"
: > game/flip.elf
cat > bin/flip <<EOF
#!/bin/sh
if [ -e "$TMP/flag" ]; then rm "$TMP/flag"; echo "[title]"; else : > "$TMP/flag"; fi
EOF
chmod +x bin/flip
run "${RN[@]}" soak --runner bin/flip --elf game/flip.elf --out "$OUTD" --runs 2 --seconds 5 --expect title
has "$OUT" "FLAKY: the same boot gave different results." "flaky"
eq "$CODE" 1 "flaky exit 1"

# ---- test_usage_errors
for args in "--runner $TMP/nope --elf game/pass.elf|runner not found: $TMP/nope (build it first; see /console-anywhere plan)" \
            "--runner $R --elf $TMP/nope.elf|ELF not found: $TMP/nope.elf (the game's main ELF in your extracted disc folder)" \
            "--runner $R --elf game/pass.elf --seconds 0|--seconds must be above 0" \
            "--runner $R --elf game/pass.elf --seconds nan|--seconds must be above 0" \
            "--runner $R --elf game/pass.elf --expect (|--expect '(' is not a valid regex: missing ), unterminated subpattern" \
            "--runner $R --elf game/pass.elf --input $TMP/x.txt|input script not found: $TMP/x.txt" \
            "--runner $R --elf game/pass.elf --input inputs/menu.txt --record r.txt|use --input or --record, not both" \
            "--runner $R --elf game/pass.elf --keep 1|--keep must be at least 10"; do
  run "${RN[@]}" boot --out "$OUTD" ${args%%|*}
  eq "$CODE" 2 "usage: ${args%%|*}"
  eq "$ERR" "error: ${args#*|}" "message: ${args#*|}"
done
printf 'x' > bin/plain
run "${RN[@]}" boot --runner bin/plain --elf game/pass.elf --out "$OUTD"
eq "$ERR" "error: runner is not executable: bin/plain" "not executable"
chmod +x bin/plain
run "${RN[@]}" boot --runner bin/plain --elf game/pass.elf --out "$OUTD"
eq "$ERR" "error: cannot start bin/plain: [Errno 8] Exec format error: '$TMP/bin/plain'" "exec format error"
run "${RN[@]}" boot --runner "$R" --elf game/pass.elf --out "$OUTD" --cwd "$TMP/nodir"
eq "$ERR" "error: cannot start $R: [Errno 2] No such file or directory: '$TMP/nodir'" "missing cwd"
run "${RN[@]}" soak --runner "$R" --elf game/pass.elf --out "$OUTD" --runs 0
eq "$ERR" "error: --runs must be at least 1" "soak --runs 0"
run "${RN[@]}"
eq "$CODE" 2 "no command exit 2"
has "$OUT" "usage: run [-h] command ..." "help on stdout"
run "${RN[@]}" boot --elf x
has "$ERR" "the following arguments are required: --runner" "--runner required"
run "${RN[@]}" boot --help
has "$OUT" "--expect REGEX" "--expect metavar"
has "$OUT" "--tail-chars TAIL_CHARS" "boot has --tail-chars"
run "${RN[@]}" soak --help
has "$OUT" "--stop-on-crash" "soak has --stop-on-crash"

done_ console-tester/run

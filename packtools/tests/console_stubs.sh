#!/usr/bin/env bash
# console-backend/stubs: the ScanParsing, RepoKnowledge, SyscallCommand and CLI-contract cases of
# console-skill/tests/test_backend.py through the CLI. The logs and the fake PS2Recomp checkout are the upstream
# test's own synthetic fixtures (fixtures/mkstubs.js), plus exact JSON keys, text layout and handler listings.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkstubs.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
node "$MK" "$TMP"
REPO="$TMP/PS2Recomp"
ST=(console-backend/stubs)
j()  { echo "$OUT" | jq -c "$1"; }
sj() { run "${ST[@]}" scan "$@" --json; }
iss() { j "[.issues[] | select(.kind == \"$1\" and (.what | contains(\"$2\")))][0] | $3"; }

# ---- test_exit_codes
run "${ST[@]}" scan run.log; eq "$CODE" 1 "issues -> rc 1"
run "${ST[@]}" scan clean.log; eq "$CODE" 0 "clean -> rc 0"
has "$OUT" "No stubs" "clean says no stubs"; has "$OUT" "ELF loaded, entry 0x00100008" "elf entry milestone"
eq "$OUT" "Read 3 lines from 1 log(s); no --repo: built-in table and name guesses
Boot: argv boot path; ELF loaded, entry 0x00100008
No stubs, syscalls, missing functions or IOP gaps in these logs." "clean text exact"

# ---- test_order_is_first_appearance
sj run.log
eq "$(j '.issues[0].kind')" '"syscall"' "first issue is the syscall"
eq "$(j '[.issues[].rank] == [range(1; (.issues | length) + 1)]')" true "ranks 1..n"
eq "$(j '[.issues[].what] | (index("sceCdRI") < index("MyLibThing"))')" true "sceCdRI before MyLibThing"
eq "$(j '.issues | length')" 18 "18 issues"
eq "$(j '[.issues[].kind]')" '["syscall","syscall","syscall","missing","missing","missing","iop","iop","io","io","io","stub","stub","stub","stub","stub","override","override"]' "kinds in order"
eq "$(j 'keys_unsorted')" '["logs","lines","repo","issues","milestones"]' "report keys"
eq "$(j '.lines')" 35 "35 lines"; eq "$(j '.repo')" null "repo null"
eq "$(j '.logs | length')" 1 "one log"
eq "$(j '.issues[0] | keys_unsorted')" '["rank","kind","what","calls","first","first_pc","first_ra","fix_tool","where","how","example","syscall"]' "syscall issue keys"
eq "$(iss stub sceSifSomething keys_unsorted)" '["rank","kind","what","calls","first","first_pc","first_ra","fix_tool","where","how","example","ended_run","suppressed_after"]' "stub issue keys"
eq "$(j '.milestones | keys_unsorted')" '["argv_boot","elf_entry","overrides_applied","modules","ended_with"]' "milestone keys"
eq "$(j '.milestones.modules[0]')" '{"op":"load-emulated","id":1,"path":"cdrom0:\\MODULES\\SIO2MAN.IRX;1"}' "module entry"
eq "$(j '.issues[0].first')" '"run.log:4"' "first = file:line"

# ---- test_syscalls_counted_and_signed
eq "$(iss syscall 0x5B .calls)" 1 "0x5B calls"
eq "$(iss syscall 0x5B .first_pc)" '"0x001234A0"' "0x5B pc"; eq "$(iss syscall 0x5B .first_ra)" '"0x00100200"' "0x5B ra"
eq "$(iss syscall -0x49 .syscall.number)" -73 "negative number"
has "$(iss syscall -0x49 .what)" "0xFFFFFFB7" "negative shows unsigned"
eq "$(iss syscall 0x99 .calls)" 5000 "hits= counter wins"
eq "$(j '[.issues[] | select(.kind == "syscall")] | length')" 3 "three syscalls"
eq "$(iss syscall 0x5B .what)" '"syscall 0x5B"' "syscall what"
eq "$(iss syscall 0x5B '.syscall | keys_unsorted')" '["number","builtin_name","dispatcher"]' "syscall info keys"

# ---- test_syscall_todo_line_alone_is_enough
for _ in 1 2 3; do echo "[Syscall TODO] encoded=0x0 v1=0x3c v0=0x0 a0=0x1 a1=0x2 a2=0x3 a3=0x4 pc=0x1000"; done > todo.log
sj todo.log
eq "$(j '.issues[0].syscall.number')" 60 "0x3C"; eq "$(j '.issues[0].syscall.builtin_name')" '"SetupThread"' "SetupThread"
eq "$(j '.issues[0].calls')" 3 "3 calls"

# ---- test_missing_functions
sj run.log
eq "$(iss missing 0x001D9410 .code_region)" '"yes"' "code region"
eq "$(iss missing 0x001D9410 .calls)" 1 "dispatch error + branch = one event"
eq "$(iss missing 0x001D9410 .first_ra)" '"0x00100300"' "ra from branch line"
eq "$(iss missing 0x001D9410 .fix_tool)" '"Recompiler"' "Recompiler"
has "$(iss missing 0x001D9410 .how)" '\"0x001D9410\"' "entry point quoted"
eq "$(iss missing 0x00900000 .fix_tool)" '"Investigate"' "outside -> Investigate"
has "$(iss missing 0x00900000 .how)" "overlay" "overlay"
has "$(iss missing 0x03000000 .how)" "outside the generated function table" "outside table"

# ---- test_iop
eq "$(iss iop sid=0x80000597 .first_pc)" '"0x0012A000"' "rpc pc"
has "$(iss iop sid=0x80000597 .how)" "MCSERV" "MCSERV"
eq "$(j '[.issues[] | select(.kind == "iop" and (.what | contains("ps2xIOP error")))] | length')" 1 "numbers normalised"
eq "$(iss iop "ps2xIOP error" .calls)" 2 "two iop errors"
eq "$(j '.milestones.modules[0].path')" '"cdrom0:\\MODULES\\SIO2MAN.IRX;1"' "module path"

# ---- test_disc_io
has "$(iss io LEVEL9.BIN .how)" "/games/disc" "cd root"
eq "$(iss io LEVEL9.BIN .fix_tool)" '"Disc files"' "Disc files"
eq "$(iss io "raw sector reads" .calls)" 2 "lbn calls"
has "$(iss io "raw sector reads" .how)" "setIoPaths" "setIoPaths"; has "$(iss io "raw sector reads" .how)" "iso.py plain" "iso plain"
has "$(iss io "fioRead error" .where)" "FileIO.cpp" "FileIO.cpp"

# ---- test_stubs_and_the_stop
eq "$(iss stub MyLibThing .ended_run)" true "ended_run"
has "$(iss stub MyLibThing .how)" "RA-8 (0x00200038)" "RA-8"
eq "$(iss stub sceSifSomething .suppressed_after)" 8 "suppressed after 8"
eq "$(iss stub sceSifSomething .calls)" 9 "9 calls"
hasnt "$(iss stub sceSifSomething .how)" "RA-8" "PC != RA"
eq "$(j .milestones.ended_with)" '"Unimplemented PS2 stub called: MyLibThing"' "ended_with"
eq "$(j '[.issues[] | select(.kind == "fatal")] | length')" 0 "stop folded into the stub"

# ---- test_overrides
eq "$(iss override sceCdReed .first_pc)" '"0x00123456"' "override pc"
has "$(iss override sceCdReed .how)" "spelling" "spelling"
has "$(iss override "outside function table" .how)" "start address" "start address"
eq "$(j .milestones.overrides_applied)" '["test mod"]' "overrides applied"

# ---- test_untranslated_instruction
sj opcode.log
eq "$(j '.issues[0].kind')" '"opcode"' "opcode kind"
eq "$(j '.issues[0].what')" '"untranslated instruction at 0x0010A2C0"' "opcode what"
has "$(j '.issues[0].how')" "0x7000003C" "raw word"
eq "$(j '.issues[0].fix_tool')" '"Game override"' "Game override"

# ---- test_other_fatal
echo "[main] fatal exception: bad_alloc at 0x1234" > fatal.log; sj fatal.log
eq "$(j '.issues[0].kind')" '"fatal"' "fatal"; eq "$(j '.issues[0].fix_tool')" '"Investigate"' "fatal Investigate"

# ---- test_markdown_table
run "${ST[@]}" scan run.log --markdown; MD="$OUT"
has "$(echo "$MD" | head -n1)" "| # | Kind | What | Calls |" "md header"
eq "$(echo "$MD" | sed -n 2p | grep -o -- '---' | wc -l)" 9 "9 separators"
eq "$(echo "$MD" | wc -l)" 20 "header + 18 rows"
eq "$(echo "$MD" | tail -n +3 | grep -vc '| open |$')" 0 "every row ends open"

# ---- test_text_output_and_limit
run "${ST[@]}" scan run.log --limit 2
has "$OUT" "Fix tool:" "Fix tool"; has "$OUT" "more (use --limit or --json)" "more"
has "$OUT" "no --repo" "no repo"; has "$OUT" "Run ended with" "Run ended"
has "$OUT" "1. syscall: syscall 0x5B  (1 call, first at run.log:4)" "text entry line"
[ "$(echo "$OUT" | grep -c '^16\. ')" -eq 0 ] && ok || bad "limit hides entry 16"
has "$OUT" "... 16 more" "16 more"

# ---- test_several_logs_and_garbage
sj run.log b.log
eq "$(j '.logs | length')" 2 "two logs"
eq "$(j '[.issues[] | select(.kind == "opcode")] | length')" 1 "opcode found after garbage"

# ---- test_bad_inputs
run "${ST[@]}" scan "$TMP/nope.log"; eq "$CODE" 2 "missing log rc 2"; has "$ERR" "cannot open" "cannot open"
run "${ST[@]}" scan "$TMP"; eq "$CODE" 2 "dir rc 2"
run "${ST[@]}" scan run.log --repo "$TMP"; eq "$CODE" 2 "not a checkout rc 2"

# ---- test_big_log_is_fast
S0=$(date +%s%N); sj big.log; S1=$(date +%s%N)
[ $(( (S1 - S0) / 1000000 )) -lt 4000 ] && ok || bad "big log took $(( (S1 - S0) / 1000000 )) ms"
eq "$(j '.issues[0].calls')" 30000 "30000 calls"; eq "$(j .lines)" 60000 "60000 lines"

# ---- test_macro_and_dispatcher_parsing (through the handlers listing)
run "${ST[@]}" handlers --repo "$REPO" --json; eq "$CODE" 0 "handlers rc 0"
eq "$(j '[.[] | select(.kind == "stub") | .name] | sort')" '["_sceMcInit","printf","ret0","sceCdRI","sceCdRead"]' "PS2_STUB_LIST names"
eq "$(j '[.[] | select(.kind == "syscall") | .name] | sort')" '["CreateThread","FlushCache","GetEntryAddress","SetAlarm"]' "PS2_SYSCALL_LIST names"
eq "$(j '[.[].name]')" '["CreateThread","FlushCache","GetEntryAddress","SetAlarm","_sceMcInit","printf","ret0","sceCdRead","sceCdRI","iDeleteSema","sceSifSetReg"]' "handler order"
eq "$(j '.[] | select(.name == "FlushCache") | .numbers')" '["0x64","-0x68 (0xFFFFFF98)"]' "FlushCache cases"
eq "$(j '.[] | select(.name == "iDeleteSema") | .numbers')" '["-0x49 (0xFFFFFFB7)"]' "iDeleteSema case"
eq "$(j '.[] | select(.name == "sceSifSetReg") | .numbers')" '["0x79"]' "namespaced handler"
eq "$(j '[.[].numbers[]] | map(select(startswith("0x77"))) | length')" 0 "commented case ignored"
eq "$(j '.[0] | keys_unsorted')" '["name","kind","numbers","file","todo"]' "row keys"

# ---- test_alias_with_leading_underscore (resolve, defined, todo)
cat > al.log <<'EOF'
Warning: Unimplemented PS2 stub called. name=sceMcInit PC=0x1, RA=0x1, Stub# guess (from $v0)=0x0
Warning: Unimplemented PS2 stub called. name=_printf PC=0x1, RA=0x1, Stub# guess (from $v0)=0x0
Warning: Unimplemented PS2 stub called. name=nothing PC=0x1, RA=0x1, Stub# guess (from $v0)=0x0
EOF
sj al.log --repo "$REPO"
has "$(iss stub sceMcInit .how)" '`_sceMcInit` has a working runtime handler' "sceMcInit -> _sceMcInit"
has "$(iss stub _printf .how)" '`printf` has a working runtime handler in ps2xRuntime/src/lib/Kernel/Stubs/LibC.cpp' "_printf -> printf"
hasnt "$(iss stub nothing .how)" "working runtime handler" "nothing resolves to nothing"
has "$(iss stub _printf .how)" "RA-8 (0xFFFFFFF9)" "RA-8 wraps to 32 bits"
run "${ST[@]}" handlers --repo "$REPO" --json
eq "$(j '.[] | select(.name == "FlushCache") | .file')" '"ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp"' "defined FlushCache"
eq "$(j '[.[] | select(.todo) | .name]')" '["sceCdRI"]' "todo = sceCdRI only"
eq "$(j '.[] | select(.name == "ret0") | .file')" null "undefined -> null"

# ---- test_scan_with_repo_names_exact_files
sj run.log --repo "$REPO"
eq "$(j .repo)" "\"$REPO\"" "repo path"
eq "$(iss stub sceCdRI .fix_tool)" '"Runtime C++"' "TODO -> Runtime C++"
eq "$(iss stub sceCdRI .where)" '"ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp"' "TODO file"
has "$(iss stub sceCdRI .how)" "still a TODO" "still a TODO"
eq "$(iss stub printf .fix_tool)" '"TOML"' "printf TOML"; has "$(iss stub printf .how)" "skip" "skip"
has "$(iss stub sub_00123456 .how)" '\"ret0@0x00123456\"' "ret0 binding"
has "$(iss stub MyLibThing .how)" "replaceFunction" "replaceFunction"; has "$(iss stub MyLibThing .how)" "ps2_call_list.h" "call list"
eq "$(iss syscall 0x5B .fix_tool)" '"Rebuild"' "dispatched -> Rebuild"
eq "$(iss syscall 0x5B .syscall.dispatcher)" '"GetEntryAddress"' "dispatcher name"
has "$(iss syscall 0x99 .how)" "Unknown syscall" "unknown"
run "${ST[@]}" scan run.log --repo "$REPO" --limit 1
has "$OUT" "from 1 log(s); handler names from $REPO" "text names the repo"

# ---- test_known_syscall_with_handler_but_no_case
echo "Warning: Unimplemented PS2 syscall called. PC=0x10, RA=0x20, Encoded=0x18, v0=0x0, v1=0x0, Chosen=0x18" > k.log
sj k.log --repo "$REPO"; H=$(j '.issues[0].how')
has "$H" "SetAlarm" "SetAlarm"; has "$H" 'Add `case 0x18:`' "add case"; has "$H" "Kernel/Syscalls/Sync.cpp" "Sync.cpp"

# ---- test_known_syscall_without_repo_guesses_file
echo "Warning: Unimplemented PS2 syscall called. PC=0x10, RA=0x20, Encoded=0x0, v0=0x0, v1=0xffffffbd, Chosen=0xffffffbd" > k2.log
sj k2.log; H=$(j '.issues[0].how')
eq "$(j '.issues[0].syscall.builtin_name')" '"iSignalSema"' "iSignalSema"
has "$H" "Kernel/Syscalls/Sync.cpp" "guessed Sync.cpp"; has "$H" "pass --repo" "pass --repo"
has "$H" "static_cast<uint32_t>(-0x43)" "negative case literal"

# ---- test_stub_without_repo_guesses_file
echo 'Warning: Unimplemented PS2 stub called. name=sceMcOpen PC=0x1, RA=0x1, Stub# guess (from $v0)=0x0' > mc.log
sj mc.log; has "$(j '.issues[0].where')" "Kernel/Stubs/MemoryCard.cpp" "MemoryCard guess"

# ---- test_handlers_listing
run "${ST[@]}" handlers --repo "$REPO" --json
eq "$(j '.[] | select(.name == "sceCdRead") | .todo')" false "sceCdRead not todo"
eq "$(j '.[] | select(.name == "iDeleteSema") | .kind')" '"dispatch-only"' "dispatch-only"
run "${ST[@]}" handlers --repo "$REPO" --grep cdr --json
eq "$(j '[.[].name] | sort')" '["sceCdRI","sceCdRead"]' "--grep cdr"
run "${ST[@]}" handlers --repo "$REPO" --todo
has "$OUT" "sceCdRI" "--todo lists sceCdRI"; hasnt "$OUT" "sceCdRead " "--todo hides sceCdRead"
has "$OUT" "1 handler(s)" "count line"
run "${ST[@]}" handlers --repo "$REPO" --kind syscall --json
eq "$(j 'all(.kind == "syscall")')" true "--kind syscall"; eq "$(j length)" 4 "4 syscalls"
run "${ST[@]}" handlers --repo "$REPO" --grep cdr
has "$OUT" "2 handler(s) matching 'cdr'" "matching repr"
eq "$(echo "$OUT" | head -n1)" "sceCdRead                          stub          -                            ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp" "row layout"

# ---- test_handlers_needs_a_real_repo
run "${ST[@]}" handlers --repo "$TMP"; eq "$CODE" 2 "fake repo rc 2"
run "${ST[@]}" handlers; eq "$CODE" 2 "--repo required"; has "$ERR" "required" "required message"

# ---- test_number_forms
for f in 0x20 32 0X20; do
  run "${ST[@]}" syscall "$f" --json; eq "$(j .builtin_name)" '"CreateThread"' "form $f"
done
for f in -0x1A -26 0xFFFFFFE6 0xffffffe6; do
  run "${ST[@]}" syscall "$f" --json; eq "$CODE" 0 "rc $f"
  eq "$(j .number)" -26 "number $f"; eq "$(j .builtin_name)" '"iEnableIntc"' "name $f"
done
run "${ST[@]}" syscall 0x20 --json
eq "$(j keys_unsorted)" '["number","display","builtin_name","dispatcher","repo"]' "syscall JSON keys"
eq "$(j .display)" '"0x20"' "display"

# ---- test_negative_number_with_options_in_any_order
run "${ST[@]}" syscall --json -0x68 --repo "$REPO"
eq "$(j .dispatcher)" '"FlushCache"' "dispatcher FlushCache"; eq "$(j .builtin_name)" '"iFlushCache"' "builtin iFlushCache"
eq "$(j .display)" '"-0x68 (0xFFFFFF98)"' "display negative"
run "${ST[@]}" syscall -0x49 --repo "$REPO"; eq "$CODE" 0 "dispatcher-only rc 0"
has "$OUT" "iDeleteSema" "iDeleteSema"; has "$OUT" "not in the built-in table" "not builtin"

# ---- test_unknown_and_bad
run "${ST[@]}" syscall 0x99; eq "$CODE" 1 "unknown rc 1"; has "$OUT" "pass --repo" "pass --repo"
run "${ST[@]}" syscall 0x99 --repo "$REPO"; has "$OUT" "[Syscall TODO]" "Syscall TODO hint"
run "${ST[@]}" syscall banana; eq "$CODE" 2 "banana rc 2"; has "$ERR" "not a number" "not a number"

# ---- test_builtin_table_is_consistent (signed forms)
run "${ST[@]}" syscall 0xFFFFFFB7 --json; eq "$(j .number)" -73 "signed_syscall(0xFFFFFFB7)"
run "${ST[@]}" syscall -0x1A; has "$OUT" "-0x1A (0xFFFFFFE6)" "fmt_syscall"
echo "Warning: Unimplemented PS2 syscall called. PC=0x10, RA=0x20, Encoded=0x0, v0=0x0, v1=0xffffffe6, Chosen=0xffffffe6" > k3.log
sj k3.log; has "$(j '.issues[0].how')" "static_cast<uint32_t>(-0x1A)" "case_literal"

# ---- test_every_subcommand_help_runs
for a in "--help" "scan --help" "handlers --help" "syscall --help"; do
  # shellcheck disable=SC2086
  run "${ST[@]}" $a; eq "$CODE" 0 "$a rc 0"; has "$(echo "$OUT" | tr 'A-Z' 'a-z')" "usage" "$a usage"
done
run "${ST[@]}"; eq "$CODE" 2 "no command rc 2"; hasnt "$ERR" "Traceback" "no traceback"
run "${ST[@]}" syscall -0x1A; eq "$CODE" 0 "lone negative rc 0"; has "$OUT" "iEnableIntc" "iEnableIntc"
run "${ST[@]}" --help; has "$OUT" "usage: stubs" "PROG is stubs"

done_ console-backend/stubs

#!/usr/bin/env bash
# console-tester/triage: the TestTriage cases of console-skill/tests/test_tester.py (the same synthesized runtime
# logs), plus exact text, markdown and JSON, map edge cases (int(x, 16) forms, ties, End <= Start, blank rows),
# suppression-only stubs, CRLF logs, --top slicing, compare in every format, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
TR=(console-tester/triage)
cat > a.log <<'EOF'
Using argv boot path
ELF file loaded successfully. Entry point: 0x100008
[Syscall TODO] encoded=0x0 v1=0x3c v0=0x0 a0=0x0 a1=0x0 a2=0x0 a3=0x0 pc=0x100100
Warning: Unimplemented PS2 syscall called. PC=0x100100, RA=0x100200, Encoded=0x0, v0=0x0, v1=0x3c, Chosen=0x3c
  Args: $a0=0x0, $a1=0x0, $a2=0x0, $a3=0x0
  -> Unknown syscallId=0x3c hits=1
[Syscall TODO] encoded=0x0 v1=0x3c v0=0x0 a0=0x0 a1=0x0 a2=0x0 a3=0x0 pc=0x100100
Warning: Unimplemented PS2 syscall called. PC=0x100100, RA=0x100200, Encoded=0x0, v0=0x0, v1=0x3c, Chosen=0x3c
  Args: $a0=0x0, $a1=0x0, $a2=0x0, $a3=0x0
[IOP/RPC trace:unhandled] sid=0x80000597 rpc=0x1 pc=0x101000 ra=0x101100 send=0x0/0 recv=0x0/0 sendBytes=[] loadedModules=[SIO2MAN,PADMAN]
[SifLoadElfPart] failed path="cdrom0:\\MODULES\\LIBSD.IRX;1" host="/games/x/MODULES/LIBSD.IRX" reason=open failed
[sceCdRead] unresolved request pc=0x102000 ra=0x102100 a0=0x1 a1=0x2 a2=0x3
Error: No exact recompiled function for guest PC 0x123456 tableBase=0x0 tableEnd=0x1000000 codeRegion=yes trace=0x100000>0x100100
[guest-branch:missing-target] kind=IndirectJump op=dispatch source=0x0 target=0x123456 pc=0x123456 ra=0x100300 sp=0x1ffff00 gp=0x0 a0=0x0 a1=0x0 a2=0x0 a3=0x0 s0=0x0 s1=0x0 v0=0x0 v1=0x0 a0Readable=yes a0[0]=0x0 a0[4]=0x0 a0[8]=0x0 a0[c]=0x0 codeRegion=yes policy=0 trace=
Warning: Unimplemented PS2 stub called. name=sceMcInit PC=0x104000, RA=0x104100, Stub# guess (from $v0)=0x0
  Args: $a0=0x0, $a1=0x0, $a2=0x0, $a3=0x0
Error during program execution: Unimplemented PS2 stub called: sceMcInit
EOF
cat > b.log <<'EOF'
Using argv boot path
ELF file loaded successfully. Entry point: 0x100008
Warning: Unimplemented PS2 syscall called. PC=0x100100, RA=0x100200, Encoded=0x0, v0=0x0, v1=0x3c, Chosen=0x3c
  -> Unknown syscallId=0x3c hits=5000
[IOP/RPC trace:unhandled] sid=0x80000597 rpc=0x1 pc=0x101000 ra=0x101100 send=0x0/0 recv=0x0/0 sendBytes=[] loadedModules=[SIO2MAN]
Warning: Unimplemented PS2 stub called. name=sceGsSyncV PC=0x105000, RA=0x105100, Stub# guess (from $v0)=0x0
Error during program execution: Unimplemented PS2 stub called: sceGsSyncV
EOF
printf 'Name,Start,End,Size\nsub_00100000,0x00100000,0x00100400,1024\nmain,0x00100100,0x00100180,128\nsub_00104000,0x00104000,0x00104200,512\n' > map.csv

# ---- test_ranks_and_owners
run "${TR[@]}" scan a.log --json
eq "$CODE" 1 "issues found exit 1"
eq "$(echo "$OUT" | jq -c '[.issues[].key]')" '["stub:sceMcInit","missing:0x123456","syscall:0x3c","iop:0x80000597:0x1","io:SifLoadElfPart:cdrom0:\\\\MODULES\\\\LIBSD.IRX;1","io:sceCdRead:0x102000"]' "ranked keys"
eq "$(echo "$OUT" | jq -c '.issues[0] | [.fatal, .owner, .details.likely_file, .tier, .count]')" '[true,"/console-saves","ps2xRuntime/src/lib/Kernel/Stubs/MemoryCard.cpp",0,1]' "fatal stub first"
eq "$(echo "$OUT" | jq -c '.issues[1] | [.owner, .count, .pc, .ra, .details]')" '["/console-reverse",1,"0x00123456","0x00100300",{"code_region":"yes","branch":"IndirectJump","source":"0x00000000"}]' "lookup + branch lines are one event"
has "$(echo "$OUT" | jq -r '.issues[1].next')" "entry_points" "missing: next step"
eq "$(echo "$OUT" | jq -c '.issues[2] | [.count, .owner, .details.encoded, .tier]')" '[2,"/console-backend","0x0",3]' "syscall counted twice"
eq "$(echo "$OUT" | jq -r '.issues[3].details.loaded_modules')" "SIO2MAN,PADMAN" "IOP modules"
eq "$(echo "$OUT" | jq -c '[.issues[4:][].kind] | unique')" '["io"]' "the rest is I/O"
eq "$(echo "$OUT" | jq -c '[.lines, .logs]')" '[17,["a.log"]]' "lines and logs"
eq "$(echo "$OUT" | jq -c '.issues[4].details')" '{"reason":"open failed"}' "elf part reason"
eq "$(echo "$OUT" | jq -c '.issues[0] | keys_unsorted')" '["key","kind","title","owner","count","fatal","tier","first_log","first_line","first_text","pc","ra","next","details"]' "issue key order without a map"

# ---- test_counts_from_hits_and_suppression
cp b.log b2.log; echo "Warning: Further calls to PS2 stub 'sceGsSyncV' are suppressed after 50 warnings" >> b2.log
run "${TR[@]}" scan b2.log --json
eq "$(echo "$OUT" | jq -c '[.issues[] | select(.key=="syscall:0x3c" or .key=="stub:sceGsSyncV") | [.key, .count, .owner]]')" '[["stub:sceGsSyncV",51,"/console-graphics"],["syscall:0x3c",5000,"/console-backend"]]' "hits and suppression raise the counts"
eq "$(echo "$OUT" | jq -c '.issues[0].details')" '{"likely_file":"ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp","suppressed":true}' "suppressed flag joins the stub line details"
printf "Warning: Further calls to PS2 stub 'sceSdInit' are suppressed after 9 warnings\n  -> Unknown syscallId=0x77 hits=3\n" > sup.log
run "${TR[@]}" scan sup.log
eq "$OUT" "triage: 1 log(s), 2 lines, 1 distinct issue(s)

 1. unimplemented stub sceSdInit  x10  -> /console-sound
    where: -
    first: sup.log line 1: Warning: Further calls to PS2 stub 'sceSdInit' are suppressed after 9 warnings" "a stub seen only through suppression; hits for an unseen syscall are ignored"

# ---- test_function_map + text with a map
run "${TR[@]}" scan a.log --map map.csv
eq "$OUT" "triage: 1 log(s), 17 lines, 6 distinct issue(s)

 1. unimplemented stub sceMcInit [FATAL]  x1  -> /console-saves
    where: pc 0x00104000 (sub_00104000), ra 0x00104100 (sub_00104000+0x100)
    first: a.log line 15: Warning: Unimplemented PS2 stub called. name=sceMcInit PC=0x104000, RA=0x104100, Stub# guess (from \$v0)=0x0
    next:  implement sceMcInit in the runtime (likely Kernel/Stubs/MemoryCard.cpp) or bind it by address; ret0/ret1/reta0 only to triage

 2. no recompiled function at 0x00123456  x1  -> /console-reverse
    where: pc 0x00123456, ra 0x00100300 (sub_00100000+0x300)
    first: a.log line 13: Error: No exact recompiled function for guest PC 0x123456 tableBase=0x0 tableEnd=0x1000000 codeRegion=yes trace=0x100000>0x100100
    next:  add 0x00123456 to entry_points in the TOML (or define a function there in Ghidra and re-export), then re-run ps2_recomp and rebuild

 3. unimplemented syscall 0x3c  x2  -> /console-backend
    where: pc 0x00100100 (main), ra 0x00100200 (sub_00100000+0x200)
    first: a.log line 4: Warning: Unimplemented PS2 syscall called. PC=0x100100, RA=0x100200, Encoded=0x0, v0=0x0, v1=0x3c, Chosen=0x3c
    next:  name it (/console-backend stubs.py syscall 0x3c) and implement it in Kernel/Syscalls/ (Dispatcher.cpp routes the number)

 4. IOP RPC sid 0x80000597 rpc 0x1 unhandled  x1  -> /console-backend
    where: pc 0x00101000, ra 0x00101100
    first: a.log line 10: [IOP/RPC trace:unhandled] sid=0x80000597 rpc=0x1 pc=0x101000 ra=0x101100 send=0x0/0 recv=0x0/0 sendBytes=[] loadedModules=[SIO2MAN,PADMAN]
    next:  find which IRX serves sid 0x80000597 (loadedModules) and answer it: run the real module in ps2xIOP or add an HLE handler

 5. module or overlay cdrom0:\\\\MODULES\\\\LIBSD.IRX;1 did not load  x1  -> /console-backend
    where: -
    first: a.log line 11: [SifLoadElfPart] failed path=\"cdrom0:\\\\MODULES\\\\LIBSD.IRX;1\" host=\"/games/x/MODULES/LIBSD.IRX\" reason=open failed
    next:  is cdrom0:\\\\MODULES\\\\LIBSD.IRX;1 in the extracted disc folder (same case)? If it is, the loader needs work

 6. sceCdRead could not resolve a disc read  x1  -> /console-backend
    where: pc 0x00102000, ra 0x00102100
    first: a.log line 12: [sceCdRead] unresolved request pc=0x102000 ra=0x102100 a0=0x1 a1=0x2 a2=0x3
    next:  the read points outside the extracted files: check cdRoot (the ELF's folder) holds the whole disc, or give the runtime the disc image (cdImage)" "scan text with a map"
run "${TR[@]}" scan a.log --map map.csv --json
eq "$(echo "$OUT" | jq -c '[.issues[] | select(.key=="syscall:0x3c" or .key=="stub:sceMcInit") | [.function, .caller]]')" '[["sub_00104000","sub_00104000+0x100"],["main","sub_00100000+0x200"]]' "function and caller"
eq "$(echo "$OUT" | jq -c '.issues[4] | [.function, .caller, (keys_unsorted | .[-2:])]')" '[null,null,["function","caller"]]' "no pc: null names, keys last"
run "${TR[@]}" lookup 0x100100 0x100200 0x100180 0x200000 0x0FFFFF --map map.csv
eq "$OUT" "0x00100100  main
0x00100200  sub_00100000+0x200
0x00100180  sub_00100000+0x180
0x00200000  (not in the map)
0x000fffff  (not in the map)" "narrowest record wins; End is exclusive"
eq "$CODE" 1 "lookup with misses exit 1"

# ---- test_bad_maps and map edge cases
printf 'Function,Addr\nfoo,1\n' > bad.csv
run "${TR[@]}" scan a.log --map bad.csv
eq "$CODE" 2 "bad header exit 2"
eq "$ERR" "error: bad.csv is not a Ghidra function map (expected the header Name,Start,End,Size)" "bad header message"
printf 'Name,Start,End,Size\nfoo,zz,0x10,4\n' > bad2.csv
run "${TR[@]}" scan a.log --map bad2.csv
eq "$ERR" "error: bad2.csv line 2: Start and End must be hex like 0x00100230" "bad hex"
printf 'Name,Start,End\n\n , ,\nf,0x1\n' > bad3.csv
run "${TR[@]}" lookup 1 --map bad3.csv
eq "$ERR" "error: bad3.csv line 4: expected Name,Start,End,Size" "blank rows skipped, rows counted"
: > empty.csv
run "${TR[@]}" lookup 1 --map empty.csv
has "$ERR" "empty.csv is not a Ghidra function map" "empty map"
run "${TR[@]}" scan a.log --map none.csv
eq "$ERR" "error: no such file: none.csv" "missing map"
mkdir -p adir
run "${TR[@]}" lookup 1 --map adir
eq "$ERR" "error: cannot read adir: [Errno 21] Is a directory: 'adir'" "map is a folder"
printf ' NAME , start ,END\r\n"a,b",0X_10,+0x2_0\r\nz,0x200000,0x100\r\ndup1,0x300000,0x300010\r\ndup2,0x300000,0x300010\r\nneg,-0x10,0x8\r\n' > edge.csv
run "${TR[@]}" lookup --map edge.csv -- 0x10 0x1f 0x200003 0x200004 0x300000 -0x8 0x0
eq "$OUT" "0x00000010  a,b
0x0000001f  a,b+0xf
0x00200003  z+0x3
0x00200004  (not in the map)
0x00300000  dup2
0x-0000008  neg+0x8
0x00000000  neg+0x10" "header any case, CRLF, quoted names, int(x, 16) forms, End <= Start is 4 bytes, ties keep the later row, negatives"
run "${TR[@]}" lookup 0x1__0 --map edge.csv
eq "$ERR" "error: '0x1__0' is not a hex address" "double underscore"
run "${TR[@]}" lookup zz --map map.csv
eq "$CODE" 2 "zz exit 2"
eq "$ERR" "error: 'zz' is not a hex address" "zz message"
run "${TR[@]}" lookup 0x100104 0x104000 --map map.csv
eq "$CODE" 0 "lookup all found exit 0"
eq "$OUT" "0x00100104  main+0x4
0x00104000  sub_00104000" "lookup text"
run "${TR[@]}" lookup 0x900000 --map map.csv --json
eq "$OUT" '[
  {
    "addr": "0x00900000",
    "function": null
  }
]' "lookup json"
run "${TR[@]}" lookup 1
has "$ERR" "the following arguments are required: --map" "lookup needs --map"

# ---- test_text_and_markdown
run "${TR[@]}" scan a.log --markdown --top 2
eq "$OUT" "| # | issue | where | count | first seen | owner | next step |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | unimplemented stub sceMcInit (fatal) | pc 0x00104000, ra 0x00104100 | 1 | a.log:15 | /console-saves | implement sceMcInit in the runtime (likely Kernel/Stubs/MemoryCard.cpp) or bind it by address; ret0/ret1/reta0 only to triage |
| 2 | no recompiled function at 0x00123456 | pc 0x00123456, ra 0x00100300 | 1 | a.log:13 | /console-reverse | add 0x00123456 to entry_points in the TOML (or define a function there in Ghidra and re-export), then re-run ps2_recomp and rebuild |" "markdown, top 2"
run "${TR[@]}" scan a.log --json --top -2
eq "$(echo "$OUT" | jq '.issues | length')" 4 "--top -2 drops the last two, like a slice"
run "${TR[@]}" scan a.log --markdown --json
eq "$CODE" 2 "--markdown and --json exclusive"
has "$ERR" "not allowed with argument" "exclusive message"

# ---- test_clean_log_and_input_errors
printf 'Using argv boot path\nELF file loaded successfully. Entry point: 0x100008\n' > clean.log
run "${TR[@]}" scan clean.log
eq "$CODE" 0 "clean log exit 0"
eq "$OUT" "triage: 1 log(s), 2 lines, 0 distinct issue(s)
  nothing the runtime flagged. If the game still misbehaves, compare against PCSX2 (abdiff.py)." "clean text"
run "${TR[@]}" scan missing.log
eq "$CODE" 2 "missing log exit 2"
eq "$ERR" "error: no such file: missing.log" "missing log message"
run "${TR[@]}" scan adir
eq "$ERR" "error: adir is a folder, not a log file" "folder message"
run "${TR[@]}"
eq "$CODE" 2 "no command exit 2"
has "$OUT" "usage: triage [-h] command ..." "no command prints the help on stdout"
has "$OUT" "Typical: triage.py scan console/runs/latest.log --map console/functions.csv
--markdown" "epilog, wrapped at the help width"
run "${TR[@]}" lookup 0x10 -0x8 --map edge.csv
has "$ERR" "unrecognized arguments: -0x8" "-0x8 is not a negative number to argparse"

# ---- test_other_lines
printf '%s\n' "[terminate] unhandled exception" "[terminate] std::exception: vector too long" "Failed to load ELF file: /x/SLUS_999.99" \
  "[game_overrides] unresolved handler 'sceFooBar' for address 0x100000" \
  "[function-table] cannot replace guest PC 0x2000000: outside generated dense table [0x0, 0x1000000)" \
  "[MPEG] runtime built without FFmpeg; MPEG video decode is disabled." "[MPEG] FFmpeg MPEG-2 decoder not found." \
  "[pad-replay] /x/s.txt line 3 is not 'frame buttons [lx ly rx ry]' in frame order; replay disabled" \
  "fioOpen error: Invalid path address" "fioOpen error: Invalid path address" \
  "ps2_stub fopen error: Failed to open '/g/DATA/X.BIN' with mode 'rb'. Error: No such file or directory" \
  "Error: No exact recompiled function for guest PC 0x2000 tableBase=0x0 tableEnd=0x1000000 codeRegion=no trace=" > other.log
run "${TR[@]}" scan other.log --json
eq "$(echo "$OUT" | jq -c '[.issues[] | [.key, .owner, .count]]')" '[["fatal:terminate","/console-backend",1],["fatal:Failed to load ELF file: /x/SLUS_999.99","/console-reverse",1],["missing:0x2000","/console-reverse",1],["io:mpeg:FFmpeg MPEGN decoder not found.","/console-graphics",1],["io:fioOpen:Invalid path address","/console-backend",2],["io:fopen:Failed to open '"'"'/g/DATA/X.BIN'"'"' with mode '"'"'rb'"'"'. Error: No such file or directory","/console-backend",1],["mod:handler:sceFooBar","/console-mod",1],["mod:table:0x2000000","/console-mod",1],["build:ffmpeg","/console-anywhere",1],["input:replay","/console-controller",1]]' "every other line kind, ranked"
eq "$(echo "$OUT" | jq -r '.issues[0].details.exception')" "std::exception: vector too long" "terminate detail"
has "$(echo "$OUT" | jq -r '.issues[2].next')" "0x00002000 is not code: something jumped through a bad pointer" "not code"
eq "$(echo "$OUT" | jq -c '[.issues[6].pc, .issues[7].pc]')" '["0x00100000","0x02000000"]' "override pcs"
printf '%s\n' "[main] fatal exception: bad 0x1234 at 0xABC" "[main] fatal exception: bad 0x9 at 0x1" "Error during program execution: " "Failed to initialize PS2 memory (x)" "[MPEG] failed -12 at 7" > fat.log
run "${TR[@]}" scan fat.log --json
eq "$(echo "$OUT" | jq -c '[.issues[] | [.key, .title, .count, .owner]]')" '[["fatal:bad 0x? at 0x?","bad 0x1234 at 0xABC",2,"/console-backend"],["fatal:","fatal_exec",1,"/console-backend"],["fatal:Failed to initialize PS2 memory (x)","Failed to initialize PS2 memory (x)",1,"/console-anywhere"],["io:mpeg:failed N at N","MPEG video: failed N at N",1,"/console-graphics"]]' "hex normalised in fatal keys; empty message titled by kind; numbers normalised"

# ---- test_compare
run "${TR[@]}" compare a.log b.log
eq "$CODE" 1 "compare with new issues exit 1"
eq "$OUT" "compare: a.log -> b.log
  fixed 4, still there 2, new 1
  FIXED  unimplemented stub sceMcInit
  FIXED  no recompiled function at 0x00123456
  FIXED  module or overlay cdrom0:\\\\MODULES\\\\LIBSD.IRX;1 did not load
  FIXED  sceCdRead could not resolve a disc read
  STILL  unimplemented syscall 0x3c  (x2 -> x5000)
  STILL  IOP RPC sid 0x80000597 rpc 0x1 unhandled  (x1 -> x1)
  NEW    unimplemented stub sceGsSyncV [FATAL]  -> /console-graphics  [pc 0x00105000, ra 0x00105100]
  New issues appeared: the change may have broken something, or the game got further and met the next blocker. Read the first NEW line." "compare text"
run "${TR[@]}" compare b.log b.log --json
eq "$CODE" 0 "compare same exit 0"
eq "$(echo "$OUT" | jq -c '[(.fixed | length), (.still | length), .new, (.still[0] | keys_unsorted | .[-1])]')" '[0,3,[],"old_count"]' "compare json; old_count added last"
run "${TR[@]}" compare a.log b.log --markdown --map map.csv
eq "$(echo "$OUT" | head -6)" "### new (1)

| # | issue | where | count | first seen | owner | next step |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | unimplemented stub sceGsSyncV (fatal) | pc 0x00105000, ra 0x00105100 | 1 | b.log:6 | /console-graphics | implement sceGsSyncV in the runtime (likely Kernel/Stubs/GS.cpp) or bind it by address; ret0/ret1/reta0 only to triage |" "compare markdown"
has "$OUT" "### still (2)" "still section"
has "$OUT" "| 1 | unimplemented syscall 0x3c | pc 0x00100100 (main), ra 0x00100200 (sub_00100000+0x200) | 5000 | b.log:3 |" "still rows use the new run and the map"
has "$OUT" "### fixed (4)" "fixed section"
run "${TR[@]}" compare clean.log clean.log --markdown
eq "$OUT" "### new (0)


### still (0)


### fixed (0)" "empty compare markdown"
run "${TR[@]}" compare a.log missing.log
eq "$ERR" "error: no such file: missing.log" "compare missing"

# ---- test_multiple_logs_first_occurrence
run "${TR[@]}" scan b.log a.log --json
eq "$(echo "$OUT" | jq -c '[([.issues[] | select(.fatal)][0] | .key, .first_log), .lines, .logs]')" '["stub:sceGsSyncV","b.log",24,["b.log","a.log"]]' "earlier log first; lines add up"
run "${TR[@]}" scan b.log a.log
has "$OUT" "triage: 2 log(s), 24 lines, 7 distinct issue(s)" "two logs header"

# ---- line handling
printf 'Error: No exact recompiled function for guest PC 0x10\r\nok\rfioRead error: short read 12 of 2048\r\n' > crlf.log
run "${TR[@]}" scan crlf.log --json
eq "$(echo "$OUT" | jq -c '[.lines, [.issues[] | [.key, .first_line, .first_text]]]')" '[3,[["missing:0x10",1,"Error: No exact recompiled function for guest PC 0x10"],["io:fioRead:short read N of N",3,"fioRead error: short read 12 of 2048"]]]' "CRLF and lone CR end lines, like text mode"
LONG=$(printf 'é%.0s' $(seq 400))
printf 'Failed to load ELF file: %s\n' "$LONG" > long.log
run "${TR[@]}" scan long.log --json
eq "$(echo "$OUT" | jq -r '.issues[0].first_text | length')" 300 "first_text keeps 300 characters"
eq "$(echo "$OUT" | jq -r '.issues[0].title | length')" 160 "titles keep 160 characters"
printf '\xff\xfeError during program execution: bad \xe9\n' > bin.log
run "${TR[@]}" scan bin.log --json
eq "$(echo "$OUT" | jq -r '.issues[0].title')" "bad �" "undecodable bytes become U+FFFD"

# ---- usage
for c in scan compare lookup; do run "${TR[@]}" $c --help; eq "$CODE" 0 "$c --help"; done
run "${TR[@]}" scan --help
has "$OUT" "usage: triage scan [-h] [--map MAP] [--top TOP] [--markdown | --json]" "scan usage"
run "${TR[@]}" scan
has "$ERR" "the following arguments are required: logs" "scan needs logs"
run "${TR[@]}" scan a.log --top x
eq "$CODE" 2 "--top needs an int"

done_ console-tester/triage

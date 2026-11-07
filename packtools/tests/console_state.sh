#!/usr/bin/env bash
# console-reverse/state: the StateTests of console-skill/tests/test_reverse.py (fake_ps2recomp fixture), plus exact
# text and JSON, detection edge cases (CSV headers, disc images, several build dirs, multi-config caches, PATH lookup),
# template quirks, section editing and the strike circuit breaker, run against the C++ tool. The skill's two templates
# are embedded below exactly as upstream ships them.
source "$(dirname "$0")/lib.sh"
LIBSKILL="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-console/skills/console-reverse"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
SKILL="$TMP/skill"; mkdir -p "$SKILL/templates"
cat > "$SKILL/templates/STATE.md" <<'STATE_EOF'
# Console project state: {{TITLE}}

> Read this file first, every session, before any other step. It is the memory between sessions.
> Rules that never bend (full list in /console-reverse):
> 1. Never clean the build. No `--clean-first`, no `--target clean`, no deleting the build folder.
> 2. Never edit generated code (`ps2xRuntime/src/runner/*.cpp`) and never list or search `runner/`.
> 3. Never edit a header without the user's OK: most headers rebuild every generated file.
> 4. No destructive git (checkout, reset, clean, stash, pull) in the PS2Recomp checkout.
> 5. The run command is the one under "Active runner command". Read it, never retype it from memory.
> 6. Three failed fixes for the same problem: stop (see "Circuit breaker").
> 7. Nothing from the game gets published: disc, ELF, IRX, assets, generated C++.

## Game
- Title: {{TITLE}}
- Serial: {{SERIAL}}
- Region: {{REGION}}
- Boot ELF name: {{ELF_NAME}}

## Workspace paths
- Game workspace: {{WORKSPACE}}
- Console folder: {{CONSOLE_DIR}}
- Disc image: {{DISC_IMAGE}}
- Extracted disc (cdRoot): {{DISC_DIR}}
- Boot ELF path: {{ELF_PATH}}
- PS2Recomp checkout: {{PS2RECOMP}}
- Recompiler config (TOML): {{TOML}}
- Ghidra function map (CSV): {{GHIDRA_CSV}}
- Generated C++ (TOML output): {{RECOMP_OUTPUT}}

## Build config
- Host: {{HOST}}
- Build dir: {{BUILD_DIR}}
- Generator: {{GENERATOR}}
- C++ compiler: {{COMPILER}}
- Build type: {{BUILD_TYPE}}
- Runner executable: {{RUNNER_EXE}}
- Runner .cpp count (count only, never list): {{RUNNER_CPP_COUNT}}

## Current phase
- Phase: {{PHASE}}

Phases: PHASE_SETUP (0), PHASE_EXTRACT (1), PHASE_FUNCTION_MAP (2), PHASE_RECOMPILE (3),
PHASE_BUILD_RUN (4), PHASE_IO_MENUS (5).

## Active runner command
```
{{RUNNER_CMD}}
```

## Binaries
| Binary | Path | TOML | Status |
| --- | --- | --- | --- |
| {{ELF_NAME}} (boot ELF) | {{ELF_PATH}} | {{TOML}} | not recompiled yet |

## Stubs and syscalls
Paste rows from `stubs.py scan --markdown` (/console-backend). Mark every ret0/ret1/reta0 as TEMPORARY.

| # | Kind | What | Calls | First PC | First RA | Fix tool | Where | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

## Crashes
| # | Symptom / PC | Layer | Fix tool | Strikes | Status |
| --- | --- | --- | --- | --- | --- |

## Circuit breaker
One line per open problem, kept by `state.py strike`. At 3 strikes: stop guessing.

## Known upstream issues
Recompiler or runtime bugs found in PS2Recomp itself: function or address, what the MIPS does, what
was generated, the workaround, and the GitHub issue if there is one.

## Learned patterns
Write patterns, not events: "X causes Y, fix with Z".

## Dead ends
What was tried, what happened, why it is not the answer. Read before trying something "new".

## Session log
STATE_EOF
cat > "$SKILL/templates/design.md" <<'DESIGN_EOF'
# Design doc: {{TITLE}} ({{SERIAL}})

Written by Claude from what the user says about the game and what the port reveals. It is a working
map, not a spec: mark every line as SEEN (observed in the port, PCSX2 or Ghidra) or SAID (the user's
memory of the game) until it is SEEN. Update it at the end of every phase. It describes the game in
your own words; it never copies the game's text, art or code.

## The game in one paragraph
What kind of game, what the player does, what makes it this game.

## The loop
The moment-to-moment loop (input, what moves, what scores), then the session loop (level, race, match,
mission), then the long loop (progress, unlocks, saves).

## Screens and menus
Boot order (logos, movies, title, main menu), every menu the user remembers, which buttons move
through them, and where a fresh save starts. One line per screen.

| Screen | How you reach it | What it loads (disc files, modules) | Port status |
| --- | --- | --- | --- |

## Files on the disc
From `iso.py ls -r` and `elf.py info`: the boot ELF, IRX modules, big archives, movies (PSS),
sound banks, level data. What each is FOR, once known (/console-levels maps them in depth).

| File or folder | Size | What it is (guess or known) | Loaded when |
| --- | --- | --- | --- |

## Code map
Functions found so far (Ghidra names, addresses). Start with the boot path: entry, main, the per-frame
function, the loader, the menu state machine, the save code.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |

## Systems
One short section each, filled in as the port reaches them: rendering, audio, input, file loading,
saves, physics, AI. Note which PS2Recomp layer handles each (see reference/guardrails.md in
/console-reverse) and which /console-* skill owns the work.

## Mod ideas
What the user wants to change, and which numbers, files or functions it will touch.
DESIGN_EOF
ST=(--skill-dir "$SKILL" console-reverse/state)
WS="$TMP/game"; REPO="$TMP/PS2Recomp"
mkdir -p "$WS/console/disc" "$REPO/ps2xRecomp" "$REPO/ps2xRuntime/src/runner" "$REPO/out/build/ps2xRuntime"
printf 'BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > "$WS/console/disc/SYSTEM.CNF"
printf '\177ELF' > "$WS/console/disc/SLUS_203.12"
printf 'project(PS2Runtime)\n' > "$REPO/ps2xRuntime/CMakeLists.txt"
for i in 0 1 2; do printf '// generated\n' > "$REPO/ps2xRuntime/src/runner/sub_0000000$i.cpp"; done
printf 'CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\nCMAKE_BUILD_TYPE:STRING=Release\n' > "$REPO/out/build/CMakeCache.txt"
RUNNER="$REPO/out/build/ps2xRuntime/ps2EntryRunner"; printf '#!/bin/sh\n' > "$RUNNER"
mkdir -p "$TMP/bin"; for t in cmake git; do printf '#!/bin/sh\n' > "$TMP/bin/$t"; chmod +x "$TMP/bin/$t"; done
printf 'x' > "$TMP/bin/ninja"   # not executable: shutil.which skips it
HOSTS="$(uname -s) $(uname -m)"
st() { run "${ST[@]}" "$@" --workspace "$WS"; }
ENVB=$(command -v env)
stp() { local OT=$T; T="$ENVB PATH=$TMP/bin $OT"; run "${ST[@]}" "$@"; T=$OT; }   # a PATH we control
STATE="$WS/console/STATE.md"
stamp() { sed -E 's/[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}/STAMP/g'; }

# ---- detect (test_detect_finds_everything_without_listing_runner)
stp detect --workspace "$WS" --json
eq "$CODE" 0 "detect exit 0"
eq "$(echo "$OUT" | jq -c '[.serial, .region, .elf_name, .elf_path, .ps2recomp, .generator, .compiler, .build_type, .runner_exe, .runner_cpp_count, .state_exists]')" \
  "[\"SLUS-20312\",\"USA\",\"SLUS_203.12\",\"$WS/console/disc/SLUS_203.12\",\"$REPO\",\"Ninja\",\"/usr/bin/clang++\",\"Release\",\"$RUNNER\",3,false]" "detect finds everything"
hasnt "$OUT" "sub_0000" "never list runner/"
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["workspace","console_dir","state_exists","host","disc_dir","elf_name","elf_path","serial","region","disc_images","tomls","ghidra_csvs","ps2recomp","build_dirs","build_dir","generator","compiler","build_type","runner_exe","ps2_recomp","ps2_analyzer","runner_cpp_count","tools","notes","recomp_output"]' "json key order (recomp_output last)"
eq "$(echo "$OUT" | jq -c '.tools')" "{\"cmake\":\"$TMP/bin/cmake\",\"ninja\":null,\"git\":\"$TMP/bin/git\",\"c++\":null,\"clang++\":null,\"g++\":null,\"clang-cl\":null,\"cl\":null,\"pkg-config\":null,\"xcrun\":null}" "tools via PATH, executables only"
eq "$(echo "$OUT" | jq -r '.host')" "$HOSTS" "host is platform.system() platform.machine()"
stp detect --workspace "$WS"
eq "$OUT" "Game workspace         $WS
State file             $WS/console/STATE.md (not yet)
Host                   $HOSTS
Extracted disc         $WS/console/disc
Boot ELF               $WS/console/disc/SLUS_203.12
Serial                 SLUS-20312 (USA)
Disc images            -
TOML configs           -
Ghidra maps            -
Generated C++          -
PS2Recomp              $REPO
Build dir              $REPO/out/build
Generator              Ninja
C++ compiler           /usr/bin/clang++
Build type             Release
ps2_recomp             -
ps2_analyzer           -
ps2EntryRunner         $RUNNER
Runner .cpp files      3
Tools on PATH          cmake, git" "detect text"
printf '#!/bin/sh\n' > xcrun; chmod +x xcrun
OT=$T; T="$ENVB PATH=::$TMP/bin $OT"; run "${ST[@]}" detect --workspace "$WS" --json; T=$OT
eq "$(echo "$OUT" | jq -r '.tools.xcrun')" xcrun "an empty PATH entry is the current folder, joined as given"
rm xcrun

# ---- TOML, CSV and disc images (test_detect_reads_paths_from_the_toml)
printf '[general]\ninput = "/g/SLUS_203.12"\noutput = "C:\\\\work\\\\out"\nghidra_output = "/g/functions.csv"\noutput = "second"\n' > "$WS/console/SLUS-20312.toml"
st detect --json
eq "$(echo "$OUT" | jq -c '[.recomp_output, .ghidra_csvs, .tomls]')" "[\"C:\\\\work\\\\out\",[\"/g/functions.csv\"],[\"$WS/console/SLUS-20312.toml\"]]" "TOML paths unescaped, first one wins"
printf 'Name, Start, End, Size\r\nmain,0x100,0x200,0x100\n' > "$WS/console/functions.csv"
printf 'name,start\n' > "$WS/console/other.csv"
mkdir -p "$WS/folder.iso"; printf 'x' > "$WS/Game.ISO"; printf 'x' > "$WS/console/raw.bin"; printf 'x' > "$WS/a.cso"
st detect --json
eq "$(echo "$OUT" | jq -c '.ghidra_csvs')" "[\"$WS/console/functions.csv\"]" "a CSV with the Ghidra header beats the TOML's path"
eq "$(echo "$OUT" | jq -c '.disc_images')" "[\"$WS/Game.ISO\",\"$WS/a.cso\",\"$WS/console/raw.bin\"]" "disc images: files only, sorted per folder, any case"
st detect
has "$OUT" "Disc images            $WS/Game.ISO, $WS/a.cso, $WS/console/raw.bin" "joined in text"
has "$OUT" "Generated C++          C:\\work\\out" "generated C++ in text"
rm -rf "$WS/console/SLUS-20312.toml" "$WS/folder.iso" "$WS/Game.ISO" "$WS/console/raw.bin" "$WS/a.cso" "$WS/console/functions.csv" "$WS/console/other.csv"

# ---- PS2Recomp checkout (test_detect_bad_ps2recomp_path)
st detect --ps2recomp "$TMP" --json
eq "$(echo "$OUT" | jq -c '[.ps2recomp, .notes]')" "[null,[\"$TMP is not a PS2Recomp checkout (no ps2xRuntime/CMakeLists.txt)\"]]" "a named path is the only guess"
st detect --ps2recomp PS2Recomp --json
eq "$(echo "$OUT" | jq -r '.ps2recomp')" "$REPO" "a relative --ps2recomp is made absolute"
mkdir -p "$REPO/build" "$REPO/out/build/x64-Debug" "$REPO/out/build/.hidden" "$REPO/out/build/build"
printf 'CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n' > "$REPO/build/CMakeCache.txt"
for d in x64-Debug .hidden build; do printf 'X:STRING=1\n' > "$REPO/out/build/$d/CMakeCache.txt"; done
st detect --json
eq "$(echo "$OUT" | jq -c '.build_dirs')" "[\"$REPO/out/build\",\"$REPO/out/build/x64-Debug\",\"$REPO/build\"]" "build dirs in upstream order, hidden and skipped names left out"
eq "$(echo "$OUT" | jq -r '.notes[0]')" "more than one build dir: $REPO/out/build, $REPO/out/build/x64-Debug, $REPO/build. Ask which one is real, never delete one." "several build dirs note"
rm -rf "$REPO/build" "$REPO/out/build/x64-Debug" "$REPO/out/build/.hidden" "$REPO/out/build/build"
R2="$TMP/w2/ps2recomp"; mkdir -p "$R2/ps2xRecomp" "$R2/ps2xRuntime" "$R2/build/ps2xRecomp/Release" "$R2/build/ps2xRecomp/Debug" "$TMP/w2/ws"
printf 'project(x)\n' > "$R2/ps2xRuntime/CMakeLists.txt"
printf 'CMAKE_BUILD_TYPE:STRING=\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\nCMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\r\nCMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n' > "$R2/build/CMakeCache.txt"
printf 'x' > "$R2/build/ps2xRecomp/Release/ps2_recomp.exe"; printf 'x' > "$R2/build/ps2xRecomp/Debug/ps2_recomp"
run "${ST[@]}" detect --workspace "$TMP/w2/ws" --json
eq "$(echo "$OUT" | jq -c '[.ps2recomp, .build_type, .generator, .ps2_recomp, .runner_cpp_count]')" "[\"$R2\",\"multi-config (Debug;Release)\",\"Ninja Multi-Config\",\"$R2/build/ps2xRecomp/Debug/ps2_recomp\",null]" "guessed in the parent; multi-config; last cache line wins; Debug before Release"
rm "$R2/build/CMakeCache.txt"
run "${ST[@]}" detect --workspace "$TMP/w2/ws" --json
eq "$(echo "$OUT" | jq -r '.notes[0]')" "no configured build dir yet (no CMakeCache.txt): Phase 0 configures one" "no build dir note"

# ---- the extracted disc elsewhere
W3="$TMP/deep/w3"; mkdir -p "$W3/console/disc" "$W3/a_disc/DIR" "$W3/runner" "$W3/.hid"
printf 'VER = 1\n' > "$W3/console/disc/SYSTEM.CNF"
printf 'BOOT2 = cdrom0:\\X.ELF;1\n' > "$W3/runner/SYSTEM.CNF"; printf 'BOOT2 = cdrom0:\\Y.ELF;1\n' > "$W3/.hid/SYSTEM.CNF"
printf 'boot2=cdrom0:\\DIR\\SLAA_123.45;1\n' > "$W3/a_disc/system.cnf"; printf 'x' > "$W3/a_disc/slaa_123.45"
run "${ST[@]}" detect --workspace "$W3" --json
eq "$(echo "$OUT" | jq -c '[.disc_dir, .elf_name, .elf_path, .serial, .region]')" "[\"$W3/a_disc\",\"SLAA_123.45\",\"$W3/a_disc/slaa_123.45\",\"SLAA-12345\",null]" "no BOOT2 skips a folder; runner/ and dot folders are not searched; names any case"
run "${ST[@]}" detect --workspace "$W3"
has "$OUT" "Serial                 SLAA-12345 (None)" "unknown prefix prints None, like upstream"
has "$OUT" "Not found: ps2recomp, build_dir. Ask the user once, then record it with \`state.py set\`." "not found line"

# ---- init (test_init_fills_template_and_refuses_overwrite, gitignore)
stp init --workspace "$WS" --game "Test Racer"
eq "$CODE" 0 "init exit 0"
eq "$OUT" "Created $STATE
Created $WS/console/.gitignore (keeps disc/, output/ and other game-derived folders out of git)
Still unknown: disc_image, toml, ghidra_csv, recomp_output
Ask the user once, then record each with \`state.py set <key> <value>\`." "init text"
TXT=$(cat "$STATE")
hasnt "$TXT" "{{" "every placeholder filled"
for l in "- Title: Test Racer" "- Serial: SLUS-20312" "- Region: USA" "- Boot ELF name: SLUS_203.12" "- Game workspace: $WS" "- Console folder: $WS/console" \
  "- Disc image: (not set)" "- Extracted disc (cdRoot): $WS/console/disc" "- PS2Recomp checkout: $REPO" "- Host: $HOSTS" "- Build dir: $REPO/out/build" \
  "- Generator: Ninja" "- C++ compiler: /usr/bin/clang++" "- Build type: Release" "- Runner executable: $RUNNER" "- Runner .cpp count (count only, never list): 3" \
  "- Phase: PHASE_SETUP (0 of 5: toolchain)" "| SLUS_203.12 (boot ELF) | $WS/console/disc/SLUS_203.12 | (not set) | not recompiled yet |"; do
  has "$TXT" "$l" "STATE.md line: $l"
done
has "$TXT" "## Active runner command
\`\`\`
\"$RUNNER\" \"$WS/console/disc/SLUS_203.12\"
\`\`\`" "runner command filled in"
eq "$(tail -2 "$STATE" | stamp)" "## Session log
- STAMP  state file created by state.py init" "first log line, dated"
[[ "$(tail -1 "$STATE")" =~ ^-\ [0-9]{4}-[0-9]{2}-[0-9]{2}\ [0-9]{2}:[0-9]{2}\ \ state ]] && ok || bad "stamp format"
eq "$(head -3 "$WS/console/.gitignore")" "# Written by console-reverse state.py. Game data, and anything derived from it, stays on this machine.
# Only your own patches, overrides, notes and art belong in a repository.
disc/" ".gitignore written"
eq "$(tail -1 "$WS/console/.gitignore")" "SC??_*" ".gitignore ends with the serial globs"
st init
eq "$CODE" 1 "init refuses to overwrite"
eq "$ERR" "state: $STATE already exists; read it (\`state.py show\`) instead of starting over" "refusal message"
cp "$STATE" "$TMP/state.bak"; rm "$STATE"; printf 'mine\n' > "$WS/console/.gitignore"
st init --json
eq "$(echo "$OUT" | jq -c '[.gitignore_written, .unknown]')" '[false,["title","disc_image","toml","ghidra_csv","recomp_output"]]' "init json; an existing .gitignore is kept"
eq "$(cat "$WS/console/.gitignore")" "mine" ".gitignore not overwritten"
rm "$STATE"
mv "$WS/console/disc" "$TMP/disc.moved"
st init --serial "sles_501.23" --json
eq "$CODE" 0 "init --serial exit 0"
TXT=$(cat "$STATE")
has "$TXT" "- Serial: SLES-50123" "serial normalised"
has "$TXT" "- Region: Europe" "region from the serial"
has "$TXT" "- Boot ELF name: SLES_501.23" "ELF name built from the serial"
has "$TXT" "(not set: record it with \`state.py set runner_cmd \"...\"\`)" "runner command placeholder"
rm "$STATE"
st init --serial " SCUS 971.01 "
has "$(cat "$STATE")" "- Serial: SCUS-97101" "space separator, stripped"
rm "$STATE"
st init --serial "SLAA12345"
has "$(cat "$STATE")" "- Region: unknown prefix" "unknown prefix"
rm "$STATE"
st init --serial nope
eq "$CODE" 2 "bad serial exit 2"
eq "$ERR" "state: 'nope' is not a PS2 serial (expected like SLUS-20312)" "bad serial message"
[ ! -e "$STATE" ] && ok || bad "bad serial writes nothing"
st init --game "{{OOPS}}"
eq "$ERR" "state: template placeholders left unfilled: {{OOPS}}" "placeholders left"
st init --game "{{SERIAL}}" --serial SLUS-20312
has "$(cat "$STATE")" "- Title: SLUS-20312" "placeholders fill in order, like str.replace"
rm "$STATE"
run --skill-dir "$TMP/nowhere" console-reverse/state init --workspace "$WS"
eq "$ERR" "state: the template $TMP/nowhere/templates/STATE.md is missing: reinstall the skill" "missing template"
mv "$TMP/disc.moved" "$WS/console/disc"
run --skill-dir "$LIBSKILL" console-reverse/state init --workspace "$WS" --game "Lib"
eq "$CODE" 0 "the library's own template works"
hasnt "$(cat "$STATE")" "{{" "library template fully filled"
rm "$STATE"
mkdir -p "$TMP/here" && ( cd "$TMP/here" && $T "${ST[@]}" init >/dev/null 2>&1 ); [ -f "$TMP/here/console/STATE.md" ] && ok || bad "init without --workspace uses ./console"
cp "$TMP/state.bak" "$STATE"

# ---- commands need a state file (test_commands_need_a_state_file)
mv "$STATE" "$TMP/state.keep"
for argv in "show" "log x" "phase 1" "set title x" "strike k n" "design"; do
  # shellcheck disable=SC2086
  st $argv
  eq "$CODE" 2 "$argv without a state file exit 2"
  eq "$ERR" "state: no state file at $STATE: run \`state.py init\` first" "$argv message"
done
st set nonsense x
eq "$ERR" "state: unknown field 'nonsense'; fields: title, serial, region, elf_name, workspace, console_dir, disc_image, disc_dir, elf_path, ps2recomp, toml, ghidra_csv, recomp_output, host, build_dir, generator, compiler, build_type, runner_exe, runner_cpp_count, runner_cmd" "the field is checked first"
st phase PHASE_9
eq "$ERR" "state: unknown phase 'PHASE_9': use one of PHASE_SETUP, PHASE_EXTRACT, PHASE_FUNCTION_MAP, PHASE_RECOMPILE, PHASE_BUILD_RUN, PHASE_IO_MENUS (or 0..5)" "the phase is checked first"
st log "   "
eq "$ERR" "state: empty log line" "the message is checked first"
mv "$TMP/state.keep" "$STATE"

# ---- phase (test_phase_by_name_number_and_bad)
st phase PHASE_EXTRACT
eq "$CODE" 0 "phase exit 0"
eq "$OUT" "Phase: PHASE_EXTRACT (1 of 5: disc and ELF) (was PHASE_SETUP (0 of 5: toolchain))
Done when: the disc is extracted (console/disc), the boot ELF is found, elf.py info is recorded and console/design.md is started." "phase text"
has "$(cat "$STATE")" "- Phase: PHASE_EXTRACT (1 of 5: disc and ELF)" "phase recorded"
st phase 4
has "$(cat "$STATE")" "- Phase: PHASE_BUILD_RUN (4 of 5" "phase by number"
st phase " io_menus "
has "$(cat "$STATE")" "- Phase: PHASE_IO_MENUS (5 of 5: I/O and menus)" "phase by short lowercase name"
has "$(cat "$STATE")" "phase -> PHASE_BUILD_RUN" "phase logged"
st phase 00002
has "$OUT" "Phase: PHASE_FUNCTION_MAP" "leading zeros"
st phase "٣"
has "$OUT" "Phase: PHASE_RECOMPILE (3 of 5: recompile) (was PHASE_FUNCTION_MAP" "any Unicode decimal digit, like int()"
st phase 6
eq "$CODE" 2 "phase 6 exit 2"
has "$ERR" "unknown phase '6'" "phase 6 message"
st phase "²"
eq "$CODE" 1 "a digit that is not decimal fails like int() does"
has "$ERR" "invalid literal for int() with base 10: '²'" "ValueError text"

# ---- log (test_log_appends_dated_lines_in_order)
st log "booted to the first file read"
eq "$OUT" "Logged: booted to the first file read" "log text"
st log second "  line	with   tabs "
eq "$OUT" "Logged: second line with tabs" "whitespace collapsed"
SEC=$(sed -n '/^## Session log/,$p' "$STATE" | stamp)
has "$SEC" "- STAMP  booted to the first file read
- STAMP  second line with tabs" "log lines appended in order"

# ---- set (test_set_fields_and_runner_cmd)
st set build_dir "/x/out/build"
eq "$OUT" "build_dir = /x/out/build" "set text"
has "$(cat "$STATE")" "- Build dir: /x/out/build" "field set"
st set Build-Dir '  C:\new\1   \g<0> '
eq "$OUT" 'build_dir = C:\new\1 \g<0>' "key normalised; value collapsed; backslashes literal"
has "$(cat "$STATE")" '- Build dir: C:\new\1 \g<0>' "replacement is literal"
st set runner_cmd '  "/r/ps2EntryRunner" "/g/SLUS_203.12"  '
has "$(cat "$STATE")" '```
"/r/ps2EntryRunner" "/g/SLUS_203.12"
```' "runner command block"
has "$(tail -1 "$STATE")" "active runner command set" "runner command change logged"
st set title " "
eq "$ERR" "state: empty value for title" "empty value"
st set phase x
has "$ERR" "unknown field 'phase'" "phase is not settable"
st show --json
eq "$(echo "$OUT" | jq -c '[.build_dir, .runner_cmd, .state, .phase, .strikes]')" "[\"C:\\\\new\\\\1 \\\\g<0>\",\"\\\"/r/ps2EntryRunner\\\" \\\"/g/SLUS_203.12\\\"\",\"$STATE\",\"PHASE_RECOMPILE (3 of 5: recompile)\",[]]" "show json"
eq "$(echo "$OUT" | jq -c 'keys_unsorted | .[-4:]')" '["phase","runner_cmd","state","strikes"]' "show json key order"
cp "$STATE" "$TMP/s2"; grep -v '^- Title:' "$TMP/s2" > "$STATE"
st set title X
eq "$ERR" "state: STATE.md has no '- Title:' line" "missing field line"
st show --json
eq "$(echo "$OUT" | jq -c '.title')" null "a missing field is null"
cp "$TMP/s2" "$STATE"

# ---- show (test_show_section_and_trimmed_log)
cp "$STATE" "$TMP/s3"
for i in $(seq 0 29); do st log "entry $i"; done
NLOG=$(sed -n '/^## Session log/,$p' "$STATE" | grep -c '^- ')
st show
has "$OUT" "($((NLOG - 15)) earlier log lines hidden; --all shows them)" "hidden count"
has "$OUT" "entry 29" "newest shown"
hasnt "$OUT" "entry 3
" "older hidden"
eq "$(echo "$OUT" | tail -1)" "$(tail -1 "$STATE")" "ends with the newest line"
st show --all
eq "$OUT" "$(cat "$STATE")" "--all is the whole file"
st show --log-lines 0
has "$OUT" "($NLOG earlier log lines hidden; --all shows them)" "--log-lines 0 hides all"
eq "$(echo "$OUT" | tail -1)" "($NLOG earlier log lines hidden; --all shows them)" "nothing after the note"
st show --log-lines 500
hasnt "$OUT" "hidden" "no note when everything fits"
st show --section "active RUNNER command"
eq "$OUT" '## Active runner command
```
"/r/ps2EntryRunner" "/g/SLUS_203.12"
```' "one section, case-insensitive"
st show --section Nope
eq "$CODE" 2 "missing section exit 2"
eq "$ERR" "state: no section 'Nope' in $STATE" "missing section message"
st show --log-lines x
eq "$CODE" 2 "--log-lines needs an int"
cp "$TMP/s3" "$STATE"

# ---- strike (test_strike_circuit_breaker)
K="PC 0x00123456 hang"
st strike "$K" "bound ret0"
eq "$OUT" "Strike 1 of 3 on [$K]." "strike 1"
st strike "  PC  0x00123456   hang " "bound ret1"
eq "$CODE" 0 "strike 2 exit 0"
eq "$OUT" "Strike 2 of 3 on [$K]. Before the next attempt, load the reference for this subsystem and re-read the original code. No third guess without new information." "strike 2 text; key whitespace collapsed"
st strike "$K" "nop patch"
eq "$CODE" 1 "strike 3 exit 1"
eq "$OUT" "STRIKE 3 of 3 on [$K]: STOP. Do not guess again.
1. Re-read STATE.md and reference/guardrails.md (/console-reverse).
2. Load the reference for this subsystem, read the original MIPS in Ghidra.
3. A/B against PCSX2 if it is available: the first divergence is the bug.
4. Still stuck: write the user one specific question with what you know and tried." "strike 3 text"
TXT=$(cat "$STATE")
has "$(echo "$TXT" | stamp)" "## Circuit breaker
One line per open problem, kept by \`state.py strike\`. At 3 strikes: stop guessing.
- [$K] 3/3 strikes; last: nop patch (STAMP)
" "one line in its section, updated in place"
eq "$(grep -c "^- \[$K\]" "$STATE")" 1 "only one line"
has "$(echo "$TXT" | stamp)" "- STAMP  strike 3/3 on [$K]: nop patch" "strike logged"
st strike --list
eq "$CODE" 1 "list with 3 strikes exit 1"
eq "$(echo "$OUT" | stamp)" "[$K] 3/3  last: nop patch (STAMP)" "list text"
st strike other ""
has "$(cat "$STATE")" "- [other] 1/3 strike; last: no note (" "empty note means no note"
st show --json
eq "$(echo "$OUT" | jq -c '[.strikes[] | [.key, .strikes]]')" "[[\"$K\",3],[\"other\",1]]" "strikes in show json"
st strike "$K" --clear
eq "$OUT" "Cleared [$K]." "clear text"
TXT=$(cat "$STATE")
hasnt "$TXT" "- [$K]" "cleared line removed"
has "$TXT" "- [other] 1/3 strike;" "other strike kept"
has "$TXT" "circuit breaker cleared for [$K]: fixed" "clear logged"
st strike "$K" --clear
eq "$OUT" "No strikes recorded for [$K]." "clear again"
st strike --list
eq "$CODE" 0 "list under 3 exit 0"
st strike "bad]key" x
eq "$ERR" "state: the problem name cannot contain [ or ]" "bracket refused"
st strike
eq "$ERR" "state: name the problem: state.py strike \"PC 0x00123456 hang\" \"what you tried\"" "no key"
st strike other "worked" --clear
has "$(cat "$STATE")" "circuit breaker cleared for [other]: worked" "clear note"
st strike --list
eq "$OUT" "No open strikes." "nothing open"
grep -v -e '^## Circuit breaker' -e '^One line per open problem' "$STATE" > "$TMP/s4"; cp "$TMP/s4" "$STATE"
st strike "new" "n1"
eq "$(tail -2 "$STATE" | stamp)" "## Circuit breaker
- [new] 1/3 strike; last: n1 (STAMP)" "a missing section is added at the end"
has "$(sed -n '/^## Session log/,/^## Circuit breaker/p' "$STATE" | stamp)" "- STAMP  strike 1/3 on [new]: n1

## Circuit breaker" "the log stays in its section, a blank line before the next"
cp "$TMP/s3" "$STATE"

# ---- design (test_design_doc_created_once)
st design
eq "$CODE" 0 "design exit 0"
eq "$OUT" "Created $WS/console/design.md" "design text"
eq "$(head -1 "$WS/console/design.md")" "# Design doc: Test Racer (SLUS-20312)" "design filled"
has "$(tail -1 "$STATE")" "design.md created" "design logged"
st design
eq "$CODE" 1 "design once"
eq "$ERR" "state: $WS/console/design.md already exists; edit it instead" "design refusal"

# ---- finding the state file (test_state_found_by_walking_up)
OUTW=$(cd "$WS/console/disc" && $T "${ST[@]}" show --json | jq -r .state)
eq "$OUTW" "$STATE" "found by walking up"
OUTW=$(cd "$WS/console" && $T "${ST[@]}" show --json | jq -r .state)
eq "$OUTW" "$STATE" "found from inside console/"
mkdir -p "$TMP/lost"; ERRW=$(cd "$TMP/lost" && $T "${ST[@]}" show 2>&1 >/dev/null)
eq "$ERRW" "state: no state file at $(cd "$TMP/lost" && pwd -P)/console/STATE.md: run \`state.py init\` first" "not found: names ./console/STATE.md"

# ---- usage
run "${ST[@]}"
eq "$CODE" 2 "no command exit 2"
has "$ERR" "state: error: the following arguments are required: COMMAND" "no command message"
run "${ST[@]}" log
has "$ERR" "the following arguments are required: message" "log needs a message"
for c in detect init design show set phase log strike; do run "${ST[@]}" $c --help; eq "$CODE" 0 "$c --help"; done
run "${ST[@]}" strike --help
has "$OUT" "usage: state strike [-h] [--workspace PATH] [--clear] [--list] [key] [note]" "strike usage line"
run "${ST[@]}" --help
has "$OUT" "set       record one field: title, serial," "set help lists the fields"

done_ console-reverse/state

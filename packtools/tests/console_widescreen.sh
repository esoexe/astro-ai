#!/usr/bin/env bash
# console-graphics/widescreen: the TestWidescreen and TestCli (widescreen part) cases of
# console-skill/tests/test_graphics.py through the CLI, plus exact JSON keys and text, the --out pnach (parsed back
# with console-mod/pnach, as the upstream test does with pnach.py), the PCSX2 CRC, serial names and the FOV table.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkwself.js"
LIBMOD="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-console/skills/console-mod"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
WS_TEXT="3C023FAA 3442AAAB 44826000 3C033F40 44831000 03E00008 00000000"
WS_DATA="f:4/3 f:0.75 f:640/448 f:1 f:448/512"
node "$MK" SLUS_999.99 "$WS_TEXT" "$WS_DATA"
E=SLUS_999.99
W=(console-graphics/widescreen)
j()  { echo "$OUT" | jq -c "$1"; }
sj() { run "${W[@]}" scan "$@" --json; }
PCRC=$(node -e 'const b=require("fs").readFileSync(process.argv[1]);let x=0;for(let i=0;i+4<=b.length;i+=4)x=(x^b.readUInt32LE(i))>>>0;console.log(x.toString(16).toUpperCase().padStart(8,"0"))' "$E")

# ---- test_scan_candidates_and_16_9_values
sj "$E"; eq "$CODE" 0 "scan exit 0"
eq "$(j '[.candidates[].addr] | sort')" '["0x00100000","0x0010000C","0x00200000","0x00200004","0x00200008","0x00200010"]' "candidates"
eq "$(j '[.candidates[].addr]')" '["0x00100000","0x0010000C","0x00200000","0x00200004","0x00200008","0x00200010"]' "sorted by address"
by() { j "[.candidates[] | select(.addr == \"$1\")][0] | $2"; }
eq "$(by 0x00200000 .wide_bits)" '"0x3FE38E39"' "16:9 = 1.7777778"
eq "$(by 0x00200000 .pnach)" '["patch=1,EE,00200000,word,3FE38E39"]' "data pnach"
eq "$(by 0x00200004 .wide_bits)" '"0x3F100000"' "0.5625"
eq "$(by 0x00200008 .wide_value)" '"1.90476191"' "640/448 widened"
eq "$(by 0x00200010 .wide_value)" '"0.65625"' "448/512 narrowed"
eq "$(by 0x00100000 .pnach)" '["patch=1,EE,00100000,word,3C023FE3","patch=1,EE,00100004,word,34428E39"]' "lui+ori pnach"
eq "$(by 0x0010000C .pnach)" '["patch=1,EE,0010000C,word,3C033F10"]' "lone lui pnach"
eq "$(j .pcsx2_pnach)" "\"SLUS-99999_$PCRC.pnach\"" "PCSX2 pnach name (serial + XOR CRC)"
has "$(j .honest)" "cannot know" "honest"
eq "$(j 'keys_unsorted')" '["elf","pcsx2_pnach","candidates","honest","out"]' "scan JSON keys"; eq "$(j .out)" null "out null"
eq "$(by 0x00100000 'keys_unsorted')" '["kind","addr","bits","value","target","where","lui","lo","lo_op","mtc1","freg","reg","wide_value","wide_bits","notes","words","pnach"]' "code candidate keys"
eq "$(by 0x00200000 'keys_unsorted')" '["kind","addr","bits","value","target","where","read_by","wide_value","wide_bits","notes","words","pnach"]' "data candidate keys"
eq "$(by 0x00100000 .words)" '[{"addr":"0x00100000","new":"0x3C023FE3"},{"addr":"0x00100004","new":"0x34428E39"}]' "words"
eq "$(by 0x00200008 '[.target, .where, .value, .bits]')" '["640/448 frame","segment 1 (RW)","1.42857146","0x3FB6DB6E"]' "target and where"
sj "$E" --limit 2; eq "$(j '[.candidates[].addr]')" '["0x00100000","0x0010000C"]' "--limit"

# ---- test_lone_lui_rounding_note
node "$MK" LUI.ELF "3C033FAA 44831000 03E00008 0" "0"
sj LUI.ELF; eq "$(j .candidates)" '[]' "1.328 is outside the 0.05% net"; eq "$CODE" 1 "no candidates exit 1"
node "$MK" LUI2.ELF "3C033FA0 44831000 03E00008 0" "0"
sj LUI2.ELF; has "$(j '.candidates[0].notes[0]')" "lone lui" "lone lui note"
eq "$(j '.candidates[0].pnach')" '["patch=1,EE,00100000,word,3C033FD5"]' "rounded lui"
eq "$(j '.candidates[0] | [.notes, .wide_value, .wide_bits]')" '[["a lone lui cannot hold 1.66666663 exactly: uses 1.6640625"],"1.6640625","0x3FD55555"]' "note, rounded wide_value, exact wide_bits"
eq "$(j '.pcsx2_pnach | test("^<SERIAL>_[0-9A-F]{8}\\.pnach$")')" true "no serial in the name"
run "${W[@]}" scan LUI2.ELF; has "$OUT" "        note: a lone lui cannot hold 1.66666663 exactly: uses 1.6640625" "note text"
# an unchanged half is not written (lui+ori where only the low half moves)
node "$MK" SAME.ELF "3C023FAA 3442AAAB 44826000 03E00008 0" "0"
sj SAME.ELF; eq "$(j '.candidates[0].words | length')" 2 "both halves change for 4:3"

# ---- test_out_pnach_parses_with_console_mod
OUTP="$TMP/console/patches/widescreen-try.pnach"
run "${W[@]}" scan "$E" --out "$OUTP"; eq "$CODE" 0 "--out exit 0"; has "$OUT" "cannot know" "honest text"
has "$OUT" "wrote $OUTP (one [section] per try). Copy the sections into PCSX2's SLUS-99999_$PCRC.pnach" "wrote line"
eq "$(grep -c 'gsaspectratio=16:9' "$OUTP")" 6 "6 sections"
run --skill-dir "$LIBMOD" console-mod/pnach parse "$OUTP" --json
eq "$(j '[(.sections | length), .errors, (.writes | length)]')" '[6,[],7]' "parses with console-mod pnach"
eq "$(head -c 200 "$OUTP" | head -n 3)" "gametitle=SLUS_999.99 widescreen candidates (console-graphics widescreen.py)
comment=Enable ONE section at a time. Each changes one 4:3 value to its 16:9 value." "pnach header"
has "$(cat "$OUTP")" "[Widescreen try 01: 1.33333337 at 0x00100000 (code, lui+ori)]
author=console-graphics widescreen.py
description=4:3 width/height 1.33333337 -> 1.77777779. Right if the view widens without stretching.
gsaspectratio=16:9
patch=1,EE,00100000,word,3C023FE3
patch=1,EE,00100004,word,34428E39
" "first section"
has "$(cat "$OUTP")" "[Widescreen try 03: 1.33333337 at 0x00200000 (data, segment 1 (RW))]" "data section title"
has "$(cat "$OUTP")" "[Widescreen try 02: 0.75 at 0x0010000C (code, lui)]" "lone lui section title"
eq "$(tail -c 2 "$OUTP" | od -An -c | tr -d ' ')" '0\n' "ends with one newline"
run "${W[@]}" scan "$E" --out "$OUTP"; eq "$CODE" 2 "exists"; eq "$ERR" "widescreen: error: $OUTP exists: pass --force to overwrite it" "exists message"
run "${W[@]}" scan "$E" --out "$OUTP" --force; eq "$CODE" 0 "--force"
sj "$E" --out "$TMP/o2.pnach"; eq "$(j .out)" "\"$TMP/o2.pnach\"" "out in JSON"
node "$MK" NONE.ELF "03E00008 0" "f:2"
run "${W[@]}" scan NONE.ELF --out none.pnach; eq "$CODE" 1 "nothing: exit 1 still writes"
eq "$(cat none.pnach)" "gametitle=NONE.ELF widescreen candidates (console-graphics widescreen.py)
comment=Enable ONE section at a time. Each changes one 4:3 value to its 16:9 value." "empty pnach"

# ---- test_text_output
run "${W[@]}" scan "$E"; has "$OUT" "try 01" "try 01"; has "$OUT" "-> 1.77777779" "16:9 value"; has "$OUT" "/console-mod" "/console-mod"
has "$OUT" $'try 01  CODE 0x00100000  lui+ori -> mtc1 $f12   1.33333337 (4:3 width/height) -> 1.77777779\n        patch=1,EE,00100000,word,3C023FE3' "code line"
has "$OUT" $'try 03  DATA 0x00200000  segment 1 (RW)   1.33333337 (4:3 width/height) -> 1.77777779\n        patch=1,EE,00200000,word,3FE38E39' "data line"
has "$OUT" "Write them as a pnach with --out console/patches/widescreen-try.pnach (PCSX2 wants SLUS-99999_$PCRC.pnach)." "no --out hint"
eq "$(echo "$OUT" | head -n 1)" "Widescreen candidates in SLUS_999.99: 6" "title"
eq "$(echo "$OUT" | tail -n 1)" "Found it? Bake it in with /console-mod (pnach.py toml / override), then stretch the port's output to 16:9 (see reference/widescreen.md)." "last line"
# read by (a data value read through lui)
node "$MK" READ.ELF "3C080020 C5040000 03E00008 0" "f:4/3"
run "${W[@]}" scan READ.ELF; has "$OUT" "        read by: lwc1 \$f4, 0(\$t0) at 0x00100004" "read by line"
# serial names
cp "$E" SLES-123.45; sj SLES-123.45; eq "$(j .pcsx2_pnach | cut -c2-12)" "SLES-12345_" "SLES-123.45"
cp "$E" slps_123.45; sj slps_123.45; eq "$(j .pcsx2_pnach | cut -c2-12)" "SLPS-12345_" "lower case serial"
cp "$E" SCUS_12345; sj SCUS_12345; eq "$(j .pcsx2_pnach | cut -c2-12)" "SCUS-12345_" "dot optional"
cp "$E" ABCD_123.4; sj ABCD_123.4; eq "$(j .pcsx2_pnach | cut -c2-10)" "<SERIAL>_" "too short: no serial"
cp "$E" "SLUS_١٢٣.٤٥"; sj "SLUS_١٢٣.٤٥"; eq "$(j .pcsx2_pnach | cut -d_ -f1)" '"SLUS-١٢٣٤٥' "\\d matches Unicode digits (Python re)"

# ---- test_values_and_fov
run "${W[@]}" values --fov 60 --json; eq "$(j .fov)" '{"horizontal_4_3":60.0,"horizontal_16_9":75.1782,"radians_4_3":1.0471976,"radians_16_9":1.3121067}' "FOV 60"
has "$OUT" '"horizontal_4_3": 60.0,' "float 60.0"
eq "$(j '.aspects[0]')" '{"label":"4:3 width/height","value":"1.33333337","bits":"0x3FAAAAAB","wide_value":"1.77777779","wide_bits":"0x3FE38E39","rule":"x4/3"}' "aspect row"
eq "$(j '[.aspects[] | .rule]')" '["x4/3","x3/4","x4/3","x3/4","x4/3","x3/4","x4/3","x3/4"]' "rules"
eq "$(j '[.aspects[] | .wide_bits]')" '["0x3FE38E39","0x3F100000","0x3FF3CF3D","0x3F066666","0x3FC30C31","0x3F280000","0x3FD55555","0x3F19999A"]' "16:9 bits"
run "${W[@]}" values --json; eq "$(j .fov)" null "no --fov"; eq "$(j 'keys_unsorted')" '["aspects","fov"]' "values keys"
run "${W[@]}" values; has "$OUT" "2*atan" "rule text"
eq "$(echo "$OUT" | head -n 2)" "4:3 value                        bits        ->  16:9 value   bits        rule
4:3 width/height   1.33333337    0x3FAAAAAB  ->  1.77777779   0x3FE38E39  x4/3" "table header and row"
run "${W[@]}" values --fov 90; has "$OUT" "Horizontal FOV 90 deg at 4:3 -> 106.2602 deg at 16:9 (1.5707963 -> 1.8545904 radians)" "FOV 90 text"
run "${W[@]}" values --fov 45.5 --json; eq "$(j '.fov.horizontal_16_9')" "$(node -e 'const r=x=>x*(Math.PI/180),d=x=>x*(180/Math.PI);console.log(+d(2*Math.atan(Math.tan(r(45.5)/2)*((16/9)/(4/3)))).toFixed(4))')" "FOV 45.5 (node)"
for bad in 200 0 180 -5 nan; do run "${W[@]}" values --fov "$bad"; eq "$CODE" 2 "--fov $bad"; done
eq "$ERR" "widescreen: error: --fov must be an angle between 0 and 180 degrees" "fov message"

# ---- test_nothing_and_bad_elf
run "${W[@]}" scan NONE.ELF; eq "$CODE" 1 "nothing exit 1"; has "$OUT" "Widescreen candidates in NONE.ELF: 0" "nothing title"
printf '\177ELF\001\001' > bad; run "${W[@]}" scan bad; eq "$CODE" 2 "bad ELF"; hasnt "$ERR" "Traceback" "clean"; eq "$ERR" "widescreen: error: bad is not an ELF file" "bad message"
run "${W[@]}" scan missing; eq "$ERR" "widescreen: error: ELF not found: missing" "missing ELF"

# ---- TestCli
run "${W[@]}" --help; eq "$CODE" 0 "--help"; has "$OUT" "scan" "help scan"; has "$OUT" "values" "help values"
has "$OUT" "Exit codes: 0 ok, 1 no candidates, 2 bad input." "epilog"
for c in scan values; do run "${W[@]}" "$c" --help; eq "$CODE" 0 "$c --help"; done
run "${W[@]}"; eq "$CODE" 2 "no command"; has "$OUT" "usage:" "no command help"
run "${W[@]}" values; eq "$CODE" 0 "values exit 0"
run "${W[@]}" values --fov x; has "$ERR" "argument --fov: invalid float value: 'x'" "invalid float"
done_ "console-graphics/widescreen"

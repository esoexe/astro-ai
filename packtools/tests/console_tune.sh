#!/usr/bin/env bash
# console-physics/tune: the TestFloatRanges, TestAnalysis, TestScan, TestFps, TestPatch and TestCli cases of
# console-skill/tests/test_physics.py through the CLI (scan --json stands in for analyze(), patch words for the
# describer), plus exact JSON keys and text, Python float formatting, argparse errors and a bit-pattern oracle in node.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mktuneelf.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
node "$MK" standard SLUS_999.99
node "$MK" nosections NOSEC
node "$MK" rwx RWX
node "$MK" two TWO
node "$MK" empty EMPTY
E=SLUS_999.99
TU=(console-physics/tune)
j()  { echo "$OUT" | jq -c "$1"; }
sj() { run "${TU[@]}" scan "$@" --json; }                     # scan --json on any ELF
pj() { run "${TU[@]}" patch "$E" --json "$@"; }               # patch --json on the standard ELF
f2u() { V="$1" node -e 'const b=Buffer.alloc(4);b.writeFloatLE(+process.env.V,0);console.log("0x"+b.readUInt32LE(0).toString(16).toUpperCase().padStart(8,"0"))'; }

# ---- TestFloatRanges: ranges are tight (every data word is a hit exactly when |float32(w) - v| <= tol)
cat > oracle.js <<'EOF'
const fs = require('fs');
const [v, tol] = process.argv.slice(2, 4).map(Number);
const f2u = (f) => { const b = Buffer.alloc(4); b.writeFloatLE(f, 0); return b.readUInt32LE(0); };
const u2f = (u) => { const b = Buffer.alloc(4); b.writeUInt32LE(u >>> 0, 0); return b.readFloatLE(0); };
const lo = v - tol, hi = v + tol, ws = new Set();
for (const x of [lo, hi, -lo, -hi, v]) {
  const base = f2u(x);
  for (let d = -2; d <= 2; d++) { const w = base + d; if (w >= 0 && w <= 0xFFFFFFFF && isFinite(u2f(w))) ws.add(w >>> 0); }
}
const list = [...ws].slice(0, 64); while (list.length < 64) list.push(0);
const hex = (w) => w.toString(16).toUpperCase().padStart(8, '0');
if (!process.argv[4]) { console.log(list.map(hex).join(' ')); process.exit(0); }
const got = new Set(JSON.parse(fs.readFileSync(process.argv[4], 'utf8')).hits.filter(h => h.kind === 'data').map(h => parseInt(h.addr, 16)));
const errs = [];
list.forEach((w, i) => {
  const d = Math.abs(u2f(w) - v), hit = got.has(0x00200000 + 4 * i);
  if (hit && d > tol * (1 + 1e-9)) errs.push('extra ' + hex(w)); if (!hit && d <= tol * (1 - 1e-9)) errs.push('missing ' + hex(w));
});
console.log(errs.length ? errs.join(' ') : 'ok ' + got.size);
EOF
for vt in "9.8 0.01" "0.016666668 1e-6" "-9.8 0.05" "0.5 1.0" "1e-30 1e-31" "3e38 1e37"; do
    read -r V TL <<<"$vt"
    WS=$(node oracle.js "$V" "$TL")
    # shellcheck disable=SC2086
    node "$MK" words RANGE $WS
    run "${TU[@]}" scan RANGE --value="$V" --tol "$TL" --limit 0 --json; echo "$OUT" > range.json
    has "$(node oracle.js "$V" "$TL" range.json)" "ok " "tight range $V +/- $TL"
done
# test_range_straddling_zero: 0.5 +/- 1.0 also finds 0.0, -0.0 and -0.5
node "$MK" words ZERO 00000000 80000000 BF000000 BF000001 3FC00000 3FC00001
sj ZERO --value 0.5 --tol 1.0 --limit 0
eq "$(j '[.hits[] | select(.kind == "data") | [.addr, .value]][0:5]')" '[["0x00200000","0"],["0x00200004","-0"],["0x00200008","-0.5"],["0x00200010","1.5"],["0x00200018","0"]]' "range straddling zero (both signs, -0 kept, padding zeros)"
eq "$(j '[.hits[] | select(.addr == "0x0020000C" or .addr == "0x00200014")]')" '[]' "just past both ends excluded"
# test_check_value
for bad in 0 nan inf 1e39 -inf; do
    run "${TU[@]}" scan "$E" --value "$bad"; eq "$CODE" 2 "check_value $bad"; hasnt "$ERR" "Traceback" "check_value $bad clean"
done
run "${TU[@]}" scan "$E" --value 0; eq "$ERR" "tune: error: 0.0 is in every zero word: search for a non-zero value (the one the game shows, or 1.0)" "zero message"
run "${TU[@]}" scan "$E" --value nan; eq "$ERR" "tune: error: the value must be a finite number" "nan message"
run "${TU[@]}" scan "$E" --value 1e39; eq "$ERR" "tune: error: 1e+39 does not fit in a float32" "too big message (Python repr)"
run "${TU[@]}" scan "$E" --value 3.5e38; eq "$ERR" "tune: error: 3.5e+38 does not fit in a float32" "between 3.4e38 and inf"
# test_split_hi_lo through the round trip (below) and patch words; test_describe through text and patch words
pj --addr 0x00100008 --float 1; eq "$CODE" 2 "lwc1 is not a constant"
eq "$ERR" "tune: error: 0x00100008 holds 'lwc1 \$f1, -32768(\$gp)', not part of a float constant: give a data float or the lui/ori/addiu of a constant (tune.py scan lists them)" "describe lwc1 negative offset"
pj --addr 0x00100034 --float 1; has "$ERR" "holds 'jr \$ra'" "describe jr"
pj --addr 0x00100038 --float 1; has "$ERR" "holds 'nop'" "describe nop"
pj --addr 0x0010004C --float 1; has "$ERR" "holds 'j 0x00100000'" "describe j with a target"
pj --addr 0x00100040 --float 1; has "$ERR" "holds 'addu'" "describe special funct"
pj --addr 0x0010002C --float 1; eq "$CODE" 0 "lui with no low half"
eq "$(j '[.kind, .words[0].original_asm, .words[0].new_asm, .pnach[0]]')" '["code lui","lui $t0, 0x0020","lui $t0, 0x3F80","patch=1,EE,0010002C,word,3C083F80"]' "describe lui (no mtc1)"
eq "$(j .used_by)" '"no mtc1 seen nearby: check it really is a float"' "no mtc1 seen"
pj --addr 0x00100014 --float 1; eq "$(j '[.words[] | .original_asm]')" '["lui $v0, 0x411C","ori $v0, $v0, 0xCCCD"]' "from the mtc1"

# ---- TestAnalysis: gp, consts and xrefs
sj "$E" --value 9.8; eq "$(j .gp)" '"0x00208000"' "gp from lui+addiu"
eq "$(j '[.hits[] | select(.kind == "code") | [.mtc1, .bits, .lo_op, .freg, .reg]]')" '[["0x00100014","0x411CCCCD","ori",12,2]]' "9.8 lui+ori const"
sj "$E" --value 12.5; eq "$(j '[.hits[] | select(.kind == "code") | [.bits, .lo, .lo_op, .mtc1]]')" '[["0x41480000",null,null,"0x0010001C"]]' "lone lui const"
sj "$E" --value 0.016666668; eq "$(j '[.hits[] | select(.kind == "code") | [.bits, .lo, .lo_op]]')" '[["0x3C888889","0x00100024","addiu"]]' "lui+addiu const"
sj "$E" --value 30; eq "$(j '[.hits[] | select(.kind == "code")]')" '[]' "register overwritten before the mtc1"
sj "$E" --value 120; eq "$CODE" 1 "not followed across a jump: nothing"; eq "$(j .hits)" '[]' "120 not tracked"
sj "$E" --value 60; eq "$(j '[.hits[] | [.kind, .addr, .mtc1]]')" '[["code","0x00100058","0x0010005C"]]' "60.0 after the jump"
sj "$E" --value 9.8; eq "$(j '[.hits[] | select(.kind == "data") | .read_by]')" '[[{"at":"0x00100008","insn":"lwc1 $f1, -32768($gp)","via":"$gp"}]]' "\$gp xref"
sj "$E" --value 0.016666668; eq "$(j '[.hits[] | select(.kind == "data") | .read_by]')" '[[{"at":"0x00100030","insn":"lwc1 $f4, 4($t0)","via":"lui"}]]' "lui xref"
# test_regions_with_and_without_sections
sj NOSEC --value 9.8; eq "$(j '[.hits[] | [.kind, .addr, .where]]')" '[["code","0x0010000C","segment 0 (RX)"],["data","0x00200000","segment 1 (RW)"]]' "no sections: segments"
sj RWX --value 9.8; eq "$(j '[.hits[] | [.kind, .addr, .where]]')" '[["code","0x0010000C","segment 0 (RWX)"],["data","0x00100068","segment 0 (RWX)"]]' "RWX: code and data scanned"
eq "$(j '.hits[1].read_by')" '[]' "RWX data: gp read points elsewhere"
sj RWX --value 30; eq "$(j '[.hits[] | [.kind, .addr]]')" '[["data","0x00100070"]]' "RWX data word"
sj RWX --value 1; eq "$(j '[.hits[] | [.kind, .addr]]')" '[["data","0x00100084"]]' "RWX last float"

# ---- TestScan
sj "$E" --value 9.8; eq "$CODE" 0 "scan exit 0"
eq "$(j '[.hits[] | [.kind, .addr]]')" '[["code","0x0010000C"],["data","0x00200000"]]' "scan finds data and code"
eq "$(j '.hits[1].where')" '".data"' "where .data"
eq "$(j 'keys_unsorted')" '["elf","value","tol","gp","hits"]' "scan JSON keys"
eq "$(j '.hits[0] | keys_unsorted')" '["kind","addr","bits","value","target","where","lui","lo","lo_op","mtc1","freg","reg"]' "code hit keys"
eq "$(j '.hits[1] | keys_unsorted')" '["kind","addr","bits","value","target","where","read_by"]' "data hit keys"
has "$OUT" '"value": 9.8,' "value as a Python float"
has "$OUT" '"tol": 0.009800000000000001,' "default tol repr"
eq "$(j '[.hits[0].value, .hits[0].target]')" '["9.80000019","9.8"]' "value %.9g, target fmt(value)"
sj "$E" --value 9.8 --tol 0.02; has "$(j '[.hits[].addr]')" '"0x0020000C"' "tolerance: 9.81"
sj "$E" --value 9.8 --tol 0; eq "$(j '[.hits[].addr]')" '[]' "--tol 0 matches only the exact double"
has "$OUT" '"tol": 0.0,' "tol 0.0"
sj "$E" --value -9.8; eq "$(j '[.hits[].addr]')" '["0x00200010"]' "negative value"
sj "$E" --value 9.80000019073486328125 --tol 0; eq "$(j '[.hits[].addr]')" '["0x0010000C","0x00200000"]' "--tol 0 with the float32's exact value"
sj "$E" --value 1e-3; eq "$CODE" 1 "--json nothing: exit 1"; has "$OUT" '"tol": 1e-06,' "tol floor 1e-6"; has "$OUT" '"value": 0.001,' "0.001 repr"
sj "$E" --value 12.5; eq "$(j '.hits[0].lo')" 'null' "lone lui lo null"; eq "$(j '.hits[0].mtc1')" '"0x0010001C"' "lone lui mtc1"
sj "$E" --value 0.016666668; eq "$(j '[.hits[] | [.kind, .addr]] | sort')" '[["code","0x00100020"],["data","0x00200004"]]' "lui+addiu and data"
sj "$E" --value 30; eq "$(j '[.hits[] | [.kind, .addr]]')" '[["data","0x00200008"]]' "killed register not reported"
run "${TU[@]}" scan "$E" --value 120; eq "$CODE" 1 "nothing found exit 1"; has "$OUT" "Nothing" "Nothing"
eq "$OUT" "120 (+/- 0.12) in SLUS_999.99: 0 in data, 0 built in code

Nothing. Try a wider --tol, the value in other units (9.8 m/s2 may be 980 cm/s2, 0.98 per frame, or 9.8/60), or find it while playing with PCSX2's memory search." "nothing text"
run "${TU[@]}" scan "$E" --value 9.8
eq "$OUT" "9.8 (+/- 0.0098) in SLUS_999.99: 1 in data, 1 built in code

CODE  0x0010000C  .text                  9.80000019 (0x411CCCCD) built by lui + ori, then mtc1 into \$f12
        0x00100004  279C8000  addiu \$gp, \$gp, 0x8000
        0x00100008  C7818000  lwc1 \$f1, -32768(\$gp)
      > 0x0010000C  3C02411C  lui \$v0, 0x411C
      > 0x00100010  3442CCCD  ori \$v0, \$v0, 0xCCCD
      > 0x00100014  44826000  mtc1 \$v0, \$f12
        0x00100018  3C014148  lui \$at, 0x4148
        0x0010001C  44811000  mtc1 \$at, \$f2

DATA  0x00200000  .data                  9.80000019 (0x411CCCCD)
      read by: lwc1 \$f1, -32768(\$gp) at 0x00100008 (\$gp)

Next: try one in PCSX2:  tune.py patch SLUS_999.99 --addr <address> --float <new value>" "scan text, exactly"
run "${TU[@]}" scan "$E" --value 30
has "$OUT" "      read by: nothing found by lui or \$gp addressing (check xrefs in Ghidra)" "no readers"
run "${TU[@]}" scan "$E" --value 60
has "$OUT" "CODE  0x00100058  .text                  60 (0x42700000) built by lui, then mtc1 into \$f7" "lone lui text"
has "$OUT" "        0x00100050  00000000  nop" "context starts two words back"
run "${TU[@]}" scan "$E" --value 9.8 --limit 1; has "$OUT" "(first 1 shown: --limit to see more)" "limit note"
sj "$E" --value 9.8 --limit 1; eq "$(j '[.hits[].kind]')" '["code"]' "limit keeps the first"
sj "$E" --value 9.8 --limit=-1; eq "$(j '[.hits[].kind]')" '["code"]' "negative limit drops from the end (slice)"
sj "$E" --value 9.8 --limit 0; eq "$(j '.hits | length')" 2 "limit 0 = all"
run "${TU[@]}" scan "$E" --value 9.8 --limit 0; has "$OUT" "(first 0 shown: --limit to see more)" "limit 0 still notes (len >= 0)"
run "${TU[@]}" scan "$E" --value 9.8 --limit 5; hasnt "$OUT" "first 5 shown" "no note under the limit"
# test_bad_inputs
for args in "--value 0" "--value nan" "--value 9.8 --tol -1"; do
    # shellcheck disable=SC2086
    run "${TU[@]}" scan "$E" $args; eq "$CODE" 2 "bad: $args"; hasnt "$ERR" "Traceback" "clean: $args"
done
run "${TU[@]}" scan "$E" --value 9.8 --tol=-1; eq "$ERR" "tune: error: --tol must be zero or more" "tol message"
printf '\177ELF' > short; printf 'MZ' > notelf; head -c 80 /dev/zero >> notelf
node -e 'const fs=require("fs");const b=fs.readFileSync("SLUS_999.99");b.writeUInt16LE(40,18);fs.writeFileSync("arm",b);fs.writeFileSync("trunc",fs.readFileSync("SLUS_999.99").subarray(0,100));
const c=fs.readFileSync("SLUS_999.99");c[4]=2;fs.writeFileSync("elf64",c);const d=fs.readFileSync("SLUS_999.99");d.writeUInt16LE(0,44);fs.writeFileSync("nophdr",d)'
for n in short notelf arm trunc elf64 nophdr; do run "${TU[@]}" scan "$n" --value 9.8; eq "$CODE" 2 "bad ELF $n"; done
run "${TU[@]}" scan short --value 9.8; eq "$ERR" "tune: error: short is not an ELF file" "short message"
run "${TU[@]}" scan arm --value 9.8; eq "$ERR" "tune: error: arm is not a MIPS ELF (machine 40, the PS2 EE is 8)" "machine message"
run "${TU[@]}" scan trunc --value 9.8; eq "$ERR" "tune: error: trunc is truncated or has a bad program header table" "truncated phdr message"
head -c 300 SLUS_999.99 > trunc2; run "${TU[@]}" scan trunc2 --value 9.8; eq "$ERR" "tune: error: trunc2 is truncated: segment 1 runs past the end of the file" "truncated segment message"
run "${TU[@]}" scan elf64 --value 9.8; eq "$ERR" "tune: error: elf64 is not a 32-bit little-endian ELF (PS2 EE ELFs are)" "class message"
run "${TU[@]}" scan nophdr --value 9.8; eq "$ERR" "tune: error: nophdr has no loadable (PT_LOAD) segments" "no segments message"
run "${TU[@]}" scan missing --value 1; eq "$CODE" 2 "missing ELF"; eq "$ERR" "tune: error: ELF not found: missing" "missing message"
run "${TU[@]}" scan . --value 1; eq "$ERR" "tune: error: ELF not found: ." "a directory is not a file"
run "${TU[@]}" scan "$E" --value abc; eq "$CODE" 2 "argparse float"; has "$ERR" "argument --value: invalid float value: 'abc'" "invalid float"
run "${TU[@]}" scan "$E"; eq "$CODE" 2 "--value required"; has "$ERR" "the following arguments are required: --value" "required message"
run "${TU[@]}" scan "$E" --value 9.8 --limit x; has "$ERR" "argument --limit: invalid int value: 'x'" "invalid int"

# ---- TestFps
run "${TU[@]}" fps "$E"; eq "$CODE" 0 "fps exit 0"
for a in 0x00200004 0x00100020 0x00200008 0x00100058; do has "$OUT" "$a" "fps $a"; done
has "$OUT" "frame time" "caveat"; has "$OUT" "test in PCSX2 first" "caveat test first"
has "$OUT" "Frame-timing candidates in SLUS_999.99: 2 in data, 2 built in code" "fps title"
has "$OUT" "built by lui + addiu, then mtc1 into \$f3  [1/60 s, one NTSC frame]" "target label"
has "$OUT" "DATA  0x00200008  .data                  30 (0x41F00000)  [30 per second]" "data label"
eq "$(echo "$OUT" | tail -n 6 | head -n 1)" "These are candidates, not answers. Many games pace frames with a vsync count (wait 2 vsyncs = 30 fps)," "caveat lines"
run "${TU[@]}" fps "$E" --json; eq "$(j '.hits | length')" 4 "fps json 4 hits"; has "$(j .caveat)" "60 fps" "caveat 60 fps"
eq "$(j 'keys_unsorted')" '["elf","hits","caveat"]' "fps JSON keys"
eq "$(j '[.hits[] | .target]')" '["1/60 s, one NTSC frame","60 per second","1/60 s, one NTSC frame","30 per second"]' "fps targets by address"
has "$(j .caveat)" "(wait 2 vsyncs = 30 fps), an integer" "caveat joined with spaces"
run "${TU[@]}" fps EMPTY; eq "$CODE" 1 "fps nothing"; has "$OUT" ": 0 in data, 0 built in code" "fps nothing title"
run "${TU[@]}" fps EMPTY --json; eq "$CODE" 1 "fps json nothing"
run "${TU[@]}" fps "$E" --limit 1 --json; eq "$(j '[.hits[].addr]')" '["0x00100020"]' "fps limit"
run "${TU[@]}" fps missing; eq "$CODE" 2 "fps missing"; hasnt "$ERR" "Traceback" "fps missing clean"

# ---- TestPatch
pj --addr 00200000 --float 12.5; eq "$CODE" 0 "data patch"
eq "$(j '[.kind, .pnach, .toml, .override, .old_value, .read_by[0].at]')" '["data",["patch=0,EE,00200000,word,41480000"],[],"Ps2FastWrite32(rdram, 0x00200000u, 0x41480000u); // 12.5f","9.80000019","0x00100008"]' "data patch result"
eq "$(j 'keys_unsorted')" '["elf","addr","new_value","new_bits","kind","old_value","old_bits","pnach","toml","where","read_by","override"]' "data patch keys"
eq "$(j '[.old_bits, .where, .new_value, .new_bits]')" '["0x411CCCCD",".data","12.5","0x41480000"]' "data patch fields"
for a in 0x0010000C 0x00100010 0x00100014; do
    pj --addr "$a" --float 12.5; eq "$CODE" 0 "lui+ori from $a"
    eq "$(j '[.kind, .pnach, .used_by]')" '["code lui+ori",["patch=1,EE,0010000C,word,3C024148","patch=1,EE,00100010,word,34420000"],"mtc1 $f12 at 0x00100014"]' "lui+ori patch from $a"
    has "$(echo "$OUT" | jq -r '.toml[0]')" $'address = "0x0010000C"\nvalue = "0x3C024148"' "toml block from $a"
done
eq "$(j 'keys_unsorted')" '["elf","addr","new_value","new_bits","kind","old_value","words","pnach","toml","used_by"]' "code patch keys"
echo "$OUT" > r.json; node "$MK" apply "$E" r.json PATCHED
sj PATCHED --value 12.5 --tol 0; eq "$(j '[.hits[] | select(.mtc1 == "0x00100014") | .bits]')" '["0x41480000"]' "lui+ori round trip"
for v in 0.03333333333333333 0.02 -3.75 0.1; do
    pj --addr 0x00100020 --float "$v"; eq "$(j .kind)" '"code lui+addiu"' "addiu kind $v"
    echo "$OUT" > r.json; node "$MK" apply "$E" r.json PATCHED
    want=$(f2u "$v")
    sj PATCHED --value="$v"; eq "$(j '[.hits[] | select(.mtc1 == "0x00100028") | .bits]')" "[\"$want\"]" "lui+addiu carry round trip $v"
done
pj --addr 0x00100020 --float 0.1; eq "$(j '[.words[] | .new_asm]')" '["lui $a0, 0x3DCD","addiu $a0, $a0, 0xCCCD"]' "addiu carry: hi rounded up"
pj --addr 0x0010000C --float 4.9; eq "$(j '[.pnach, (.toml | length), (.words | length)]')" '[["patch=1,EE,0010000C,word,3C02409C"],1,2]' "unchanged words are not patched"
pj --addr 0x0010000C --float 9.8; eq "$(j .pnach)" '[]' "already holds"; has "$(j .note)" "already holds" "already holds note"
run "${TU[@]}" patch "$E" --addr 0x0010000C --float 9.8; eq "$OUT" "0x0010000C  code lui+ori in SLUS_999.99
  value       9.80000019 -> 9.80000019 (0x411CCCCD)
  0x0010000C  0x3C02411C  lui \$v0, 0x411C          -> 0x3C02411C  lui \$v0, 0x411C
  0x00100010  0x3442CCCD  ori \$v0, \$v0, 0xCCCD     -> 0x3442CCCD  ori \$v0, \$v0, 0xCCCD
  used by     mtc1 \$f12 at 0x00100014
  NOTE: the constant already holds this value: nothing to patch

Test in PCSX2 first (add to the game's .pnach, see /console-mod pnach.py crc):" "already holds text"
pj --addr 0x00100018 --float 10; eq "$CODE" 0 "lone lui exact"; eq "$(j .pnach)" '["patch=1,EE,00100018,word,3C014120"]' "lone lui 10"
pj --addr 0x00100018 --float 9.8; eq "$CODE" 1 "lone lui rounds"; eq "$(j .new_bits)" '"0x411D0000"' "nearest bits"; has "$(j .note)" "nearest" "nearest note"
eq "$(j 'keys_unsorted')" '["elf","addr","new_value","new_bits","kind","note","old_value","words","pnach","toml","used_by"]' "lone lui keys (note before old_value)"
eq "$(j '[.new_value, .old_value, .note]')" '["9.8125","12.5","9.8 needs a low half but this constant is a lone lui: using the nearest value it can hold, 9.8125 (off by 0.0125). For the exact value, write the C++ by hand (/console-mod)."]' "lone lui note text"
pj --addr 0x00100018 --float 9.8 --exact; eq "$CODE" 2 "--exact refuses"; has "$ERR" "lone lui" "--exact message"
eq "$ERR" "tune: error: 9.8 needs a low half (0x411CCCCD) but this constant is a lone lui with no room for one; the nearest lui-only value is 9.8125" "--exact text"
pj --addr 0x00100018 --float 12.5; eq "$(j '[.pnach, .note]')" '[[],"the constant already holds this value: nothing to patch"]' "lone lui already holds"
pj --addr 0x00200800 --float 2; eq "$(j '[.kind, .pnach]')" '["runtime",["patch=1,EE,00200800,word,40000000"]]' "runtime value"; has "$(j .note)" "frame-hook" "frame-hook note"
eq "$(j 'keys_unsorted')" '["elf","addr","new_value","new_bits","kind","pnach","toml","note"]' "runtime keys"
has "$(j .note)" "0x00200800 is zero-filled (BSS): the game sets" "BSS note"
pj --addr 0x01000000 --float 2; has "$(j .note)" "outside the ELF" "outside the ELF"
pj --addr 0x20200000 --float 2; eq "$(j '[.kind, .pnach[0], .old_bits]')" '["data","patch=0,EE,20200000,word,40000000","0x411CCCCD"]' "uncached mirror reads the data, keeps the address"
pj --addr 0x00200000 --float 1 --place 1; eq "$(j '.pnach[0]')" '"patch=1,EE,00200000,word,3F800000"' "place override"
pj --addr 0x0010000C --float 1 --place 0; eq "$(j '.pnach[0]')" '"patch=0,EE,0010000C,word,3C023F80"' "place 0 for code"
pj --addr 0x00200000 --float -0; eq "$(j '[.new_value, .new_bits]')" '["-0","0x80000000"]' "-0.0 float"
run "${TU[@]}" patch TWO --addr 0x00200000 --float 3 --json; has "$(j .note)" "changes every one" "shared constant note"
eq "$(j .note)" '"2 instructions read this constant: changing it changes every one of them. To change only one, patch that instruction'"'"'s code instead (Ghidra)."' "shared note text"
run "${TU[@]}" patch "$E" --addr 0x0010000C --float 12.5; has "$OUT" "9.80000019 -> 12.5" "patch text value"; has "$OUT" "[[patches.instructions]]" "patch text toml"
has "$OUT" $'  [[patches.instructions]]\n  address = "0x0010000C"\n  value = "0x3C024148"\n  [[patches.instructions]]' "toml indented"
run "${TU[@]}" patch "$E" --addr 0x00200000 --float 12.5; has "$OUT" "pnach.py override" "data text"
eq "$OUT" "0x00200000  data in SLUS_999.99
  value       9.80000019 -> 12.5 (0x41480000)
  read by     lwc1 \$f1, -32768(\$gp) at 0x00100008

Test in PCSX2 first (add to the game's .pnach, see /console-mod pnach.py crc):
  patch=0,EE,00200000,word,41480000

Then the port: it is data, so a game override writes it at load. Put the pnach line in a .pnach and
run /console-mod: pnach.py override <file.pnach> --elf SLUS_999.99, or by hand in an override:
  Ps2FastWrite32(rdram, 0x00200000u, 0x41480000u); // 12.5f" "data text, exactly"
# test_bad_patch_inputs
for args in "--addr 0x00100034 --float 1" "--addr 0x00100002 --float 1" "--addr zz --float 1" "--addr 0x70000000 --float 1" "--addr 0x00200000 --float nan"; do
    # shellcheck disable=SC2086
    run "${TU[@]}" patch "$E" $args; eq "$CODE" 2 "bad patch: $args"; hasnt "$ERR" "Traceback" "clean: $args"
done
run "${TU[@]}" patch "$E" --addr zz --float 1; eq "$ERR" "tune: error: --addr is not a hex address: 'zz'" "hex message"
run "${TU[@]}" patch "$E" --addr 0x00100002 --float 1; eq "$ERR" "tune: error: --addr 0x00100002 is not 4-byte aligned" "align message"
run "${TU[@]}" patch "$E" --addr 0x70000000 --float 1; eq "$ERR" "tune: error: 0x70000000 is not main RAM" "RAM message"
run "${TU[@]}" patch "$E" --addr 0x00200000 --float inf; eq "$ERR" "tune: error: --float must be a finite float32 value" "inf float"
run "${TU[@]}" patch "$E" --addr " 0X0020_0000 " --float 1 --json; eq "$(j .addr)" '"0x00200000"' "int(x, 16): spaces, 0X, underscores"
run "${TU[@]}" patch "$E" --addr 0x00200000 --float 1 --place 3; eq "$CODE" 2 "place choices"; has "$ERR" "argument --place: invalid choice: 3 (choose from 0, 1, 2)" "choices message"
run "${TU[@]}" patch missing --addr zz --float 1; eq "$ERR" "tune: error: ELF not found: missing" "ELF read before --addr"

# ---- TestCli
run "${TU[@]}" --help; eq "$CODE" 0 "--help"
for c in scan fps patch; do has "$OUT" "$c" "help lists $c"; done
has "$OUT" "Exit codes: 0 ok, 1 nothing found or an approximation was used, 2 bad input." "epilog"
for c in scan fps patch; do run "${TU[@]}" "$c" --help; eq "$CODE" 0 "$c --help"; done
run "${TU[@]}" scan --help; has "$OUT" "absolute tolerance (default 0.1% of the value)" "%% in help"
run "${TU[@]}" patch --help; has "$OUT" "--place {0,1,2}" "choices metavar"
run "${TU[@]}"; eq "$CODE" 2 "no command"; has "$OUT" "usage:" "no command prints help"
run "${TU[@]}" scan "$E" --value 9.8; eq "$CODE" 0 "end to end scan"
run "${TU[@]}" patch "$E" --addr 0x0010000C --float 4.9; eq "$CODE" 0 "end to end patch"
done_ "console-physics/tune"

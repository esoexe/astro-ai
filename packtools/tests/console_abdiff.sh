#!/usr/bin/env bash
# console-tester/abdiff: the TestAbdiffRegs and TestAbdiffMem cases of console-skill/tests/test_tester.py, plus exact
# text output, 128-bit values (big and negative JSON ints, word lists, float bits), duplicate short names, text dumps
# with Unicode digits, int(x, 0) bases, tails merged into ranges, and every error message.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
AB=(console-tester/abdiff)
Z32=00000000000000000000000000000000

# ---- test_json_nested_aliases_and_width
echo '{"pc": "0x00100100", "gpr": {"r2": "0x0000000000000000_0000000000000005", "a0": "0x1000", "sp": 33554176, "ra": [1049088, 0, 0, 0]}, "fpr": {"f0": 1.5}}' > a.json
echo '{"pc": "0x100100", "v0": "0xFFFFFFFF00000005", "$a0": "0x1004", "sp": "01ffff00", "ra": "0x100200", "f0": "0x3fc00000", "k0": "0x1"}' > b.json
run "${AB[@]}" regs a.json b.json --json
eq "$CODE" 1 "differences exit 1"
eq "$(echo "$OUT" | jq -c '[.compared, .width, [.differences[].name], .first.name, .only_in_a, .only_in_b]')" '[6,128,["v0","a0"],"v0",[],["k0"]]' "f0 1.5 == 0x3fc00000, ra words match"
eq "$(echo "$OUT" | jq -c '.differences[0]')" '{"name":"v0","a":"0x00000000000000000000000000000005","b":"0x0000000000000000ffffffff00000005","xor":"0x0000000000000000ffffffff00000000"}' "128-bit difference"
run "${AB[@]}" regs a.json b.json
eq "$OUT" "registers: 6 compared at 128 bits (A = a.json, B = b.json)
  first divergence: v0  A=0x00000000000000000000000000000005  B=0x0000000000000000ffffffff00000005
  v0     A=0x00000000000000000000000000000005  B=0x0000000000000000ffffffff00000005  xor=0x0000000000000000ffffffff00000000
  a0     A=0x00000000000000000000000000001000  B=0x00000000000000000000000000001004  xor=0x00000000000000000000000000000004
  only in B: k0" "regs text"
run "${AB[@]}" regs a.json b.json --width 32 --json
eq "$(echo "$OUT" | jq -c '[[.differences[].name], .differences[0].a, .width]')" '[["a0"],"0x00001000",32]' "width 32"
run "${AB[@]}" regs a.json b.json --width 64 --json
eq "$(echo "$OUT" | jq -r '.differences[0].b')" "0xffffffff00000005" "width 64: 16 digits"
run "${AB[@]}" regs a.json b.json --width 32 --ignore a0
eq "$CODE" 0 "ignored: exit 0"
eq "$OUT" "registers: 5 compared at 32 bits (A = a.json, B = b.json)
  identical
  only in B: k0" "identical text"
run "${AB[@]}" regs a.json b.json --only sp,ra
eq "$CODE" 0 "--only sp,ra exit 0"
has "$OUT" "registers: 2 compared" "--only compares two"
run "${AB[@]}" regs a.json b.json --only sp,ra --ignore k0,ra --json
eq "$(echo "$OUT" | jq -c '[.compared, .only_in_b]')" '[1,[]]' "--ignore also drops only-in names"

# ---- test_text_dump_and_pc_hint
echo "pc=0x00123456 ra=0x00100300 sp=0x01ffff00 v0=0x5 hits=7" > a.txt
echo "[guest-branch:missing-target] kind=IndirectJump op=dispatch source=0x0 target=0x123456 pc=0x123460 ra=0x100300 sp=0x1ffff00 v0=0x5 a0[0]=0x9 codeRegion=yes" > b.log
run "${AB[@]}" regs a.txt b.log --only pc,ra,sp,v0
eq "$CODE" 1 "pc differs exit 1"
eq "$OUT" "registers: 4 compared at 128 bits (A = a.txt, B = b.log)
  first divergence: pc  A=0x00000000000000000000000000123456  B=0x00000000000000000000000000123460
  pc differs: the dumps are not from the same point. Break at the same address on both sides first.
  pc     A=0x00000000000000000000000000123456  B=0x00000000000000000000000000123460  xor=0x00000000000000000000000000000036
  only in A: hits
  only in B: source, target" "text dumps: a0[0] skipped, kind=IndirectJump skipped"
echo "target=0x123456 source=0x0" > t.txt
run "${AB[@]}" regs b.log t.txt --json
eq "$(echo "$OUT" | jq -c '[.compared, .differences, .only_in_a]')" '[2,[],["pc","v0","sp","ra"]]' "target parsed as 0x123456; only-in sorted pc, GPRs"

# ---- test_list_form_and_errors
echo '[{"name": "v0", "value": "0x1"}, {"name": "r29", "value": 16}]' > la.json
echo '{"v0": 1, "sp": "0x10"}' > lb.json
run "${AB[@]}" regs la.json lb.json
eq "$CODE" 0 "list form identical"
printf '{oops' > bad.json
run "${AB[@]}" regs bad.json lb.json
eq "$CODE" 2 "bad json exit 2"
eq "$ERR" "error: bad.json looks like JSON but is not valid (line 1: Expecting property name enclosed in double quotes)" "bad json message"
printf '{"a":\n\n  }' > bad3.json
run "${AB[@]}" regs bad3.json lb.json
eq "$ERR" "error: bad3.json looks like JSON but is not valid (line 3: Expecting value)" "line number of the JSON error"
echo "nothing here" > empty.txt
run "${AB[@]}" regs empty.txt lb.json
eq "$ERR" "error: empty.txt has no registers (JSON name -> value, or name=0x... text)" "no registers"
printf '[1, 2' > brk.txt
run "${AB[@]}" regs brk.txt lb.json
eq "$ERR" "error: brk.txt has no registers (JSON name -> value, or name=0x... text)" "broken [ is read as text"
echo '{"v0": "0xZZ"}' > badval.json
run "${AB[@]}" regs badval.json lb.json
eq "$ERR" "error: badval.json: v0: '0xZZ' is not a hex value" "bad value"
echo '{"g": {"v0": true}}' > bool.json
run "${AB[@]}" regs bool.json lb.json
eq "$ERR" "error: bool.json: g.v0: true/false is not a register value" "bool value"
echo '{"v0": null}' > null.json
run "${AB[@]}" regs null.json lb.json
eq "$ERR" "error: null.json: v0: unsupported value None" "null value"
echo '{"v0": [1, "x"]}' > lst.json
run "${AB[@]}" regs lst.json lb.json
eq "$ERR" "error: lst.json: v0: 'x' is not a hex value" "bad word in a list"
echo '{"v0": 1e39}' > big.json
run "${AB[@]}" regs big.json lb.json
eq "$CODE" 1 "a float past float32 is a runtime error"
run "${AB[@]}" regs la.json lb.json --width 16
eq "$ERR" "error: --width must be 32, 64 or 128" "bad width"
run "${AB[@]}" regs la.json lb.json --only t9
eq "$ERR" "error: no register appears in both dumps (after --only/--ignore)" "nothing in common after --only"
echo '{"k0": 1}' > k.json
run "${AB[@]}" regs la.json k.json
eq "$ERR" "error: no register appears in both dumps" "nothing in common"
run "${AB[@]}" regs la.json none.json
eq "$ERR" "error: no such file: none.json" "missing file"
mkdir -p adir
run "${AB[@]}" regs adir lb.json
eq "$ERR" "error: cannot read adir: [Errno 21] Is a directory: 'adir'" "folder"

# ---- test_canonical_names and values
echo '{"$R31": "0x1", "gpr[4]": "0x2", "s8": "0x3", "r0": "0x0", "PC": "0x5", "r32": "0x6", " $$T9 ": "0x7", "GPR٣": 8}' > c1.json
echo '{"ra": 1, "a0": 2, "fp": 3, "zero": 0, "pc": 5, "r32": 6, "t9": 7, "v1": 8}' > c2.json
run "${AB[@]}" regs c1.json c2.json --json
eq "$(echo "$OUT" | jq -c '[.compared, .differences, .only_in_a, .only_in_b]')" '[8,[],[],[]]' "canonical names (R31, gpr[4], s8, r0, PC, \$\$, Unicode digits)"
echo '{"ee": {"pc": "0x1", "v0": 1}, "iop": {"pc": "0x2"}}' > d1.json
echo '{"ee": {"pc": "0x1"}, "iop": {"pc": "0x3"}, "v0": 1}' > d2.json
run "${AB[@]}" regs d1.json d2.json --json
eq "$(echo "$OUT" | jq -c '[.compared, [.differences[].name]]')" '[3,["iop.pc"]]' "repeated short names keep their group"
echo '{"v0": -1, "v1": 340282366920938463463374607431768211457, "a0": [1, 2], "a1": -0.0, "a2": [4294967297], "a3": -340282366920938463463374607431768211455, "t0": [1, 2, 3, 4, 5]}' > n1.json
echo '{"v0": "0xffffffffffffffffffffffffffffffff", "v1": 1, "a0": "0x200000001", "a1": "0x80000000", "a2": 1, "a3": 1, "t0": "0x00000004000000030000000200000001"}' > n2.json
run "${AB[@]}" regs n1.json n2.json --json
eq "$(echo "$OUT" | jq -c '[.compared, .differences]')" '[7,[]]' "negative and big ints wrap to 128 bits, words, float bits, words past 128 bits dropped"
echo '{"v0": [{"a": "0x1"}, {"b": "0x2"}], "x": {"y": [{"name": 5, "value": "0x3"}]}}' > f1.json
echo '{"a": 1, "b": 2, "x.5": 3}' > f2.json
run "${AB[@]}" regs f1.json f2.json --json
eq "$(echo "$OUT" | jq -c '[.compared, .only_in_a, .only_in_b]')" '[2,["5"],["x.5"]]' "lists of dicts flattened, name/value items keep the short name"
printf 'v0=\xd9\xa1\xd9\xa2 v1=12345678 $a0: 0x1_0 v0=0x99 PC=7 r5 = 0X5\n' > u1.txt
echo '{"v0": 12, "v1": "0x12345678", "a0": 16, "pc": 7, "a1": 0}' > u2.json
run "${AB[@]}" regs u1.txt u2.json --json
eq "$(echo "$OUT" | jq -c '[.compared, .differences, .only_in_b]')" '[4,[],["a1"]]' "text: Unicode digits, 8 digits are hex, \$name: value, the first wins, r5 = 0X5 is no pair (no word boundary after 0)"

# ---- memory: test_identical
head -c 1000 /dev/zero > z1.bin; cp z1.bin z2.bin
run "${AB[@]}" mem z1.bin z2.bin --base 0x100000
eq "$CODE" 0 "identical exit 0"
eq "$OUT" "memory: A z1.bin (1000 bytes), B z2.bin (1000 bytes), base 0x00100000
  identical" "identical text"
: > e1.bin; : > e2.bin
run "${AB[@]}" mem e1.bin e2.bin --json
eq "$(echo "$OUT" | jq -c '[.size_a, .first, .ranges, .truncated, .base]')" '[0,null,[],false,"0x00000000"]' "two empty dumps"

# ---- test_first_divergence_and_ranges
perl -e 'my $s = "\0" x 4096; substr($s, 0x105, 1) = "\xAA"; substr($s, 0x10A, 1) = "\xBB"; substr($s, 0x800, 4) = "\x01\x02\x03\x04"; print $s' > m2.bin
head -c 4096 /dev/zero > m1.bin
run "${AB[@]}" mem m1.bin m2.bin --base 0x00100000 --json
eq "$CODE" 1 "differs exit 1"
eq "$(echo "$OUT" | jq -c '.first')" '{"address":"0x00100105","offset":261,"a_byte":"0x00","b_byte":"0xaa","a_word":"0x00000000","b_word":"0x0000aa00","word_address":"0x00100104","context_address":"0x001000f5","a_context":"'$Z32$Z32'","b_context":"00000000000000000000000000000000aa00000000bb00000000000000000000"}' "first divergence detail"
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .range_count, .ranges, .truncated, .size_a, .size_b, .a, .b]')" '[6,2,[{"start":"0x00100105","end":"0x0010010b","length":6},{"start":"0x00100800","end":"0x00100804","length":4}],false,4096,4096,"m1.bin","m2.bin"]' "ranges merged within the gap"
run "${AB[@]}" mem m1.bin m2.bin --gap 0 --json
eq "$(echo "$OUT" | jq '.range_count')" 3 "--gap 0"
run "${AB[@]}" mem m1.bin m2.bin --gap 0 --max-ranges 1
eq "$OUT" "memory: A m1.bin (4096 bytes), B m2.bin (4096 bytes), base 0x00000000
  first divergence at 0x00000105: A=0x00 B=0xaa (word at 0x00000104: A=0x00000000 B=0x0000aa00)
  A 0x000000f5  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  B 0x000000f5  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 aa 00 00 00 00 bb 00 00 00 00 00 00 00 00 00 00
  6 differing byte(s) in 3 range(s) (first 1 shown):
    0x00000105 - 0x00000106  1 bytes" "mem text, truncated"
run "${AB[@]}" mem m1.bin m2.bin --base 0x100000
has "$OUT" "first divergence at 0x00100105" "base applied"
has "$OUT" "    0x00100800 - 0x00100804  4 bytes" "range line"

# ---- test_ignore_ranges
perl -e 'my $s = "\0" x 256; substr($s, 0x10, 1) = "\x01"; substr($s, 0x80, 1) = "\x02"; print $s' > i2.bin
head -c 256 /dev/zero > i1.bin
run "${AB[@]}" mem i1.bin i2.bin --base 0x1000 --ignore-range 0x1010-0x1011 --json
eq "$(echo "$OUT" | jq -c '[.first.address, .differing_bytes, .range_count]')" '["0x00001080",1,1]' "ignored range skipped"
run "${AB[@]}" mem i1.bin i2.bin --base 0x1000 --ignore-range 0x1000-0x1100
eq "$CODE" 0 "all ignored exit 0"
has "$OUT" "  identical (outside the ignored ranges)" "identical outside the ignored ranges"
run "${AB[@]}" mem i1.bin i2.bin --ignore-range " 16 : 17 " --ignore-range 0x80-0x81
eq "$CODE" 0 "decimal and colon ranges"
run "${AB[@]}" mem i1.bin i2.bin --ignore-range 0x20-0x10
eq "$ERR" "error: --ignore-range '0x20-0x10' ends before it starts" "backwards range"
run "${AB[@]}" mem i1.bin i2.bin --ignore-range lots
eq "$ERR" "error: --ignore-range 'lots' should look like 0x00200000-0x00200100" "bad range"
run "${AB[@]}" mem i1.bin i2.bin --ignore-range 0100-0x200
eq "$CODE" 1 "a leading-zero decimal is a ValueError, as in the original"

# ---- test_size_mismatch_is_fast_and_reported
head -c 3145728 /dev/zero > big.bin
S0=$(date +%s%N)
run "${AB[@]}" mem big.bin e1.bin --json
eq "$(( ($(date +%s%N) - S0) / 1000000 < 3000 ))" 1 "fast"
eq "$CODE" 1 "size mismatch exit 1"
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .range_count, .ranges, .first.address, .first.a_byte, .first.b_byte, .first.a_word, .first.b_word, .first.b_context]')" '[3145728,1,[{"start":"0x00000000","end":"0x00300000","length":3145728}],"0x00000000","0x00",null,"0x00000000",null,""]' "the tail is one range"
run "${AB[@]}" mem big.bin e1.bin
has "$OUT" "  sizes differ: the extra tail counts as different. Dump the same range on both sides." "sizes differ"
has "$OUT" "  first divergence at 0x00000000: A=0x00 B=None (word at 0x00000000: A=0x00000000 B=None)" "None for the missing side"
has "$OUT" "  B 0x00000000  
" "empty context line"
printf '\0\0\1\0' > t2.bin; head -c 8 /dev/zero > t1.bin
run "${AB[@]}" mem t1.bin t2.bin --json
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .ranges]')" '[5,[{"start":"0x00000002","end":"0x00000008","length":6}]]' "a tail within the gap joins the range"
run "${AB[@]}" mem t1.bin t2.bin --ignore-range 0x100-0x200 --json
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .ranges, .first.offset]')" '[5,[{"start":"0x00000002","end":"0x00000008","length":6}],2]' "with --ignore-range the tail is marked byte by byte"
run "${AB[@]}" mem t1.bin t2.bin --ignore-range 2-6 --json
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .ranges, .first.offset]')" '[2,[{"start":"0x00000006","end":"0x00000008","length":2}],6]' "ignored tail bytes"
perl -e 'print "\0" x 1048580' > c1.bin; perl -e 'print "\0" x 1048570, "\1"' > c2.bin
run "${AB[@]}" mem c1.bin c2.bin --json
eq "$(echo "$OUT" | jq -c '[.differing_bytes, .ranges]')" '[10,[{"start":"0x000ffffa","end":"0x00100004","length":10}]]' "across the 1 MiB chunk"

# ---- test_bad_input
printf '\0' > o1.bin; printf '\0' > o2.bin
run "${AB[@]}" mem o1.bin o2.bin --base banana
eq "$ERR" "error: --base 'banana' is not a number (use hex like 0x00100000)" "bad base"
run "${AB[@]}" mem o1.bin o2.bin --base 010
eq "$CODE" 2 "010 is not an int(x, 0)"
run "${AB[@]}" mem o1.bin o2.bin --base=-1
eq "$ERR" "error: --base must not be negative" "negative base"
run "${AB[@]}" mem o1.bin o2.bin --base 0o20 --json
eq "$(echo "$OUT" | jq -r .base)" "0x00000010" "0o prefix"
run "${AB[@]}" mem o1.bin o2.bin --base " 1_0 " --json
eq "$(echo "$OUT" | jq -r .base)" "0x0000000a" "underscores and spaces"
run "${AB[@]}" mem o1.bin o2.bin --gap=-1
eq "$ERR" "error: --gap must be 0 or more and --max-ranges at least 1" "bad gap"
run "${AB[@]}" mem o1.bin o2.bin --max-ranges 0
eq "$ERR" "error: --gap must be 0 or more and --max-ranges at least 1" "bad max ranges"
run "${AB[@]}" mem o1.bin none.bin
eq "$ERR" "error: no such file: none.bin" "missing dump"
run "${AB[@]}" mem o1.bin adir
eq "$ERR" "error: adir is a folder, not a memory dump" "folder dump"
run "${AB[@]}"
eq "$CODE" 2 "no command exit 2"
has "$OUT" "usage: abdiff [-h] command ..." "help"
run "${AB[@]}" mem --help
has "$OUT" "--ignore-range START-END" "metavar"

done_ console-tester/abdiff

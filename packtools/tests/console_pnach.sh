#!/usr/bin/env bash
# console-mod/pnach: the TestParse, TestElfAndCrc, TestClassify, TestCommands and TestCli cases of
# console-skill/tests/test_mod.py through the CLI (parse/classify --json stand in for the internal calls), plus exact
# messages, the MIPS describer, wide values, Unicode places and keys, and the override module compiled with g++
# against mock PS2Recomp headers.
source "$(dirname "$0")/lib.sh"
LIBSKILL="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-console/skills/console-mod"
MOCK="$FIX/ps2mock"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
node "$FIX/mkmodelf.js" standard SLUS_999.99
node "$FIX/mkmodelf.js" nosections NOSEC.ELF
node "$FIX/mkmodelf.js" rwx RWX.ELF
PN=(--skill-dir "$LIBSKILL" console-mod/pnach)
read -r PCRC CRC < <(node -e 'const b=require("fs").readFileSync(process.argv[1]);let x=0;for(let i=0;i+4<=b.length;i+=4)x=(x^b.readUInt32LE(i))>>>0;
const h=v=>v.toString(16).toUpperCase().padStart(8,"0");console.log(h(x),h(require("zlib").crc32(b)))' SLUS_999.99)
P="SLUS-99999_$PCRC.pnach"; cp "$FIX/synthetic.pnach" "$P"
j()  { echo "$OUT" | jq -c "$1"; }
pp() { printf "$1" > t.pnach; run "${PN[@]}" parse t.pnach --json; }                  # parse_pnach_text
cl() { printf "$1" > t.pnach; shift; run "${PN[@]}" classify t.pnach --elf SLUS_999.99 --json "$@"; }
compiles() {
    command -v g++ >/dev/null || { ok; return; }
    if g++ -std=c++20 -fsyntax-only -Wall -Wextra -Werror -I "$MOCK" -x c++ "$1" 2>/tmp/pn_cxx; then ok; else bad "$2: $(head -5 /tmp/pn_cxx)"; fi
}

# ---- TestParse
run "${PN[@]}" parse "$P" --json
eq "$(j .sections)" '["Data","Boot clears","Unportable"]' "sections"
eq "$(j '.meta.gametitle[0].value')" '"Synthetic Test Game [SLUS-99999] (U)"' "gametitle"
eq "$(j '.meta.comment[0].value')" '"Hand-written test patches // not stripped in metadata"' "meta keeps //"
eq "$(j '[.meta.gsaspectratio[0].value, .meta.gsinterlacemode[0].value, .meta.description[0].section]')" '["16:9","1","Data"]' "meta values"
eq "$(j '.meta | keys_unsorted')" '["gametitle","comment","author","gsaspectratio","gsinterlacemode","description"]' "meta order"
eq "$(j .errors)" '[]' "no bad lines"
eq "$(j '[.writes[] | [.line, .bytes]] | map(select(.[0] == 7 or .[0] == 8 or .[0] == 9 or .[0] == 13 or .[0] == 16 or .[0] == 17 or .[0] == 18))')" \
   '[[7,"00000000"],[8,"00"],[9,"0000"],[13,"0F270000"],[16,"AABBCCDD"],[17,"0807060504030201"],[18,"DEADBEEF01"]]' "every type to bytes"
eq "$(j '[.writes[] | select(.line == 15) | .address][0]')" '"0x20200008"' "mirror address kept by parse"
eq "$(j '[.writes[] | select(.line == 13 or .line == 14) | .place]')" '[0,1]' "places"
eq "$(j '.writes[0]')" '{"line":7,"section":"","place":1,"type":"word","cpu":"EE","address":"0x00100018","bytes":"00000000","source":"patch=1,EE,00100018,word,00000000"}' "write JSON keys"
eq "$(j '.manual')" '[{"line":27,"section":"Unportable","reason":"IOP patch: the port runs IRX modules in ps2xIOP; port by hand","source":"patch=1,IOP,00012340,word,00000000"}]' "IOP manual"
eq "$(j '.file')" "\"$P\"" "file name"
pp 'patch=1,EE,00200000,beshort,1234\npatch=1,EE,00200008,bedouble,0102030405060708\n'
eq "$(j '[.writes[].bytes]')" '["1234","0102030405060708"]' "be variants"
pp 'PATCH = 3 , ee , 0x00200000 , WORD , 0x1\n'
eq "$(j '[.writes[0].place, .writes[0].bytes, .writes[0].type, .writes[0].cpu]')" '[3,"01000000","word","EE"]' "spaces, case, place 3"
pp 'patch=1,EE,00200000,byte,1FF\npatch=1,EE,00200004,word,123456789ABCDEF0123\npatch=1,EE,000000000000200008,word,0000000000000000005\n'
eq "$(j '[.writes[] | [.bytes, .notes]]')" '[["FF",["value 0x1FF is wider than a byte; PCSX2 keeps the low 1 bytes"]],["2301EFCD",["value 0x123456789ABCDEF0123 is wider than a word; PCSX2 keeps the low 4 bytes"]],["05000000",null]]' "wide values keep every digit in the note"
eq "$(j '.writes[2].address')" '"0x00200008"' "leading zeros are not wide"
pp 'patch=1,EE,00200000,word\npatch=9,EE,0,word,0\npatch=1,GS,0,word,0\npatch=1,EE,zz,word,0\npatch=1,EE,0,quad,0\npatch=1,EE,0,bytes,ABC\njunk line\npatch=1,EE,00200000,word,1\npatch=1,EE,100000000,word,0\npatch=1,EE,0,word,0xg\npatch=x,EE,0,word,0\npatch=1,EE,0x,word,0\n'
eq "$(j '[.errors[] | [.line, .error]]')" '[[1,"patch= needs 5 fields: place,cpu,address,type,value"],[2,"place must be 0, 1, 2 or 3"],[3,"cpu must be EE or IOP"],[4,"address is not a hex number: '"'zz'"'"],[5,"unknown type '"'quad'"'"],[6,"bytes value must be an even-length hex string"],[7,"not key=value"],[9,"address wider than 32 bits"],[10,"value is not a hex number: '"'0xg'"'"],[11,"place must be 0, 1, 2 or 3"],[12,"address is not a hex number: '"'0x'"'"]]' "bad lines reported, not fatal"
eq "$(j '.writes | length')" 1 "one good write"
eq "$(j '.errors[6].text')" '"junk line"' "bad line text"
eq "$CODE" 1 "bad lines: exit 1"
pp 'patch=+1,EE,00200000,word,1\npatch=\xef\xbc\x90,EE,00200004,word,1\npatch=1_0,EE,00200008,word,1\npatch=-0,EE,0020000C,word,1\n'
eq "$(j '[[.writes[].place], [.errors[].line]]')" '[[1,0,0],[3]]' "int() rules for place: sign, Unicode digits, underscores"
pp 'patch=1,IOP,00012340,word,0\ndpatch=0,1,1,0,0,word,0,0\n'
eq "$(j '[.writes, [.manual[].reason]]')" '[[],["IOP patch: the port runs IRX modules in ps2xIOP; port by hand","dpatch (pattern-matched dynamic patch): port by hand once you know the address"]]' "IOP and dpatch are manual"
pp 'patch=1,EE,20200010,extended,0000ABCD\npatch=1,EE,10200014,extended,00001234\npatch=1,EE,00200016,extended,000000FF\n'
eq "$(j '[.writes[] | [.address, .bytes, .extended]]')" '[["0x00200010","CDAB0000","32-bit write (code 20200010 0000ABCD)"],["0x00200014","3412","16-bit write (code 10200014 00001234)"],["0x00200016","FF","8-bit write (code 00200016 000000FF)"]]' "extended plain writes"
pp 'patch=1,EE,40200020,extended,00040001\npatch=1,EE,00000005,extended,00000001\npatch=1,EE,50200000,extended,00000010\npatch=1,EE,00300000,extended,00000000\npatch=1,EE,30400000,extended,00200000\npatch=1,EE,00000001,extended,00000000\npatch=1,EE,20200040,extended,00000001\n'
eq "$(j '[.writes[].address]')" '["0x00200040"]' "two-line codes consume their second line"
eq "$(j '[.manual[].reason]')" '["extended serial (repeated) write: needs a manual port","second line of the serial (repeated) write code on line 1","extended copy bytes: needs a manual port","second line of the copy bytes code on line 3","extended increment/decrement: needs a manual port","second line of the increment/decrement code on line 5"]' "two-line reasons"
pp 'patch=1,EE,60200030,extended,000000FF\npatch=1,EE,00020003,extended,00000010\npatch=1,EE,00000020,extended,00000030\npatch=1,EE,20200044,extended,00000002\n'
eq "$(j '[[.writes[].address], [.manual[].reason]]')" '[["0x00200044"],["extended pointer write: needs a manual port","second line of the pointer write code on line 1","second line of the pointer write code on line 1"]]' "pointer code with 3 levels"
pp 'patch=1,EE,30000001,extended,00200000\npatch=1,EE,70200000,extended,00100001\npatch=1,EE,20200048,extended,00000003\n'
eq "$(j '[[.writes[].address], (.manual | length)]')" '[["0x00200048"],2]' "single-line inc/dec and bitwise"
pp '[C]\npatch=1,EE,C0200050,extended,00000001\npatch=1,EE,20200054,extended,00000002\npatch=1,EE,20200058,extended,00000002\n[D]\npatch=1,EE,D0200060,extended,00000001\npatch=1,EE,20200064,extended,00000002\npatch=1,EE,20200068,extended,00000003\n[E]\npatch=1,EE,E0020000,extended,00200070\npatch=1,EE,20200074,extended,00000001\npatch=1,EE,20200078,extended,00000001\npatch=1,EE,2020007C,extended,00000001\n'
eq "$(j '[.writes[].address] | sort')" '["0x00200068","0x0020007C"]' "conditionals gate following writes"
eq "$(j '[.manual[] | select(.reason | contains("only runs when"))] | length')" 5 "five gated"
eq "$(j '[.manual[] | select(.line == 3) | .reason][0]')" '"32-bit write that only runs when the conditional on line 2 is true"' "gate message"
eq "$(j '[.manual[] | select(.line == 2) | .reason][0]')" '"extended 32-bit conditional (gates every following code): needs a manual port"' "conditional itself"
pp 'patch=1,EE,C0200050,extended,1\npatch=1,EE,40200020,extended,00040001\n'
eq "$(j '.manual[1].reason')" '"extended serial (repeated) write: needs a manual port (and it is gated by the conditional on line 1)"' "gated manual code"
pp 'patch=1,EE,F0100000,extended,00000000\npatch=1,EE,80100000,extended,0\n'
eq "$(j '[.manual[].reason]')" '["extended unsupported CodeBreaker command F: needs a manual port","extended unsupported CodeBreaker command 8: needs a manual port"]' "unknown extended command"
pp '[]\npatch=1,EE,00200000,word,1\n[ A ]\npatch=1,EE,00200000,word,1\n[A]\n\xc3\x9cn\xc3\xafcode = x\n'
eq "$(j '[.sections, [.writes[].section], .meta]')" '[["","A"],["","A"],{"ünïcode":[{"section":"A","value":"x"}]}]' "empty section name, stripped names, Unicode keys"
eq "$(echo "$OUT" | grep -c 'u00fcn\\u00efcode')" 1 "JSON is ASCII-escaped"
run "${PN[@]}" parse "$P" --json --section Data
eq "$CODE" 0 "section filter: exit 0"; eq "$(j '[(.writes | length), ([.writes[].section] | unique)]')" '[6,["Data"]]' "section filter"
run "${PN[@]}" parse "$P" --section Nope --section Data --section ""
eq "$CODE" 2 "missing section"; eq "$ERR" "pnach: error: section(s) not in $P: Nope (have: Data, Boot clears, Unportable)" "missing section message"
printf '\177ELF\0\0\0binary' > x.pnach; run "${PN[@]}" parse x.pnach
eq "$ERR" "pnach: error: x.pnach looks binary, not a .pnach text file" "binary refused"
run "${PN[@]}" parse nope.pnach; eq "$ERR" "pnach: error: pnach not found: nope.pnach" "missing pnach"

# ---- parse text
printf 'gametitle=G\n[S]\npatch=3,EE,00200000,word,0xabc\npatch=1,EE,00200000,byte,1FF\nbad\npatch=1,IOP,0,word,0\n' > t.pnach
run "${PN[@]}" parse t.pnach
eq "$OUT" "t.pnach: 2 write(s), 1 needing a manual port, 1 bad line(s)
  gametitle       G
  sections        [S]

[S]
  line 3    on load or when enabled    0x00200000 word     0XABC
  line 4    continuously (every vsync) 0x00200000 byte     1FF
            note: value 0x1FF is wider than a byte; PCSX2 keeps the low 1 bytes

Needs a manual port:
  line 6    IOP patch: the port runs IRX modules in ps2xIOP; port by hand

Bad lines:
  line 5    not key=value: bad" "parse text"
run "${PN[@]}" parse "$P"
eq "$CODE" 1 "parse: IOP needs attention"; has "$OUT" "
(no section)
  line 7    continuously (every vsync) 0x00100018 word     00000000" "no-section group"
has "$OUT" "  description     data writes   [Data]" "meta section tag"

# ---- TestElfAndCrc
run "${PN[@]}" crc SLUS_999.99 --json
eq "$OUT" "{
  \"elf\": \"SLUS_999.99\",
  \"size\": 676,
  \"serial\": \"SLUS-99999\",
  \"crc32\": \"$CRC\",
  \"pcsx2_crc\": \"$PCRC\",
  \"entry\": \"0x00100000\",
  \"pnach\": \"SLUS-99999_$PCRC.pnach\",
  \"pnach_legacy\": \"$PCRC.pnach\"
}" "crc JSON"
[ "$PCRC" != "$CRC" ] && ok || bad "the two CRCs differ"
run "${PN[@]}" crc SLUS_999.99
eq "$OUT" "ELF          SLUS_999.99  (676 bytes, entry 0x00100000)
Serial       SLUS-99999
CRC-32       $CRC  standard CRC-32 of the whole file: the PS2Recomp game override key
PCSX2 CRC    $PCRC  XOR of every little-endian 32-bit word: the pnach key
Pnach file   SLUS-99999_$PCRC.pnach  (PCSX2 2.x: the patches/ or cheats/ folder)
Old name     $PCRC.pnach  (PCSX2 1.6 and older)" "crc text"
cp SLUS_999.99 a.elf; { cat SLUS_999.99; printf '\001\002\003'; } > b.elf
run "${PN[@]}" crc a.elf --json; A=$OUT; run "${PN[@]}" crc b.elf --json
eq "$(echo "$OUT" | jq -r .pcsx2_crc)" "$(echo "$A" | jq -r .pcsx2_crc)" "PCSX2 CRC ignores trailing bytes"
[ "$(echo "$OUT" | jq -r .crc32)" != "$(echo "$A" | jq -r .crc32)" ] && ok || bad "CRC-32 sees them"
cp SLUS_999.99 game.elf
run "${PN[@]}" crc game.elf --serial SLES-12345 --json; eq "$(j .pnach)" "\"SLES-12345_$PCRC.pnach\"" "--serial"
run "${PN[@]}" crc game.elf --json; eq "$(j '[.serial, .pnach]')" '[null,null]' "unknown serial: nulls"
run "${PN[@]}" crc game.elf; has "$OUT" "Serial       unknown (name the ELF like SLUS_203.12 or pass --serial SLUS-20312)" "unknown serial text"
hasnt "$OUT" "Pnach file" "no pnach line without a serial"
for nm in "SLUS_203.12:SLUS-20312" "SCES_123.45;1:SCES-12345" "slus-20312.elf:SLUS-20312" "SLUS_20312:SLUS-20312" "main.elf:null" "SLUS_2031:null"; do
    cp SLUS_999.99 "${nm%%:*}"; run "${PN[@]}" crc "${nm%%:*}" --json
    want="${nm##*:}"; [ "$want" != null ] && want="\"$want\""
    eq "$(j .serial)" "$want" "serial_from_name ${nm%%:*}"
done
printf '\177ELF' > short; printf 'NOPE%060d' 0 | tr 0 '\0' > magic
{ printf '\177ELF\002\001'; head -c 60 /dev/zero; } > class64; { printf '\177ELF\001\002'; head -c 60 /dev/zero; } > bigendian
cp SLUS_999.99 x86; perl -e 'open F,"+<","x86"; seek F,18,0; print F pack("v",3); close F'
head -c 120 SLUS_999.99 > truncated; head -c 80 SLUS_999.99 > phcut
cp SLUS_999.99 phsize; perl -e 'open F,"+<","phsize"; seek F,42,0; print F pack("v",16); close F'
cp SLUS_999.99 noload; perl -e 'open F,"+<","noload"; seek F,44,0; print F pack("v",0); close F'
cp SLUS_999.99 fz; perl -e 'open F,"+<","fz"; seek F,104,0; print F pack("V",16); close F'
for c in "short:short is too short to be an ELF (4 bytes)" "magic:magic is not an ELF file (bad magic)" \
         "class64:class64 is not a 32-bit ELF (PS2 game ELFs are ELF32)" "bigendian:bigendian is not little-endian (PS2 EE ELFs are little-endian)" \
         "x86:x86 is not a MIPS ELF (machine 3, the PS2 EE is 8)" "truncated:truncated is truncated: segment 0 runs past the end of the file" \
         "phcut:phcut is truncated: program headers run past the end of the file" "phsize:phsize has a bad program header size (16)" \
         "noload:noload has no loadable (PT_LOAD) segments" "fz:fz segment 1 has filesz > memsz"; do
    run "${PN[@]}" crc "${c%%:*}"; eq "$CODE" 2 "bad ELF ${c%%:*}"; eq "$ERR" "pnach: error: ${c#*:}" "message ${c%%:*}"
done
run "${PN[@]}" crc "$TMP/missing"; eq "$ERR" "pnach: error: ELF not found: $TMP/missing" "missing ELF"

# ---- TestClassify
run "${PN[@]}" classify "$P" --elf SLUS_999.99 --json
eq "$(j '[.writes[] | [.line, .class]] | map(select(.[0] != 13 or true))')" '[[7,"CODE"],[8,"CODE"],[9,"CODE"],[13,"DATA"],[14,"DATA"],[15,"DATA"],[16,"DATA"],[17,"DATA"],[18,"DATA"],[21,"BSS"],[22,"BSS"],[23,"HEAP"]]' "every class"
eq "$(j '[.manual[] | [.line, .reason]]')" '[[26,"OUTSIDE: scratchpad (16 KB at 0x70000000) at 0x70000010: no RDRAM write can reach it, port by hand"],[27,"IOP patch: the port runs IRX modules in ps2xIOP; port by hand"]]' "manual: scratchpad, IOP"
eq "$(j '[.code_patches[] | [.address, .original, .new, .original_asm, .new_asm, .lines, .confidence]]')" '[["0x00100004","0x3442CCCD","0x34420000","ori $v0, $v0, 0xCCCD","ori $v0, $v0, 0x0000",[9],"high"],["0x00100018","0x8C820010","0x00000000","lw $v0, 0x10($a0)","nop",[7],"high"],["0x0010001C","0x10400003","0x10000003","beq $v0, $zero, 0x0010002C","beq $zero, $zero, 0x0010002C",[8],"high"]]' "byte and short merge into the original word"
eq "$(j '[.writes[] | [.line, .override]]')" '[[7,[]],[8,[]],[9,[]],[13,["load"]],[14,["load","every"]],[15,["load","every"]],[16,["load","every"]],[17,["load"]],[18,["load"]],[21,["every"]],[22,["first"]],[23,["every"]]]' "buckets follow place"
eq "$(j '[.writes[0].why, .writes[3].why, .writes[9].why, .writes[11].why]')" '["section .text is executable","section .data is data","zero-filled part of segment 1","RAM outside every ELF segment (heap, stack or loaded files)"]' "why texts"
eq "$(j '[.pnach, .elf, .pcsx2_crc, .crc_match, .counts]')" "[\"$P\",\"SLUS_999.99\",\"$PCRC\",true,{\"CODE\":3,\"DATA\":6,\"BSS\":2,\"HEAP\":1}]" "classify header"
eq "$(j 'keys_unsorted')" '["pnach","elf","pcsx2_crc","crc_match","writes","code_patches","manual","counts"]' "classify key order"
cl 'patch=1,EE,00100018,byte,11\npatch=1,EE,00100019,byte,22\npatch=1,EE,00100018,byte,33\n'
eq "$(j '[(.code_patches | length), .code_patches[0].new, .code_patches[0].lines]')" '[1,"0x8C822233",[1,2,3]]' "two writes to one word: last wins"
cl 'patch=1,EE,00100018,double,0000000000000000\n'
eq "$(j '[.code_patches[].address]')" '["0x00100018","0x0010001C"]' "double spans two words"
cl 'patch=1,EE,000FFFFE,word,AABBCCDD\npatch=1,EE,002000FE,word,11223344\n'
eq "$(j '[.writes[] | [.class, .ram, .bytes]]')" '[["HEAP","0x000FFFFE","DDCC"],["CODE","0x00100000","BBAA"],["DATA","0x002000FE","4433"],["BSS","0x00200100","2211"]]' "straddling writes are split"
cl 'patch=1,EE,80200000,word,1\npatch=1,EE,A0100018,word,0\n'
eq "$(j '[.writes[0].class, .writes[0].ram, .code_patches[0].address]')" '["DATA","0x00200000","0x00100018"]' "KSEG addresses normalise"
cl 'patch=1,EE,1000F000,word,1\npatch=1,EE,12000000,word,1\npatch=1,EE,91000000,word,1\npatch=1,EE,BFC00000,word,1\npatch=1,EE,40000000,word,1\npatch=1,EE,01FFFFFE,word,1\n'
eq "$(j '[.writes, [.manual[].reason]]')" '[[],["OUTSIDE: EE hardware registers at 0x1000F000: no RDRAM write can reach it, port by hand","OUTSIDE: GS privileged registers at 0x12000000: no RDRAM write can reach it, port by hand","OUTSIDE: VU0/VU1 memory at 0x91000000: no RDRAM write can reach it, port by hand","OUTSIDE: BIOS ROM at 0xBFC00000: no RDRAM write can reach it, port by hand","OUTSIDE: not main RAM at 0x40000000: no RDRAM write can reach it, port by hand","OUTSIDE: not main RAM at 0x01FFFFFE: no RDRAM write can reach it, port by hand"]]' "outside reasons"
cl 'patch=1,EE,00100014,word,00000000\npatch=1,EE,0010002C,word,00000000\n'
eq "$(j '[.code_patches[].notes[0]]')" '["the original is a SYSCALL instruction: ps2_recomp only applies this patch when patch_syscalls = true in [general]","the original is a COP0 instruction: ps2_recomp only applies this patch when patch_cop0 = true in [general]"]' "syscall and cop0 gates"
cl 'patch=1,EE,00100018,word,8C820010\n'
eq "$(j '.code_patches[0].notes')" '["no change: the ELF already holds this value"]' "no change noted"
cl '[Cave]\npatch=1,EE,01F00000,word,24020001\npatch=1,EE,01F00004,word,03E00008\npatch=1,EE,01F00008,word,00000000\npatch=1,EE,00100020,word,0C7C0000\n[Fine]\npatch=1,EE,00100018,word,00000000\n'
eq "$(j '[[.manual[] | select(.reason | startswith("CAVE")) | .line], [.code_patches[].address]]')" '[[2,3,4,5],["0x00100018"]]' "jal into new code on the heap"
eq "$(j '[.manual[0].reason, .manual[3].reason]')" '["CAVE: new code written at 0x01F00000; rewrite its logic as an override function (override.py new --replace or --wrap)","CAVE: jal to 0x01F00000 lands in memory this pnach writes; rewrite its logic as an override function (override.py new --replace or --wrap)"]' "cave reasons"
eq "$(j '.counts')" '{"CAVE":4,"CODE":1}' "cave counts"
cl 'patch=1,EE,00200020,word,24020001\npatch=1,EE,00200024,word,03E00008\n'
eq "$(j '[.writes[].class] | unique')" '["CAVE"]' "function body written into data"
cl 'patch=1,EE,00200021,bytes,AABBCC0800E003DD\n'
eq "$(j '[.writes[].class] | unique')" '["CAVE"]' "jr \$ra found at an aligned offset of an unaligned block"
cl 'patch=1,EE,00200020,word,03E00008\n'
eq "$(j '[.writes[].class]')" '["DATA"]' "blocks under 8 bytes are not caves"
cl 'patch=1,EE,00100020,word,08080000\n'
eq "$(j '[.code_patches, .manual[0].reason]')" '[[],"CAVE: j to 0x00200000 lands outside the game'"'"'s code; rewrite its logic as an override function (override.py new --replace or --wrap)"]' "jump into data is a cave"
cl 'patch=1,EE,00100020,word,0C040000\n'
eq "$(j '[(.code_patches | length), .manual]')" '[1,[]]' "jump to existing code is fine"
cl 'patch=1,EE,00100020,word,10000100\n'
eq "$(j '.manual[0].reason')" '"CAVE: beq to 0x00100424 lands outside the game'"'"'s code; rewrite its logic as an override function (override.py new --replace or --wrap)"' "branch out of the code"
cl 'patch=1,EE,00100020,word,08800000\n'
eq "$(j '[(.code_patches | length), .manual]')" '[1,[]]' "jump outside RAM is not checked"
cl 'patch=1,EE,00100018,word,0\npatch=1,EE,00100100,word,5\n' 
cl2() { printf "$1" > t.pnach; shift; run "${PN[@]}" classify t.pnach --elf RWX.ELF --json "$@"; }
cl2 'patch=1,EE,00100018,word,0\npatch=1,EE,00100100,word,5\n'
eq "$(j '[.writes[] | [.class, .confidence, .override]]')" '[["CODE","low",["load","every"]],["CODE","low",["load","every"]]]' "RWX without a map: low confidence"
eq "$(j '.writes[0].why')" '"segment 0 is read-write-execute (code and data mixed, no sections)"' "RWX why"
printf 'Name,Start,End,Size\nsub_00100000,0x00100000,0x00100030,48\n' > map.csv
cl2 'patch=1,EE,00100018,word,0\npatch=1,EE,00100100,word,5\n' --map map.csv
eq "$(j '[.writes[] | [.class, .confidence, .why]]')" '[["CODE","high","inside a function in the Ghidra map"],["DATA","medium","no function in the Ghidra map covers it"]]' "the map refines an RWX segment"
printf 'patch=1,EE,00100018,word,0\npatch=1,EE,00100100,word,5\n' > t.pnach
run "${PN[@]}" toml t.pnach --elf RWX.ELF
has "$OUT" "# NOTE: segment 0 is read-write-execute (code and data mixed, no sections); the override also writes it to RDRAM in case it is data" "toml low-confidence note"
run "${PN[@]}" classify t.pnach --elf RWX.ELF
has "$OUT" "  line 1    CODE  0x00100018 0x00000000         -> TOML instruction patch (low confidence: segment 0 is read-write-execute (code and data mixed, no sections))" "classify low confidence text"
printf 'Name,Start,End,Size\nsub_00100000,0x00100000,0x00100010,16\n' > map2.csv
cl 'patch=1,EE,00100018,word,0\npatch=1,EE,00100004,word,0\n' --map map2.csv
eq "$(j '[[.code_patches[].address], .manual[0].reason]')" '[["0x00100004"],"CAVE: code written at 0x00100018, where no function in the Ghidra map is (ps2_recomp never decodes it); rewrite its logic as an override function (override.py new --replace or --wrap)"]' "map flags code outside functions"
cl 'patch=1,EE,00100004,word,0C040008\n' --map map2.csv
eq "$(j '.manual[0].reason')" '"CAVE: jal to 0x00100020 lands where no function in the Ghidra map is; rewrite its logic as an override function (override.py new --replace or --wrap)"' "jal to no function"
cl 'patch=1,EE,00012340,word,1\npatch=1,EE,80001000,word,1\n'
eq "$(j '[.writes, [.manual[].reason]]')" '[[],["OUTSIDE: EE kernel memory at 0x00012340 (below 0x00080000): the port'"'"'s kernel is high-level emulation, so this write does not do what it did on a PS2","OUTSIDE: EE kernel memory at 0x80001000 (below 0x00080000): the port'"'"'s kernel is high-level emulation, so this write does not do what it did on a PS2"]]' "kernel memory is outside"
printf 'Name,Start,End,Size\nfoo,zz,yy,1\n' > bad.csv; cl 'patch=1,EE,00100018,word,0\n' --map bad.csv
eq "$ERR" "pnach: error: Ghidra map line 2 has a bad address: 'foo,zz,yy,1'" "bad map"
printf '\n  foo,1\n' > bad2.csv; cl 'patch=1,EE,00100018,word,0\n' --map bad2.csv
eq "$ERR" "pnach: error: Ghidra map line 2 is not Name,Start,End,Size: 'foo,1'" "short map line"
cl 'patch=1,EE,00100018,word,0\n' --map nomap.csv; eq "$ERR" "pnach: error: Ghidra map not found: nomap.csv" "missing map"
printf 'patch=1,EE,00100018,word,0\npatch=1,EE,00200000,word,1\npatch=1,EE,00200800,word,1\n' > t.pnach
run "${PN[@]}" classify t.pnach --elf NOSEC.ELF --json
eq "$(j '[.writes[] | [.class, .confidence, .why]]')" '[["CODE","medium","segment 0 is executable"],["DATA","high","file-backed data in segment 1"],["BSS","high","zero-filled part of segment 1"]]' "section-less ELF uses segment flags"

# ---- the describer (classify's asm columns)
cl 'patch=1,EE,00100000,bytes,0800E003\npatch=1,EE,00100004,word,00801025\npatch=1,EE,00100008,word,2442FFFF\npatch=1,EE,0010000C,word,8FBF0010\npatch=1,EE,00100010,word,AFA4FFF0\npatch=1,EE,00100014,word,C7A00004\npatch=1,EE,00100018,word,00041000\npatch=1,EE,0010001C,word,0000000D\npatch=1,EE,00100020,word,70000000\npatch=1,EE,00100024,word,FC000000\npatch=1,EE,00100028,word,48000000\npatch=1,EE,0010002C,word,EC000000\npatch=1,EE,00100030,word,00000001\npatch=1,EE,00100034,word,44021000\npatch=1,EE,00100038,word,46000000\npatch=1,EE,0010003C,word,00A0302D\n'
eq "$(j '[.code_patches[].new_asm]')" '["jr $ra","move $v0, $a0","addiu $v0, $v0, 0xFFFF","lw $ra, 0x10($sp)","sw $a0, -0x10($sp)","lwc1 $f0, 0x4($sp)","sll","break","mmi","sd $zero, 0x0($zero)","cop2","op3B","special","mfc1 $v0, $f2","cop1","move $a2, $a1"]' "describe"
eq "$(j '[.code_patches[].original_asm]')" '["lui $v0, 0x411C","ori $v0, $v0, 0xCCCD","mtc1 $v0, $f12","jr $ra","nop","syscall","lw $v0, 0x10($a0)","beq $v0, $zero, 0x0010002C","nop","jr $ra","nop","cop0","jal 0x00100018","nop","j 0x00100024","nop"]' "describe the original text"
cl 'patch=1,EE,00100020,word,1443FFFE\npatch=1,EE,00100024,word,04010002\n'
eq "$(j '[.code_patches[].new_asm]')" '["bne $v0, $v1, 0x0010001C","regimm"]' "branches with targets"

# ---- TestCommands
run "${PN[@]}" classify "$P" --elf SLUS_999.99
eq "$CODE" 1 "classify: exit 1"
eq "$(echo "$OUT" | head -3)" "$P on SLUS_999.99 (PCSX2 CRC $PCRC, matches the file name)
  CODE 3, DATA 6, BSS 2, HEAP 1, manual 2" "classify header text"
has "$OUT" "  line 15   DATA  0x00200008 0x12345678         -> override: at load + every frame" "classify row"
has "$OUT" "  line 18   DATA  0x00200018 DEADBEEF01         -> override: at load" "bytes row"
has "$OUT" "  0x0010001C  0x10400003 beq \$v0, \$zero, 0x0010002C -> 0x10000003 beq \$zero, \$zero, 0x0010002C" "code word row"
has "$OUT" "
Needs a manual port:
  line 26   OUTSIDE: scratchpad" "manual list"
run "${PN[@]}" classify "$P" --elf SLUS_999.99 --json --section Data
eq "$CODE" 0 "Data only: exit 0"; eq "$(j '[.crc_match, .counts]')" '[true,{"DATA":6}]' "Data only counts"
cp "$FIX/synthetic.pnach" SLUS-99999_00000000.pnach
run "${PN[@]}" classify SLUS-99999_00000000.pnach --elf SLUS_999.99
eq "$CODE" 2 "CRC mismatch refused"
eq "$ERR" "pnach: error: SLUS-99999_00000000.pnach is for the build with PCSX2 CRC 00000000 but SLUS_999.99 has $PCRC: the addresses will not line up (use --ignore-crc only if you are sure)" "mismatch message"
run "${PN[@]}" classify SLUS-99999_00000000.pnach --elf SLUS_999.99 --ignore-crc
eq "$CODE" 1 "ignored"; has "$OUT" "(PCSX2 CRC $PCRC, DOES NOT match the file name)" "DOES NOT match"
run "${PN[@]}" classify SLUS-99999_00000000.pnach --elf SLUS_999.99 --ignore-crc --json; eq "$(j .crc_match)" false "crc_match false"
cp "$FIX/synthetic.pnach" 1a2b3c4d.PNACH; run "${PN[@]}" classify 1a2b3c4d.PNACH --elf SLUS_999.99
has "$ERR" "with PCSX2 CRC 1A2B3C4D but" "legacy name, any case"
cp "$FIX/synthetic.pnach" cheats.pnach; run "${PN[@]}" classify cheats.pnach --elf SLUS_999.99 --json
eq "$(j .crc_match)" null "no CRC in the name"
run "${PN[@]}" classify cheats.pnach --elf SLUS_999.99; eq "$(echo "$OUT" | head -1)" "cheats.pnach on SLUS_999.99 (PCSX2 CRC $PCRC)" "no CRC note"

run "${PN[@]}" toml "$P" --elf SLUS_999.99
eq "$CODE" 1 "toml exit"
eq "$OUT" "# Console skill: instruction patches from $P for SLUS_999.99
# Paste at the end of the game's ps2_recomp TOML (as top-level [[patches.instructions]] blocks),
# then re-run ps2_recomp and rebuild. ps2_recomp only patches words inside functions it recompiles.

# line 9: patch=0,EE,00100004,short,0000
# original 0x3442CCCD (ori \$v0, \$v0, 0xCCCD) -> new 0x34420000 (ori \$v0, \$v0, 0x0000)
[[patches.instructions]]
address = \"0x00100004\"
value = \"0x34420000\"

# line 7: patch=1,EE,00100018,word,00000000
# original 0x8C820010 (lw \$v0, 0x10(\$a0)) -> new 0x00000000 (nop)
[[patches.instructions]]
address = \"0x00100018\"
value = \"0x00000000\"

# line 8: patch=1,EE,0010001E,byte,00
# original 0x10400003 (beq \$v0, \$zero, 0x0010002C) -> new 0x10000003 (beq \$zero, \$zero, 0x0010002C)
[[patches.instructions]]
address = \"0x0010001C\"
value = \"0x10000003\"

# 9 data/BSS/heap write(s) are not instruction patches: use  pnach.py override  for them.
# 2 line(s) need a manual port: see  pnach.py classify." "toml text"
printf '[One]\npatch=1,EE,00100014,word,00000000\n[Two]\npatch=1,EE,00100016,short,0000\npatch=1,EE,0010002C,word,0\n' > g.pnach
run "${PN[@]}" toml g.pnach --elf SLUS_999.99 --out o/patches.toml
eq "$CODE" 0 "toml --out"; eq "$ERR" "wrote o/patches.toml" "wrote on stderr"; eq "$OUT" "" "nothing on stdout"
TXT=$(cat o/patches.toml)
has "$TXT" "# [One], [Two] line 2: patch=1,EE,00100014,word,00000000
# [One], [Two] line 4: patch=1,EE,00100016,short,0000" "sections and lines of a merged word"
has "$TXT" "# Some originals are gated: set patch_cop0 and patch_syscalls = true in [general] or those patches are skipped." "gate summary"
run "${PN[@]}" toml g.pnach --elf SLUS_999.99 --out o/patches.toml
eq "$CODE" 2 "--force needed"; eq "$ERR" "pnach: error: o/patches.toml exists: pass --force to overwrite it (it may hold your own edits)" "toml exists message"
run "${PN[@]}" toml g.pnach --elf SLUS_999.99 --out o/patches.hpp; eq "$ERR" "pnach: error: refusing to write a header: header edits force a full rebuild" "header refused"
printf 'patch=1,EE,00200000,word,1\n' > d.pnach; run "${PN[@]}" toml d.pnach --elf SLUS_999.99
eq "$CODE" 0 "toml without code"; has "$OUT" "
# No code patches: nothing in this pnach lands in the game's code.

# 1 data/BSS/heap write(s)" "No code patches"; hasnt "$OUT" $'\n[[patches.instructions]]\n' "no blocks"

OV="$TMP/console/patches/inf.cpp"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --frame-hook 0x00100018 --out "$OV" --name "Infinite things"
eq "$CODE" 1 "override: manual lines -> exit 1"
eq "$OUT" "wrote $OV  (keys: SLUS_999.99, entry 0x00100000, CRC-32 0x$CRC)
  at load 6, first frame 1, every frame 5
  frame hook: 0x00100018 is reached by 1 jal and 0 j instruction(s) in the ELF.
  3 code patch(es) are not in this file: run  pnach.py toml  and add them to the game's TOML
  2 line(s) need a manual port (listed at the top of the file)
  install: wrote $TMP/console/patches/console_mods.cmake; add once at the end of ps2xRuntime/CMakeLists.txt: include(\"$TMP/console/patches/console_mods.cmake\" OPTIONAL)
  (the include adds every .cpp in that folder to the ps2EntryRunner executable itself: in the static
  ps2_runtime library the linker would drop the registration)" "override stdout"
TXT=$(cat "$OV")
eq "$(cat console/patches/console_mods.cmake)" "$(cat "$LIBSKILL/templates/console_mods.cmake.txt")" "cmake template copied"
has "$TXT" "PS2_REGISTER_GAME_OVERRIDE(\"Infinite things\", \"SLUS_999.99\", 0x00100000u, 0x${CRC}u, &applyOverride)" "keys"
has "$TXT" "namespace console_mod_infinite_things" "namespace"
for s in "Ps2FastWrite32(rdram, 0x00200000u, 0x0000270Fu);" "Ps2FastWrite32(rdram, 0x00200008u, 0x12345678u);" "Ps2FastWrite32(rdram, 0x0020000Cu, 0xDDCCBBAAu);" \
         "Ps2FastWrite64(rdram, 0x00200010u, 0x0102030405060708ULL);" "kBytes[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01}" "constexpr uint32_t kFrameHookAddress = 0x00100018u;" \
         "runtime.lookupFunction(kFrameHookAddress)" "runtime.replaceFunction(kFrameHookAddress, &frameHook);" "if (current != &frameHook)" \
         "// [Data] line 15 (continuously (every vsync)): patch=1,EE,20200008,word,12345678  // uncached mirror of 0x00200008"; do
    has "$TXT" "$s" "module: $s"
done
hasnt "$TXT" "0x00100004u" "code patches stay in the TOML"
eq "$(grep -cE '@[A-Z][A-Z0-9_]*@' "$OV")" 0 "no unfilled fields"
EVERY=$(sed -n '/^void applyEveryFrameWrites/,/^constexpr/p' "$OV")
has "$EVERY" "0x00200100u" "BSS place 1 every frame"; has "$EVERY" "0x01000000u" "heap place 1 every frame"; has "$EVERY" "0x00200004u" "data place 1 also every frame"
FIRST=$(sed -n '/^void applyFirstFrameWrites/,/^void applyEveryFrameWrites/p' "$OV")
has "$FIRST" "Ps2FastWrite8(rdram, 0x00200200u, 0x01u);" "first frame"
has "$TXT" "// Source: $P (sections: Data, Boot clears, Unportable)
// Generated by console-mod pnach.py override on $(date +%F).
// Writes at load: 6, on the first frame: 1, every frame: 5.
// Code patches are NOT here: 3 instruction patch(es) go in the game's TOML (pnach.py toml).
// Frame hook: the game's function at 0x00100018 is wrapped. Only calls that go through the
// runtime's function table see the wrapper (jal, jalr); a j tail jump to it is a direct C++ call.
// 0x00100018 is reached by 1 jal and 0 j instruction(s) in the ELF.
// Not ported (2, do these by hand):
//   line 26: OUTSIDE: scratchpad (16 KB at 0x70000000) at 0x70000010: no RDRAM write can reach it, port by hand
//   line 27: IOP patch: the port runs IRX modules in ps2xIOP; port by hand
#include" "notes block"
compiles "$OV" "override with a frame hook compiles"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --out "$OV"
eq "$CODE" 2 "override exists"; eq "$ERR" "pnach: error: $OV exists: pass --force to overwrite it (it may hold your own edits)" "override exists message"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --out "$OV" --force --name "mod one"
eq "$CODE" 1 "no hook"; has "$OUT" "  WARNING: 6 write(s) need --frame-hook 0xADDR (a function the game calls once per frame)" "unwired warning"
has "$OUT" "  install: console_mods.cmake already next to it" "cmake already there"
TXT=$(cat "$OV")
has "$TXT" "// WARNING: 6 write(s) need a frame hook and are NOT wired up. Find a function the game
// calls once per frame (Ghidra: the main loop's per-frame call, or whatever waits for vsync)
// and re-run: pnach.py override ... --frame-hook 0xADDR" "unwired note"
has "$TXT" "[[maybe_unused]] void applyEveryFrameWrites" "maybe_unused"
has "$TXT" "// NOT WIRED UP: no --frame-hook was given (see the note at the top)." "not wired comment"
hasnt "$TXT" "frameHook" "no frame hook code"
compiles "$OV" "override without a hook compiles"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --frame-hook 0x00100018 --out "$TMP/two.cpp" --name "mod two"
cat "$OV" > unity.cpp; echo >> unity.cpp; cat "$TMP/two.cpp" >> unity.cpp
compiles unity.cpp "two overrides in one unity file"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --section Data --out "$TMP/x/x.cpp"
eq "$CODE" 1 "Data, no hook: exit 1"; has "$OUT" "need --frame-hook" "warning"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --section Data --frame-hook 0x00100000 --out "$TMP/x/x.cpp" --force
eq "$CODE" 0 "Data with a hook: exit 0"; hasnt "$OUT" "need --frame-hook" "no warning"
has "$OUT" "  frame hook: No direct jal or j: fine if it is called through a pointer (jalr goes through the table); check its references in Ghidra." "no-caller advice"
has "$(cat "$TMP/x/x.cpp")" "// Source: $P (sections: Data, Boot clears, Unportable)" "Source lists every section"
compiles "$TMP/x/x.cpp" "Data module compiles"
printf 'patch=1,EE,00200000,word,1\n' > d.pnach
run "${PN[@]}" override d.pnach --elf SLUS_999.99 --frame-hook 0x00100024 --out "$TMP/h.cpp"
eq "$CODE" 1 "hook reached only by j"
has "$OUT" "  frame hook: 1 j tail jump(s) to it (0x00100038) compile to a direct C++ call that skips the function table: the hook does not run on those paths.
  frame hook: Nothing reaches it with jal: pick the caller one level up (a function called with jal) or check the callers in Ghidra." "j-only advice"
has "$(cat "$TMP/h.cpp")" "0 jal and 1 j" "advice in the notes"
run "${PN[@]}" override d.pnach --elf SLUS_999.99 --frame-hook 0x00100018 --out "$TMP/h.cpp" --force
eq "$CODE" 0 "jal hook: exit 0"; has "$OUT" "1 jal and 0 j" "jal advice"; has "$OUT" "ps2EntryRunner executable" "install note"
run "${PN[@]}" override d.pnach --elf SLUS_999.99 --frame-hook 0x00100018 --map map2.csv --out "$TMP/h.cpp" --force
eq "$ERR" "warning: 0x00100018 is not a function start in the Ghidra map; the hook must be a function's first instruction" "map start warning on stderr"
eq "$CODE" 0 "map warning does not change the exit"
run "${PN[@]}" override d.pnach --elf RWX.ELF --frame-hook 0x00100100 --map map.csv --out "$TMP/h.cpp" --force
eq "$CODE" 2 "hook outside the map's functions"; eq "$ERR" "pnach: error: --frame-hook 0x00100100 is not in the ELF's code" "RWX + map: only functions are code"
run "${PN[@]}" override d.pnach --elf RWX.ELF --frame-hook 0x00100100 --out "$TMP/h.cpp" --force
eq "$CODE" 0 "RWX without a map: the hook is (low-confidence) code"
run "${PN[@]}" override "$P" --elf SLUS_999.99 --out "$TMP/PS2Recomp/ps2xRuntime/src/runner/mod.cpp"
eq "$CODE" 2 "runner refused"; eq "$ERR" "pnach: error: refusing to write into ps2xRuntime/src/runner/ (generated code): use console/patches/" "runner message"
for h in "0x00200000:--frame-hook 0x00200000 is not in the ELF's code" "0x00100002:--frame-hook 0x00100002 is not 4-byte aligned" \
         "zz:--frame-hook is not a hex number: 'zz'" "0x100000000:--frame-hook is out of range: '0x100000000'" "70000000:--frame-hook 0x70000000 is not in the ELF's code" \
         "100018:" ; do
    run "${PN[@]}" override "$P" --elf SLUS_999.99 --frame-hook "${h%%:*}" --out "$TMP/hh.cpp" --force
    if [ -n "${h#*:}" ]; then eq "$CODE" 2 "bad hook ${h%%:*}"; eq "$ERR" "pnach: error: ${h#*:}" "hook message ${h%%:*}"; else eq "$CODE" 1 "bare hex hook"; fi
done
run "${PN[@]}" override "$P" --elf SLUS_999.99 --out "$TMP/m.h"; eq "$ERR" "pnach: error: refusing to write a header: header edits force a full rebuild" "override header refused"
run --skill-dir "$TMP/nowhere" console-mod/pnach override "$P" --elf SLUS_999.99 --out "$TMP/n.cpp"
eq "$ERR" "pnach: error: templates/override.cpp is missing (reinstall the console-mod skill)" "template missing"; [ -e n.cpp ] && bad "nothing written" || ok

# ---- end to end in a working folder (TestCli)
mkdir w && cd w || exit 1
cp "../$P" . ; cp ../SLUS_999.99 .
run "${PN[@]}" override "$P" --elf SLUS_999.99 --frame-hook 0x00100018
eq "$CODE" 1 "default path exit"
f="console/patches/$(echo "slus_99999_$PCRC" | tr 'A-Z' 'a-z').cpp"
[ -f "$f" ] && ok || bad "default file $f"
has "$(cat "$f")" "PS2_REGISTER_GAME_OVERRIDE(\"pnach SLUS-99999_$PCRC\"" "default mod name"
eq "$(echo "$OUT" | head -1)" "wrote $f  (keys: SLUS_999.99, entry 0x00100000, CRC-32 0x$CRC)" "relative path printed"
run --skill-dir "$LIBSKILL" console-mod/override new --name "Moon jump" --elf SLUS_999.99 --wrap 0x00100000=moon
eq "$CODE" 0 "override.py in the same folder"; [ -f console/patches/moon_jump.cpp ] && ok || bad "moon_jump.cpp"
run "${PN[@]}" classify nope.pnach --elf SLUS_999.99; eq "$CODE" 2 "missing pnach exit 2"
cd "$TMP" || exit 1

# ---- help
run "${PN[@]}" --help
eq "$CODE" 0 "help exit"
for c in parse classify toml override crc; do
    has "$OUT" "$c" "help lists $c"
    run "${PN[@]}" "$c" --help; eq "$CODE" 0 "$c --help"; has "$OUT" "usage:" "$c usage"
    run "${PN[@]}" --help
done
has "$OUT" "Exit codes: 0 ok, 1 something needs a manual port or a bad line, 2 bad input." "epilog"
run "${PN[@]}" override --help; has "$OUT" "--frame-hook 0xADDR" "frame-hook metavar"; has "$OUT" "--section NAME" "section metavar"
run "${PN[@]}"; eq "$CODE" 2 "no command"; has "$OUT" "usage:" "no command prints help"
run "${PN[@]}" classify "$P"; eq "$CODE" 2 "--elf required"
done_ "console-mod/pnach"

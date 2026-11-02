#!/usr/bin/env bash
# console-reverse/elf: the ElfTests of console-skill/tests/test_reverse.py, plus exact text, JSON order, the string
# scanners' edge cases, TOML and CSV details, run against the C++ tool. Fixtures come from a JavaScript copy of the
# test file's make_elf(); reference CRCs from Node's zlib.crc32 and a JavaScript XOR of the words.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
EL=(console-reverse/elf)
cat > mkelf.js <<'EOF'
// node mkelf.js OUT [json options]: make_elf(entry, machine, etype, sections, symtab, strings, big_endian, elf64, code, extra)
const fs = require("fs"); const zlib = require("zlib");
const [out, optj] = process.argv.slice(2); const o = Object.assign({ entry: 0x00100008, machine: 8, etype: 2, sections: true, symtab: true, strings: "", big_endian: false, elf64: false }, optj ? JSON.parse(optj) : {});
const bin = (s) => Buffer.from(s, "latin1");
let code = o.code !== undefined ? bin(o.code) : Buffer.concat(Array(4).fill(Buffer.from([...Array(256).keys()])));
let data = Buffer.concat([bin("DATA".repeat(16)), bin(o.strings)]); data = Buffer.concat([data, Buffer.alloc((4 - data.length % 4) % 4)]);
const codeOff = 0x100, dataOff = codeOff + code.length;
let body = Buffer.concat([Buffer.alloc(codeOff), code, data]);
const segs = [[codeOff, 0x00100000, code.length, code.length, 5], [dataOff, 0x00100000 + code.length, data.length, data.length + 0x200, 6]];
if (o.segs) for (const [k, v] of Object.entries(o.segs)) segs[k] = v;   // override (offset, vaddr, filesz, memsz, flags)
const I = (...v) => { const b = Buffer.alloc(4 * v.length); v.forEach((x, k) => b.writeUInt32LE(x >>> 0, 4 * k)); return b; };
let shoff = 0, shnum = 0, shstrndx = 0;
if (o.sections) {
  const names = bin("\0.text\0.data\0.bss\0.shstrtab\0.symtab\0"); const shstrOff = body.length; body = Buffer.concat([body, names]);
  const symOff = body.length; if (o.symtab) body = Buffer.concat([body, Buffer.alloc(48)]);
  body = Buffer.concat([body, Buffer.alloc((4 - body.length % 4) % 4)]); shoff = body.length;
  const sh = [I(0,0,0,0,0,0,0,0,0,0), I(1,1,6,0x00100000,codeOff,code.length,0,0,16,0), I(7,1,3,0x00100000+code.length,dataOff,data.length,0,0,16,0),
    I(13,8,3,0x00100000+code.length+data.length,0,0x200,0,0,16,0), I(18,3,0,0,shstrOff,names.length,0,0,1,0)];
  if (o.symtab) sh.push(I(28,2,0,0,symOff,48,4,1,4,16));
  shnum = sh.length; shstrndx = 4; body = Buffer.concat([body, ...sh]);
}
const hdr = Buffer.alloc(52); bin("\x7fELF").copy(hdr, 0); hdr[4] = o.elf64 ? 2 : 1; hdr[5] = o.big_endian ? 2 : 1; hdr[6] = 1;
const H = [o.etype, o.machine], W = [1, o.entry, 52, shoff, 0x20924001], H2 = [52, 32, 2, 40, shnum, shstrndx];
hdr.writeUInt16LE(H[0], 16); hdr.writeUInt16LE(H[1], 18); W.forEach((x, k) => hdr.writeUInt32LE(x >>> 0, 20 + 4 * k)); H2.forEach((x, k) => hdr.writeUInt16LE(x, 40 + 2 * k));
hdr.copy(body, 0); segs.forEach(([off, va, fs_, ms, fl], i) => I(1, off, va, va, fs_, ms, fl, 16).copy(body, 52 + 32 * i));
if (o.cut) body = body.subarray(0, o.cut);
fs.writeFileSync(out, body);
let x = 0; for (let k = 0; k + 4 <= body.length; k += 4) x = (x ^ body.readUInt32LE(k)) >>> 0;
const hex = (v) => v.toString(16).toUpperCase().padStart(8, "0");
console.log(body.length, hex(zlib.crc32(body) >>> 0), hex(x));
EOF
IRX='cdrom0:\\MODULES\\SIO2MAN.IRX;1\u0000rom0:PADMAN\u0000cdrom0:\\DATA\\LEVEL1.BIN;1\u0000'   # JSON-escaped
mk() { node "$TMP/mkelf.js" "$@"; }

# ---- upstream test_info_text + exact text (the layout is worked out in the comments)
read -r LEN CRC XOR < <(mk SLUS_203.12 "{\"strings\": \"$IRX\"}")
eq "$LEN" 1736 "fixture: 0x100 + 1024 code + 132 data + 36 names + 48 symtab + 6*40 headers"
run "${EL[@]}" info SLUS_203.12
eq "$CODE" 0 "info exit 0"
eq "$OUT" "File         SLUS_203.12 (1736 bytes)
Serial       SLUS-20312 (USA)
Type         EXEC (executable), MIPS (8), ELF32 little-endian
Entry        0x00100008
Segments     2 PT_LOAD
  R-X  0x00100000-0x00100400  file 0x400 bytes at 0x100
  RW-  0x00100400-0x00100684  file 0x84 bytes at 0x500  (+0x200 zero-filled bss)
Sections     6: .text .data .bss .shstrtab .symtab
Symbols      2 in .symtab
CRC-32       0x$CRC  (PS2_REGISTER_GAME_OVERRIDE key; standard CRC-32 of the whole file)
PCSX2 CRC    0x$XOR  (pnach file: SLUS-20312_$XOR.pnach)
IRX modules  2 referenced:
  cdrom0:\\MODULES\\SIO2MAN.IRX;1
  rom0:PADMAN
Disc paths   1 other device paths (first 20):
  cdrom0:\\DATA\\LEVEL1.BIN;1" "info text, line for line"

# ---- upstream test_crcs_match_reference + JSON shape
run "${EL[@]}" info SLUS_203.12 --json
eq "$(echo "$OUT" | jq -r '[.crc32, .pnach_crc, .pnach_file] | join(" ")')" "0x$CRC 0x$XOR SLUS-20312_$XOR.pnach" "CRC-32 = zlib.crc32, PCSX2 CRC = XOR of the words"
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["file","name","bytes","type","machine","entry","flags","is_irx","segments","sections","section_count","symbols","stripped","debug_info","crc32","pnach_crc","serial","region","pnach_file","warnings","irx_modules","disc_paths"]' "JSON keys, in order"
eq "$(echo "$OUT" | jq -c '.segments[1]')" '{"vaddr":"0x00100400","end":"0x00100684","filesz":132,"memsz":644,"bss":512,"offset":"0x500","flags":"RW-"}' "segment JSON"
eq "$(echo "$OUT" | jq -c '[.file, .flags, .is_irx, .debug_info, .warnings, .irx_modules, .disc_paths]')" "[\"$TMP/SLUS_203.12\",\"0x20924001\",false,false,[],[\"cdrom0:\\\\MODULES\\\\SIO2MAN.IRX;1\",\"rom0:PADMAN\"],[\"cdrom0:\\\\DATA\\\\LEVEL1.BIN;1\"]]" "file is absolute; references"
has "$OUT" $'{\n  "file": ' "indent 2"

# ---- upstream test_pnach_crc_ignores_trailing_bytes (through a file: the tail after the last whole word is ignored)
read -r _ _ X1 < <(mk t1)
printf '\x01\x02\x03' >> t1
eq "$($T "${EL[@]}" crc t1 --json | jq -r '.pnach_crc')" "0x$X1" "trailing 3 bytes do not change the PCSX2 CRC"

# ---- upstream test_crc_subcommand
run "${EL[@]}" crc SLUS_203.12 --json
eq "$(echo "$OUT" | jq -c 'keys_unsorted, .pnach_file')" $'["crc32","pnach_crc","pnach_file","entry","elf_name"]\n'"\"SLUS-20312_$XOR.pnach\"" "crc --json"
run "${EL[@]}" crc SLUS_203.12
eq "$OUT" "CRC-32     0x$CRC  (runtime override key)
PCSX2 CRC  0x$XOR  (SLUS-20312_$XOR.pnach)
Entry      0x00100008   ELF name SLUS_203.12" "crc text"

# ---- upstream test_stripped_and_no_sections, test_symbols_counted
mk nosym '{"symtab": false}' >/dev/null
run "${EL[@]}" info nosym
has "$OUT" "Symbols      none: stripped. Use the Ghidra export (ExportPS2Functions.java) for function bounds" "stripped line"
has "$OUT" "PCSX2 CRC    0x" "no serial in the name"; has "$OUT" "(pnach file: <SERIAL>_" "pnach name without a serial"
hasnt "$OUT" "Serial " "no Serial line"
mk nosec '{"sections": false}' >/dev/null
run "${EL[@]}" info nosec --json
eq "$(echo "$OUT" | jq -c '[.stripped, .section_count, .sections, .serial, .region]')" '[true,0,[],null,null]' "no section headers"
has "$($T "${EL[@]}" info nosec)" "Sections     none (no section headers)" "no sections line"
run "${EL[@]}" info SLUS_203.12 --json
eq "$(echo "$OUT" | jq -c '[.stripped, .symbols, (.sections | index(".text") != null)]')" '[false,2,true]' "3 symtab entries - the null one = 2"

# ---- upstream test_irx_detected
mk SIO2MAN.IRX '{"etype": 65408}' >/dev/null
run "${EL[@]}" info SIO2MAN.IRX
eq "$CODE" 0 "IRX info exit 0"
has "$OUT" $'Type         SCE IOP relocatable (IRX module), MIPS (8), ELF32 little-endian\n             This is an IOP module (IRX), not the game\'s main program. ps2xIOP runs it.' "IOP module note"
mk odd '{"etype": 4660}' >/dev/null
eq "$($T "${EL[@]}" info odd --json | jq -r '.type')" "0x1234" "unknown type as 0x%04X"

# ---- upstream test_bad_inputs_exit_2 (and the messages)
printf 'hello world%.0s' {1..10} > notelf; printf '\x7fELF\x01\x01' > short
mk arm '{"machine": 40}' >/dev/null; mk be '{"big_endian": true}' >/dev/null; mk elf64 '{"elf64": true}' >/dev/null
for c in "notelf|elf: notelf is not an ELF file (no 0x7F 'ELF' magic)" "short|elf: short is too short to be an ELF (6 bytes)" \
         "arm|elf: arm is not a MIPS program (machine 40, expected 8)" "be|elf: be is big-endian; PS2 programs are little-endian" \
         "elf64|elf: elf64 is a 64-bit ELF; PS2 programs are ELF32"; do
  run "${EL[@]}" info "$TMP/${c%%|*}"
  eq "$CODE:$ERR" "2:${c#*|}" "bad input ${c%%|*}: exit 2, one line, the base name"
done

# ---- upstream test_truncated_segment_exit_2, test_folder_and_missing
mk cut '{"cut": 384}' >/dev/null
run "${EL[@]}" info cut
eq "$CODE:$ERR" "2:elf: cut is truncated: segment 0 needs bytes up to 0x500, the file has 0x180" "truncated segment"
run "${EL[@]}" info "$TMP"
eq "$CODE:$ERR" "2:elf: $TMP is a folder, not an ELF file" "a folder"
run "${EL[@]}" info "$TMP/nope"
eq "$CODE:$ERR" "2:elf: cannot open $TMP/nope: No such file or directory" "missing file"
mk ph '{"cut": 60}' >/dev/null
run "${EL[@]}" info ph
eq "$CODE:$ERR" "2:elf: ph is truncated: program headers run past the end of the file" "program headers cut off"

# ---- warnings: memsz < filesz, past 32 MB of RDRAM (the end can pass 32 bits: Python prints all the digits)
# segment 0: memsz 1000 < filesz 1024; segment 1 at 0xFFFFFF00 + 0x200: (0x1FFFFF00 + 0x200) > 32 MB and the end needs 33 bits
mk warn '{"segs": {"0": [256, 1048576, 1024, 1000, 5], "1": [1280, 4294967040, 64, 512, 6]}}' >/dev/null
run "${EL[@]}" info warn --json
eq "$(echo "$OUT" | jq -c '[.warnings, .segments[0].bss, .segments[1].end, .segments[1].bss]')" '[["segment 0 has memsz < filesz","segment at 0xFFFFFF00 runs past the 32 MB of RDRAM"],0,"0x100000100",448]' "warnings, max(0, bss), a 33-bit end"
has "$($T "${EL[@]}" info warn)" $'  RW-  0xFFFFFF00-0x100000100  file 0x40 bytes at 0x500  (+0x1C0 zero-filled bss)' "33-bit end in the text"
has "$($T "${EL[@]}" info warn)" $'Warning      segment 0 has memsz < filesz\nWarning      segment at 0xFFFFFF00 runs past the 32 MB of RDRAM' "warning lines"

# ---- the string scanners (re.finditer on bytes, IGNORECASE):
#  host:/ then NUL: [\\/]? gives the slash back, so the match is "host:/" | trailing dots are stripped | ROM0: is a module |
#  a bare LIBSD.IRX;2 counts, a bare sio2man.irx inside an earlier path does not | 41 name chars before .IRX: no match
S='host:/\u0000mc0:BASLUS..\u0000ROM0:SIO2MAN\u0000cdrom0:\\MODULES\\SIO2MAN.IRX;1\u0000 LIBSD.IRX;2\u0000 sio2man.irx\u0000'
S+=" $(printf 'A%.0s' {1..41}).IRX\u0000 $(printf 'B%.0s' {1..40}).irx;x\u0000cdrom:\u0000cdrom0:$(printf 'C%.0s' {1..130})\u0000mc0:BASLUS..\u0000"
mk scan "{\"strings\": \"$S\"}" >/dev/null
run "${EL[@]}" info scan --json
eq "$(echo "$OUT" | jq -c '.irx_modules')" "[\"ROM0:SIO2MAN\",\"cdrom0:\\\\MODULES\\\\SIO2MAN.IRX;1\",\"LIBSD.IRX;2\",\"$(printf 'B%.0s' {1..40}).irx\"]" "IRX modules: device paths first, then bare names"
eq "$(echo "$OUT" | jq -c '.disc_paths')" "[\"host:/\",\"mc0:BASLUS\",\"cdrom0:$(printf 'C%.0s' {1..120})\"]" "disc paths: slash given back, dots stripped, 120-char cap, duplicates once"
mk many "{\"strings\": \"$(for i in $(seq 1 25); do printf 'mc0:F%02d\\u0000' "$i"; done)\"}" >/dev/null
run "${EL[@]}" info many
eq "$(echo "$OUT" | grep -c '^  mc0:F')" 20 "text shows the first 20 disc paths"
has "$OUT" "Disc paths   25 other device paths (first 20):" "but counts all 25"

# ---- upstream test_toml_writes_real_keys_only (+ the exact head)
mk SLUS_203.12 >/dev/null
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/console/SLUS-20312.toml"
eq "$CODE" 0 "toml exit 0"
eq "$OUT" "Wrote $TMP/console/SLUS-20312.toml
Generated C++ will go to $TMP/console/output
Next: ps2_recomp $TMP/console/SLUS-20312.toml" "toml text (not stripped: no Ghidra hint)"
TOML=$(cat "$TMP/console/SLUS-20312.toml")
eq "$(grep -oP '^[a-z_]+(?= = )' <<<"$TOML" | sort -u | tr '\n' ' ')" "entry_points input low_memory_mode output output_worker_threads patch_cache patch_syscalls single_file_output skip stubs " "only keys the recompiler reads (upstream's ^[a-z_]+ skips patch_cop0 too)"
has "$TOML" "patch_cop0 = true" "patch_cop0 true"
has "$TOML" "patch_syscalls = false" "patch_syscalls false"; has "$TOML" "[general]" "[general]"
hasnt "$TOML" "code_base" "no code_base"; hasnt "$TOML" "runtime_header" "no runtime_header"
eq "$(head -6 <<<"$TOML" | sed -n '1p;6p')" $'# PS2Recomp starter config for SLUS-20312, written by console-reverse elf.\n# ELF: entry 0x00100008, CRC-32 0x'"$(mk /dev/null | cut -d' ' -f2)"', 2 PT_LOAD segments, 2 symbols.' "header lines"
eq "$(grep -E '^(input|output) = ' <<<"$TOML")" "input = \"$TMP/SLUS_203.12\""$'\n'"output = \"$TMP/console/output\"" "absolute input and output"
eq "$(tail -c 1 "$TMP/console/SLUS-20312.toml" | od -An -c | tr -d ' ')" '\n' "ends with the empty last line"

# ---- upstream test_toml_refuses_overwrite_and_force
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/c.toml"; eq "$CODE" 0 "first write"
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/c.toml"
eq "$CODE:$ERR" "1:elf: $TMP/c.toml already exists; not overwriting your edits (pass --force to replace it)" "refuses to overwrite: exit 1"
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/c.toml" --force; eq "$CODE" 0 "--force replaces it"
mkdir -p d && cd d && mk SLUS_203.12 '{"symtab": false}' >/dev/null
run "${EL[@]}" toml SLUS_203.12
eq "$OUT" "Wrote console/SLUS-20312.toml
Generated C++ will go to $TMP/d/console/output
Stripped ELF: export functions from Ghidra (ExportPS2Functions.java) and pass --ghidra-csv,
or use the TOML the export writes. ps2_analyzer is the quick fallback.
Next: ps2_recomp console/SLUS-20312.toml" "default console/<serial>.toml; stripped hint"
mk game.elf >/dev/null
run "${EL[@]}" toml game.elf; has "$OUT" "Wrote console/game_elf.toml" "no serial: dots become underscores"
cd "$TMP" || exit 1

# ---- upstream test_toml_with_ghidra_csv (CRLF, blank and whitespace-only lines are not rows)
printf 'Name,Start,End,Size\r\nsub_00100230,0x00100230,0x00100290,96\r\n\r\n  \r\nsub_2,0x1,0x2,1' > functions.csv
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/g.toml" --ghidra-csv functions.csv --output-dir "$TMP/gen" --json
eq "$CODE" 0 "toml with CSV exit 0"
eq "$(echo "$OUT" | jq -c '.')" "{\"toml\":\"$TMP/g.toml\",\"output\":\"$TMP/gen\",\"ghidra_rows\":2,\"entry\":\"0x00100008\",\"crc32\":\"0x$(mk /dev/null | cut -d' ' -f2)\"}" "toml --json"
eq "$(grep -A1 '^# Ghidra function map' g.toml)" $'# Ghidra function map (Name,Start,End,Size): 2 functions. Its bounds win over heuristics.\nghidra_output = "'"$TMP"'/functions.csv"' "ghidra_output line"
printf 'name, start ,END,size\n' > spaced.csv
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/s.toml" --ghidra-csv spaced.csv --json
eq "$(echo "$OUT" | jq '.ghidra_rows')" 0 "header check ignores spaces and case"

# ---- upstream test_toml_bad_csv_and_irx
printf 'a,b\n1,2\n' > bad.csv
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/b.toml" --ghidra-csv bad.csv
eq "$CODE:$ERR" "2:elf: bad.csv does not look like an ExportPS2Functions.java map (header 'a,b', expected 'Name,Start,End,Size')" "bad CSV header (repr)"
printf "it's \"x\"\n" > q.csv
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/b.toml" --ghidra-csv q.csv
has "$ERR" "(header 'it\\'s \"x\"', expected" "repr escapes like Python"
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/b.toml" --ghidra-csv "$TMP/none.csv"
eq "$CODE:$ERR" "2:elf: no such Ghidra CSV: $TMP/none.csv" "missing CSV"
mk X.IRX '{"etype": 65408}' >/dev/null
run "${EL[@]}" toml X.IRX --out "$TMP/i.toml"
eq "$CODE:$ERR" "2:elf: X.IRX is an IRX module; recompile the EE boot ELF instead" "IRX refused"
mk rel '{"etype": 1}' >/dev/null
run "${EL[@]}" toml rel --out "$TMP/r.toml"
eq "$CODE:$ERR" "2:elf: rel is not an executable ELF (type REL (relocatable))" "REL refused"
[ ! -e "$TMP/b.toml" ] && [ ! -e "$TMP/i.toml" ] && ok || bad "nothing written on bad input"

# ---- upstream test_toml_escapes_windows_style_paths (toml_str through --output-dir)
run "${EL[@]}" toml SLUS_203.12 --out "$TMP/w.toml" --output-dir 'C:\Games\"x"'
has "$(cat w.toml)" '\\Games\\\"x\"' "backslashes and quotes escaped"

# ---- upstream test_serial_both_ways
run "${EL[@]}" serial SLUS_203.12 --json
eq "$(echo "$OUT" | jq -c '.')" '{"serial":"SLUS-20312","elf_name":"SLUS_203.12","prefix":"SLUS","region":"USA"}' "serial --json"
for form in "SLUS-20312" "slus20312" "SLUS 20312" 'cdrom0:\SLUS_203.12;1' "/x/y/SLUS_203.12" "  SLUS_203.12  "; do
  eq "$($T "${EL[@]}" serial "$form" --json | jq -r '.elf_name')" "SLUS_203.12" "serial form [$form]"
done
eq "$($T "${EL[@]}" serial SCES-50123)" $'Serial     SCES-50123\nELF name   SCES_501.23\nRegion     Europe' "serial text"
eq "$($T "${EL[@]}" serial ABCD-12345 --json | jq -r '.region')" "unknown region prefix" "unknown prefix"
eq "$($T "${EL[@]}" serial 'SLPM_٦٥٤.٣٢' --json | jq -c '[.serial, .region]')" '["SLPM-٦٥٤٣٢","Japan"]' "\\d is Unicode, as in Python"
run "${EL[@]}" serial hello
eq "$CODE:$ERR" "2:elf: 'hello' is not a PS2 serial (expected SLUS-20312, SLUS_203.12 or SLUS20312)" "not a serial"

# ---- argparse
run "${EL[@]}"
eq "$CODE" 2 "no command: exit 2"; has "$ERR" "elf: error: the following arguments are required: COMMAND" "COMMAND required"
run "${EL[@]}" --help
has "$OUT" $'usage: elf [-h] COMMAND ...\n\nRead a PS2 boot ELF' "usage"
has "$OUT" "    serial    convert SLUS_203.12 <-> SLUS-20312 and name the region" "sub-command list"

done_ "console-reverse/elf"

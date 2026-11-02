#!/usr/bin/env bash
# console-levels/datascan: the Afs, GenericToc, ScanAndSniff, Diff, Strings, Files and Cli cases of
# console-skill/tests/test_levels.py through the CLI, plus exact-output checks. Archives come from
# fixtures/mkdatascan.js (its own make_afs / make_toc / elf); the zlib sniff is fuzzed against Node's zlib
# (inflate with Z_SYNC_FLUSH = zlib.decompressobj().decompress). Read-chunk edge cases use the test-only
# PACKTOOLS_DATASCAN_CHUNK (upstream patches datascan.CHUNK).
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkdatascan.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
D=(console-levels/datascan)
j()  { echo "$OUT" | jq -c "$1"; }
mk() { node "$MK" "$@"; }
jf() { jq -c "$2" "$1"; }
same() { cmp -s "$1" "$2" && ok || bad "$3: $1 and $2 differ"; }
size() { node -e 'console.log(require("fs").statSync(process.argv[1]).size)' "$1"; }

# ================================================================ Afs
for v in 1 2; do
  mkdir -p v$v; mk afs v$v/LEVEL.AFS $v
  run "${D[@]}" unpack v$v/LEVEL.AFS --out v$v/unpacked; eq "$CODE" 0 "v$v unpack rc"
  eq "$(ls v$v/unpacked/files | tr '\n' ' ')" "0000_STAGE1.DAT 0001_STAGE1.TM2 0002_MUSIC.VAG " "v$v file names"
  IX=v$v/unpacked/index.json
  eq "$(jf $IX .format)" '"afs"' "v$v format"
  eq "$(jf $IX .alignment)" 2048 "v$v alignment"
  eq "$(jf $IX .afs.meta_where)" "$([ $v = 1 ] && echo '"after_toc"' || echo '"before_data"')" "v$v meta_where"
  eq "$(jf $IX .afs.name_rule)" "$([ $v = 1 ] && echo '"offset"' || echo '"header_seq"')" "v$v name_rule"
  eq "$(jf $IX '.entries[1].date')" "[2003,7,15,10,20,30]" "v$v date"
  run "${D[@]}" repack v$v/unpacked --out v$v/NEW.AFS; eq "$CODE" 0 "v$v repack rc"
  has "$OUT" "layout keep" "v$v layout keep"
  same v$v/NEW.AFS v$v/LEVEL.AFS "v$v byte identical"
done
# exact text of unpack / repack (v1: 16384 bytes, meta at 14336, no extras)
run "${D[@]}" unpack v1/LEVEL.AFS --out v1/u2
eq "$OUT" "unpacked 3 entries from v1/LEVEL.AFS (AFS, 2048-byte alignment) into v1/u2/files
edit the files (same names), then: datascan.py repack v1/u2 --out <new archive>" "unpack text"
run "${D[@]}" repack v1/u2 --out v1/N2.AFS
eq "$OUT" "wrote v1/N2.AFS: 16.0 KB (16.0 KB before), layout keep, 0 entries changed
verified: every entry reads back" "repack text"
# the index.json itself: key order, indent 1, the AFS block
IX=v1/u2/index.json
eq "$(jq -c 'keys_unsorted' $IX)" '["tool","version","format","source","source_size","source_sha256","afs","layout","alignment","header_file","pad_byte","extras","entries"]' "index key order"
eq "$(jq -c '.afs' $IX)" '{"meta_where":"after_toc","meta_ptr_at":32,"meta_offset":14336,"meta_length":144,"name_rule":"offset","raw_tail":null}' "afs block"
eq "$(jq -c '.layout' $IX)" '{"kind":"afs","n":3,"table_at":8,"count_at":4,"off_scale":1,"size_scale":1,"sentinel":false}' "afs layout"
eq "$(jq -c '.entries[0] | keys_unsorted' $IX)" '["index","offset","size","name","date","slot","file","sha256"]' "entry key order"
eq "$(jq -c '[.entries[] | [.offset, .size, .slot]]' $IX)" '[[2048,3000,4096],[6144,5004,6144],[12288,104,2048]]' "offsets, sizes, slots"
eq "$(jq -c '[.tool, .source, .source_size, .pad_byte, .extras, .header_file]' $IX)" '["datascan.py","LEVEL.AFS",16384,0,[],"_header.bin"]' "index fields"
eq "$(jq -r .source_sha256 $IX)" "$(sha256sum v1/LEVEL.AFS | cut -d' ' -f1)" "source sha256"
eq "$(jq -r '.entries[2].sha256' $IX)" "$(sha256sum v1/u2/files/0002_MUSIC.VAG | cut -d' ' -f1)" "entry sha256"
eq "$(head -c 9 $IX | od -v -An -c | tr -s ' ' | sed 's/^ //')" '{ \n " t o o l "' "indent 1"
eq "$(size v1/u2/_header.bin)" 2048 "header file size"

# ---- test_bigger_edit_repacks_with_new_offsets
mk afs LEVEL.AFS 2; run "${D[@]}" unpack LEVEL.AFS --out u
mk blob u/files/0000_STAGE1.DAT 9000 9; cp u/files/0000_STAGE1.DAT bigger.bin
run "${D[@]}" repack u --out NEW.AFS; eq "$CODE" 0 "bigger rc"
has "$OUT" "layout pack" "bigger layout pack"; has "$OUT" "offsets changed" "bigger note"
has "$OUT" "1 entry changed, 2 moved to new offsets" "bigger summary"
A=$(mk afsread NEW.AFS)
eq "$(echo "$A" | jq -c .names)" '["STAGE1.DAT","STAGE1.TM2","MUSIC.VAG"]' "bigger names"
eq "$(echo "$A" | jq -c .meta_where)" '"before_data"' "bigger meta_where"
eq "$(echo "$A" | jq -c '[.entries[][0] % 2048] | unique')" "[0]" "bigger offsets aligned"
eq "$(echo "$A" | jq -c '.size % 2048')" 0 "bigger size aligned"
i=0; for want in bigger.bin u/files/0001_STAGE1.TM2 u/files/0002_MUSIC.VAG; do
  o=$(echo "$A" | jq ".entries[$i][0]"); s=$(echo "$A" | jq ".entries[$i][1]")
  mk slice NEW.AFS "$o" "$s" got$i.bin; same got$i.bin "$want" "bigger entry $i"; i=$((i+1))
done

# ---- test_keep_layout_refuses_growth
rm -rf u NEW.AFS; mk afs LEVEL.AFS 1; run "${D[@]}" unpack LEVEL.AFS --out u
mk blob u/files/0001_STAGE1.TM2 7000 4
run "${D[@]}" repack u --out NEW.AFS --layout keep; eq "$CODE" 1 "keep refuses rc"
has "$ERR" "slot" "keep refuses message"
eq "$ERR" "refused: entry 1 is now 7000 bytes but its slot holds 6144; --layout keep cannot move anything (shrink it, or use --layout pack if the game reads offsets from this table)" "keep refusal text"
[ -e NEW.AFS ] && bad "keep refusal wrote NEW.AFS" || ok

# ---- test_smaller_edit_keeps_every_offset
rm -rf u; run "${D[@]}" unpack LEVEL.AFS --out u
printf 'short' > u/files/0000_STAGE1.DAT
run "${D[@]}" repack u --out NEW.AFS; eq "$CODE" 0 "smaller rc"
eq "$(mk afsread NEW.AFS | jq -c '[.entries[][0]]')" "$(mk afsread LEVEL.AFS | jq -c '[.entries[][0]]')" "smaller keeps offsets"
eq "$(mk afsread NEW.AFS | jq -c '.entries[0][1]')" 5 "smaller size 5"
eq "$(size NEW.AFS)" "$(size LEVEL.AFS)" "smaller same length"

# ---- test_pack_layout_on_request
rm -rf u; run "${D[@]}" unpack LEVEL.AFS --out u
run "${D[@]}" repack u --out N.AFS --layout pack --json; eq "$CODE" 0 "pack rc"
eq "$(j .layout)" '"pack"' "pack layout"; eq "$(j .verified)" true "pack verified"
eq "$(j 'keys_unsorted')" '["layout","changed","size","original_size","dropped_extra_bytes","moved","verified","out"]' "repack json keys"
eq "$(j '[.changed, .moved, .dropped_extra_bytes, .size]')" '[[],0,0,16384]' "pack report"
same N.AFS LEVEL.AFS "pack of an unchanged AFS is identical"

# ---- test_unsafe_names_stay_inside_the_folder
mk afs EVIL.AFS 1 evil; run "${D[@]}" unpack EVIL.AFS --out ue; eq "$CODE" 0 "evil rc"
[ -e evil.bin ] && bad "evil.bin escaped" || ok
eq "$(ls ue/files | tr '\n' ' ')" "0000_evil.bin 0001_c " "safe names"
eq "$(jq -c '[.entries[].name]' ue/index.json)" '["../../evil.bin","a/b\\c"]' "raw names kept in the index"

# ---- test_index_with_path_tricks_is_refused
jq '.entries[0].file = "../../outside.bin"' u/index.json > idx.tmp && mv idx.tmp u/index.json
run "${D[@]}" repack u --out N2.AFS; eq "$CODE" 2 "path tricks rc"; has "$ERR" "outside" "path tricks message"
eq "$ERR" "error: index.json names a path outside the folder: '../../outside.bin'" "path tricks text"
jq '.entries[0].file = "/etc/passwd"' u/index.json > idx.tmp && mv idx.tmp u/index.json
run "${D[@]}" repack u --out N2.AFS; eq "$CODE" 2 "absolute path rc"
jq '.entries[0].file = "files/nope.bin"' u/index.json > idx.tmp && mv idx.tmp u/index.json
run "${D[@]}" repack u --out N2.AFS; eq "$CODE" 2 "missing file rc"; eq "$ERR" "error: missing u/files/nope.bin (named in index.json)" "missing file text"
echo '{"tool":"other"}' > u/index.json
run "${D[@]}" repack u --out N2.AFS; eq "$CODE" 2 "foreign index rc"; eq "$ERR" "error: u/index.json was not written by datascan.py unpack" "foreign index text"
echo '{nope' > u/index.json
run "${D[@]}" repack u --out N2.AFS; eq "$CODE" 2 "bad json rc"; eq "$ERR" "error: u/index.json is not valid JSON" "bad json text"

# ---- test_truncated_afs
head -c 5000 LEVEL.AFS > CUT.AFS
run "${D[@]}" unpack CUT.AFS --out uc; eq "$CODE" 2 "truncated rc"; has "$ERR" "outside the file" "truncated message"
eq "$ERR" "error: AFS entry 0 (offset 0x800, size 3000) points outside the file (5000 bytes): truncated?" "truncated text"
run "${D[@]}" unpack LEVEL.AFS --out uf --format afs; eq "$CODE" 0 "--format afs"
mk blob notafs.bin 100 1
run "${D[@]}" unpack notafs.bin --out un --format afs; eq "$CODE" 2 "--format afs on other data"
eq "$ERR" "error: notafs.bin is not an AFS archive (no 'AFS\\0' magic)" "not afs text"

# ---- test_repack_refuses_to_overwrite / non-empty folder / no index
rm -rf u; run "${D[@]}" unpack LEVEL.AFS --out u
run "${D[@]}" repack u --out LEVEL.AFS; eq "$CODE" 2 "overwrite rc"; has "$ERR" "exists" "overwrite message"
run "${D[@]}" repack u --out LEVEL.AFS --force; eq "$CODE" 0 "--force overwrites"
mkdir -p ne; echo mine > ne/keep.txt
run "${D[@]}" unpack LEVEL.AFS --out ne; eq "$CODE" 2 "non-empty rc"
eq "$ERR" "error: ne is not empty (pass --force to write into it)" "non-empty text"
run "${D[@]}" unpack LEVEL.AFS --out ne --force; eq "$CODE" 0 "non-empty --force"
mkdir -p empty; run "${D[@]}" repack empty --out x; eq "$CODE" 2 "no index rc"
eq "$ERR" "error: empty has no index.json (run unpack first)" "no index text"
run "${D[@]}" repack LEVEL.AFS --out x; eq "$CODE" 2 "repack of a file"; eq "$ERR" "error: LEVEL.AFS is not a folder" "not a folder text"
run "${D[@]}" unpack nope.bin --out x; eq "$CODE" 2 "unpack missing"; eq "$ERR" "error: nope.bin does not exist" "missing text"
run "${D[@]}" unpack empty --out x; eq "$CODE" 2 "unpack folder"; eq "$ERR" "error: empty is not a file" "folder text"

# ================================================================ GenericToc
mk toc STAGE.PAK
run "${D[@]}" scan STAGE.PAK --json; eq "$CODE" 0 "toc scan rc"
eq "$(j '.[0].toc')" '{"kind":"count+pairs","header":0,"count_at":0,"table_at":4,"n":4,"off_scale":1,"size_scale":1,"sentinel":false,"table_end":36,"score":0.997,"alignment":16,"coverage":0.989}' "count+pairs candidate"
run "${D[@]}" unpack STAGE.PAK --out t; eq "$CODE" 0 "toc unpack rc"
eq "$OUT" "unpacked 4 entries from STAGE.PAK (count+pairs table at 0x4 (confidence 1.00), 16-byte alignment) into t/files
edit the files (same names), then: datascan.py repack t --out <new archive>" "toc unpack text"
eq "$(jq -c '[.entries[] | [.offset, .size]]' t/index.json)" "[[48,700],[752,1500],[2256,33],[2304,2100]]" "toc entries"
eq "$(jq -c .layout t/index.json)" '{"kind":"count+pairs","n":4,"header":0,"count_at":0,"table_at":4,"off_scale":1,"size_scale":1,"sentinel":false,"table_end":36,"score":0.997}' "toc layout"
eq "$(jq -c '.entries[0] | keys_unsorted' t/index.json)" '["index","offset","size","slot","file","sha256"]' "toc entry keys"
eq "$(ls t/files | tr '\n' ' ')" "0000.bin 0001.bin 0002.bin 0003.bin " "toc file names"
run "${D[@]}" repack t --out NEW.PAK; eq "$CODE" 0 "toc repack rc"; same NEW.PAK STAGE.PAK "toc round trip"

# ---- test_header_magic_before_the_count / test_force_layout_hint
mk toc H.PAK magic=PAK1
run "${D[@]}" scan H.PAK --json
eq "$(j '.[0].toc | [.kind, .header, .table_at]')" '["count+pairs",4,8]' "magic header 4"
run "${D[@]}" unpack H.PAK --out h --header 4 --layout count+pairs; eq "$CODE" 0 "hint rc"
eq "$(jq .layout.header h/index.json)" 4 "hint header"
run "${D[@]}" unpack H.PAK --out h2 --header 0; eq "$CODE" 1 "no table at header 0"
has "$ERR" "best guess scored 0.00" "header 0 refusal"
run "${D[@]}" unpack H.PAK --out h3 --layout pairs; eq "$CODE" 0 "pairs reading of the same table"
eq "$(jq -c '.layout | [.kind, .header, .count_at, .table_at, .n, .table_end]' h3/index.json)" '["pairs",8,null,8,4,48]' "pairs layout skips the count"

# ---- test_sector_offsets
mk toc SECT.BIN off_scale=2048
run "${D[@]}" scan SECT.BIN --json
eq "$(j '.[0].toc | [.off_scale, .size_scale, .alignment]')" "[2048,1,2048]" "sector scales"
run "${D[@]}" unpack SECT.BIN --out s; eq "$CODE" 0 "sector unpack"
eq "$(jq .alignment s/index.json)" 2048 "sector alignment"
run "${D[@]}" repack s --out same.bin; eq "$CODE" 0 "sector repack"; same same.bin SECT.BIN "sector round trip"
mk blob s/files/0000.bin 5000 99; cp s/files/0000.bin grown0.bin
run "${D[@]}" repack s --out grown.bin; eq "$CODE" 0 "sector grow rc"
eq "$(for i in 0 1 2 3; do mk u32 grown.bin $((4 + 8 * i)); done | tr '\n' ' ')" "1 4 5 6 " "sector table in sectors"
for i in 0 1 2 3; do
  off=$(( $(mk u32 grown.bin $((4 + 8 * i))) * 2048 )); sz=$(mk u32 grown.bin $((8 + 8 * i)))
  mk slice grown.bin "$off" "$sz" g$i.bin
  if [ $i = 0 ]; then same g0.bin grown0.bin "sector entry 0"; else mk slice SECT.BIN $(( (i + 1) * 2048 )) "$sz" o$i.bin; same g$i.bin o$i.bin "sector entry $i"; fi
done
eq "$(size grown.bin)" 16384 "sector grown size"

# ---- test_count_offsets_with_end_sentinel
mk toc OFFS.BIN kind=count+offsets sentinel=1
run "${D[@]}" scan OFFS.BIN --json
eq "$(j '.[0].toc | [.kind, .sentinel, .score, .table_end]')" '["count+offsets",true,0.85,24]' "sentinel candidate"
run "${D[@]}" unpack OFFS.BIN --out o; eq "$CODE" 0 "offsets unpack"
run "${D[@]}" repack o --out same2.bin; eq "$CODE" 0 "offsets repack"; same same2.bin OFFS.BIN "offsets round trip"
mk blob o/files/0001.bin 3000 7
run "${D[@]}" repack o --out grown2.bin; eq "$CODE" 0 "offsets grow"
eq "$(for i in 0 1 2 3 4; do mk u32 grown2.bin $((4 + 4 * i)); done | tr '\n' ' ')" "32 736 3744 3792 5904 " "offsets table and sentinel after growth"

# ---- test_bytes_outside_the_table_are_kept
mk toc JUNK.PAK gap_junk=1
run "${D[@]}" unpack JUNK.PAK --out k; eq "$CODE" 0 "junk unpack"; has "$OUT" "_extra_" "junk extra named"
has "$OUT" "kept 1 byte range(s) outside the entries as _extra_*.bin (repack puts them back)" "junk kept line"
eq "$(jq -c .extras k/index.json)" '[{"offset":2252,"length":20,"file":"_extra_000008cc.bin","after_last":false}]' "junk extras"
run "${D[@]}" repack k --out same.pak; eq "$CODE" 0 "junk repack"; same same.pak JUNK.PAK "junk round trip"
mk blob k/files/0000.bin 4000 5
run "${D[@]}" repack k --out grown.pak; eq "$CODE" 1 "junk dropped rc"; has "$OUT" "dropped" "junk dropped"
has "$OUT" "WARNING: 20 bytes found between entries were dropped by the pack layout" "junk dropped text"

# ---- test_random_data_has_no_table / test_zero_filled_file_has_no_table
mk blob noise.bin 20000 7
run "${D[@]}" scan noise.bin --json; eq "$(j '.[0] | has("toc")')" false "noise no toc"
run "${D[@]}" unpack noise.bin --out nz; eq "$CODE" 1 "noise refused"; has "$ERR" "per-game work" "noise message"
head -c 4096 /dev/zero > zero.bin
run "${D[@]}" unpack zero.bin --out zz --force-toc; eq "$CODE" 1 "zero refused even with --force-toc"
eq "$ERR" "refused: no table of contents found in zero.bin (best guess scored 0.00, need 0.60). This format needs per-game work: find the loader in Ghidra, or try --layout/--header with --force-toc." "zero refusal text"

# ================================================================ ScanAndSniff
mk tree disc
run "${D[@]}" scan disc --json; eq "$CODE" 0 "tree scan rc"
K=$(echo "$OUT" | jq -c 'map({(.path): .kind}) | add')
for pk in SYSTEM.CNF=text SLUS_203.12=ELF MODULES/SIO2MAN.IRX=IRX MODULES/OLD.IRX=IRX OTHER.ELF=ELF DATA/LEVEL.AFS=AFS DATA/TITLE.TM2=TIM2 \
          SOUND/SE.VAG=VAG SOUND/SE.HD=IECS MOVIE/OPEN.PSS=MPEG-PS MOVIE/OPEN.IPU=IPU PACK/A.GZ=gzip PACK/B.Z=zlib PACK/C.ZIP=ZIP \
          PACK/D.WAV=RIFF PACK/E.CPK=CPK PACK/NOISE.BIN=data EMPTY.DAT=empty DATA/STAGE.PAK=data PACK/ZERO.BIN=data; do
  eq "$(echo "$K" | jq -r --arg p "${pk%%=*}" '.[$p]')" "${pk#*=}" "kind ${pk%%=*}"
done
R() { echo "$OUT" | jq -c --arg p "$1" ".[] | select(.path == \$p) | $2"; }
eq "$(R SLUS_203.12 .detail)" '"MIPS executable (EE)"' "EE detail"
eq "$(R OTHER.ELF .detail)" '"machine 40, not MIPS"' "not MIPS detail"
eq "$(R MODULES/SIO2MAN.IRX .detail)" '"IOP module (ELF type 0xFF80)"' "IRX type detail"
eq "$(R MODULES/OLD.IRX .detail)" '"IOP module (.iopmod section)"' "IRX section detail"
eq "$(R SYSTEM.CNF .detail)" '"BOOT2 = cdrom0:\\SLUS_203.12;1"' "text detail"
eq "$(R PACK/D.WAV .detail)" '"WAVE"' "RIFF detail"
eq "$(R DATA/LEVEL.AFS .toc)" '{"kind":"AFS","n":3,"score":1.0,"names":true}' "AFS toc"
eq "$(R DATA/STAGE.PAK .toc.kind)" '"count+pairs"' "PAK toc"
eq "$(R PACK/NOISE.BIN 'has("toc")')" false "noise no toc in tree"
eq "$(R PACK/NOISE.BIN '.entropy > 7.5')" true "noise entropy"
eq "$(R PACK/ZERO.BIN '[.entropy, .entropy_label]')" '[0.0,"mostly empty"]' "zero entropy"
eq "$(R EMPTY.DAT '[.size, .entropy, .detail]')" '[0,0.0,""]' "empty row"
eq "$(R DATA/TITLE.TM2 'keys_unsorted')" '["path","size","kind","detail","entropy","entropy_label"]' "row keys"
eq "$(echo "$OUT" | jq -c '[.[].path] | .[0:4]')" '["EMPTY.DAT","OTHER.ELF","SLUS_203.12","SYSTEM.CNF"]' "walk order: files first, sorted"
eq "$(echo "$OUT" | jq -c '[.[].path] | .[4]')" '"DATA/LEVEL.AFS"' "walk order: then sorted folders"

# ---- test_scan_text_summary_and_sort
ENT=$(R DATA/LEVEL.AFS '.entropy' | xargs printf '%-5.2f')
run "${D[@]}" scan disc --sort size; eq "$CODE" 0 "text scan rc"
has "$OUT" "By kind:" "by kind"; has "$OUT" "Biggest:" "biggest"; has "$OUT" "AFS, 3 entries, names" "afs summary"
eq "$(echo "$OUT" | head -n1)" "size       kind      ent   table of contents        path" "table header"
eq "$(echo "$OUT" | sed -n 2p | cut -c1-27)" "16.0 KB    AFS       $ENT " "biggest row first"
has "$OUT" "AFS, 3 entries, names    DATA/LEVEL.AFS  [CRI archive (unpack works)]" "afs row tail"
has "$OUT" "count+pairs n=4 (1.00)" "toc column"
has "$OUT" "20 files, " "file count"
has "$(echo "$OUT" | tail -n1)" "Biggest: DATA/LEVEL.AFS (16.0 KB), PACK/NOISE.BIN (4.9 KB), PACK/ZERO.BIN (4.9 KB), DATA/STAGE.PAK (4.3 KB)" "biggest list"
run "${D[@]}" scan disc --min-size 5000 --json
eq "$(j '[.[].path]')" '["DATA/LEVEL.AFS","PACK/NOISE.BIN","PACK/ZERO.BIN"]' "--min-size"
run "${D[@]}" scan disc --min-size 0x1388 --json; eq "$(j 'length')" 3 "--min-size hex"
run "${D[@]}" scan disc --min-size nope; eq "$CODE" 2 "bad --min-size"; has "$ERR" "not a number: 'nope'" "bad number text"
run "${D[@]}" scan disc/SOUND/SE.VAG --json; eq "$(j '.[0].path')" '"disc/SOUND/SE.VAG"' "file target keeps its path"
mkdir -p nofiles; run "${D[@]}" scan nofiles; eq "$OUT" "no files" "no files"

# ---- test_scan_embedded_signatures / test_embedded_across_read_chunks
node -e 'process.stdout.write(Buffer.concat([Buffer.from("junk".repeat(10)),Buffer.from("TIM2"),Buffer.alloc(20),Buffer.from("VAGp"),Buffer.alloc(40),Buffer.from("TIM2")]))' > big.bin
run "${D[@]}" scan big.bin --embedded --json; eq "$CODE" 0 "embedded rc"
eq "$(j '.[0].embedded')" '{"TIM2":{"count":2,"first_offsets":[40,108]},"VAG":{"count":1,"first_offsets":[64]}}' "embedded"
run "${D[@]}" scan big.bin --embedded
has "$OUT" "           inside: TIM2 x2 (first 0x28), VAG x1 (first 0x40)" "embedded text"
node -e 'process.stdout.write(Buffer.concat(Array(20).fill(Buffer.concat([Buffer.alloc(61,1),Buffer.from("TIM2")]))))' > c.bin
OUT=$(PACKTOOLS_DATASCAN_CHUNK=64 $T "${D[@]}" scan c.bin --embedded --json)
eq "$(j '.[0].embedded.TIM2')" '{"count":20,"first_offsets":[61,126,191,256,321]}' "embedded across chunks"
OUT=$(PACKTOOLS_DATASCAN_CHUNK=5 $T "${D[@]}" scan c.bin --embedded --json)
eq "$(j '.[0].embedded.TIM2.count')" 20 "embedded with a chunk smaller than the magic"

# ---- test_scan_missing / test_entropy_bounds
run "${D[@]}" scan nope; eq "$CODE" 2 "scan missing"; eq "$ERR" "error: nope does not exist" "scan missing text"
node -e 'process.stdout.write(Buffer.from([...Array(256).keys()]))' > all256.bin
run "${D[@]}" scan all256.bin --json; eq "$(j '.[0] | [.entropy, .entropy_label]')" '[8.0,"compressed/encrypted/ADPCM"]' "entropy 8"

# ---- sniff details: zlib edge cases (and a fuzz against Node's zlib), ISO, memcard, text
mkdir -p zz2; mk zlib zz2/full.z stage; mk zlib zz2/cut.z stage cut=9; mk zlib zz2/bad.z stage badsum
mk hex zz2/btype3.z 789cff00; mk hex zz2/incomplete.z 789c0500020000
run "${D[@]}" scan zz2 --json
eq "$(j 'map({(.path): .kind}) | add')" '{"bad.z":"data","btype3.z":"data","cut.z":"zlib","full.z":"zlib","incomplete.z":"data"}' "zlib edge cases"
mk zfuzz zf 900 > zf.json
run "${D[@]}" scan zf --json
eq "$(echo "$OUT" | jq -c --slurpfile v zf.json '[.[] | select((.kind == "zlib") != $v[0][.path])] | length')" 0 "zlib sniff agrees with Node on 900 files"
eq "$(echo "$OUT" | jq -c '[.[] | select(.kind == "zlib")] | length')" "$(jq '[.[] | select(.)] | length' zf.json)" "zlib accept count"
node -e 'const b=Buffer.alloc(0x8100);b.write("CD001",0x8001);process.stdout.write(b)' > disc.iso
node -e 'const b=Buffer.alloc(600);b.write("Sony PS2 Memory Card Format 1.2.0.0");process.stdout.write(b)' > card.ps2
printf '  \tHello there  \nsecond' > t.txt
printf 'RIFF\0\0\0\0  \t ' > r.riff; printf 'RIFF\0\0\0\0  \t\0' > r0.riff
run "${D[@]}" scan disc.iso --json; eq "$(j '.[0].kind')" '"ISO9660"' "iso"
run "${D[@]}" scan card.ps2 --json; eq "$(j '.[0].kind')" '"memcard"' "memcard"
run "${D[@]}" scan t.txt --json; eq "$(j '.[0] | [.kind, .detail]')" '["text","Hello there"]' "text detail stripped"
run "${D[@]}" scan r.riff --json; eq "$(j '.[0] | [.kind, .detail]')" '["RIFF","RIFF"]' "blank RIFF form"
run "${D[@]}" scan r0.riff --json; eq "$(j '.[0].detail')" '"\u0000"' "str.strip keeps NUL"

# ================================================================ Diff
node -e 'const a=Buffer.alloc(1000),b=Buffer.alloc(1000);b.writeFloatLE(9.8,100);b[110]=1;b[500]=7;require("fs").writeFileSync("a.bin",a);require("fs").writeFileSync("b.bin",b)'
run "${D[@]}" diff a.bin b.bin --json --as f32; eq "$CODE" 1 "diff rc"
eq "$(j '[.ranges[] | [.start, .end]]')" "[[100,111],[500,501]]" "diff ranges"
eq "$(j '.ranges[0].b_values[0]')" '"9.8"' "f32 value"
eq "$(j '.ranges[0] | [.values_at, .a_values, .b_values]')" '[100,["0","0","0"],["9.8","0","9.18355e-41"]]' "f32 values"
eq "$(j '.ranges[1].b_values')" '["9.80909e-45"]' "f32 denormal"
eq "$(j 'keys_unsorted')" '["a","b","size_a","size_b","differing_bytes","ranges","more_ranges"]' "diff keys"
eq "$(j '[.differing_bytes, .more_ranges, .ranges[0].a, .ranges[0].b]')" '[6,0,"00000000000000000000000000000000","cdcc1c41000000000000010000000000"]' "diff summary"
run "${D[@]}" diff a.bin b.bin --json --gap 2; eq "$(j '.ranges | length')" 3 "--gap 2"
run "${D[@]}" diff a.bin b.bin --as u16
eq "$OUT" "6 differing bytes in 2 range(s) (ranges closer than 8 bytes merged)
  0x00000064-0x0000006e      11 bytes  a=00000000000000000000000000000000  b=cdcc1c41000000000000010000000000
      as u16 from 0x64: a=[0, 0, 0, 0] b=[52429, 16668, 0, 0]
  0x000001f4-0x000001f4       1 bytes  a=00000000000000000000000000000000  b=07000000000000000000000000000000
      as u16 from 0x1f4: a=[0] b=[7]" "diff text"
run "${D[@]}" diff a.bin b.bin --max 1
has "$OUT" "6 differing bytes in 2 range(s), first 1 shown" "--max"
run "${D[@]}" diff a.bin b.bin --json --as u32; eq "$(j '.ranges[0].b_values')" '["1092406477","0","65536"]' "u32 values"

# ---- test_identical_and_size_change
printf same > s1.bin; printf same > s2.bin; printf 'same plus' > s3.bin
run "${D[@]}" diff s1.bin s2.bin; eq "$CODE" 0 "identical rc"; eq "$OUT" "identical (4 bytes)" "identical text"
run "${D[@]}" diff s1.bin s3.bin; eq "$CODE" 1 "size change rc"; has "$OUT" "sizes differ" "sizes differ"
eq "$(echo "$OUT" | head -n1)" "sizes differ: s1.bin is 4 bytes, s3.bin is 9 bytes (the last 5 bytes exist only in the second file)" "sizes differ text"
eq "$(echo "$OUT" | sed -n 2p)" "0 differing bytes in 0 range(s) (ranges closer than 8 bytes merged)" "size-only text"

# ---- test_ranges_across_read_chunks
node -e 'const b=Buffer.alloc(300);b.fill(1,60,70);require("fs").writeFileSync("cb.bin",b);require("fs").writeFileSync("ca.bin",Buffer.alloc(300))'
OUT=$(PACKTOOLS_DATASCAN_CHUNK=64 $T "${D[@]}" diff ca.bin cb.bin --json)
eq "$(j '[[.ranges[] | [.start, .end]], .differing_bytes]')" "[[[60,70]],10]" "diff across chunks"

# ---- test_folders / test_file_against_folder
mkdir -p x y; printf 1 > x/same.bin; printf 1 > y/same.bin; printf abcd > x/level.dat; printf abXd > y/level.dat; printf g > x/gone.bin; printf n > y/new.bin
run "${D[@]}" diff x y --json; eq "$CODE" 1 "folders rc"
eq "$(j '[.added, .removed, [.changed[].path]]')" '[["new.bin"],["gone.bin"],["level.dat"]]' "folders"
eq "$(j '.changed[0]')" '{"path":"level.dat","size_a":4,"size_b":4,"differing_bytes":1,"ranges":1}' "changed row"
run "${D[@]}" diff x y
eq "$OUT" "  added    new.bin
  removed  gone.bin
  changed  level.dat: 1 bytes in 1 range(s)" "folders text"
run "${D[@]}" diff x x; eq "$CODE" 0 "same folder rc"; eq "$OUT" "identical folders (3 files)" "same folder text"
run "${D[@]}" diff a.bin x; eq "$CODE" 2 "file vs folder"; eq "$ERR" "error: compare two files or two folders, not one of each" "file vs folder text"
run "${D[@]}" diff a.bin nope; eq "$CODE" 2 "diff missing"

# ================================================================ Strings
printf '\0\0STAGE_01\0\001ab\0enemy_spawn\377' > s.bin
run "${D[@]}" strings s.bin --json
eq "$(j '[.[] | [.offset, .text]]')" '[[2,"STAGE_01"],[15,"enemy_spawn"]]' "ascii offsets"
run "${D[@]}" strings s.bin
eq "$OUT" "0x00000002  STAGE_01
0x0000000f  enemy_spawn" "ascii text"
run "${D[@]}" strings s.bin -n 2 --json; eq "$(j '[.[].text]')" '["STAGE_01","ab","enemy_spawn"]' "-n 2"

# ---- test_utf16_and_sjis ("ステージ１" in cp932 is 8358 8365 815b 8357 8250)
mk hex u.bin 01024c006500760065006c0000000383588365815b83578250009fff
run "${D[@]}" strings u.bin --encoding all --json
eq "$OUT" '[
  {
    "offset": 2,
    "encoding": "utf16le",
    "text": "Level"
  },
  {
    "offset": 15,
    "encoding": "sjis",
    "text": "ステージ１"
  }
]' "utf16 and sjis json"
run "${D[@]}" strings u.bin --encoding all
eq "$OUT" "0x00000002  utf16le Level
0x0000000f  sjis    ステージ１" "utf16 and sjis text"
mk hex k.bin 00889f889f889f00   # three kanji: decodes, but under half kana
run "${D[@]}" strings k.bin --encoding sjis --json -n 1; eq "$(j 'length')" 0 "kanji-only run dropped"
mk hex k2.bin 00838883888388889f00   # three katakana + a kanji
run "${D[@]}" strings k2.bin --encoding sjis --json -n 1; eq "$(j '.[0].offset')" 1 "kana-heavy run kept"
mk hex k3.bin 008540854000   # 0x8540 is unmapped in cp932: strict decode fails
run "${D[@]}" strings k3.bin --encoding sjis --json -n 1; eq "$(j 'length')" 0 "unmapped double byte dropped"

# ---- test_sjis_mostly_ignores_random_bytes
mk blob noise2.bin 50000 21
run "${D[@]}" strings noise2.bin --encoding sjis --json; eq "$(j 'length < 20')" true "sjis on noise"

# ---- test_grep_and_max
printf 'alpha\0BOSS_ROOM\0boss_hp\0other\0' > g.bin
run "${D[@]}" strings g.bin --grep boss; eq "$(echo "$OUT" | wc -l)" 2 "grep 2 lines"
run "${D[@]}" strings g.bin --max 1; has "$OUT" "more" "max more"
eq "$(echo "$OUT" | tail -n1)" "... 3 more (raise --max)" "max text"
run "${D[@]}" strings g.bin --max -1 --json; eq "$(j '[.[].text]')" '["alpha","BOSS_ROOM","boss_hp"]' "negative --max slices from the end"
run "${D[@]}" strings g.bin --min -1; eq "$CODE" 2 "negative --min refused"

# ---- test_strings_across_read_chunks
node -e 'process.stdout.write(Buffer.concat([Buffer.alloc(60),Buffer.from("crossing_the_edge"),Buffer.alloc(10),Buffer.from("ab"),Buffer.alloc(50),Buffer.from("tail_string")]))' > cs.bin
OUT=$(PACKTOOLS_DATASCAN_CHUNK=64 $T "${D[@]}" strings cs.bin --json)
eq "$(j '[.[] | [.offset, .text]]')" '[[60,"crossing_the_edge"],[139,"tail_string"]]' "strings across chunks"
OUT=$(PACKTOOLS_DATASCAN_CHUNK=7 $T "${D[@]}" strings u.bin --encoding all --json)
eq "$(j '[.[] | [.offset, .encoding]]')" '[[2,"utf16le"],[15,"sjis"]]' "utf16/sjis across tiny chunks"

# ---- test_strings_on_folder
run "${D[@]}" strings "$TMP"; eq "$CODE" 2 "strings on folder"

# ================================================================ Files
printf '%s\n' 'Using argv boot path' 'ELF file loaded successfully. Entry point: 0x00100008' \
  '[sceCdSearchFile] pc=0x1000 ra=0x2000 file=0x3000 pathAddr=0x4000 path="\DATA\STAGE1.AFS;1"' \
  '[sceCdSearchFile:ok] path="\DATA\STAGE1.AFS;1" lsn=0x1a2 size=0x8000 sectors=0x10' \
  'sceCdSearchFile failed: \DATA\MISSING.BIN;1 (root: /games/x, repeat=1)' \
  '[sceCdSearchFile:ok] path="\DATA\STAGE1.AFS;1" lsn=0x1a2 size=0x8000 sectors=0x10' \
  '[sceCdSearchFile:ok] path="\SOUND\BGM.VAG;1" lsn=0x300 size=0x100 sectors=0x1' > run.log
run "${D[@]}" files run.log --json; eq "$CODE" 1 "files rc"
eq "$(j '[.[].path]')" '["\\DATA\\STAGE1.AFS;1","\\DATA\\MISSING.BIN;1","\\SOUND\\BGM.VAG;1"]' "files paths"
eq "$(j '.[0]')" '{"path":"\\DATA\\STAGE1.AFS;1","first_line":4,"lookups":2,"status":"found","size":32768,"lsn":418}' "files row"
eq "$(j '.[1] | [.status, .size, .lsn]')" '["missing",null,null]' "missing row"
run "${D[@]}" files run.log
eq "$OUT" 'line 4       found    32.0 KB    x2    \DATA\STAGE1.AFS;1
line 5       missing  -          x1    \DATA\MISSING.BIN;1
line 7       found    256 B      x1    \SOUND\BGM.VAG;1
3 file(s) looked up, 1 missing. The runtime traces the first 128 lookups, then every 512th.' "files text"
printf 'sceCdSearchFile failed: \\data\\x.bin;1 (normalized path is empty, root: /r)\r\n[sceCdSearchFile:ok] path="\\DATA\\X.BIN;1" lsn=0x5 size=0x10\r\n' > crlf.log
run "${D[@]}" files crlf.log --json; eq "$CODE" 0 "found later wins"
eq "$(j '.[0] | [.path, .status, .lookups, .size]')" '["\\data\\x.bin;1","found",2,16]' "case-folded key, first spelling kept"

# ---- test_no_lookups
printf 'nothing here\n' > none.log
run "${D[@]}" files none.log; eq "$CODE" 0 "no lookups rc"; has "$OUT" "no sceCdSearchFile" "no lookups text"
eq "$OUT" "no sceCdSearchFile lines in none.log (runtime logs need PS2X_ENABLE_RUNTIME_LOGS, on by default)" "no lookups exact"

# ================================================================ Cli
run "${D[@]}" --help; eq "$CODE" 0 "help rc"; has "$OUT" "usage" "usage"
has "$OUT" "Only for discs you own. Unpacked game files stay on your machine; never" "epilog"
for c in scan unpack repack diff strings files; do run "${D[@]}" "$c" --help; eq "$CODE" 0 "$c help"; has "$OUT" "usage: datascan $c" "$c usage"; done
run "${D[@]}" scan --help; has "$OUT" "[--min-size MIN_SIZE]" "min-size metavar"
run "${D[@]}" diff --help; has "$OUT" "[--as {u8,u16,u32,f32}]" "--as metavar"
run "${D[@]}"; eq "$CODE" 2 "no command rc 2"; has "$ERR" "required: command" "command required"
run "${D[@]}" unpack LEVEL.AFS; eq "$CODE" 2 "--out required"

done_ console-levels/datascan

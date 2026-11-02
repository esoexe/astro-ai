#!/usr/bin/env bash
# console-reverse/iso: the IsoTests of console-skill/tests/test_reverse.py, plus exact text and JSON, raw 2352-byte
# layouts, multi-extent records, damaged and truncated images, unsafe names, symlinks, SYSTEM.CNF edge cases, run
# against the C++ tool. Images come from a JavaScript copy of the test file's make_iso(); expected LBAs are worked out
# by hand in the comments (PVD 16, terminator 17, directories from 18 in (depth, path) order, then files sorted).
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
IS=(console-reverse/iso)
cat > mkiso.js <<'EOF'
// node mkiso.js iso OUT [json spec] | node mkiso.js file NAME OUT
// spec: {game, files:{path: latin1}, layout:"2048"|"mode1"|"mode2", extra_root:[[raw, [chunks], [flags]]], volume_id, truncate, patch:[[off,[bytes]]]}
const fs = require("fs");
const BLOCK = 2048, SYNC = Buffer.from([0, ...Array(10).fill(255), 0]);
const bin = (s) => Buffer.from(s, "latin1");
const CNF = bin("BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n");
const game = () => ({
  "SYSTEM.CNF": CNF,
  "SLUS_203.12": Buffer.concat([bin("\x7fELF"), Buffer.from(Array.from({ length: 2996 }, (_, i) => i % 251))]),
  "MODULES/SIO2MAN.IRX": Buffer.concat([bin("\x7fIRX"), Buffer.alloc(100, 1)]),
  "DATA/LEVEL1.BIN": Buffer.from(Array.from({ length: 5000 }, (_, i) => (i * 7) & 0xff)),
  "DATA/SUB/DEEP.DAT": bin("deep"),
  "EMPTY.TXT": Buffer.alloc(0),
});
const be32 = (v) => { const b = Buffer.alloc(8); b.writeUInt32LE(v >>> 0, 0); b.writeUInt32BE(v >>> 0, 4); return b; };
const be16 = (v) => { const b = Buffer.alloc(4); b.writeUInt16LE(v, 0); b.writeUInt16BE(v, 2); return b; };
function dirRecord(raw, lba, size, flags) {
  const nl = raw.length, len = 33 + nl + (nl % 2 === 0 ? 1 : 0), r = Buffer.alloc(len);
  r[0] = len; be32(lba).copy(r, 2); be32(size).copy(r, 10); Buffer.from([126, 1, 1, 0, 0, 0, 0]).copy(r, 18);
  r[25] = flags; be16(1).copy(r, 28); r[32] = nl; raw.copy(r, 33); return r;
}
function pack(recs) {
  const secs = [[]]; let used = 0;
  for (const r of recs) { if (used + r.length > BLOCK) { secs.push([]); used = 0; } secs[secs.length - 1].push(r); used += r.length; }
  return secs.map((s) => { const b = Buffer.concat(s); return Buffer.concat([b, Buffer.alloc(BLOCK - b.length)]); });
}
const cmpStr = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
function makeIso(files, { layout = "2048", extra_root = [], volume_id = "TESTDISC", truncate = 0 } = {}) {
  const dirs = { "": [] };
  const names = Object.keys(files).sort(cmpStr);
  for (const p of names) {
    const parts = p.split("/");
    for (let i = 1; i < parts.length; i++) {
      const d = parts.slice(0, i).join("/");
      if (!(d in dirs)) { dirs[d] = []; dirs[parts.slice(0, i - 1).join("/")].push(["dir", parts[i - 1], d]); }
    }
    dirs[parts.slice(0, -1).join("/")].push(["file", parts[parts.length - 1], p]);
  }
  const depth = (d) => (d ? d.split("/").length - 1 : -1);
  const order = Object.keys(dirs).sort((a, b) => depth(a) - depth(b) || cmpStr(a, b));
  const extras = extra_root.map(([raw, chunks, flags]) => [bin(raw), chunks.map(bin), flags]);
  const recordsFor = (d, lbas, sizes, flba) => {
    const parent = d ? d.slice(0, Math.max(0, d.lastIndexOf("/"))) : "";
    const recs = [dirRecord(Buffer.from([0]), lbas[d], sizes[d], 2), dirRecord(Buffer.from([1]), d ? lbas[d.lastIndexOf("/") < 0 ? "" : parent] : lbas[""], 0, 2)];
    for (const [kind, name, full] of [...dirs[d]].sort((a, b) => cmpStr(a[1], b[1])))
      recs.push(kind === "dir" ? dirRecord(bin(name), lbas[full], sizes[full], 2) : dirRecord(bin(name + ";1"), flba[full], files[full].length, 0));
    if (d === "") extras.forEach(([raw, chunks, flags], k) => chunks.forEach((c, j) => recs.push(dirRecord(raw, flba["\0x" + k][j], c.length, flags[j]))));
    return recs;
  };
  const zero = {}; for (const d in dirs) zero[d] = 0;
  const zf = {}; for (const p of names) zf[p] = 0; extras.forEach(([, chunks], k) => (zf["\0x" + k] = chunks.map(() => 0)));
  const sizes = {}; for (const d of order) sizes[d] = pack(recordsFor(d, zero, zero, zf)).length * BLOCK;
  let nxt = 18; const lbas = {};
  for (const d of order) { lbas[d] = nxt; nxt += sizes[d] / BLOCK; }
  const flba = {}, blobs = [];
  for (const p of names) { flba[p] = nxt; blobs.push([nxt, files[p]]); nxt += Math.max(1, Math.ceil(files[p].length / BLOCK)); }
  extras.forEach(([, chunks], k) => { flba["\0x" + k] = []; for (const c of chunks) { flba["\0x" + k].push(nxt); blobs.push([nxt, c]); nxt += Math.max(1, Math.ceil(c.length / BLOCK)); } });
  const total = nxt, blocks = Array.from({ length: total }, () => Buffer.alloc(BLOCK));
  const pvd = blocks[16]; pvd[0] = 1; bin("CD001").copy(pvd, 1); pvd[6] = 1;
  bin("PLAYSTATION".padEnd(32)).copy(pvd, 8); bin(volume_id.padEnd(32)).copy(pvd, 40);
  be32(total).copy(pvd, 80); be16(BLOCK).copy(pvd, 128); dirRecord(Buffer.from([0]), lbas[""], sizes[""], 2).copy(pvd, 156);
  const term = blocks[17]; term[0] = 255; bin("CD001").copy(term, 1); term[6] = 1;
  for (const d of order) pack(recordsFor(d, lbas, sizes, flba)).forEach((s, i) => (blocks[lbas[d] + i] = s));
  for (const [start, data] of blobs) for (let i = 0; i < Math.max(1, data.length); i += BLOCK) data.subarray(i, i + BLOCK).copy(blocks[start + i / BLOCK]);
  let img;
  if (layout === "2048") img = Buffer.concat(blocks);
  else img = Buffer.concat(blocks.map((b, n) => {
    const mode = layout === "mode1" ? 1 : 2, hdr = Buffer.concat([SYNC, Buffer.from([0, 2, n % 75, mode])]);
    return mode === 1 ? Buffer.concat([hdr, b, Buffer.alloc(288)]) : Buffer.concat([hdr, Buffer.alloc(8), b, Buffer.alloc(280)]);
  }));
  if (truncate) img = img.subarray(0, img.length - truncate);
  return { img, total, lbas, flba };
}
const [cmd, a1, a2] = process.argv.slice(2);
if (cmd === "file") { fs.writeFileSync(a2, game()[a1]); process.exit(0); }
const spec = a2 ? JSON.parse(a2) : { game: true };
const files = spec.game ? game() : {};
for (const [k, v] of Object.entries(spec.files || {})) files[k] = bin(v);
const r = makeIso(files, spec);
let img = Buffer.from(r.img);
for (const [off, bytes] of spec.patch || []) Buffer.from(bytes).copy(img, off);
fs.writeFileSync(a1, img);
console.log(img.length, r.total);
EOF
mk() { node "$TMP/mkiso.js" iso "$@" >/dev/null; }
gf() { node "$TMP/mkiso.js" file "$1" "$2"; }
CNFJ='BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n'   # JSON-escaped CNF
mk g.iso; mk m1.bin '{"game":true,"layout":"mode1"}'; mk m2.bin '{"game":true,"layout":"mode2"}'
eq "$(wc -c < g.iso | tr -d ' ')" 63488 "fixture: 31 blocks"
eq "$(wc -c < m1.bin | tr -d ' ')" 72912 "raw fixture: 31 x 2352"

# ---- info (test_info_complete, test_info_json_and_truncated_dump)
run "${IS[@]}" info g.iso
eq "$CODE" 0 "info exit 0"
eq "$OUT" "Image        $TMP/g.iso
Layout       2048-byte sectors (plain ISO)
Volume       TESTDISC (system PLAYSTATION)
Size         63488 bytes, volume says 31 blocks (63488 bytes)
Dump         complete (the image is at least as big as the volume)" "info text"
mk short.iso '{"game":true,"truncate":4000}'
run "${IS[@]}" info short.iso --json
eq "$CODE" 1 "short dump exit 1"
eq "$OUT" "{
  \"image\": \"$TMP/short.iso\",
  \"layout\": \"2048-byte sectors (plain ISO)\",
  \"volume_id\": \"TESTDISC\",
  \"system_id\": \"PLAYSTATION\",
  \"volume_blocks\": 31,
  \"image_bytes\": 59488,
  \"expected_bytes\": 63488,
  \"complete\": false
}" "info json, key order"
run "${IS[@]}" info short.iso
has "$OUT" "Dump         SHORT by 4000 bytes: re-dump the disc, some files will be missing" "short text"
mk novol.iso '{"game":true,"volume_id":""}'
run "${IS[@]}" info novol.iso
has "$OUT" "Volume       (no name) (system PLAYSTATION)" "empty volume id"
run "${IS[@]}" ls novol.iso
has "$OUT" "(no volume name)  (2048-byte sectors (plain ISO))" "ls header without a volume name"

# ---- raw layouts (test_raw_2352_layouts)
for L in m1 m2; do
  run "${IS[@]}" info $L.bin
  eq "$CODE" 0 "$L info exit 0"
  has "$OUT" "2352" "$L layout"
done
run "${IS[@]}" info m1.bin --json
has "$OUT" '"layout": "2352-byte raw sectors, Mode 1"' "mode 1 name"
has "$OUT" '"expected_bytes": 72912' "raw expected bytes"
run "${IS[@]}" info m2.bin
has "$OUT" "Layout       2352-byte raw sectors, Mode 2 Form 1" "mode 2 name"
node -e 'const f=require("fs");const b=f.readFileSync("m1.bin");b[16*2352+1]=0;f.writeFileSync("nosync.bin",b)'
run "${IS[@]}" info nosync.bin
eq "$CODE" 2 "raw image without SYNC exit 2"
eq "$ERR" "iso: nosync.bin is not an ISO 9660 image (no CD001 volume descriptor at sector 16)" "SYNC required for raw layouts"

# ---- ls (test_ls_root, subdir, recursive json, case-insensitive, device prefix, missing)
run "${IS[@]}" ls g.iso
eq "$CODE" 0 "ls exit 0"
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  dir             -  lba 19       /DATA/
  file            0  lba 26       /EMPTY.TXT
  dir             -  lba 20       /MODULES/
  file         3000  lba 28       /SLUS_203.12
  file           57  lba 30       /SYSTEM.CNF
5 entries, 3 files, 3057 bytes" "ls root text"
hasnt "$OUT" ";1" "versions stripped"
run "${IS[@]}" ls g.iso /
eq "$(echo "$OUT" | tail -1)" "5 entries, 3 files, 3057 bytes" "ls / is the root"
run "${IS[@]}" ls g.iso DATA
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  file         5000  lba 22       /DATA/LEVEL1.BIN
  dir             -  lba 21       /DATA/SUB/
2 entries, 1 files, 5000 bytes" "ls subdir"
run "${IS[@]}" ls g.iso -r
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  dir             -  lba 19       /DATA/
  file         5000  lba 22       /DATA/LEVEL1.BIN
  dir             -  lba 21       /DATA/SUB/
  file            4  lba 25       /DATA/SUB/DEEP.DAT
  file            0  lba 26       /EMPTY.TXT
  dir             -  lba 20       /MODULES/
  file          104  lba 27       /MODULES/SIO2MAN.IRX
  file         3000  lba 28       /SLUS_203.12
  file           57  lba 30       /SYSTEM.CNF
9 entries, 6 files, 8165 bytes" "ls -r is depth first"
run "${IS[@]}" ls g.iso -r --json
eq "$(echo "$OUT" | jq -r '.[] | select(.path=="/DATA/LEVEL1.BIN") | .size')" 5000 "json size"
eq "$(echo "$OUT" | jq -c '.[] | select(.path=="/MODULES")')" '{"path":"/MODULES","name":"MODULES","type":"dir","size":null,"lba":20}' "json dir row, key order"
eq "$(echo "$OUT" | jq length)" 9 "json rows"
has "$OUT" '[
  {
    "path": "/DATA",' "json indent 2"
run "${IS[@]}" ls g.iso 'cdrom0:\modules\sio2man.irx;1'
eq "$CODE" 0 "device prefix + case-insensitive exit 0"
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  file          104  lba 27       /MODULES/SIO2MAN.IRX
1 entries, 1 files, 104 bytes" "ls of one file"
run "${IS[@]}" ls g.iso '  CDROM:/data//sub/  '
has "$OUT" "/DATA/SUB/DEEP.DAT" "cdrom: prefix, stripped, doubled slashes"
run "${IS[@]}" ls g.iso NOPE
eq "$CODE" 2 "missing path exit 2"
eq "$ERR" "iso: not found on the disc: NOPE" "missing path message"
run "${IS[@]}" ls g.iso SYSTEM.CNF/X
eq "$ERR" "iso: not found on the disc: SYSTEM.CNF/X" "a file is not a folder"

# ---- names: Unicode digits after ';', trailing dot, latin-1, non-versions
mk u.iso "{\"files\":{\"SYSTEM.CNF\":\"$CNFJ\"},\"extra_root\":[[\"A.TXT;\u00b2\",[\"a\"],[0]],[\"README.;1\",[\"b\"],[0]],[\"CAF\u00c9.TXT;1\",[\"c\"],[0]],[\"X;1A\",[\"d\"],[0]],[\"V;\",[\"e\"],[0]]]}"
run "${IS[@]}" ls u.iso
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  file           57  lba 19       /SYSTEM.CNF
  file            1  lba 20       /A.TXT
  file            1  lba 21       /README
  file            1  lba 22       /CAFÉ.TXT
  file            1  lba 23       /X;1A
  file            1  lba 24       /V;
6 entries, 6 files, 62 bytes" "clean_name: ;² counts as digits, one trailing dot, latin-1"
run "${IS[@]}" ls u.iso --json
has "$OUT" '"name": "CAF\u00c9.TXT"' "json is ASCII-escaped"
run "${IS[@]}" ls u.iso 'café.txt'
has "$OUT" "lba 22       /CAFÉ.TXT" "case-insensitive beyond ASCII"
run "${IS[@]}" ls u.iso 'cdrom0:\README.;1'
has "$OUT" "/README" "disc path cleaned like a record"
run "${IS[@]}" ls u.iso 'Ā.TXT'
eq "$ERR" "iso: not found on the disc: Ā.TXT" "characters above U+00FF become ? and do not match"

# ---- multi-extent and many entries (test_multi_extent_file_is_joined, test_many_entries_span_directory_sectors)
A2048=$(printf 'A%.0s' $(seq 2048))
mk multi.iso "{\"files\":{\"SYSTEM.CNF\":\"$CNFJ\"},\"extra_root\":[[\"BIG.DAT;1\",[\"$A2048\",\"$A2048\",\"$(printf 'B%.0s' $(seq 100))\"],[128,128,0]],[\"HALF.DAT;1\",[\"$A2048\"],[128]],[\"NEXT.DAT;1\",[\"n\"],[0]]]}"
run "${IS[@]}" ls multi.iso
eq "$OUT" "TESTDISC  (2048-byte sectors (plain ISO))
  file           57  lba 19       /SYSTEM.CNF
  file         4196  lba 20       /BIG.DAT
  file         2048  lba 23       /HALF.DAT
  file            1  lba 24       /NEXT.DAT
4 entries, 4 files, 6302 bytes" "three extents joined; a different name ends a pending file"
run "${IS[@]}" extract multi.iso --out m
eq "$CODE" 0 "multi extract exit 0"
eq "$(wc -c < m/BIG.DAT | tr -d ' ')" 4196 "joined size on disk"
eq "$(head -c 4096 m/BIG.DAT | tr -d A | wc -c | tr -d ' ')$(tail -c 100 m/BIG.DAT | tr -d B | wc -c | tr -d ' ')" 00 "joined bytes in order"
MJ='{"files":{"SYSTEM.CNF":"'"$CNFJ"'"'
for i in $(seq 0 119); do MJ+=",\"MANY/FILE_$(printf %03d "$i").DAT\":\"x\""; done
mk many.iso "$MJ}}"
run "${IS[@]}" ls many.iso MANY --json
eq "$(echo "$OUT" | jq length)" 120 "120 entries across directory sectors"
eq "$(echo "$OUT" | jq -r '.[119].name')" FILE_119.DAT "last entry from the second sector"

# ---- extract (round trip, --only, keep/force, json)
run "${IS[@]}" extract g.iso --out console/disc
eq "$CODE" 0 "extract exit 0"
eq "$OUT" "Extracted 6 files (8165 bytes) into $TMP/console/disc
Boot ELF     $TMP/console/disc/SLUS_203.12  (SLUS-20312)
Run the port with that path as argv[1]: the runtime reads cdrom0: from this folder." "extract text"
ALLSAME=1
for f in SYSTEM.CNF SLUS_203.12 MODULES/SIO2MAN.IRX DATA/LEVEL1.BIN DATA/SUB/DEEP.DAT EMPTY.TXT; do
  gf "$f" ref.bin; cmp -s ref.bin "console/disc/$f" || { ALLSAME=0; echo "  differs: $f"; }
done
eq "$ALLSAME" 1 "every file round-trips byte for byte"
eq "$(find console -name '*.part' | wc -l | tr -d ' ')" 0 "no .part files left"
for L in m1 m2; do
  run "${IS[@]}" extract $L.bin --out raw_$L
  gf DATA/LEVEL1.BIN ref.bin; cmp -s ref.bin raw_$L/DATA/LEVEL1.BIN && ok || bad "$L extract bytes"
done
printf 'my edit' > console/disc/DATA/LEVEL1.BIN
run "${IS[@]}" extract g.iso --out console/disc
eq "$OUT" "Extracted 0 files (0 bytes) into $TMP/console/disc
Kept 6 files that already existed (your edits stay; pass --force to overwrite them)
Boot ELF     $TMP/console/disc/SLUS_203.12  (SLUS-20312)
Run the port with that path as argv[1]: the runtime reads cdrom0: from this folder." "re-extract keeps everything"
eq "$(cat console/disc/DATA/LEVEL1.BIN)" "my edit" "the edit survives"
run "${IS[@]}" extract g.iso --out console/disc --force --json
eq "$OUT" "{
  \"out\": \"$TMP/console/disc\",
  \"written\": 6,
  \"kept_existing\": 0,
  \"bytes\": 8165,
  \"boot_elf\": \"$TMP/console/disc/SLUS_203.12\",
  \"serial\": \"SLUS-20312\"
}" "extract json, key order"
gf DATA/LEVEL1.BIN ref.bin; cmp -s ref.bin console/disc/DATA/LEVEL1.BIN && ok || bad "--force restores the file"
run "${IS[@]}" extract g.iso --out x --only DATA/SUB SYSTEM.CNF --json
eq "$CODE" 0 "--only exit 0"
eq "$(echo "$OUT" | jq -c '[.written, .bytes, .boot_elf, .serial]')" '[2,61,null,"SLUS-20312"]' "--only: 2 files; SYSTEM.CNF read, boot ELF not extracted"
[ -f x/DATA/SUB/DEEP.DAT ] && [ -f x/SYSTEM.CNF ] && [ ! -e x/DATA/LEVEL1.BIN ] && [ ! -e x/MODULES ] && ok || bad "--only keeps the tree and nothing else"
run "${IS[@]}" extract g.iso --out x2 --only /
eq "$ERR" "iso: refusing unsafe path on the disc: '' (nothing written)" "--only / names the root, which has no name"
run "${IS[@]}" extract g.iso --out x3 --only NOPE
eq "$ERR" "iso: not found on the disc: NOPE" "--only missing"
mkdir -p dang && ln -s "$TMP/nowhere" dang/EMPTY.TXT
run "${IS[@]}" extract g.iso --out dang --json
eq "$(echo "$OUT" | jq -c '[.written, .kept_existing]')" '[5,1]' "a dangling symlink counts as existing"
mkdir -p sy elsewhere && ln -s "$TMP/elsewhere" sy/DATA
run "${IS[@]}" extract g.iso --out sy
eq "$CODE" 2 "symlinked folder exit 2"
eq "$ERR" "iso: refusing to write outside $TMP/sy: DATA/LEVEL1.BIN" "realpath containment"
eq "$(ls elsewhere | wc -l | tr -d ' ')" 0 "nothing written through the symlink"

# ---- unsafe names and truncation (test_extract_refuses_*); a raw ".." loses one trailing dot in clean_name, like upstream
mk evil.iso "{\"files\":{\"SYSTEM.CNF\":\"$CNFJ\"},\"extra_root\":[[\"../EVIL.TXT;1\",[\"x\"],[0]]]}"
run "${IS[@]}" extract evil.iso --out out
eq "$CODE" 2 "path traversal exit 2"
eq "$ERR" "iso: refusing unsafe path on the disc: '../EVIL.TXT' (nothing written)" "traversal message"
[ ! -e EVIL.TXT ] && [ ! -e out ] && ok || bad "nothing written, not even the folder"
for pair in '..|'"'.'" 'A\\B.TXT;1|'"'A\\\\\\\\B.TXT'" 'C:X;1|'"'C:X'" '\u0001X;1|'"'\\\\x01X'" '.|'"'.'"; do
  RAW=${pair%%|*}; REPR=${pair#*|}
  mk bad.iso "{\"files\":{\"SYSTEM.CNF\":\"$CNFJ\"},\"extra_root\":[[\"$RAW\",[\"x\"],[0]]]}"
  run "${IS[@]}" extract bad.iso --out o2
  eq "$CODE" 2 "unsafe $RAW exit 2"
  eq "$ERR" "iso: refusing unsafe path on the disc: $(printf '%b' "$REPR") (nothing written)" "unsafe $RAW repr"
done
[ ! -e o2 ] && ok || bad "unsafe images write nothing"
run "${IS[@]}" extract short.iso --out o
eq "$CODE" 2 "truncated extract exit 2"
eq "$ERR" "iso: /SLUS_203.12 points past the end of the image: the dump is truncated (nothing written; re-dump the disc)" "truncated message names the first short file"
[ ! -e o ] && ok || bad "truncated: nothing written"
mk cut.iso '{"game":true,"truncate":23488}'   # 40000 bytes: the root (lba 18) fits, DATA (lba 19) does not
run "${IS[@]}" ls cut.iso DATA
eq "$ERR" "iso: directory /DATA points past the end of the image (truncated dump?)" "truncated directory"

# ---- damaged images
mk bs.iso '{"game":true,"patch":[[32896,[0,2]]]}'
run "${IS[@]}" info bs.iso
eq "$ERR" "iso: unsupported logical block size 512 (expected 2048)" "block size"
mk nopvd.iso '{"game":true,"patch":[[32768,[2]]]}'
run "${IS[@]}" info nopvd.iso
eq "$ERR" "iso: nopvd.iso has no ISO 9660 primary volume descriptor" "no PVD"
mk noroot.iso '{"game":true,"patch":[[32924,[0]]]}'
run "${IS[@]}" info noroot.iso
eq "$ERR" "iso: the root directory record is damaged" "root record"
mk dmg.iso '{"game":true,"patch":[[36864,[20]]]}'
run "${IS[@]}" ls dmg.iso
eq "$CODE" 2 "damaged record exit 2"
eq "$ERR" "iso: damaged directory record in / (sector 18)" "damaged record"
mk loop.iso '{"game":true,"patch":[[39028,[18,0,0,0]]]}'   # DATA/SUB's record (offset 114 in lba 19) points at the root
run "${IS[@]}" ls loop.iso -r
eq "$CODE" 0 "directory loop terminates"
has "$OUT" "  dir             -  lba 18       /DATA/SUB/" "the looping folder is listed once"
eq "$(echo "$OUT" | tail -1)" "8 entries, 5 files, 8161 bytes" "and not walked again"

# ---- not an image (test_not_an_iso, test_missing_file)
head -c 40960 /dev/zero > junk.iso
run "${IS[@]}" info junk.iso
eq "$CODE" 2 "junk exit 2"
eq "$ERR" "iso: junk.iso is not an ISO 9660 image (no CD001 volume descriptor at sector 16)" "junk message"
head -c 34816 /dev/zero > edge.iso; head -c 34815 /dev/zero > tiny.iso
run "${IS[@]}" ls edge.iso
eq "$ERR" "iso: edge.iso is not an ISO 9660 image (no CD001 volume descriptor at sector 16)" "17 sectors is big enough"
run "${IS[@]}" ls tiny.iso
eq "$ERR" "iso: tiny.iso is too small to be a disc image (34815 bytes)" "one byte less is too small"
run "${IS[@]}" info nope.iso
eq "$CODE" 2 "missing exit 2"
eq "$ERR" "iso: cannot open nope.iso: No such file or directory" "missing message"
run "${IS[@]}" info console
eq "$ERR" "iso: cannot open console: Is a directory" "folder message"

# ---- plain (test_plain_converts_raw_images)
for L in m1 m2; do
  run "${IS[@]}" plain $L.bin --out plain_$L.iso
  eq "$CODE" 0 "$L plain exit 0"
  eq "$OUT" "Wrote plain_$L.iso: 31 sectors of 2048 bytes (63488 bytes)
It is a copy of your own disc: keep it on this machine." "$L plain text"
  cmp -s plain_$L.iso g.iso && ok || bad "$L plain equals the 2048-byte image"
  run "${IS[@]}" plain $L.bin --out plain_$L.iso
  eq "$CODE" 1 "$L refuses to overwrite"
  eq "$ERR" "iso: plain_$L.iso already exists (pass --force to replace it)" "$L refusal message"
  run "${IS[@]}" plain $L.bin --out plain_$L.iso --force
  eq "$CODE" 0 "$L --force"
done
[ ! -e plain_m1.iso.part ] && ok || bad "no .part left"
run "${IS[@]}" plain g.iso --out x.iso
eq "$OUT" "g.iso already has 2048-byte sectors: use it as it is." "plain on a plain image"
[ ! -e x.iso ] && ok || bad "nothing written for a plain image"
run "${IS[@]}" plain m1.bin --out m1.bin --force
eq "$ERR" "iso: --out must be a new file, not the source image" "plain onto itself"
run "${IS[@]}" plain m1.bin --out nodir/x.iso
eq "$ERR" "iso: cannot write nodir/x.iso: No such file or directory" "plain into a missing folder"
head -c 71912 m2.bin > m2short.bin
run "${IS[@]}" plain m2short.bin --out p2s.iso
has "$OUT" "Wrote p2s.iso: 30 sectors of 2048 bytes (61440 bytes)" "whole sectors only"

# ---- boot (from image, from folder, missing ELF, PS1, no SYSTEM.CNF, regions)
run "${IS[@]}" boot g.iso
eq "$CODE" 0 "boot exit 0"
eq "$OUT" 'BOOT2        cdrom0:\SLUS_203.12;1
Boot ELF     SLUS_203.12
Serial       SLUS-20312
Region       USA
VER          1.00
VMODE        NTSC
Found        /SLUS_203.12 (3000 bytes)' "boot text"
run "${IS[@]}" boot m2.bin --json
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["boot2","ver","vmode","elf","serial","prefix","region","source","elf_found","elf_path","elf_size","elf_lba"]' "boot json key order"
eq "$(echo "$OUT" | jq -c '[.elf, .serial, .prefix, .region, .ver, .vmode, .elf_found, .elf_lba, .source]')" "[\"SLUS_203.12\",\"SLUS-20312\",\"SLUS\",\"USA\",\"1.00\",\"NTSC\",true,28,\"$TMP/m2.bin\"]" "boot json values"
run "${IS[@]}" boot console/disc
eq "$CODE" 0 "boot from folder exit 0"
has "$OUT" "Found        $TMP/console/disc/SLUS_203.12 (3000 bytes)" "folder ELF path"
run "${IS[@]}" boot console/disc --json
eq "$(echo "$OUT" | jq -c 'keys_unsorted | length')" 11 "folder json has no elf_lba"
mk noelf.iso "{\"files\":{\"SYSTEM.CNF\":\"$CNFJ\"}}"
run "${IS[@]}" boot noelf.iso
eq "$CODE" 1 "missing ELF exit 1"
has "$OUT" "Found        NO: the boot ELF named in SYSTEM.CNF is not there" "missing ELF text"
run "${IS[@]}" boot noelf.iso --json
eq "$(echo "$OUT" | jq -c '[.elf_found, .elf_path, .elf_size, has("elf_lba")]')" '[false,null,null,false]' "missing ELF json"
mk ps1.iso '{"files":{"SYSTEM.CNF":"BOOT = cdrom:\\SLUS_000.01;1\r\n"}}'
run "${IS[@]}" boot ps1.iso
eq "$CODE" 2 "PS1 exit 2"
eq "$ERR" "iso: this is a PlayStation 1 disc (SYSTEM.CNF has BOOT, not BOOT2); PS2Recomp ports PS2 games only" "PS1 message"
mk nocnf.iso '{"files":{"README.TXT":"hi"}}'
run "${IS[@]}" boot nocnf.iso
eq "$ERR" "iso: no SYSTEM.CNF on this disc: not a PS2 game disc, or not its data track" "no SYSTEM.CNF on the image"
mkdir -p empty
run "${IS[@]}" boot empty
eq "$CODE" 2 "empty folder exit 2"
eq "$ERR" "iso: no SYSTEM.CNF in empty: is this the extracted disc folder?" "no SYSTEM.CNF in the folder"
run "${IS[@]}" boot nope
eq "$ERR" "iso: no such file or folder: nope" "boot missing"
mkdir -p d1/sub && printf 'boot2 = cdrom0:\\SUB\\game.elf;1\nver=2\n' > d1/system.cnf && printf 'elf' > d1/sub/GAME.ELF
run "${IS[@]}" boot d1
eq "$CODE" 0 "case-insensitive folder lookup exit 0"
eq "$OUT" "BOOT2        cdrom0:\\SUB\\game.elf;1
Boot ELF     game.elf
Serial       (the ELF name is not a serial)
Region       unknown
VER          2
VMODE        -
Found        $TMP/d1/sub/GAME.ELF (3 bytes)" "lowercase keys and names, no serial"
mkdir -p d2 && printf 'BOOT2 = cdrom0:\\SLAA_123.45;1\x85VMODE = PAL\n' > d2/SYSTEM.CNF
run "${IS[@]}" boot d2 --json
eq "$CODE" 1 "d2 ELF missing exit 1"
eq "$(echo "$OUT" | jq -c '[.boot2, .vmode, .ver, .serial, .region]')" '["cdrom0:\\SLAA_123.45;1","PAL",null,"SLAA-12345","unknown region prefix"]' "latin-1 NEL splits lines; unknown prefix"
mkdir -p d3 && printf 'VER = 1\n' > d3/SYSTEM.CNF
run "${IS[@]}" boot d3
eq "$ERR" "iso: SYSTEM.CNF has no BOOT2 line" "no BOOT2"
mkdir -p d4 && printf 'BOOT2 =\nBOOT = cdrom:\\X;1\n' > d4/SYSTEM.CNF
run "${IS[@]}" boot d4
has "$ERR" "PlayStation 1" "an empty BOOT2 with BOOT is PS1"
for pair in 'sces_501.23|SCES-50123|Europe' 'SLPM_650.01|SLPM-65001|Japan' 'SLKA_250.01|SLKA-25001|Korea' 'SCAJ_200.01|SCAJ-20001|Asia' 'SCUS_971.01|SCUS-97101|USA'; do
  IFS='|' read -r NAME SER REG <<< "$pair"
  mkdir -p "r_$NAME" && printf 'BOOT2 = A\nBOOT2 = cdrom0:\\%s;1\n' "$NAME" > "r_$NAME/SYSTEM.CNF"
  run "${IS[@]}" boot "r_$NAME" --json
  eq "$(echo "$OUT" | jq -r '[.serial, .prefix, .region] | join(" ")')" "$SER ${SER:0:4} $REG" "region of $NAME (last BOOT2 wins)"
done

# ---- usage
run "${IS[@]}"
eq "$CODE" 2 "no command exit 2"
has "$ERR" "iso: error: the following arguments are required: COMMAND" "no command message"
run "${IS[@]}" extract g.iso
has "$ERR" "the following arguments are required: --out" "extract needs --out"
run "${IS[@]}" bogus
has "$ERR" "invalid choice: 'bogus'" "bad command"
run "${IS[@]}" extract g.iso --out z --only
eq "$CODE" 2 "--only needs a path"
for c in info ls extract plain boot; do run "${IS[@]}" $c --help; eq "$CODE" 0 "$c --help"; done
run "${IS[@]}" ls --help
has "$OUT" "usage: iso ls [-h] [-r] [--json] image [path]" "ls usage line"

done_ console-reverse/iso

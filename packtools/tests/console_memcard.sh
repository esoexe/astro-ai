#!/usr/bin/env bash
# console-saves/memcard: the CardImage, Export, Psu, BackupRestore, Sync, Helpers and Cli cases of
# console-skill/tests/test_saves.py through the CLI, plus cp932 title decoding checks. Card images, .psu files and
# icon.sys blobs come from fixtures/mkmemcard.js; zips written by the tool are read back with Node's own inflate.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkmemcard.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
M=(console-saves/memcard)
mk() { node "$MK" "$@"; }
j()  { echo "$OUT" | jq -c "$1"; }
hexf() { node "$MK" bytes "$1" 0 999999999; }
wr() { printf '%s' "$2" > "$1"; node "$MK" mtime "$1" $(( $(date +%s) + 5 )); }   # write + mtime 5 s ahead
GAME=$(mk datahex game); BIG=$(mk datahex big)
DSTART=$(mk card card.mcd | jq .data_start); mk card card.ps2 --ecc >/dev/null
fresh() { rm -rf "$TMP/w"; mkdir -p "$TMP/w"; cd "$TMP/w" || exit 1; }
export_game() { run "${M[@]}" export "$TMP/card.mcd" --to "$TMP/w/game/mc0"; eq "$CODE" 0 "setup export rc"; }

# ================================================================ CardImage
eq "$(node -e 'console.log(require("fs").statSync("card.mcd").size)')" 8388608 "plain card is 8 MB"
eq "$(node -e 'console.log(require("fs").statSync("card.ps2").size)')" 8650752 "ECC card size"
for c in card.mcd:false card.ps2:true; do
  f=${c%%:*}; e=${c##*:}
  run "${M[@]}" info "$f" --json; eq "$CODE" 0 "info $f rc"
  eq "$(j .ecc)" "$e" "info $f ecc"; eq "$(j .version)" '"1.2.0.0"' "version"; eq "$(j .clusters_per_card)" 8192 "clusters"
  eq "$(j .alloc_offset)" 41 "alloc_offset"; eq "$(j .alloc_end)" 8135 "alloc_end"; eq "$(j .card_type)" 2 "card_type"
  eq "$(j .usable_kb)" 8135 "usable_kb"; eq "$(j .free_kb)" $((8135 - 15)) "free_kb (root 3, dirs 3+2, files 1+3+1 and 2)"
  eq "$(j .card_flags)" '"0x52"' "card_flags"; eq "$(j .backup_block1)" 1023 "backup1"; eq "$(j .backup_block2)" 1022 "backup2"
  eq "$(j .bytes)" "$( [ "$e" = true ] && echo 8650752 || echo 8388608)" "bytes"
  eq "$(j 'keys_unsorted')" '["card","bytes","ecc","version","page_len","pages_per_cluster","pages_per_block","clusters_per_card","alloc_offset","alloc_end","rootdir_cluster","backup_block1","backup_block2","card_type","card_flags","free_kb","usable_kb"]' "info key order"
done
run "${M[@]}" info card.ps2; eq "$CODE" 0 "info text rc"
has "$OUT" "with ECC" "info text ECC"; has "$OUT" "8 MB" "info text size"
has "$OUT" "card.ps2: PS2 memory card, format 1.2.0.0, 8 MB, with ECC (528-byte pages)" "info line 1"
has "$OUT" "  free 8120 KB of 8135 KB; 512-byte pages, 2 per cluster, allocation from cluster 41" "info line 2"
run "${M[@]}" info card.mcd; has "$OUT" ", no ECC" "plain info text"

for f in card.mcd card.ps2; do
  run "${M[@]}" ls "$f" --json; eq "$CODE" 0 "ls $f rc"
  eq "$(j '[.[].name]')" '["BASLUS-20312GAME","BESLES-50001SAVE"]' "ls names"
  eq "$(j '.[0].title')" '"GAME / セーブ"' "title from icon.sys (cp932 + NFKC)"
  eq "$(j '.[0].serial')" '"SLUS-20312"' "serial"; eq "$(j '.[0].region')" '"USA"' "region"
  eq "$(j '.[0].files')" 3 "files"; eq "$(j '.[0].bytes')" $((964 + 3000 + 100)) "bytes"
  eq "$(j '.[0].modified')" '"2004-05-06 07:08"' "modified"; eq "$(j '.[1].region')" '"Europe"' "region 2"
  eq "$(j '.[1].title')" null "no icon.sys, no title"
done
has "$OUT" '"title": "GAME / セーブ"' "ls --json keeps non-ASCII (ensure_ascii=False)"
eq "$(j '.[0]|keys_unsorted')" '["name","dir","files","bytes","modified","title","serial","region","subfolders","file_list"]' "ls row key order"
eq "$(j '.[1].file_list')" '[{"name":"BIG.DAT","bytes":2048,"mode":"0x8497"},{"name":"EMPTY","bytes":0,"mode":"0x8497"}]' "file_list"
run "${M[@]}" ls card.mcd; eq "$CODE" 0 "ls text rc"
hasnt "$OUT" "OLDSAVE" "deleted entry hidden"; has "$OUT" "2 save(s)" "save count"
has "$OUT" "save folder            files      size  modified         serial      title" "ls header"
has "$OUT" "BASLUS-20312GAME           3        4K  2004-05-06 07:08 SLUS-20312  GAME / セーブ" "ls row"
has "$OUT" "2 save(s). Card times are the PS2 clock (Japan time, UTC+9)." "ls footer"
run "${M[@]}" ls card.mcd --files; eq "$CODE" 0 "ls --files rc"
has "$OUT" "DATA" "lists DATA"; has "$OUT" "0x8497" "lists modes"
has "$OUT" "    DATA                               3000 bytes  mode 0x8497" "file line format"

# same bytes with and without ECC
fresh
run "${M[@]}" export "$TMP/card.mcd" --to a; run "${M[@]}" export "$TMP/card.ps2" --to b
same=1; for f in BASLUS-20312GAME/icon.sys BASLUS-20312GAME/DATA BASLUS-20312GAME/icon.ico BESLES-50001SAVE/BIG.DAT BESLES-50001SAVE/EMPTY; do cmp -s "a/$f" "b/$f" || same=0; done
eq "$same" 1 "card reads the same with and without ECC"

node -e 'require("fs").writeFileSync("bad.ps2", Buffer.alloc(8388608))'
run "${M[@]}" ls bad.ps2; eq "$CODE" 2 "wrong magic rc 2"; has "$ERR" "not a PS2 memory card image" "wrong magic message"
eq "$ERR" "error: bad.ps2 is not a PS2 memory card image (no 'Sony PS2 Memory Card Format' superblock)" "wrong magic full message"
head -c 5000000 "$TMP/card.ps2" > cut.ps2
run "${M[@]}" info cut.ps2; eq "$CODE" 2 "truncated rc 2"; has "$ERR" "truncated" "truncated message"; hasnt "$ERR" "Traceback" "no traceback"
eq "$ERR" "error: cut.ps2 is 5000000 bytes; a card with this superblock is 8388608 bytes (no ECC) or 8650752 (with ECC): truncated or not a raw card dump" "truncated full message"
printf 'Sony PS2 Memory Card Format ' > tiny.ps2
run "${M[@]}" info tiny.ps2; eq "$CODE" 2 "tiny file rc 2"
run "${M[@]}" ls nope.ps2; eq "$CODE" 2 "missing card rc 2"; eq "$ERR" "error: nope.ps2 does not exist" "missing card message"
run "${M[@]}" info nope.ps2; eq "$ERR" "error: cannot read nope.ps2: No such file or directory" "info of a missing card"
mkdir adir; run "${M[@]}" info adir; eq "$ERR" "error: cannot read adir: Is a directory" "info of a folder"

mk patchfat "$TMP/card.mcd" loop.mcd "$DSTART" $((0x80000000 | DSTART))
run "${M[@]}" export loop.mcd --to mc0; eq "$CODE" 1 "FAT loop rc 1"; has "$OUT" "loops" "loop reported"
has "$OUT" "WARNING: skipped BASLUS-20312GAME, nothing written for it: a cluster chain loops back on itself at cluster $DSTART: the card is damaged" "loop warning"
eq "$(ls mc0 | tr '\n' ' ')" "BESLES-50001SAVE " "damaged save skipped, no temp folder left"
rm -rf mc0; mk patchfat "$TMP/card.mcd" free.mcd "$DSTART" $((0x123))
run "${M[@]}" export free.mcd --to mc0; eq "$CODE" 1 "free cluster rc 1"; has "$OUT" "free cluster" "free cluster reported"
rm -rf mc0; mk patchfat "$TMP/card.mcd" short.mcd "$DSTART" 4294967295
run "${M[@]}" ls short.mcd --json; eq "$CODE" 0 "ls reads only the directories"
run "${M[@]}" export short.mcd --to mc0; eq "$CODE" 1 "short chain rc 1"; has "$OUT" "damaged" "short chain reported"
has "$OUT" "a chain ends after 1 clusters but its entry needs 3: the card is damaged" "short chain message"
mk card small.mcd --clusters 1024 >/dev/null
mk poke32 small.mcd noroot.mcd $((41 * 1024 + 4)) 0
run "${M[@]}" ls noroot.mcd; eq "$CODE" 2 "damaged root rc 2"; has "$ERR" "damaged" "damaged root message"
eq "$ERR" "error: the root directory claims 0 entries (at least . and .. are needed): the card is damaged" "damaged root full message"
run "${M[@]}" info small.mcd --json; eq "$CODE" 0 "small card rc"; eq "$(j .clusters_per_card)" 1024 "geometry from the superblock"
run "${M[@]}" ls small.mcd; eq "$CODE" 0 "small card ls"
mk poke32 small.mcd badpage.mcd $((0x28)) $(( (2 << 16) | 100 ))
run "${M[@]}" info badpage.mcd; eq "$ERR" "error: badpage.mcd: implausible page size 100 / pages per cluster 2" "implausible page size"
mk poke32 small.mcd badcl.mcd $((0x30)) 8
run "${M[@]}" info badcl.mcd; eq "$ERR" "error: badcl.mcd: implausible cluster count 8" "implausible cluster count"
mk poke32 small.mcd badalloc.mcd $((0x38)) 5000
run "${M[@]}" info badalloc.mcd; eq "$ERR" "error: badalloc.mcd: allocation area (41 + 5000 clusters) does not fit the card" "allocation area check"

# ================================================================ Export
fresh; export_game
eq "$(ls game/mc0 | tr '\n' ' ')" "BASLUS-20312GAME BESLES-50001SAVE " "mc0 layout"
eq "$(hexf game/mc0/BASLUS-20312GAME/DATA)" "$GAME" "DATA bytes"
eq "$(hexf game/mc0/BESLES-50001SAVE/BIG.DAT)" "$BIG" "BIG.DAT bytes"
eq "$(stat -c %s game/mc0/BESLES-50001SAVE/EMPTY)" 0 "EMPTY is empty"
[ ! -e game/mc0/OLDSAVE ] && ok || bad "deleted save not exported"
eq "$(echo "$OUT" | head -n1)" "exported BASLUS-20312GAME (3 files) -> $TMP/w/game/mc0/BASLUS-20312GAME" "export line"
has "$OUT" "2 save(s) in $TMP/w/game/mc0. The port reads them as mc0:/<folder>/... (slot 2 is the sibling mc1 folder)." "export footer"
eq "$(mk getmtime game/mc0/BASLUS-20312GAME/DATA)" 1083794889 "file time from the card (JST)"
eq "$(mk getmtime game/mc0/BASLUS-20312GAME)" 1083794889 "folder time from the card"
fresh; run "${M[@]}" export "$TMP/card.ps2" --to mc0; eq "$CODE" 0 "export from ECC card"
eq "$(hexf mc0/BASLUS-20312GAME/icon.ico)" "$(printf '01%.0s' {1..100})" "icon.ico from ECC card"
fresh; export_game
printf 'newer progress' > game/mc0/BASLUS-20312GAME/DATA
run "${M[@]}" export "$TMP/card.mcd" --to game/mc0; eq "$CODE" 1 "refuses to overwrite"
has "$ERR" "--force" "mentions --force"
eq "$ERR" "refused: already in game/mc0: BASLUS-20312GAME, BESLES-50001SAVE. Nothing written. Pass --force to replace them (mc0 is backed up first)." "refusal message"
eq "$(cat game/mc0/BASLUS-20312GAME/DATA)" "newer progress" "nothing overwritten"
run "${M[@]}" export "$TMP/card.mcd" --to game/mc0 --force --backups bk; eq "$CODE" 0 "force rc 0"
eq "$(ls bk | wc -l)" 1 "one backup zip"
Z=$(ls bk/*.zip)
eq "$(mk zipread "$Z" BASLUS-20312GAME/DATA)" "$(printf 'newer progress' | od -v -An -tx1 | tr -d ' \n')" "backup holds the old DATA"
has "$OUT" "backed up game/mc0 to $Z before replacing 2 save(s)" "backup line"
eq "$(hexf game/mc0/BASLUS-20312GAME/DATA)" "$GAME" "replaced with the card's DATA"
fresh; run "${M[@]}" export "$TMP/card.mcd" --to mc0 --only BESLES-50001SAVE; eq "$CODE" 0 "--only rc"
eq "$(ls mc0)" "BESLES-50001SAVE" "--only one save"
run "${M[@]}" export "$TMP/card.mcd" --to mc1 --only NOPE ZED; eq "$CODE" 2 "--only unknown rc 2"; has "$ERR" "NOPE" "names the missing save"
eq "$ERR" "error: not on the card: NOPE, ZED" "missing saves sorted"
fresh; mkdir -p Mcd001.ps2/BASLUS-20312GAME
printf x > Mcd001.ps2/_pcsx2_superblock; printf y > Mcd001.ps2/BASLUS-20312GAME/_pcsx2_index; printf 'save bytes' > Mcd001.ps2/BASLUS-20312GAME/DATA
run "${M[@]}" export Mcd001.ps2 --to mc0; eq "$CODE" 0 "PCSX2 folder card rc"
eq "$(ls -A mc0/BASLUS-20312GAME)" "DATA" "PCSX2 metadata skipped"; eq "$(cat mc0/BASLUS-20312GAME/DATA)" "save bytes" "PCSX2 data"
fresh; mkdir mc0; run "${M[@]}" export mc0 --to mc0; eq "$CODE" 2 "export to itself rc 2"
eq "$ERR" "error: the source and --to are the same folder" "export to itself message"
fresh; export_game
run "${M[@]}" ls game/mc0 --json; eq "$CODE" 0 "ls of mc0"
eq "$(j '.[0].title')" '"GAME / セーブ"' "mc0 title"; eq "$(j '.[1].files')" 2 "mc0 file count"
eq "$(j '.[0].modified')" '"2004-05-06 07:08"' "mc0 times read back as card times"
fresh; mk card "$TMP/odd.mcd" --clusters 1024 --odd >/dev/null
run "${M[@]}" export "$TMP/odd.mcd" --to mc0; eq "$CODE" 1 "unsafe names rc 1"
has "$OUT" "WARNING" "warning printed"; eq "$(ls mc0/BASLUS-20312GAME)" "OK" "only the safe file"
has "$OUT" "WARNING: skipped BASLUS-20312GAME/b'A:B': name b'A:B' has characters a host folder cannot hold" "unsafe name warning (bytes repr)"

# ================================================================ Psu
fresh; export_game
run "${M[@]}" psu export game/mc0/BASLUS-20312GAME --out s.psu; eq "$CODE" 0 "psu export rc"
eq "$OUT" "wrote s.psu: BASLUS-20312GAME, 3 file(s), 8192 bytes" "psu export line"
eq "$(stat -c %s s.psu)" $((512 * 6 + 1024 + 3072 + 1024)) "psu size, data padded to 1024"
eq "$(mk bytes s.psu 0 2)" "2784" "header mode 0x8427"
eq "$(mk bytes s.psu 4 8)" "05000000" "three files plus . and .."
eq "$(mk bytes s.psu 16 20)" "00000000" "cluster field is 0"
eq "$(mk bytes s.psu 64 80)" "$(printf 'BASLUS-20312GAME' | od -v -An -tx1 | tr -d ' \n')" "folder name"
eq "$(mk bytes s.psu $((512 + 64)) $((512 + 66)))" "2e00" "dot entry"
eq "$(mk bytes s.psu $((1024 + 64)) $((1024 + 67)))" "2e2e00" "dotdot entry"
eq "$(mk bytes s.psu 1536 1538)" "9784" "first file mode 0x8497"
eq "$(mk bytes s.psu 8 16)" "000908070605d407" "created time packed (JST card time from the mtime)"
run "${M[@]}" psu import s.psu --to mc0b; eq "$CODE" 0 "psu import rc"
eq "$OUT" "imported BASLUS-20312GAME (3 file(s)) -> mc0b/BASLUS-20312GAME" "psu import line"
eq "$(hexf mc0b/BASLUS-20312GAME/DATA)" "$GAME" "round trip DATA"
cmp -s mc0b/BASLUS-20312GAME/icon.sys game/mc0/BASLUS-20312GAME/icon.sys && ok || bad "round trip icon.sys"
eq "$(hexf mc0b/BASLUS-20312GAME/icon.ico)" "$(printf '01%.0s' {1..100})" "round trip icon.ico"
eq "$(mk getmtime mc0b/BASLUS-20312GAME/DATA)" 1083794889 "import keeps the time"
run "${M[@]}" psu export "$TMP/card.ps2" --save BESLES-50001SAVE --out s2.psu; eq "$CODE" 0 "psu from a card image"
run "${M[@]}" psu ls s2.psu --json; eq "$CODE" 0 "psu ls rc"
eq "$(j .folder)" '"BESLES-50001SAVE"' "psu folder"; eq "$(j '[.files[].name]')" '["BIG.DAT","EMPTY"]' "psu files"
eq "$(j .modified)" '"2005-12-31 23:59"' "psu modified"; eq "$(j 'keys_unsorted')" '["folder","modified","title","files"]' "psu ls key order"
eq "$(j .files[0])" '{"name":"BIG.DAT","bytes":2048,"mode":"0x8497"}' "psu file row"
run "${M[@]}" psu ls s.psu; eq "$(echo "$OUT" | head -n1)" "BASLUS-20312GAME  (2004-05-06 07:08)  GAME / セーブ" "psu ls text head"
has "$OUT" "    DATA                               3000 bytes" "psu ls text file"
run "${M[@]}" psu export "$TMP/card.mcd" --out x.psu; eq "$CODE" 2 "card needs --save"; has "$ERR" "--save" "--save message"
run "${M[@]}" psu export "$TMP/card.mcd" --save NOPE --out x.psu; eq "$ERR" "error: NOPE is not on $TMP/card.mcd" "unknown --save"
run "${M[@]}" psu export game/mc0/BESLES-50001SAVE; eq "$CODE" 0 "default psu path"
[ -f console/saves/BESLES-50001SAVE.psu ] && ok || bad "console/saves/BESLES-50001SAVE.psu written"
run "${M[@]}" psu export game/mc0/BESLES-50001SAVE; eq "$CODE" 2 "no overwrite"
eq "$ERR" "error: console/saves/BESLES-50001SAVE.psu exists (pass --force to overwrite)" "no overwrite message"
run "${M[@]}" psu export game/mc0/BESLES-50001SAVE/ --force; eq "$CODE" 0 "--force and a trailing slash"
mkdir -p sub/SAVE/SUB; run "${M[@]}" psu export sub/SAVE --out x.psu; eq "$CODE" 2 "subfolder rc 2"; has "$ERR" "subfolder" "subfolder message"
mk psu t.psu SAVE - "A=$(node -e 'console.log("78".repeat(3000))')"; head -c 2100 t.psu > tt.psu
run "${M[@]}" psu import tt.psu --to mc0; eq "$CODE" 2 "truncated psu rc 2"; has "$ERR" "truncated" "truncated psu message"
eq "$ERR" "error: tt.psu is truncated inside b'A' (3000 bytes expected, 52 present)" "truncated psu full message"
mk psu t3.psu SAVE 3 A=616263
run "${M[@]}" psu import t3.psu --to mc0; eq "$CODE" 2 "missing header rc 2"
eq "$ERR" "error: t3.psu is truncated: header 2 of 3 is missing" "missing header message"
for spec in "..:A" "SAVE:../../evil" "a/b:A"; do
  mk psu trick.psu "${spec%%:*}" - "${spec##*:}=31"
  run "${M[@]}" psu import trick.psu --to mc0; eq "$CODE" 2 "path trick $spec refused"
done
[ ! -e evil ] && [ ! -e ../evil ] && ok || bad "no file escaped"
node -e 'require("fs").writeFileSync("x.psu", Buffer.alloc(4096))'
run "${M[@]}" psu ls x.psu; eq "$CODE" 2 "not a psu"; eq "$ERR" "error: x.psu is not a .psu save (the first three headers must be folders)" "not a psu message"
printf 'short' > y.psu; run "${M[@]}" psu ls y.psu; eq "$ERR" "error: y.psu is too short to be a .psu" "short psu"
run "${M[@]}" psu ls none.psu; eq "$ERR" "error: cannot read none.psu: No such file or directory" "missing psu"
rm -rf mc0; mk psu p.psu SAVE - A=616263
run "${M[@]}" psu import p.psu --to mc0; eq "$CODE" 0 "first import"
printf mine > mc0/SAVE/A
run "${M[@]}" psu import p.psu --to mc0; eq "$CODE" 1 "existing needs --force"; eq "$(cat mc0/SAVE/A)" mine "kept"
eq "$ERR" "refused: mc0/SAVE already exists. Nothing written. Pass --force to replace it (mc0 is backed up first)." "psu refusal"
run "${M[@]}" psu import p.psu --to mc0 --force --backups bk; eq "$CODE" 0 "--force rc 0"
eq "$(cat mc0/SAVE/A)" abc "replaced"; eq "$(ls bk | wc -l)" 1 "backed up first"
has "$OUT" "backed up mc0 to bk/" "psu backup line"
mk psu case.psu SAVE - A=31 a=32
run "${M[@]}" psu import case.psu --to mc9; eq "$ERR" "error: case.psu has two files whose names differ only in case" "case clash"

# ================================================================ BackupRestore
fresh; export_game; MC=game/mc0
run "${M[@]}" backup "$MC" --to bk; eq "$CODE" 0 "backup rc"
NAME=$(ls bk); [[ "$NAME" =~ ^game-mc0-[0-9]{8}T[0-9]{6}Z\.zip$ ]] && ok || bad "backup name $NAME"
eq "$(mk zipread "bk/$NAME" | jq -c sort)" '["BASLUS-20312GAME/DATA","BASLUS-20312GAME/icon.ico","BASLUS-20312GAME/icon.sys","BESLES-50001SAVE/BIG.DAT","BESLES-50001SAVE/EMPTY"]' "zip holds every file"
eq "$(mk zipread "bk/$NAME" BESLES-50001SAVE/BIG.DAT)" "$BIG" "zip member bytes (Node inflate)"
eq "$(mk zipread "bk/$NAME" BESLES-50001SAVE/EMPTY)" "" "empty member"
eq "$OUT" "backed up $MC (5 files) to bk/$NAME; keeping the newest 20" "backup line"
fresh; export_game; MC=game/mc0; mkdir bk
mk zip bk/othergame-mc0-20200101T000000Z.zip
for _ in 1 2 3 4; do run "${M[@]}" backup "$MC" --to bk --keep 2; eq "$CODE" 0 "backup --keep 2"; done
eq "$(ls bk | grep -c '^game-mc0-')" 2 "keeps the newest 2"
[ -e bk/othergame-mc0-20200101T000000Z.zip ] && ok || bad "other card's backup untouched"
run "${M[@]}" backup "$MC" --to bk --label "my card+1" --keep 0; eq "$CODE" 0 "custom label"
ls bk | grep -q '^my card+1-' && ok || bad "label used as given"
run "${M[@]}" backup "$MC" --to "$MC/bk"; eq "$CODE" 2 "backup inside refused"; has "$ERR" "outside" "outside message"
run "${M[@]}" backup nope; eq "$CODE" 2 "backup of missing folder"; eq "$ERR" "error: nope is not a folder (point at the port's mc0 folder)" "missing folder message"
fresh; export_game; MC=game/mc0
run "${M[@]}" backup "$MC" --to bk; Z=$(ls bk/*.zip)
run "${M[@]}" restore "$Z" --to mc0new; eq "$CODE" 0 "restore into an empty folder"
eq "$(hexf mc0new/BASLUS-20312GAME/DATA)" "$GAME" "restored DATA"
eq "$OUT" "restored 5 file(s) from $Z into mc0new" "restore line"
eq "$(mk getmtime mc0new/BASLUS-20312GAME/DATA)" 1083794888 "restore keeps the times (zip: 2 s precision, 89 -> 88)"
printf later > $MC/BASLUS-20312GAME/DATA
run "${M[@]}" restore "$Z" --to $MC; eq "$CODE" 1 "restore over existing needs --force"; eq "$(cat $MC/BASLUS-20312GAME/DATA)" later "kept"
eq "$ERR" "refused: $MC is not empty. Nothing written. Pass --force to replace its contents (it is backed up first)." "restore refusal"
run "${M[@]}" restore "$Z" --to $MC --force --backups bk2; eq "$CODE" 0 "restore --force"
eq "$(hexf $MC/BASLUS-20312GAME/DATA)" "$GAME" "restored over"
B2=$(ls bk2/*.zip); eq "$(mk zipread "$B2" BASLUS-20312GAME/DATA)" "$(printf later | od -v -An -tx1 | tr -d ' \n')" "what was there is backed up"
has "$OUT" "backed up the current $MC to $B2" "restore backup line"
mk zip evil.zip "../outside.txt=x"
run "${M[@]}" restore evil.zip --to mc0x; eq "$CODE" 2 "zip slip refused"; has "$ERR" "unsafe" "unsafe message"
eq "$ERR" "error: backup contains an unsafe path '../outside.txt'; refusing to restore" "zip slip message"
[ ! -e outside.txt ] && [ ! -e mc0x ] && ok || bad "nothing written for zip slip"
for nm in "/abs.txt" "C:/x.txt" 'a\..\b.txt'; do mk zip e2.zip "$nm=x"; run "${M[@]}" restore e2.zip --to m2; eq "$CODE" 2 "unsafe $nm"; done
printf nope > bad.zip; run "${M[@]}" restore bad.zip --to m; eq "$CODE" 2 "not a zip"
eq "$ERR" "error: bad.zip is not a readable zip: File is not a zip file" "not a zip message"
run "${M[@]}" restore none.zip --to m; eq "$ERR" "error: none.zip is not a readable zip: [Errno 2] No such file or directory: 'none.zip'" "missing zip"

# ================================================================ Sync
sync_setup() { fresh; export_game; MC=$TMP/w/game/mc0; CL=$TMP/w/iCloud/console-saves/SLUS-20312; mkdir -p "$(dirname "$CL")"; }
sy() { run "${M[@]}" sync "${1:-$MC}" --cloud "$CL" --backups "$TMP/w/bk" "${@:2}"; }
sync_setup
sy; eq "$CODE" 0 "first sync rc"; has "$OUT" "5 pushed" "pushes everything"
eq "$(hexf "$CL/BASLUS-20312GAME/DATA")" "$GAME" "cloud DATA"
[ -f game/.console-sync.json ] && ok || bad "manifest next to mc0"
ls -A "$MC" | grep -q '^\.console' && bad "nothing .console inside mc0" || ok
has "$OUT" "push       BASLUS-20312GAME/DATA  (new here)" "plan line"
M1=$(cat game/.console-sync.json)
eq "$(echo "$M1" | jq -c 'keys')" "[\"$MC -> $CL\"]" "manifest key"
eq "$(echo "$M1" | jq -c '.[] | .files["BESLES-50001SAVE/EMPTY"]')" '{"sha":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","size":0}' "manifest entry (sha256 of nothing)"
eq "$(echo "$M1" | jq -r '.[] | .last_sync' | grep -cE '^[0-9]{8}T[0-9]{6}Z$')" 1 "last_sync stamp"
eq "$(echo "$M1" | head -n3 | tail -n1)" '  "device": '"$(echo "$M1" | jq -c '.[] | .device')"',' "indent 1, sorted keys"
sy; eq "$CODE" 0 "second sync rc"; has "$OUT" "0 pushed, 0 pulled, 5 already the same" "nothing to do"
sy "$MC" --verbose; has "$OUT" "same       BASLUS-20312GAME/DATA  (identical)" "--verbose lists same files"
mkdir -p deck/mc0; sy "$TMP/w/deck/mc0"; eq "$CODE" 0 "second device rc"; has "$OUT" "5 pulled" "second device pulls"
eq "$(hexf deck/mc0/BESLES-50001SAVE/BIG.DAT)" "$BIG" "pulled bytes"
hasnt "$OUT" "backed up first" "empty mc0 is not backed up"

sync_setup; sy
wr "$MC/BASLUS-20312GAME/DATA" "level 2"
sy; eq "$CODE" 0 "one side changed rc"; has "$OUT" "1 pushed" "push the change"; eq "$(cat "$CL/BASLUS-20312GAME/DATA")" "level 2" "cloud updated"
has "$OUT" "push       BASLUS-20312GAME/DATA  (changed here since the last sync)" "changed here reason"
wr "$CL/BASLUS-20312GAME/DATA" "level 3 from the deck"
sy; eq "$CODE" 0 "cloud changed rc"; has "$OUT" "1 pulled" "pull the change"; eq "$(cat "$MC/BASLUS-20312GAME/DATA")" "level 3 from the deck" "mc0 updated"
[ -n "$(ls bk)" ] && ok || bad "mc0 backed up before a pull"
has "$OUT" "mc0 was backed up first: $TMP/w/bk/game-mc0-" "backup line"

sync_setup; sy
wr "$MC/BASLUS-20312GAME/DATA" "played here"; wr "$CL/BASLUS-20312GAME/DATA" "played on the deck"
sy; eq "$CODE" 1 "conflict rc 1"; has "$OUT" "1 conflict" "conflict counted"
eq "$(cat "$MC/BASLUS-20312GAME/DATA")" "played here" "local kept"; eq "$(cat "$CL/BASLUS-20312GAME/DATA")" "played on the deck" "cloud kept"
CD=$(ls "$CL/.console-conflicts"); eq "$(echo "$CD" | wc -l)" 1 "one conflict folder"
eq "$(cat "$CL/.console-conflicts/$CD/here/BASLUS-20312GAME/DATA")" "played here" "here copy"
eq "$(cat "$CL/.console-conflicts/$CD/cloud/BASLUS-20312GAME/DATA")" "played on the deck" "cloud copy"
has "$OUT" "both versions of every conflict are in $CL/.console-conflicts/$CD (here/ and cloud/)" "conflict copies line"
has "$OUT" "conflict   BASLUS-20312GAME/DATA  (changed on both sides since the last sync)" "conflict reason"
sy; eq "$CODE" 1 "still a conflict"
sy "$MC" --prefer local; eq "$CODE" 0 "--prefer local settles it"; eq "$(cat "$CL/BASLUS-20312GAME/DATA")" "played here" "local won"
has "$OUT" "push-keep  BASLUS-20312GAME/DATA  (changed on both sides since the last sync; --prefer local)" "push-keep reason"
sy; eq "$CODE" 0 "settled"

sync_setup; mkdir -p "$CL/BASLUS-20312GAME"; printf "someone else's progress" > "$CL/BASLUS-20312GAME/DATA"
sy; eq "$CODE" 1 "never synced and different"; has "$OUT" "never synced" "never synced reason"
eq "$(hexf "$MC/BASLUS-20312GAME/DATA")" "$GAME" "local untouched"

sync_setup; sy
wr "$MC/BASLUS-20312GAME/DATA" here; wr "$CL/BASLUS-20312GAME/DATA" cloud; mk mtime "$CL/BASLUS-20312GAME/DATA" $(( $(date +%s) + 100 ))
sy "$MC" --prefer newest; eq "$CODE" 0 "--prefer newest"; eq "$(cat "$MC/BASLUS-20312GAME/DATA")" cloud "newest (cloud) won"
wr "$MC/BASLUS-20312GAME/DATA" "here again"; wr "$CL/BASLUS-20312GAME/DATA" "cloud again"
sy "$MC" --prefer cloud; eq "$CODE" 0 "--prefer cloud"; eq "$(cat "$MC/BASLUS-20312GAME/DATA")" "cloud again" "cloud won"

sync_setup; sy
wr "$CL/BASLUS-20312GAME/DATA" "cloud change"
sy "$MC" --push; eq "$CODE" 0 "--push rc"; has "$OUT" "1 skipped" "pull skipped"
has "$OUT" "(changed in the cloud since the last sync (run without --push to bring it here))" "skip reason"
eq "$(hexf "$MC/BASLUS-20312GAME/DATA")" "$GAME" "--push never pulls"
sy "$MC" --pull; has "$OUT" "1 pulled" "--pull pulls"
sy "$MC" --push --pull; eq "$CODE" 2 "--push and --pull together"; has "$ERR" "not allowed with argument" "exclusive group message"

sync_setup
sy "$MC" --dry-run; eq "$CODE" 0 "dry run rc"; has "$OUT" "dry run: 5 pushed" "dry run summary"
[ ! -e "$CL" ] && ok || bad "dry run creates no cloud folder"; [ ! -e game/.console-sync.json ] && ok || bad "dry run writes no manifest"
sy "$MC" --dry-run --json; eq "$(j '[.[].action]|unique')" '["push"]' "dry run json actions"
eq "$(j '.[0]')" '{"path":"BASLUS-20312GAME/DATA","action":"push","reason":"new here"}' "dry run json row"

sync_setup; mkdir -p "$CL/BASLUS-20312GAME"; printf bplist > "$CL/BASLUS-20312GAME/.DATA.icloud"
sy; eq "$CODE" 1 "placeholders block"; has "$ERR" "placeholder" "placeholder message"
eq "$ERR" "refused: 1 file(s) are cloud placeholders not downloaded yet (BASLUS-20312GAME/.DATA.icloud). Download the folder first (Finder: Download Now, or keep it downloaded), then sync." "placeholder full message"
[ ! -e "$CL/BASLUS-20312GAME/DATA" ] && ok || bad "nothing pushed"

sync_setup; sy; rm "$CL/BASLUS-20312GAME/icon.ico"
sy; eq "$CODE" 0 "deleted file rc"; has "$OUT" "sync never deletes" "never deletes"; [ -e "$CL/BASLUS-20312GAME/icon.ico" ] && ok || bad "file came back"
run "${M[@]}" sync "$MC" --cloud "$MC/cloud"; eq "$CODE" 2 "nested refused"; eq "$ERR" "error: the cloud folder and mc0 must not contain each other" "nested message"
sync_setup; printf x > "$CL"; sy; eq "$CODE" 2 "cloud is a file"; eq "$ERR" "error: $CL is a file, not a folder" "cloud file message"
run "${M[@]}" sync "$MC" --cloud "$TMP/w/no/such/place"; eq "$CODE" 2 "cloud parent must exist"; has "$ERR" "check the cloud path" "parent message"
run "${M[@]}" sync nope --cloud x; eq "$ERR" "error: nope is not a folder (point at the port's mc0 folder)" "sync of missing mc0"
sync_setup; sy "$MC" --json; eq "$CODE" 0 "json summary rc"; eq "$(j .pushed)" 5 "json pushed"; eq "$(j '.plan|length')" 5 "json plan"
eq "$(j 'keys_unsorted')" '["pushed","pulled","same","conflicts","skipped","backup","dry_run","conflict_copies","plan"]' "summary key order"
eq "$(j '[.backup,.dry_run,.conflict_copies]')" '[null,false,null]' "summary nulls"
printf '{bad' > game/.console-sync.json; sy; eq "$CODE" 2 "bad manifest"; eq "$ERR" "error: $TMP/w/game/.console-sync.json is not valid JSON; move it aside to start fresh" "bad manifest message"
sy "$MC" --manifest "$TMP/w/state.json"; eq "$CODE" 0 "--manifest elsewhere"; [ -f state.json ] && ok || bad "custom manifest written"

# ================================================================ Helpers (through folder sources and psu files)
fresh; mkdir -p mc0/EPOCH mc0/BISLPS-25051SAVE mc0/MYSAVE mc0/NOMAGIC mc0/SHORT
mk mtime mc0/EPOCH 0
node -e 'require("fs").writeFileSync("mc0/NOMAGIC/icon.sys", Buffer.alloc(964))'; printf PS2D > mc0/SHORT/icon.sys
run "${M[@]}" ls mc0 --json; eq "$CODE" 0 "helpers ls rc"
eq "$(j '.[] | select(.name=="EPOCH") | .modified')" '"1970-01-01 09:00"' "epoch 0 is 09:00 Japan time"
eq "$(j '.[] | select(.name=="BISLPS-25051SAVE") | [.serial,.region]')" '["SLPS-25051","Japan/Asia"]' "Japanese serial"
eq "$(j '.[] | select(.name=="MYSAVE") | [.serial,.region]')" '[null,null]' "no serial"
eq "$(j '.[] | select(.name=="NOMAGIC") | .title')" null "icon.sys needs PS2D"
eq "$(j '.[] | select(.name=="SHORT") | .title')" null "icon.sys needs 0x104 bytes"
eq "$(j '[.[].name]')" '["BISLPS-25051SAVE","EPOCH","MYSAVE","NOMAGIC","SHORT"]' "sorted by lower-case name"
for bad in "tab	here" "$(printf 'x%.0s' {1..40})" 'a\b'; do
  mk psu hn.psu "$bad" - A=31; run "${M[@]}" psu import hn.psu --to hn; eq "$CODE" 2 "host name rule: $bad"
done
eq "$ERR" "error: name b'a\\\\b' has characters a host folder cannot hold" "bytes repr doubles a backslash"
mk psu hn2.psu "$(printf 'x%.0s' {1..32})" - A=31; run "${M[@]}" psu import hn2.psu --to hn
eq "$ERR" "error: name b'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' is longer than 31 bytes" "long name (cut at 32 by the dirent)"

# ---- cp932 titles: Python's decoder rules, checked against Node's WHATWG Shift_JIS where the two agree
fresh; mkdir mc0
t_icon() {   # t_icon <name> <hex> <expected title JSON> [break]
  mkdir -p "mc0/$1"; mk iconraw "mc0/$1/icon.sys" "$2" $4
  run "${M[@]}" ls mc0 --json; eq "$(j ".[] | select(.name==\"$1\") | .title")" "$3" "cp932 $1"
}
nfkc() { node -e 'const s=JSON.parse(process.argv[1]).normalize("NFKC").trim();console.log(JSON.stringify(s))' "$1"; }
t_icon KANA "b6c0b6c5" '"カタカナ"'                                  # half-width katakana, NFKC to full width
t_icon KANJI "8abf8e9a" "$(nfkc "$(mk decode 8abf8e9a)")"
t_icon KANJI2 "93fa967b8cea" '"日本語"'
t_icon NEC13 "87408741" '"12"'                                       # NEC row 13 circled digits
t_icon IBMEXT "fa40fa41" '"iii"'                                     # IBM small roman numerals (NFKC "i" + "ii")
t_icon NECIBM "ed40" "$(nfkc "$(mk decode ed40)")"
t_icon USER "f040f9fc" "$(jq -cn '"\ue000\ue757"')"                              # user-defined area -> private use
t_icon BADTRAIL "8540" '"�@"'                                        # unmapped code: the lead byte only is replaced
t_icon BADLEAD "859f41" "\"�$(nfkc "$(mk decode 9f41)" | jq -r .)\""   # Python re-reads 0x9F (WHATWG would not)
t_icon LONE "414182" '"AA�"'                                          # incomplete sequence at the end
t_icon SINGLE "a0fdfeff" "$(jq -cn '"\uf8f0\uf8f1\uf8f2\uf8f3"')"                # Windows compatibility single bytes
t_icon ASCII5C "5c7e" '"\\~"'                                        # 0x5C and 0x7E stay ASCII in cp932
t_icon WAVE "8160815f817c" '"~\\-"'                                # CP932's U+FF5E U+FF3C U+FF0D, folded by NFKC
t_icon TWOLINE "82668260826c8264835a815b8375" '"GAME / セーブ"' 8
t_icon BIGBREAK "41424344" '"ABCD"' 300                              # a break past the end is clamped
t_icon SPACES "81408140418140" '"A"'                                 # ideographic spaces stripped after NFKC
t_icon ONLYSPACE "8140" 'null'
t_icon NONASCIITRAIL "84bf" '"�ソ"'                                  # unmapped, non-ASCII trail: Python re-reads 0xBF as ｿ
for code in 889f 9ffc e040 eaa4 fb40 fc4b 8199 81f0 84be 8794; do
  t_icon "C$code" "$code" "$(nfkc "$(mk decode "$code")")"
done

# ================================================================ Cli
run "${M[@]}" --help; eq "$CODE" 0 "--help rc"; has "$OUT" "usage" "usage shown"
has "$OUT" "usage: memcard [-h] command ..." "usage line"
has "$OUT" "    psu       .psu single-save files: export, import, ls" "subcommand list"
has "$OUT" "Card images are read only." "epilog"
for argv in "info" "ls" "export" "psu" "psu export" "psu import" "psu ls" "backup" "restore" "sync"; do
  # shellcheck disable=SC2086
  run "${M[@]}" $argv --help; eq "$CODE" 0 "$argv --help rc"; has "$OUT" "usage" "$argv usage"
done
run "${M[@]}" psu --help; has "$OUT" "usage: memcard psu [-h] action ..." "psu usage"; has "$OUT" "    import    a .psu into mc0/<SAVE>/" "psu actions"
run "${M[@]}" sync x; eq "$CODE" 2 "usage error rc 2"; has "$ERR" "the following arguments are required: --cloud" "missing --cloud"
run "${M[@]}" psu; eq "$CODE" 2 "psu needs an action"
run "${M[@]}" sync x --cloud y --prefer bogus; eq "$CODE" 2 "bad --prefer"; has "$ERR" "invalid choice" "choices message"
run "${M[@]}" backup x --keep two; eq "$CODE" 2 "bad --keep"
done_ console-saves/memcard

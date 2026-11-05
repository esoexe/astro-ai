#!/usr/bin/env bash
# modder/formats: C++ ports of the reusable file-format tools in universal-modder's examples (Rehan and
# universal-modder contributors, MIT): um sld (examples/aoe2-de-civ/sld.py), um vpk and um vtf
# (examples/outer-wilds-portal-gun/tools/vpk.py, vtf.py). Fixtures come from fixtures/modder/gen_fixtures.js
# (hand-assembled blocks with known colours), PNGs are read back with png_stats.js. No Python.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export UM_HOME="$TMP/umh" HOME="$TMP/home"; mkdir -p "$HOME"
u() { run um "$@"; }
node "$FIX/modder/gen_fixtures.js" fx || { echo "modder/formats: fixture generator failed"; exit 1; }
px() { node "$FIX/modder/png_stats.js" "$1" "$2" | jq -c ".px[\"$2\"]"; }

# ================================================================ sld
u sld --help
eq "$CODE" 0 "sld help"; has "$OUT" "flags & 0x80: blocks not drawn this frame are copied" "format notes"
u sld info fx/unit.sld
eq "$CODE" 0 "sld info"
eq "$(sed -n 1p <<<"$OUT")" "fx/unit.sld: 160 bytes, version 4, 2 frames, header 0x10 ff000000" "header line"
eq "$(sed -n 2p <<<"$OUT")" "  canvas 8x8, hotspot (4, 6): 2 frames" "canvas line"
eq "$(sed -n 3p <<<"$OUT")" "  layer main   in 2 frames, 3 blocks, 1 frames reuse blocks of the previous one (flag 0x80)" "main layer line"
eq "$(sed -n 5p <<<"$OUT")" "  layer 0x04   in 1 frames, 0 blocks, 0 frames reuse blocks of the previous one (flag 0x80)" "0x04 layer line"
eq "$(sed -n 6p <<<"$OUT")" "  layer player in 1 frames, 1 blocks, 0 frames reuse blocks of the previous one (flag 0x80)" "player layer line"
u sld to-png fx/unit.sld out/ --layers
eq "$OUT" "wrote 2 frames to out" "to-png summary (pathlib-style folder)"
eq "$(LC_ALL=C ls out | tr '\n' ' ')" "unit_0000.png unit_0000_main.png unit_0000_player.png unit_0000_shadow.png unit_0001.png unit_0001_main.png " "files"
eq "$(px out/unit_0000_main.png 0,0)" "[255,0,0,255]" "BC1 index 0 (4-colour)"
eq "$(px out/unit_0000_main.png 5,5)" "[0,0,255,255]" "BC1 index 1, skip command"
eq "$(px out/unit_0000_main.png 4,0)" "[0,0,0,0]" "skipped block is empty"
eq "$(px out/unit_0000.png 0,0)" "[29,59,152,255]" "preview: player colour tint (blue player)"
eq "$(px out/unit_0000.png 6,6)" "[0,0,255,255]" "preview: sprite over its shadow"
eq "$(px out/unit_0000_shadow.png 6,6)" "[200,200,200,255]" "BC4 shadow"
eq "$(px out/unit_0000_player.png 1,1)" "[255,255,255,255]" "player mask uses the main box"
eq "$(px out/unit_0001.png 0,0)" "[255,0,0,255]" "flag 0x80 keeps the previous frame's block"
eq "$(px out/unit_0001.png 4,0)" "[0,255,0,255]" "and draws the new one"
u sld roundtrip fx/unit.sld
eq "$(sed -n 1p <<<"$OUT")" "roundtrip fx/unit.sld: 2 frames, 160 -> 184 bytes (no inter-frame reuse)" "roundtrip sizes"
eq "$(sed -n 2p <<<"$OUT")" "  main   RGB mean abs error 0.00/255 over 80 opaque pixels, 0 pixels changed opacity" "flat blocks are lossless"
has "$OUT" "  shadow mean abs error 0.00/255" "shadow roundtrip"
u sld write w.sld fx/sp_0.png fx/sp_1.png --hotspot 6,7 --shadows --player-suffix _p
eq "$CODE" 0 "sld write"
eq "$OUT" "wrote w.sld: 2 frames, 332 bytes, hotspot (6, 7), 1 with a shadow, 2 with a player-colour mask" "write summary"
u sld info w.sld
has "$OUT" "canvas 12x8, hotspot (6, 7): 2 frames" "written canvas + hotspot"
has "$OUT" "layer shadow in 1 frames, 3 blocks" "shadow from <frame>_s.png"
has "$OUT" "layer 0x04   in 2 frames, 0 blocks" "empty 0x04 layer like the stock files"
has "$OUT" "layer player in 2 frames, 8 blocks" "player blocks only where the sprite is drawn"
u sld roundtrip w.sld
has "$OUT" "332 -> 332 bytes" "write is deterministic"
E=$(sed -n 's/.*RGB mean abs error \([0-9.]*\)\/255.*/\1/p' <<<"$OUT")
awk -v e="$E" 'BEGIN{exit !(e != "" && e + 0 < 2.0)}' && ok || bad "BC1 encoder error on a gradient ($E/255)"
has "$OUT" "0 pixels changed opacity" "alpha kept"
u sld to-png w.sld wo
eq "$(px wo/w_0000.png 0,0)" "[0,0,0,0]" "outside the box stays transparent"
u sld write o.sld fx/odd.png --hotspot 1,1
eq "$CODE" 1 "odd canvas"; has "$ERR" "frame 0: canvas 10x7 is not a multiple of 4 - pad it" "odd canvas message"
u sld write o.sld fx/odd.png --hotspot 1,1 --pad
eq "$CODE" 0 "--pad"; has "$(run um sld info o.sld; echo "$OUT")" "canvas 12x8" "padded canvas"
u sld write o.sld fx/odd.png --hotspot 1
eq "$CODE" 1 "bad hotspot"
u sld info fx/unit.glb
eq "$CODE" 1 "not an SLD"; has "$ERR" "not an SLD file" "not an SLD message"
head -c 60 fx/unit.sld > cut.sld
u sld info cut.sld
eq "$CODE" 1 "truncated"; has "$ERR" "truncated SLD" "truncated message"

# ================================================================ vtf
u vtf info fx/dxt1.vtf
eq "$OUT" "fx/dxt1.vtf: VTF 7.2, 4x4, DXT1 (13), 1 mips, 1 frames" "vtf info"
u vtf to-png fx/dxt1.vtf d1.png
eq "$OUT" "wrote d1.png 4x4" "vtf to-png"
eq "$(px d1.png 0,0)$(px d1.png 1,0)$(px d1.png 2,0)$(px d1.png 3,0)" "[255,0,0,255][0,0,255,255][170,0,85,255][85,0,170,255]" "DXT1 palette"
u vtf to-png fx/dxt5.vtf d5.png
eq "$(px d5.png 0,0)$(px d5.png 1,0)$(px d5.png 2,0)" "[0,255,0,255][0,255,0,0][0,255,0,218]" "DXT5 interpolated alpha, 4-colour colour block"
u vtf info fx/bgra.vtf
eq "$OUT" "fx/bgra.vtf: VTF 7.3, 2x2, BGRA8888 (12), 2 mips, 2 frames" "7.3 info"
u vtf to-png fx/bgra.vtf b0.png
eq "$(px b0.png 0,0)$(px b0.png 1,1)" "[30,20,10,40][150,140,130,160]" "7.3 resource table, mips smallest first, BGRA"
u vtf to-png fx/bgra.vtf b1.png --frame 1
eq "$(px b1.png 0,0)" "[3,2,1,4]" "--frame 1"
u vtf to-png fx/bgra.vtf b9.png --frame 9
eq "$(px b9.png 0,0)" "[3,2,1,4]" "frame past the end clamps to the last"
u vtf info fx/unit.sld
eq "$CODE" 1 "not a VTF"; has "$ERR" "not a VTF" "not a VTF message"
head -c 84 fx/dxt1.vtf > cut.vtf
u vtf to-png cut.vtf x.png
eq "$CODE" 1 "truncated vtf"; has "$ERR" "truncated VTF" "truncated vtf message"

# ================================================================ vpk
u vpk list fx/pak01_dir.vpk
eq "$OUT" $'materials/models/gun.vmt\t6\nreadme\t3\nsound/fire.wav\t5\n3 files' "vpk list (lower case, sorted, size = preload + data)"
u vpk list fx/pak02_dir.vpk --find GUN
eq "$OUT" $'materials/models/gun.vmt\t6\n1 files matching GUN' "v2 header, case-insensitive --find"
u vpk extract fx/pak01_dir.vpk --all --out ex
eq "$OUT" "extracted 3 files, 14 bytes -> ex" "extract summary"
eq "$(cat ex/materials/models/gun.vmt)" "ABCDEF" "preload + embedded data"
eq "$(cat ex/sound/fire.wav)" "BOOM!" "data in pak01_000.vpk"
eq "$(cat ex/readme)" "XYZ" "root entry without an extension"
u vpk extract fx/pak01_dir.vpk 'Sound\Fire.WAV' --out ex2
eq "$(cat ex2/sound/fire.wav)" "BOOM!" "backslashes and case"
mv fx/pak01_000.vpk fx/gone.vpk
u vpk extract fx/pak01_dir.vpk sound/fire.wav --out ex3
eq "$CODE" 1 "missing part"; has "$ERR" "missing archive part" "missing part message"
u vpk extract fx/evil_dir.vpk --all --out ex4
eq "$CODE" 1 "path escape refused"; has "$ERR" "refusing to write outside --out" "escape message"
[ -e evil ] && bad "wrote outside --out" || ok
u vpk extract fx/pak02_dir.vpk nope.txt
eq "$CODE" 1 "unknown path"
u vpk list fx/unit.sld
eq "$CODE" 1 "not a _dir.vpk"

done_ "modder/formats"

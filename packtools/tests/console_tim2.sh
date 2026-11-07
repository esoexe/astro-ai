#!/usr/bin/env bash
# console-graphics/tim2: the tim2 cases of console-skill/tests/test_graphics.py (TestParse, TestToPng, TestFromPng,
# TestPng through the CLI, TestScanInsert, TestCli) plus exact texts, JSON keys and refusals. PNGs are read and
# written by fixtures/mktim2.js with node's own zlib, so the built-in deflate/inflate are checked independently.
source "$(dirname "$0")/lib.sh"
M="$FIX/mktim2.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
X=(console-graphics/tim2)
j()  { echo "$OUT" | jq -c "$1"; }
mk() { node "$M" tm2 "$@"; }
pdec() { node "$M" pngdecode "$1"; }
tdec() { node "$M" decode "$@" | jq -c .rgba; }
md5() { md5sum "$1" | cut -d' ' -f1; }

# ---- TestParse
mk all.tm2 pic32 pic24 pic16 pic8 pic4 pic8mips
run "${X[@]}" info all.tm2 --json; eq "$CODE" 0 "info all"
eq "$(j '.pictures | length')" 6 "6 pictures"
eq "$(j '[.pictures[].bpp]')" '[32,24,16,8,4,8]' "bpp"
eq "$(j '[.pictures[3].palette_order, .pictures[4].palette_order]')" '["CSM1 swizzled","linear"]' "swizzled flags"
eq "$(j '.pictures[5].mipmaps')" 2 "mips"
eq "$(j '.size')" "$(stat -c %s all.tm2)" "size"
eq "$(j 'keys_unsorted')" '["file","version","align","size","pictures"]' "info keys"
eq "$(j '.pictures[0] | keys_unsorted')" '["index","offset","width","height","type","bpp","mipmaps","image_bytes","header_bytes","tex0","tex0_raw","tex1_raw","gs_regs","texclut"]' "direct picture keys"
eq "$(j '.pictures[3] | keys_unsorted | .[14:]')" '["palette_format","palette_colors","palettes","palette_order","clut_type"]' "indexed extra keys"
eq "$(j '.pictures[0] | [.tex0_raw, .tex1_raw, .gs_regs, .texclut]')" '["0x000000A111308000","0x0000000000000060","0x00000000","0x00000000"]' "raw registers"
eq "$(j '.pictures[0].tex0 | [.PSM, .TW, .TH, .TBW, .CBP]')" '["PSMT8",4,4,2,5]' "test_tex0_fields"
eq "$(j '.pictures[3] | [.palette_format, .palette_colors, .palettes, .clut_type]')" '["32-bit",256,1,"0x03"]' "palette fields"
mk al.tm2 --align1 pic32
run "${X[@]}" info al.tm2 --json; eq "$(j '[.align, .pictures[0].offset]')" '[128,128]' "test_128_byte_alignment"
# test_bad_files
mk good.tm2 pic8
node -e '
const fs=require("fs"), g=fs.readFileSync("good.tm2"), c=(...p)=>Buffer.concat(p.map(x=>typeof x==="string"?Buffer.from(x,"latin1"):x));
const cases=[Buffer.from("TIM"), c("NOPE",g.subarray(4)), c(g.subarray(0,4),"\x04\x07",g.subarray(6)), c(g.subarray(0,6),"\0\0",g.subarray(8)),
  g.subarray(0,40), g.subarray(0,g.length-10), c(g.subarray(0,16+0x13),"\x09",g.subarray(16+0x14)), c(g.subarray(0,16+0x14),"\0\0",g.subarray(16+0x16))];
cases.forEach((d,i)=>fs.writeFileSync("bad"+i+".tm2",d));'
BADMSG=("bad0.tm2 is too short to be a TIM2 file" "bad1.tm2 is not a TIM2 file (no 'TIM2' magic)" "bad2.tm2 has an unknown TIM2 alignment id 7"
        "bad3.tm2 holds no pictures" "bad4.tm2 is truncated: picture 0 header runs past the end" "bad5.tm2 is truncated: picture 0 runs past the end of the file"
        "bad6.tm2 picture 0 has an unknown image type 9" "bad7.tm2 picture 0 has a zero size")
for i in 0 1 2 3 4 5 6 7; do
    run "${X[@]}" info "bad$i.tm2"; eq "$CODE" 2 "bad file $i"; eq "$ERR" "tim2: error: ${BADMSG[$i]}" "bad file $i message"
done
# more refusals: inconsistent sizes, no palette, short palette, mip level
node -e '
const fs=require("fs"), g=Buffer.from(fs.readFileSync("good.tm2"));
let b=Buffer.from(g); b.writeUInt16LE(0x20,16+12); fs.writeFileSync("hs.tm2",b);
b=Buffer.from(g); b.writeUInt16LE(0,16+14); fs.writeFileSync("nopal.tm2",b);
b=Buffer.from(g); b[16+18]=0x05; fs.writeFileSync("cf.tm2",b);
b=Buffer.from(g); b.writeUInt16LE(300,16+14); fs.writeFileSync("short.tm2",b);
b=Buffer.from(g); b[16+17]=2; fs.writeFileSync("mip.tm2",b);'
run "${X[@]}" info hs.tm2; eq "$ERR" "tim2: error: hs.tm2 picture 0 has inconsistent sizes" "inconsistent sizes"
run "${X[@]}" info nopal.tm2; eq "$ERR" "tim2: error: nopal.tm2 picture 0 is indexed but has no usable palette" "no palette"
run "${X[@]}" info cf.tm2; eq "$ERR" "tim2: error: cf.tm2 picture 0 is indexed but has no usable palette" "unknown clut format"
run "${X[@]}" info short.tm2; eq "$ERR" "tim2: error: short.tm2 picture 0 palette is shorter than its colour count" "short palette"
run "${X[@]}" info mip.tm2; eq "$ERR" "tim2: error: mip.tm2 picture 0 mip level 1 does not fit its image data" "mip does not fit"
run "${X[@]}" info nothere.tm2; eq "$ERR" "tim2: error: TIM2 not found: nothere.tm2" "missing"
# test_info
mk a.tm2 pic8 pic4x2
run "${X[@]}" info a.tm2; eq "$CODE" 0 "info"
has "$OUT" "16x16 8-bit indexed" "16x16 8-bit indexed"; has "$OUT" "CSM1 swizzled" "CSM1 swizzled"; has "$OUT" "2 palette(s)" "2 palette(s)"
eq "$OUT" "a.tm2: TIM2 version 4, 2 picture(s), 16-byte alignment, 1472 bytes

  picture 0 at 0x10: 16x16 8-bit indexed
    palette   256 x 32-bit colours (1 palette(s)), CSM1 swizzled order
    TEX0      PSM PSMT8, TW 4 (16 px), TH 4 (16 px), TBW 2, CPSM PSMCT32, CSM 0

  picture 1 at 0x540: 8x4 4-bit indexed
    palette   32 x 16-bit colours (2 palette(s)), linear order
    TEX0      PSM PSMT8, TW 4 (16 px), TH 4 (16 px), TBW 2, CPSM PSMCT32, CSM 0" "info text"
run "${X[@]}" info a.tm2 --json; eq "$(j '[.pictures[0].tex0.PSM, .pictures[1].palettes]')" '["PSMT8",2]' "info json"
run "${X[@]}" info all.tm2; has "$OUT" "  picture 5 at 0x" "mip picture"; has "$OUT" ": 8x8 8-bit indexed, 2 mip levels" "mip levels text"
cat good.tm2 good.tm2 > extra.tm2; run "${X[@]}" info extra.tm2; has "$OUT" "

  $(stat -c %s good.tm2) extra bytes after the last picture" "extra bytes"

# ---- TestToPng
mk c32.tm2 pic32; run "${X[@]}" topng c32.tm2 --out o32; eq "$CODE" 0 "topng 32"
P=$(pdec o32/c32.png); eq "$(echo "$P" | jq -c '[.w,.h]')" '[4,2]' "32 size"
eq "$(echo "$P" | jq -c '[.rgba[][3]]')" '[255,128,0,255,255,255,64,255]' "test_32bit_alpha_scaled"
eq "$(echo "$P" | jq -c '.rgba[0][:3]')" '[255,0,0]' "32 colour"; eq "$(echo "$P" | jq -c .ctype)" 6 "RGBA PNG"
eq "$OUT" "wrote o32/c32.png
1 PNG(s) from 1 file(s). Repaint them with your own art, keep the size, then: tim2.py frompng" "topng text"
mk c24.tm2 pic24; run "${X[@]}" topng c24.tm2 --out o24; P=$(pdec o24/c24.png)
eq "$(echo "$P" | jq -c '[.w,.h,.rgba[1],.ctype]')" '[3,2,[3,4,5,255],2]' "test_24bit_odd_width"
mk c16.tm2 pic16; run "${X[@]}" topng c16.tm2 --out o16; P=$(pdec o16/c16.png)
eq "$(echo "$P" | jq -c '[.rgba[0],.rgba[1],.rgba[3]]')" '[[255,0,0,255],[0,255,0,255],[255,255,255,0]]' "test_16bit"
mk n16.tm2 pic16n; run "${X[@]}" topng n16.tm2 --out on16; P=$(pdec on16/n16.png)
eq "$(echo "$P" | jq -c '[.rgba[][3]] | unique')" '[255]' "16-bit alpha unused: opaque"
mk s8.tm2 pic8; run "${X[@]}" topng s8.tm2 --out os8; P=$(pdec os8/s8.png)
eq "$(echo "$P" | jq -c '.idx != null')" true "8-bit written as a palette PNG"
eq "$(echo "$P" | jq -c '.rgba')" "$(node -e 'const o=[];for(let i=0;i<256;i++){const k=(i*37+11)&255;o.push([k,255-k,(k*7)&255,255])}console.log(JSON.stringify(o))')" "test_8bit_swizzled_palette"
mk l8.tm2 pic8l; run "${X[@]}" topng l8.tm2 --out ol8; eq "$(pdec ol8/l8.png | jq -c .rgba)" "$(echo "$P" | jq -c .rgba)" "same picture either way"
mk c4.tm2 pic4x2; run "${X[@]}" topng c4.tm2 --out oc4 --all-palettes
PA=$(pdec oc4/c4_pal0.png); PB=$(pdec oc4/c4_pal1.png)
eq "$(echo "$PA" | jq -c .idx)" "$(echo "$PB" | jq -c .idx)" "same indices"
[ "$(echo "$PA" | jq -c .rgba)" != "$(echo "$PB" | jq -c .rgba)" ] && ok || bad "different colours"
eq "$(echo "$PA" | jq -c '.idx[:4]')" '[0,1,2,3]' "nibble order"
hasnt "$OUT" "(name_N.png" "no hint for _palN alone (the pattern needs _<digits>)"
run "${X[@]}" topng c4.tm2 --out oc4r --rgba; eq "$(pdec oc4r/c4.png | jq -c .idx)" null "--rgba"
run "${X[@]}" topng c4.tm2 --out oc4p --palette 1; eq "$(ls oc4p)" "c4_pal1.png" "--palette 1 name"
eq "$(pdec oc4p/c4_pal1.png | jq -c .rgba)" "$(echo "$PB" | jq -c .rgba)" "--palette 1 colours"
run "${X[@]}" topng c4.tm2 --out oc4x --palette 2; eq "$CODE" 2 "missing palette"; eq "$ERR" "tim2: error: picture 0 has no palette 2 (it has 2)" "missing palette message"
mk m.tm2 pic8mips pic32; run "${X[@]}" topng m.tm2 --out om --mips
eq "$(ls om | tr '\n' ' ')" "m_0.png m_0_mip1.png m_1.png " "test_mips_and_multi_picture_names"
eq "$(pdec om/m_0_mip1.png | jq -c '[.w,.h]')" '[4,4]' "mip size"
eq "$(pdec om/m_0_mip1.png | jq -c .rgba)" "$(tdec m.tm2 0 1)" "mip pixels (oracle)"
# test_folder_with_a_broken_file
mkdir -p tex/sub; mk tex/a.tm2 pic32; mk tex/sub/b.TM2 pic8; printf 'TIM2junk' > tex/sub/broken.tm2; printf hi > tex/readme.txt
run "${X[@]}" topng tex --out pngs; eq "$CODE" 1 "folder exit 1"
eq "$ERR" "skipped sub/broken.tm2: sub/broken.tm2 is too short to be a TIM2 file" "skipped line"
[ -f pngs/a.png ] && [ -f pngs/sub/b.png ]; eq $? 0 "folder PNGs"
eq "$OUT" "wrote pngs/a.png
wrote pngs/sub/b.png
2 PNG(s) from 3 file(s). Repaint them with your own art, keep the size, then: tim2.py frompng" "folder text"
mkdir empty; run "${X[@]}" topng empty --out oe; eq "$CODE" 2 "test_no_files"; eq "$ERR" "tim2: error: no .tm2 files under empty" "no files message"
run "${X[@]}" topng missing.tm2 --out om2; eq "$CODE" 2 "missing input"; eq "$ERR" "tim2: error: TIM2 not found: missing.tm2" "missing input message"
cp c32.tm2 .hidden.tm2; run "${X[@]}" topng .hidden.tm2 --out oh; eq "$(ls -A oh)" ".hidden.png" "splitext .hidden.tm2"
mkdir dotdir; cp c32.tm2 dotdir/.tm2; run "${X[@]}" topng dotdir/.tm2 --out od; eq "$(ls -A od)" ".tm2.png" "splitext leaves a leading-dot name whole"
cp c32.tm2 c32.v1.tm2; run "${X[@]}" topng c32.v1.tm2 --out ov; eq "$(ls ov)" "c32.v1.png" "splitext last dot"

# ---- TestFromPng
roundtrip() {   # NAME PICS... [-- TOPNG_ARGS]
    local name=$1; shift; local pics=() extra=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do pics+=("$1"); shift; done; [ "$1" = "--" ] && shift; extra=("$@")
    mk "$name" "${pics[@]}"; rm -rf "rt_$name"; run "${X[@]}" topng "$name" --out "rt_$name" "${extra[@]}"
    run "${X[@]}" frompng "$name" "rt_$name/$(ls "rt_$name" | head -n1)" --out "new_$name"
}
for spec in "a.tm2 pic32" "b.tm2 pic24" "c.tm2 pic16" "d.tm2 pic16n" "e.tm2 pic8" "f.tm2 pic8l" "g.tm2 pic4" "h.tm2 pic8mips" "i.tm2 --align1 pic32"; do
    set -- $spec; roundtrip "$@"; eq "$CODE" 0 "round trip $1"
    eq "$(md5 "new_$1")" "$(md5 "$1")" "unchanged round trip is byte identical: $1"; has "$OUT" "0 of" "0 of: $1"
done
roundtrip j.tm2 pic8 -- --rgba; eq "$(md5 new_j.tm2)" "$(md5 j.tm2)" "--rgba round trip identical"
eq "$OUT" "wrote new_j.tm2 (1344 bytes, same size and format as j.tm2; GS registers unchanged)
  0 of 256 pixels changed" "frompng text"

edit() {   # NAME PICS JQ [EXTRA...] ; sets OUT/CODE/ERR, writes edited_NAME.png and new_NAME
    local name=$1 pics=$2 f=$3; shift 3
    mk "$name" $pics; rm -rf "ed_$name"; run "${X[@]}" topng "$name" --out "ed_$name" --rgba
    pdec "ed_$name/$(ls "ed_$name" | head -n1)" | jq -c "{w,h,rgba: (.rgba | $f)}" > "edited_$name.json"
    node "$M" pngencode "edited_$name.png" "@edited_$name.json"
    run "${X[@]}" frompng "$name" "edited_$name.png" --out "new_$name" "$@"
}
want() { jq -c .rgba "edited_$1.json"; }
edit x.tm2 pic32 '.[0] = [12,34,56,255] | .[5] = [99,98,97,128]'
eq "$CODE" 0 "test_direct_colour_edit"; has "$OUT" "2 of 8 pixels changed" "2 of 8"
eq "$(stat -c %s new_x.tm2)" "$(stat -c %s x.tm2)" "same size"
eq "$(head -c 64 new_x.tm2 | md5sum)" "$(head -c 64 x.tm2 | md5sum)" "header and GS registers untouched"
eq "$(tdec new_x.tm2)" "$(want x.tm2)" "decodes to the edit"
eq "$(od -An -tx1 -j$((16+0x30+20)) -N4 new_x.tm2 | tr -d ' ')" "63626140" "alpha 128 -> PS2 0x40"
edit y.tm2 pic16n '.[0] = [255,255,0,255]'
eq "$CODE" 0 "test_16bit_edit_keeps_unused_alpha"
eq "$(od -An -tu2 -j$((16+0x30)) -N16 new_y.tm2 | tr -s ' ' '\n' | grep -v '^$' | awk '$1>=32768' | wc -l)" 0 "no alpha bit set"
eq "$(tdec new_y.tm2)" "$(want y.tm2)" "16-bit decodes to the edit"
edit z.tm2 pic8 '.[0] as $a | .[0] = .[1] | .[1] = $a'
eq "$CODE" 0 "test_indexed_edit_with_existing_colours_keeps_palette"; hasnt "$OUT" "palette 0 rewritten" "palette kept"
eq "$(tail -c 1024 new_z.tm2 | md5sum)" "$(tail -c 1024 z.tm2 | md5sum)" "palette bytes identical"
eq "$(tdec new_z.tm2)" "$(want z.tm2)" "indexed decodes to the edit"
eq "$OUT" "wrote new_z.tm2 (1344 bytes, same size and format as z.tm2; GS registers unchanged)
  2 of 256 pixels changed" "indexed edit text"
edit sl.tm2 slot8 '.[10] = [1,2,3,255]'
eq "$CODE" 0 "test_indexed_new_colour_goes_in_a_free_slot"; has "$OUT" "palette 0 rewritten (1 entries changed)" "1 entry changed"
eq "$(tdec new_sl.tm2)" "$(want sl.tm2)" "free slot decodes to the edit"
SLOT=$(od -An -tu1 -j$((16+0x30+10)) -N1 new_sl.tm2 | tr -d ' ')
[ "$SLOT" -ge 4 ]; eq $? 0 "slot >= 4 (got $SLOT)"
eq "$SLOT" 4 "first free slot"
SW=$(( (SLOT & 31) >= 8 && (SLOT & 31) < 16 ? SLOT + 8 : (SLOT & 31) >= 16 && (SLOT & 31) < 24 ? SLOT - 8 : SLOT ))
eq "$(od -An -tx1 -j$((16+0x30+64+4*SW)) -N4 new_sl.tm2 | tr -d ' ')" "01020380" "stored at the swizzled entry"
edit q1.tm2 pic4 '[range(length) as $i | [$i % 256, ($i*7) % 256, ($i*13) % 256, 255]]'
eq "$CODE" 2 "test_too_many_colours_needs_quantize"; has "$ERR" "--quantize" "needs --quantize"
eq "$ERR" "tim2: error: the edit uses 32 colours but this 4-bit texture holds 16: reduce the colours in your editor (indexed/palette mode), or pass --quantize" "quantize message"
edit q.tm2 pic4 '[range(length) as $i | [$i % 256, ($i*7) % 256, ($i*13) % 256, 255]]' --quantize
eq "$CODE" 0 "--quantize"; has "$OUT" "quantised 32 colours down to 16 (median cut)" "quantised"
eq "$(tdec new_q.tm2 | jq 'unique | length <= 16')" true "at most 16 colours"
eq "$(stat -c %s new_q.tm2)" "$(stat -c %s q.tm2)" "same size after quantize"
has "$OUT" "palette 0 rewritten" "quantize rewrites the palette"
# a 5-colour edit of the 16-colour texture: new colours fill slots no pixel keeps
edit five.tm2 pic4 '[range(length) as $i | [[0,8,16,24,33][$i % 5], 0, 0, 255]]'
eq "$CODE" 0 "few new colours"; eq "$(tdec new_five.tm2)" "$(want five.tm2)" "few new colours decode exactly (5-bit values)"
# test_second_palette_edit_leaves_first
mk p2.tm2 pic4x2
node "$M" decode p2.tm2 0 0 1 | jq -c '.rgba[0] as $f | {w,h,rgba: [.rgba[] | if . == $f then [255,0,255,255] else . end]}' > p2.json
node "$M" pngencode p2.png @p2.json
run "${X[@]}" frompng p2.tm2 p2.png --out p2_new.tm2 --palette 1; eq "$CODE" 0 "second palette edit"
eq "$(tdec p2_new.tm2 0 0 0)" "$(tdec p2.tm2 0 0 0)" "first palette unchanged"
eq "$(tdec p2_new.tm2 0 0 1)" "$(jq -c .rgba p2.json)" "second palette edited"
run "${X[@]}" frompng p2.tm2 p2.png --out p2_x.tm2 --palette 2; eq "$ERR" "tim2: error: palette 2 does not exist (picture 0 has 2)" "palette does not exist"
# test_mips_regenerated_or_kept
edit mm.tm2 pic8mips '.[0] as $a | [range(length) | $a]'
has "$OUT" "regenerated 1 mip level(s) from your image" "regenerated"
eq "$(tdec new_mm.tm2 0 1 | jq -c unique)" "[$(want mm.tm2 | jq -c '.[0]')]" "mip is the new colour"
edit k.tm2 pic8mips '.[0] as $a | [range(length) | $a]' --keep-mips
has "$OUT" "kept the original 1 smaller mip levels (they still show the old art far away)" "kept"
eq "$(tail -c $((1024+16)) new_k.tm2 | head -c 16 | md5sum)" "$(tail -c $((1024+16)) k.tm2 | head -c 16 | md5sum)" "mip bytes kept"
# test_24bit_alpha_dropped_note
edit t24.tm2 pic24 '[[1,2,3,0]] + .[1:]'; has "$OUT" "24-bit textures have no alpha: transparency in the PNG was dropped" "test_24bit_alpha_dropped_note"
# test_wrong_size_and_bad_choices
mk s.tm2 pic32
node "$M" pngencode big.png '{"w":5,"h":2,"rgba":[[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255]]}'
run "${X[@]}" frompng s.tm2 big.png --out o.tm2; eq "$CODE" 2 "wrong size"; has "$ERR" "same size" "same size"
eq "$ERR" "tim2: error: the PNG is 5x2 but picture 0 is 4x2: keep the same size (the game's code and GS registers expect it)" "wrong size message"
node "$M" pngencode ok.png '{"w":4,"h":2,"rgba":[[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255],[0,0,0,255]]}'
run "${X[@]}" frompng s.tm2 ok.png --out o2.tm2 --picture 3; eq "$CODE" 2 "--picture 3"; eq "$ERR" "tim2: error: picture 3 does not exist (the file has 1)" "picture message"
run "${X[@]}" frompng s.tm2 ok.png --out s.tm2; eq "$CODE" 2 "--out original"; eq "$ERR" "tim2: error: --out is the original file: write a new file (or pass --force; keep a backup)" "out original message"
run "${X[@]}" frompng s.tm2 ok.png --out o3.tm2; eq "$CODE" 0 "frompng ok"; has "$OUT" "8 of 8 pixels changed" "all changed"
run "${X[@]}" frompng s.tm2 ok.png --out o3.tm2; eq "$CODE" 2 "exists"; eq "$ERR" "tim2: error: o3.tm2 exists: pass --force to overwrite it" "exists message"
run "${X[@]}" frompng s.tm2 ok.png --out o3.tm2 --force; eq "$CODE" 0 "--force"
run "${X[@]}" frompng s.tm2 ok.png --out deep/er/o4.tm2; eq "$CODE" 0 "makedirs"; [ -f deep/er/o4.tm2 ]; eq $? 0 "nested out written"
run "${X[@]}" frompng s.tm2 nope.png --out o5.tm2; eq "$ERR" "tim2: error: PNG not found: nope.png" "PNG not found"

# ---- TestPng (decoder through frompng; encoder through topng above)
node "$M" pngraw f.png filters > fpx.json; mk f5.tm2 pic32
run "${X[@]}" frompng f5.tm2 f.png --out f5o.tm2; eq "$ERR" "tim2: error: the PNG is 5x5 but picture 0 is 4x2: keep the same size (the game's code and GS registers expect it)" "filter PNG decodes (size)"
# decode every filter for real: a 5x5 32-bit TIM2 rebuilt from the filtered PNG must hold exactly those pixels
node -e '
const fs=require("fs"); const px=JSON.parse(fs.readFileSync("fpx.json")); const u32=v=>{const b=Buffer.alloc(4);b.writeUInt32LE(v);return b};
const img=Buffer.from(px.map(c=>[c[0],c[1],c[2],0x80]).flat()); const hs=0x30;
const ph=Buffer.concat([u32(hs+img.length),u32(0),u32(img.length),Buffer.from([hs,0,0,0,0,1,0,3,5,0,5,0]),Buffer.alloc(24)]);
fs.writeFileSync("p5.tm2",Buffer.concat([Buffer.from("TIM2"),Buffer.from([4,0,1,0]),Buffer.alloc(8),ph,img]));'
run "${X[@]}" frompng p5.tm2 f.png --out p5o.tm2; eq "$CODE" 0 "all filters"
eq "$(tdec p5o.tm2 | jq -c '[.[] | .[:3]]')" "$(jq -c '[.[] | .[:3]]' fpx.json)" "test_all_filters (colours)"
eq "$(tdec p5o.tm2 | jq -c '[.[] | .[3]]')" "$(jq -c '[.[] | .[3] | if . >= 255 then 255 else ((. * 128 + 127) / 255 | floor) as $p | if $p >= 128 then 255 else (($p * 255 + 64) / 128 | floor) end end]' fpx.json)" "test_all_filters (alpha via PS2)"
# grey, grey+alpha, RGB+tRNS, low bit depths: rebuild a 32-bit TIM2 of the same size and read the colours back
tim32() { node -e 'const fs=require("fs"),[o,w,h]=process.argv.slice(1).map((x,i)=>i?+x:x);const u32=v=>{const b=Buffer.alloc(4);b.writeUInt32LE(v);return b};const img=Buffer.alloc(w*h*4);const ph=Buffer.concat([u32(0x30+img.length),u32(0),u32(img.length),Buffer.from([0x30,0,0,0,0,1,0,3,w,0,h,0]),Buffer.alloc(24)]);fs.writeFileSync(o,Buffer.concat([Buffer.from("TIM2"),Buffer.from([4,0,1,0]),Buffer.alloc(8),ph,img]))' "$@"; }
decodes_to() {   # CASE W H EXPECTED
    node "$M" pngraw "$1.png" "$1"; tim32 "$1.tm2" "$2" "$3"
    run "${X[@]}" frompng "$1.tm2" "$1.png" --out "$1_o.tm2"; eq "$CODE" 0 "$1 decodes"
    eq "$(tdec "$1_o.tm2")" "$4" "$1 colours"
}
decodes_to grey 2 1 '[[7,7,7,255],[9,9,9,0]]'
decodes_to greyalpha 1 1 '[[5,5,5,6]]'
decodes_to rgbtrns 2 1 '[[1,2,3,255],[4,5,6,0]]'
decodes_to lowdepth2 4 1 '[[0,0,0,255],[255,255,255,255],[255,0,0,255],[0,0,255,255]]'
decodes_to lowdepth4 3 1 '[[255,255,255,255],[255,0,0,255],[0,0,0,255]]'
# test_refusals
tim32 one.tm2 1 1; tim32 two.tm2 2 2
for c in notpng crc interlaced 16bit truncated truncated2 badfilter noplte badindex colortype badzlib shortdata; do node "$M" pngraw "$c.png" "$c"; done
refuse() { run "${X[@]}" frompng "$1" "$2.png" --out "r_$2.tm2"; eq "$CODE" 2 "refuses $2"; eq "$ERR" "tim2: error: $2.png $3" "$2 message"; }
refuse one.tm2 notpng "is not a PNG"
refuse two.tm2 crc "is corrupt (bad CRC in chunk 'IHDR')"
refuse one.tm2 interlaced "is interlaced: save it again without interlacing"
refuse one.tm2 16bit "is 16-bit per channel: save it as an 8-bit PNG (RGB, RGBA, grey or palette)"
refuse two.tm2 truncated "has corrupt image data"   # good[:40] stops before the IDAT header: nothing to inflate
refuse two.tm2 truncated2 "is truncated (chunk b'IDAT')"
refuse one.tm2 badfilter "has an unknown row filter 9"
refuse one.tm2 noplte "is a palette PNG without a palette"
refuse one.tm2 badindex "uses palette entry 5 but its palette has 2"
refuse one.tm2 colortype "has an unknown colour type 5"
refuse one.tm2 badzlib "has corrupt image data"
refuse one.tm2 shortdata "image data is truncated"
printf '\x89PNG\r\n\x1a\n' > noihdr.png; refuse one.tm2 noihdr "has no IHDR"

# ---- TestScanInsert
INFO=$(node "$M" container archive.bin blob)
O1=$(echo "$INFO" | jq .offs[0]); O2=$(echo "$INFO" | jq .offs[1])
run "${X[@]}" scan archive.bin --json --extract ex; eq "$CODE" 0 "test_scan_and_extract"
eq "$(j '[.blobs[].offset]')" "[\"$(printf '0x%X' "$O1")\",\"$(printf '0x%X' "$O2")\"]" "offsets"
eq "$(j '[.blobs[].size]')" "$(echo "$INFO" | jq -c .sizes)" "sizes"
eq "$(j '[.blobs[].pictures]')" '[1,2]' "pictures"; eq "$(j '[.blobs[].first]')" '["16x16 8-bit indexed","4x2 32-bit RGBA"]' "first"
eq "$(j '.blobs[0] | keys_unsorted')" '["offset","size","pictures","first","extracted"]' "blob keys"
eq "$(md5 "$(echo "$OUT" | jq -r '.blobs[0].extracted')")" "$(md5 blob_a.tm2)" "extracted a"
eq "$(md5 "$(echo "$OUT" | jq -r '.blobs[1].extracted')")" "$(md5 blob_b.tm2)" "extracted b"
eq "$(echo "$OUT" | jq -r '.blobs[0].extracted')" "ex/archive.bin_$(printf '%08X' "$O1").tm2" "extract name"
run "${X[@]}" scan archive.bin; has "$OUT" "2 TIM2 blob(s)" "2 blobs"
eq "$OUT" "archive.bin: 2 TIM2 blob(s)
$(printf '  %-12s %9d bytes  1 picture(s), first 16x16 8-bit indexed' "$(printf '0x%X' "$O1")" "$(echo "$INFO" | jq .sizes[0])")
$(printf '  %-12s %9d bytes  2 picture(s), first 4x2 32-bit RGBA' "$(printf '0x%X' "$O2")" "$(echo "$INFO" | jq .sizes[1])")
Extract them with --extract DIR, edit, then put one back with: tim2.py insert" "scan text"
run "${X[@]}" scan archive.bin --extract ex2; has "$OUT" "  -> ex2/archive.bin_" "extract arrow"; hasnt "$OUT" "Extract them" "no hint with --extract"
run "${X[@]}" scan archive.bin --json --limit 1; eq "$(j '.blobs | length')" 1 "--limit 1"
run "${X[@]}" scan archive.bin --json --limit=-1; eq "$(j '.blobs | length')" 1 "negative limit stops after one (Python truthiness)"
# blobs across the 8 MiB read boundary
node -e 'const fs=require("fs");const a=fs.readFileSync("blob_a.tm2");const big=Buffer.alloc(8*1024*1024+4096,0x41);a.copy(big,8*1024*1024-2);fs.writeFileSync("big.bin",big)'
run "${X[@]}" scan big.bin --json; eq "$(j '[.blobs[].offset]')" "[\"$(printf '0x%X' $((8*1024*1024-2)))\"]" "magic split across chunks"
printf 'no textures here%.0s' {1..10} > plain.bin; run "${X[@]}" scan plain.bin; eq "$CODE" 1 "test_scan_nothing"; eq "$OUT" "plain.bin: 0 TIM2 blob(s)" "nothing text"
run "${X[@]}" scan plain.bin --json; eq "$CODE" 1 "nothing json exit 1"; eq "$(j .blobs)" '[]' "empty blobs"
run "${X[@]}" scan nope.bin; eq "$ERR" "tim2: error: file not found: nope.bin" "scan missing"
# test_insert_round_trip
cp blob_a.tm2 edited.tm2; node -e 'const fs=require("fs");const b=fs.readFileSync("edited.tm2");b[16+0x30]^=0xFF;fs.writeFileSync("edited.tm2",b)'
run "${X[@]}" insert archive.bin --offset "$(printf '0x%x' "$O1")" --tm2 edited.tm2 --out patched/archive.bin; eq "$CODE" 0 "insert"
eq "$OUT" "wrote patched/archive.bin: $(stat -c %s edited.tm2) bytes at $(printf '0x%X' "$O1") replaced, file size unchanged" "insert text"
eq "$(stat -c %s patched/archive.bin)" "$(stat -c %s archive.bin)" "size unchanged"
eq "$(tail -c +$((O1+1)) patched/archive.bin | head -c "$(stat -c %s edited.tm2)" | md5sum)" "$(md5sum < edited.tm2)" "new blob in place"
eq "$(head -c "$O1" patched/archive.bin | md5sum)" "$(head -c "$O1" archive.bin | md5sum)" "before unchanged"
eq "$(tail -c +$((O1+1+$(stat -c %s edited.tm2))) patched/archive.bin | md5sum)" "$(tail -c +$((O1+1+$(stat -c %s edited.tm2))) archive.bin | md5sum)" "after unchanged"
run "${X[@]}" insert archive.bin --offset "$(printf '%x' "$O1")" --tm2 edited.tm2 --out p2.bin; eq "$CODE" 0 "offset without 0x"
run "${X[@]}" insert archive.bin --offset " 0X$(printf '%X' "$O1") " --tm2 edited.tm2 --out p3.bin; eq "$CODE" 0 "0X and spaces"
# test_insert_refusals
mk wrong.tm2 pic32; cp blob_a.tm2 right.tm2
irun() { run "${X[@]}" insert archive.bin "$@"; }
irun --offset "$(printf '0x%x' "$O1")" --tm2 wrong.tm2 --out o.bin; eq "$CODE" 2 "different size"
eq "$ERR" "tim2: error: wrong.tm2 is $(stat -c %s wrong.tm2) bytes but the TIM2 at $(printf '0x%X' "$O1") is $(stat -c %s right.tm2): it must be exactly the same size" "size message"
irun --offset 0x10 --tm2 right.tm2 --out o.bin; eq "$ERR" "tim2: error: there is no TIM2 at 0x10 in archive.bin (tim2.py scan lists the offsets)" "no TIM2 there"
irun --offset 0xFFFFFFF --tm2 right.tm2 --out o.bin; eq "$ERR" "tim2: error: offset 0xFFFFFFF is past the end of archive.bin" "past the end"
irun --offset zz --tm2 right.tm2 --out o.bin; eq "$ERR" "tim2: error: --offset is not a hex number: 'zz'" "not hex"
irun --offset "$(printf '0x%x' "$O1")" --tm2 right.tm2 --out archive.bin; eq "$ERR" "tim2: error: --out is the container itself: write a new file (or pass --force; keep a backup)" "in place without --force"
irun --offset 64 --tm2 right.tm2 --out o.bin; eq "$ERR" "tim2: error: the TIM2 at 0x64 in archive.bin is not readable" "fake TIM2 not readable"
irun --offset "$(printf '0x%x' "$O1")" --tm2 right.tm2 --out p2.bin; eq "$ERR" "tim2: error: p2.bin exists: pass --force to overwrite it" "out exists"
node -e 'const fs=require("fs");const b=fs.readFileSync("right.tm2");b.writeUInt16LE(8,16+20);b.writeUInt16LE(32,16+22);fs.writeFileSync("resized.tm2",b)'
irun --offset "$(printf '0x%x' "$O1")" --tm2 resized.tm2 --out o6.bin; eq "$ERR" "tim2: error: picture 0 differs in size or format from the one in the container" "format differs"
cp archive.bin inplace.bin; run "${X[@]}" insert inplace.bin --offset "$(printf '0x%x' "$O1")" --tm2 edited.tm2 --out inplace.bin --force
eq "$CODE" 0 "in place with --force"; eq "$(md5 inplace.bin)" "$(md5 patched/archive.bin)" "in place result"
irun --offset 0x10 --tm2 nope.tm2 --out o.bin; eq "$ERR" "tim2: error: TIM2 not found: nope.tm2" "tm2 missing"
run "${X[@]}" insert nope.bin --offset 0 --tm2 right.tm2 --out o.bin; eq "$ERR" "tim2: error: file not found: nope.bin" "container missing"
irun --offset 0x_1_0 --tm2 right.tm2 --out o.bin; has "$ERR" "there is no TIM2 at 0x10" "underscores in hex"

# ---- TestCli
run "${X[@]}" --help; eq "$CODE" 0 "--help"; TOPHELP=$OUT
for c in info topng frompng scan insert; do has "$TOPHELP" "$c" "help lists $c"; run "${X[@]}" "$c" --help; eq "$CODE" 0 "$c --help"; done
run "${X[@]}" --help; has "$OUT" "Exit codes: 0 ok, 1 nothing found or some files skipped, 2 bad input." "epilog"
run "${X[@]}"; eq "$CODE" 2 "no command"
run "${X[@]}" topng c32.tm2; eq "$CODE" 2 "--out required"; has "$ERR" "the following arguments are required: --out" "required message"
run "${X[@]}" scan --help; has "$OUT" "--extract DIR" "metavar DIR"
# test_subprocess_end_to_end
mk t.tm2 pic8; run "${X[@]}" topng t.tm2 --out o; eq "$CODE" 0 "e2e topng"
run "${X[@]}" frompng t.tm2 o/t.png --out n.tm2; eq "$CODE" 0 "e2e frompng"
run "${X[@]}" info missing.tm2; eq "$CODE" 2 "e2e missing"; hasnt "$ERR" "Traceback" "no traceback"
done_ "console-graphics/tim2"

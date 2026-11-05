#!/usr/bin/env bash
# modder/3d: the C++ ports of universal-modder's last Python (Rehan and universal-modder contributors, MIT):
#   um render3d  (um/render3d.py + um/blender/render_sprites.py: Blender -> a built-in software renderer)
#   um b3d       (skills/blitz3d-decomp/scripts/roblox: b3d_skin.py, vm_anim_data.py, vm_build.py, vm_render.py)
#   um upk       (skills/reverse-engineering/references/ue3-packages/unpack_upk.py: minilzo -> a C++ LZO1X decoder)
# Fixtures are written by fixtures/modder/gen_fixtures.js (Node, no Python); PNGs are inspected with png_stats.js.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export UM_HOME="$TMP/umh" HOME="$TMP/home"; mkdir -p "$HOME"
unset FAL_KEY SPRIXEN_API_KEY VOIDLESS_API_KEY UM_FAKE_WSL UM_RENDER_THREADS
u() { run um "$@"; }
node "$FIX/modder/gen_fixtures.js" fx || { echo "modder/3d: fixture generator failed"; exit 1; }
st() { node "$FIX/modder/png_stats.js" "$1"; }
j() { jq -r "$1" <<<"$2"; }
nfiles() { find "$1" -maxdepth 1 -name "$2" | wc -l | tr -d ' '; }

# ================================================================ render3d
u render3d --help
eq "$CODE" 0 "render3d help"
has "$OUT" "aoe2,iso8,trueiso,topdown,side,turntable" "presets listed"
has "$OUT" "bob,walk,lunge,die,wreck,spin,still" "motions listed"
has "$OUT" "no Blender, no Python" "help says what renders"

u render3d fx/unit.glb r1 --preset aoe2 --canvas 160 --length 80 --headings 4 --anims idle:3:bob,death:1:die --shadows
eq "$CODE" 0 "render aoe2"
has "$OUT" "rendering 32 images with the built-in renderer" "image count announced"
has "$OUT" "rendered 32 images -> " "done line"
eq "$(nfiles r1 '*.png')" 32 "32 files"
for f in idle_00_000 idle_00_001 idle_03_002 death_02_000 idle_01_000_s death_03_000_s; do [ -f "r1/$f.png" ] && ok || bad "missing r1/$f.png"; done
S0=$(st r1/idle_00_000.png); S1=$(st r1/idle_01_000.png)
eq "$(j .w "$S0")x$(j .h "$S0")" "160x160" "canvas size"
eq "$(j .max_alpha "$S0")" 255 "model is opaque"
eq "$(j .min_alpha "$S0")" 0 "background is transparent"
W=$(j '.bbox[2]-.bbox[0]' "$S0")
[ "$W" -ge 77 ] && [ "$W" -le 83 ] && ok || bad "--length 80: heading 0 width $W"
# heading 0 = east: the red nose (+X in glTF) is right of centre; heading 1 (clockwise) = south, facing the camera
awk -v x="$(j .red_cx "$S0")" 'BEGIN{exit !(x != "null" && x + 0 > 100)}' && ok || bad "heading 0 nose points east (red_cx $(j .red_cx "$S0"))"
awk -v x="$(j .red_cx "$S1")" 'BEGIN{exit !(x != "null" && x + 0 > 70 && x + 0 < 90)}' && ok || bad "heading 1 nose faces the camera (red_cx $(j .red_cx "$S1"))"
[ "$(j .red "$S0")" -gt 20 ] && ok || bad "untextured red material rendered"
SS=$(st r1/idle_00_000_s.png)
eq "$(j .max_rgb "$SS")" 0 "shadow pass is black"
[ "$(j .covered "$SS")" -gt 200 ] && ok || bad "shadow pass has a shadow ($(j .covered "$SS") px)"
# the bob motion moves frame 1; the die motion rolls the model
cmp -s r1/idle_00_000.png r1/idle_00_001.png && bad "bob frames are identical" || ok
cmp -s r1/idle_00_000.png r1/death_00_000.png && ok || bad "die at t=0 is the rest pose"
# deterministic, whatever the thread count
UM_RENDER_THREADS=1 u render3d fx/unit.glb r1b --preset aoe2 --canvas 160 --length 80 --headings 4 --anims idle:3:bob,death:1:die --shadows
has "$OUT" "1 thread)" "UM_RENDER_THREADS=1"
same=1; for f in r1/*.png; do cmp -s "$f" "r1b/$(basename "$f")" || same=0; done
eq "$same" 1 "same images single-threaded"
# a .gltf with a data: URI buffer renders the same
u render3d fx/unit.gltf r2 --preset aoe2 --canvas 160 --length 80 --headings 4 --anims idle:3:bob,death:1:die
eq "$CODE" 0 "render .gltf"
cmp -s r1/idle_01_000.png r2/idle_01_000.png && ok || bad ".gltf and .glb render the same"

# side view: 2 headings, standing on 92% of the canvas height; --measure height
u render3d fx/unit.glb r3 --preset side --canvas 128 --length 60 --measure height
eq "$CODE" 0 "render side"
eq "$(nfiles r3 '*.png')" 2 "side: 2 files"
S3=$(st r3/idle_00_000.png)
B=$(j '.bbox[3]' "$S3"); H=$(j '.bbox[3]-.bbox[1]' "$S3")
[ "$B" -ge 116 ] && [ "$B" -le 120 ] && ok || bad "side: feet at 0.92 x 128 (bottom $B)"
[ "$H" -ge 58 ] && [ "$H" -le 62 ] && ok || bad "side: --measure height 60 (height $H)"
u render3d fx/unit.glb r4 --preset side --canvas 128 --length 60 --ground-y 64 --forward-yaw 90 --samples 1
B4=$(j '.bbox[3]' "$(st r4/idle_00_000.png)")
[ "$B4" -ge 62 ] && [ "$B4" -le 66 ] && ok || bad "--ground-y 64 (bottom $B4)"
# perspective turntable, presets and overrides
u render3d fx/unit.glb r5 --preset turntable --canvas 96 --samples 4
eq "$(nfiles r5 '*.png')" 24 "turntable: 24 headings"
u render3d fx/unit.glb r6 --preset topdown --canvas 64x48 --headings 3 --elevation 60 --samples 1
eq "$(nfiles r6 '*.png')" 3 "--headings override"
eq "$(j '"\(.w)x\(.h)"' "$(st r6/idle_02_000.png)")" "64x48" "WxH canvas"

# errors
u render3d missing.glb rx
eq "$CODE" 1 "missing file"; has "$ERR" "no such file" "missing message"
echo "not a model" > fx/junk.glb
u render3d fx/junk.glb rx
eq "$CODE" 1 "junk"; has "$ERR" "not a GLB or glTF file" "junk message"
echo '{"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"]}' > fx/draco.gltf
u render3d fx/draco.gltf rx
eq "$CODE" 1 "draco"; has "$ERR" "KHR_draco_mesh_compression" "draco message"
echo '{"asset":{"version":"2.0"},"nodes":[{"name":"empty"}],"scenes":[{"nodes":[0]}]}' > fx/empty.gltf
u render3d fx/empty.gltf rx
eq "$CODE" 1 "empty"; has "$ERR" "no triangle meshes" "empty message"
u render3d fx/unit.glb rx --preset nope
eq "$CODE" 2 "bad preset is a usage error"

# ================================================================ b3d dump (b3d_skin.py)
u b3d dump fx/viewmodel.b3d vm.json
eq "$CODE" 0 "b3d dump"
eq "$(sed -n 1p <<<"$OUT")" "texs ['Textures\\\\arms.png', 'gun.png']" "texs line"
eq "$(sed -n 2p <<<"$OUT")" "brus [('arms', [0], 1, 0), ('gun', [1], 1, 0)]" "brus line"
eq "$(sed -n 3p <<<"$OUT")" "Scene_Root MESH 8 verts, surfaces [(0, 6), (1, 6)] | anim {'flags': 0, 'frames': 10, 'fps': 30.0} | seqs [('idle', 1, 5), ('fire', 6, 10)]" "root line"
eq "$(sed -n 4p <<<"$OUT")" "  arm bone 5 | keys 10" "arm line"
eq "$(sed -n 6p <<<"$OUT")" "      weaponfire " "leaf line"
eq "$(sed -n 7p <<<"$OUT")" "  Scene_Root " "duplicate name line"
D=$(cat vm.json)
eq "$(j .version "$D")" 1 "version"
eq "$(j '[keys_unsorted[]]|join(",")' "$D")" "texs,brus,root,version" "top-level key order"
eq "$(j '.root.children[0].keys[0]|[keys_unsorted[]]|join(",")' "$D")" "frame,rot,pos" "KEYS chunks merged per frame, in order"
eq "$(j '.root.children[0].keys|length' "$D")" 10 "10 keys"
eq "$(j '.root.children[0].keys[9].pos[1]' "$D")" 2.5 "second KEYS chunk"
eq "$(j '.root.seqs|map(.name)|join(",")' "$D")" "idle,fire" "SEQS"
eq "$(j '.root.children[1].unknown[0]' "$D")" "XTRA" "unknown chunk kept"
eq "$(j '.brus[1].rgba[1]' "$D")" "0.8999999761581421" "float32 values exact"
eq "$(j '.root.mesh.surfaces[1].tris|length' "$D")" 6 "TRIS per brush"
eq "$(j '.root.mesh.verts.uv|length' "$D")" 8 "uvs"
has "$(head -c 30 vm.json)" '{"texs": [{"file": ' "json.dump separators"
u b3d dump fx/unit.glb x.json
eq "$CODE" 1 "not a b3d"; has "$ERR" "not a Blitz3D file" "not a b3d message"

# ================================================================ b3d anim (vm_anim_data.py)
u b3d anim vm.json vm.luau --scale 0.06 --name Test
eq "$CODE" 0 "b3d anim"
eq "$OUT" '{"nodes": 4, "static": 3, "bytes": 182, "luau_chars": 1113}' "anim summary"
L=$(cat vm.luau)
eq "$(sed -n 1p vm.luau)" "-- GENERATED by vm_anim_data.py from the Test model's keyframes; keep the model's credit and licence with it." "header"
has "$L" $'\tScale = 0.06,\n\tFps = 30.0,\n\tFirst = 1,\n\tLast = 10,\n\tPosQ = 16000.0,' "module fields"
has "$L" $'\t\tidle = { 1, 5 },\n\t\tfire = { 6, 10 },' "seqs"
has "$L" $'\t\t{ "arm", 0, { -0.060000, 0.120000, 0.180000 }, { -0.000000, 0.000000, 0.000000, 1.000000 }, false },' "arm row"
has "$L" $'\t\t{ "weaponfire", 2, { -0.000000, 0.000000, 1.021800 },' "leaf row (parent index)"
has "$L" '"Scene_Root_1", 0,' "unique names"
sed -n 's/^\tBlob = "\(.*\)",$/\1/p' vm.luau | base64 -d > blob.bin
eq "$(wc -c < blob.bin | tr -d ' ')" 182 "blob decodes to 182 bytes"
eq "$(od -An -t d2 -N 14 blob.bin | xargs)" "-960 1920 2880 0 0 0 32767" "frame 1 of arm as int16"
eq "$(tail -c 1 vm.luau | od -An -c | tr -d ' ')" '\n' "ends with a newline"
jq 'del(.root.seqs)' vm.json > noseq.json
u b3d anim noseq.json x.luau --scale 0.06 --name Test
eq "$CODE" 1 "anim without seqs"; has "$ERR" "has no seqs" "seqs hint"

# ================================================================ b3d build (vm_build.py -> glTF)
u b3d build vm.json build --scale 0.06 --tex arms=fx/arms.png --tex-dir fx
eq "$CODE" 0 "b3d build"
eq "$OUT" 'VMBUILD {"bones": 4, "meshes": [["arms", 6, 7, 2], ["gun", 6, 7, 2]], "frames": [1, 10], "files": ["build/vm.glb", "build/vm.preview.glb"]}' "VMBUILD line"
gj() { node -e 'const b=require("fs").readFileSync(process.argv[1]);if(b.toString("latin1",0,4)!=="glTF"||b.readUInt32LE(4)!==2||b.readUInt32LE(8)!==b.length)process.exit(3);process.stdout.write(b.subarray(20,20+b.readUInt32LE(12)).toString())' "$1"; }
G=$(gj build/vm.glb); eq "$?" 0 "GLB header"
eq "$(j '.skins[0].joints|length' "$G")" 5 "joints: rig + 4 nodes"
eq "$(j '.nodes[1].name + "," + .nodes[5].name' "$G")" "Scene_Root,Scene_Root_1" "node names"
eq "$(j '.meshes|length' "$G")" 2 "a mesh per brush"
eq "$(j '.images|length' "$G")" 2 "--tex and --tex-dir textures embedded"
eq "$(j '.materials[1].alphaMode' "$G")" "MASK" "masked texture flag 4"
eq "$(j '.animations|length' "$G")" 0 "bind pose only"
eq "$(j '.accessors[0].min|map(.*1000|round)|join(",")' "$G")" "-180,-180,600" "positions: (-x, y, z) x scale"
eq "$(j '.scenes[0].nodes|length' "$G")" 3 "skinned meshes at the scene root"
GP=$(gj build/vm.preview.glb)
eq "$(j '.animations[0].channels|length' "$GP")" 2 "preview animation: translation + rotation of the keyed node"
eq "$(j '.animations[0].name' "$GP")" "vm_all" "animation name"
u render3d build/vm.glb rb --preset turntable --canvas 64 --headings 2 --samples 1
eq "$CODE" 0 "the built GLB renders"
[ "$(j .covered "$(st rb/idle_00_000.png)")" -gt 50 ] && ok || bad "built GLB has visible geometry"

# ================================================================ b3d render (vm_render.py)
u b3d render vm.json frames 74 1 5 10 --tex arms=fx/arms.png --tex-dir fx
eq "$CODE" 0 "b3d render"
has "$OUT" "rendered 3 frames" "render summary"
SF=$(st frames/f001.png)
eq "$(j '"\(.w)x\(.h)"' "$SF")" "640x360" "viewmodel frame size"
eq "$(j .min_alpha "$SF")" 255 "opaque frames"
cmp -s frames/f001.png frames/f010.png && bad "keyframes don't move the skin" || ok
u b3d render vm.json frames2 74 3 --size 320x180
eq "$(j '"\(.w)x\(.h)"' "$(st frames2/f003.png)")" "320x180" "--size"

# ================================================================ upk (unpack_upk.py)
u upk unpack fx/lzo.upk out.upk
eq "$CODE" 0 "upk unpack"
eq "$(sed -n 1p <<<"$OUT")" "version=801/30 compressionFlags=2@0x79 chunks=1 table@0x81 uncompressed=187" "summary line"
has "$OUT" "wrote out.upk 187 (set CompressionFlags@0x79 to 0 for UELib" "wrote line"
eq "$(tail -c 42 out.upk)" "$(cat fx/UPK_EXPECTED)" "LZO1X literal runs + M1/M2/M3 matches"
cmp -s <(head -c 145 out.upk) <(head -c 145 fx/lzo.upk) && ok || bad "summary region copied"
eq "$(od -An -t u4 -j $((0x79)) -N 4 out.upk | tr -d ' ')" 2 "flags kept without --clear-flags"
u upk unpack fx/lzo.upk out2.upk --clear-flags
eq "$(od -An -t u4 -j $((0x79)) -N 8 out2.upk | xargs)" "0 1" "--clear-flags zeroes only the flags dword"
cp out.upk out3.upk; u upk clear-flags out3.upk 0x79
eq "$CODE" 0 "clear-flags"; cmp -s out2.upk out3.upk && ok || bad "clear-flags = --clear-flags"
cp out.upk out4.upk; u upk clear-flags out4.upk 121
cmp -s out2.upk out4.upk && ok || bad "decimal offset"
u upk clear-flags out4.upk 0xzz
eq "$CODE" 1 "bad offset"
u upk unpack fx/zlib.upk x.upk
eq "$CODE" 1 "zlib refused"; has "$ERR" "only LZO (2) is handled here" "zlib message"
u upk unpack fx/bad.upk x.upk
eq "$CODE" 1 "corrupt stream"; has "$ERR" "LZO error" "corrupt message"
u upk unpack fx/unit.glb x.upk
eq "$CODE" 1 "not UE3"; has "$ERR" "not a UE3 package (tag 46546C67)" "tag message"
{ printf '\xc1\x83\x2a\x9e'; head -c 1200 /dev/zero; } > notable.upk
u upk unpack notable.upk x.upk
eq "$CODE" 1 "no table"; has "$ERR" "could not locate the compressed chunk table" "no table message"

done_ "modder/3d"

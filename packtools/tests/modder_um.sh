#!/usr/bin/env bash
# modder/um: the built-in C++ port of universal-modder's `um` CLI (Rehan and universal-modder contributors, MIT).
# Mirrors the offline parts of upstream tests/test_um.py: scan fingerprints, publish check, backup round trip,
# the knowledge base (new / check / search / index), sprites, IWI and weapon files, and the guards of the
# online groups (they must fail cleanly without keys or servers, and never call out from this suite).
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export UM_HOME="$TMP/umh"
unset FAL_KEY SPRIXEN_API_KEY VOIDLESS_API_KEY UM_FAKE_WSL
export HOME="$TMP/home"; mkdir -p "$HOME"   # no keyring / key files of the person running the tests
u() { run um "$@"; }

# ---------------------------------------------------------------- top level
u --help
eq "$CODE" 0 "help"
for g in scan fal comfy sprixen voidless sprite render3d video win backup publish kb fastfile iwi weapon b3d upk sld vpk vtf; do has "$OUT" "    $g " "help lists $g"; done
has "$OUT" "render3d  render a 3D model (GLB)" "render3d is ported (built-in renderer)"
hasnt "$OUT" "not in this C++ port" "nothing marked as not ported"
u --version
has "$OUT" "universal-modder" "version"

# ---------------------------------------------------------------- scan
mkdir -p u/Game_Data/Managed && printf MZ > u/UnityPlayer.dll && printf MZ > u/Game_Data/Managed/Assembly-CSharp.dll
{ head -c 20 /dev/zero; printf '2022.3.21f1\0'; head -c 100 /dev/zero; } > u/Game_Data/globalgamemanagers
printf 'Studio\nCoolGame' > u/Game_Data/app.info
u scan u --json
eq "$CODE" 0 "scan unity"
eq "$(jq -r .engine.key <<<"$OUT")" "unity-mono" "unity mono"
eq "$(jq -r .engine.version <<<"$OUT")" "2022.3.21f1" "unity version"
eq "$(jq -r .engine.product <<<"$OUT")" "CoolGame" "unity product"
mkdir -p il/Game_Data/il2cpp_data/Metadata && printf MZ > il/UnityPlayer.dll && printf MZ > il/GameAssembly.dll && printf '\xaf\x1b\xb1\xfa' > il/Game_Data/il2cpp_data/Metadata/global-metadata.dat
u scan il --json
eq "$(jq -r .engine.key <<<"$OUT")" "unity-il2cpp" "unity il2cpp"
mkdir g && printf MZ > g/game.exe && { printf GDPC; printf '\x02\0\0\0\x04\0\0\0\x02\0\0\0\x01\0\0\0'; } > g/game.pck
u scan g --json
eq "$(jq -r .engine.key <<<"$OUT")" "godot" "godot"
has "$(jq -r .engine.version <<<"$OUT")" "4.2.1" "godot version"
mkdir -p ue/Proj/Binaries/Win64 ue/Proj/Content/Paks
{ printf MZ; head -c 5000 /dev/zero; printf '++UE5+Release-5.3' | sed 's/./&\x00/g'; head -c 100 /dev/zero; } > ue/Proj/Binaries/Win64/Proj-Win64-Shipping.exe
printf x > ue/Proj/Content/Paks/Proj-Windows.pak; printf x > ue/Proj/Content/Paks/Proj-Windows.utoc
u scan ue --json
eq "$(jq -r .engine.key <<<"$OUT")" "unreal" "unreal"
eq "$(jq -r .engine.engine_version <<<"$OUT")" "UE5+Release-5.3" "unreal version"
eq "$(jq -r .engine.iostore <<<"$OUT")" "true" "unreal iostore"
u scan u
has "$OUT" "engine:    Unity (Mono)" "scan text"
has "$OUT" "anti-cheat: none found" "scan anti-cheat line"

# ---------------------------------------------------------------- publish check
mkdir -p mod/src game/data
printf 'int FUN''_00401000();\n// Decompiled with ''ILSpy' > mod/src/Mod.cs
echo "My mod, built with dnSpy notes" > mod/README.md
printf 'FAL''_KEY=abcdefghijklmnopqrstuvwxyz0123' > mod/config.txt
head -c 4096 /dev/zero | tr '\0' x > game/data/big.bin; cp game/data/big.bin mod/copied.bin
u publish check mod --game game
eq "$CODE" 1 "publish check fails"
has "$OUT" "game file copied verbatim: copied.bin" "copied game file"
has "$OUT" "FAL_KEY assignment in config.txt" "secret"
has "$OUT" "Ghidra auto-name x1 in src/Mod.cs" "ghidra name"
has "$OUT" "decompiler header x1 in src/Mod.cs" "decompiler header"
hasnt "$OUT" "README.md" "dnSpy mention in prose is fine"
rm mod/copied.bin mod/config.txt mod/src/Mod.cs; echo "public class Mod {}" > mod/src/Mod.cs
u publish check mod --game game
eq "$CODE" 0 "clean mod passes"

# ---------------------------------------------------------------- backup
mkdir -p src/sub; echo v1 > src/save.dat; echo a=1 > src/sub/cfg.ini
u backup create src --name t
eq "$CODE" 0 "backup create"
echo v2 > src/save.dat; rm src/sub/cfg.ini; echo new > src/extra.log
u backup diff t src
eq "$(jq -c '[.changed,.removed,.added]' <<<"$OUT")" '[["save.dat"],["sub/cfg.ini"],["extra.log"]]' "backup diff"
u backup restore t --to src
eq "$CODE" 1 "restore without --yes refuses"
has "$ERR" "re-run with --yes" "restore asks for --yes"
eq "$(cat src/save.dat)" "v2" "nothing touched without --yes"
u backup restore t --to src --clean --yes
eq "$CODE" 0 "restore"
eq "$(cat src/save.dat)" "v1" "restored file"
eq "$(cat src/sub/cfg.ini)" "a=1" "restored deleted file"
[ ! -e src/extra.log ] && ok || bad "--clean removes new files"
u backup list
has "$OUT" "t-pre-restore" "state before the restore kept"

# ---------------------------------------------------------------- kb
mkdir knowledge && cp "$FIX/modder/TEMPLATE.md" knowledge/
u kb new --root knowledge --game "Hades II" --title "A new boon god" --agent "Codex (gpt-6)" --route loader-api
eq "$CODE" 0 "kb new"
P=knowledge/games/hades-ii/a-new-boon-god.md
[ -f "$P" ] && ok || bad "kb new writes the note"
u kb check "$P" --root knowledge
eq "$CODE" 1 "a fresh scaffold fails"
has "$OUT" "unfilled template text" "scaffold reason"
sed -i.bak 's/FILL IN: exact build/1.0.1 (Steam)/; s/anti_cheat: FILL IN/anti_cheat: none/; s/> Two to four sentences: what you built/> Added a boon god via a Lua mod loader/; s/The most valuable section. Numbered; each one symptom → cause → fix.//; s/1\. \*\*Symptom\.\*\* What you saw\. \*\*Cause:\*\* what it really was\. \*\*Fix:\*\* what worked\./1. **Boons never offered.** **Cause:** pool cached at load. **Fix:** register before the run starts./' "$P"
rm -f "$P.bak"   # -i.bak: GNU and BSD sed both take it
u kb check "$P" --root knowledge
eq "$CODE" 0 "filled note passes"
# an apostrophe inside a plain scalar of a multi-line flow list (a real note in the knowledge base has one)
cat > fm.txt <<'EOF2'
tools: [UELib, the core's collision library (StaticMapTree),
  a debugger, 'it''s quoted', "a \"b\""]
EOF2
sed -i.bak '1r fm.txt' "$P"; rm -f "$P.bak"
u kb check "$P" --root knowledge
eq "$CODE" 0 "apostrophe inside a multi-line flow list"
u kb search boon --root knowledge
has "$OUT" "games/hades-ii/a-new-boon-god.md" "kb search finds it"
u kb search boon --route native-hook --root knowledge
has "$OUT" "nothing in knowledge matches" "route filter"
u kb index --root knowledge
has "$OUT" "1 notes" "kb index"
[ -f knowledge/INDEX.md ] && [ -f knowledge/index.json ] && ok || bad "index files"
printf -- '---\ntitle: x\n---\nsk-ant-api03-%s\n' "$(printf 'a%.0s' $(seq 40))" > knowledge/bad.md
u kb check knowledge/bad.md --root knowledge
eq "$CODE" 1 "kb check rejects a secret"

# ---------------------------------------------------------------- sprite (needs ImageMagick to draw fixtures)
if command -v convert >/dev/null 2>&1; then
    convert -size 64x48 xc:white -fill 'rgb(200,30,30)' -draw 'rectangle 20,10 39,29' -fill white -draw 'point 30,20' w.png
    u sprite cutout w.png c.png
    eq "$CODE" 0 "cutout"
    eq "$(identify -format '%wx%h' c.png)" "20x20" "cutout trims"
    eq "$(convert c.png -format '%[pixel:p{10,10}]' info:)" "srgba(255,255,255,1)" "interior white kept opaque"
    eq "$(convert c.png -format '%[pixel:p{0,0}]' info:)" "srgba(200,30,30,1)" "cutout colour"
    u sprite fit c.png f.png --size 10x10 --anchor bottom
    eq "$(identify -format '%wx%h' f.png)" "10x10" "fit size"
    for i in 0 1 2 3 4; do convert -size 8x8 "xc:rgb($((i*40)),0,0)" "fr$i.png"; done
    u sprite sheet sh.png fr0.png fr1.png fr2.png fr3.png fr4.png --cols 3
    eq "$(identify -format '%wx%h' sh.png)" "24x16" "sheet size"
    u sprite slice sh.png sl --frame 8x8
    eq "$(ls sl | wc -l | tr -d ' ')" "5" "slice skips empty cells"
    # ---------------------------------------------------------------- iwi round trip
    convert -size 16x16 gradient:red-blue g.png
    u iwi png2iwi g.png g.iwi --format dxt1
    eq "$CODE" 0 "png2iwi"
    u iwi iwi2png g.iwi g2.png
    eq "$(identify -format '%wx%h' g2.png)" "16x16" "iwi2png size"
else
    echo "  (sprite/iwi checks skipped: ImageMagick not installed)"
fi

# ---------------------------------------------------------------- weapon
mkdir -p raw/weapons/sp
printf 'WEAPONFILE\\displayName\\WEAPON_COLT\\damage\\40\\clipSize\\8\\fireTime\\0.1' > raw/weapons/sp/colt
u weapon get raw/weapons/sp/colt damage
has "$OUT" "40" "weapon get"
u weapon set raw/weapons/sp/colt damage=500 clipSize=16 -o raw/weapons/sp/colt_mod
eq "$CODE" 0 "weapon set"
u weapon get raw/weapons/sp/colt_mod clipSize
has "$OUT" "16" "weapon set wrote the copy"
u weapon get raw/weapons/sp/colt damage
has "$OUT" "40" "the original is untouched"
u weapon diff raw/weapons/sp/colt raw/weapons/sp/colt_mod
has "$OUT" "damage" "weapon diff"

# ---------------------------------------------------------------- video (needs ffmpeg)
if command -v ffmpeg >/dev/null 2>&1 && command -v ffprobe >/dev/null 2>&1; then
    ffmpeg -nostdin -v error -y -f lavfi -i testsrc=size=320x240:rate=30:duration=3 -f lavfi -i sine=frequency=440:duration=3 -shortest -pix_fmt yuv420p clip.mp4
    u video probe clip.mp4
    eq "$CODE" 0 "video probe"
    eq "$(jq -c "[.width,.height,(.fps|floor),.audio]" <<<"$OUT")" "[320,240,30,true]" "probe size, fps, audio"
    u video contact clip.mp4 sheet.png --every 1
    eq "$CODE" 0 "contact sheet"
    [ -s sheet.png ] && ok || bad "contact sheet written"
    # the title fonts are built in (Library packs are text-only) and written with their OFL texts on first use
    for f in SpaceGrotesk-Bold.ttf SpaceGrotesk-Medium.ttf JetBrainsMono-Bold.ttf OFL-SpaceGrotesk.txt OFL-JetBrainsMono.txt; do
        [ -s "$UM_HOME/fonts/$f" ] && ok || bad "embedded font written: $f"
    done
    eq "$(head -c 4 "$UM_HOME/fonts/SpaceGrotesk-Bold.ttf" | od -An -tx1 | tr -d ' ')" "00010000" "font is a TrueType file"
else
    echo "  (video checks skipped: ffmpeg not installed)"
fi

# ---------------------------------------------------------------- guards of the online / platform groups
u win ps
eq "$CODE" 1 "win refuses off Windows/WSL"
has "$ERR" "drives Windows games (native Windows or WSL)" "win platform message"
u fal key status
has "$OUT" "no fal key configured" "fal key status without a key"
u comfy status --url http://127.0.0.1:9
eq "$CODE" 1 "comfy without a server"
has "$ERR" "no ComfyUI at http://127.0.0.1:9" "comfy message"
u sprixen credits
eq "$CODE" 1 "sprixen without a key"
has "$ERR" "no Sprixen key" "sprixen message"
u voidless credits
eq "$CODE" 1 "voidless without a key"
has "$ERR" "no Voidless key" "voidless message"

done_ "modder/um"

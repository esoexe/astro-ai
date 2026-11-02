#!/usr/bin/env bash
# console-anywhere/targets: the Doctor, Plan, Guard, Package, RecompJson and Docs cases of
# console-skill/tests/test_anywhere.py through the CLI, plus exact-output checks. The upstream FakeProbe becomes a JSON
# file in PACKTOOLS_TARGETS_PROBE (system, machine, tools, runs, paths, env, files). Synthesized files come from perl
# (the upstream elf_header / fake_iso), and the generated launch scripts are run with /bin/sh as upstream does.
source "$(dirname "$0")/lib.sh"
LIBSKILL="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-console/skills/console-anywhere"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
D=(--skill-dir "$LIBSKILL" console-anywhere/targets)
j()  { echo "$OUT" | jq -c "$1"; }
jf() { jq -c "$2" "$1"; }
# doctor with a fake probe: dr <probe.json> [args...]
dr() { local f=$1; shift; OUT=$(PACKTOOLS_TARGETS_PROBE="$f" $T "${D[@]}" doctor "$@" 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err); }
# field of the check named $2 in target $1
ck() { echo "$OUT" | jq -c --arg t "$1" --arg n "$2" ".targets[\$t][] | select(.name==\$n) | .$3"; }
names() { echo "$OUT" | jq -c --arg t "$1" '[.targets[$t][].name]'; }
elfh() { mkdir -p "$(dirname "$1")"; perl -e 'print "\x7fELF", chr($ARGV[1]), "\x01\x01", "\0" x 11, pack("v", $ARGV[0]), "\0" x 44, $ARGV[2]' "$2" "${3:-1}" "${4:-}" > "$1"; }
iso()  { mkdir -p "$(dirname "$1")"; perl -e 'my $d = "\0" x 0x9400; substr($d, $ARGV[0], 5) = "CD001"; print $d' "$2" > "$1"; }
put()  { mkdir -p "$(dirname "$1")"; printf '%b' "$2" > "$1"; }
pad()  { mkdir -p "$(dirname "$1")"; perl -e 'print $ARGV[0], "\0" x $ARGV[1]' "$2" "$3" > "$1"; }

# ================================================================ Doctor
MAC_RUNS='{"xcode-select -p": [0, "/Library/Developer/CommandLineTools\n"], "cmake --version": [0, "cmake version 3.29.2\n"], "pkg-config --exists": [0, ""]}'
echo "{\"tools\": [\"clang++\", \"cmake\", \"git\", \"ninja\", \"pkg-config\"], \"runs\": $MAC_RUNS}" > mac.json
dr mac.json --target macos --json; eq "$CODE" 0 "mac all good rc"
eq "$(j '[.targets.macos[] | select(.required and (.ok|not))] | length')" 0 "mac all required ok"
eq "$(ck macos 'Apple silicon: sse2neon' required)" false "sse2neon is a note"
eq "$(ck macos 'cmake 3.21+' found)" '"3.29.2"' "cmake found version"
eq "$(j .host)" '{"system":"Darwin","machine":"arm64"}' "fake host"
eq "$(names macos)" '["Xcode Command Line Tools","clang++ (C++20)","Apple silicon: sse2neon","cmake 3.21+","git","ninja","FFmpeg libraries (pkg-config)"]' "mac check order"
eq "$(j '.targets.macos[0]')" '{"name":"Xcode Command Line Tools","ok":true,"required":true,"found":"/Library/Developer/CommandLineTools","fix":"xcode-select --install"}' "check fields"
dr mac.json --target macos; eq "$CODE" 0 "mac text rc"
eq "$OUT" "doctor: this machine is Darwin arm64

macos: macOS (Apple silicon or Intel)
  ok      Xcode Command Line Tools  (/Library/Developer/CommandLineTools)
  ok      clang++ (C++20)  (/usr/bin/clang++)
  ok      Apple silicon: sse2neon  (CMake fetches sse2neon automatically on arm64)
  ok      cmake 3.21+  (3.29.2)
  ok      git  (/usr/bin/git)
  ok      ninja  (/usr/bin/ninja)
  ok      FFmpeg libraries (pkg-config)  (found)

all required tools found" "mac text exact"

echo '{"tools": ["clang++", "cmake", "git"], "runs": {"xcode-select -p": [0, "/x\n"], "cmake --version": [0, "cmake version 3.16.3\n"], "pkg-config --exists": [0, ""]}}' > macold.json
dr macold.json --target macos --json; eq "$CODE" 1 "old cmake rc"
eq "$(ck macos 'cmake 3.21+' ok)" false "cmake 3.16 too old"
eq "$(ck macos 'cmake 3.21+' fix)" '"install a newer CMake (https://cmake.org/download/)"' "old cmake fix"
eq "$(ck macos 'pkg-config + FFmpeg libraries' ok)" false "ffmpeg missing"
eq "$(ck macos 'pkg-config + FFmpeg libraries' fix)" '"brew install pkg-config ffmpeg   (or configure with -DPS2X_ENABLE_FFMPEG=OFF: cutscenes then show stub frames)"' "ffmpeg fix exact"
eq "$(ck macos ninja required)" false "ninja optional"
eq "$(ck macos ninja fix)" '"brew install ninja"' "ninja brew hint"
dr macold.json --target macos
has "$OUT" "  MISSING cmake 3.21+  (3.16.3)
          -> install a newer CMake (https://cmake.org/download/)" "missing line + fix"
has "$OUT" "  note    ninja
          -> brew install ninja" "note line"
has "$OUT" "2 required item(s) missing" "missing count text"

echo '{"system": "Linux", "machine": "x86_64", "tools": ["c++", "cmake", "git", "pkg-config"], "runs": {"cmake --version": [0, "cmake version 3.28.3"], "pkg-config --exists libavcodec": [0, ""], "pkg-config --exists x11": [1, ""]}, "files": {"/etc/os-release": "ID=steamos\n"}}' > deck.json
dr deck.json --target steamdeck --json
eq "$(ck steamdeck "raylib's X11 and OpenGL headers" ok)" false "x11 headers missing"
has "$(ck steamdeck "raylib's X11 and OpenGL headers" fix)" "libx11-dev" "x11 fix"
has "$(ck steamdeck 'SteamOS: writable build environment' fix)" "distrobox" "steamos distrobox"
eq "$(ck steamdeck 'SteamOS: writable build environment' required)" false "distrobox optional"
eq "$(names steamdeck)" "[\"C++20 compiler (g++ 11+ or clang++ 14+)\",\"cmake 3.21+\",\"git\",\"ninja\",\"FFmpeg libraries (pkg-config)\",\"raylib's X11 and OpenGL headers\",\"SteamOS: writable build environment\"]" "deck check order"
eq "$(ck steamdeck 'C++20 compiler (g++ 11+ or clang++ 14+)' found)" '"/usr/bin/c++"' "c++ found"
eq "$(ck steamdeck git fix)" '""' "git found no fix"
eq "$(ck steamdeck ninja fix)" '"sudo apt install ninja-build  (Fedora: sudo dnf install ninja-build)"' "linux hint with Fedora"
dr deck.json --target linux --json
eq "$(names linux | jq -c 'map(select(startswith("SteamOS")))')" '[]' "steamos check only for the deck"
echo '{"system": "Linux", "machine": "aarch64", "tools": ["c++", "cmake"], "runs": {"cmake --version": [0, "cmake version 3.28.3"]}}' > arm.json
dr arm.json --target steamdeck --json
eq "$(ck steamdeck 'x86_64 CPU' ok)" false "aarch64 deck"
eq "$(ck steamdeck 'x86_64 CPU' found)" '"aarch64"' "cpu found"
eq "$(j '.targets.steamdeck[0].name')" '"x86_64 CPU"' "cpu check first"
eq "$(ck steamdeck 'pkg-config + FFmpeg libraries' fix)" '"sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev   (or configure with -DPS2X_ENABLE_FFMPEG=OFF: cutscenes then show stub frames)"' "linux ffmpeg fix, no Fedora"
eq "$(ck steamdeck "raylib's X11 and OpenGL headers" ok)" false "no pkg-config: x11 not ok"

echo '{}' > plainmac.json
for tn in "windows:Windows" "linux:Linux" "steamdeck:Proton"; do
  t=${tn%%:*}; needle=${tn#*:}
  dr plainmac.json --target "$t" --json
  eq "$(j ".targets.$t | length")" 1 "wrong host $t one check"
  eq "$(j ".targets.$t[0].ok")" false "wrong host $t not ok"
  has "$(j ".targets.$t[0].name + .targets.$t[0].fix")" "$needle" "wrong host $t says $needle"
done
dr plainmac.json --target linux --json
eq "$(j .targets.linux[0])" '{"name":"a Linux machine","ok":false,"required":true,"found":"","fix":"build on a Linux machine (or WSL2 on Windows)"}' "linux on mac exact"
dr plainmac.json --target steamdeck --json
eq "$(j .targets.steamdeck[0].fix)" '"build on Linux x86_64: a PC, a VM, WSL2 on Windows, or the Steam Deck itself in Desktop Mode inside distrobox; or run the Windows build under Proton"' "deck on mac exact"
echo '{"system": "Linux", "machine": "x86_64"}' > plainlin.json
dr plainlin.json --target macos --json
has "$(j .targets.macos[0].name)" "Mac" "mac on linux"

cat > win.json <<'EOF'
{"system": "Windows", "machine": "amd64", "tools": ["cmake", "git"],
 "runs": {"cmake --version": [0, "cmake version 3.30.0"], "vswhere.exe -latest": [0, "C:\\VS\\2022\\Community\n"]},
 "paths": ["C:\\Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"],
 "env": {"ProgramFiles(x86)": "C:\\Program Files (x86)"}}
EOF
dr win.json --target windows --json
eq "$(ck windows 'MSVC (Visual Studio 2022, Desktop development with C++)' ok)" true "msvc via vswhere"
eq "$(ck windows 'MSVC (Visual Studio 2022, Desktop development with C++)' found)" '"C:\\VS\\2022\\Community"' "vswhere path stripped"
eq "$(names windows)" '["cmake 3.21+","git","MSVC (Visual Studio 2022, Desktop development with C++)","clang-cl","ninja"]' "windows check order"
eq "$CODE" 0 "windows rc"
sed 's/"env": {[^}]*}/"env": {"ProgramFiles(x86)": "D:\\\\PF"}/' win.json > win2.json
dr win2.json --target windows --json
eq "$(ck windows 'MSVC (Visual Studio 2022, Desktop development with C++)' ok)" false "vswhere elsewhere"
eq "$CODE" 1 "no msvc rc"
echo '{"system": "Windows", "machine": "amd64"}' > winbare.json
dr winbare.json --target windows --json
eq "$(ck windows 'cmake 3.21+' fix)" '"winget install Kitware.CMake"' "windows cmake winget"

cat > android.json <<'EOF'
{"tools": ["java", "gradle", "adb"],
 "runs": {"java -version": [0, "openjdk version \"17.0.9\" 2023-10-17\n"], "gradle --version": [0, "\nGradle 8.9\n"]},
 "paths": ["/sdk/platforms/android-34", "/sdk/ndk/28.2.13676358", "/sdk/cmake/3.22.1"], "env": {"ANDROID_HOME": "/sdk"}}
EOF
dr android.json --target android --json; eq "$CODE" 0 "android all good rc"
eq "$(j '[.targets.android[] | select(.ok|not)] | length')" 0 "android all ok"
eq "$(ck android 'JDK 17' found)" '"Java 17"' "java 17 found"
eq "$(ck android 'Gradle 8.7+ (or Android Studio)' found)" '"Gradle 8.9"' "gradle found"
eq "$(ck android 'Android SDK' found)" '"/sdk"' "sdk found"
eq "$(names android)" '["JDK 17","Gradle 8.7+ (or Android Studio)","Android SDK","SDK platform 34","NDK 28.2.13676358 (pinned in android/app/build.gradle)","CMake 3.22.1 from the SDK","adb (to install and push files)"]' "android check order"
cat > androidold.json <<'EOF'
{"tools": ["java", "gradle"], "runs": {"java -version": [0, "java version \"1.8.0_292\"\n"], "gradle --version": [0, "Gradle 8.2.1\n"]}, "env": {}}
EOF
dr androidold.json --target android --json; eq "$CODE" 1 "old android rc"
eq "$(ck android 'JDK 17' ok)" false "java 8 not ok"
eq "$(ck android 'JDK 17' found)" '"Java 8"' "java 1.8 is 8"
eq "$(ck android 'Gradle 8.7+ (or Android Studio)' ok)" false "gradle 8.2 old"
eq "$(ck android 'Gradle 8.7+ (or Android Studio)' found)" '"Gradle 8.2.1"' "gradle old found"
eq "$(ck android 'Android SDK' ok)" false "no sdk"
eq "$(names android)" '["JDK 17","Gradle 8.7+ (or Android Studio)","Android SDK","adb (to install and push files)"]' "no sdk: no sdk sub-checks"
cat > androidpart.json <<'EOF'
{"tools": ["java"], "runs": {"java -version": [0, "openjdk version \"17\""]}, "env": {"ANDROID_SDK_ROOT": "/sdk"}, "paths": ["/Applications/Android Studio.app"]}
EOF
dr androidpart.json --target android --json
eq "$(ck android 'Gradle 8.7+ (or Android Studio)' ok)" true "studio counts"
eq "$(ck android 'Gradle 8.7+ (or Android Studio)' found)" '"Android Studio"' "studio found"
eq "$(ck android 'NDK 28.2.13676358 (pinned in android/app/build.gradle)' ok)" false "ndk missing"
has "$(ck android 'NDK 28.2.13676358 (pinned in android/app/build.gradle)' fix)" 'ndk;28.2.13676358' "ndk fix"
eq "$(ck android 'JDK 17' ok)" true "java 17 bare"
echo '{"env": {"HOME": "/h"}, "paths": ["/h/Android/Sdk", "/h/Android/Sdk/platform-tools/adb"]}' > androidhome.json
dr androidhome.json --target android --json
eq "$(ck android 'Android SDK' found)" '"/h/Android/Sdk"' "sdk under HOME"
eq "$(ck android 'adb (to install and push files)' found)" '"/h/Android/Sdk/platform-tools/adb"' "adb from the sdk"
eq "$(ck android 'JDK 17' fix)" '"install Android Studio (bundles a JDK) or JDK 17"' "no java fix"

echo '{"system": "Linux", "machine": "x86_64", "tools": ["cmake", "curl", "unzip", "bash", "nproc"], "env": {}}' > vita.json
dr vita.json --target vita --json
eq "$(ck vita 'VITASDK set' ok)" false "no vitasdk"
cat > vitaready.json <<'EOF'
{"system": "Linux", "machine": "x86_64", "tools": ["cmake", "curl", "unzip", "bash", "nproc", "arm-vita-eabi-gcc"], "env": {"VITASDK": "/usr/local/vitasdk"},
 "paths": ["/usr/local/vitasdk/share/vita.toolchain.cmake", "/usr/local/vitasdk/arm-vita-eabi/include/SDL2/SDL.h", "/usr/local/vitasdk/arm-vita-eabi/include/gpu_es4/psp2_pvr_hint.h"]}
EOF
dr vitaready.json --target vita --json; eq "$CODE" 0 "vita ready rc"
eq "$(j '[.targets.vita[] | select(.required and (.ok|not))] | length')" 0 "vita ready all ok"
eq "$(names vita)" '["VITASDK set","VitaSDK toolchain file","arm-vita-eabi-gcc in PATH","GL stack from vita/setup.sh (SDL2 + PVR headers)","cmake","curl","unzip","bash","nproc (used by setup.sh and build.sh)","ninja"]' "vita check order"
echo '{"tools": ["cmake"], "env": {}}' > vitamac.json
dr vitamac.json --target vita --json
has "$(ck vita 'nproc (used by setup.sh and build.sh)' fix)" "coreutils" "nproc fix"
eq "$(ck vita 'GNU install (setup.sh uses install -D -t)' required)" false "gnu install note"
eq "$(ck vita curl fix)" '"brew install curl"' "curl brew"
echo '{"system": "Windows", "machine": "amd64", "env": {}}' > vitawin.json
dr vitawin.json --target vita --json
eq "$(j '.targets.vita[0].name')" '"Linux or WSL"' "vita on windows"

echo '{"tools": ["git", "cmake", "ninja", "clang++", "python3", "bash"], "runs": {"cmake --version": [0, "cmake version 3.29.0"], "bash --version": [0, "GNU bash, version 3.2.57(1)-release"]}}' > web.json
dr web.json --target web --json
eq "$(ck web 'bash 4+ (macOS ships 3.2)' ok)" false "bash 3.2"
eq "$(ck web 'bash 4+ (macOS ships 3.2)' found)" '"3.2"' "bash found"
has "$(ck web 'bash 4+ (macOS ships 3.2)' fix)" "brew install bash" "bash fix"
eq "$(ck web '7-Zip (7zz)' ok)" false "no 7zz"
eq "$(ck web Emscripten ok)" true "emscripten note ok"
eq "$(ck web 'cmake 3.20+' ok)" true "web cmake 3.20"
eq "$(names web)" '["git","cmake 3.20+","ninja","C++20 compiler (clang preferred)","python3","bash 4+ (macOS ships 3.2)","7-Zip (7zz)","Emscripten"]' "web check order"

dr mac.json --json; eq "$CODE" 0 "default target rc"
eq "$(j '.targets | keys_unsorted')" '["macos"]' "default target is the host"
dr mac.json --target all; eq "$CODE" 1 "all rc"
for t in windows macos linux steamdeck android vita web; do has "$OUT" "$t: " "all lists $t"; done
has "$OUT" "MISSING" "all has MISSING"; has "$OUT" "-> " "all has fixes"
dr mac.json --target all --json
eq "$(j '.targets | keys_unsorted')" '["windows","macos","linux","steamdeck","android","vita","web"]' "all order"
dr plainmac.json --target linux --target linux --json
eq "$(j .missing)" 2 "repeated target counted twice"
eq "$(j '.targets | keys_unsorted')" '["linux"]' "repeated target listed once"
dr plainmac.json --target linux --target macos --target linux --json
eq "$(j '.targets | keys_unsorted')" '["linux","macos"]' "first position kept"
run "${D[@]}" doctor --target gamecube; eq "$CODE" 2 "doctor bad target"
run "${D[@]}" doctor --json; [[ "$CODE" == 0 || "$CODE" == 1 ]] && ok || bad "real doctor rc $CODE"
has "$(j 'keys')" "targets" "real doctor json"
eq "$(j '.host.system')" "\"$(uname -s)\"" "real host system"

# ================================================================ Plan
for t in windows macos linux steamdeck android vita web; do
  run "${D[@]}" plan --target "$t" --elf-name SLUS_203.12; eq "$CODE" 0 "plan $t rc"
  has "$OUT" "plan: " "plan $t header"
  hasnt "$OUT" "—" "plan $t no em dash"
  hasnt "$OUT" "(SLUS_203.12 below" "plan $t elf given: no hint"
  run "${D[@]}" plan --target "$t" --json
  eq "$(j '(.steps|length) > 0 and (.sources|length) > 0')" true "plan $t json steps+sources"
done
for ts in windows:5:2 macos:5:3 linux:5:2 steamdeck:5:2 android:6:5 vita:6:5 web:4:1; do
  IFS=: read -r t ns nsrc <<<"$ts"
  run "${D[@]}" plan --target "$t" --json
  eq "$(j '[(.steps|length), (.sources|length)]')" "[$ns,$nsrc]" "plan $t counts"
done
run "${D[@]}" plan --target windows --elf-name SLUS_203.12
has "$OUT" "git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git" "win clone"
has "$OUT" "cmake -S . -B out/build" "win configure"
has "$OUT" "cmake --build out/build --config Release --target ps2EntryRunner" "win build"
has "$OUT" "PS2X_SHOW_WINDOWS_CONSOLE=OFF" "win console flag"
has "$OUT" '     $ out\build\ps2xRuntime\Release\ps2EntryRunner.exe C:\games\mygame\SLUS_203.12' "win try it backslashes"
has "$OUT" 'saves go to mc0\ beside it.' "win mc0 backslash"
has "$OUT" "from: README.md (Build, Requirements); ps2xRuntime/CMakeLists.txt (runner target, FFmpeg DLLs)" "win sources"
has "$OUT" "Players always supply their own disc. Nothing from the game goes in a package." "plan footer"
run "${D[@]}" plan --target macos --elf-name SLUS_203.12
has "$OUT" "sse2neon" "mac sse2neon"; has "$OUT" "-DPS2X_ENABLE_FFMPEG=OFF" "mac ffmpeg off"; has "$OUT" "--target ps2EntryRunner" "mac target"
run "${D[@]}" plan --target macos --json
eq "$(j '.steps[3].notes | length')" 2 "mac Gatekeeper note appended to Try it"
has "$(j '.steps[3].notes[1]')" "com.apple.quarantine" "gatekeeper note"
eq "$(j '.steps[3].commands')" '["./out/release/ps2xRuntime/ps2EntryRunner ~/games/mygame/SLUS_203.12"]' "default elf in json"
run "${D[@]}" plan --target linux --elf-name SLUS_203.12 --repo "~/src/PS2Recomp"
has "$OUT" "cd ~/src/PS2Recomp" "linux repo"; has "$OUT" "libavcodec-dev" "linux packages"
run "${D[@]}" plan --target linux --build-dir out/dev --json
has "$(j '.steps[2].notes[0]')" "(out/dev, the one in console/STATE.md)" "build dir in note"
run "${D[@]}" plan --target steamdeck --elf-name SLUS_203.12
has "$OUT" "Add a Non-Steam Game" "deck steam"; has "$OUT" "Proton" "deck proton"
has "$OUT" 'put game\SLUS_203.12 in its Launch Options' "deck proton elf"
run "${D[@]}" plan --target android --elf-name SLUS_203.12
has "$OUT" "gradle wrapper --gradle-version 8.9" "android wrapper"
has "$OUT" "./gradlew assembleRelease -Pps2xBootElf=/storage/emulated/0/Android/data/com.ps2x.runner/files/SLUS_203.12" "android boot elf"
has "$OUT" "android/app/build/outputs/apk/release/app-release.apk" "android apk"
has "$OUT" "adb shell mkdir -p /storage/emulated/0/Android/data/com.ps2x.runner/files" "android mkdir"
has "$OUT" "28.2.13676358" "android ndk"; has "$OUT" "ps2_android_runtime.cpp" "android runtime"
has "$OUT" '     $ adb push "path/to/your/extracted/disc/." /storage/emulated/0/Android/data/com.ps2x.runner/files/' "android push"
run "${D[@]}" plan --target vita --elf-name SLUS_203.12
has "$OUT" "./vita/setup.sh" "vita setup"; has "$OUT" "./vita/build.sh ux0:data/ps2x/SLUS_203.12" "vita build"
has "$OUT" "CLEAN=1" "vita clean"; has "$OUT" "RANJ00001" "vita title id"
has "$OUT" "plan: PS Vita (homebrew)

1. What you need
     - A PS Vita that already runs homebrew, with VitaShell.
     - A Linux machine or WSL2: vita/setup.sh uses GNU install -D and nproc (on macOS put GNU coreutils first in PATH, or use a Linux VM).

2. Install VitaSDK
     \$ export VITASDK=/usr/local/vitasdk
     \$ export PATH=\$VITASDK/bin:\$PATH
     - https://vitasdk.org has the installer (vdpm)." "vita text exact start"
has "$OUT" "6. Package it
     \$ python3 targets.py package --runner outvita/ps2xRuntime/ps2EntryRunner.vpk --target vita --elf-name SLUS_203.12 --out console/dist/mygame-vita

from: vita/setup.sh; vita/build.sh" "vita last step has no notes"
run "${D[@]}" plan --target web
has "$OUT" "second port of the same disc" "web second port"; has "$OUT" "scripts/port.sh mygame" "web port.sh"
has "$OUT" "scripts/serve.py build/web-mygame" "web serve"; has "$OUT" "WebGPU" "web webgpu"; has "$OUT" "--native-only" "web native"
hasnt "$OUT" "(SLUS_203.12 below" "web: no elf hint"
run "${D[@]}" plan --target vita
has "$OUT" "(SLUS_203.12 below stands for your disc's ELF name: pass --elf-name)" "elf hint without --elf-name"
run "${D[@]}" plan --target vita --elf-name ""
has "$OUT" "(SLUS_203.12 below stands" "empty elf name = not given"
has "$OUT" "./vita/build.sh ux0:data/ps2x/SLUS_203.12" "empty elf name falls back"
run "${D[@]}" plan --target gamecube; eq "$CODE" 2 "plan bad target"
run "${D[@]}" plan; eq "$CODE" 2 "plan needs --target"
run "${D[@]}" plan --target web --json
eq "$(j '.title')" '"web browser (snuri00/ps2-web-recomp, a separate toolchain)"' "web title"
eq "$(j '.steps[0] | keys_unsorted')" '["title","commands","notes"]' "step keys"
eq "$(j 'keys_unsorted')" '["target","title","steps","sources"]' "plan keys"

# ================================================================ Guard
iso dist/backup.dat 32769
iso dist/raw.dat 37657
put dist/disc.iso ""
pad dist/disc.chd.part "MComprHD" 100
pad dist/disc.cso.part "CISO" 100
elfh dist/main.dat 8
put dist/SLUS_203.12 "anything"
put dist/MODULES/PADMAN.IRX "x"
put dist/SYSTEM.CNF 'BOOT2 = cdrom0:\\\\SLUS_203.12;1\n'
put dist/src/runner/sub_00100000.cpp "// code\n"
put dist/register_functions.cpp ""
put dist/gen.cpp "#include <ps2_recompiled_functions.h>\n"
put dist/game/stuff.bin.txt "x"
run "${D[@]}" guard dist --json; eq "$CODE" 1 "guard finds game data"
eq "$(j '[.refused[].path] | sort')" '["MODULES/PADMAN.IRX","SLUS_203.12","SYSTEM.CNF","backup.dat","disc.chd.part","disc.cso.part","disc.iso","game/stuff.bin.txt","gen.cpp","main.dat","raw.dat","register_functions.cpp","src/runner/sub_00100000.cpp"]' "guard paths"
rs() { echo "$OUT" | jq -r --arg p "$1" '.refused[] | select(.path==$p) | .reason'; }
for pn in "backup.dat:ISO 9660" "raw.dat:ISO 9660" "disc.iso:disc image" "disc.chd.part:CHD" "disc.cso.part:CSO" "main.dat:MIPS" \
          "SLUS_203.12:main ELF" "MODULES/PADMAN.IRX:IRX" "SYSTEM.CNF:SYSTEM.CNF" "src/runner/sub_00100000.cpp:runner/" \
          "register_functions.cpp:recompiled" "gen.cpp:recompiled" "game/stuff.bin.txt:inside game/"; do
  has "$(rs "${pn%%:*}")" "${pn#*:}" "reason ${pn%%:*}"
done
eq "$(j '[.refused[].path]')" '["SLUS_203.12","SYSTEM.CNF","backup.dat","disc.chd.part","disc.cso.part","disc.iso","gen.cpp","main.dat","raw.dat","register_functions.cpp","MODULES/PADMAN.IRX","game/stuff.bin.txt","src/runner/sub_00100000.cpp"]' "walk order: files, then sorted folders"
eq "$(rs disc.iso)" "a disc image file (.iso)" "disc reason exact"
eq "$(rs main.dat)" "a PS2 ELF or IRX (32-bit MIPS)" "mips reason exact"
eq "$(rs SLUS_203.12)" "named like a disc's main ELF (SLUS_203.12)" "serial reason exact"
eq "$(rs gen.cpp)" "recompiled game code (references the generated function table)" "marker reason exact"
eq "$(rs register_functions.cpp)" "recompiled game code (register_functions.cpp)" "recomp name exact"
eq "$(rs MODULES/PADMAN.IRX)" "an IOP module (.IRX) from the disc" "irx exact"
eq "$(j '[.folder, .ok, .allowed]')" '["dist",false,[]]' "guard json fields"

elfh ok/ps2EntryRunner 62 2
elfh ok/libraylib.so.5 62 2
put ok/patches/SLUS-20312_1A2B3C4D.pnach "patch=1,EE,00100000,word,00000000\n"
put ok/patches/gold_rifle.cpp '#include "game_overrides.h"\n'
put ok/README.md "# my mod\n"
put ok/game/PUT-YOUR-GAME-HERE.txt "put it here"
put ok/.console-dist.json "{}"
run "${D[@]}" guard ok --json; eq "$CODE" 0 "own files rc"
eq "$(j .refused)" '[]' "own files left alone"
run "${D[@]}" guard ok; eq "$OUT" "guard: ok
  ok: nothing that looks like game data" "guard ok text"

mkdir -p big; truncate -s $((512*1024*1024+1)) big/huge.dat; truncate -s $((512*1024*1024)) big/exact.dat
run "${D[@]}" guard big --json
eq "$(j '[.refused[] | [.path, .reason]]')" '[["huge.dat","over 512 MB: only disc images and dumps are that big"]]' "big file"
rm -rf big

put d/config.dat "my own settings"; put d/disc.iso ""
printf '# reviewed\nconfig.dat  # my own settings file, not from the disc\n' > allow.txt
run "${D[@]}" guard d --allow allow.txt --json
eq "$(j '[.refused[].path]')" '["disc.iso"]' "allow file: disc still refused"
eq "$(j .allowed)" '[]' "config.dat not flagged at all"
iso d/config.dat 32769
run "${D[@]}" guard d --allow allow.txt --json
eq "$(j .allowed)" '[{"path":"config.dat","reason":"an ISO 9660 disc image","allowed_because":"my own settings file, not from the disc"}]' "allowed entry"
run "${D[@]}" guard d --allow allow.txt
eq "$OUT" "guard: d
  REFUSED  disc.iso: a disc image file (.iso)
  allowed  config.dat: an ISO 9660 disc image (allow file: my own settings file, not from the disc)
Nothing from the game may be packaged or published: the disc, its ELF and IRX modules, its assets and the recompiled runner/ code stay on your machine." "guard text exact"
printf 'config.dat\n' > bad.txt
run "${D[@]}" guard d --allow bad.txt; eq "$CODE" 2 "allow without reason"
eq "$ERR" "error: bad.txt line 1: every allowed path needs a reason: '<path or pattern>  # why'" "allow reason error"
printf '#only a comment\n   # \nconfig.dat #\n' > bad2.txt
run "${D[@]}" guard d --allow bad2.txt; eq "$CODE" 2 "allow empty reason"
eq "$ERR" "error: bad2.txt line 3: expected '<path or pattern>  # why'" "allow empty reason error"
printf '  # why only\n' > bad3.txt
run "${D[@]}" guard d --allow bad3.txt; eq "$CODE" 1 "comment-only line skipped (no usage error)"
printf '\r\n x  #  y \r\n #c\r\n\xef\xbb\xbfz # w\n' > crlf.txt
run "${D[@]}" guard d --allow crlf.txt; eq "$CODE" 1 "crlf allow file parses"
printf 'a # b\n\xff\n' > bin.txt
run "${D[@]}" guard d --allow bin.txt; eq "$CODE" 2 "non-utf8 allow file"
eq "$ERR" "error: cannot read bin.txt: 'utf-8' codec can't decode byte 0xff in position 6: invalid start byte" "utf8 error text"
run "${D[@]}" guard d --allow d; eq "$CODE" 2 "allow file is a folder"
eq "$ERR" "error: cannot read d: [Errno 21] Is a directory: 'd'" "folder allow error"
run "${D[@]}" guard d --allow none.txt; eq "$CODE" 2 "missing allow file"
eq "$ERR" "error: no such allow file: none.txt" "missing allow text"
run "${D[@]}" guard nope; eq "$CODE" 2 "guard missing folder"
eq "$ERR" "error: not a folder: nope" "not a folder text"
run "${D[@]}" guard allow.txt; eq "$CODE" 2 "guard on a file"
run "${D[@]}" guard nope --allow none.txt; eq "$ERR" "error: not a folder: nope" "folder checked before the allow file"
# fnmatch rules: * crosses /, [seq], [!seq], ?, a backslash pattern becomes /
mkdir -p fm/sub/deep; put fm/a.iso ""; put fm/b.iso ""; put fm/sub/deep/c.iso ""; put fm/x1.iso ""; put fm/'[q].iso' ""
printf '*b.iso # b\nsub/* # deep too\nx?.iso # one char\n[[]q].iso # bracket\n' > fm.txt
run "${D[@]}" guard fm --allow fm.txt --json
eq "$(j '[.refused[].path]')" '["a.iso"]' "fnmatch refused"
eq "$(j '[.allowed[] | [.path, .allowed_because]]')" '[["[q].iso","bracket"],["b.iso","b"],["x1.iso","one char"],["sub/deep/c.iso","deep too"]]' "fnmatch allowed"
printf '[!b].iso # not b\nsub\\deep\\c.iso # backslashes\n' > fm2.txt
run "${D[@]}" guard fm --allow fm2.txt --json
eq "$(j '[.allowed[].path]')" '["a.iso","sub/deep/c.iso"]' "negated class + backslash rule"
printf '[a-c].iso # range\n[.iso # literal bracket\n' > fm3.txt
run "${D[@]}" guard fm --allow fm3.txt --json
eq "$(j '[.allowed[].path]')" '["a.iso","b.iso"]' "range class"
printf 'A.ISO # case\n' > fm4.txt
run "${D[@]}" guard fm --allow fm4.txt --json
eq "$(j '.allowed')" '[]' "fnmatchcase is case-sensitive"
# names and extensions
mkdir -p nm; put nm/.iso ""; put nm/x.ISO ""; put nm/sles-123.45 ""; put nm/SLUS_20312 ""; put nm/sles_123.45\;1 ""; put nm/Runner.h ""
mkdir -p nm/RUNNER; put nm/RUNNER/a.hh ""; put nm/runnerx/b.c ""; put nm/system.cnf ""
run "${D[@]}" guard nm --json
eq "$(j '[.refused[].path]')" '["SLUS_20312","sles-123.45","sles_123.45;1","system.cnf","x.ISO","RUNNER/a.hh"]' "names, case and leading-dot rules"
eq "$(j '.refused[4].reason')" '"a disc image file (.iso)"' "extension shown lower-case"
# ELF classes and endianness
elfh el/m64.o 8 2; perl -e 'print "\x7fELF\x01\x02\x01", "\0" x 11, "\x00\x08", "\0" x 44' > el/mbe.o
elfh el/short 8 1; head -c 19 el/short > el/short19.o; rm el/short
run "${D[@]}" guard el --json
eq "$(j '[.refused[].path]')" '["mbe.o"]' "64-bit MIPS ignored, big-endian MIPS caught, short header ignored"
# symlinks: a linked folder is listed but not entered; a broken link is a file of size 0
mkdir -p sl/real; put sl/real/disc.iso ""; ln -s real sl/link; ln -s nowhere sl/broken.iso
run "${D[@]}" guard sl --json
eq "$(j '[.refused[].path]')" '["broken.iso","real/disc.iso"]' "symlink rules"

# ================================================================ Package
P() { run "${D[@]}" package "$@"; }
elfh build/ps2EntryRunner 62 2 '_libavcodec.so.61_'; chmod +x build/ps2EntryRunner
elfh build/libextra.so.1 62 2
put build/CMakeCache.txt "not shipped"
P --runner build/ps2EntryRunner --target linux --out console/dist/mygame-linux --game "My Game" --serial SLUS_203.12 --status playable
eq "$CODE" 0 "linux package rc"
O=console/dist/mygame-linux
eq "$(ls -A $O | LC_ALL=C sort | tr '\n' ' ')" ".console-dist.json .recomp.json PLAYERS.md game libextra.so.1 play.sh ps2EntryRunner " "linux files"
eq "$(ls -A $O/game)" "PUT-YOUR-GAME-HERE.txt" "game folder holds only the placeholder"
[ -x $O/play.sh ] && ok || bad "play.sh executable"
[ -x $O/ps2EntryRunner ] && ok || bad "runner executable"
PL=$(cat $O/PLAYERS.md)
hasnt "$PL" "{{" "PLAYERS.md rendered"; has "$PL" "My Game" "PLAYERS.md game"; has "$PL" "SLUS-20312" "PLAYERS.md serial"
has "$PL" "FFmpeg" "PLAYERS.md ffmpeg"; has "$PL" "Never download a disc image" "PLAYERS.md warning"; hasnt "$PL" "—" "PLAYERS.md no em dash"
has "$PL" "Also: FFmpeg's libraries: sudo apt install ffmpeg (or your distro's ffmpeg package)." "needs ffmpeg"
has "$PL" '- `ps2EntryRunner`
- `libextra.so.1`
- `play.sh`' "files list"
eq "$(jf $O/.recomp.json .status)" '"playable"' "recomp status"
eq "$(jf $O/.recomp.json .original)" '{"region":"USA","serial":"SLUS-20312"}' "recomp original"
M=$O/.console-dist.json
eq "$(jf $M .target)" '"linux"' "manifest target"
eq "$(jf $M '[.tool, .runner, .elf, .files, .allowed]')" '["console-anywhere targets.py","ps2EntryRunner",null,[".recomp.json","PLAYERS.md","libextra.so.1","play.sh","ps2EntryRunner"],[]]' "manifest fields"
eq "$(jf $M 'keys_unsorted')" '["tool","target","runner","elf","files","allowed","created"]' "manifest key order"
[[ "$(jq -r .created $M)" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]] && ok || bad "created stamp format"
has "$OUT" "for your own devices" "package text"
has "$OUT" "packaged $TMP/console/dist/mygame-linux for Linux (x86_64)
  .recomp.json
  PLAYERS.md
  libextra.so.1
  play.sh
  ps2EntryRunner
  game/  (empty: the player puts their own extracted disc here)" "package text exact"
eq "$(ls console/dist | grep -c staging)" 0 "no staging left"
cmp -s build/ps2EntryRunner $O/ps2EntryRunner && ok || bad "runner copied byte for byte"
eq "$(stat -c %Y build/libextra.so.1)" "$(stat -c %Y $O/libextra.so.1)" "copy2 keeps mtime"
eq "$(cat $O/game/PUT-YOUR-GAME-HERE.txt)" "Put the files from YOUR OWN disc here (the whole extracted disc: SYSTEM.CNF, the main ELF, the folders).
See PLAYERS.md. Nothing from the game ships with this folder." "placeholder text"

printf '#!/bin/sh\necho "runner got: $1"\n' > build/ps2EntryRunner; chmod +x build/ps2EntryRunner
O=console/dist/deck
P --runner build/ps2EntryRunner --target steamdeck --out $O; eq "$CODE" 0 "deck package rc"
has "$OUT" "warning: build/ps2EntryRunner is not a recognised executable; check it is the runner built for steamdeck" "not recognised warning"
R=$(/bin/sh $O/play.sh 2>&1 >/dev/null); eq "$?" 1 "play.sh without a disc"
has "$R" "Put your extracted disc in game/" "play.sh asks for a disc"
printf 'BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > $O/game/SYSTEM.CNF
R=$(/bin/sh $O/play.sh 2>&1 >/dev/null); has "$R" "game/SLUS_203.12 not found" "play.sh reads SYSTEM.CNF"
: > $O/game/SLUS_203.12
eq "$(/bin/sh $O/play.sh)" "runner got: game/SLUS_203.12" "play.sh runs the runner"
mkdir -p $O/game/DATA; : > $O/game/DATA/MAIN.ELF
printf 'BOOT2=cdrom0:\\DATA\\MAIN.ELF;1\n' > $O/game/SYSTEM.CNF
eq "$(/bin/sh $O/play.sh)" "runner got: game/DATA/MAIN.ELF" "play.sh subfolder ELF"
P --runner build/ps2EntryRunner --target steamdeck --out $O --replace; eq "$CODE" 2 "replace refuses a game folder with files"
has "$ERR" "is ever deleted" "game folder error"
eq "$ERR" "error: $TMP/$O/game/ holds files (your disc, or saves in mc0/): move them somewhere safe first, or package to a new --out. Nothing in game/ is ever deleted" "game folder error exact"
[ -f $O/game/SYSTEM.CNF ] && ok || bad "disc kept"
rm -rf $O/game/DATA $O/game/SYSTEM.CNF $O/game/SLUS_203.12
P --runner build/ps2EntryRunner --target steamdeck --out $O --replace; eq "$CODE" 0 "replace after emptying"
eq "$(ls -A $O/game)" "PUT-YOUR-GAME-HERE.txt" "game folder after replace"
eq "$(sed -n 4p $O/play.sh)" 'ELF=""' "no elf baked"
eq "$(sed -n 11p $O/play.sh)" "  # BOOT2 = cdrom0:\\SLUS_203.12;1  ->  SLUS_203.12 (a subfolder becomes DIR/NAME)" "play.sh comment backslash"
eq "$(sed -n 13p $O/play.sh)" "        | tr '\\\\' '/' | sed -e 's#^/*##' -e 's/[[:space:]]*\$//')" "play.sh tr line"
eq "$(tail -n1 $O/play.sh)" 'exec "./ps2EntryRunner" "game/$ELF" "$@"' "play.sh exec line"

O=console/dist/baked
P --runner build/ps2EntryRunner --target linux --out $O --elf-name SCES_500.51; eq "$CODE" 0 "baked rc"
: > $O/game/SCES_500.51
eq "$(/bin/sh $O/play.sh)" "runner got: game/SCES_500.51" "baked elf name"
has "$(cat $O/game/PUT-YOUR-GAME-HERE.txt)" "the main ELF SCES_500.51, the folders" "placeholder names the elf"
P --runner build/ps2EntryRunner --target linux --out console/dist/x --elf-name ../SLUS_203.12; eq "$CODE" 2 "elf name with a path"
eq "$ERR" "error: --elf-name is just the ELF's file name, like SLUS_203.12" "elf name error"
P --runner build/ps2EntryRunner --target linux --out console/dist/x --elf-name .hidden; eq "$CODE" 2 "elf name with a dot"

pad build/Release/ps2EntryRunner.exe "MZ" 100
pad build/Release/avcodec-61.dll "MZ" 10
put build/Release/notes.txt "x"
O=console/dist/win
P --runner build/Release/ps2EntryRunner.exe --target windows --out $O --elf-name SLUS_203.12; eq "$CODE" 0 "windows rc"
[ -f $O/avcodec-61.dll ] && ok || bad "dll copied"
[ -f $O/notes.txt ] && bad "notes.txt copied" || ok
BAT=$(od -An -c $O/play.bat | tr -d ' \n')
has "$BAT" '\r\n' "bat crlf"
has "$(cat $O/play.bat)" '"%~dp0ps2EntryRunner.exe" "game\%ELF%" %*' "bat run line"
has "$(cat $O/play.bat)" 'set "ELF=SLUS_203.12"' "bat elf"
has "$(cat $O/play.bat)" 'tokens=1,* delims=\' "bat delims"
has "$(cat $O/play.bat)" "for /f \"tokens=1,* delims=\\\" %%A in ('findstr /b /i \"BOOT2\" \"game\\SYSTEM.CNF\"') do set \"ELF=%%B\"" "bat for line exact"
eq "$(tr -d '\r' < $O/play.bat | wc -l)" 23 "bat line count"
eq "$(tail -c 2 $O/play.bat | od -An -tx1 | tr -d ' ')" "0d0a" "bat ends with crlf"
has "$(cat $O/PLAYERS.md)" "play.bat" "players mentions play.bat"
has "$(cat $O/PLAYERS.md)" 'It starts ps2EntryRunner.exe with game\SLUS_203.12.' "windows howto"
has "$(cat $O/PLAYERS.md)" '`game\mc0\`' "windows saves"
eq "$(jf $O/.console-dist.json .files)" '["PLAYERS.md","avcodec-61.dll","play.bat","ps2EntryRunner.exe"]' "windows manifest files"

pad build/mac/ps2EntryRunner $'\xcf\xfa\xed\xfe' 60
O=console/dist/mac
P --runner build/mac/ps2EntryRunner --target macos --out $O; eq "$CODE" 0 "mac rc"
[ -x $O/play.command ] && ok || bad "play.command executable"
has "$(cat $O/PLAYERS.md)" "If macOS blocks it the first time" "mac howto"
has "$(cat $O/PLAYERS.md)" "game/<the ELF named in your disc's SYSTEM.CNF>" "elf shown default"
pad apk/app-release.apk $'PK\x03\x04' 30
O=console/dist/android
P --runner apk/app-release.apk --target android --out $O --elf-name SLUS_203.12; eq "$CODE" 0 "android rc"
[ -f $O/install-android.sh ] && ok || bad "install-android.sh"
[ -f $O/install-android.bat ] && ok || bad "install-android.bat"
[ -x $O/install-android.sh ] && ok || bad "install-android.sh executable"
PL=$(cat $O/PLAYERS.md)
has "$PL" "/storage/emulated/0/Android/data/com.ps2x.runner/files/SLUS_203.12" "android elf"
has "$PL" "mc0/\` on the device" "android saves"
has "$(cat $O/install-android.sh)" "grep -v 'PUT-YOUR-GAME-HERE.txt'" "install sh placeholder"
has "$(cat $O/install-android.sh)" 'adb install -r "app-release.apk" || exit 1' "install sh apk"
eq "$(tail -n1 $O/install-android.sh)" "adb push game/. /storage/emulated/0/Android/data/com.ps2x.runner/files/" "install sh push"
has "$(cat $O/install-android.bat)" 'adb push game\. /storage/emulated/0/Android/data/com.ps2x.runner/files/' "install bat push"
pad vpk/ps2EntryRunner.vpk $'PK\x03\x04' 30
O=console/dist/vita
P --runner vpk/ps2EntryRunner.vpk --target vita --out $O --elf-name SLUS_203.12; eq "$CODE" 0 "vita rc"
has "$(cat $O/PLAYERS.md)" "ux0:data/ps2x/SLUS_203.12" "vita elf"
eq "$(jf $O/.console-dist.json .files)" '["PLAYERS.md","ps2EntryRunner.vpk"]' "vita has no script"
P --runner vpk/ps2EntryRunner.vpk --target vita --out console/dist/vita2 --name "Cool Port"
has "$(cat console/dist/vita2/PLAYERS.md)" "Cool Port" "--name used"
has "$(cat console/dist/vita2/PLAYERS.md)" "ux0:data/ps2x/<the ELF name it was built with>" "vita elf default"

elfh build2/ps2EntryRunner 62 2; chmod +x build2/ps2EntryRunner
iso stuff/extra.dat 32769
O=console/dist/refused
P --runner build2/ps2EntryRunner --target linux --out $O --lib stuff/extra.dat; eq "$CODE" 1 "refuses game data"
has "$OUT" "package REFUSED: 1 file(s) look like game data" "refused text"
has "$OUT" "  REFUSED  extra.dat: an ISO 9660 disc image" "refused line"
[ -e $O ] && bad "refused out exists" || ok
eq "$(ls console/dist | grep -c staging)" 0 "no staging after refusal"
P --runner build2/ps2EntryRunner --target linux --out $O --lib stuff/extra.dat --json; eq "$CODE" 1 "refusal with --json"
has "$OUT" "package REFUSED" "refusal stays text with --json"
elfh stuff/SLUS_203.12 8
P --runner build2/ps2EntryRunner --target linux --out $O --lib stuff/SLUS_203.12; eq "$CODE" 1 "refuses a mips lib"
put build2/PADMAN.IRX "x"
P --runner build2/ps2EntryRunner --target linux --out $O; eq "$CODE" 0 "irx next to the runner is not copied"
[ -e $O/PADMAN.IRX ] && bad "irx copied" || ok

put mine/settings.dat "my own"
iso mine/settings.dat 32769
printf 'settings.dat  # my own settings, made by me\n' > allow2.txt
O=console/dist/allowed
P --runner build2/ps2EntryRunner --target linux --out $O --lib mine/settings.dat --allow allow2.txt; eq "$CODE" 0 "allow file lets a false positive through"
eq "$(jf $O/.console-dist.json '.allowed[0].allowed_because')" '"my own settings, made by me"' "allowed in manifest"
has "$OUT" "allowed  settings.dat: an ISO 9660 disc image (allow file: my own settings, made by me)" "allowed text"

elfh build2/game.elf 8
pad build2/mac/ps2EntryRunner $'\xcf\xfa\xed\xfe' 60
O=console/dist/e
pe() { P --out $O "$@"; eq "$CODE" 2 "usage: $*"; [[ "$ERR" == "error: "* ]] && ok || bad "usage error prefix: $ERR"; }
pe --runner none --target linux
eq "$ERR" "error: runner not found: none (build it first: targets.py plan --target linux)" "runner not found text"
pe --runner build2/game.elf --target linux
eq "$ERR" "error: build2/game.elf is a PS2 (MIPS) ELF: that is the game, not the runner" "mips runner text"
pe --runner build2/ps2EntryRunner --target windows
eq "$ERR" "error: the Windows runner is ps2EntryRunner.exe" "windows runner text"
pe --runner build2/mac/ps2EntryRunner --target linux
eq "$ERR" "error: build2/mac/ps2EntryRunner looks like a macos binary, not a linux one" "wrong binary text"
pe --runner build2/ps2EntryRunner --target android
eq "$ERR" "error: the Android runner is the APK (android/app/build/outputs/apk/release/app-release.apk)" "apk text"
pe --runner build2/ps2EntryRunner --target vita
eq "$ERR" "error: the Vita runner is the .vpk that vita/build.sh makes" "vpk text"
pe --runner build2/ps2EntryRunner --target linux --game X
eq "$ERR" "error: --game and --serial go together (they fill .recomp.json)" "game+serial text"
pe --runner build2/ps2EntryRunner --target linux --lib nope.so
eq "$ERR" "error: --lib file not found: nope.so" "lib text"
pe --runner build2/ps2EntryRunner --target linux --game X --serial ABCD-12345
eq "$ERR" "error: cannot tell the region from ABCD-12345: pass --region" "region text"
pe --runner build2/ps2EntryRunner --target linux --allow none.txt
pe --runner build2 --target linux
eq "$ERR" "error: runner not found: build2 (build it first: targets.py plan --target linux)" "runner is a folder"
[ -e $O ] && bad "usage error left out folder" || ok
mkdir -p $O; put $O/somebody-else.txt "x"
P --runner build2/ps2EntryRunner --target linux --out $O; eq "$CODE" 2 "foreign folder"
eq "$ERR" "error: $TMP/$O is not empty and was not made by this tool; pick another --out" "foreign folder text"
put afile "x"
P --runner build2/ps2EntryRunner --target linux --out afile; eq "$ERR" "error: $TMP/afile exists and is not a folder" "out is a file"
mkdir -p console/dist/empty
P --runner build2/ps2EntryRunner --target linux --out console/dist/empty; eq "$CODE" 0 "empty folder is fine"
O=console/dist/twice
P --runner build2/ps2EntryRunner --target linux --out $O; eq "$CODE" 0 "first package"
P --runner build2/ps2EntryRunner --target linux --out $O; eq "$CODE" 2 "second needs --replace"
has "$ERR" "--replace" "replace hint"
P --runner build2/ps2EntryRunner --target linux --out $O --replace --json; eq "$CODE" 0 "replace json rc"
eq "$(j '[.ok, .out, .warnings, .manifest.target, .manifest.elf]')" "[true,\"$TMP/$O\",[],\"linux\",null]" "package json"
eq "$(j 'keys_unsorted')" '["ok","out","manifest","warnings"]' "package json keys"
P --runner build2/ps2EntryRunner --target web --out $O; eq "$CODE" 2 "web is not a package target"
elfh build3/runner-arm 183 2
P --runner build3/runner-arm --target linux --out console/dist/arm; eq "$CODE" 0 "arm64 linux runner accepted for linux"
P --runner build3/runner-arm --target steamdeck --out console/dist/arm2; eq "$CODE" 2 "arm64 not for the deck"
eq "$ERR" "error: build3/runner-arm looks like a arm64-linux binary, not a steamdeck one" "arm deck text"
elfh build3/elf-other 20 2
P --runner build3/elf-other --target linux --out console/dist/oth; eq "$ERR" "error: build3/elf-other looks like a elf binary, not a linux one" "other elf machine"
pad build3/zip.exe $'PK\x03\x04' 10
P --runner build3/zip.exe --target windows --out console/dist/zipw; eq "$ERR" "error: build3/zip.exe looks like a zip binary, not a windows one" "zip as windows"
pad build4/ps2EntryRunner.EXE "MZ" 10; elfh build4/lib.so.6 62 2; pad build4/x.DLL "MZ" 1
P --runner build4/ps2EntryRunner.EXE --target windows --out console/dist/upper; eq "$CODE" 0 "upper-case .EXE"
eq "$(jf console/dist/upper/.console-dist.json .files)" '["PLAYERS.md","play.bat","ps2EntryRunner.EXE","x.DLL"]' "dll match is case-insensitive, .so not for windows"
P --runner build2/ps2EntryRunner --target linux --out console/dist/libs --lib stuff/extra.dat --lib mine/settings.dat --allow allow2.txt
eq "$CODE" 1 "one bad lib refuses the package"
P --runner build2/ps2EntryRunner --target linux --out console/dist/meta --game "  Spaced  " --serial "slus-203.12;1" --region Mars --status released
eq "$(jf console/dist/meta/.recomp.json '[.game, .status, .original]')" '["Spaced","released",{"region":"Mars","serial":"SLUS-20312"}]' "package meta"
P --runner build2/ps2EntryRunner --target linux --out console/dist/nested/deeper/pkg; eq "$CODE" 0 "creates parent folders"

# ================================================================ RecompJson
run "${D[@]}" recomp-json --game "My Game" --serial SLUS_203.12 --out rj/.recomp.json; eq "$CODE" 0 "recomp rc"
eq "$(jf rj/.recomp.json 'keys_unsorted')" '["$schema","game","system","type","toolchain","status","original"]' "recomp keys"
eq "$(jf rj/.recomp.json '."$schema"')" '"https://recomp.fyi/schema/v1.json"' "schema"
eq "$(jf rj/.recomp.json '[.system, .type, .toolchain, .status]')" '["PS2","recomp","PS2Recomp","in-progress"]' "recomp fields"
eq "$(jf rj/.recomp.json .original)" '{"region":"USA","serial":"SLUS-20312"}' "recomp original"
eq "$OUT" "wrote rj/.recomp.json: My Game (SLUS-20312, USA), status in-progress
Optional file: PS2Recomp does not read it; recomp lists such as recomp.fyi do. Update the status when the project moves on." "recomp text"
eq "$(tail -c1 rj/.recomp.json | od -An -tx1 | tr -d ' ')" "0a" "trailing newline"
eq "$(sed -n 2p rj/.recomp.json)" '  "$schema": "https://recomp.fyi/schema/v1.json",' "indent 2"
rj() { run "${D[@]}" recomp-json --out r.json "$@"; }
rj --game G --serial "sces_500.51;1"; eq "$(jf r.json .original)" '{"region":"Europe","serial":"SCES-50051"}' "normalize sces"
rm r.json; rj --game G --serial SLPM-65432; eq "$(jf r.json .original.serial)" '"SLPM-65432"' "slpm kept"
rm r.json; rj --game G --serial SCES-50051 --status paused; eq "$(jf r.json .original.region)" '"Europe"' "europe"
rm r.json; rj --game G --serial SLPS_250.01 --status complete; eq "$(jf r.json .original.region)" '"Japan"' "japan"
rm r.json; rj --game G --serial game.iso; eq "$CODE" 2 "bad serial"; eq "$ERR" "error: 'game.iso' is not a disc serial like SLUS-20312 or SLUS_203.12" "bad serial text"
rj --game G --serial ABCD-12345 --status playable; eq "$CODE" 2 "unknown prefix"
rj --game G --serial ABCD-12345 --status playable --region USA; eq "$(jf r.json .original.region)" '"USA"' "region override"
rm r.json; rj --game "  " --serial SLUS-20312; eq "$ERR" "error: --game needs the title as it shipped" "blank game"
rj --game G --serial SLUS-20312 --status done; eq "$CODE" 2 "bad status"
rj --game G --serial "it's"; eq "$ERR" "error: \"it's\" is not a disc serial like SLUS-20312 or SLUS_203.12" "repr with quote"
rj --game G --serial SCKA-20001; eq "$(jf r.json .original.region)" '"Korea"' "korea"
rm r.json; rj --game G --serial scaj_200.01; eq "$(jf r.json .original.region)" '"Asia"' "asia"
rm r.json; rj --game G --serial "SLUS 20312"; eq "$CODE" 2 "space in serial"
echo '{"game": "Old", "status": "exploring", "links": {"repo": "https://github.com/me/port"}, "original": {"serial": "SLUS-20312", "region": "USA", "notes": "greatest hits"}}' > r.json
rj --game "My Game" --serial SLUS-20312 --status playable; eq "$CODE" 0 "update rc"
has "$OUT" "updated" "update text"
eq "$(jf r.json '[.status, .links.repo, .original.notes, .game]')" '["playable","https://github.com/me/port","greatest hits","My Game"]' "update keeps other fields"
eq "$(jf r.json 'keys_unsorted')" '["game","status","links","original","$schema","system","type","toolchain"]' "update key order"
eq "$(jf r.json '.original | keys_unsorted')" '["serial","region","notes"]' "original key order"
echo '{"links": {"download": "https://example.com/files/mygame.iso"}}' > r.json
rj --game G --serial SLUS-20312; eq "$CODE" 2 "link refused"
eq "$ERR" "error: refusing: 1 link(s) to a game file or archive (\$.links.download). Never put a game file, or a link to one, in .recomp.json" "link text"
echo '{"links": ["magnet:?xt=urn:btih:abc"], "x": "  HTTPS://a.b/c.ZIP?dl=1  ", "y": "https://a.b/c.zip/page", "z": [{"w": "ftp://h/x.7z"}]}' > r.json
rj --game G --serial SLUS-20312; eq "$CODE" 2 "magnet refused"
has "$ERR" "refusing: 3 link(s) to a game file or archive (\$.links[0], \$.x, \$.z[0].w)" "link paths"
printf '{broken' > r.json
rj --game G --serial SLUS-20312; eq "$CODE" 2 "broken json"
eq "$ERR" "error: r.json exists but is not valid JSON (Expecting property name enclosed in double quotes: line 1 column 2 (char 1)); fix or remove it" "broken json text"
echo '[1]' > r.json; rj --game G --serial SLUS-20312; eq "$ERR" "error: r.json exists but is not a JSON object" "not an object"
echo '{}' > r.json; rj --game G --serial SLUS-20312; eq "$CODE" 0 "empty object"; has "$OUT" "wrote r.json" "empty object counts as new"
rj --game G --serial SLUS-20312 --status done; eq "$CODE" 2 "bad status again"
mkdir -p rdir; run "${D[@]}" recomp-json --game G --serial SLUS-20312 --out rdir; eq "$ERR" "error: rdir exists but is not valid JSON ([Errno 21] Is a directory: 'rdir'); fix or remove it" "out is a folder"
run "${D[@]}" recomp-json --game G --serial SLUS-20312 --out new/sub/x.json; eq "$CODE" 0 "creates folders"
run "${D[@]}" recomp-json --serial SLUS-20312; eq "$CODE" 2 "--game required"
(cd rj && $T "${D[@]}" recomp-json --game G --serial SLUS-20312 >/dev/null && [ -f .recomp.json ]) && ok || bad "default --out .recomp.json"
printf '\xef\xbb\xbf{}' > r.json; rj --game G --serial SLUS-20312; eq "$CODE" 2 "BOM is not valid JSON"
printf '{"a": "caf\xc3\xa9"}' > r.json; rj --game $'Caf\xc3\xa9' --serial SLUS-20312; has "$(cat r.json)" '"a": "caf\u00e9"' "ensure_ascii"; has "$(cat r.json)" '"game": "Caf\u00e9"' "ensure_ascii game"

# ================================================================ Docs / CLI
run "${D[@]}"; eq "$CODE" 2 "no command rc"; has "$OUT" "usage: targets" "no command prints help"
for s in doctor plan package guard recomp-json; do run "${D[@]}" "$s" --help; eq "$CODE" 0 "$s --help"; done
run "${D[@]}" --help; has "$OUT" "Typical: targets.py doctor --target android" "epilog"
has "$OUT" "recomp-json" "subcommand listed"
done_ "console-anywhere/targets"

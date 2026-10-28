#!/usr/bin/env bash
# Build the custom Astro AI installer (Windows x64) for one edition.
# Usage: build_setup.sh <stage-dir> <out.exe> "<App Name>" <AppId> <Exe.exe> <keys:0|1> <work-dir>
#   <stage-dir> is the folder that gets installed (the same files as the portable zip).
set -euo pipefail
STAGE="$1"; OUT="$2"; APP="$3"; ID="$4"; EXE="$5"; KEYS="$6"; WORK="$7"
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
TP="$ROOT/desktop/third_party"
CXX=x86_64-w64-mingw32-g++-posix; CC=x86_64-w64-mingw32-gcc-posix; RC=x86_64-w64-mingw32-windres
mkdir -p "$WORK"
cat > "$WORK/edition.h" <<H
#pragma once
#define APP_NAME_W    L"$APP"
#define APP_NAME_A    "$APP"
#define APP_ID_W      L"$ID"
#define EXE_NAME_W    L"$EXE"
#define APP_VERSION_W L"1.4.0"
#define EDITION_KEYS  $KEYS
#define SETUP_PAYLOAD "$WORK/payload.zip"
#define SETUP_WV2     "$TP/webview2-bootstrapper/MicrosoftEdgeWebview2Setup.exe"
#define SETUP_ICON    "$ROOT/desktop/assets/astro.ico"
#define SETUP_LOGO    "$ROOT/desktop/assets/icon-64.png"
#define SETUP_UNINST  "$WORK/Uninstall.exe"
H
[ -f "$WORK/miniz.o" ] || $CC -O2 -c "$TP/miniz/miniz.c" -o "$WORK/miniz.o"
LIBS="-lgdiplus -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -ladvapi32 -luser32 -lgdi32"
# 1) the uninstaller: same program, no payload
(cd "$HERE" && $RC -I"$WORK" -I"$HERE" astro_setup.rc -O coff -o "$WORK/uninst_res.o")
$CXX -std=c++20 -O2 -municode -mwindows -I"$WORK" -I"$HERE" -I"$TP/miniz" -c "$HERE/astro_setup.cpp" -o "$WORK/astro_setup.o"
$CXX -o "$WORK/Uninstall.exe" "$WORK/astro_setup.o" "$WORK/miniz.o" "$WORK/uninst_res.o" -mwindows -municode -static -static-libgcc -static-libstdc++ $LIBS
x86_64-w64-mingw32-strip "$WORK/Uninstall.exe"
# 2) the installer: payload (app files + the uninstaller) + WebView2 bootstrapper
rm -f "$WORK/payload.zip"
(cd "$STAGE" && zip -qr9 -X "$WORK/payload.zip" .)
(cd "$WORK" && zip -q9 -X "$WORK/payload.zip" Uninstall.exe)
(cd "$HERE" && $RC -DWITH_PAYLOAD -I"$WORK" -I"$HERE" astro_setup.rc -O coff -o "$WORK/setup_res.o")
$CXX -o "$OUT" "$WORK/astro_setup.o" "$WORK/miniz.o" "$WORK/setup_res.o" -mwindows -municode -static -static-libgcc -static-libstdc++ $LIBS
x86_64-w64-mingw32-strip "$OUT"

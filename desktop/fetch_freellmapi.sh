#!/usr/bin/env bash
# Downloads the FreeLLMAPI router (MIT, https://github.com/tashfeenahmed/freellmapi) for the personal desktop
# build: its own portable Electron build, which A.S.T.R.O AI runs headless as Node (ELECTRON_RUN_AS_NODE=1)
# through desktop/freellmapi/astro-freellmapi.mjs. Pinned version, SHA-256 checked against the release.
#   bash desktop/fetch_freellmapi.sh [windows|linux]      -> .cache/freellmapi/<platform>/app
# The trimmed copy drops what a window-less Node run never loads (Chromium UI translations, native-module sources).
set -euo pipefail
cd "$(dirname "$0")/.."
PLAT="${1:-windows}"
VER="0.16.19"
BASE="https://github.com/tashfeenahmed/freellmapi/releases/download/v$VER"
case "$PLAT" in
    windows) FILE="FreeLLMAPI-$VER-win.zip";            SHA="173129da49512203772412e9293013df926eff3063ac2b1286e9aaaa316f2092";;
    linux)   FILE="freellmapi-desktop-$VER.tar.xz";     SHA="372d0dd000cf2c8078d61a7c92f09a45ea2c2d4ac30ec549946f8bf69298b5c8";;
    *) echo "usage: $0 [windows|linux]" >&2; exit 2;;
esac
DEST=".cache/freellmapi/$PLAT"
mkdir -p "$DEST"
if [ ! -f "$DEST/$FILE" ] || ! echo "$SHA  $DEST/$FILE" | sha256sum -c --quiet - 2>/dev/null; then
    curl -fL --retry 3 -o "$DEST/$FILE.part" "$BASE/$FILE"
    mv "$DEST/$FILE.part" "$DEST/$FILE"
fi
echo "$SHA  $DEST/$FILE" | sha256sum -c --quiet - || { echo "SHA-256 mismatch for $FILE" >&2; exit 1; }
rm -rf "$DEST/app" "$DEST/x"; mkdir -p "$DEST/x"
if [ "$PLAT" = windows ]; then unzip -q "$DEST/$FILE" -d "$DEST/x"; mv "$DEST/x" "$DEST/app"
else tar xf "$DEST/$FILE" -C "$DEST/x"; mv "$DEST/x/"*/ "$DEST/app"; rm -rf "$DEST/x"; fi
A="$DEST/app"
# trim: UI translations except en-US, the native module's C/C++ sources and build intermediates
find "$A/locales" -type f ! -name 'en-US.pak' -delete
BS="$A/resources/app.asar.unpacked/node_modules/better-sqlite3"
rm -rf "$BS/src" "$BS/deps" "$BS/build/deps" "$BS/build/Release/obj" "$BS/build/Release/obj.target" "$BS/build/Release/test_extension.node" \
       "$BS/build/Release/.deps" "$BS/build/"*.vcxproj* "$BS/build/"*.sln "$BS/build/"*.Makefile "$BS/build/Makefile" "$BS/build/config.gypi" 2>/dev/null || true
curl -fsL --retry 3 -o "$A/LICENSE.freellmapi.txt" "https://raw.githubusercontent.com/tashfeenahmed/freellmapi/v$VER/LICENSE"
grep -q "MIT License" "$A/LICENSE.freellmapi.txt" || { echo "could not fetch the FreeLLMAPI license" >&2; exit 1; }
cat > "$A/ASTRO-NOTE.txt" <<EOF
FreeLLMAPI $VER (MIT License, https://github.com/tashfeenahmed/freellmapi), unmodified release build:
$BASE/$FILE  (SHA-256 $SHA)
Trimmed for A.S.T.R.O AI: Chromium UI translations other than en-US and better-sqlite3 build sources were removed.
A.S.T.R.O AI runs it without a window (ELECTRON_RUN_AS_NODE=1) through ../astro-freellmapi.mjs.
EOF
echo "FreeLLMAPI $VER ($PLAT) ready in $A ($(du -sh "$A" | cut -f1))"

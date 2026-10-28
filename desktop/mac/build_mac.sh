#!/usr/bin/env bash
# Builds the Astro AI macOS app (universal: Apple Silicon + Intel) and a .dmg from this kit.
# Needs macOS 11+ and the Xcode Command Line Tools:  xcode-select --install
# Usage:  ./build_mac.sh            -> "<Name>.app" and "<Name>.dmg" next to this script
#         ARCHS="-arch arm64" ./build_mac.sh   (build for one architecture only)
set -euo pipefail
cd "$(dirname "$0")"

field() { sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" payload/edition.json | head -1; }
EDITION="$(field edition)"; NAME="$(field name)"
BUNDLE_ID="ai.astro.desktop"; [ "$EDITION" = "clean" ] && BUNDLE_ID="ai.astro.desktop.clean"
ARCHS="${ARCHS:--arch arm64 -arch x86_64}"
MIN="-mmacosx-version-min=11.0"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT

command -v clang++ >/dev/null || { echo "clang++ not found. Run: xcode-select --install"; exit 1; }
echo "==> Building the server (C++20)"
clang++ -std=c++20 -O2 $ARCHS $MIN -Isrc/server src/server/workspace_server.cpp -o "$W/astro-server" -lcurl
echo "==> Building the desktop window (WKWebView)"
clang++ -std=c++17 -O2 $ARCHS $MIN -Isrc/webview/include src/desktop/astro_desktop.cpp -o "$W/astro-desktop" -framework WebKit -framework Cocoa

echo "==> Icon"
ICONSET="$W/astro.iconset"; mkdir -p "$ICONSET"
# assets/icon-mac-1024.png already follows the macOS icon grid (rounded tile with margin).
for s in 16 32 128 256 512; do
    sips -z $s $s assets/icon-mac-1024.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) assets/icon-mac-1024.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$W/astro.icns"

echo "==> Bundling $NAME.app"
APP="$NAME.app"; rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$W/astro-desktop" "$APP/Contents/MacOS/astro-desktop"
cp "$W/astro-server" "$APP/Contents/Resources/astro-server"
cp "$W/astro.icns" "$APP/Contents/Resources/astro.icns"
cp -R payload/. "$APP/Contents/Resources/"
chmod 755 "$APP/Contents/MacOS/astro-desktop" "$APP/Contents/Resources/astro-server"
[ -f "$APP/Contents/Resources/seed-keys.env" ] && chmod 600 "$APP/Contents/Resources/seed-keys.env"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleExecutable</key><string>astro-desktop</string>
  <key>CFBundleIconFile</key><string>astro.icns</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.3.0</string>
  <key>CFBundleVersion</key><string>1.3.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSMicrophoneUsageDescription</key><string>Astro AI uses the microphone for voice input and Live voice chat.</string>
  <key>NSAppTransportSecurity</key><dict><key>NSAllowsLocalNetworking</key><true/></dict>
</dict></plist>
PLIST

if [ -n "${SIGN_IDENTITY:-}" ]; then
    # Developer ID signing (CI sets SIGN_IDENTITY from repository secrets). Hardened runtime is required for notarization.
    echo "==> Signing with ${SIGN_IDENTITY%% (*}…"
    codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP/Contents/Resources/astro-server"
    cat > "$W/ent.plist" <<'ENT'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>com.apple.security.device.audio-input</key><true/></dict></plist>
ENT
    codesign --force --options runtime --timestamp --entitlements "$W/ent.plist" --sign "$SIGN_IDENTITY" "$APP"
    codesign --verify --strict --verbose=1 "$APP"
else
    echo "==> Signing (ad-hoc, for this Mac)"
    codesign --force --deep --sign - "$APP" || echo "   (codesign failed; the app still runs when built on this Mac)"
fi

echo "==> Disk image"
rm -f "$NAME.dmg"
hdiutil create -volname "$NAME" -srcfolder "$APP" -ov -format UDZO "$NAME.dmg" >/dev/null && echo "   $NAME.dmg"
echo "Done: $(pwd)/$APP   (drag it to Applications, or open the .dmg)"

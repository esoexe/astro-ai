#!/usr/bin/env bash
# Builds the A.S.T.R.O AI desktop apps (display name; folders and data keep "A.S.T.R.O AI"), two editions:
#   with-keys/  "A.S.T.R.O AI"        - includes YOUR provider keys (.astro_keys.env) and gateway settings (.workspace.env)
#   clean/      "A.S.T.R.O AI Clean"  - no keys at all; a fresh app (keys can be added in Settings > Keys)
# Output: ../desktop-builds/{with-keys,clean}/
#   Windows: installer .exe + portable .zip      (cross-built with mingw-w64)
#   Linux:   .deb + portable .tar.gz              (glibc 2.35+: Ubuntu 22.04+, Debian 12+, Fedora 36+)
#   macOS:   source kit .tar.gz + build_mac.sh    (must be built on a Mac: Apple's toolchain)
# Needs (Debian/Ubuntu): g++ libwebkit2gtk-4.1-dev libcurl4-openssl-dev libssl-dev g++-mingw-w64-x86-64-posix zip
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VER="1.5.0"
OUT="$ROOT/desktop-builds"
WORK="$ROOT/.cache/pkg"
TP="$ROOT/desktop/third_party"
C="$TP/curl-win64"
rm -rf "$WORK"; mkdir -p "$WORK/bin" "$OUT"

echo "==> Linux binaries"
gcc -std=c11 -O2 -c desktop/src/linux_compat.c -o "$WORK/linux_compat.o"
COMPAT=("$WORK/linux_compat.o" -lm -static-libstdc++ -static-libgcc -Wl,--wrap=fmod -s)
g++ -std=c++20 -O2 -pthread workspace_server.cpp -o "$WORK/bin/astro-server" $(pkg-config --cflags --libs libcurl) -lcrypto -lutil "${COMPAT[@]}" &
g++ -std=c++17 -O2 -I"$TP/webview/include" desktop/src/astro_desktop.cpp -o "$WORK/bin/astro-desktop" \
    $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1) -ldl -pthread "${COMPAT[@]}"
wait
# The terminal app (astro): a small client of the engine's agent API
g++ -std=c++20 -O2 -pthread cli/astro.cpp -o "$WORK/bin/astro" $(pkg-config --cflags --libs libcurl) "${COMPAT[@]}"

echo "==> Windows binaries"
x86_64-w64-mingw32-gcc-posix -O2 -c desktop/src/win_compat.c -o "$WORK/win_compat.o"
(cd desktop/src && x86_64-w64-mingw32-windres astro_desktop.rc -O coff -o "$WORK/astro_desktop_res.o")
x86_64-w64-mingw32-g++-posix -std=c++20 -O2 -DCURL_STATICLIB -I"$C/include" workspace_server.cpp "$WORK/win_compat.o" \
    -o "$WORK/bin/astro-server.exe" -static -static-libgcc -static-libstdc++ -L"$C/lib" -lcurl -lnghttp3 -lngtcp2_crypto_libressl \
    -lngtcp2 -lnghttp2 -lssh2 -lssl -lcrypto -lpsl -lbrotlidec -lbrotlicommon -lzstd -lz -lws2_32 -lcrypt32 -lbcrypt -lwldap32 \
    -liphlpapi -lsecur32 -lnormaliz -ladvapi32 -luser32 -s  # sequential: two parallel mingw compiles need ~1.5 GB RAM
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wno-unknown-pragmas -I"$TP/mingw-compat" -I"$TP/webview/include" -I"$TP/webview2/include" \
    desktop/src/astro_desktop.cpp "$WORK/astro_desktop_res.o" -o "$WORK/bin/AstroAI.exe" -mwindows -static -static-libgcc -static-libstdc++ \
    -lole32 -lshell32 -lshlwapi -lversion -luser32 -ladvapi32 -lws2_32 -luuid -s
x86_64-w64-mingw32-g++-posix -std=c++20 -O2 -DCURL_STATICLIB -I"$C/include" cli/astro.cpp "$WORK/win_compat.o" -o "$WORK/bin/astro.exe" \
    -static -static-libgcc -static-libstdc++ -L"$C/lib" -lcurl -lnghttp3 -lngtcp2_crypto_libressl -lngtcp2 -lnghttp2 -lssh2 -lssl -lcrypto \
    -lpsl -lbrotlidec -lbrotlicommon -lzstd -lz -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -liphlpapi -lsecur32 -lnormaliz -ladvapi32 -luser32 \
    -lole32 -luuid -lshell32 -s
wait

# ------------------------------------------------------------------------------------------------
# Edition payloads (web app + registry + edition.json [+ keys])
# ------------------------------------------------------------------------------------------------
make_payload() {  # $1 = edition (personal|clean), $2 = dir
    local ed="$1" dir="$2"
    mkdir -p "$dir"
    cp -r web providers.json "$dir/"
    if [ "$ed" = personal ]; then
        echo '{"edition":"personal","name":"Astro AI","display":"A.S.T.R.O AI","data_name":"astro-ai","port":47611}' > "$dir/edition.json"
        # Your saved provider keys: copied into the app's data folder on first run.
        [ -f .astro_keys.env ] && install -m 600 .astro_keys.env "$dir/seed-keys.env"
        # Gateway settings (LLM_*), exported to the server on every launch. OpenCode password left out.
        if [ -f .workspace.env ]; then
            ( set -a; . ./.workspace.env; set +a
              umask 077
              {
                  echo "# A.S.T.R.O AI gateway settings (personal edition). Keep this file private."
                  env | grep -E '^LLM_' | grep -v '^LLM_CREDENTIALS_URL=' | sort
                  echo "LLM_CREDENTIALS_URL=${LLM_CREDENTIALS_URL:-${LLM_BASE_URL%/}/credentials}"
                  [ -n "${LLM_WAIT_SECONDS:-}" ] || echo "LLM_WAIT_SECONDS=60"
              } > "$dir/gateway.env" )
        fi
    else
        echo '{"edition":"clean","name":"Astro AI Clean","display":"A.S.T.R.O AI Clean","data_name":"astro-ai-clean","port":47612}' > "$dir/edition.json"
    fi
}

readme() {  # $1 = edition, $2 = platform, $3 = file
    local ed="$1" plat="$2" f="$3" title note
    if [ "$ed" = personal ]; then
        title="A.S.T.R.O AI $VER - WITH YOUR API KEYS (personal edition)"
        note="THIS BUILD CONTAINS YOUR API KEYS (seed-keys.env, gateway.env). Do not share it publicly.
Data folder: Windows %APPDATA%\\Astro AI | Linux ~/.local/share/astro-ai | macOS ~/Library/Application Support/Astro AI"
    else
        title="A.S.T.R.O AI Clean $VER - CLEAN (no API keys)"
        note="This build contains NO API keys. It starts fresh with the keyless free providers;
add your own keys in Settings > Keys. Safe to share.
Data folder: Windows %APPDATA%\\Astro AI Clean | Linux ~/.local/share/astro-ai-clean | macOS ~/Library/Application Support/Astro AI Clean"
    fi
    {
        echo "$title"; echo "Platform: $plat"; echo
        echo "$note"; echo
        case "$plat" in
            windows*) echo "Install: run the Setup .exe (no admin needed), or unzip the portable folder and run the .exe.
Needs the Microsoft Edge WebView2 Runtime (built into Windows 11 / updated Windows 10; the installer adds it if missing).
To run code and terminal commands in the Workspace/Agent, install Git for Windows (bash): https://git-scm.com/download/win";;
            linux*) echo "Install: sudo apt install ./<file>.deb    (or: tar xf <file>.tar.gz && ./astro-ai*/install.sh)
Needs WebKitGTK 4.1 (libwebkit2gtk-4.1-0), libcurl and OpenSSL 3 (glibc 2.35+: Ubuntu 22.04+, Debian 12+, Fedora 36+).";;
            macos*) echo "Build on your Mac: install the Xcode Command Line Tools (xcode-select --install), then run ./build_mac.sh
It produces the .app and a .dmg (universal: Apple Silicon + Intel).";;
        esac
        echo; echo "The app runs a local server on 127.0.0.1 only; AI calls go straight from your computer to the providers."
    } > "$f"
}

# EDITIONS="clean" builds only the shareable edition. The personal (with-keys) edition is never built on CI
# or any shared machine: it would bake whatever keys exist there into a downloadable file.
EDITIONS="${EDITIONS:-personal clean}"
if [ -n "${CI:-}${GITHUB_ACTIONS:-}" ] && [[ " $EDITIONS " == *" personal "* ]]; then
    echo "Refusing to build the WITH-KEYS edition on CI. Use EDITIONS=clean." >&2; exit 1
fi
for ed in $EDITIONS; do
    if [ "$ed" = personal ]; then
        TAG="WITH-KEYS"; DEST="$OUT/with-keys"; APP="Astro AI"; DISP="A.S.T.R.O AI"; ID="AstroAI"; EXE="AstroAI.exe"; PKG="astro-ai"; CLI="astro-personal"
        NOTE="PERSONAL EDITION: this build contains your API keys."
    else
        TAG="CLEAN"; DEST="$OUT/clean"; APP="Astro AI Clean"; DISP="A.S.T.R.O AI Clean"; ID="AstroAIClean"; EXE="AstroAI-Clean.exe"; PKG="astro-ai-clean"; CLI="astro"
        NOTE="CLEAN EDITION: no API keys included. Add your own in Settings > Keys."
    fi
    echo "==> $DISP ($TAG)"
    rm -rf "$DEST"; mkdir -p "$DEST"
    PAY="$WORK/payload-$ed"; make_payload "$ed" "$PAY"

    # ---------------- Windows: portable zip + installer ----------------
    WD="$WORK/AstroAI-$VER-windows-x64-$TAG"; mkdir -p "$WD"
    cp -r "$PAY/." "$WD/"
    cp "$WORK/bin/AstroAI.exe" "$WD/$EXE"; cp "$WORK/bin/astro-server.exe" "$WD/"; cp "$WORK/bin/astro.exe" "$WD/$CLI.exe"
    readme "$ed" "windows x64" "$WD/README.txt"
    (cd "$WORK" && zip -qr9 "$DEST/AstroAI-$VER-windows-x64-portable-$TAG.zip" "$(basename "$WD")")
    # Custom A.S.T.R.O AI installer (desktop/setup): payload = the same files as the portable zip.
    KEYS=0; [ "$ed" = "personal" ] && KEYS=1
    desktop/setup/build_setup.sh "$WD" "$DEST/AstroAI-$VER-windows-x64-Setup-$TAG.exe" "$APP" "$ID" "$EXE" "$KEYS" "$WORK/setup-$ed" "$DISP"

    # ---------------- Linux: .deb ----------------
    DEB="$WORK/deb-$ed"; L="$DEB/usr/lib/$PKG"
    mkdir -p "$L" "$DEB/usr/bin" "$DEB/usr/share/applications" "$DEB/DEBIAN" "$DEB/usr/share/doc/$PKG"
    cp -r "$PAY/." "$L/"; cp "$WORK/bin/astro-desktop" "$WORK/bin/astro-server" "$L/"; cp "$WORK/bin/astro" "$L/$CLI"
    chmod 755 "$L/astro-desktop" "$L/astro-server" "$L/$CLI"; [ -f "$L/seed-keys.env" ] && chmod 644 "$L/seed-keys.env"
    [ -f "$L/gateway.env" ] && chmod 644 "$L/gateway.env"
    ln -s "/usr/lib/$PKG/astro-desktop" "$DEB/usr/bin/$PKG"
    ln -s "/usr/lib/$PKG/$CLI" "$DEB/usr/bin/$CLI"   # terminal app: astro (clean) / astro-personal (with keys)
    for s in 16 24 32 48 64 128 256 512; do
        mkdir -p "$DEB/usr/share/icons/hicolor/${s}x${s}/apps"; cp "desktop/assets/icon-$s.png" "$DEB/usr/share/icons/hicolor/${s}x${s}/apps/$PKG.png"
    done
    cat > "$DEB/usr/share/applications/$PKG.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$DISP
GenericName=AI workspace
Comment=Chat, code Workspace and Agent powered by one brain of free AI models
Exec=/usr/bin/$PKG %u
Icon=$PKG
Terminal=false
Categories=Development;Utility;
Keywords=AI;chat;assistant;code;agent;
StartupWMClass=$PKG
MimeType=x-scheme-handler/astroai;
EOF
    readme "$ed" "linux x86_64" "$DEB/usr/share/doc/$PKG/README.txt"
    cat > "$DEB/DEBIAN/control" <<EOF
Package: $PKG
Version: $VER
Section: devel
Priority: optional
Architecture: amd64
Maintainer: A.S.T.R.O AI <astro-ai@localhost>
Depends: libwebkit2gtk-4.1-0, libgtk-3-0t64 | libgtk-3-0, libcurl4t64 | libcurl4, libssl3t64 | libssl3, libc6 (>= 2.35)
Recommends: xdg-utils, git
Suggests: g++, python3, nodejs
Description: $DISP - AI chat, code Workspace and Agent (desktop)
 Desktop app for A.S.T.R.O AI: chat, a code Workspace with run/preview/terminal and an
 autonomous Agent, powered by one brain of free AI models. $NOTE
EOF
    dpkg-deb --root-owner-group -Zxz --build "$DEB" "$DEST/${PKG}_${VER}_amd64-$TAG.deb" >/dev/null

    # ---------------- Linux: portable tar.gz ----------------
    LT="$WORK/$PKG-$VER-linux-x64-$TAG"; mkdir -p "$LT"
    cp -r "$L/." "$LT/"; cp desktop/assets/icon-256.png "$LT/icon.png"; cp "$DEB/usr/share/doc/$PKG/README.txt" "$LT/"
    cat > "$LT/install.sh" <<EOF
#!/usr/bin/env bash
# Adds $DISP to your app menu (no root needed). Run it from where you keep this folder.
set -e
D="\$(cd "\$(dirname "\$0")" && pwd)"
mkdir -p ~/.local/share/applications ~/.local/share/icons/hicolor/256x256/apps
cp "\$D/icon.png" ~/.local/share/icons/hicolor/256x256/apps/$PKG.png
sed -e "s|^Exec=.*|Exec=\"\$D/astro-desktop\" %u|" > ~/.local/share/applications/$PKG.desktop <<'DESK'
$(cat "$DEB/usr/share/applications/$PKG.desktop")
DESK
update-desktop-database ~/.local/share/applications 2>/dev/null || true
xdg-mime default $PKG.desktop x-scheme-handler/astroai 2>/dev/null || true
echo "$DISP added to your applications menu. You can also run: \$D/astro-desktop"
EOF
    chmod 755 "$LT/install.sh"
    (cd "$WORK" && tar -czf "$DEST/$PKG-$VER-linux-x64-portable-$TAG.tar.gz" "$(basename "$LT")")

    # ---------------- macOS: source kit ----------------
    MK="$WORK/AstroAI-$VER-macos-source-$TAG"; mkdir -p "$MK/src/server" "$MK/src/desktop" "$MK/src/webview" "$MK/assets" "$MK/payload"
    cp workspace_server.cpp platform.h ./*.inc "$MK/src/server/"
    mkdir -p "$MK/src/cli"; cp cli/*.cpp cli/*.h cli/*.inc "$MK/src/cli/"; echo "$CLI" > "$MK/payload/cli-name"
    cp desktop/src/astro_desktop.cpp "$MK/src/desktop/"
    cp -r "$TP/webview/include" "$MK/src/webview/"
    cp desktop/assets/icon-mac-1024.png "$MK/assets/"
    cp -r "$PAY/." "$MK/payload/"
    cp desktop/mac/build_mac.sh "$MK/"; chmod 755 "$MK/build_mac.sh"
    readme "$ed" "macos (build on a Mac)" "$MK/README.txt"
    (cd "$WORK" && tar -czf "$DEST/AstroAI-$VER-macos-source-$TAG.tar.gz" "$(basename "$MK")")
done

# ------------------------------------------------------------------------------------------------
# VS Code extension (same for both editions; it contains no keys): shipped with the clean downloads.
# ------------------------------------------------------------------------------------------------
if [[ " $EDITIONS " == *" clean "* ]]; then
    echo "==> VS Code extension"
    if (cd ide/vscode && npx -y @vscode/vsce@3 package --allow-missing-repository -o "$OUT/clean/astro-ai-vscode-$VER.vsix" >/dev/null 2>&1); then
        echo "   astro-ai-vscode-$VER.vsix"
    else
        echo "   (skipped: npx/vsce not available)"
    fi
fi

# ------------------------------------------------------------------------------------------------
# Safety check: the clean edition must not contain any key value from the personal one.
# ------------------------------------------------------------------------------------------------
echo "==> Checking the clean edition for keys"
SECRETS="$WORK/secrets.txt"; umask 077
{ if [ -f .astro_keys.env ]; then sed -n 's/^[A-Za-z_][A-Za-z0-9_]*=//p' .astro_keys.env; fi
  if [ -f "$WORK/payload-personal/gateway.env" ]; then sed -n 's/^LLM_API_KEY=//p' "$WORK/payload-personal/gateway.env"; fi; } \
  | tr -d "\"'" | awk 'length($0) >= 12' > "$SECRETS"
# The installer payload is compressed: unpack the clean one so the scan can read it.
rm -rf "$WORK/setup-clean/unz"; mkdir -p "$WORK/setup-clean/unz"; (cd "$WORK/setup-clean/unz" && unzip -q ../payload.zip)
LEAKS=0
for d in "$WORK/payload-clean" "$WORK/deb-clean" "$WORK/AstroAI-$VER-windows-x64-CLEAN" "$WORK/AstroAI-$VER-macos-source-CLEAN" "$WORK/setup-clean/unz"; do
    n=$(grep -rFl -f "$SECRETS" "$d" 2>/dev/null | wc -l || true); LEAKS=$((LEAKS + n))
done
# Positive control: the same scan must find the keys in the personal payload, or the check proves nothing.
FOUND=$(grep -rFl -f "$SECRETS" "$WORK/payload-personal" 2>/dev/null | wc -l || true)
rm -f "$SECRETS"
if [ -f .astro_keys.env ] && [ "$FOUND" -eq 0 ]; then echo "!! Key scan found nothing in the personal payload; the check is broken. Aborting."; exit 1; fi
echo "   personal edition: keys present in $FOUND file(s) (expected)"
if [ "$LEAKS" -ne 0 ]; then echo "!! The clean edition contains key material ($LEAKS file(s)). Aborting."; rm -rf "$OUT/clean"; exit 1; fi
echo "   clean edition: 0 keys found"

cat > "$OUT/README.txt" <<EOF
A.S.T.R.O AI desktop $VER

  with-keys/  ->  "A.S.T.R.O AI"        PERSONAL edition: CONTAINS YOUR API KEYS. Keep it private.
  clean/      ->  "A.S.T.R.O AI Clean"  CLEAN edition: NO API keys. Safe to share.

Both can be installed side by side: separate app names, folders, data and ports (47611 / 47612).
Windows: *-Setup-*.exe (installer) or *-portable-*.zip | Linux: *.deb or *.tar.gz | macOS: *-macos-source-*.tar.gz -> ./build_mac.sh
EOF
echo "==> Done"
ls -la "$OUT"/clean "$OUT"/with-keys 2>/dev/null || true

#!/usr/bin/env bash
# Builds the A.S.T.R.O AI engine (workspace_server) for Android arm64 as one static executable.
#
# Output: android/app/src/main/jniLibs/arm64-v8a/libastro_engine.so
#   (named lib*.so so Android installs it into the app's nativeLibraryDir, the one place an app may
#    execute its own programs from; it is a normal executable, not a shared library)
#
# Pieces, all pinned by SHA-256 and built from source with the Android NDK:
#   OpenSSL 3.5.9 (LTS) -> libssl.a / libcrypto.a
#   curl 8.23.0         -> libcurl.a
#   NDK zlib, NDK libc.a / libc++_static.a
#
# Needs: ANDROID_NDK_ROOT (NDK r30 or newer), make, perl, tar, xz, curl, sha256sum.
# Usage: ANDROID_NDK_ROOT=/path/to/android-ndk-r30 bash android/build_engine.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
API="${ANDROID_API:-26}"                        # Android 8.0+
WORK="${ASTRO_ANDROID_WORK:-$HOME/.cache/android/work}"
PREFIX="$WORK/prefix"
JOBS="${JOBS:-2}"
OUT_DIR="$ROOT/android/app/src/main/jniLibs/arm64-v8a"

OPENSSL_VER=3.5.9
OPENSSL_SHA=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
CURL_VER=8.23.0
CURL_SHA=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
CACERT_DATE=2026-10-20                          # Mozilla CA list, as extracted by curl.se
CACERT_SHA=796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe
ASSETS_DIR="$ROOT/android/app/src/main/assets"

: "${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to the Android NDK folder (r30 or newer).}"
TC="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
[ -x "$TC/bin/aarch64-linux-android$API-clang++" ] || { echo "NDK toolchain not found under $TC" >&2; exit 1; }
CC="$TC/bin/aarch64-linux-android$API-clang"
CXX="$TC/bin/aarch64-linux-android$API-clang++"
AR="$TC/bin/llvm-ar"
RANLIB="$TC/bin/llvm-ranlib"
STRIP="$TC/bin/llvm-strip"

mkdir -p "$WORK" "$PREFIX" "$OUT_DIR" "$ASSETS_DIR"

fetch() {   # url file sha256
    local url="$1" file="$WORK/$2" sha="$3"
    if [ ! -f "$file" ] || ! echo "$sha  $file" | sha256sum -c --quiet - 2>/dev/null; then
        echo "Downloading $2"
        curl -fsSL --retry 3 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --quiet - || { echo "Checksum mismatch for $2" >&2; exit 1; }
}

# ---- OpenSSL --------------------------------------------------------------------------------------
if [ ! -f "$PREFIX/lib/libssl.a" ]; then
    fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz" \
          "openssl-$OPENSSL_VER.tar.gz" "$OPENSSL_SHA"
    rm -rf "$WORK/openssl-$OPENSSL_VER"
    tar -xzf "$WORK/openssl-$OPENSSL_VER.tar.gz" -C "$WORK"
    (
        cd "$WORK/openssl-$OPENSSL_VER"
        export PATH="$TC/bin:$PATH"
        ./Configure android-arm64 --prefix="$PREFIX" --libdir=lib \
            no-shared no-tests no-docs no-apps no-module no-dso no-engine no-legacy no-ui-console
        make -j"$JOBS" build_libs >/dev/null
        make install_dev >/dev/null
    )
    rm -rf "$WORK/openssl-$OPENSSL_VER"
fi

# ---- curl -----------------------------------------------------------------------------------------
# No CA bundle path is compiled in. --with-ca-fallback makes libcurl use OpenSSL's default store, which
# honours SSL_CERT_FILE; the app points that at the Mozilla CA bundle it ships (assets/cacert.pem).
if [ ! -f "$PREFIX/lib/libcurl.a" ]; then
    fetch "https://curl.se/download/curl-$CURL_VER.tar.xz" "curl-$CURL_VER.tar.xz" "$CURL_SHA"
    rm -rf "$WORK/curl-$CURL_VER"
    tar -xJf "$WORK/curl-$CURL_VER.tar.xz" -C "$WORK"
    (
        cd "$WORK/curl-$CURL_VER"
        # Feature tests link against the API-$API libc.so stubs (not libc.a, which has every newer function),
        # so curl only uses what Android $API provides. The final engine is still linked statically.
        CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="-O2 -fPIE" \
        ./configure --host=aarch64-linux-android --prefix="$PREFIX" \
            --disable-shared --enable-static --with-openssl="$PREFIX" --with-zlib \
            --without-ca-bundle --without-ca-path --with-ca-fallback \
            --without-libpsl --without-libidn2 --without-nghttp2 --without-brotli --without-zstd --without-libssh2 \
            --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
            --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt --disable-smb \
            --disable-manual --disable-docs >/dev/null 2>&1 || { echo "curl configure failed; see config.log" >&2; tail -40 config.log >&2; exit 1; }
        make -j"$JOBS" >/dev/null
        make install >/dev/null
    )
    rm -rf "$WORK/curl-$CURL_VER"
fi

# ---- CA certificates (HTTPS) ---------------------------------------------------------------------
# Android keeps its system CAs in a format OpenSSL cannot read, so the app ships Mozilla's list (pinned).
fetch "https://curl.se/ca/cacert-$CACERT_DATE.pem" "cacert-$CACERT_DATE.pem" "$CACERT_SHA"
cp "$WORK/cacert-$CACERT_DATE.pem" "$ASSETS_DIR/cacert.pem"

# License texts for what the APK contains, taken from the same verified tarballs (shown as assets/licenses/).
LIC_DIR="$ASSETS_DIR/licenses"
rm -rf "$LIC_DIR" && mkdir -p "$LIC_DIR"
tar -xzOf "$WORK/openssl-$OPENSSL_VER.tar.gz" "openssl-$OPENSSL_VER/LICENSE.txt" > "$LIC_DIR/OpenSSL-LICENSE.txt"
tar -xJOf "$WORK/curl-$CURL_VER.tar.xz" "curl-$CURL_VER/COPYING" > "$LIC_DIR/curl-COPYING.txt"
cp "$ROOT/LICENSE" "$LIC_DIR/A.S.T.R.O-AI-LICENSE.txt" 2>/dev/null || true
cp "$ROOT/desktop/setup/fonts/FONTS-LICENSE.txt" "$LIC_DIR/FONTS-LICENSE.txt"
cat > "$LIC_DIR/NOTICE.txt" <<NOTICE
A.S.T.R.O AI for Android - third-party components inside this app

* OpenSSL $OPENSSL_VER - Apache License 2.0 (OpenSSL-LICENSE.txt). https://www.openssl.org/
* curl / libcurl $CURL_VER - curl license (curl-COPYING.txt). https://curl.se/
* Mozilla CA certificate list, extract of $CACERT_DATE (cacert.pem), unmodified - Mozilla Public License 2.0,
  https://www.mozilla.org/MPL/2.0/ . Source: https://curl.se/docs/caextract.html
* zlib (Android system library) - zlib license.
* LLVM libc++ (Android NDK) - Apache License 2.0 with LLVM Exceptions.
* Kotlin standard library and kotlinx.coroutines (JetBrains), AndroidX and Jetpack Compose including
  Material 3 (Google) - Apache License 2.0 (same text as OpenSSL-LICENSE.txt). https://developer.android.com/jetpack
* Fonts Inter, Instrument Serif and JetBrains Mono - SIL Open Font License 1.1 (FONTS-LICENSE.txt).
See THIRD-PARTY-NOTICES.md in the source repository for the full list.

A.S.T.R.O AI itself: MIT OR Apache-2.0 OR GPL-3.0-only (A.S.T.R.O-AI-LICENSE.txt).
Source code: https://github.com/esoexe/astro-ai
NOTICE

# ---- engine ---------------------------------------------------------------------------------------
# Shipped build: a normal Android PIE executable linked against the phone's own libc (API $API stubs), with
# OpenSSL, curl, zlib and libc++ linked in statically. Linking against the API-$API stubs also proves at build
# time that nothing newer than Android 8.0 is used. (A fully static bionic binary is NOT shipped: libc.a comes
# from the newest Android and may make system calls that older phones' app sandbox forbids.)
echo "Compiling the engine for Android arm64 (API $API)"
COMMON=(-std=c++20 -O2 -Wall -Wextra -pthread -DCURL_STATICLIB -I"$PREFIX/include" "$ROOT/workspace_server.cpp")
LIBS=(-L"$PREFIX/lib" -l:libcurl.a -l:libssl.a -l:libcrypto.a -l:libz.a -static-libstdc++)
"$CXX" "${COMMON[@]}" -o "$WORK/workspace_server" "${LIBS[@]}"
"$STRIP" "$WORK/workspace_server"
cp "$WORK/workspace_server" "$OUT_DIR/libastro_engine.so"
chmod 755 "$OUT_DIR/libastro_engine.so"
# Test-only build: fully static, so it can run under qemu-aarch64 on a Linux PC (android/test_engine_qemu.sh).
if [ "${ASTRO_ANDROID_STATIC_TEST:-0}" = 1 ]; then
    "$CXX" "${COMMON[@]}" -static -o "$WORK/workspace_server-static" "${LIBS[@]}"
    "$STRIP" "$WORK/workspace_server-static"
    echo "Static test build: $WORK/workspace_server-static"
fi
ls -l "$OUT_DIR/libastro_engine.so"
file "$OUT_DIR/libastro_engine.so" 2>/dev/null || true

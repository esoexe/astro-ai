#!/usr/bin/env bash
# Downloads the Windows ARM64 cross toolchain into .cache/arm (used by desktop/package.sh for the windows-arm64 target):
#   llvm-mingw (clang + mingw-w64 for aarch64-w64-mingw32)   https://github.com/mstorsjo/llvm-mingw
#   static curl for Windows ARM64 (curl-for-win)             https://curl.se/windows/
# Both are checked against pinned SHA-256 checksums (the publishers' own: GitHub release digest, curl.se hashes.txt).
set -euo pipefail
cd "$(dirname "$0")/.."
A="$PWD/.cache/arm"; mkdir -p "$A"; cd "$A"
LLVM=llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64
LLVM_SHA=55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335
CURL=curl-8.23.0_6-win64a-mingw
CURL_SHA=58d1bafcbcde6fe5a1e3072400253465e59fdb70c0f68a98c58a9eb73d9cd035
if [ ! -x "$LLVM/bin/aarch64-w64-mingw32-clang++" ]; then
    curl -fsSL -o llvm.tar.xz "https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/$LLVM.tar.xz"
    echo "$LLVM_SHA  llvm.tar.xz" | sha256sum -c -
    # only the ARM64 target and the compilers (saves ~1 GB)
    tar xJf llvm.tar.xz --exclude="*/x86_64-w64-mingw32" --exclude="*/i686-w64-mingw32" --exclude="*/armv7-w64-mingw32" \
        --exclude="*/lib/liblldb*" --exclude="*/bin/lldb*"
    rm -f llvm.tar.xz
fi
if [ ! -f "$CURL/lib/libcurl.a" ]; then
    curl -fsSL -o curl.zip "https://curl.se/windows/dl-8.23.0_6/$CURL.zip"
    echo "$CURL_SHA  curl.zip" | sha256sum -c -
    unzip -q -o curl.zip && rm -f curl.zip
fi
"$LLVM/bin/aarch64-w64-mingw32-clang++" --version | head -1
echo "Windows ARM64 toolchain ready in $A"

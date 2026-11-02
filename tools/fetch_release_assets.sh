#!/usr/bin/env bash
# Puts the CLEAN downloads the website self-hosts back into website/downloads, from the public GitHub releases, each
# checked against the release's own SHA256SUMS.txt. The workspace keeps them out of its saved snapshot (they are large
# and already published), so run this before `cd website && ASTRO_DL_DIR=downloads node build.js` after a reset.
#   tools/fetch_release_assets.sh [tag ...]      default: the desktop and Android releases the site links to
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=esoexe/astro-ai
DL=website/downloads
TAGS=("$@"); [ ${#TAGS[@]} -gt 0 ] || TAGS=(v1.8.1 android-v1.8.2)
# the files website/downloads carries (the Windows installers are linked from the release page, not mirrored)
WANT='^(astro-ai-clean.*|AstroAI-.*-(macos-.*|windows-arm64-portable-CLEAN\.zip|android-arm64\.apk)|astro-ai-vscode-.*\.vsix)$'
mkdir -p "$DL"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
for tag in "${TAGS[@]}"; do
    curl -fsSL "https://api.github.com/repos/$REPO/releases/tags/$tag" > "$tmp/rel.json"
    curl -fsSL -o "$tmp/SHA256SUMS.txt" "https://github.com/$REPO/releases/download/$tag/SHA256SUMS.txt"
    jq -r '.assets[].name' "$tmp/rel.json" | grep -E "$WANT" | while read -r name; do
        sum=$(awk -v n="$name" '{f=$2; sub(/^\*/, "", f)} f == n {print $1}' "$tmp/SHA256SUMS.txt")
        [ -n "$sum" ] || { echo "no checksum for $name in $tag/SHA256SUMS.txt" >&2; exit 1; }
        if [ -f "$DL/$name" ] && echo "$sum  $DL/$name" | sha256sum -c --quiet - 2>/dev/null; then echo "ok    $name"; continue; fi
        curl -fsSL -o "$tmp/$name" "https://github.com/$REPO/releases/download/$tag/$name"
        echo "$sum  $tmp/$name" | sha256sum -c --quiet -
        mv "$tmp/$name" "$DL/$name"; echo "got   $name"
    done
done

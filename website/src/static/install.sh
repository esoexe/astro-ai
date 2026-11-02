#!/bin/sh
# A.S.T.R.O AI: terminal install (no setup wizard, no admin rights).
#
#   curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh
#
# Installs the CLEAN edition of the `astro` command and its local engine into ~/.astro-ai
# and links `astro` into ~/.local/bin. Linux x64 and arm64 (1.8.0+), macOS (Apple Silicon + Intel).
# Downloads come from the official GitHub release and are checked against SHA256SUMS.txt.
#
# Options (environment variables):
#   ASTRO_VERSION=1.7.0     install a specific version (default: the latest release)
#   ASTRO_HOME=~/.astro-ai  where the files go
#   ASTRO_BIN=~/.local/bin  where the `astro` link goes
# Uninstall:  sh install.sh --uninstall   (keeps your chats, settings and keys)
set -eu

REPO="esoexe/astro-ai"
FALLBACK_VERSION="__ASTRO_VERSION__"
HOME_DIR="${ASTRO_HOME:-$HOME/.astro-ai}"
BIN_DIR="${ASTRO_BIN:-$HOME/.local/bin}"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then P='\033[38;2;243;166;121m'; G='\033[32m'; R='\033[31m'; D='\033[2m'; B='\033[1m'; X='\033[0m'; else P=''; G=''; R=''; D=''; B=''; X=''; fi
say() { printf '%b\n' "$*"; }
step() { say "${P}●${X} $*"; }
die() { say "${R}✗ $*${X}" >&2; exit 1; }

if [ "${1:-}" = "--uninstall" ]; then
  rm -f "$BIN_DIR/astro"
  rm -rf "$HOME_DIR"
  say "${G}✓${X} A.S.T.R.O AI terminal install removed. Your chats, settings and keys were kept."
  exit 0
fi

need() { command -v "$1" >/dev/null 2>&1 || die "This installer needs '$1'. Please install it and try again."; }
need curl; need tar; need uname; need mktemp

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else echo ""; fi
}

OS="$(uname -s)"; ARCH="$(uname -m)"
case "$OS" in
  Linux)
    case "$ARCH" in x86_64|amd64) LARCH=x64 ;; aarch64|arm64) LARCH=arm64 ;;
      *) die "Linux $ARCH isn't built yet (x64 and arm64 only). You can build from source: https://github.com/$REPO";; esac
    PLATFORM=linux ;;
  Darwin) PLATFORM=mac ;;
  MINGW*|MSYS*|CYGWIN*) die "On Windows, run this in PowerShell instead:  irm https://esoexe.github.io/astro-ai/install.ps1 | iex" ;;
  *) die "Unsupported system: $OS $ARCH" ;;
esac

# Version: ASTRO_VERSION, else the latest GitHub release, else the version this script shipped with.
VERSION="${ASTRO_VERSION:-}"
if [ -z "$VERSION" ]; then
  LATEST="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "https://github.com/$REPO/releases/latest" 2>/dev/null || true)"
  case "$LATEST" in */tag/v*) VERSION="${LATEST##*/tag/v}" ;; esac
fi
[ -n "$VERSION" ] || VERSION="$FALLBACK_VERSION"
case "$VERSION" in __*) die "Could not find the latest version. Set one, e.g. ASTRO_VERSION=1.7.0";; esac
VERSION="${VERSION#v}"

if [ "$PLATFORM" = linux ]; then FILE="astro-ai-clean-$VERSION-linux-$LARCH-portable-CLEAN.tar.gz"
  if [ "$LARCH" = arm64 ] && ! curl -fsSLI -o /dev/null "https://github.com/$REPO/releases/download/v$VERSION/$FILE" 2>/dev/null; then
    die "There is no Linux arm64 build of v$VERSION (they start at 1.8.0). Try the latest version, or build from source: https://github.com/$REPO"
  fi
else FILE="AstroAI-$VERSION-macos-universal-CLEAN.dmg"; fi
BASE="https://github.com/$REPO/releases/download/v$VERSION"

say ""
say "  ${B}A.S.T.R.O AI${X} ${D}v$VERSION · terminal install · $OS $ARCH${X}"
say ""

TMP="$(mktemp -d 2>/dev/null || mktemp -d -t astro)"
MNT=""
cleanup() { if [ -n "$MNT" ]; then hdiutil detach "$MNT" -quiet >/dev/null 2>&1 || true; fi; rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

step "Downloading $FILE"
curl -fL --progress-bar -o "$TMP/$FILE" "$BASE/$FILE" || die "Download failed: $BASE/$FILE"

step "Checking the download"
if curl -fsSL -o "$TMP/SHA256SUMS.txt" "$BASE/SHA256SUMS.txt" 2>/dev/null; then
  WANT="$(grep " \*\{0,1\}$FILE\$" "$TMP/SHA256SUMS.txt" | head -1 | cut -d' ' -f1 || true)"
  GOT="$(sha256 "$TMP/$FILE")"
  if [ -n "$WANT" ] && [ -n "$GOT" ]; then
    [ "$WANT" = "$GOT" ] || die "Checksum mismatch for $FILE. Nothing was installed."
    say "  ${G}✓${X} SHA-256 matches the release"
  else
    say "  ${D}(no checksum available for this file; skipped)${X}"
  fi
else
  say "  ${D}(SHA256SUMS.txt not found; skipped)${X}"
fi

step "Installing into $HOME_DIR"
NEW="$HOME_DIR.new.$$"
rm -rf "$NEW"; mkdir -p "$NEW"
if [ "$PLATFORM" = linux ]; then
  tar -xzf "$TMP/$FILE" -C "$TMP"
  SRC="$(find "$TMP" -mindepth 1 -maxdepth 1 -type d -name 'astro-ai-clean-*' | head -1)"
  [ -n "$SRC" ] && [ -f "$SRC/astro" ] || die "The archive doesn't look right (no astro inside)."
  for f in astro astro-server edition.json providers.json README.txt icon.png; do
    if [ -e "$SRC/$f" ]; then cp -p "$SRC/$f" "$NEW/"; fi
  done
  cp -R "$SRC/web" "$NEW/web"
else
  need hdiutil
  MNT="$TMP/mnt"; mkdir -p "$MNT"
  hdiutil attach "$TMP/$FILE" -nobrowse -readonly -quiet -mountpoint "$MNT" || { MNT=""; die "Could not open the disk image."; }
  APPDIR="$(find "$MNT" -maxdepth 1 -name '*.app' | head -1)"
  RES="$APPDIR/Contents/Resources"
  [ -n "$APPDIR" ] && [ -f "$RES/astro" ] || die "The disk image doesn't look right (no astro inside)."
  cp -R "$RES/." "$NEW/"
  hdiutil detach "$MNT" -quiet >/dev/null 2>&1 || true; MNT=""
  xattr -dr com.apple.quarantine "$NEW" 2>/dev/null || true
fi
chmod 755 "$NEW/astro" "$NEW/astro-server" 2>/dev/null || true
# an engine left running from the old version would keep answering with old code: stop it first
if command -v pkill >/dev/null 2>&1; then pkill -f "$HOME_DIR/astro-server" >/dev/null 2>&1 || true; fi
rm -rf "$HOME_DIR"; mv "$NEW" "$HOME_DIR"

mkdir -p "$BIN_DIR"
ln -sf "$HOME_DIR/astro" "$BIN_DIR/astro"
say "  ${G}✓${X} astro → $BIN_DIR/astro"

if "$HOME_DIR/astro" --version >/dev/null 2>&1; then
  say "  ${G}✓${X} $("$HOME_DIR/astro" --version 2>/dev/null | head -1)"
fi

say ""
say "${G}✓ Installed.${X} Open a project folder and run:"
say ""
say "    ${P}astro${X}                      ${D}# chat with the team in this folder${X}"
say "    ${P}astro -p \"fix the tests\"${X}   ${D}# one task, start to finish${X}"
say "    ${P}astro --help${X}"
say ""
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *)
    SHELL_RC="$HOME/.profile"
    case "${SHELL:-}" in */zsh) SHELL_RC="$HOME/.zshrc";; */bash) SHELL_RC="$HOME/.bashrc";; esac
    say "${P}!${X} $BIN_DIR isn't on your PATH yet. Add it with:"
    say ""
    say "    echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> $SHELL_RC && . $SHELL_RC"
    say ""
    ;;
esac
say "${D}The first run starts the local engine by itself and works right away with the free models that need no key.${X}"
say "${D}To add more free providers, open http://127.0.0.1:47612 in your browser while it runs (Settings → Providers).${X}"
say "${D}Uninstall: curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh -s -- --uninstall${X}"

#!/usr/bin/env bash
# export_um_cpp.sh - assembles universal-modder-cpp (https://github.com/esoexe/universal-modder-cpp): the C++ edition
# of Rehan's universal-modder (https://github.com/rehan-remade/universal-modder, MIT) as a repository of its own.
#
#   bash tools/export_um_cpp.sh <universal-modder checkout> <out dir>
#
# What goes in:
#   src/            the `um` program: standalone/um/um_main.cpp + the packtools files it includes (the same C++ the
#                   A.S.T.R.O AI engine compiles as its built-in `um` tool), found with the compiler's -MM
#   skills/ agents/ upstream's skills and agents, rewritten by tools/port_modder.cpp --standalone (plain `um`, the
#                   Python helper scripts replaced by built-in commands), plus the .agents/ and .claude/ copies
#   knowledge/ docs/ examples/ hooks/ plugins/ and the agent manifests: upstream's files, without any .py file
#   tests/          the bash suites A.S.T.R.O AI runs against `um` (no Python, not even as a test oracle)
#   Makefile, bin/um, bin/um.cmd, CI and release workflows, README.md, NOTICE.md, PORTING.md
set -euo pipefail
SRC="${1:?usage: export_um_cpp.sh <universal-modder checkout> <out dir>}"
OUT="${2:?usage: export_um_cpp.sh <universal-modder checkout> <out dir>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(cd "$SRC" && pwd)"
[ -f "$SRC/skills/mod-any-game/SKILL.md" ] && [ -f "$SRC/LICENSE" ] || { echo "not a universal-modder checkout: $SRC" >&2; exit 2; }
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
EDITION_VERSION="1.0.0"
mkdir -p "$OUT"
# keep .git (the export is pushed from here); everything else is regenerated
find "$OUT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

# ------------------------------------------------------------------ src/
mkdir -p "$OUT/src/packtools"
deps=$(cd "$ROOT" && g++ -std=c++20 -MM standalone/um/um_main.cpp | tr ' \\' '\n\n' | grep '^standalone/um/\.\./\.\./packtools/' | sed 's|^standalone/um/\.\./\.\./packtools/||' | sort -u)
for f in $deps; do cp "$ROOT/packtools/$f" "$OUT/src/packtools/$f"; done
# packtools.inc also lists the engine's other tool groups (behind #if PACKTOOLS_IN(n)); drop the includes of files
# that are not part of the edition so the tree is self-contained
keep=" $(echo $deps | tr '\n' ' ') "
awk -v keep="$keep" '{ if (match($0, /^#include "[^"]+\.inc"/)) { f = substr($0, 11); sub(/".*/, "", f); if (index(keep, " " f " ") == 0) next } print }' \
  "$ROOT/packtools/packtools.inc" > "$OUT/src/packtools/packtools.inc"
sed 's|#include "../../packtools/packtools.inc"|#include "packtools/packtools.inc"|' "$ROOT/standalone/um/um_main.cpp" > "$OUT/src/um_main.cpp"
grep -q '#include "packtools/packtools.inc"' "$OUT/src/um_main.cpp" || { echo "um_main.cpp include not rewritten" >&2; exit 1; }

# ------------------------------------------------------------------ skills/ agents/ (+ the copies upstream keeps)
g++ -std=c++20 -O1 "$ROOT/tools/port_modder.cpp" -o "$OUT/.port_modder"
"$OUT/.port_modder" "$SRC" "$OUT" --standalone
rm -f "$OUT/.port_modder" "$OUT/.mcp.json"
mkdir -p "$OUT/.agents" "$OUT/.claude"
cp -r "$OUT/skills" "$OUT/.agents/skills"; cp -r "$OUT/skills" "$OUT/.claude/skills"

# ------------------------------------------------------------------ upstream files, minus Python
for p in knowledge docs examples hooks plugins .agents/plugins .claude/settings.json .claude-plugin .codex-plugin .cursor-plugin .codex .cursor .vscode \
         .mcp.json mcp.json opencode.json gemini-extension.json plugin.json .gitattributes CLAUDE.md GEMINI.md SECURITY.md AGENTS.md CONTRIBUTING.md \
         .github/pull_request_template.md bin/um.cmd; do
  [ -e "$SRC/$p" ] || continue
  mkdir -p "$OUT/$(dirname "$p")"; cp -r "$SRC/$p" "$OUT/$p"
done
removed=$(cd "$OUT" && find examples -name '*.py' | sort)
find "$OUT/examples" -name '*.py' -delete
find "$OUT/examples" -type d -empty -delete
printf '%s\n' build/ .venv/ '*.o' '*.exe' >> "$OUT/.gitignore.tmp"; [ -f "$SRC/.gitignore" ] && cat "$SRC/.gitignore" "$OUT/.gitignore.tmp" > "$OUT/.gitignore" || mv "$OUT/.gitignore.tmp" "$OUT/.gitignore"; rm -f "$OUT/.gitignore.tmp"

# manifests, AGENTS.md, CONTRIBUTING.md: asserted edits (an upstream change fails the export loudly)
node - "$OUT" "$EDITION_VERSION" <<'JS'
const fs = require('fs'), path = require('path');
const [out, ver] = process.argv.slice(2);
let bad = 0;
const edit = (rel, pairs) => {
  const f = path.join(out, rel); if (!fs.existsSync(f)) return;
  let s = fs.readFileSync(f, 'utf8');
  for (const [a, b] of pairs) { if (!s.includes(a)) { console.error(`export: expected text not found in ${rel}: ${a.slice(0, 100)}`); bad++; continue; } s = s.split(a).join(b); }
  fs.writeFileSync(f, s);
};
const DESC = 'Mod almost any game you own with an AI agent: engine recon, reverse engineering, fal-generated art/3D/audio, in-game testing, showcase videos, and a shared knowledge base of how games were modded.';
const DESC2 = DESC + ' C++ edition: the um CLI as one native program, no Python.';
for (const rel of ['plugin.json', '.claude-plugin/plugin.json', '.claude-plugin/marketplace.json', '.codex-plugin/plugin.json', '.cursor-plugin/plugin.json',
                   '.cursor-plugin/marketplace.json', '.agents/plugins/marketplace.json', 'gemini-extension.json']) {
  const f = path.join(out, rel); if (!fs.existsSync(f)) continue;
  const j = JSON.parse(fs.readFileSync(f, 'utf8'));
  const fix = (o) => {
    if (o.name === 'universal-modder') o.name = 'universal-modder-cpp';
    if (o.version) o.version = ver;
    if (o.description === DESC) o.description = DESC2;
    for (const k of ['homepage', 'repository']) if (o[k] === 'https://github.com/rehan-remade/universal-modder') o[k] = 'https://github.com/esoexe/universal-modder-cpp';
    if (o.interface) { o.interface.displayName = 'universal-modder-cpp'; o.interface.websiteURL = 'https://github.com/esoexe/universal-modder-cpp'; if (o.interface.longDescription === DESC) o.interface.longDescription = DESC2; }
    if (Array.isArray(o.plugins)) o.plugins.forEach(fix);
  };
  fix(j);
  fs.writeFileSync(f, JSON.stringify(j, null, 2) + '\n');
}
edit('AGENTS.md', [
  ['- **`bin/um`** is the Python CLI, and it sets itself up with `uv`.', '- **`bin/um`** is the CLI: one C++ program (this is the C++ edition; `bin/um` runs `make` the first time).'],
  ['    `bin\\um.cmd` runs it), or install it anywhere with\n    `uv tool install git+https://github.com/rehan-remade/universal-modder`.',
   '    `bin\\um.cmd` runs `build\\um.exe`), or put a release binary from\n    https://github.com/esoexe/universal-modder-cpp/releases anywhere on PATH.'],
  ['(`render3d` needs Blender on PATH or `BLENDER=...`)', '(`render3d` has its own software renderer: no Blender)'],
  ['- **Python:** 3.10+, deps Pillow, numpy and PyYAML (`uv` handles them via `bin/um`). Code lives in `um/`,\n  one module per CLI group, each with `register(sub)` and a docstring that doubles as `--help`.',
   '- **C++20:** one program, `make` builds `build/um` (needs libcurl and zlib; Windows builds use WinHTTP). Code lives in\n  `src/packtools/modder_*.inc`, one file per CLI group, each with `reg(sub)` and a DOC string that doubles as `--help`.\n  The Python helper scripts are built-in groups too: `b3d`, `upk`, `sld`, `vpk`, `vtf` (see PORTING.md).'],
  ['- **Windows tools:** `um/ps1/*.ps1` embed C# 5', '- **Windows tools:** the PowerShell scripts in `src/packtools/modder_win_ps1.inc` embed C# 5'],
  ['- **Tests:** `uv run --with pytest pytest -q tests`.', '- **Tests:** `make test` (bash suites; they need `jq` and `node`).'],
]);
edit('CONTRIBUTING.md', [
  ['- **Tools (`um/`):**', '- **Tools (`src/packtools/`):**'],
  ['  - one module per CLI group, with a docstring that doubles as `--help`;', '  - one `modder_<group>.inc` per CLI group, with a DOC string that doubles as `--help`;'],
  ['  - add a test in `tests/`;\n  - `uv run --with pytest pytest -q tests` must pass.', '  - add checks to a suite in `tests/`;\n  - `make test` must pass, and the Windows cross-build (`make windows`) must stay warning-free.'],
]);
if (bad) process.exit(1);
JS

# ------------------------------------------------------------------ bin/
mkdir -p "$OUT/bin"
cat > "$OUT/bin/um" <<'SH'
#!/usr/bin/env bash
# universal-modder-cpp launcher: runs build/um, building it with `make` the first time.
# Works from a clone, a plugin install, or a PATH symlink.
set -euo pipefail
SRC="${BASH_SOURCE[0]}"
while [ -L "$SRC" ]; do DIR="$(cd -P "$(dirname "$SRC")" && pwd)"; SRC="$(readlink "$SRC")"; [[ "$SRC" != /* ]] && SRC="$DIR/$SRC"; done
ROOT="$(cd -P "$(dirname "$SRC")/.." && pwd)"
BIN="$ROOT/build/um"; [ -x "$BIN.exe" ] && BIN="$BIN.exe"
if [ ! -x "$BIN" ]; then
  echo "um: building build/um (first run; needs a C++20 compiler, libcurl and zlib)..." >&2
  make -C "$ROOT" -s >&2
fi
exec "$BIN" "$@"
SH
cat > "$OUT/bin/um.cmd" <<'CMD'
@echo off
rem universal-modder-cpp launcher for cmd and PowerShell: runs build\um.exe (a release binary or `make windows`).
if not exist "%~dp0..\build\um.exe" (
  echo um: build\um.exe is missing. Download it from https://github.com/esoexe/universal-modder-cpp/releases 1>&2
  echo     and put it in "%~dp0..\build\", or build it with `make windows` ^(MinGW-w64^). 1>&2
  exit /b 1
)
"%~dp0..\build\um.exe" %*
CMD
sed -i 's/$/\r/' "$OUT/bin/um.cmd"
chmod +x "$OUT/bin/um" "$OUT/hooks/"*.sh 2>/dev/null || true

# ------------------------------------------------------------------ Makefile
cat > "$OUT/Makefile" <<'MK'
# universal-modder-cpp: `make` builds build/um; `make test` runs the suites; `make windows` cross-builds build/um.exe
CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -pthread
LDLIBS ?= -lcurl -lz
MINGW ?= x86_64-w64-mingw32-g++-posix
SOURCES := src/um_main.cpp $(wildcard src/packtools/*)

all: build/um
build/um: $(SOURCES)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/um_main.cpp -o $@ $(LDFLAGS) $(LDLIBS)
windows: build/um.exe
build/um.exe: $(SOURCES)
	@mkdir -p build
	$(MINGW) -std=c++20 -O2 -Wall -Wextra -Wpedantic src/um_main.cpp -o $@ -static -lwinhttp -lshell32 -lws2_32
test: build/um
	bash tests/run_all.sh
clean:
	rm -rf build
.PHONY: all windows test clean
MK

# ------------------------------------------------------------------ tests/
mkdir -p "$OUT/tests/fixtures/modder"
cp "$ROOT/packtools/tests/fixtures/modder/"* "$OUT/tests/fixtures/modder/"
for t in modder_um modder_win modder_3d modder_formats; do cp "$ROOT/packtools/tests/$t.sh" "$OUT/tests/$t.sh"; done
cat > "$OUT/tests/lib.sh" <<'SH'
# lib.sh - tiny assertion helpers for the suites (bash + jq + node). T runs `um`: the suites call `$T um <group> ...`
# (the same files run inside A.S.T.R.O AI, where `um` is a tool of the engine), so tests/um-tool drops that word.
T="${T:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/um-tool}"
export ASTRO_TOOL_OFFLINE=1   # never call live services from the suites
FIX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fixtures"
PASS=0; FAILN=0
ok()   { PASS=$((PASS+1)); }
bad()  { FAILN=$((FAILN+1)); echo "  FAIL: $*"; }
eq()   { [ "$1" == "$2" ] && ok || bad "$3: expected [$2], got [$1]"; }
has()  { [[ "$1" == *"$2"* ]] && ok || bad "$3: [$2] not in output"; }
hasnt(){ [[ "$1" != *"$2"* ]] && ok || bad "$3: [$2] should not be in output"; }
run()  { OUT=$($T "$@" 2>"${TMPDIR:-/tmp}/um_err"); CODE=$?; ERR=$(cat "${TMPDIR:-/tmp}/um_err"); }
done_() { echo "$1: $PASS passed, $FAILN failed"; [ "$FAILN" -eq 0 ]; }
SH
cat > "$OUT/tests/um-tool" <<'SH'
#!/bin/sh
# `um-tool um <args>` -> build/um <args>
shift
d="$(cd "$(dirname "$0")/.." && pwd)/build"
[ -x "$d/um.exe" ] && exec "$d/um.exe" "$@"
exec "$d/um" "$@"
SH
cat > "$OUT/tests/run_all.sh" <<'SH'
#!/usr/bin/env bash
# Runs every suite against build/um (or T=...). Needs bash, jq and node; ffmpeg is optional (video checks skip).
cd "$(dirname "$0")/.." || exit 1
rc=0
for t in tests/modder_*.sh; do bash "$t" || rc=1; done
exit $rc
SH
chmod +x "$OUT/tests/"*.sh "$OUT/tests/um-tool"

# ------------------------------------------------------------------ CI + release
mkdir -p "$OUT/.github/workflows"
cat > "$OUT/.github/workflows/ci.yml" <<'YML'
name: ci
on: [push, pull_request]
permissions:
  contents: read
jobs:
  unix:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v7
      - uses: actions/setup-node@v6
        with:
          node-version: 22
      - name: Dependencies (Linux)
        if: runner.os == 'Linux'
        run: sudo apt-get update -q && sudo apt-get install -y -q libcurl4-openssl-dev zlib1g-dev jq ffmpeg
      - name: Dependencies (macOS)
        if: runner.os == 'macOS'
        run: command -v jq || brew install jq
      - name: Build
        run: make -j2
      - name: Test
        run: make test
      - name: The knowledge base and the repo pass the toolkit's own checks
        run: build/um kb check --index && build/um publish check .
  windows:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v7
      - run: sudo apt-get update -q && sudo apt-get install -y -q g++-mingw-w64-x86-64-posix
      - run: make windows
      - uses: actions/upload-artifact@v7
        with:
          name: um-windows
          path: build/um.exe
  windows-smoke:
    needs: windows
    runs-on: windows-latest
    timeout-minutes: 15
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@v7
      - uses: actions/download-artifact@v8
        with:
          name: um-windows
          path: build
      - name: The .exe runs and handles files
        run: |
          build/um.exe --version
          build/um.exe --help > /dev/null
          node tests/fixtures/modder/gen_fixtures.js fx
          build/um.exe upk unpack fx/lzo.upk out.upk
          build/um.exe sld info fx/unit.sld
          build/um.exe vtf to-png fx/dxt1.vtf d1.png
          build/um.exe render3d fx/unit.glb frames --preset aoe2 --canvas 64 --headings 2 --samples 2
          test -f frames/idle_01_000.png
          build/um.exe b3d dump fx/viewmodel.b3d vm.json
          cmd //c "bin\\um.cmd --version"
YML
cat > "$OUT/.github/workflows/release.yml" <<'YML'
name: release
on:
  push:
    tags: ['v*']
permissions:
  contents: write
jobs:
  build:
    strategy:
      matrix:
        include:
          - { os: ubuntu-22.04, name: um-linux-x64 }
          - { os: macos-latest, name: um-macos-arm64 }
          - { os: ubuntu-latest, name: um-windows-x64.exe, cross: true }
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v7
      - if: runner.os == 'Linux' && !matrix.cross
        run: sudo apt-get update -q && sudo apt-get install -y -q libcurl4-openssl-dev zlib1g-dev
      - if: matrix.cross
        run: sudo apt-get update -q && sudo apt-get install -y -q g++-mingw-w64-x86-64-posix && make windows && cp build/um.exe "${{ matrix.name }}"
      - if: '!matrix.cross'
        run: make && cp build/um "${{ matrix.name }}"
      - uses: actions/upload-artifact@v7
        with:
          name: ${{ matrix.name }}
          path: ${{ matrix.name }}
  publish:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v8
        with:
          path: dist
          merge-multiple: true
      - name: Checksums and release
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          cd dist && sha256sum um-* > SHA256SUMS.txt && cat SHA256SUMS.txt
          gh release create "$GITHUB_REF_NAME" --repo "$GITHUB_REPOSITORY" --title "universal-modder-cpp $GITHUB_REF_NAME" \
            --notes "The um CLI of universal-modder as one native program (C++20, no Python). Linux x64 (needs libcurl), macOS arm64, Windows x64 (no DLLs). Ported from rehan-remade/universal-modder (MIT); see PORTING.md." \
            um-* SHA256SUMS.txt || gh release upload "$GITHUB_REF_NAME" --repo "$GITHUB_REPOSITORY" --clobber um-* SHA256SUMS.txt
YML

# ------------------------------------------------------------------ LICENSE, NOTICE, PORTING, README
{ cat "$SRC/LICENSE"; printf '\n---\n\nThe C++ edition (src/, tests/, the Makefile, the launchers and the rewritten skill text) is\nCopyright (c) 2026 The Astro AI team, under the same MIT License.\n'; } > "$OUT/LICENSE"
node - "$OUT" "$COMMIT" "$removed" <<'JS'
const fs = require('fs'), path = require('path');
const [out, commit, removedList] = process.argv.slice(2);
const removed = removedList.split('\n').filter(Boolean);
const ported = {
  'examples/aoe2-de-civ/sld.py': '`um sld` (info, roundtrip, to-png, and `write` for what `sld.write()` did)',
  'examples/outer-wilds-portal-gun/tools/vpk.py': '`um vpk` (list, extract)',
  'examples/outer-wilds-portal-gun/tools/vtf.py': '`um vtf` (info, to-png)',
};
const rows = removed.map((f) => `| \`${f}\` | ${ported[f] ? 'ported: ' + ported[f] : 'not ported: a case study tied to one setup ([upstream](https://github.com/rehan-remade/universal-modder/blob/' + commit + '/' + f + '))'} |`);
const nPorted = removed.filter((f) => ported[f]).length;
fs.writeFileSync(path.join(out, 'PORTING.md'), `# Porting status

universal-modder-cpp is the C++ edition of [universal-modder](https://github.com/rehan-remade/universal-modder)
by Rehan and universal-modder contributors (MIT), at upstream commit \`${commit}\`. Every Python file
upstream has is listed here with what took its place.

## The \`um\` CLI (\`um/*.py\`, \`bin/um\`)

All of it, as one C++20 program: \`scan\`, \`fal\`, \`comfy\`, \`sprixen\`, \`voidless\`, \`sprite\`, \`render3d\`, \`video\`, \`win\`,
\`backup\`, \`publish\`, \`kb\`, \`fastfile\`, \`iwi\`, \`weapon\`. Same commands, flags, output and file names.

- \`render3d\` scripted Blender (\`um/blender/render_sprites.py\`). Here it is a built-in software renderer (glTF 2.0
  GLB/.gltf, skinned and animated meshes, the six presets, the seven motions, shadow passes). It doesn't need Blender, and the light is simpler
  than Cycles: a sun plus ambient.
- Pillow, numpy and PyYAML are replaced by the C++ in \`src/packtools/\` (PNG codec, resampling, quantizing, YAML).
- HTTP goes through libcurl on Linux and macOS and WinHTTP on Windows.

## Skill helper scripts

| Upstream | Here |
|---|---|
| \`skills/blitz3d-decomp/scripts/roblox/b3d_skin.py\` | \`um b3d dump\` |
| \`skills/blitz3d-decomp/scripts/roblox/vm_anim_data.py\` | \`um b3d anim\` |
| \`skills/blitz3d-decomp/scripts/roblox/vm_build.py\` (Blender, FBX) | \`um b3d build\` (glTF: \`NAME.glb\` + \`NAME.preview.glb\`, no Blender) |
| \`skills/blitz3d-decomp/scripts/roblox/vm_render.py\` (Blender) | \`um b3d render\` |
| \`skills/reverse-engineering/references/ue3-packages/unpack_upk.py\` (minilzo) | \`um upk unpack\` / \`um upk clear-flags\` (own LZO1X decompressor) |

## Examples (${removed.length} Python files: ${nPorted} ported, ${removed.length - nPorted} not)

The reusable file-format tools are built-in commands. The rest belong to one mod's own build: build scripts, the
Minecraft passthrough hosts and video directors, the Portal 2 model converter. They need that exact setup and
can't be tested without it, so they are not part of this edition. Each example's write-up, data and media are here.
The scripts are in the upstream repository.

| Upstream file | Status |
|---|---|
${rows.join('\n')}

## Tests and CI

\`tests/*.py\` (pytest) are replaced by bash suites (\`tests/modder_*.sh\`) with fixtures made by a Node script.
They check the same behaviour plus the new groups. No Python is used, not even as a test oracle.
Upstream's star-chart workflow (\`.github/star-chart/*.py\`) draws upstream's own README chart, so it is not carried over.
`);
JS
cat > "$OUT/NOTICE.md" <<EOF
# Notice

universal-modder-cpp is the C++ edition of **universal-modder** by Rehan and universal-modder contributors
(https://github.com/rehan-remade/universal-modder, commit ${COMMIT}), used under the MIT License (LICENSE).

- The \`um\` CLI is reimplemented in C++20 (\`src/\`). It is the same code the A.S.T.R.O AI engine
  (https://github.com/esoexe/astro-ai) compiles as its built-in \`um\` tool.
- The skills and agents are upstream's text. Where a skill ran a Python helper script, it now names the built-in
  command, and the edition notes say what changed. The guardrails are unchanged: mod only games the user owns, keep
  game files and decompiled code out of anything shared, no cheats against other players, back up saves first.
- The knowledge base, docs, examples (without their Python scripts), hooks and agent manifests are upstream files.
  Contributions to the knowledge base belong upstream: \`um kb pr\` opens them there.
- Space Grotesk and JetBrains Mono (SIL Open Font License 1.1) are embedded for \`um video\` and written, with
  their licence texts, to the um data folder the first time a video needs them.

This edition is not made or endorsed by the universal-modder authors, fal, Sprixen, Voidless or ComfyUI.
EOF
cat > "$OUT/README.md" <<EOF
# universal-modder-cpp

The C++ edition of [universal-modder](https://github.com/rehan-remade/universal-modder) by Rehan and
universal-modder contributors: skills, agents, a shared knowledge base and the \`um\` toolkit for modding games you
own with an AI agent. Here \`um\` is **one native program** (C++20). It needs no Python, uv, Pillow, numpy or Blender.

> Upstream is the original and the place for knowledge-base notes (\`um kb pr\` sends them there). This edition
> tracks upstream commit \`${COMMIT:0:12}\`. PORTING.md lists every upstream Python file and what replaced it.

## Get \`um\`

- **Download** a binary from [Releases](https://github.com/esoexe/universal-modder-cpp/releases): Linux x64 (needs
  libcurl, which most distributions ship), macOS arm64, Windows x64 (a single .exe, no DLLs). Put it on PATH as \`um\`.
- **Or build it:** \`make\` (a C++20 compiler, libcurl and zlib), then \`make test\`. \`make windows\` cross-builds
  \`build/um.exe\` with MinGW-w64. In a clone, \`bin/um\` runs \`build/um\`, building it the first time.

## What \`um\` does

| Group | |
|---|---|
| \`scan\` | installed games, engine, anti-cheat, loaders, saves, routes |
| \`fal\`, \`comfy\`, \`sprixen\`, \`voidless\` | art, 3D, audio and video through fal (your key), a local ComfyUI, or Sprixen / Voidless (paid, your keys) |
| \`sprite\` | cut out, fit, pixelate, recolour, pack sprite sheets |
| \`render3d\` | GLB / glTF to sprite frames from a game's camera: aoe2, iso8, trueiso, topdown, side, turntable; motions; shadow passes |
| \`video\` | contact sheets and EDL showcase edits (needs ffmpeg) |
| \`win\` | launch, screenshot, input and record on Windows (also from WSL) |
| \`backup\`, \`publish\`, \`kb\` | save snapshots, a pre-release lint, the knowledge base |
| \`fastfile\`, \`iwi\`, \`weapon\` | Call of Duty 4 / World at War rawfiles, IWI textures, weapon files |
| \`b3d\` | Blitz3D viewmodels: dump, Luau keyframes, a skinned glTF for Roblox, preview frames |
| \`upk\` | decompress LZO-compressed Unreal Engine 3 packages for UELib |
| \`sld\` | Age of Empires II DE sprites: info, roundtrip, to PNG, PNG frames to .sld |
| \`vpk\`, \`vtf\` | Valve archives and textures: list, extract, decode |

Every group has \`--help\`. The last four replace the Python helper scripts that upstream's skills and examples ran.

## Use it with an agent

Open the clone in Claude Code, Codex, Cursor, Gemini CLI or OpenCode. The skills (\`skills/\`, with copies in
\`.claude/skills\` and \`.agents/skills\`), the agents (\`agents/\`), the fal MCP server (\`.mcp.json\`, needs \`FAL_KEY\`) and
the PATH hook work as they do upstream. Start with the **mod-any-game** skill. AGENTS.md has the loop and the rules.

## Rules (the same as upstream)

Only games you own. Back up saves before touching anything. Never ship game files, extracted assets or decompiled
code (\`um publish check\` looks for them, and for secrets). No cheats or advantages against other players; stay away
from games with online anti-cheat.

## Limits

- \`render3d\` is a software renderer, not Blender Cycles: same cameras and file names, simpler light.
- \`um win\` works on Windows and WSL only. \`um video\` needs ffmpeg (\`um win setup\` fetches a checksum-verified build).
- The examples' one-off scripts are not ported (PORTING.md says which and why).
- The macOS build is checked in CI on Apple Silicon only.

## Licence

MIT, like upstream (LICENSE). Made by the Astro AI team as part of A.S.T.R.O AI (https://github.com/esoexe/astro-ai),
whose engine compiles the same \`um\` code as a built-in tool.
EOF
echo "universal-modder-cpp exported to $OUT: $(cd "$OUT" && find . -path ./.git -prune -o -type f -print | wc -l) files, $(find "$OUT/src" -type f | wc -l) source files, upstream ${COMMIT:0:12}"
[ -z "$(cd "$OUT" && find . -path ./.git -prune -o -name '*.py' -print)" ] || { echo "Python files left in the export" >&2; exit 1; }

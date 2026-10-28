#!/usr/bin/env bash
# Exports a clean, secret-free copy of Astro AI for publishing (GitHub).
#   tools/export_repo.sh <out-dir> [github-login]
# Only an explicit allowlist is copied. Keys, env files, user data, uploads and the WITH-KEYS builds are never
# included, and tools/scan_secrets.js must pass before the export is considered usable.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: tools/export_repo.sh <out-dir> [github-login]}"
LOGIN="${2:-YOUR-GITHUB-NAME}"
SITE="https://${LOGIN,,}.github.io/astro-ai"

rm -rf "$OUT"; mkdir -p "$OUT"

ALLOW=(
  workspace_server.cpp platform.h astro_ext.inc workspace_code.inc workspace_intel.inc workspace_ide.inc workspace_agent.inc
  astro_dev.inc astro_library.inc astro_quality.inc astro_engine.inc cli ide sdk bench library .github docs
  providers.json Makefile Dockerfile .dockerignore .env.example README.md
  LICENSE LICENSE-MIT LICENSE-APACHE LICENSE-GPL THIRD-PARTY-NOTICES.md
  start_workspace.sh start_local_ai.sh start_opencode.sh
  opencode_layer web tools tests supabase desktop
  website/src website/build.js website/icons.js website/serve.js website/site.json website/README.md
)
for f in "${ALLOW[@]}"; do cp -a --parents "$f" "$OUT/"; done

# local-only leftovers that can live inside allowed folders
find "$OUT" \( -name node_modules -o -name __pycache__ -o -name .cache \) -prune -exec rm -rf {} +
rm -f "$OUT/website/netlify.toml"

# the website's downloads: CLEAN edition only
mkdir -p "$OUT/website/downloads"
cp desktop-builds/clean/*CLEAN* "$OUT/website/downloads/"
cp desktop-builds/clean/astro-ai-vscode-*.vsix "$OUT/website/downloads/"

# point the site at GitHub Pages
node -e '
  const fs = require("fs"), f = process.argv[1], j = JSON.parse(fs.readFileSync(f, "utf8"));
  j.url = process.argv[2]; j.repo = process.argv[3]; fs.writeFileSync(f, JSON.stringify(j, null, 2) + "\n");' "$OUT/website/site.json" "$SITE" "https://github.com/$LOGIN/astro-ai"

# repo README: a short header with the website + download links on top of the developer README
node -e '
  const fs = require("fs"), f = process.argv[1], site = process.argv[2];
  let s = fs.readFileSync(f, "utf8");
  const head = [
    "<p align=\"center\"><img src=\"desktop/assets/icon-256.png\" width=\"112\" alt=\"Astro AI icon\"></p>",
    "",
    "<h1 align=\"center\">Astro AI</h1>",
    "<p align=\"center\"><b>One brain, many minds.</b> A free desktop app where a team of free AI models answers as one.</p>",
    "<p align=\"center\"><a href=\"" + site + "/\">Website</a> · <a href=\"" + site + "/download.html\">Download</a> · <a href=\"" + site + "/docs.html\">Docs</a> · <a href=\"" + site + "/api.html\">Astro API</a> · <a href=\"" + site + "/changelog.html\">Changelog</a></p>",
    "",
    "<p align=\"center\"><a href=\"" + site + "/#trailer\"><img src=\"website/src/static/assets/video/astro-ai-trailer-poster.jpg\" width=\"860\" alt=\"Watch the Astro AI trailer\"></a><br><sub>▶ <a href=\"" + site + "/#trailer\">Watch the 90-second trailer</a> (real app footage) · <a href=\"" + site + "/assets/video/astro-ai-trailer.mp4\">MP4</a><br>Music: “Clean Soul” by Kevin MacLeod (incompetech.com), CC BY 4.0</sub></p>",
    "",
    "<p align=\"center\"><img src=\"website/src/static/assets/img/shots/chat-answer.webp\" width=\"860\" alt=\"Astro AI: a One Brain answer written by 19 models\"></p>",
    "",
    "> **Downloads:** Windows, Linux and macOS builds are on the [download page](" + site + "/download.html). They are the *Clean* edition and ship with no API keys.",
    "",
  ].join("\n");
  s = s.replace(/^# Astro AI\s*\n/, head + "\n## About\n\n");
  s = s.trimEnd() + "\n\n## License\n\nAstro AI is available under your choice of the **MIT**, **Apache-2.0** or **GPL-3.0** license; see [LICENSE](LICENSE). Bundled third-party components keep their own licenses; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).\n";
  fs.writeFileSync(f, s);' "$OUT/README.md" "$SITE"

cat > "$OUT/.gitignore" <<'EOF'
# ---- secrets and personal data: never commit ----
.env
.workspace.env
.astro_keys.env
.astro_auth.json
.astro_sessions
seed-keys.env
gateway.env
astro_data/
uploads/
desktop-builds/
*WITH-KEYS*

# ---- build output ----
/workspace_server
/astro_measure
/astro
/ide/vscode/*.vsix
website/dist/
node_modules/
.cache/
*.o
EOF

# GitHub Actions workflows (website, CI, desktop builds) are copied from .github/ with the allowlist above.

chmod +x "$OUT"/*.sh "$OUT"/tests/*.sh "$OUT"/desktop/package.sh "$OUT"/desktop/mac/build_mac.sh "$OUT"/desktop/setup/build_setup.sh "$OUT"/tools/*.sh 2>/dev/null || true

echo "Exported to $OUT ($(du -sh "$OUT" | cut -f1), $(find "$OUT" -type f | wc -l) files). Scanning for secrets..."
node tools/scan_secrets.js "$OUT"

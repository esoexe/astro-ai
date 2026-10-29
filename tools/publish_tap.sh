#!/usr/bin/env bash
# Pushes the Homebrew formula + cask (made by tools/package_managers.js) to github.com/esoexe/homebrew-tap.
#   bash tools/publish_tap.sh "<commit message>"
# Token: ~/.config/astro-ai/gh_token (never printed).
set -euo pipefail
cd "$(dirname "$0")/.."
TAP="${TAP_REPO:-esoexe/homebrew-tap}"
MSG="${1:-Update A.S.T.R.O AI}"
TOKEN="$(cat ~/.config/astro-ai/gh_token)"
api() { curl -fsS -H "Authorization: Bearer $TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

README_TMP="$(mktemp)"
VER="$(node -e 'console.log(require("./website/site.json").version)')"
cat > "$README_TMP" <<EOF
# A.S.T.R.O AI Homebrew tap

[A.S.T.R.O AI](https://esoexe.github.io/astro-ai/) is a free desktop app and terminal agent: a team of free AI models that answers as one.

\`\`\`sh
# macOS: the app and the astro command
brew install --cask esoexe/tap/astro-ai

# Linux (x64): the astro command and its engine
brew install esoexe/tap/astro-ai
\`\`\`

Current version: **$VER**. Update with \`brew upgrade astro-ai\`.

The macOS app is not notarized by Apple yet. If macOS blocks the first start, right-click the app and choose Open,
or install with \`brew install --cask --no-quarantine esoexe/tap/astro-ai\`.

Free model tiers have daily limits set by each provider. Source, issues and discussions: https://github.com/esoexe/astro-ai
EOF

put() {   # put <local file> <path in tap>
  local file="$1" dest="$2" sha="" body
  sha="$(api "https://api.github.com/repos/$TAP/contents/$dest" 2>/dev/null | node -e 'let d="";process.stdin.on("data",c=>d+=c).on("end",()=>{try{process.stdout.write(JSON.parse(d).sha||"")}catch{}})' || true)"
  body="$(node -e '
    const fs = require("fs"); const [file, msg, sha] = process.argv.slice(1);
    const o = { message: msg, content: fs.readFileSync(file).toString("base64") }; if (sha) o.sha = sha;
    process.stdout.write(JSON.stringify(o));' "$file" "$MSG" "$sha")"
  api -X PUT "https://api.github.com/repos/$TAP/contents/$dest" -d "$body" > /dev/null
  echo "  $dest"
}

[ -f packaging/homebrew/Formula/astro-ai.rb ] || { echo "Run node tools/package_managers.js first." >&2; exit 1; }
echo "Publishing to $TAP:"
put packaging/homebrew/Formula/astro-ai.rb Formula/astro-ai.rb
if [ -f packaging/homebrew/Casks/astro-ai.rb ]; then put packaging/homebrew/Casks/astro-ai.rb Casks/astro-ai.rb; fi
put "$README_TMP" README.md
rm -f "$README_TMP"
echo "Done: https://github.com/$TAP"

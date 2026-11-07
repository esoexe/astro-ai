#!/usr/bin/env bash
# design ui-ux-pro-max/search: assertions of UI/UX Pro Max's own tests (scripts/tests/test_core.py, test_design_system_*.py,
# test_rtl_support.py; MIT, Next Level Builder), run against the C++ tool and the CSV data the Design Pack ships.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-design/skills/ui-ux-pro-max"
S() { run --skill-dir "$SK" ui-ux-pro-max/search "$@"; }
J() { S "$@" --json; }
cnt() { jq -r '.count' <<<"$OUT"; }
# ---- searching
J "ui minimalism" --domain style -n 1; [ "$(cnt)" -gt 0 ] && ok || bad "the literal 'ui' token is searchable"
J "accessibility contrast wcag keyboard" --domain ux -n 3; [ "$(cnt)" -gt 0 ] && ok || bad "accessibility hits ux"
J "zzqqxx totally made up gibberish" --domain ux -n 2; eq "$(cnt)" 0 "gibberish finds nothing"; eq "$(jq -r 'has("suggestions") and (has("error")|not)' <<<"$OUT")" true "suggestions, not an error"
for d in style color chart landing product ux typography icons gsap react web; do J "sourdough starter crumb fermentation" --domain $d -n 1; eq "$(cnt)" 0 "hard negative abstains: $d"; done
for st in react nextjs vue svelte astro swiftui react-native flutter nuxtjs nuxt-ui html-tailwind shadcn jetpack-compose threejs angular laravel javafx wpf winui avalonia uno uwp; do
  J "sourdough starter crumb fermentation" --stack $st -n 1; eq "$(cnt)" 0 "hard negative abstains: stack $st"
  J "performance" --stack $st -n 1; eq "$(jq -r 'has("error")' <<<"$OUT")" false "stack file is there: $st"; done
J "testimonal" --domain landing -n 3; eq "$(cnt)" 0 "typo finds nothing"; SUG=$(jq -r '.suggestions[0] // ""' <<<"$OUT"); [ -n "$SUG" ] && ok || bad "a typo gets a suggestion"
J "$SUG" --domain landing -n 3; [ "$(cnt)" -gt 0 ] && ok || bad "the first suggestion works on retry ($SUG)"
J "pricing" --domain landing -n 3; eq "$(jq -r '(.suggestions // []) | index("pricing")' <<<"$OUT")" null "a suggestion never repeats the input"
J "minimal" --domain style -n 3; eq "$(cnt)" 1 "minimal is one style"; eq "$(jq -r '.results[0]["Style ID"]' <<<"$OUT")" minimalism-and-swiss-style "minimal resolves to Swiss minimalism"
J "lucide icon"; eq "$(jq -r '.domain' <<<"$OUT")" icons "lucide routes to icons"; eq "$(cnt)" 0 "an unsupported icon library abstains"
for d in style color chart landing product ux typography icons gsap react web; do J "design" --domain $d -n 1; eq "$(jq -r 'has("error")' <<<"$OUT")" false "domain file is there: $d"; done
J "time series chart" --domain chart -n 1; eq "$(cnt)" 1 "chart"; eq "$(jq -r '.results[0]["Accessibility Grade"]' <<<"$OUT")" "deprecated: use Accessibility Risk" "legacy grade kept"; eq "$(jq -r '.results[0] | has("Accessibility Risk")' <<<"$OUT")" true "risk column"
# ---- domain detection (test_core.py TestDetectDomain)
dd() { J "$1"; eq "$(jq -r '.domain' <<<"$OUT")" "$2" "detect: $1"; }
dd "glassmorphism dark ui" style; dd "accessibility contrast wcag" ux; dd "...!!!???" style; dd "semantic color tokens palette" color
dd "lucide search icon outline" icons; dd "time series chart forecast" chart; dd "use #ff00aa as the accent" color
dd "beauty spa" product; dd "salon booking" product; dd "drag reorder react native" web
J "serif font for a blog"; eq "$(jq -r '.file' <<<"$OUT")" typography.csv "google-fonts questions are answered from the pairings"
J "C# WPF desktop app"; [ "$(jq -r '.domain' <<<"$OUT")" != color ] && ok || bad "C# is not a hex colour"
J "font pairing elegant serif body font"; eq "$(jq -r '.domain' <<<"$OUT")" typography "detect: font pairing"; eq "$(jq -r '.file' <<<"$OUT")" typography.csv "the curated pairings answer font questions"
# ---- rtl
J "editorial" --domain style --rtl full -n 5; eq "$(jq -r '[.results[].rtl_level] | all(. == "full")' <<<"$OUT")" true "--rtl full keeps only full"
S "x" --rtl full; eq "$CODE" 2 "--rtl needs style or product"; has "$ERR" "--rtl is only supported with --domain style or --domain product" "rtl message"
S "x" --stack react --rtl; eq "$CODE" 2 "--rtl with --stack"
# ---- text output
S "glasmorphsm" --domain style; has "$OUT" "No matches. This is not a match with an empty value" "honest zero"; has "$OUT" "**Closest known terms:** glassmorphism" "typo suggestion"
S "fintech crypto dashboard"; has "$OUT" "**Domain:** product (auto-detected" "auto-detected note"; has "$OUT" "Fintech/Crypto" "fintech"
S "useState memo" --stack react -n 1; has "$OUT" "## UI Pro Max Stack Guidelines" "stack header"; has "$OUT" "React.memo" "react memo guideline"
S "a" --stack nope; eq "$CODE" 2 "unknown stack"; S --variance 11 x; eq "$CODE" 2 "dial range"; S; eq "$CODE" 2 "query is required"
# ---- design system (test_design_system_*.py)
J "fitness tracking app" --design-system; eq "$(jq -r '.design_system.category' <<<"$OUT")" "Fitness/Gym App" "category from products"; eq "$(jq -r '.persistence' <<<"$OUT")" null "no persistence asked"
J "luxury skincare ecommerce dark mode" --design-system; eq "$(jq -r '.design_system.color_mode' <<<"$OUT")" dark "a dark request resolves dark"
BG=$(jq -r '.design_system.colors.background' <<<"$OUT"); [[ "$BG" =~ ^#(0|1)[0-9A-Fa-f]{5}$ ]] && ok || bad "dark background ($BG)"
J "saas analytics dashboard" --design-system --variance 9; eq "$(jq -r '.design_system.style.id' <<<"$OUT")" brutalism "bold variance picks brutalism"
J "saas analytics dashboard" --design-system --density 9 --motion 9; eq "$(jq -r '.design_system.spacing_scale.md' <<<"$OUT")" 8px "dense spacing"; eq "$(jq -r '.design_system.motion_snippet["Intensity Tier"]' <<<"$OUT")" Complex "complex motion snippet"
S "saas analytics dashboard" --design-system -p "Acme"; has "$OUT" "## Design System: Acme" "markdown header"; has "$OUT" "### Pre-Delivery Checklist" "checklist"; hasnt "$OUT" "### Design Dials" "no dials unless set"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
S "fitness tracking app" --design-system --persist --output-dir "$TMP" --page "Check-out" -p "../../etc/Evil Name"
[ -f "$TMP/design-system/etc-evil-name/MASTER.md" ] && ok || bad "project name is slugged inside the folder"; [ -f "$TMP/design-system/etc-evil-name/pages/check-out.md" ] && ok || bad "page file"
has "$(cat "$TMP/design-system/etc-evil-name/MASTER.md")" "> If that file exists, its rules **override** this Master file." "master logic"
echo "keep" >> "$TMP/design-system/etc-evil-name/MASTER.md"
J "fitness tracking app" --design-system --persist --output-dir "$TMP" -p "../../etc/Evil Name"; eq "$(jq -r '.persistence.status' <<<"$OUT")" skipped_exists "an existing master is kept"
has "$(tail -1 "$TMP/design-system/etc-evil-name/MASTER.md")" keep "untouched"
J "fitness tracking app" --design-system --persist --output-dir "$TMP" -p "../../etc/Evil Name" --force; eq "$(jq -r '.persistence.status' <<<"$OUT")" success "--force rewrites"
hasnt "$(tail -1 "$TMP/design-system/etc-evil-name/MASTER.md")" keep "rewritten"
done_ design_uipro

#!/usr/bin/env bash
# master-build/contrast: the cases of master-skills/skills/build/tools/test_build.py (ContrastTest) plus hand-worked
# ratios, run against the C++ tool. Ratios are worked by hand from the WCAG formula (noted below); the suggested colors
# were cross-checked against an independent JavaScript transcription of contrast.py's search.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-build"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
CT=(--skill-dir "$SK" master-build/contrast)
cd "$TMP" || exit 1
ratio_of() { $T "${CT[@]}" "$@" --json | jq -c '[.pairs[].ratio]'; }

# ---- upstream: ratios and parsing (#777: luminance ((119/255 + .055) / 1.055)^2.4 = 0.18448 -> 1.05 / 0.23448 = 4.48)
eq "$(ratio_of "#000" white "#777777" "#fff")" "[21.0,4.48]" "black on white 21, #777 on white 4.48"
run "${CT[@]}" "rgb(255 0 0)" white "hsl(120, 100%, 25%)" white "oklch(62.8% 0.2577 29.23)" white
eq "$OUT" "$(cat <<'EOF'
FAIL   4.00:1  rgb(255 0 0) on white  [AA text no, AA large yes, AAA no]
        try #ec0000 for the foreground (4.60:1, same hue)
PASS   5.17:1  hsl(120, 100%, 25%) on white  [AA text yes, AA large yes, AAA no]
FAIL   4.00:1  oklch(62.8% 0.2577 29.23) on white  [AA text no, AA large yes, AAA no]
        try #ec0000 for the foreground (4.60:1, same hue)
1 of 3 pairs meet 4.5:1 (text).
EOF
)" "rgb(), hsl() and oklch() read as #ff0000, #008000 (hsl gives exactly .5) and #ff0000"
eq "$CODE" 1 "a failing pair: exit 1"

# ---- upstream: the suggestion passes (L stops at cbrt(0.18333) = #777777, one -0.005 nudge lands on 117 = #757575)
run "${CT[@]}" "#999" "#fff" --json
eq "$(echo "$OUT" | jq -c '.pairs[0] | [.ratio, .pass, .required, .suggest]')" '[2.85,false,4.5,{"color":"#757575","ratio":4.61}]' "#999 on white: try #757575"
eq "$(echo "$OUT" | jq -c 'keys_unsorted, (.pairs[0] | keys_unsorted)')" $'["tool","version","min","pairs"]\n["fg","bg","fg_color","bg_color","ratio","aa_text","aa_large","aaa_text","pass","required","suggest"]' "JSON keys, in order"
has "$OUT" $'{\n  "tool": "contrast",\n  "version": "1.0.0",' "indent 2"
run "${CT[@]}" "#3366ff" "#ffffff" "#888888" "#111111" "#ff00ff" "#000000" --min aaa
has "$OUT" "try #1c46df for the foreground (7.02:1, same hue)" "blue darkens for AAA"
has "$OUT" "try #9f9f9f for the foreground (7.13:1, same hue)" "grey on a dark background lightens"
has "$OUT" "try #ff29fe for the foreground (7.00:1, same hue)" "magenta on black lightens"
has "$OUT" "0 of 3 pairs meet 7.0:1 (aaa)." "the AAA summary"
eq "$($T "${CT[@]}" "#ffcc00" "#ffffff" --min large --json | jq -c '.pairs[0] | [.ratio, .required, .suggest.color, .suggest.ratio]')" '[1.51,3.0,"#b48f00",3.06]' "--min large needs 3.0"

# ---- upstream: CSS tokens and exit codes
printf ':root { --ink: #111; --paper: #fff; --muted: #aaa; }\n@media (prefers-color-scheme: dark) { :root { --ink: #eee; --paper: #111; } }' > s.css
run "${CT[@]}" --css s.css --pair ink:paper --json
eq "$CODE" 0 "ink on paper passes"
run "${CT[@]}" --css s.css --pair muted:paper --json
eq "$CODE" 1 "muted on paper fails"
run "${CT[@]}" --css s.css --dark --pair ink:paper
has "$OUT" "ink (#eee) on paper (#111)" "--dark reads the prefers-color-scheme block"

# ---- colors (alpha composites over the background; the background over white)
eq "$(ratio_of "#00000080" white "rgba(0, 0, 0, 50%)" white "rgb(0 0 0 / 0.5)" white "#000" "rgb(0 0 0 / .5)")" "[4.0,3.98,3.98,5.28]" "alpha: 128/255 -> 4.00, .5 -> 3.98, a half-black background -> 5.28"
eq "$(ratio_of "hsl(180deg, 100%, 50%)" black "hsl(0.5turn 100% 50%)" black cyan black)" "[16.75,16.75,16.75]" "hue units: deg, turn; cyan on black (0.7874 + .05) / .05"
run "${CT[@]}" "hsl(200grad 100% 50%)" black
eq "$CODE:$ERR" "2:contrast: could not read color 'hsl(200grad 100% 50%)' (could not convert string to float: '200g')" "grad: the original tests rad first, so 200grad reads as rad (kept as is)"
eq "$(ratio_of "hsl(120 100 25)" white)" "[5.17]" "hsl without % reads 0..100"
run "${CT[@]}" transparent white
eq "$OUT" "$(printf 'FAIL   1.00:1  transparent on white  [AA text no, AA large no, AAA no]\n        try #757575 for the foreground (4.61:1, same hue)\n0 of 1 pairs meet 4.5:1 (text).')" "transparent is white on white"

# ---- CSS: comments, first definition wins, dark blocks then [data-theme=dark], var() fallbacks and loops
cat > t.css <<'EOF'
/* --ink: red; */
:root { --ink: #111; --paper: #fff; --ink: #222; --link: var(--brand, #0645ad); --loop: var(--loop); }
[data-theme="dark"] { --paper: #000; }
@media (prefers-color-scheme: dark) { :root { --paper: #121212; --ink: #eee; } }
EOF
run "${CT[@]}" --css t.css --pair ink:paper --pair "link on paper" --pair "#fff/--ink"
eq "$OUT" "$(cat <<'EOF'
PASS  18.88:1  ink (#111) on paper (#fff)  [AA text yes, AA large yes, AAA yes]
PASS   8.53:1  link (#0645ad) on paper (#fff)  [AA text yes, AA large yes, AAA yes]
PASS  18.88:1  #fff on --ink (#111)  [AA text yes, AA large yes, AAA yes]
3 of 3 pairs meet 4.5:1 (text).
EOF
)" "tokens: the commented --ink is ignored, the first --ink wins, var() falls back"
run "${CT[@]}" --css t.css --dark --pair ink:paper
has "$OUT" "PASS  18.10:1  ink (#eee) on paper (#000)" "--dark: the [data-theme=dark] block applies after the media block"
run "${CT[@]}" --css t.css --pair loop:paper
eq "$CODE:$ERR" "2:contrast: var() loop at var(--loop)" "a var() loop"
run "${CT[@]}" --css t.css --pair=--nope:paper
eq "$CODE:$ERR" "2:contrast: undefined variable --nope" "an undefined token"
run "${CT[@]}" "var(--x)" white
eq "$CODE:$ERR" "2:contrast: undefined variable --x" "var() without a fallback"
run "${CT[@]}" "var(--x, #333)" white
has "$OUT" "PASS  12.63:1  var(--x, #333) (#333) on white" "var() fallback without --css"

# ---- bad input: exit 2
run "${CT[@]}" paper white
eq "$CODE:$ERR" "2:contrast: unsupported color: paper" "a token name without --css"
run "${CT[@]}" "#12345" white
eq "$CODE:$ERR" "2:contrast: bad hex color: #12345" "five hex digits"
run "${CT[@]}" "rgb(1 2)" white
eq "$ERR" "contrast: expected 3 channels in rgb(1 2)" "two channels"
run "${CT[@]}" "hsl(abcdeg 50% 50%)" white
eq "$ERR" "contrast: could not read color 'hsl(abcdeg 50% 50%)' (could not convert string to float: 'abc')" "the float() error is quoted"
run "${CT[@]}" "rgb(0 0 0 / )" white
eq "$ERR" "contrast: could not read color 'rgb(0 0 0 / )' (could not convert string to float: '')" "an empty alpha"
run "${CT[@]}" --pair "rgb(0 0 0):white"
eq "$ERR" "contrast: pair must look like fg:bg, fg/bg or 'fg on bg': rgb(0 0 0):white" "function colors cannot be split"
run "${CT[@]}" "#fff"
eq "$CODE:$ERR" "2:contrast: colors must come in FG BG pairs" "an odd number of colors"
run "${CT[@]}"
eq "$CODE:$ERR" "$(printf '2:usage: contrast [-h] [--css FILE] [--pair FG:BG] [--dark]\n                [--min {aaa,large,text,ui}] [--json] [--version]\n                [colors ...]\ncontrast: give FG BG colors or --pair (with --css for token names)')" "nothing to check: usage and a hint"
run "${CT[@]}" --css missing.css --pair a:b
eq "$CODE:$ERR" "2:contrast: [Errno 2] No such file or directory: 'missing.css'" "a missing CSS file"
run "${CT[@]}" --min huge "#000" white
eq "$CODE" 2 "a bad --min"
has "$ERR" "argument --min: invalid choice: 'huge' (choose from 'aaa', 'large', 'text', 'ui')" "lists the choices"

# ---- version and help
run "${CT[@]}" --version
eq "$CODE:$OUT" "0:contrast 1.0.0" "--version"
run "${CT[@]}" -h
has "$OUT" $'  --pair FG:BG          pair of token names or colors, e.g. ink:paper or \'--\n                        muted on --bg\' (repeatable)' "help wraps like textwrap (it splits '--muted after the quote)"
has "$OUT" $'  --min {aaa,large,text,ui}\n                        required level: text 4.5 (default), large 3, ui 3, aaa\n                        7' "a long option takes its own line"
has "$OUT" "  --version             show program's version number and exit" "the version action's help"
has "$OUT" 'Examples: contrast "#777" white | contrast --css styles.css --pair ink:paper' "the epilog"
done_ "master-build/contrast"

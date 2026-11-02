#!/usr/bin/env bash
# master-build/sitecheck: the SiteCheckTest cases of master-skills/skills/build/tools/test_build.py (the planted "bad"
# site, the clean "good" site, an external site on a second local server), with the exact report lines worked out
# by hand from the fixture line numbers; then urllib's behaviour on the wire (chunked bodies, redirect rules and
# loops, retries, timeouts, charsets) against a scripted local server (node). Needs jq; no network beyond 127.0.0.1.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-build"
TMP=$(mktemp -d)
SC=(--skill-dir "$SK" master-build/sitecheck)
SV=(--skill-dir "$SK" master-build/serve)
PIDS=()
trap 'kill ${PIDS[@]} 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
sc() { OUT=$($T "${SC[@]}" "$@" 2>"$TMP/err"); CODE=$?; ERR=$(cat "$TMP/err"); }
start() {   # start NAME DIR: a serve process; sets URL
    $T "${SV[@]}" "$2" --port 0 > "$1.out" 2> "$1.err" &
    PIDS+=($!)
    for _ in $(seq 1 50); do grep -q "Ctrl+C" "$1.out" 2>/dev/null && break; sleep 0.1; done
    URL=$(grep -o 'http://127.0.0.1:[0-9]*/' "$1.out" | head -1)
}
# png W H [TOTAL]: a PNG signature + IHDR (all sitecheck reads), padded with random bytes to TOTAL bytes
png() {
    local w=$1 h=$2 total=${3:-0}
    printf '\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR'
    printf "\\x$(printf %02x $((w>>24&255)))\\x$(printf %02x $((w>>16&255)))\\x$(printf %02x $((w>>8&255)))\\x$(printf %02x $((w&255)))"
    printf "\\x$(printf %02x $((h>>24&255)))\\x$(printf %02x $((h>>16&255)))\\x$(printf %02x $((h>>8&255)))\\x$(printf %02x $((h&255)))"
    printf '\x08\x00\x00\x00\x00\x00\x00\x00\x00'
    [ "$total" -gt 33 ] && head -c $((total-33)) /dev/urandom
}
mk() { mkdir -p "$(dirname "$1")"; cat > "$1"; }

GOOD_HEAD() {   # GOOD_HEAD TITLE
cat <<EOF
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>$1</title>
<meta name="description" content="A fixture page.">
<meta property="og:image" content="https://example.com/share.png">
<link rel="icon" href="/favicon.svg">
</head>
EOF
}

# ---- the external site and the planted bad site ----
mkdir -p external; png 4 3 > external/ok.png
start ext external; EXT=$URL
GOOD_HEAD Home | grep -v 'name="description"' | sed 's#content="https://example.com/share.png"#content="/img/hero.png"#' > bad.head
{ cat bad.head; cat <<EOF
<body>
<link rel="stylesheet" href="/css/site.css">
<link rel="stylesheet" href="/css/missing.css">
<h1>Home</h1>
<nav><a href="/about.html">About</a> <a href="/nope.html">Gone</a> <a href="#pricing">Pricing</a>
<a href="#">Soon</a> <a href="/about.html#team">Team</a> <a href="/about.html#nobody">Nobody</a>
<a href="/zoom.html">Zoom</a> <a href="${EXT}dead">Partner</a></nav>
<img src="/img/hero.png" width="4" height="3">
<img src="/img/heavy.png" alt="Heavy">
<img src="/img/wide.png" alt="Wide" width="100" height="50">
<img src="/img/missing.png" alt="Missing" width="10" height="10">
<img src="${EXT}ok.png" alt="Partner logo" width="4" height="3">
<section id="dup">A</section><section id="dup">B</section>
<a href="/docs/"><svg viewBox="0 0 10 10"><path d="M0 0h1"/></svg></a>
<button><svg viewBox="0 0 10 10"><path d="M0 0h1"/></svg></button>
<button aria-label="Close menu"><svg viewBox="0 0 10 10"></svg></button>
<form action="/subscribe"><input type="email" placeholder="Email"><button>Join</button></form>
<label>Name <input type="text" name="n"></label>
<label for="phone">Phone</label><input id="phone" type="tel">
<p>Lorem ipsum dolor sit amet.</p>
<script src="/js/app.js"></script>
<script src="/js/missing.js"></script>
</body></html>
EOF
} | mk bad/index.html
printf '<html><head></head><body>\n<h2 id="team">Team</h2>\n<a href="/deep1.html">Deeper</a>\n</body></html>\n' | mk bad/about.html
{ GOOD_HEAD Zoom | sed 's/initial-scale=1"/initial-scale=1, maximum-scale=1, user-scalable=no"/; s#</head>#<meta name="robots" content="noindex">\n</head>#'; printf '<body><h1>Zoom</h1></body></html>'; } | mk bad/zoom.html
for n in 1 2 3; do { GOOD_HEAD "D$n"; printf '<body><h1>D%s</h1><a href="/deep%s.html">n</a></body></html>' $n $((n+1)); } | mk bad/deep$n.html; done
printf '@font-face { font-family: X; src: url("/fonts/x.woff2"); }\n.hero { background: url(../img/hero.png); }\n' | mk bad/css/site.css
printf "console.log('ok');\n" | mk bad/js/app.js
printf '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"/>' | mk bad/favicon.svg
png 4 3 | mk bad/img/hero.png
png 260 260 67584 | mk bad/img/heavy.png   # exactly 66 KB, over a 50 KB budget
png 2000 1000 | mk bad/img/wide.png        # tiny file, far too many pixels

# ---- the good site ----
for spec in "index.html Home /about.html" "about.html About /"; do
    set -- $spec
    { GOOD_HEAD "$2"; printf '<body>\n<main><h1>%s</h1><p>Real words.</p><a href="%s">Next</a>\n<img src="/img/a.png" alt="A square" width="4" height="3"></main></body></html>\n' "$2" "$3"; } | mk good/$1
done
printf '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"/>' | mk good/favicon.svg
png 4 3 | mk good/img/a.png

start bad bad; BAD=$URL
sc "$BAD" --max-image-kb 50 --json; REP=$OUT
eq "$CODE" 1 "bad site exits 1"
find_() {   # find_ RULE NEEDLE [REPORT]: matching findings, one detail line each
    jq -r --arg r "$1" --arg n "$2" '.findings[] | select(.rule == $r and ((.url + " " + .page + " " + .detail) | contains($n))) | .detail' <<<"${3:-$REP}"
}
cnt() { find_ "$@" | grep -c . ; }

# test_planted_errors_are_found
eq "$(find_ broken-link /nope.html)" "HTTP 404, linked from / line 14" "broken link"
eq "$(find_ broken-link /docs/)" "HTTP 404, linked from / line 23" "folder without index.html"
eq "$(find_ broken-anchor pricing)" 'no id="pricing" on /, linked from / line 14' "broken anchor on the same page"
eq "$(find_ broken-anchor nobody)" 'no id="nobody" on /about.html, linked from / line 15' "broken anchor on another page"
eq "$(cnt broken-anchor team)" 0 "existing #team is not flagged"
eq "$(find_ broken-asset /css/missing.css)" "HTTP 404, used on / line 12" "missing stylesheet"
eq "$(find_ broken-asset /img/missing.png)" "HTTP 404, used on / line 20" "missing image"
eq "$(find_ broken-asset /js/missing.js)" "HTTP 404, used on / line 31" "missing script"
eq "$(find_ broken-asset /fonts/x.woff2)" "HTTP 404, used on /css/site.css line 1" "font from inside the stylesheet"
for rule in missing-title missing-viewport missing-lang missing-doctype missing-description missing-h1 missing-charset; do
    eq "$(cnt $rule /about.html)" 1 "$rule on /about.html"
done
eq "$(find_ zoom-disabled /zoom.html)" 'viewport is "width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no"' "zoom disabled"
eq "$(find_ img-missing-alt /img/hero.png)" "line 17" "image without alt"
eq "$(find_ duplicate-id 'id="dup"')" 'id="dup" is used 2 times (lines 22, 22)' "duplicate id"
eq "$(find_ unlabeled-field '')" '<input type="email"> at line 26 (has a placeholder, but a placeholder is not a label)' "only the placeholder-only field is unlabeled"
eq "$(find_ unnamed-button '')" "<button> at line 24" "the aria-label button is named"
eq "$(find_ unnamed-link '')" "<a> at line 23" "icon link"
# test_planted_warnings_are_found
eq "$(cnt img-missing-size /img/heavy.png)" 1 "image without size"
eq "$(find_ image-heavy /img/heavy.png)" "66 KB (260x260), over the 50 KB budget, used on / line 18" "heavy image"
eq "$(cnt image-heavy /img/wide.png)" 0 "a tiny file is not heavy"
eq "$(find_ image-oversized /img/wide.png)" "2000x1000 pixels shown at width=100 on / line 19 (about 20x too large)" "oversized image"
eq "$(find_ placeholder-link '')" 'href="#" at line 15' "placeholder link"
eq "$(cnt lorem-ipsum '')" 1 "lorem ipsum"
eq "$(find_ noindex /zoom.html)" "robots noindex is set" "noindex"
eq "$(find_ relative-og-image '')" 'og:image is "/img/hero.png"' "relative og:image"
eq "$(jq -r '.findings[] | select(.rule == "relative-og-image") | .url' <<<"$REP")" "${BAD}img/hero.png" "relative og:image url is resolved"
# test_existing_things_are_not_flagged
for okp in /js/app.js /img/hero.png /favicon.svg /about.html /css/site.css; do
    eq "$(jq --arg p "$okp" '[.findings[] | select((.rule == "broken-asset" or .rule == "broken-link") and (.url | endswith($p)))] | length' <<<"$REP")" 0 "$okp is fine"
done
eq "$(jq '[.findings[] | select(.rule == "missing-favicon")] | length' <<<"$REP")" 0 "icon link present"
# report shape
eq "$(jq '(.findings | length) as $n | [.findings[] | select(has("severity") and has("rule") and has("url") and has("line") and has("fix"))] | length == $n' <<<"$REP")" true "every finding has severity, rule, url, line, fix"
eq "$(jq -c 'keys_unsorted' <<<"$REP")" '["tool","version","target","start_url","origins","options","stats","summary","findings","pages"]' "report keys (URL target)"
eq "$(jq -c '.options' <<<"$REP")" '{"depth":3,"limit":100,"external":false,"max_image_kb":50,"strict":false,"ignore":[]}' "options"
eq "$(jq -c '.stats | del(.seconds)' <<<"$REP")" '{"pages":5,"links":9,"assets":11,"external_skipped":2,"excluded":0,"requests":20}' "stats"
eq "$(jq -c '[.pages[] | [.url, .status, .depth, .title]]' <<<"$REP" | sed "s#${BAD}#/#g")" '[["/",200,0,"Home"],["/about.html",200,1,""],["/zoom.html",200,1,"Zoom"],["/deep1.html",200,2,"D1"],["/deep2.html",200,3,"D2"]]' "pages in crawl order"
eq "$(jq -r '[.findings[].rule] | unique | length' <<<"$REP")" 23 "23 different rules fired"
eq "$(jq -r '.summary | "\(.errors) \(.warnings) \(.result)"' <<<"$REP")" "18 11 fail" "summary"
eq "$(jq -r '[.findings[].severity] | (index("warn")) as $w | .[$w:] | all(. == "warn")' <<<"$REP")" true "errors come before warnings"

# test_external_only_with_flag
eq "$(cnt external-link '')" 0 "external links not checked by default"
sc "$BAD" --external --json; EXTREP=$OUT
eq "$(find_ external-link /dead "$EXTREP")" "HTTP 404, linked from / line 16" "dead external link"
eq "$(cnt external-asset ok.png "$EXTREP")" 0 "working external image"
eq "$(jq .stats.external_skipped <<<"$EXTREP")" 0 "nothing skipped with --external"
# test_depth_and_limit
eq "$(cnt broken-link /deep4.html)" 0 "deep3 is past depth 3"
sc "$BAD" --depth 4 --json
eq "$(find_ broken-link /deep4.html "$OUT")" "HTTP 404, linked from /deep3.html line 11" "depth 4 reaches deep3"
sc "$BAD" --depth 0 --json; eq "$(jq .stats.pages <<<"$OUT")" 1 "depth 0 checks the start page only"
sc "$BAD" --limit 2 --json; eq "$(jq .stats.pages <<<"$OUT")" 2 "limit 2"
# test_ignore_and_exclude
sc "$BAD" --ignore lorem-ipsum --exclude nope --json
eq "$(cnt lorem-ipsum '' "$OUT")" 0 "ignored rule"
eq "$(cnt broken-link /nope.html "$OUT")" 0 "excluded URL"
eq "$(jq -c '[.options.ignore, .stats.excluded]' <<<"$OUT")" '[["lorem-ipsum"],1]' "ignore listed, one URL excluded"
sc "$BAD" --ignore "lorem-ipsum, noindex" --ignore missing-h1 --json
eq "$(jq -c .options.ignore <<<"$OUT")" '["lorem-ipsum","missing-h1","noindex"]' "comma lists and repeats, sorted"
# test_clean_site_passes_strict (folder mode)
sc good --strict --json
eq "$CODE" 0 "good site passes --strict"
eq "$(jq -c .findings <<<"$OUT")" "[]" "no findings"
eq "$(jq -r .summary.result <<<"$OUT")" pass "pass"
eq "$(jq -r .served_from <<<"$OUT")" "$(pwd -P)/good" "served_from"
eq "$(jq -c 'keys_unsorted' <<<"$OUT")" '["tool","version","target","start_url","origins","options","stats","summary","findings","pages","served_from"]' "report keys (folder target)"
sc good
eq "$CODE" 0 "good site exits 0"
TXT=$(sed -E 's/in [0-9]+\.[0-9]s\./in Xs./; s#http://127.0.0.1:[0-9]+/#URL/#' <<<"$OUT")
eq "$TXT" "sitecheck 1.0.0  URL/
Checked 2 pages (depth 3, limit 100), 2 links and 3 assets in Xs.
Not checked: 1 external URL (add --external to include them).

PASS: 0 errors, 0 warnings." "good site text report"

# text report of the bad site: headings, rule groups, fix lines, "+N more pages"
sc bad --max-image-kb 50
eq "$CODE" 1 "folder mode exits 1"
has "$OUT" "ERRORS (18)" "errors heading"
has "$OUT" "WARNINGS (11)" "warnings heading"
has "$OUT" "  broken-link: Link to a page that does not load
    /nope.html  HTTP 404, linked from / line 14
    /docs/  HTTP 404, linked from / line 23
    fix: Fix the href or create the page. Paths are case-sensitive on most hosts." "a rule group"
has "$OUT" "Not checked: 2 external URLs (add --external to include them)." "external URLs skipped"
has "$OUT" "FAIL: 18 errors, 11 warnings. Fix every error, then run sitecheck again." "summary line"
sc good --max-image-kb 50 --strict
has "$OUT" "PASS: 0 errors, 0 warnings." "pass line"
# warnings only: PASS with a review note, FAIL with --strict
mkdir -p warn; { GOOD_HEAD W; printf '<body><h1>W</h1><p>lorem ipsum</p></body></html>'; } > warn/index.html; cp good/favicon.svg warn/
sc warn; eq "$CODE" 0 "warnings alone pass"; has "$OUT" "PASS: 0 errors, 1 warnings. Review the warnings before you ship." "review note"
sc warn --strict; eq "$CODE" 1 "--strict fails on warnings"; has "$OUT" "FAIL: 0 errors, 1 warnings. Fix the warnings (--strict), then run sitecheck again." "strict line"
has "$OUT" "  lorem-ipsum: Placeholder lorem ipsum text left on the page
    /  lorem ipsum found in the page text
    fix: Replace it with the owner's real words." "warning group"
# the same broken asset on several pages: "(+N more pages)"
mkdir -p multi; for p in index a b; do { GOOD_HEAD "$p"; printf '<body><h1>%s</h1><a href="a.html">a</a><a href="b.html">b</a><img src="gone.png" alt="g" width="1" height="1"></body></html>' $p; } > multi/$p.html; done; cp good/favicon.svg multi/
sc multi; has "$OUT" "    /gone.png  HTTP 404, used on / line 11 (+2 more pages)" "+2 more pages"

# test_cli_json_and_exit_codes
sc http://127.0.0.1:9/; eq "$CODE" 2 "server down exits 2"
eq "$ERR" "sitecheck: could not load http://127.0.0.1:9/ (connection refused (is the server running?))" "server down message"
sc --list-rules; eq "$CODE" 0 "--list-rules"
eq "$(head -3 <<<"$OUT")" "Rules (error = fails the check, warn = review before shipping):
  broken-link          error  Link to a page that does not load
                              fix: Fix the href or create the page. Paths are case-sensitive on most hosts." "rules layout"
eq "$(grep -c '^  [a-z]' <<<"$OUT")" 30 "30 rules"
has "$OUT" "  external-unverified  warn   External URL could not be verified (blocked, rate limited, server error or timed out)" "last rule"
sc; eq "$CODE" 2 "no target"
eq "$(tail -1 <<<"$ERR")" "sitecheck: give a URL, a folder or an .html file (or --list-rules)" "no target message"
has "$ERR" "usage: sitecheck [-h] [--depth DEPTH]" "usage first"
sc good --ignore nope,zap; eq "$CODE" 2 "unknown rule"; eq "$ERR" "sitecheck: unknown rule(s): nope, zap (see --list-rules)" "unknown rule message"
sc good --exclude '('; eq "$CODE" 2 "bad regex"; has "$ERR" "sitecheck: bad --exclude regex: missing ), unterminated subpattern" "bad regex message"
sc good --depth -1; eq "$ERR" "sitecheck: --depth must be >= 0, --limit and --workers >= 1" "depth check"
sc good --limit 0; eq "$CODE" 2 "limit check"
sc good --workers 0; eq "$CODE" 2 "workers check"
sc ./no/such/place; eq "$CODE" 2 "missing path"; eq "$ERR" "sitecheck: no such file, folder or URL: ./no/such/place" "missing path message"
sc --version; eq "$OUT" "sitecheck 1.0.0" "version"
sc "ftp://x/"; eq "$ERR" "sitecheck: no such file, folder or URL: ftp://x/" "other schemes are paths"
# .html file and file:// targets serve the file's folder and start at it
sc good/about.html --json; eq "$(jq -r '.start_url | sub("http://127.0.0.1:[0-9]+"; "")' <<<"$OUT")" "/about.html" "file target"
eq "$(jq -r .served_from <<<"$OUT")" "$(pwd -P)/good" "file target serves its folder"
sc "file://$(pwd -P)/good/about.html" --json; eq "$(jq -r '.start_url | sub("http://127.0.0.1:[0-9]+"; "")' <<<"$OUT")" "/about.html" "file:// target"
mkdir -p "sp ace"; cp good/index.html good/favicon.svg "sp ace/"; mkdir -p "sp ace/img"; cp good/img/a.png "sp ace/img/"
sc "file://$(pwd -P)/sp%20ace/index.html" --json; eq "$(jq -r '.start_url | sub("http://127.0.0.1:[0-9]+"; "")' <<<"$OUT")" "/index.html" "file:// is unquoted"
# not HTML / not loadable start pages
sc "${BAD}js/app.js"; eq "$CODE" 2 "not html"; eq "$ERR" "sitecheck: ${BAD}js/app.js is not an HTML page (content-type: text/javascript)" "not html message"
sc "${BAD}nope.html"; eq "$ERR" "sitecheck: could not load ${BAD}nope.html (HTTP 404)" "404 start page"
# --report writes the same JSON (indent 2, no trailing newline) and creates folders
sc good --report out/deep/r.json --json
eq "$(cat out/deep/r.json)" "$OUT" "report file = --json output"
eq "$(tail -c 1 out/deep/r.json | od -An -c | tr -d ' ')" "}" "no trailing newline"
# --spa and --clean-urls reach serve
mkdir -p app; { GOOD_HEAD App; printf '<body><h1>App</h1><a href="/route/x">r</a><a href="/about">a</a></body></html>'; } > app/index.html; cp good/favicon.svg app/; cp good/index.html app/about.html
sc app --json; eq "$(jq -r '[.findings[] | select(.rule == "broken-link") | .url | sub("http://127.0.0.1:[0-9]+"; "")] | join(",")' <<<"$OUT")" "/about,/route/x" "plain folder"
sc app --spa --clean-urls --json; eq "$(jq '[.findings[] | select(.rule == "broken-link")] | length' <<<"$OUT")" 0 "spa + clean urls"
# srcset, CSS in <style> and style="", <base href>, normalize()
mkdir -p misc/img misc/sub; { GOOD_HEAD M; cat <<'EOF'
<body><h1>M</h1>
<style>
.a { background: url('/img/s1.png') }
/* url(/img/commented.png) */
.b { background: url(  "/img/s2.png"  ) }
@import url(/style2.css);
</style>
<img src="/img/a.png" srcset="/img/a.png 1x, /img/b.png 2x,/img/c.png" alt="x" width="1" height="1">
<div style="background-image: url(/img/inline.png)"></div>
<a href="HTTP://127.0.0.1:PORT/a b#x">n</a>
<picture><source srcset="/img/p.webp 1x, /img/p2.webp 2x"></picture>
<link rel="preload" as="font" href="/f.woff2">
<video src="/v.mp4" poster="/img/poster.png"></video>
<a href="javascript:void(0)">j</a> <a href="">e</a> <a href="mailto:a@b.c">m</a> <a href="tel:1">t</a>
<a href="file:///etc/passwd">f</a>
<img src="" alt="empty">
<img src="data:image/png;base64,AAAA" alt="d">
</body></html>
EOF
} > misc/index.html
cp good/favicon.svg good/img/a.png misc/ 2>/dev/null; cp good/img/a.png misc/img/a.png
start misc misc; MISC=$URL; MPORT=${MISC##*:}; MPORT=${MPORT%/}
sed -i "s/PORT/$MPORT/" misc/index.html
sc "$MISC" --json; M=$OUT
broken() { jq -r '[.findings[] | select(.rule == "broken-asset") | (.url | sub("http://127.0.0.1:[0-9]+"; "")) + " " + .detail] | join("|")' <<<"$M" | sed -E 's#http://127.0.0.1:[0-9]+##g'; }
eq "$(broken)" "/img/s1.png HTTP 404, used on / line 13|/img/s2.png HTTP 404, used on / line 15|/style2.css HTTP 404, used on / line 16|/img/b.png HTTP 404, used on / line 18|/img/c.png HTTP 404, used on / line 18|/img/inline.png HTTP 404, used on / line 19|/img/p.webp HTTP 404, used on / line 21|/img/p2.webp HTTP 404, used on / line 21|/f.woff2 HTTP 404, used on / line 22|/img/poster.png HTTP 404, used on / line 23|/v.mp4 HTTP 404, used on / line 23|/ <img> at line 26 has an empty src" "assets from srcset, <style>, style=, preload, media (same line: URL order)"
hasnt "$M" "commented.png" "CSS comments are skipped"
eq "$(jq -r '[.findings[] | select(.rule == "broken-link") | .url] | join(",")' <<<"$M")" "${MISC}a%20b" "normalize: scheme lowercased, space quoted, fragment dropped"
eq "$(jq -r '[.findings[] | select(.rule == "placeholder-link") | .detail] | join("|")' <<<"$M")" 'href="javascript:void(0)" at line 24|href="" at line 24' "javascript: and empty hrefs"
eq "$(jq -r '[.findings[] | select(.rule == "local-url") | .detail] | join("|")' <<<"$M")" "link at line 25 points at a file:// path" "file:// link"
eq "$(jq -r '[.findings[] | select(.rule == "img-missing-size" or .rule == "img-missing-alt") | .rule + " " + .url] | join("|")' <<<"$M")" "img-missing-size <img>|img-missing-size inline data: image" "unsized empty-src and data: images, named as such"
eq "$(jq -r '.stats.links' <<<"$M")" 1 "mailto:, tel:, javascript: are not links to check"
# image_size on the formats sitecheck reads: GIF, JPEG (SOF0 after APP0), WebP VP8X, AVIF ispe
mkdir -p fmt
printf 'GIF89a\x20\x03\x58\x02' > fmt/a.gif
printf '\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xc0\x00\x11\x08\x02\x58\x03\x20\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01' > fmt/a.jpg
printf 'RIFF\x00\x00\x00\x00WEBPVP8X\x0a\x00\x00\x00\x00\x00\x00\x00\x1f\x03\x00\x57\x02\x00' > fmt/a.webp
printf '\x00\x00\x00\x1cftypavif\x00\x00\x00\x00avifmif1miaf\x00\x00\x00\x14ispe\x00\x00\x00\x00\x00\x00\x03\x20\x00\x00\x02\x58' > fmt/a.avif
{ GOOD_HEAD F; printf '<body><h1>F</h1>'; for x in gif jpg webp avif; do printf '<img src="a.%s" alt="a" width="100" height="75">\n' $x; done; printf '</body></html>'; } > fmt/index.html
cp good/favicon.svg fmt/
sc fmt --json
eq "$(jq -r '[.findings[] | select(.rule == "image-oversized") | .detail] | join("|")' <<<"$OUT")" "800x600 pixels shown at width=100 on / line 11 (about 8x too large)|800x600 pixels shown at width=100 on / line 12 (about 8x too large)|800x600 pixels shown at width=100 on / line 13 (about 8x too large)|800x600 pixels shown at width=100 on / line 14 (about 8x too large)" "GIF, JPEG, WebP and AVIF sizes"

# ---- urllib on the wire, against a scripted server ----
if command -v node >/dev/null; then
cat > fake.js <<'EOF'
const net = require('net'); const fs = require('fs');
const log = process.argv[2]; let flaky = 0;
const page = (t) => '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><meta name="description" content="d"><title>' + t + '</title><link rel="icon" href="data:,"></head><body><h1>' + t + '</h1></body></html>';
const srv = net.createServer((s) => {
  let buf = '';
  s.on('data', (d) => {
    buf += d.toString('latin1'); if (!buf.includes('\r\n\r\n')) return;
    const [line, ...hdrs] = buf.split('\r\n'); const [method, path] = line.split(' ');
    fs.appendFileSync(log, method + ' ' + path + ' ' + (hdrs.find((h) => /^range:/i.test(h)) || '') + '\n');
    const send = (head, body) => { s.write(head + '\r\n\r\n', 'latin1'); if (body !== undefined && method !== 'HEAD') s.write(body, 'latin1'); s.end(); };
    const html = 'Content-Type: text/html';
    if (path === '/chunked') { s.write('HTTP/1.1 200 OK\r\n' + html + '\r\nTransfer-Encoding: chunked\r\n\r\n'); if (method === 'HEAD') return s.end(); const b = page('Chunked'); s.write((10).toString(16) + ';ext=1\r\n' + b.slice(0, 10) + '\r\n'); s.write((b.length - 10).toString(16) + '\r\n' + b.slice(10) + '\r\n0\r\n\r\n'); return s.end(); }
    if (path === '/loop') return send('HTTP/1.1 302 Found\r\nLocation: /loop');
    if (path === '/noloc') return send('HTTP/1.1 301 Moved');
    if (path === '/flaky') { flaky += 1; return flaky === 1 ? send('HTTP/1.1 503 Busy\r\nContent-Length: 0', '') : send('HTTP/1.1 200 OK\r\n' + html, page('Flaky')); }
    if (path === '/stall' || path === '/stall.zip') return;   /* never answers */
    if (path === '/hang-body') { s.write('HTTP/1.1 200 OK\r\n' + html + '\r\nContent-Length: 999\r\n\r\n<!doctype html>'); return; }
    if (path === '/latin') return send('HTTP/1.1 200 OK\r\nContent-Type: TEXT/HTML; Charset="ISO-8859-1"', Buffer.from(page('Caf\u00e9')).toString('latin1').replace('Caf\u00c3\u00a9', 'Caf\u00e9'));
    if (path === '/sniff') return send('HTTP/1.1 200 OK', '  \n<!DOCTYPE html><html lang="en"><head><meta charset=windows-1252><title>Q\x93x\x94</title></head><body><h1>s</h1></body></html>');
    if (path === '/noslash') return send('HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:' + srv.address().port);
    if (path === '/' ) return send('HTTP/1.1 200 OK\r\n' + html, page('Root'));
    if (path === '/space') return send('HTTP/1.1 301 Moved\r\nLocation: /a b.html');
    if (path === '/a%20b.html') return send('HTTP/1.1 200 OK\r\n' + html, page('Spaced'));
    if (path === '/see') return send('HTTP/1.1 303 See Other\r\nLocation: /');
    if (path === '/gopher') return send('HTTP/1.1 302 Found\r\nLocation: gopher://x/');
    if (path === '/headless.pdf') return method === 'HEAD' ? send('HTTP/1.1 405 No') : send('HTTP/1.1 200 OK\r\nContent-Length: 2', 'ok');
    if (path === '/media.mp4') return method === 'HEAD' ? send('HTTP/1.1 403 No') : send('HTTP/1.1 206 Partial\r\nContent-Length: 2', 'ok');
    if (path === '/gone') return send('HTTP/1.1 410 Gone\r\nContent-Length: 0', '');
    if (path === '/teapot') return send('HTTP/1.1 418 Teapot\r\nContent-Length: 0', '');
    if (path === '/robots') return send('HTTP/1.1 200 OK\r\n' + html + '\r\nX-Robots-Tag: NoIndex', page('R'));
    if (path === '/empty') return s.end();
    if (path === '/lan') return send('HTTP/1.1 200 OK\r\n' + html, page('Lan').replace('</body>', '<a href="http://localhost:3000/">dev</a>\n<img src="http://127.0.0.1:9/x.png" alt="x" width="1" height="1">\n<a href="http://app.test/">t</a></body>'));
    if (path === '/start') return send('HTTP/1.1 200 OK\r\n' + html, page('Start').replace('</body>', '<a href="/chunked">c</a><a href="/loop">l</a><a href="/noloc">n</a><a href="/headless.pdf">h</a><a href="/stall.zip">z</a><a href="/gone">g</a><a href="/teapot">t</a><a href="/empty">e</a><a href="/stall">s</a><video src="/media.mp4"></video></body>'));
    send('HTTP/1.1 404 Not Found\r\nContent-Length: 0', '');
  });
  s.on('error', () => {});
});
srv.listen(0, '0.0.0.0', () => { fs.writeFileSync(log + '.port', String(srv.address().port)); });
EOF
: > hits.log
node fake.js "$TMP/hits.log" & PIDS+=($!)
for _ in $(seq 1 50); do [ -s hits.log.port ] && break; sleep 0.1; done
F="http://127.0.0.1:$(cat hits.log.port)"
hits() { grep -c "^$1 $2 " hits.log; }

sc "$F/chunked" --json; eq "$CODE" 0 "chunked page"; eq "$(jq -r '.pages[0].title' <<<"$OUT")" "Chunked" "chunked body joined"
sc "$F/loop"; eq "$ERR" "sitecheck: $F/loop is not an HTML page (content-type: unknown)" "a redirect loop ends as HTTPError 302 (which counts as ok)"
eq "$(hits GET /loop)" 5 "the same URL is followed 4 times, then urllib gives up"
sc "$F/noloc"; eq "$ERR" "sitecheck: $F/noloc is not an HTML page (content-type: unknown)" "30x without Location"
: > hits.log
sc "$F/flaky" --json; eq "$CODE" 0 "503 is retried once"; eq "$(hits GET /flaky)" 2 "two requests"; eq "$(jq .stats.requests <<<"$OUT")" 2 "requests counts the retry (a data: icon link skips the favicon check)"
sc "$F/stall" --timeout 0.3; eq "$ERR" "sitecheck: could not load $F/stall (timed out after 0.3s)" "no response: read timeout"
sc "$F/hang-body" --timeout 0.3 --json; eq "$CODE" 1 "body timeout: status kept, page checked with an empty body"
eq "$(jq -c '[.pages[0].status, .pages[0].title, (.findings | map(.rule) | index("missing-doctype") != null)]' <<<"$OUT")" '[200,"",true]' "empty body analysed"
sc "$F/latin" --json; eq "$(jq -r '.pages[0].title' <<<"$OUT")" "Café" "Content-Type charset (quoted, any case) decodes latin-1"
eq "$(jq '[.findings[] | select(.rule == "missing-charset")] | length' <<<"$OUT")" 0 "the header charset counts"
sc "$F/sniff" --json; eq "$(jq -r '.pages[0].title' <<<"$OUT")" "Q“x”" "no content-type: sniffed as HTML, meta charset windows-1252"
sc "$F/noslash" --json; eq "$(jq -r '.start_url' <<<"$OUT")" "$F/" "Location without a path gets /"
sc "$F/space" --json; eq "$(jq -r '.start_url' <<<"$OUT")" "$F/a%20b.html" "Location is quoted"
sc "$F/see" --json; eq "$(jq -r '.pages[0].title' <<<"$OUT")" "Root" "303"
sc "$F/gopher"; eq "$ERR" "sitecheck: $F/gopher is not an HTML page (content-type: unknown)" "redirect to another scheme: HTTPError 302"
sc "$F/robots" --json; eq "$(jq -r '[.findings[] | select(.rule == "noindex")] | length' <<<"$OUT")" 1 "X-Robots-Tag header"
sc "$F/empty"; eq "$ERR" "sitecheck: could not load $F/empty (Remote end closed connection without response)" "closed without a response"
: > hits.log
sc "$F/start" --timeout 0.3 --json; S=$OUT
eq "$(jq -r '[.findings[] | select(.rule == "broken-link") | (.url | sub("http://127.0.0.1:[0-9]+"; "")) + " " + (.detail | sub(", linked.*"; ""))] | join("|")' <<<"$S")" "/empty Remote end closed connection without response|/gone HTTP 410|/stall timed out after 0.3s|/stall.zip timed out after 0.3s|/teapot HTTP 418" "link outcomes (URL order)"
eq "$(hits HEAD /headless.pdf) $(hits GET /headless.pdf)" "1 1" "HEAD refused: GET fallback"
eq "$(grep -c '^GET /media.mp4 Range: bytes=0-1023' hits.log)" 1 "media GET fallback asks for a byte range"
eq "$(hits HEAD /stall.zip) $(hits GET /stall.zip)" "2 0" "a timed-out HEAD is retried once, never re-fetched with GET"
eq "$(hits GET /stall)" 2 "a page that times out is fetched twice"
eq "$(jq -r '[.findings[] | select(.url | endswith("/stall.zip")) | .rule + " " + (.detail | sub(", linked.*"; ""))] | join("|")' <<<"$S")" "broken-link timed out after 0.3s" "timed-out link"
eq "$(jq '[.findings[] | select(.url | endswith("/headless.pdf"))] | length' <<<"$S")" 0 "GET fallback succeeded"
eq "$(hits GET /chunked)" 1 "chunked page crawled as a page"
# a site on a non-local address: links and resources pointing at localhost are flagged (test_mixed_content_and_local_urls)
LAN=$(hostname -I 2>/dev/null | tr ' ' '\n' | grep -E '^[0-9.]+$' | grep -v '^127\.' | head -1)
if [ -n "$LAN" ]; then
    sc "http://$LAN:${F##*:}/lan" --json
    eq "$(jq -r '[.findings[] | select(.rule == "local-url") | .url + " " + .detail] | join("|")' <<<"$OUT")" "http://localhost:3000/ link at line 1 points at a local address|http://127.0.0.1:9/x.png resource at line 2 points at a local address|http://app.test/ link at line 3 points at a local address" "local URLs on a public site (line order)"
    sc "$F/lan" --json
    eq "$(jq '[.findings[] | select(.rule == "local-url")] | length' <<<"$OUT")" 0 "a local site may link to localhost"
fi
fi

done_ "master_sitecheck"

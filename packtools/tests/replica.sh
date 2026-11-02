#!/usr/bin/env bash
# The Replica pack: replica-design/contrast, replica-brand/sweep, replica-launch/listing, replica-diff/parity,
# replica-diff/imgdiff and replica-entrepreneur/reviews. The assertions port the upstream unittest files
# (replica-skill@77c9436, tests/test_*.py); the exact numbers were worked out by hand from the Python originals.
# PNG fixtures come from fixtures/mkpng.cpp, which encodes with the system zlib, so the built-in inflate is
# checked against an independent deflate. Python behaviour asserted where it is easy to get wrong: round() to
# 2 places on WCAG ratios, "" config/out treated as unset, ZWJ emoji counted as one character after NFC,
# csv rows with extra cells crash the way Python does, struct.error on a short IHDR is uncaught (rc 1).
source "$(dirname "$0")/lib.sh"
PK="$(cd "$(dirname "$0")/../../library/plugins/astro-replica/skills" && pwd)"
PNG="$FIX/replica-imgdiff"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
j() { jq -c "$1" <<<"$OUT"; }
r() { local k="$1"; shift; run --skill-dir "$PK/${k%%/*}" "$k" "$@"; }

# ================================================================ contrast
r replica-design/contrast '#000' '#fff' --json; eq "$(j '.[0].ratio')" "21.0" "black on white is 21"; eq "$CODE" 0 "black on white passes"
r replica-design/contrast '#ffffff' '#ffffff' --json; eq "$(j '.[0].ratio')" "1.0" "white on white is 1"; eq "$CODE" 1 "white on white fails"
r replica-design/contrast '#767676' '#ffffff' --json; eq "$(j '.[0] | [.ratio, .aa, .aaa]')" "[4.54,true,false]" "#767676 just passes AA"
r replica-design/contrast '#777777' '#ffffff' --json; eq "$(j '.[0] | [.ratio, .aa]')" "[4.48,false]" "#777777 just fails AA"
r replica-design/contrast '#aaaaaa' '#ffffff'; eq "$CODE" 1 "failing pair exits 1"
has "$OUT" "FAIL" "failing pair says FAIL"; has "$OUT" "1 pairs, 1 failing AA" "summary"
r replica-design/contrast '#222222' '#ffffff'; eq "$CODE" 0 "passing pair exits 0"
eq "$(head -1 <<<"$OUT")" "  AAA   15.91:1  fg                       on bg                   " "AAA row, exact"
cat > nested.json <<'EOF'
{"color": {"text": {"default": "#111", "muted": {"value": "#999"}}, "bg": "#fff", "$type": "color"}}
EOF
r replica-design/contrast nested.json --json
eq "$(j '[.[] | [.fg, .bg, .ratio, .fg_hex]]')" '[["text-default","bg",18.88,"#111"],["text-muted","bg",2.85,"#999"]]' "nested tokens flatten, default pairs"
eq "$CODE" 1 "muted grey fails"
cat > pairs.json <<'EOF'
{"color": {"text": "#111111", "text-muted": "#949494", "bg": "#ffffff", "accent": "#2563eb"},
 "pairs": [["text-muted", "bg"], ["text-muted", "bg", "large"], ["accent", "nope"], ["accent", "bg", "ui"], ["text", "bg", "huge"]]}
EOF
r replica-design/contrast pairs.json --json
eq "$(j '[.[] | .aa]')" '[false,true,null,true,true]' "large text passes at 3:1, normal does not"
eq "$(j '.[2].error')" '"unknown token"' "unknown token"; eq "$(j '.[4].size')" '"normal"' "unknown size becomes normal"
eq "$(j '.[1].ratio')" "3.03" "#949494 on white"
r replica-design/contrast pairs.json
has "$OUT" "  ??    accent                   on nope                 unknown token" "unknown token row"
has "$OUT" "  PASS   " "ui pairs grade PASS"; has "$OUT" "5 pairs, 2 failing AA" "summary counts errors as failing"
printf '{"color": {"text": "#111", "bg": "#fff", "accent": "#2563eb"}}' > dp.json
r replica-design/contrast dp.json --json; eq "$(j '[.[] | [.fg, .bg]]')" '[["text","bg"]]' "accent is not a text colour"
r replica-design/contrast "$PK/replica-design/tokens.json"; eq "$CODE" 0 "shipped tokens template passes"
printf '{"color": {"accent": "#2563eb"}}' > np.json
r replica-design/contrast np.json; eq "$CODE" 2 "no pairs exits 2"; has "$ERR" 'no pairs to check. Add a "pairs" list' "no pairs message"
r replica-design/contrast missing.json; eq "$CODE" 2 "missing file exits 2"; has "$ERR" "contrast: [Errno 2] No such file or directory: 'missing.json'" "missing file message"
printf '{"color": ' > broken.json
r replica-design/contrast broken.json; eq "$CODE" 2 "bad JSON exits 2"; has "$ERR" "contrast: Expecting value: line 1 column 11 (char 10)" "JSON error"
printf '[1]' > list.json
r replica-design/contrast list.json; eq "$CODE" 1 "a list is a crash in Python"; has "$ERR" "AttributeError: 'list' object has no attribute 'get'" "list crash"

# ================================================================ sweep
mk() { mkdir -p "$(dirname "$1")"; printf '%b' "$2" > "$1"; }
S=proj
mk $S/src/app/page.tsx 'export default function Page() {\n  return <h1>Slotwise</h1>\n}\n'
mk $S/src/components/CalendlyEmbed.tsx '// left over\n'
mk $S/src/lib/copy.ts "export const hero = 'The calendly alternative'\n"
mk $S/src/styles/tokens.css ':root { --accent: #006BFF; --bg: #fff; }\n'
mk $S/src/lib/links.ts "const help = 'https://help.calendly.com/x'\n"
mk $S/src/lib/team.ts 'export const AcuitySchedulingSync = 1\n'
mk $S/replica/recon.md '# Recon: Calendly\n'
mk $S/node_modules/x/index.js 'calendly\n'
mk $S/public/logo.png '\x89PNG\x00calendly'
mk $S/package-lock.json '{"calendly": 1}\n'
SW=(--avoid "Calendly,Acuity Scheduling" --domains calendly.com --colors "#006bff")
r replica-brand/sweep $S "${SW[@]}" --json
eq "$CODE" 1 "hits exit 1"
eq "$(j '[.[] | [.kind, (.file | split("/") | last)]] | unique | map(join(":")) | join(" ")')" '"color:tokens.css domain:links.ts name:copy.ts name:links.ts name:team.ts path:CalendlyEmbed.tsx"' "names, paths, domains and colours"
eq "$(j '[.[] | .file | select(startswith("replica") or contains("node_modules") or . == "package-lock.json" or . == "public/logo.png" or . == "src/app/page.tsx")] | length')" "0" "skips planning folder, deps, locks, binaries"
r replica-brand/sweep $S "${SW[@]}" --include-replica --json
eq "$(j '[.[] | .file] | index("replica/recon.md") != null')" "true" "--include-replica"
mk $S/src/a.css 'a { color: #06f }\n'
r replica-brand/sweep $S --colors '#0066FF' --json
eq "$(j '[.[] | .file | select(endswith("a.css"))] | length')" "1" "short hex matches long"
mk $S/brand.json '{"avoid": ["Calendly"]}'
r replica-brand/sweep $S --config $S/brand.json; eq "$CODE" 1 "--config exits 1 on hits"
mkdir -p $S/clean; mk $S/clean/index.html '<h1>Slotwise</h1>'
r replica-brand/sweep $S/clean --avoid Calendly; eq "$CODE" 0 "clean exits 0"; has "$OUT" "Clean" "says Clean"
r replica-brand/sweep $S/clean; eq "$CODE" 2 "nothing to look for exits 2"
r replica-brand/sweep $S/clean --config "" --avoid Calendly; eq "$CODE" 0 "--config '' is unset, like Python"
r replica-brand/sweep $S/clean --config nope.json; eq "$CODE" 2 "missing config exits 2"

# ================================================================ listing
good() { cat <<'EOF'
{"avoid": ["Calendly"],
 "app_store": {"name": "Slotwise: Booking Links", "subtitle": "Share a link, get booked",
   "promotional_text": "Text reminders are here.", "description": "Share one link. Clients pick a time that works.",
   "keywords": "scheduler,appointment,calendar,meetings,reminders", "whats_new": "Text reminders."},
 "google_play": {"title": "Slotwise: Booking Links", "short_description": "Share one link and let clients book a time.",
   "full_description": "Share one link. Clients pick a time that works."}}
EOF
}
good > g.json
r replica-launch/listing g.json --json; eq "$(j '[.[] | select(.level == "error")] | length')" "0" "clean listing has no errors"
r replica-launch/listing g.json; eq "$CODE" 0 "clean listing exits 0"
good | jq '.app_store.subtitle = ("x" * 31)' > over.json
r replica-launch/listing over.json; eq "$CODE" 1 "over the limit exits 1"; has "$OUT" "31 characters, limit is 30 (1 over)" "1 over"
good | jq '.google_play.full_description = "A better calendly alternative."' > name.json
r replica-launch/listing name.json; eq "$CODE" 1 "original name is an error"; has "$OUT" "mentions 'Calendly'" "names Calendly"
good | jq '.google_play.title = "#1 Best Booking App" | .app_store.keywords = "booking, links,calendar,calendar"' > claims.json
r replica-launch/listing claims.json
has "$OUT" "ranking or price claim" "ranking claim"; has "$OUT" "spaces after commas" "spaces after commas"
has "$OUT" "repeated: calendar" "repeated keyword"; has "$OUT" "already in the name" "keyword already in the name"
printf '{"app_store": {"name": "a\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbbb", "subtitle": "cafe\xcc\x81"}}' > emoji.json
r replica-launch/listing emoji.json
has "$OUT" "  name                   3 / 30    ok" "ZWJ emoji counts as one"; has "$OUT" "  subtitle               4 / 30    ok" "cafe+U+0301 is 4 after NFC"
has "$OUT" "WARN  app_store.name: emoji in a short field." "emoji warning"; has "$OUT" "1 errors, 4 warnings" "empty fields"
r replica-launch/listing "$PK/replica-launch/listing.example.json"; eq "$CODE" 0 "shipped example is clean"
good | jq 'del(.avoid) | .app_store.description += " Like Acuity, but simple."' > av.json
r replica-launch/listing av.json; eq "$CODE" 0 "no avoid list: clean"
r replica-launch/listing av.json --avoid Acuity; eq "$CODE" 1 "--avoid flags it"
printf '{"nothing": 1}' > none.json
r replica-launch/listing none.json; eq "$CODE" 1 "no store section exits 1"; has "$OUT" "no app_store or google_play section found" "no store message"
r replica-launch/listing nope.json; eq "$CODE" 2 "missing file exits 2"

# ================================================================ parity
cat > features.csv <<'EOF'
feature,area,priority,original,clone,notes
Pick a time slot,booking page,must,yes,yes,
Confirmation email,booking page,must,yes,partial,no calendar file yet
Reschedule link,booking page,must,yes,no,
Round-robin across a team,team,should,yes,no,
Custom booking questions,booking page,should,yes,yes,
Embed on a website,sharing,could,yes,no,
Their partner marketplace,integrations,could,yes,skip,their network not ours
SMS reminders,notifications,should,no,yes,ours: the top request in reviews
EOF
r replica-diff/parity features.csv --json
eq "$(j '[.feature_score, .counted, .must_have_done, .must_have_total]')" "[46.4,6,1,3]" "6.5 of 14 weighted, 1 of 3 musts"
eq "$(j '.skipped | map(.feature)')" '["Their partner marketplace"]' "skipped"
eq "$(j '.extras | map(.feature)')" '["SMS reminders"]' "extras"
eq "$(j '.missing | map(.feature)')" '["Reschedule link","Confirmation email","Round-robin across a team","Embed on a website"]' "missing in build order"
eq "$(j '[.by_area[] | [.area, .score]]')" '[["team",0.0],["sharing",0.0],["booking page",59.1]]' "by area, weakest first (6.5/11)"
r replica-diff/parity features.csv
has "$OUT" "Not shippable yet: 2 must-have" "not shippable"; has "$OUT" "their network not ours" "skip reason"
eq "$(head -1 <<<"$OUT")" "Parity: 46.4 / 100" "headline"
printf '{"score": 80.0, "mode": "layout"}' > v1.json; printf '{"score": 60.0, "mode": "layout"}' > v2.json
r replica-diff/parity features.csv --visual v1.json v2.json --json
eq "$(j '[.layout_score, .overall]')" "[70.0,51.1]" "layout 70, overall 0.8*46.4+14"
printf 'feature,priority,clone\nThing,urgent,maybe\n' > bad.csv
r replica-diff/parity bad.csv --json; eq "$(j '[(.problems | length), .feature_score]')" "[2,0.0]" "two problems, score 0"
r replica-diff/parity bad.csv --markdown; has "$OUT" "## Fix in the matrix" "markdown headings"; has "$OUT" "- [could] -: Thing, maybe" "no area is -"
printf 'feature,area\nx,y\n' > nocol.csv
r replica-diff/parity nocol.csv; eq "$CODE" 2 "missing column exits 2"; has "$ERR" "has no 'priority' column" "missing column message"
r replica-diff/parity features.csv --fail-under 80; eq "$CODE" 1 "--fail-under 80"
r replica-diff/parity features.csv --fail-under 10; eq "$CODE" 0 "--fail-under 10"
r replica-diff/parity "$PK/replica-recon/features.csv" --json; eq "$(j '.problems')" "[]" "shipped matrix parses"
: > empty.csv
r replica-diff/parity empty.csv; eq "$CODE" 2 "empty CSV exits 2"; has "$ERR" "parity: empty.csv is empty" "empty message"
printf 'feature,priority,clone\na,must,yes,EXTRA\n' > extra.csv
r replica-diff/parity extra.csv; eq "$CODE" 1 "extra cell crashes like Python"; has "$ERR" "AttributeError: 'list' object has no attribute 'strip'" "extra cell crash"

# ================================================================ reviews
cat > reviews.csv <<'EOF'
source,url,date,rating,text
app-store,https://apps.apple.com/r/1,2026-08-01,1,Way too expensive for what it is. I wish it had SMS reminders.
google-play,https://play.google.com/r/2,2026-07-15,2,The price doubled this year. Support never replied to my ticket.
g2,https://www.g2.com/r/3,2026-06-01,2,Overpriced per seat. No way to set buffer times between meetings.
reddit,https://www.reddit.com/r/x/comments/4,2026-05-01,,Honestly the subscription is the dealbreaker for my small team.
app-store,https://apps.apple.com/r/5,2026-08-10,5,"Love it, works great every day."
capterra,https://www.capterra.com/r/6,2026-07-01,1,It crashes every time I open the calendar view.
app-store,https://apps.apple.com/r/7,2026-08-02,1,Booked twice for the same slot and the client was furious.
app-store,,2026-08-01,1,Expensive and buggy.
google-play,https://play.google.com/r/9,2026-07-16,2,The price doubled this year.  Support never replied to my ticket.
EOF
r replica-entrepreneur/reviews reviews.csv --json --today 2026-10-01
eq "$(j '[.dropped, .duplicates, .reviews]')" "[1,1,7]" "unsourced dropped, duplicate removed"
eq "$(j '[.themes[] | select(.kind == "complaint")][0] | [.id, .count, .thin, (.sources | length), .score, .avg_rating, .share_pct]')" '["pricing",4,false,4,3.2,1.67,57.1]' "price is the top complaint (1+.8+.8+.6)"
eq "$(j '[.themes[] | .id]')" '["pricing","bugs","notifications","support","collaboration"]' "ranked by score, count, id"
eq "$(j '[.themes[] | select(.id == "bugs")][0].thin')" "true" "single-source theme is thin"
eq "$(j '[.unthemed_negative[].url]')" '["https://apps.apple.com/r/7"]' "unthemed low ratings surfaced"
eq "$(j '[.requests[].quote]')" '["I wish it had SMS reminders.","No way to set buffer times between meetings."]' "requests in their own words"
eq "$(jq -r '. as $d | [($d.themes[].quotes[]), ($d.requests[])] | map(.url) | unique | length' <<<"$OUT")" "5" "quotes link r1 r2 r3 r4 r6"
eq "$(j '[.themes[0].quotes[].quote]')" '["Way too expensive for what it is.","Overpriced per seat.","The price doubled this year."]' "quotes by rating, then length"
r replica-entrepreneur/reviews reviews.csv --today 2026-10-01
has "$OUT" "Small sample" "small sample warning"; has "$OUT" "1 rows dropped" "dropped line"; has "$OUT" "https://www.g2.com/r/3" "links"
hasnt "$OUT" "—" "no em dash"
has "$OUT" "| 1 | Price and paywalls | 3.2 | 4 | 57% | 1.7 | 4 |" "table row, exact"
r replica-entrepreneur/reviews reviews.csv --today 2026-10-01 --out fb.md; eq "$CODE" 0 "--out exits 0"
has "$(cat fb.md)" "# What users of the original hate and want" "--out writes the report"
r replica-entrepreneur/reviews reviews.csv --today 2026-10-01 --out ""; has "$OUT" "# What users of the original hate and want" "--out '' is unset, prints"
printf 'source,url,date,rating,text\napp-store,https://a/1,2023-01-01,1,Way too expensive.\nreddit,https://a/2,,,Way too expensive here too.\n' > old.csv
r replica-entrepreneur/reviews old.csv --json --today 2026-10-01; eq "$(j '.themes[0].score')" "1.1" "old 1-star counts half, undated unrated is 0.6"
r replica-entrepreneur/reviews old.csv --json --today 2026-10-01 --months 60; eq "$(j '.themes[0].score')" "1.6" "--months 60 keeps 2023 at full weight"
{ printf 'source,url,date,rating,text\nx,https://a/1,2026-01-01,1,'; printf 'x%.0s' $(seq 400); printf ' too expensive '; printf 'y%.0s' $(seq 400); printf '\n'; } > long.csv
r replica-entrepreneur/reviews long.csv --json --today 2026-10-01
X105=$(printf 'x%.0s' $(seq 105)); Y100=$(printf 'y%.0s' $(seq 100))
eq "$(jq -r '.themes[0].quotes[0].quote' <<<"$OUT")" "...$X105 too expensive $Y100..." "snippet: 220 around the match (start 405-110)"
printf 'source,text\napp-store,great\n' > nourl.csv
r replica-entrepreneur/reviews nourl.csv; eq "$CODE" 2 "url column required"
r replica-entrepreneur/reviews nope.csv; eq "$CODE" 2 "missing file exits 2"

# ================================================================ imgdiff
im() { r replica-diff/imgdiff "$@"; }
px() { im "$PNG/$1" "$PNG/$2" --mode pixel --tolerance 0 --json; eq "$(j '[.score, .changed_pixels]')" "[100.0,0]" "$3"; }
for f in 0 1 2 3 4; do px filter$f.png filter_ref.png "RGBA filter $f decodes"; done
px alpha0.png white1.png "transparent composites over white"
px grey.png grey_ref.png "8-bit grey"
px pal2.png pal2_ref.png "2-bit palette"
px rgb16.png rgb16_ref.png "16-bit RGB keeps the high byte"
px sq_grey16.png sq.png "16-bit grey, every filter"
px sq_bit1.png sq.png "1-bit grey"
px sq_rgba.png sq.png "RGBA transparent background"
px sq_pal4.png sq.png "4-bit palette, index past the palette is black"
px sq_greyalpha.png sq_127.png "grey+alpha 0 at 128 is 127 over white"
px sq_paltrns.png sq_blank.png "tRNS alpha 0 composites to white"
im "$PNG/interlaced.png" "$PNG/sq.png"; eq "$CODE" 2 "interlaced exits 2"; has "$ERR" "is interlaced. Re-save it without interlacing" "interlace advice"
im "$PNG/gif.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: $PNG/gif.png is not a PNG" "not a PNG"
im "$PNG/bad_truncated.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: image data is truncated" "truncated"
im "$PNG/bad_filter.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: unknown filter type 7" "bad filter"
im "$PNG/bad_ctype.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: unsupported PNG colour type 5" "bad colour type"
im "$PNG/bad_depth.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: unsupported bit depth 3" "bad depth"
im "$PNG/bad_noplte.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: palette PNG without a palette" "no PLTE"
im "$PNG/bad_noihdr.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: $PNG/bad_noihdr.png has no IHDR chunk" "no IHDR"
im "$PNG/bad_zlib.png" "$PNG/sq.png"; eq "$ERR" "imgdiff: Error -3 while decompressing data: incorrect header check" "zlib error"; eq "$CODE" 2 "zlib error exits 2"
im "$PNG/bad_ihdr12.png" "$PNG/sq.png"; eq "$CODE" 1 "short IHDR is an uncaught struct.error"; has "$ERR" "struct.error: unpack requires a buffer of 13 bytes" "struct.error"
im /nope/a.png /nope/b.png; eq "$CODE" 2 "bad file exits 2"; eq "$ERR" "imgdiff: [Errno 2] No such file or directory: '/nope/a.png'" "bad file message"
# upstream Compare
for m in layout pixel; do
  im "$PNG/page.png" "$PNG/page.png" --mode $m --json; eq "$(j '[.score, .regions, .verdict]')" '[100.0,[],"matches"]' "identical is 100 ($m)"
done
im "$PNG/page.png" "$PNG/page_rebrand.png" --json; L=$(j .score); eq "$L" "100.0" "rebrand keeps the layout"
im "$PNG/page.png" "$PNG/page_rebrand.png" --mode pixel --json; eq "$(j '[.score < '"$L"', (.regions | length > 0)]')" "[true,true]" "rebrand loses pixels"
im "$PNG/page.png" "$PNG/page_moved.png" --json
eq "$(j '[.score < 100, (.regions | length > 0), all(.regions[]; .y >= 200 and .x < 160), any(.regions[]; .where | test("bottom|middle"))]')" "[true,true,true,true]" "moved button found where it is"
im "$PNG/page.png" "$PNG/page_moved.png" --mode pixel --json; eq "$(j '[.score, .changed_pixels]')" "[90.0,8640]" "moved button: 2*120*36 of 86400 pixels"
im "$PNG/page.png" "$PNG/blank.png" --json; eq "$(j '[.score < 50, .verdict]')" '[true,"different"]' "blank clone is different"
im "$PNG/page_small.png" "$PNG/page_retina.png" --json; eq "$(j '[.score >= 90, .compared_at.width]')" "[true,120]" "retina compares at the smaller width"
im "$PNG/page.png" "$PNG/page_taller.png" --json; eq "$(j .height_delta_pct)" "25.0" "height difference"
im "$PNG/page.png" "$PNG/page_taller.png"; has "$OUT" "height: the clone is 25% taller than the original" "taller in the text"
# hand-computed: a 16x16 square on 64x48, cell 5
cp "$PNG/sq.png" "$PNG/sq_moved.png" .
im sq.png sq_moved.png --mode pixel
eq "$OUT" "  sq.png vs sq_moved.png
  PIXEL  ######################..   91.7  MATCHES
  where they differ (original's pixels, biggest first):
    top left       x=5 y=5 w=30 h=20" "moved square, pixel: 256 of 3072"
im "$PNG/sq.png" "$PNG/sq_moved.png" --json
eq "$(j '[.score, [.regions[] | [.x, .y, .w, .h, .cells, .where]]]')" '[15.0,[[5,5,10,20,6,"top left"],[25,5,10,20,6,"top centre"],[15,10,10,10,4,"top left"]]]' "moved square, layout: sims 5/6 5/6 5/8 5/7 of 20"
im "$PNG/sq.png" "$PNG/sq_blank.png"; has "$OUT" "LAYOUT ........................    0.0  DIFFERENT" "blank square bar"; has "$OUT" "top left       x=5 y=5 w=20 h=20" "ring region"
im "$PNG/sq.png" "$PNG/sq_blue.png" --json; eq "$(j '[.score, .regions]')" "[100.0,[]]" "colour change keeps the layout"
im "$PNG/sq.png" "$PNG/sq_blue.png" --mode pixel --json; eq "$(j .changed_pixels)" "3072" "but every pixel moved by more than 24"
im "$PNG/sq.png" "$PNG/sq_blue.png" --mode pixel --tolerance 255 --json; eq "$(j .score)" "100.0" "--tolerance 255 ignores everything"
im "$PNG/sq_2x.png" "$PNG/sq.png" --mode pixel --json; eq "$(j '[.score, .compared_at, .original]')" '[100.0,{"width":64,"height":48},{"width":128,"height":96}]' "2x original resized down"
im "$PNG/sq_2x.png" "$PNG/sq_moved.png" --mode pixel --json; eq "$(j '.regions[0] | [.x, .y, .w, .h]')" "[10,10,60,40]" "regions are in the original's pixels"
# --out diff images, pixel-exact against the expected ones
im "$PNG/sq.png" "$PNG/sq_moved.png" --mode pixel --out d1.png; has "$OUT" "  diff image: d1.png" "diff image line"
px_out() { im "$1" "$PNG/$2" --mode pixel --tolerance 0 --json; eq "$(j '[.score, .changed_pixels]')" "[100.0,0]" "$3"; }
px_out d1.png expect_pixel_diff.png "pixel diff image: dimmed grey, red where changed"
im "$PNG/sq.png" "$PNG/sq_blank.png" --out d2.png --json; eq "$(j .diff_image)" '"d2.png"' "diff_image in JSON"
px_out d2.png expect_layout_diff.png "layout diff image: flagged cells tinted"
im "$PNG/page.png" "$PNG/page_moved.png" --json --out d3.png
eq "$(j '[(.files.clone | endswith("page_moved.png")), .compared_at]')" '[true,{"width":240,"height":360}]' "files and compared_at"
im d3.png d3.png --json; eq "$(j .original)" '{"width":240,"height":360}' "--out size is compared_at"
im "$PNG/page.png" "$PNG/page_moved.png" --fail-under 99.9; eq "$CODE" 1 "--fail-under 99.9 fails"
im "$PNG/page.png" "$PNG/page.png" --fail-under 99.9; eq "$CODE" 0 "--fail-under 99.9 identical passes"
im "$PNG/sq.png" "$PNG/sq.png" --cols 0; eq "$CODE" 1 "--cols 0 crashes like Python"; has "$ERR" "ZeroDivisionError" "cols 0 ZeroDivisionError"

done_ replica

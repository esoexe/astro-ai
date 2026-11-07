#!/usr/bin/env bash
# seed-scout/scout: the assertions of seed-skill/tests/test_scout.py, run against the C++ tool through the CLI, plus
# hand-worked edge cases. The App Store fixtures are the upstream synthetic ones (Muttmile, Pawpost); the weak-point
# cases are saved-reviews files built here, one keyword per review, so every count and gap is worked by hand.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
DIR="$FIX/seed/seed-scout"; MUTT=1900000101; PAW=1900000202
S() { run seed-scout/scout "$@"; }
C() { run seed-scout/scout "$@" --dir "$TMP/w/seed" --today 2026-10-26; }   # the upstream cli(): --dir and --today
J() { jq -c "$@"; }
nice() { LC_ALL=C date -u -d "$1" '+%b %-d %Y'; }
# mk FILE ID theme=N ...: a saved reviews file; each review carries one keyword (meh = no theme, high = 5 stars)
mk() {
  local f=$1 id=$2; shift 2; local rows=() n=0 s k c kw st i
  for s in "$@"; do
    k=${s%=*}; c=${s#*=}; st=1
    case $k in fees) kw="fee";; cancellations) kw="cancel";; support) kw="support";; bugs) kw="crash";; trust) kw="scam";;
               payments) kw="refund";; availability) kw="in my area";; meh) kw="meh";; high) kw="great"; st=5;; esac
    for ((i = 0; i < c; i++)); do n=$((n + 1)); rows+=("{\"id\":\"$id-$n\",\"date\":\"2026-10-01\",\"updated\":\"2026-10-01T10:00:00-07:00\",\"stars\":$st,\"title\":\"$kw\",\"text\":\"\"}"); done
  done
  local IFS=,; printf '{"app_id":%s,"name":"A%s","app_name":"App %s","rating":null,"ratings":null,"reviews":[%s]}\n' "$id" "$id" "$id" "${rows[*]}" > "$f"
}
mkdir -p "$TMP/w" "$TMP/g"; cd "$TMP/w" || exit 1

# ---- find
S find "dog walking" --offline "$DIR"; eq "$CODE" 0 "find"
eq "$(sed -n 1p <<<"$OUT")" "App Store search 'dog walking' (US), in the store's own order:" "find header"
eq "$(sed -n 2p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  %s' "app id" app rating ratings seller)" "find columns"
has "$OUT" "  1900000101  Muttmile – Dog Walkers " "find row 1"; has "$OUT" "204,118" "thousands"
eq "$(sed -n 4p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  %s' 1900000303 "Muttmile for Walkers" 4.4 2,210 "Muttmile Labs (synthetic)")" "find row 2"
eq "$(sed -n 5p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  %s' 1900000202 "Pawpost - Pet Sitters & Walkers" 4.8 31,007 "Pawpost Inc (synthetic)")" "4.7501 half up -> 4.8"
eq "$(tail -1 <<<"$OUT")" "Next: scout.py reviews APP_ID --write for the ones that are real rivals (most ratings first)." "find next"
S find "dog walking" --offline "$DIR" --json; eq "$CODE" 0 "find json"
eq "$(J '[.apps[].app_id]' <<<"$OUT")" "[$MUTT,1900000303,$PAW]" "the store's order"
eq "$(J '.apps[0]|[.name,.rating]' <<<"$OUT")" '["Muttmile",4.9]' "short name, rating 4.86213 -> 4.9"
eq "$(J '.apps[0]|keys_unsorted' <<<"$OUT")" '["app_id","app_name","name","seller","rating","ratings","genre","price","url"]' "app row fields"
eq "$(J '.apps[0]|[.url,.price,.genre,.ratings]' <<<"$OUT")" '["https://apps.apple.com/us/app/synthetic/id1900000101","Free","Lifestyle",204118]' "url without the query, formattedPrice"
eq "$(J '[.apps[].name]' <<<"$OUT")" '["Muttmile","Muttmile for Walkers","Pawpost"]' "short names"
eq "$(J '.term' <<<"$OUT")" '"dog walking"' "json term"
S find x --offline "$DIR" --json --limit 2; eq "$(J '[.apps[].app_id]' <<<"$OUT")" "[$MUTT,1900000303]" "--limit 2"
S find x --offline "$DIR" --json --limit -1; eq "$(J '[.apps[].app_id]' <<<"$OUT")" "[$MUTT,1900000303]" "--limit -1 is out[:-1]"
S find x --offline "$DIR" --json --limit 0; eq "$CODE" 1 "--limit 0: nothing, exit 1"; eq "$(J '.apps' <<<"$OUT")" '[]' "empty apps"
S find x --offline "$DIR" --limit 0; eq "$CODE" 1 "none text"; eq "$OUT" "No App Store apps for 'x'. Try the words a customer would type." "none message"
S find x --offline "$DIR/search.json" --json; eq "$(J '.apps|length' <<<"$OUT")" 3 "--offline can be the search file itself"
mkdir -p "$TMP/empty"; S find nothing --offline "$TMP/empty"; eq "$CODE" 1 "no search.json"; eq "$ERR" "scout.py: offline: no $TMP/empty/search.json" "offline message"
S find x --offline "$DIR" --country gb; has "$OUT" "(GB)" "country upper"
cat > "$TMP/g/search.json" <<'EOF'
{"results": [
 {"trackId": "1900000501", "trackName": "Half Up", "averageUserRating": 4.75, "userRatingCount": 1},
 {"trackId": 1900000502, "trackName": "Quarter: Up", "averageUserRating": 4.25, "artistName": "Artist", "price": 1.99, "kind": "software"},
 {"trackId": 1900000503, "trackName": "No Rating | x", "kind": "song", "wrapperType": "software"},
 {"trackId": 1900000504, "trackName": "A song", "kind": "song", "wrapperType": "track"},
 {"trackName": "No id"},
 {"trackId": 0, "trackName": "Zero id"},
 {"trackId": 1900000505, "trackName": "  Spaces  ", "averageUserRating": 3, "userRatingCount": true}
]}
EOF
S find x --offline "$TMP/g" --json; eq "$CODE" 0 "custom search"
eq "$(J '[.apps[].app_id]' <<<"$OUT")" '[1900000501,1900000502,1900000503,1900000505]' "songs, no id and id 0 are left out; a string id is an int"
eq "$(J '[.apps[].rating]' <<<"$OUT")" '[4.8,4.3,null,3.0]' "rating1: 4.75 -> 4.8, 4.25 -> 4.3, None, 3 -> 3.0"
eq "$(J '[.apps[].name]' <<<"$OUT")" '["Half Up","Quarter","No Rating","Spaces"]' "short_name separators and strip"
eq "$(J '[.apps[1].seller,.apps[1].price,.apps[0].seller,.apps[0].url]' <<<"$OUT")" '["Artist",1.99,null,""]' "artistName, price, missing seller and url"
S find x --offline "$TMP/g"
eq "$(sed -n 4p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  %s' 1900000502 "Quarter: Up" 4.3 "?" Artist)" "no ratings -> ?"
eq "$(sed -n 6p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  %s' 1900000505 "  Spaces  " 3.0 1 "")" "a bool count formats as 1"
eq "$(sed -n 5p <<<"$OUT")" "$(printf '  %-11s %-34s %6s %10s  ' 1900000503 "No Rating | x" "?" "?")" "no rating -> ?"
S find "dog walking"; eq "$CODE" 2 "live and the network is off"; has "$ERR" "scout.py: could not reach the App Store" "network error"

# ---- reviews (offline) and the feed
read -r LO HI <<<"$(jq -s -r '[.[].feed.entry[]|.updated.label[:10]]|"\(min) \(max)"' "$DIR"/rss-$MUTT-*.json)"
STARS=$(jq -s -r '[.[].feed.entry[]]|unique_by(.id.label)|[5,4,3,2,1] as $s|[$s[] as $k|"\($k): \(map(select(.["im:rating"].label=="\($k)"))|length)"]|join("  ")' "$DIR"/rss-$MUTT-*.json)
LOWS=$(jq -s '[.[].feed.entry[]]|unique_by(.id.label)|map(select((.["im:rating"].label|tonumber)<=2))|length' "$DIR"/rss-$MUTT-*.json)
C reviews $MUTT --offline "$DIR"; eq "$CODE" 0 "reviews"
eq "$(sed -n 1p <<<"$OUT")" "Muttmile – Dog Walkers ($MUTT): read 30 reviews, the most recent first (2 pages), $(nice "$LO") to $(nice "$HI")" "reviews header"
eq "$(sed -n 2p <<<"$OUT")" "  App Store: 4.9 stars from 204,118 ratings" "app store line"
eq "$(sed -n 3p <<<"$OUT")" "  Stars: $STARS" "stars line"
eq "$(sed -n 4p <<<"$OUT")" "  1-2 star reviews: $LOWS of 30" "low line"; eq "$LOWS" 24 "24 low (upstream)"
eq "$(sed -n 5p <<<"$OUT")" "Next: scout.py themes $MUTT, then scout.py compare with the other rivals." "reviews next"
[ -e "$TMP/w/seed" ] && bad "reviews writes nothing without --write" || ok
C reviews $MUTT --offline "$DIR" --write; eq "$CODE" 0 "reviews --write"
has "$OUT" "Wrote $TMP/w/seed/reviews/$MUTT.json." "wrote"; RV="$TMP/w/seed/reviews/$MUTT.json"
eq "$(J '[.app_id,.name,.rating,.ratings]' "$RV")" "[$MUTT,\"Muttmile\",4.9,204118]" "saved app"
eq "$(J '[(.reviews|length),.pages,.fetched,.country,.created_by,.seller]' "$RV")" '[30,2,"2026-10-26","us","seed-scout","Muttmile Labs (synthetic)"]' "saved counts"
eq "$(J 'keys_unsorted' "$RV")" '["created_by","app_id","name","app_name","seller","rating","ratings","country","fetched","pages","source","reviews"]' "doc fields"
eq "$(J '.source' "$RV")" "\"https://itunes.apple.com/us/rss/customerreviews/page=1/id=$MUTT/sortby=mostrecent/json (pages 1-2)\"" "source"
eq "$(J '.reviews[0]|keys_unsorted' "$RV")" '["id","date","updated","stars","title","text","version","votes"]' "no reviewer names are kept"
eq "$(J '[.reviews[].id]|unique|length' "$RV")" 30 "page 2 repeats one review: kept once"
eq "$(J '.reviews[0]' "$RV")" "$(jq -c '.feed.entry[0]|{id:.id.label,date:.updated.label[:10],updated:.updated.label,stars:(.["im:rating"].label|tonumber),title:.title.label,text:.content.label,version:.["im:version"].label,votes:(.["im:voteSum"].label|tonumber)}' "$DIR/rss-$MUTT-1.json")" "a row from the feed"
eq "$(tail -c 2 "$RV" | od -An -c | tr -d ' ')" '}\n' "trailing newline"
C reviews $MUTT --offline "$DIR" --pages 1 --json; eq "$(J '[(.reviews|length),.pages]' <<<"$OUT")" '[20,1]' "--pages 1"
eq "$(J '.source' <<<"$OUT")" "\"https://itunes.apple.com/us/rss/customerreviews/page=1/id=$MUTT/sortby=mostrecent/json\"" "one page: no (pages ...)"
C reviews $MUTT --offline "$DIR" --pages 1; has "$OUT" "(1 page)," "page singular"
for p in 0 11; do C reviews $MUTT --offline "$DIR" --pages $p; eq "$CODE" 2 "--pages $p"; eq "$ERR" "scout.py: --pages is 1 to 10 (the feed stops at 10 pages of 50: the 500 most recent)" "--pages $p message"; done
C reviews 1900000999 --offline "$DIR"; eq "$CODE" 1 "no such app"; eq "$ERR" "scout.py: no reviews for app 1900000999 (a wrong id, or an app with no reviews in this country)" "no reviews message"
C reviews pawpost --offline "$DIR" --json; eq "$(J '.app_id' <<<"$OUT")" $PAW "a name resolves (case-insensitive)"
C reviews Muttmile --offline "$DIR" --json; eq "$(J '.app_id' <<<"$OUT")" $MUTT "two name matches: the most ratings"
C reviews id$PAW --offline "$DIR" --json; eq "$(J '.app_id' <<<"$OUT")" $PAW "id prefix"
C reviews "  $PAW  " --offline "$DIR" --json; eq "$(J '.app_id' <<<"$OUT")" $PAW "spaces around an id"
C reviews nomatch --offline "$DIR"; eq "$CODE" 1 "no name match"
eq "$ERR" "scout.py: no app named 'nomatch' in the search; use its id: Muttmile – Dog Walkers ($MUTT), Muttmile for Walkers (1900000303), Pawpost - Pet Sitters & Walkers ($PAW)" "it lists the ids it found"
C reviews 12345 --offline "$DIR"; eq "$CODE" 1 "5 digits is a name"; has "$ERR" "no app named '12345'" "5 digits message"
C reviews x --offline "$TMP/empty"; eq "$CODE" 1 "a name with no search.json"
S reviews $MUTT --offline "$DIR" --today 26/10/2026; eq "$CODE" 2 "bad --today"; eq "$ERR" "scout.py: --today wants YYYY-MM-DD, not '26/10/2026'" "today message"
S reviews 1900000999 --offline "$DIR" --today 2026-02-30; eq "$CODE" 2 "--today is read before the app"
S reviews $MUTT --offline "$DIR" --today 2026-1-5 --json; eq "$(J '.fetched' <<<"$OUT")" '"2026-01-05"' "one-digit month and day"
S reviews $PAW --offline "$DIR" --country GB --json; eq "$(J '[.country,.source]' <<<"$OUT")" "[\"gb\",\"https://itunes.apple.com/gb/rss/customerreviews/page=1/id=$PAW/sortby=mostrecent/json\"]" "country"
S reviews $PAW; eq "$CODE" 2 "live reviews, network off"; has "$ERR" "scout.py: could not reach the App Store: " "could not reach"
# gzip, not-JSON pages, rev- names
mkdir -p "$TMP/gz"; cp "$DIR"/lookup-$PAW.json "$TMP/gz/"; gzip -c "$DIR/rss-$PAW-1.json" > "$TMP/gz/rss-$PAW-1.json"
S reviews $PAW --offline "$TMP/gz" --json; eq "$CODE" 0 "a gzip body"; eq "$(J '.reviews|length' <<<"$OUT")" 23 "23 reviews from gzip"
mkdir -p "$TMP/rev"; cp "$DIR/rss-$PAW-1.json" "$TMP/rev/rev-$PAW-1.json"
S reviews $PAW --offline "$TMP/rev" --json; eq "$(J '[(.reviews|length),.name,.app_name,.rating]' <<<"$OUT")" "[23,\"$PAW\",\"$PAW\",null]" "rev- name; no lookup: the id stands in"
S reviews $PAW --offline "$TMP/rev"; hasnt "$OUT" "App Store:" "no rating line without a lookup"
mkdir -p "$TMP/nj"; echo 'nope' > "$TMP/nj/rss-$PAW-1.json"
S reviews $PAW --offline "$TMP/nj"; eq "$CODE" 2 "not JSON"; eq "$ERR" "scout.py: the App Store sent something that is not JSON for reviews page 1" "not JSON message"
echo 'nope' > "$TMP/nj/lookup-$PAW.json"; S reviews $PAW --offline "$TMP/nj"; eq "$ERR" "scout.py: the App Store sent something that is not JSON for lookup $PAW" "lookup not JSON"
# feed shapes (test_parse_feed_shapes)
mkdir -p "$TMP/fs"; ONE='{"im:rating": {"label": "2"}, "id": {"label": "9"}, "title": {"label": "t"}, "content": {"label": "c", "attributes": {"type": "text"}}, "updated": {"label": "2026-10-02T08:00:00-07:00"}, "im:version": {"label": "1"}, "im:voteSum": {"label": "-1"}}'
echo "{\"feed\": {\"entry\": $ONE}}" > "$TMP/fs/rss-1900000601-1.json"
S reviews 1900000601 --offline "$TMP/fs" --json; eq "$(J '.reviews|length' <<<"$OUT")" 1 "one entry as a dict"
eq "$(J '.reviews[0]|[.stars,.date,.votes,.id,.title,.text,.version]' <<<"$OUT")" '[2,"2026-10-02",-1,"9","t","c","1"]' "stars, date, votes -1"
echo "{\"feed\": {\"entry\": [{\"im:name\": {\"label\": \"The app\"}, \"id\": {\"label\": \"1\"}}, $ONE]}}" > "$TMP/fs/rss-1900000602-1.json"
S reviews 1900000602 --offline "$TMP/fs" --json; eq "$(J '.reviews|length' <<<"$OUT")" 1 "older feeds put the app first"
echo '{"feed": {}}' > "$TMP/fs/rss-1900000603-1.json"; S reviews 1900000603 --offline "$TMP/fs"; eq "$CODE" 1 "an empty feed"
echo 'null' > "$TMP/fs/rss-1900000604-1.json"; S reviews 1900000604 --offline "$TMP/fs"; eq "$CODE" 1 "a null feed"
cat > "$TMP/fs/rss-1900000605-1.json" <<'EOF'
{"feed": {"entry": [
 {"im:rating": {"label": "x"}, "id": {"label": "a"}},
 {"im:rating": {"label": " 3 "}, "id": {"label": "b"}, "im:voteSum": {"label": "+5"}},
 {"im:rating": {"label": "٤"}, "id": {"label": "c"}, "im:voteSum": {"label": "--"}},
 {"im:rating": {"label": ""}, "id": {"label": "d"}},
 {"im:rating": {"label": 5}, "id": {"label": "e"}, "im:voteSum": {"label": "7"}, "updated": {"label": "2026-09-01"}},
 {"im:rating": {}, "id": {"label": "f"}},
 {"im:rating": {"label": "1"}, "id": {"label": "b"}},
 {"im:rating": {"label": "1"}},
 {"im:rating": {"label": "1"}},
 "junk"
]}}
EOF
S reviews 1900000605 --offline "$TMP/fs" --json; eq "$CODE" 0 "odd labels"
eq "$(J '[.reviews[]|[.id,.stars,.votes,.date]]' <<<"$OUT")" '[["b",3,0,""],["c",4,0,""],["e",5,7,"2026-09-01"],["",1,0,""],["",1,0,""]]' "int(' 3 '), Arabic-Indic 4, a numeric label; bad ratings skipped; an empty id is never a duplicate"
S reviews 1900000605 --offline "$TMP/fs"; eq "$(sed -n 2p <<<"$OUT")" "  Stars: 5: 1  4: 1  3: 1  2: 0  1: 2" "stars of the odd feed"
has "$(sed -n 1p <<<"$OUT")" "read 5 reviews, the most recent first (1 page),  to Sep 1 2026" "empty dates sort first and print as is"
echo '{"feed": {"entry": [{"im:rating": {"label": "2"}, "im:voteSum": {"label": "--2"}}]}}' > "$TMP/fs/rss-1900000606-1.json"
S reviews 1900000606 --offline "$TMP/fs"; eq "$CODE" 1 "'--2' passes isdigit after lstrip, then int() raises (a crash, as upstream)"
# pages stop at the first empty page
mkdir -p "$TMP/pg"; cp "$DIR/rss-$MUTT-1.json" "$TMP/pg/rss-$MUTT-1.json"; echo '{"feed": {}}' > "$TMP/pg/rss-$MUTT-2.json"; cp "$DIR/rss-$MUTT-2.json" "$TMP/pg/rss-$MUTT-3.json"
S reviews $MUTT --offline "$TMP/pg" --json; eq "$(J '[(.reviews|length),.pages]' <<<"$OUT")" '[20,1]' "an empty page ends the feed"

# ---- themes
C reviews $PAW --offline "$DIR" --write >/dev/null
C themes $PAW --offline "$DIR"; eq "$CODE" 0 "themes"
row() { printf '  %-28s %7d of %-4d %6s%%' "$1" "$2" "$3" "$4"; }
EXPT="Pawpost - Pet Sitters & Walkers ($PAW): 20 of the 23 most recent reviews are 1 or 2 stars (Sep 5 2026 to Oct 20 2026)
  App Store: 4.8 stars from 31,007 ratings
$(printf '  %-28s %14s %7s' theme "low reviews" share)
$(row "customer support" 9 20 45.0)
$(row "cancellations and no-shows" 8 20 40.0)
$(row "fees and pricing" 2 20 10.0)
$(row "payments and refunds" 2 20 10.0)
$(row "bugs and app quality" 1 20 5.0)
$(row "trust and safety" 1 20 5.0)
$(row "availability" 0 20 0.0)
$(row "(no theme)" 0 20 0.0)
Most mentioned: customer support, 9 of 20 low reviews.
Examples (customer support, newest first):"
eq "$(head -13 <<<"$OUT")" "$EXPT" "themes table (ties keep the rule order)"
has "$OUT" "customer support                   9 of 20     45.0%" "upstream row"
eq "$(tail -1 <<<"$OUT")" "A review can mention several themes. Read the examples before you quote a theme." "themes footer"
eq "$(sed -n 14,16p <<<"$OUT" | grep -c '^  [A-Z][a-z][a-z] [0-9 ][0-9]* 2026 *1 star  "')" 3 "three examples"
eq "$(sed -n 14p <<<"$OUT")" "$(printf '  %-12s %d star%s  "%s"' "Sep 30 2026" 1 "" "Cancelled and charged")" "example line"
C themes $PAW --offline "$DIR" --examples 1; eq "$(grep -c 'star[s]*  "' <<<"$OUT")" 1 "--examples 1"
C themes $PAW --offline "$DIR" --examples -1; eq "$(grep -c 'star[s]*  "' <<<"$OUT")" 8 "--examples -1 is rows[:-1] of 9"
C themes $PAW --offline "$DIR" --examples 0; eq "$(grep -c 'star[s]*  "' <<<"$OUT")" 0 "--examples 0"
C themes $MUTT --offline "$DIR" --stars 3 --json; eq "$(J '[.low_reviews,.themes.fees,.themes.bugs]' <<<"$OUT")" '[25,10,4]' "--stars 3"
eq "$(J 'keys_unsorted' <<<"$OUT")" '["app_id","name","reviews_read","low_reviews","themes","unthemed","most_mentioned","examples"]' "themes json fields"
C themes $MUTT --offline "$DIR" --json; eq "$(J '[.most_mentioned,.reviews_read,.low_reviews,.name]' <<<"$OUT")" '["fees",30,24,"Muttmile"]' "themes json"
eq "$(J '.themes' <<<"$OUT")" '{"fees":9,"cancellations":3,"support":5,"bugs":3,"trust":1,"payments":3,"availability":1}' "Muttmile themes (upstream)"
C themes $MUTT --offline "$DIR" --stars 1 --json; eq "$(J '.low_reviews' <<<"$OUT")" 17 "--stars 1"
C themes $MUTT --offline "$DIR" --stars 1; has "$OUT" "are 1 star (" "1 star wording"
for st in 0 4 5; do C themes $MUTT --offline "$DIR" --stars $st; eq "$CODE" 2 "--stars $st"; eq "$ERR" "scout.py: --stars is 1, 2 or 3 (the low reviews are the ones at or under it)" "--stars $st message"; done
C themes 1900000999 --offline "$DIR" --stars 9; eq "$CODE" 1 "the app is read before --stars is checked"
C themes "$TMP/w/seed/reviews/$PAW.json"; eq "$CODE" 0 "a saved reviews file"; has "$OUT" "9 of 20" "file themes"
C themes $PAW; eq "$CODE" 0 "a saved review in seed/reviews is used: no network"
# keyword rules (test_keyword_rules_and_multi_theme_reviews, test_words_that_must_not_count)
cat > "$TMP/g/rows.json" <<'EOF'
{"app_id": 1, "name": "R", "app_name": "Rows", "reviews": [
 {"id": "a", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 1, "title": "Fees on fees", "text": "and customer service never replied"},
 {"id": "b", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 2, "title": "Nobody came", "text": "The walker didn’t show up"},
 {"id": "c", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 1, "title": "They keep 25%", "text": ""},
 {"id": "d", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 1, "title": "Since the update", "text": "I can’t log in"},
 {"id": "e", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 1, "title": "Refund?", "text": "charged twice"},
 {"id": "f", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 1, "title": "Bad", "text": "would not recommend"},
 {"id": "g", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 3, "title": "Price is fine", "text": "a fee but ok"},
 {"id": "h", "date": "2026-10-01", "updated": "2026-10-01T10:00:00-07:00", "stars": 5, "title": "Great price"}]}
EOF
S themes "$TMP/g/rows.json" --json; eq "$(J '[.low_reviews,.reviews_read,.unthemed]' <<<"$OUT")" '[6,8,1]' "low, read, unthemed"
eq "$(J '.themes' <<<"$OUT")" '{"fees":2,"cancellations":1,"support":1,"bugs":1,"trust":0,"payments":1,"availability":0}' "multi-theme counts (curly apostrophes)"
S themes "$TMP/g/rows.json" --json --stars 3; eq "$(J '[.low_reviews,.themes.fees]' <<<"$OUT")" '[7,3]' "--stars 3 rows"
S themes "$TMP/g/rows.json" --json --stars 1; eq "$(J '.low_reviews' <<<"$OUT")" 5 "--stars 1 rows"
words() { printf '{"app_id": 1, "app_name": "W", "reviews": [{"id": "1", "date": "2026-10-01", "stars": 1, "title": "%s"}]}' "$1" > "$TMP/g/w.json"; S themes "$TMP/g/w.json" --json; J ".themes.$2" <<<"$OUT"; }
eq "$(words "I feel let down" fees)" 0 "fee inside feel"
eq "$(words "The coffee was cold" fees)" 0 "coffee"
eq "$(words "Feedback ignored" fees)" 0 "feedback"
eq "$(words "Who is in charge here" payments)" 0 "charge is not charged"
eq "$(words "Show me the map" cancellations)" 0 "show"
eq "$(words "No-show again" cancellations)" 1 "no-show"
eq "$(words "Stood me up" cancellations)" 1 "stood me up"
eq "$(words "the 20% cut" fees)" 1 "20%"
eq "$(words "FEES EVERYWHERE" fees)" 1 "IGNORECASE"
eq "$(words "Can’t reach anyone" support)" 1 "a curly apostrophe is straightened"
eq "$(words "nobody available in my area" availability)" 1 "availability"
# no low reviews; dates
mk "$TMP/g/high.json" 7 high=3; S themes "$TMP/g/high.json"; eq "$CODE" 1 "no low reviews"; eq "$OUT" "App 7 has no reviews at 2 stars or under among the 3 read." "no low message"
S themes "$TMP/g/high.json" --json; eq "$CODE" 0 "no low json exits 0"; eq "$(J '[.most_mentioned,.examples]' <<<"$OUT")" '[null,[]]' "nothing mentioned"
dated() { printf '{"app_id": 1, "app_name": "D", "reviews": [{"id": "1", "date": "%s", "stars": 1, "title": "fee"}]}' "$1" > "$TMP/g/d.json"; S themes "$TMP/g/d.json"; sed -n 1p <<<"$OUT" | sed 's/.*(\(.*\) to .*/\1/'; }
eq "$(dated 2026-10-04)" "Oct 4 2026" "nice_date"
eq "$(dated 2026-0-5)" "Dec 5 2026" "month 0 is MONTHS[-1]"
eq "$(dated 2026-13-01)" "2026-13-01" "month 13: IndexError, as is"
eq "$(dated 2026-10)" "2026-10" "two parts: as is"
eq "$(dated 'abc')" "abc" "not a date"
eq "$(dated '2026-٠٣-04')" "Mar 4 2026" "Unicode digits"
eq "$(dated ' 2026-3-04xyz')" "Mar 4 2026" "the first 10 characters, spaces allowed"
eq "$(dated ' 2026- 3-04')" "Mar 0 2026" "the 10th character cuts the day to 0"
eq "$(dated 2026-03-04-1)" "Mar 4 2026" "only the first 10 characters"
eq "$(dated 26-3-4-1)" "26-3-4-1" "four parts: as is"
eq "$(dated '2026-+3-04')" "Mar 4 2026" "a sign is fine for int()"
dated "" >/dev/null; has "$(sed -n 1p <<<"$OUT")" "( to )" "no dates"
# custom rules (test_custom_rules_add_and_replace)
cat > "$TMP/g/rules.json" <<'EOF'
{"walker_pay": {"label": "walker pay", "patterns": ["\\bwalkers? (?:get|got|make) "]}, "fees": {"label": "fees only", "patterns": ["\\bfees?\\b"]}}
EOF
printf '{"app_id": 1, "app_name": "C", "reviews": [{"id": "1", "date": "2026-10-01", "stars": 1, "title": "Walkers get almost nothing"}, {"id": "2", "date": "2026-10-01", "stars": 1, "title": "Too expensive"}]}' > "$TMP/g/c.json"
S themes "$TMP/g/c.json" --rules "$TMP/g/rules.json" --json; eq "$CODE" 0 "custom rules"
eq "$(J '.themes|keys_unsorted' <<<"$OUT")" '["fees","cancellations","support","bugs","trust","payments","availability","walker_pay"]' "replaced in place, added last"
eq "$(J '[.themes.walker_pay,.themes.fees]' <<<"$OUT")" '[1,0]' "walker pay 1, fees only 0"
S themes "$TMP/g/c.json" --rules "$TMP/g/rules.json"; has "$OUT" "$(printf '  %-28s' 'fees only')" "the new label"; has "$OUT" "Most mentioned: walker pay, 1 of 2 low reviews." "the added theme"
S compare "$TMP/g/c.json" --rules "$TMP/g/rules.json" --json; eq "$(J '[.theme_labels.fees,.theme_labels.walker_pay,.apps[0].weak_point,.apps[0].top_theme]' <<<"$OUT")" '["fees only","walker pay","walker pay","walker_pay"]' "labels in compare"
echo '{"x": {"patterns": ["(unclosed"]}}' > "$TMP/g/r1.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r1.json"; eq "$CODE" 2 "bad pattern"; has "$ERR" "scout.py: rules 'x' has a bad pattern: missing ), unterminated subpattern" "bad pattern message"
echo '{"x": {"label": "no patterns"}}' > "$TMP/g/r2.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r2.json"; eq "$CODE" 2 "no patterns"; eq "$ERR" "scout.py: rules 'x' needs a list of patterns" "no patterns message"
echo '{"x": {"patterns": []}}' > "$TMP/g/r3.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r3.json"; eq "$CODE" 2 "empty patterns"
echo '{"x": ["fee"]}' > "$TMP/g/r4.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r4.json"; eq "$ERR" "scout.py: rules 'x' needs a list of patterns" "a list is not a rule"
for body in '[]' '{}' '"x"'; do echo "$body" > "$TMP/g/r5.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r5.json"; eq "$CODE" 2 "rules $body"; eq "$ERR" "scout.py: $TMP/g/r5.json wants {\"key\": {\"label\": \"...\", \"patterns\": [\"regex\", ...]}}" "rules $body message"; done
S themes "$TMP/g/c.json" --rules "$TMP/g/none.json"; eq "$CODE" 1 "no rules file"; eq "$ERR" "scout.py: no rules file at $TMP/g/none.json" "no rules message"
echo 'nope' > "$TMP/g/r6.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r6.json"; eq "$CODE" 2 "rules not JSON"; eq "$ERR" "scout.py: $TMP/g/r6.json is not valid JSON: Expecting value: line 1 column 1 (char 0)" "rules not JSON message"
echo '{"x": {"patterns": "xp"}}' > "$TMP/g/r7.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r7.json" --json; eq "$(J '[.themes.x,.themes.fees]' <<<"$OUT")" '[1,1]' "a string of patterns is one pattern per character (x, p: only Too expensive has one; the default fees still counts it)"
echo '{"x": {"patterns": ["zzz"]}}' > "$TMP/g/r8.json"; S themes "$TMP/g/c.json" --rules "$TMP/g/r8.json"; has "$OUT" "$(printf '  %-28s %7d of %-4d %6s%%' x 0 2 0.0)" "no label: the key"
S themes "$TMP/g/c.json" --rules "$TMP/g/r1.json" --stars 9; eq "$CODE" 2 "rules are read first"; has "$ERR" "bad pattern" "rules before stars"

# ---- weak points (test_the_gap_rule_exactly, tiny themes, pooled shares, no low reviews)
G="$TMP/g"; wk() { S compare "$@" --json; printf '%s' "$OUT" > "$TMP/wk.json"; J '[.apps[]|[.top_theme,.weak_point_rule,.gap_points]]' <<<"$OUT"; }   # runs in $(...): the checks after it read wk.json
mk $G/a.json 11 fees=9 cancellations=3 support=5 bugs=3 trust=1 payments=3 availability=1
mk $G/b.json 12 fees=2 cancellations=8 support=9 bugs=1 trust=1 payments=2
# the fixture apps are multi-theme, so the counts come from the real reviews; these files have one theme per review
mk $G/a1.json 11 fees=9 cancellations=3 support=5 bugs=3 trust=1 meh=3; mk $G/b1.json 12 fees=2 cancellations=8 support=9 meh=1
eq "$(wk $G/a1.json $G/b1.json)" '[["fees","sets it apart",27.5],["cancellations","sets it apart",27.5]]' "9/24 - 2/20 = 27.5; 8/20 - 3/24 = 27.5"
eq "$(wk $G/a1.json $G/b1.json --by count)" '[["fees","most mentioned",null],["support","most mentioned",null]]' "--by count"
eq "$(J '.weak_point_by' "$TMP/wk.json")" '"count"' "weak_point_by count"
eq "$(wk $G/b1.json)" '[["support","most mentioned",null]]' "one app: the most mentioned"
eq "$(J '.weak_point_by' "$TMP/wk.json")" '"count"' "one app: by count"
mk $G/t1.json 21 fees=10 availability=1 meh=29; mk $G/t2.json 22 fees=12 meh=28; mk $G/t3.json 23 fees=10 availability=2 meh=28
eq "$(wk $G/t1.json $G/t2.json | J '.[0]')" '["fees","most mentioned",null]' "availability 1 of 40 is under 5%: skipped"
eq "$(wk $G/t3.json $G/t2.json | J '.[0]')" '["availability","sets it apart",5.0]' "2 of 40 is 5%: it counts"
mk $G/p1.json 31 fees=5 meh=5; mk $G/p2.json 32 fees=2 support=8 meh=10; mk $G/p3.json 33 fees=3 support=3 meh=24
eq "$(wk $G/p1.json $G/p2.json $G/p3.json)" '[["fees","sets it apart",40.0],["support","sets it apart",32.5],["fees","most mentioned",null]]' "pooled shares with three apps"
mk $G/z1.json 41 high=2; mk $G/z2.json 42 fees=1 meh=4
eq "$(wk $G/z1.json $G/z2.json)" '[[null,"most mentioned",null],["fees","most mentioned",null]]' "no low reviews: no weak point"
eq "$(J '.apps[0]|[.weak_point,.weak_point_count,.weak_point_share,.weak_point_line,.examples,.most_mentioned,.reviews_from]' "$TMP/wk.json")" '[null,0,0.0,"no theme matched",[],null,"2026-10-01"]' "the empty row"
eq "$(J '.apps[1]|[.weak_point_share,.weak_point_line]' "$TMP/wk.json")" '[20.0,"1 of 5 recent 1-2 star reviews mention fees and pricing"]' "1 of 5 = 20.0 (n < 2 cannot set it apart)"
mk $G/e1.json 51 fees=2 cancellations=4 meh=4; mk $G/e2.json 52 cancellations=2 meh=8
eq "$(wk $G/e1.json $G/e2.json | J '.[0]')" '["cancellations","sets it apart",20.0]' "equal gaps: the theme with more mentions"
mk $G/f1.json 61 availability=99 meh=1901; mk $G/f2.json 62 meh=10
eq "$(wk $G/f1.json $G/f2.json | J '.[0]')" '["availability","sets it apart",5.0]' "99 of 2000 = 4.95, which rounds to 5.0 (the double is just above): it counts"
eq "$(J '.apps[0].weak_point_share' "$TMP/wk.json")" '5.0' "share 5.0"
mk $G/s1.json 71 fees=3 meh=1; mk $G/s2.json 72 fees=3 meh=1
eq "$(wk $G/s1.json $G/s2.json)" '[["fees","most mentioned",null],["fees","most mentioned",null]]' "the same share everywhere: a gap of 0 is not a weak point"
S compare $G/z1.json $G/z2.json; eq "$(sed -n 3p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %d of %d (%s%%)' A41 "?" "?" 0/2 none 0 0 0.0)" "the empty row in text"
eq "$(sed -n 4p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %d of %d (%s%%)' A42 "?" "?" 5/5 "fees and pricing" 1 5 20.0)" "row two"
eq "$(sed -n 5p <<<"$OUT")" "Weak point = the complaint that sets each app apart: its share of low reviews minus the others'." "gap wording"
eq "$(sed -n 6p <<<"$OUT")" "$(printf '  %s  %-12s %d star%s  "%s"' A42 "Oct 1 2026" 1 "" fee)" "example line in compare"
S compare $G/b1.json; eq "$(sed -n 1p <<<"$OUT")" "Competitors: their most recent App Store reviews, 1-2 star only" "compare header"; eq "$(sed -n 4p <<<"$OUT")" "Weak point = the most mentioned complaint." "count wording"
S compare $G/b1.json --stars 1; has "$OUT" "1-star only" "1-star header"
S compare $G/b1.json --stars 3 --json; eq "$(J '[.low_stars,.apps[0].weak_point_line]' <<<"$OUT")" '[3,"9 of 20 recent 1-3 star reviews mention customer support"]' "1-3 star line"
S compare $G/a1.json $G/a1.json; eq "$CODE" 2 "twice"; eq "$ERR" "scout.py: the same app is in the list twice" "twice message"
S compare $G/a1.json $G/b1.json --by x; eq "$CODE" 2 "--by choices"
S compare $G/a1.json $G/b1.json --stars 4; eq "$CODE" 2 "compare --stars 4"
S compare $G/a1.json $G/b1.json --today nope; eq "$CODE" 2 "compare --today"; has "$ERR" "--today wants YYYY-MM-DD, not 'nope'" "compare today"
S compare $G/a1.json $G/b1.json --today 2026-02-28 --json; eq "$(J '.generated' <<<"$OUT")" '"2026-02-28"' "generated"
S compare $G/a1.json $G/none.json; eq "$CODE" 2 "a missing file is a name: the live search, and the network is off"
echo 'nope' > $G/bad.json; S compare $G/bad.json; eq "$CODE" 2 "a reviews file that is not JSON"; eq "$ERR" "scout.py: $G/bad.json is not valid JSON: Expecting value: line 1 column 1 (char 0)" "reviews file message"

# ---- the round trip (test_reviews_compare_pricing_show_round_trip)
rm -rf "$TMP/w/seed"; cd "$TMP/w" || exit 1
C reviews $MUTT --offline "$DIR" --write >/dev/null; C reviews $PAW --offline "$DIR" --write >/dev/null
C compare $MUTT $PAW --write; eq "$CODE" 0 "compare reads the saved reviews: no --offline, and the network is off"
has "$OUT" "fees and pricing" "fees"; has "$OUT" "9 of 24 (37.5%)" "9 of 24"; has "$OUT" "8 of 20 (40.0%)" "8 of 20"
has "$OUT" "Pawpost: the most mentioned is customer support (9 of 20), but the other apps get it about as often." "most mentioned line"
eq "$(sed -n 2p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %s' app rating ratings low/read "weak point" evidence)" "compare columns"
eq "$(sed -n 3p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %s' Muttmile 4.9 204,118 24/30 "fees and pricing" "9 of 24 (37.5%)")" "Muttmile row"
eq "$(sed -n 4p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %s' Pawpost 4.8 31,007 20/23 "cancellations and no-shows" "8 of 20 (40.0%)")" "Pawpost row"
eq "$(sed -n 6,8p <<<"$OUT")" "  Muttmile  Oct 20 2026  1 star  \"Fees on fees\"
  Muttmile  Oct 16 2026  1 star  \"Too expensive now\"
  Muttmile  Oct 14 2026  2 stars  \"They keep 25%\"" "examples, newest first"
eq "$(tail -2 <<<"$OUT")" "Wrote $TMP/w/seed/competitors.json.
Next: read each rival's pricing page and record it (scout.py pricing), then /seed-market." "compare footer"
CJ="$TMP/w/seed/competitors.json"
eq "$(J '[.generated,.weak_point_by,.created_by,.low_stars]' "$CJ")" '["2026-10-26","gap","seed-scout",2]' "competitors header"
eq "$(J 'keys_unsorted' "$CJ")" '["created_by","generated","low_stars","weak_point_by","theme_labels","apps"]' "competitors fields"
eq "$(J '.apps[0]|keys_unsorted' "$CJ")" '["name","app_name","app_id","rating","ratings","reviews_read","low_reviews","themes","top_theme","weak_point","weak_point_count","weak_point_share","weak_point_rule","gap_points","most_mentioned","weak_point_line","examples","reviews_from","reviews_to","fetched","source"]' "app fields"
eq "$(J '.apps[0].themes' "$CJ")" '{"fees":9,"cancellations":3,"support":5,"bugs":3,"trust":1,"payments":3,"availability":1}' "Muttmile themes"
eq "$(J '.apps[0]|[.top_theme,.weak_point,.weak_point_count,.low_reviews,.reviews_read]' "$CJ")" '["fees","fees and pricing",9,24,30]' "Muttmile weak point"
eq "$(J '.apps[0].weak_point_line' "$CJ")" '"9 of 24 recent 1-2 star reviews mention fees and pricing"' "weak point line"
eq "$(J '.apps[0].examples' "$CJ")" '[{"title":"Fees on fees","date":"2026-10-20","stars":1},{"title":"Too expensive now","date":"2026-10-16","stars":1},{"title":"They keep 25%","date":"2026-10-14","stars":2}]' "examples"
eq "$(J '.apps[1]|[.top_theme,.most_mentioned,.weak_point_share,.gap_points]' "$CJ")" '["cancellations","support",40.0,27.5]' "Pawpost weak point"
eq "$(J '.apps[1]|[.rating,.ratings]' "$CJ")" '[4.8,31007]' "Pawpost rating"
eq "$(J '.apps[0]|[.reviews_from,.reviews_to,.fetched]' "$CJ")" "[\"$LO\",\"$HI\",\"2026-10-26\"]" "review dates"
has "$(J -r '.apps[0].source' "$CJ")" "https://itunes.apple.com/us/rss/customerreviews/page=1/id=1900000101/" "source"
C pricing Muttmile --note "keeps 25% of each walk" --source "not a url"; eq "$CODE" 1 "not a URL"; eq "$ERR" "scout.py: --source needs the URL of the page you read (their pricing or help page)" "URL message"
C pricing Muttmile --note "  " --source "https://muttmile.example/help/fees"; eq "$CODE" 1 "blank note"; eq "$ERR" "scout.py: --note needs what the page says, in a sentence" "note message"
C pricing Muttmile --note "  " --source "http://x"; eq "$ERR" "scout.py: --source needs the URL of the page you read (their pricing or help page)" "the source is checked first (needs a dot)"
cp "$CJ" "$TMP/before.json"
C pricing muttmile --note "  keeps 25% of each walk " --source "https://muttmile.example/help/fees"; eq "$CODE" 0 "pricing without --write"
eq "$OUT" "Muttmile pricing: keeps 25% of each walk (https://muttmile.example/help/fees)
Not saved: add --write." "not saved"; cmp -s "$CJ" "$TMP/before.json" && ok || bad "nothing written without --write"
C pricing muttmile --note "keeps 25% of each walk" --source "https://muttmile.example/help/fees" --write; eq "$CODE" 0 "pricing --write"
eq "$(tail -1 <<<"$OUT")" "Wrote $TMP/w/seed/competitors.json." "pricing wrote"
C pricing Nobody --note x --source https://a.example/b; eq "$CODE" 1 "not in the file"; eq "$ERR" "scout.py: Nobody is not in $TMP/w/seed/competitors.json" "not in message"
S pricing Nobody --note x --source https://a.example/b --dir "$TMP/w/seed" --today nope; eq "$CODE" 1 "not in the file before --today"
S pricing $PAW --note "takes 20%" --source https://pawpost.example/fees --dir "$TMP/w/seed" --today bad; eq "$CODE" 2 "a found app, then --today"
C pricing " $PAW " --note "takes 20%" --source https://pawpost.example/fees --write; eq "$CODE" 0 "by app id"; has "$OUT" "Pawpost pricing: takes 20% (https://pawpost.example/fees)" "by id line"
C compare $MUTT $PAW --write >/dev/null
eq "$(J '.apps[0].pricing' "$CJ")" '[{"note":"keeps 25% of each walk","source":"https://muttmile.example/help/fees","read":"2026-10-26"}]' "a re-run keeps the pricing notes"
eq "$(J '.apps[0]|keys_unsorted|last' "$CJ")" '"pricing"' "pricing goes last"
C pricing Muttmile --note "and a booking fee" --source https://muttmile.example/b --write >/dev/null
eq "$(J '.apps[0].pricing|length' "$CJ")" 2 "a second note is added"
C compare $MUTT $PAW --json >/dev/null; eq "$(J '.apps[0].pricing|length' "$CJ")" 2 "compare without --write leaves the file"
C compare $MUTT $PAW --json; eq "$(J '.apps[0]|has("pricing")' <<<"$OUT")" false "pricing is merged only with --write"
C show; eq "$CODE" 0 "show"
has "$OUT" "Muttmile pricing: keeps 25% of each walk (https://muttmile.example/help/fees)" "show pricing"
has "$OUT" "  Pawpost pricing: takes 20% (https://pawpost.example/fees)" "show second app"
eq "$(sed -n 3p <<<"$OUT")" "$(printf '  %-14s %6s %10s %9s   %-27s %s' Muttmile 4.9 204,118 24/30 "fees and pricing" "9 of 24 (37.5%)")" "show table"
hasnt "$OUT" "Next:" "show has no next line"
C show --json; eq "$(jq -S . <<<"$OUT")" "$(jq -S . "$CJ")" "show --json is the file"
C compare $MUTT $PAW --json --write; eq "$(J '.apps[0].pricing|length' <<<"$OUT")" 2 "json and write: merged"; hasnt "$OUT" "Wrote" "json has no Wrote line"

# ---- refusals and exit codes (test_refusals_and_exit_codes)
rm -rf "$TMP/w/seed"
C themes $MUTT --offline "$DIR" --stars 5; eq "$CODE" 2 "--stars 5"
C reviews $MUTT --offline "$DIR" --pages 11; eq "$CODE" 2 "--pages 11"
C compare $MUTT $MUTT --offline "$DIR"; eq "$CODE" 2 "the same app twice"
C reviews 1900000999 --offline "$DIR"; eq "$CODE" 1 "no such app"
C show; eq "$CODE" 1 "show without a file"; eq "$OUT" "No $TMP/w/seed/competitors.json yet. Run: scout.py compare APP_ID APP_ID --write" "show message"
C pricing Muttmile --note x --source https://a.example/b; eq "$CODE" 1 "pricing without a file"
eq "$ERR" "scout.py: no competitors.json (run scout.py compare --write first) at $TMP/w/seed/competitors.json" "pricing without a file message"
S; eq "$CODE" 2 "no command"; has "$OUT" "usage: scout.py [-h] {find,reviews,themes,compare,pricing,show} ..." "usage"
has "$OUT" "Competitor Scout (seed-scout): your rivals' weak points, from their reviews." "description"
S nope; eq "$CODE" 2 "unknown command"
S pricing Muttmile --note x; eq "$CODE" 2 "--source is required"; has "$ERR" "the following arguments are required: --source" "required message"
S compare; eq "$CODE" 2 "compare needs an app"
S find; eq "$CODE" 2 "find needs a term"
S find x --limit nope; eq "$CODE" 2 "--limit int"
S reviews --help; eq "$CODE" 0 "help"; has "$OUT" "--offline DIR" "metavar"; has "$OUT" "save to seed/reviews/<app_id>.json" "write help"
S show --help; hasnt "$OUT" "--offline" "show has no network options"
[ -e "$TMP/w/seed" ] && bad "nothing written without --write" || ok
S show --dir "$TMP/w/other"; eq "$OUT" "No $TMP/w/other/competitors.json yet. Run: scout.py compare APP_ID APP_ID --write" "--dir"
cd "$TMP/w" && S show; eq "$OUT" "No seed/competitors.json yet. Run: scout.py compare APP_ID APP_ID --write" "default ./seed"

done_ "seed-scout/scout"

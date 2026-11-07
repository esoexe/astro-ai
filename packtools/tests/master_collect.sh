#!/usr/bin/env bash
# master-virality/collect: the cases of master-skills/skills/virality/tools/test_virality.py (test_counts_and_cta_strip,
# test_formats_and_dedup, test_cli_pipeline) plus hand-worked parses, run against the C++ tool. Every expected value
# is worked out by hand from collect.py's rules (noted where it is not obvious).
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
CO=(--skill-dir "$SK" master-virality/collect)
q() { $T "${CO[@]}" "$@" --quiet; }
cd "$TMP" || exit 1

# ---- upstream: three formats, one dead row, one duplicate Short
printf '# creator | views | hook | url\n@a | 1.2M | I feed four people on $62 a week. | https://www.tiktok.com/@a/video/1\n@a | 48K | Three pantry meals | https://www.tiktok.com/@a/video/2\n@a | 51,300 | My Sunday prep | https://www.tiktok.com/@a/video/3\n@a | 39.000 | Stop buying pre-cut vegetables. | https://www.tiktok.com/@a/video/4\n@a | n/a | no count here |\n@b | 3,1M | Comment "PLAN" and I'"'"'ll send you the plan \xf0\x9f\x91\x87 I cut our bill by $300. |\n' > p.txt
printf 'Account,Views,Caption,Link\n@c,22K,Five freezer meals,https://www.instagram.com/reel/x\n' > e.csv
echo '{"uploader_id": "@d", "channel": "D", "entries": [{"title": "One", "view_count": 100, "url": "https://www.youtube.com/shorts/AAAAAAAA1"}, {"title": "One again", "view_count": 100, "url": "https://youtube.com/shorts/AAAAAAAA1?x=1"}]}' > y.json
run "${CO[@]}" p.txt e.csv y.json
eq "$CODE" 0 "usable posts: exit 0"
eq "$(echo "$OUT" | jq -c '[.[] | [.channel, .views, .hook, .platform]]')" '[["@a",1200000,"I feed four people on $62 a week.","tiktok"],["@a",48000,"Three pantry meals","tiktok"],["@a",51300,"My Sunday prep","tiktok"],["@a",39000,"Stop buying pre-cut vegetables.","tiktok"],["@b",3100000,"I cut our bill by $300.",null],["@c",22000,"Five freezer meals","instagram"],["@d",100,"One","shorts"]]' "7 posts: the n/a row skipped, the Short kept once"
eq "$(echo "$OUT" | jq -c '.[4].title')" '"Comment \"PLAN\" and I'"'"'ll send you the plan 👇 I cut our bill by $300."' "the title keeps the call to action; the hook drops it"
eq "$(echo "$OUT" | jq -c '.[0] | keys_unsorted')" '["channel","title","hook","views","likes","comments","shares","saves","url","duration","posted","platform","followers","median"]' "post keys, in order"
has "$OUT" $'[\n {\n  "channel": "@a",' "indent 1"
eq "$ERR" "$(printf '\n  collected 7 posts from 4 creators, skipped 1 unusable rows\n      4  @a\n      1  @b   under 4: swipe will skip it unless you add a median\n      1  @c   under 4: swipe will skip it unless you add a median\n      1  @d   under 4: swipe will skip it unless you add a median\n')" "the summary"
run "${CO[@]}" p.txt e.csv y.json --min-posts 1
eq "$ERR" "$(printf '\n  collected 7 posts from 4 creators, skipped 1 unusable rows\n      4  @a\n      1  @b\n      1  @c\n      1  @d\n')" "--min-posts 1: no warnings"

# ---- the pipeline into swipe
run "${CO[@]}" p.txt -o c.json --quiet
eq "$CODE:$OUT:$ERR" "0::" "-o --quiet: silent"
SW=$($T --skill-dir "$SK" master-virality/swipe c.json --min 2.0)
has "$SW" "1,200,000" "swipe reads it"
has "$SW" "The Payoff" "and names the formula"
has "$SW" "@b (1)" "and says it skipped the thin creator"
eq "$($T --skill-dir "$SK" master-virality/swipe c.json --json | jq -c '.outliers[0] | [.channel, .multiple > 20]')" '["@a",true]' "the top outlier"
printf 'nothing here\n' > j.txt
run "${CO[@]}" j.txt
eq "$CODE" 1 "nothing usable: exit 1"
eq "$ERR" "no usable posts. Each needs a creator, a hook or title, and at least one count (views, likes, comments, shares or saves)." "and says why"
run "${CO[@]}" missing.txt
eq "$CODE:$ERR" "2:no such file: missing.txt" "missing file: exit 2"

# ---- counts (half to even; a 30-digit count stays exact)
cat > counts.json <<'EOF'
[{"creator":"@x","title":"a","likes":1,"views":"1.2M views"},{"creator":"@x","title":"b","likes":1,"views":"412K"},
{"creator":"@x","title":"c","likes":1,"views":"1.234"},{"creator":"@x","title":"d","likes":1,"views":"3,1M"},
{"creator":"@x","title":"e","likes":1,"views":"12,400 views"},{"creator":"@x","title":"f","likes":1,"views":"1.234.567"},
{"creator":"@x","title":"g","likes":1,"views":"3,5"},{"creator":"@x","title":"h","likes":1,"views":"2,5"},
{"creator":"@x","title":"i","likes":1,"views":"1,2,3"},{"creator":"@x","title":"j","likes":1,"views":"1.5k"},
{"creator":"@x","title":"k","likes":1,"views":"2bn"},{"creator":"@x","title":"l","likes":1,"views":"1 200"},
{"creator":"@x","title":"m","likes":1,"views":"1\u00a0200"},{"creator":"@x","title":"n","likes":1,"views":"~5K+"},
{"creator":"@x","title":"o","likes":1,"views":"n/a"},{"creator":"@x","title":"p","likes":1,"views":"-"},
{"creator":"@x","title":"q","likes":1,"views":12.6},{"creator":"@x","title":"r","likes":1,"views":2.5},
{"creator":"@x","title":"s","likes":1,"views":-3},{"creator":"@x","title":"t","likes":1,"views":true},
{"creator":"@x","title":"u","likes":1,"views":"1,234.5k"},{"creator":"@x","title":"v","likes":1,"views":"1,25M"},
{"creator":"@x","title":"w","likes":1,"views":"1,250M"},{"creator":"@x","title":"x","likes":1,"views":"\u0663\u0664"},
{"creator":"@x","title":"y","likes":1,"views":"12 likes"},{"creator":"@x","title":"z","likes":1,"views":"5 million"},
{"creator":"@x","title":"aa","likes":1,"views":"1.2.3"},{"creator":"@x","title":"ab","likes":1,"views":"1e5"},
{"creator":"@x","title":"ac","likes":1,"views":123456789012345678901234567890}]
EOF
eq "$(q counts.json | jq -c '[.[].views]')" '[1200000,412000,1234,3100000,12400,1234567,4,2,123,1500,2000000000,1200,1200,5000,null,null,13,2,null,null,1234500,1250000,1250000000,34,12,5000000,null,null,123456789012345678901234567890]' "parse_count"
has "$(q counts.json)" '"views": 123456789012345678901234567890,' "big counts print exactly"

# ---- durations
cat > dur.json <<'EOF'
[{"creator":"@x","title":"a","views":1,"duration":42},{"creator":"@x","title":"b","views":1,"duration":"0:42"},
{"creator":"@x","title":"c","views":1,"duration":"1:02:03"},{"creator":"@x","title":"d","views":1,"duration":"42s"},
{"creator":"@x","title":"e","views":1,"duration":"1m 20s"},{"creator":"@x","title":"f","views":1,"duration":"PT1M20S"},
{"creator":"@x","title":"g","views":1,"duration":"PT1H"},{"creator":"@x","title":"h","views":1,"duration":"2 hours 5 min"},
{"creator":"@x","title":"i","views":1,"duration":"1:2"},{"creator":"@x","title":"j","views":1,"duration":"90.5"},
{"creator":"@x","title":"k","views":1,"duration":"abc"},{"creator":"@x","title":"l","views":1,"duration":-5},
{"creator":"@x","title":"m","views":1,"duration":"-5"},{"creator":"@x","title":"n","views":1,"duration":"PT"}]
EOF
eq "$(q dur.json | jq -c '[.[].duration]')" '[42.0,42.0,3723.0,42.0,80.0,80.0,3600.0,7500.0,62.0,90.5,null,null,-5.0,null]' "parse_duration (a string -5 falls through to float())"
has "$(q dur.json)" '"duration": 42.0,' "durations are floats"

# ---- dates (1767225600 = 2026-01-01T00:00:00Z; .9999996 s rounds up to the next second)
cat > dates.json <<'EOF'
[{"creator":"@x","title":"a","views":1,"date":20260901},{"creator":"@x","title":"b","views":1,"date":"2026-09-01T10:00:00Z"},
{"creator":"@x","title":"c","views":1,"date":1767225600},{"creator":"@x","title":"d","views":1,"date":1767225600000},
{"creator":"@x","title":"e","views":1,"date":1767225599.9999996},{"creator":"@x","title":"f","views":1,"date":1767225599.4},
{"creator":"@x","title":"g","views":1,"date":"Sep 1, 2026"},{"creator":"@x","title":"h","views":1,"date":"september 1, 2026"},
{"creator":"@x","title":"i","views":1,"date":"1 Sep 2026"},{"creator":"@x","title":"j","views":1,"date":"01/02/2026"},
{"creator":"@x","title":"k","views":1,"date":"13/02/2026"},{"creator":"@x","title":"l","views":1,"date":"2026/2/3"},
{"creator":"@x","title":"m","views":1,"date":"Feb 30, 2026"},{"creator":"@x","title":"n","views":1,"date":"Sept 1, 2026"},
{"creator":"@x","title":"o","views":1,"date":"Sep  1,   2026"},{"creator":"@x","title":"p","views":1,"date":"2026-13-45"},
{"creator":"@x","title":"q","views":1,"date":"SEP 1, 2026"},{"creator":"@x","title":"r","views":1,"date":"Feb 29, 2024"},
{"creator":"@x","title":"s","views":1,"date":"Feb 29, 2025"},{"creator":"@x","title":"t","views":1,"date":99999999},
{"creator":"@x","title":"u","views":1,"date":100000001},{"creator":"@x","title":"v","views":1,"date":"20261"}]
EOF
eq "$(q dates.json | jq -c '[.[].posted]')" '["2026-09-01","2026-09-01","2026-01-01","2026-01-01","2026-01-01","2025-12-31","2026-09-01","2026-09-01","2026-09-01","2026-01-02","2026-02-13","2026-02-03",null,null,"2026-09-01","2026-13-45","2026-09-01","2024-02-29",null,"9999-99-99","1973-03-03",null]' "parse_date"
echo '[{"creator":"@x","title":"a","views":1,"date":NaN}]' > nan.json
run "${CO[@]}" nan.json
eq "$CODE:$ERR" "2:cannot convert float NaN to integer" "a NaN date is bad input"
echo '[{"creator":"@x","title":"a","views":1,"date":253402300800000}]' > yr.json
run "${CO[@]}" yr.json
eq "$CODE:$ERR" "2:year 10000 is out of range" "past 9999-12-31 is bad input"
eq "$(echo '[{"creator":"@x","title":"b","views":1,"date":253402300799000}]' | q - | jq -c '[.[].posted]')" '["9999-12-31"]' "the last day"

# ---- platform, hooks
echo '[{"creator":"@p","title":"a","views":1,"platform":"YoutubeTab"},{"creator":"@p","title":"b","views":1,"platform":"TikTok"},{"creator":"@p","title":"c","views":1,"platform":"Mastodon"},{"creator":"@p","title":"d","views":1,"url":"https://www.linkedin.com/x","platform":"tiktok"},{"creator":"@p","title":"e","views":1,"url":"https://youtu.be/abcdefg?feature=shorts"},{"creator":"@p","title":"f","views":1,"url":"https://youtu.be/abcdefh"},{"creator":"@p","title":"g","views":1}]' > plat.json
eq "$(q plat.json --platform tiktok | jq -c '[.[].platform]')" '["youtube","tiktok","mastodon","linkedin","shorts","youtube","tiktok"]' "platforms: the url wins, then the hint, then --platform"
echo '[{"creator":"@x","title":"a","views":1,"platform":5}]' > pe.json
run "${CO[@]}" pe.json
eq "$CODE:$ERR" "1:master-virality/collect: AttributeError: 'int' object has no attribute 'strip'" "a numeric platform fails as the original does"
X200=$(printf 'x%.0s' $(seq 200))
echo '[{"creator":"@c","views":1,"title":"Type this into ChatGPT. It plans your week."},{"creator":"@c","views":1,"title":"Comment GUIDE for the full guide: Why most diets fail."},{"creator":"@c","views":1,"title":"Comment X and I'"'"'ll send it!"},{"creator":"@c","views":1,"title":"'"$X200"'"},{"creator":"@c","views":1,"title":"t","hook":"  Real   hook  "},{"creator":"@c","views":1,"title":"Line one\nLine two"},{"creator":"@c","views":1,"title":"Big news \ud83d\udc47 details"},{"creator":"@c","views":1,"title":"Comment \"X\" and I'"'"'ll send it \ud83d\udc47 I made $9M."}]' > cta.json
eq "$(q cta.json | jq -c '[.[].hook | if length > 100 then length else . end]')" '["Type this into ChatGPT.","Why most diets fail.","Comment X and I'"'"'ll send it!",160,"Real hook","Line one Line two","Big news","I made $9M."]' "hooks: first sentence, CTA stripped unless it is everything, 160 max"

# ---- shapes
echo '{"channel":"Top","channel_follower_count":5000,"extractor_key":"YoutubeTab","entries":[{"entries":[{"title":"Deep","view_count":7,"url":"https://www.youtube.com/watch?v=BBBBBBBB2"}],"uploader":"Inner"},{"title":"Flat","view_count":9,"id":"x","uploader_id":"@flat"}]}' > yt2.json
eq "$(q yt2.json | jq -c '[.[] | [.channel, .title, .views, .platform, .followers]]')" '[["Inner","Deep",7,"youtube",null],["@flat","Flat",9,"youtube",5000]]' "yt-dlp: nested tabs, parent defaults, @handles"
echo '[{"creator":"@u","title":"one","views":1,"url":"https://x.com/a/status/1?s=20"},{"creator":"@u","title":"two","views":2,"url":"https://X.com/a/status/1/"},{"creator":"@V","title":"Same","views":1},{"creator":"v","title":"same","views":2}]' > dup.json
eq "$(q dup.json | jq -c '[.[] | [.channel, .title, .platform]]')" '[["@u","one","x"],["@V","Same",null]]' "duplicates by canonical url, else by creator + title"
printf '{"creator":"@j","title":"L1","views":1}\n{"creator":"@j","title":"L2","views":2}\n' > l.jsonl
eq "$(q l.jsonl | jq -c '[.[].title]')" '["L1","L2"]' "JSON lines"
echo '{"posts": [{"creator":"@w","title":"Wrapped","views":3}, 5]}' > w.json
eq "$(q w.json | jq -c '[.[].title]')" '["Wrapped"]' "a {posts: [...]} wrapper"
printf '{oops\n' > bad.json
run "${CO[@]}" bad.json
eq "$CODE:$ERR" "2:bad.json looks like JSON but does not parse" "broken JSON: exit 2"
printf 'creator\tviews\ttitle\n@t\t1,5K\tTab post\n' > t.tsv
printf 'creator,views,title\n@q,"12,400","Hello, world"\n' > q.csv
printf 'creator | plays | caption\n@h | 5K | Hello there\n' > hd.txt
eq "$(q t.tsv q.csv hd.txt | jq -c '[.[] | [.channel, .views, .title]]')" '[["@t",1500,"Tab post"],["@q",12400,"Hello, world"],["@h",5000,"Hello there"]]' "TSV, quoted CSV, a pasted header row"
eq "$(echo '[{"title":"Mine","views":5}]' | q - --creator @me | jq -c '[.[].channel]')" '["@me"]' "--creator fills rows without one"
echo '[{"creator":"@m","title":"a","likes":5},{"creator":"@m","title":"b","likes":6,"median":100}]' > mv.json
run "${CO[@]}" mv.json
eq "$ERR" "$(printf '\n  collected 2 posts from 1 creators\n      2  @m\n  2 posts have no view count; rank them with swipe --metric likes\n')" "a median silences the thin warning; missing views are counted"

# ---- --append
echo '[{"creator":"@a","title":"Dup","views":9,"url":"https://www.tiktok.com/@a/video/1?lang=en"},{"creator":"@z","title":"New","views":3}]' > a.json
run "${CO[@]}" a.json -o c.json --append
eq "$(jq -c '[.[].title]' c.json)" '["I feed four people on $62 a week.","Three pantry meals","My Sunday prep","Stop buying pre-cut vegetables.","Comment \"PLAN\" and I'"'"'ll send you the plan 👇 I cut our bill by $300.","New"]' "--append keeps the old list and adds only new urls"
has "$ERR" "  wrote c.json" "says where"
has "$ERR" "collected 6 posts from 3 creators" "and summarises the merged list"

# ---- help
run "${CO[@]}" -h
eq "$OUT" "$(cat <<'EOF'
usage: collect [-h] [-o OUT] [--creator CREATOR] [--platform PLATFORM]
               [--append] [--min-posts MIN_POSTS] [--quiet]
               inputs [inputs ...]

Normalize pasted posts, CSV exports and yt-dlp JSON into one list for swipe.

positional arguments:
  inputs                files to read, or - for stdin

options:
  -h, --help            show this help message and exit
  -o OUT, --out OUT     write the JSON here (default: stdout)
  --creator CREATOR     creator to use for rows that have none (one creator's
                        list)
  --platform PLATFORM   platform for rows that have none: shorts, tiktok,
                        instagram, linkedin
  --append              merge into an existing --out file
  --min-posts MIN_POSTS
                        warn about creators with fewer posts than this
                        (default 4)
  --quiet               no summary on stderr

Paste format, one post per line: creator | views | hook or title | url |
duration | date
EOF
)" "argparse help, wrapped at 78 columns"
run "${CO[@]}"
eq "$CODE" 2 "no inputs: argparse exit 2"
has "$ERR" "collect: error: the following arguments are required: inputs" "says so"
done_ "master-virality/collect"

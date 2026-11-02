#!/usr/bin/env bash
# The X pack: x-post/count, x-post/hookscore, x-human/detect, x-human/humanize, x-thread/thread, x-plan/weights,
# x-viral/pull, x-viral/swipe and x-audit/audit. Expected values were worked out by hand from the Python originals
# (x-agent-skill@7822c29). Python behaviour asserted where it is easy to get wrong: a short post's VOICE score is
# the int 50 in JSON, statistics.median of an even count is a float, round() is half-even on the exact double,
# strptime ignores %a (2026-10-06 is a Tuesday whatever the CSV says), and emoji ZWJ sequences and flag tag
# characters survive the invisible-character pass.
source "$(dirname "$0")/lib.sh"
PK="$(cd "$(dirname "$0")/../../library/plugins/astro-x/skills" && pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
j() { jq -c "$1" <<<"$OUT"; }
x() { local k="$1"; shift; run --skill-dir "$PK/${k%%/*}" "$k" "$@"; }

# ---------------------------------------------------------------- fixtures
printf 'A thread \xf0\x9f\xa7\xb5 on how I grew to 10k followers\n\nFirst, I stopped posting links \xe2\x80\x94 every link is https://example.com/very/long/path?x=1 and costs 23.\n\nThoughts?\n#growth #x\n' > p1.txt
{ for i in $(seq 1 30); do printf 'word%s ' "$i"; done
  printf 'then a much longer tail of text that keeps going and going until it crosses the fold for sure, honestly, with more words here and there and everywhere so that we pass two hundred and eighty characters @latecomer\n'; } > long.txt
printf 'I spent 9 months building the wrong product. Here is what I learned.\n\nNobody wanted the dashboard. They wanted the CSV export, which took me one afternoon.\n\nSo I deleted 40,000 lines of code. Revenue went up the next month.\n\nFollow for more, and steal this: ship the boring thing first.\n' > t1.txt
yes 'This sentence just keeps on going with more and more words that never reach a full stop and so on' | head -5 | tr '\n' ' ' > run.txt
printf 'hey guys, in this thread I will show you how\n1/12 Nobody tells you why your SaaS churns\nStop scrolling\n' > hooks.txt
echo 'A thread 🧵 on pricing.' > s1.txt
echo 'I shipped the new pricing page today. Thoughts?' > s2.txt
printf 'Built this with my team \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb in Cardiff \xf0\x9f\x8f\xb4\xf3\xa0\x81\xa7\xf3\xa0\x81\xa2\xf3\xa0\x81\xb7\xf3\xa0\x81\xac\xf3\xa0\x81\xb3\xf3\xa0\x81\xbf \xe2\x80\x8btoday. It is important to note \xe2\x80\x94 we delve into it.\n\nfollow me for more and RT if you agree!\n' > h1.txt
{ printf 'url\taccount\tformat\tviews\tlikes\treplies\thook\ttext\n'
  for i in 1 2 3 4 5; do printf 'https://x.com/a/status/%s\t@alice\ttext\t%s\t%s\t%s\tI lost $%s,000 because nobody told me this\t\n' "$i" $((i*1000)) $((i*10)) "$i" "$i"; done
  printf 'https://x.com/a/status/9\t@alice\tmedia\t40k\t900\t30\tStop writing threads. Do this instead\tStop writing threads. Do this instead \xe2\x8f\x8e 1. one \xe2\x8f\x8e 2. two \xe2\x8f\x8e 3. three\n'
  printf 'https://x.com/b/status/1\t@bob\ttext\t5,000\t\t\tHello friends\t\n'
} > sw.tsv
{ printf '\xef\xbb\xbfPost id,Date,Post text,Post Link,Impressions,Likes,Engagements,Bookmarks,Shares,New follows,Replies,Reposts,Profile visits\n'
  printf '1,"Mon, Oct 06, 2026","I lost $18,000 in 3 months because nobody told me this",https://x.com/a/status/1,"12,000",300,600,40,5,3,20,10,9\n'
  printf '2,"Tue, Oct 07, 2026",Here is my morning routine for focus,https://x.com/a/status/2,800,10,20,1,0,0,2,1,0\n'
  printf '3,2026-10-08,"Stop writing threads.\nDo this instead",https://x.com/a/status/3,5000,90,250,9,2,1,8,4,3\n'
  printf '4,2026-10-09 14:30:00,A short one https://t.co/abc,https://x.com/a/status/4,40,1,2,0,0,0,0,0,0\n'
  printf '5,10/10/2026,@bob great point,https://x.com/a/status/5,300,2,4,0,0,0,1,0,0\n'
  printf '6,"Oct 11, 2026",Nobody tells you this about pricing,https://x.com/a/status/6,2000,40,90,5,1,0,6,2,1\n'
} > au.csv

# ---------------------------------------------------------------- x-post/count
x x-post/count p1.txt
eq "$CODE" 0 "count rc"
has "$OUT" "+--------------------------------------------- 150/280 +" "count box footer"
has "$OUT" "counted as: 1 link(s) at 23, 1 emoji at 2" "count weights"
has "$OUT" "PASS  LENGTH      150 / 280, 130 left" "count length"
has "$OUT" 'WARN  FIRST LINE  opens on "thread". That is packaging' "count thread opener"
has "$OUT" "WARN  HASHTAGS    2: #growth #x." "count hashtags"
has "$OUT" "WARN  EM DASH     1. Run it through /x-human" "count em dash"
has "$OUT" "PASS  ENDING      no reflex bait" "count ending"
x x-post/count p1.txt --json
eq "$(j '[.weighted,.limit]')" "[150,280]" "count json weighted"
eq "$(j '[.checks[].status]')" '["PASS","WARN","NOTE","WARN","PASS","PASS","WARN"]' "count json statuses"
x x-post/count long.txt --premium
has "$OUT" "WARN  LENGTH      412 / 25000 (Premium long post)" "premium length"
has "$OUT" "WARN  MENTIONS    @latecomer sits past character 280." "premium late mention"
x x-post/count long.txt
has "$OUT" "WHAT FITS" "over-limit fits box"
x x-post/count /dev/null
eq "$CODE" 2 "count empty rc"
eq "$ERR" "nothing to count" "count empty msg"

# ---------------------------------------------------------------- x-post/hookscore
x x-post/hookscore --hook 'I lost $18,000 in 3 months because nobody told me this'
eq "$CODE" 0 "hookscore rc"
has "$OUT" "formula: #3 Cost Confession" "hookscore formula"
has "$OUT" '2 concrete marker(s): $18,000, 3 months' "hookscore specificity"
has "$OUT" "4 tension marker(s): lost, nobody, a price, an open loop" "hookscore stakes"
has "$OUT" "ADDRESS       ##################......  75.0" "hookscore address"
has "$OUT" "HOOK SCORE    #####################...  87.0   STRONG" "hookscore total"
x x-post/hookscore --hook "Stop scrolling" --json
eq "$(j '[.formula_id,.score,.verdict,.weakest]')" '[12,56.4,"OK","LENGTH"]' "hookscore json"
eq "$(j '.checks.SPECIFICITY.detail')" '"1 concrete marker(s): Stop"' "hookscore capitalised opener is a name"
x x-post/hookscore hooks.txt
eq "$CODE" 0 "ranking rc (top >= 70)"
has "$OUT" "->  85.0 OK      1/12 Nobody tells you why your SaaS churns" "ranking top"
has "$OUT" "    56.4 OK      Stop scrolling" "ranking second"
has "$OUT" "     0.0 WEAK    hey guys, in this thread I will show you how" "ranking last"
has "$OUT" "dealbreaker: Greeting. Nobody opened X to be greeted." "ranking greeting"
x x-post/hookscore /dev/null
eq "$CODE" 2 "hookscore empty rc"

# ---------------------------------------------------------------- x-human/detect
x x-human/detect p1.txt
eq "$CODE" 1 "detect flagged rc"
has "$OUT" "n/a: 3 sentence(s), needs 4 to measure variation" "detect burstiness n/a"
has "$OUT" "2 concrete markers, 6.9 per 100 words (want 4+)" "detect specificity"
has "$OUT" "VOICE         #############...........  52.9" "detect voice"
has "$OUT" "HUMAN SCORE   #########...............  37.9   FLAGGED" "detect total"
has "$OUT" "Weakest signal: FINGERPRINT. Fix that first." "detect weakest"
x x-human/detect s2.txt --json
eq "$(j '.checks.VOICE.score')" "50" "short-post voice stays an int"
x x-human/detect s2.txt
has "$OUT" "HUMAN SCORE   #################.......  70.0   REVIEW" "short post total"
has "$OUT" "short post: scored on 3 of 5 checks" "short post note"
x x-human/detect s1.txt s2.txt
has "$OUT" "100.0 PASS  ->  70.0 REVIEW   (-30.0)" "detect compare"

# ---------------------------------------------------------------- x-human/humanize
x x-human/humanize h1.txt
eq "$CODE" 0 "humanize rc"
eq "$OUT" "$(printf 'Built this with my team \xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb in Cardiff \xf0\x9f\x8f\xb4\xf3\xa0\x81\xa7\xf3\xa0\x81\xa2\xf3\xa0\x81\xb7\xf3\xa0\x81\xac\xf3\xa0\x81\xb3\xf3\xa0\x81\xbf today. It is important to note, we look at it.')" "humanize keeps ZWJ + flag tags"
x x-human/humanize h1.txt --report
has "$ERR" "5 machine artefacts removed, 0 structural tells flagged for rewrite" "humanize report total"
has "$ERR" "1x  U+200B ZERO WIDTH SPACE  -> delete" "humanize report zwsp"
hasnt "$ERR" "U+200D" "humanize report leaves the joiner alone"
has "$ERR" "1x  rt if you agree  -> (deleted)   [closers]" "humanize report closer"

# ---------------------------------------------------------------- x-thread/thread
x x-thread/thread t1.txt --number
eq "$CODE" 0 "thread rc"
has "$OUT" "THREAD  ·  4 posts  ·  300 weighted characters" "thread header"
has "$OUT" "[1/4]  73/280" "thread numbered weight"
has "$OUT" "PASS  LAST POST  has an ask" "thread ask"
x x-thread/thread t1.txt --pack --limit 120 --json
eq "$(j '[.posts[].weighted]')" "[68,85,66,61]" "thread pack keeps posts over budget apart"
x x-thread/thread run.txt
has "$OUT" "[1/2]  278/280  ! split mid-sentence" "thread long sentence split"
has "$OUT" "WARN  BREAKS     post(s) [1] end mid-sentence" "thread breaks warn"
x x-thread/thread /dev/null
eq "$CODE" 2 "thread empty rc"

# ---------------------------------------------------------------- x-plan/weights (offline: snapshot)
x x-plan/weights
eq "$CODE" 0 "weights rc"
has "$ERR" "could not read the live file (<urlopen error network is off (ASTRO_TOOL_OFFLINE=1)>). Showing the snapshot instead." "weights offline note"
has "$OUT" "  share by copying the link                     20     40.0x" "weights row"
x x-plan/weights --json
eq "$(j '.weights|length')" "21" "weights json count"

# ---------------------------------------------------------------- x-viral/pull (offline)
x x-viral/pull https://x.com/jack/status/20 "see twitter.com/jack/status/20 and https://mobile.twitter.com/x/statuses/99"
eq "$CODE" 0 "pull rc"
eq "$OUT" "$(printf 'url\taccount\tname\tdate\tformat\tlines\twords\tviews\tlikes\treplies\treposts\thook\ttext')" "pull header only"
has "$ERR" "2/2  skipped: <urlopen error network is off (ASTRO_TOOL_OFFLINE=1)>  https://x.com/x/status/99" "pull dedupe + mobile url"
has "$ERR" "0 pulled, 2 skipped." "pull summary"
x x-viral/pull nothing-here.txt
eq "$CODE" 1 "pull missing file rc"

# ---------------------------------------------------------------- x-viral/swipe
x x-viral/swipe sw.tsv --out swo/s.md
eq "$CODE" 0 "swipe rc"
has "$OUT" "   11.4x  #12 The Command          list (3 items)  media  @alice              40,000 views" "swipe top (even median 3500.0)"
has "$OUT" "    1.4x  #3  Cost Confession      one line        text   @alice               5,000 views" "swipe second"
has "$OUT" "  hook words top: 7.0   bottom: 8.0" "swipe word medians are floats"
has "$OUT" "  replies per like   top: 0.067   bottom: 0.1" "swipe reply ratio half-even"
has "$OUT" "SKIPPED (too few posts to know the account's normal): @bob (1)" "swipe skipped"
eq "$ERR" "wrote swo/s.md" "swipe wrote"
has "$(cat swo/s.md)" "- views: 40,000  (account median 3,500)" "swipe markdown median"
x x-viral/swipe sw.tsv --min-posts 7
eq "$CODE" 2 "swipe min-posts rc"
x x-viral/swipe /dev/null
eq "$ERR" "no usable rows. Fill in the views (or likes) column first." "swipe empty"

# ---------------------------------------------------------------- x-audit/audit
x x-audit/audit au.csv
eq "$CODE" 0 "audit rc"
has "$OUT" "AUDIT  ·  5 posts  ·  Oct 06 - Oct 11  ·  1 replies set aside" "audit header"
has "$OUT" '    5.0%    1.7 rep/1k    1.2 shr/1k    12,000 imp  "I lost $18,000 in 3 months because nobody..."' "audit top"
has "$OUT" '    2.5%    2.5 rep/1k    1.2 shr/1k       800 imp  "Here is my morning routine for focus"' "audit bottom"
has "$OUT" "Replies: 1 of them earned 300 impressions, 2% of what your posts earned." "audit replies"
has "$OUT" "Day of week shows nothing. Stop optimising it." "audit weekday"
x x-audit/audit au.csv --json
eq "$(j '[.posts,.replies_set_aside,.floor]')" "[5,1,50]" "audit json counts"
eq "$(j '[.ranked[0].date,.ranked[0].weekday,.ranked[0].formula]')" '["2026-10-06T00:00:00","Tue","Cost Confession"]' "audit strptime ignores %a"
eq "$(j '[.ranked[].impressions]')" "[12000.0,5000.0,2000.0,800.0]" "audit ranked (floor drops the 40)"
x x-audit/audit /dev/null
eq "$CODE" 2 "audit empty rc"

done_ "x pack"

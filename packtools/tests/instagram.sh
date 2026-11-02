#!/usr/bin/env bash
# The instagram pack: ig-caption/caption, ig-reel/beats, ig-reel/hookscore, ig-viral/swipe and ig-human/humanize
# (ig-human/detect is covered by youtube.sh). Expected values were worked out by hand from the Python originals
# (instagram-agent-skill@d03c56b); where the Node port differed, the Python behaviour is what is asserted:
# textwrap keeps a first line's leading tabs, negative --truncate slices from the end, \d and int() take any
# Unicode decimal digit, statistics.median of an odd count keeps the int, and os.makedirs("") fails.
source "$(dirname "$0")/lib.sh"
PK="$(cd "$(dirname "$0")/../../library/plugins/astro-instagram/skills" && pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
CA="ig-caption/caption"; BE="ig-reel/beats"; HS="ig-reel/hookscore"; SW="ig-viral/swipe"
j() { jq -c "$1" <<<"$OUT"; }
sw() { run --skill-dir "$PK/ig-viral" "$SW" "$@"; }

# ---------------------------------------------------------------- fixtures
cat > cap1.txt <<'EOF'
I lost $18,000 because Sarah never signed the contract. Here is the clause that would have saved me, word for word, and why it works.

Most freelancers skip this. Don't.
Save this for your next client. Comment CLAUSE and I'll DM it.

#freelance #contracts #viral #fyp #smallbusiness #explore
EOF
printf '#start here\n' > cap2.txt
printf 'hello friends \xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80 \xe2\x98\x80 \xe2\x86\x92 ok\nlink: example.com/x and https://a.b/c\n\tTabbed\tline with a verylongwordthatkeepsgoingandgoingandgoingandgoingandgoingpastfiftytwo chars\n' > cap3.txt
printf '\n\n   \n' > blank.txt
printf 'Sarah saved $4,000 with one clause in her client contract.\nIt names the payment date, the late fee and the kill fee, all in plain words, so nobody argues later.\nSave this for your next contract.\n\n#contracts #freelance\n' > ready.txt
cat > script.txt <<'EOF'
I lost $18,000 on one missing contract.
Here is what happened.
The client loved the work.
Then they stopped answering.
No signature meant no leverage, so I ate the cost and the lesson, and it took me three months to recover from it all.
This is the clause I use now. It names the payment date, the late fee and the kill fee, so nobody can walk away without paying for what they asked for. Copy it. It took a lawyer two hours to write and it has saved me more than that every single year since.
Save this before your next contract costs you $18,000.
EOF
printf 'hello there\nwhat is up\nnothing much really\njust vibes today\n' > abstract.txt
printf 'I spent three years and forty thousand dollars building a product nobody wanted to buy\n' > longhook.txt
cat > hooks.txt <<'EOF'
I lost $18,000 on one missing contract.
Hey guys, welcome back to my channel
Stop sending proposals without this clause
nobody tells you your first 30 reels flop
In this video I'm going to show you how to grow #growth 🚀
Three clients. Zero contracts. One expensive lesson that I will never forget for the rest of my career honestly
EOF
printf 'account\tfollowers\tmedian\tviews\thook\n@a\t48000\t11000\t412000\tnobody tells you your first 30 flop\n@a\t48000\t11000\t9000\tI lost $18,000 on one missing contract.\n@b\t2,000,000\t\t400,000\tHey guys welcome back\n@c\t4000\t\t400000\tStop sending proposals without this clause\n@d\t\t\t5000\tthree mistakes that cost me my first client\n# comment\n@e\t10k\t900\t0\tzero views row dropped\n@f\t1000\t500\t12000\t\n@g\t\xd9\xa3\xd9\xa0\xd9\xa0\t\t\xd9\xa1\xd9\xa2\xd9\xa0\xd9\xa0\tArabic digits count too\n' > cap.tsv
printf '@x\t1000\t50000\tno header row: four columns\n@y\t2000\t1000\tsecond row with a figure 3x\n' > nohead.tsv

# ================================================================ ig-caption/caption
run $CA cap1.txt
eq "$CODE" "1" "caption: FIX exits 1"
has "$OUT" "CAPTION LINT  ·  291 / 2200 chars  ·  6 hashtags  ·  2 ask(s)" "caption: header (U+00B7 separators)"
has "$OUT" "  | I lost \$18,000 because Sarah never signed the        |" "caption: box line 1 (textwrap at 52)"
has "$OUT" "  | me, word for word, and why i                         |" "caption: the cut lands mid-word at 125"
has "$OUT" "  +-------------------------------------------- ... more +" "caption: truncated tail"
has "$OUT" "  WARN  FIRST LINE        133 characters, so it gets cut at 125 mid-thought." "caption: first line too long"
has "$OUT" "  PASS  HOOK IS CONCRETE  4 number(s) or name(s) in the visible window" "caption: concrete (\$1, 000, Sarah, Here)"
has "$OUT" "  FAIL  HASHTAGS          6 tags, over Instagram's cap of 5. Tags past the fifth do not count" "caption: over the cap"
has "$OUT" "  WARN  ONE ASK           2 asks (comment a keyword, save this). Two asks is the same as none" "caption: two asks"
has "$OUT" "  PASS  EMOJI             0 emoji, 0.0 per 100 characters" "caption: no emoji"
has "$OUT" "  VERDICT  FIX" "caption: verdict"
run $CA cap1.txt --json
eq "$CODE" "1" "caption --json still exits on the verdict"
eq "$(j '[.characters,.limit,.truncate_at,.first_line_chars,.emoji,.truncated,.verdict]')" '[291,2200,125,133,0,true,"FIX"]' "caption json: numbers"
eq "$(j '.hashtags')" '["#freelance","#contracts","#viral","#fyp","#smallbusiness","#explore"]' "caption json: hashtags"
eq "$(j '[.asks, .links, .mentions]')" '[["comment a keyword","save this"],[],[]]' "caption json: asks, links, mentions"
eq "$(j '[.checks[].check]')" '["LENGTH","FIRST LINE","HOOK IS CONCRETE","HASHTAGS","TAG PLACEMENT","LINKS","ONE ASK","EMOJI"]' "caption json: eight checks, in order"
eq "$(j '.checks[0] | keys_unsorted')" '["check","status","detail"]' "caption json: check keys"
eq "$(j '.visible | length')" "125" "caption json: visible window is 125 code points"
eq "$(sed -n 2p <<<"$OUT")" '  "characters": 291,' "caption json: indent 2"

run $CA cap2.txt --json
eq "$(j '[.checks[1].status,.checks[1].detail]')" '["FAIL","opens on a hashtag or a mention, which is the one position worth a sentence"]' "caption: opens on a hashtag"
eq "$(j '[.checks[4].status,.checks[4].detail]')" '["WARN","a hashtag is inside the visible window, spending feed space on a label"]' "caption: tag in the window"
eq "$(j '[.checks[3].detail,.truncated]')" '["1 tag(s): #start",false]' "caption: one tag listed"
run $CA cap2.txt
has "$OUT" "  +-------------------------------- (whole caption fits) +" "caption: whole caption fits"

run $CA cap3.txt
has "$OUT" "  |         Tabbed  line with a                          |" "caption: tabs expanded to 8, first-line indent kept (textwrap)"
has "$OUT" "  | verylongwordthatkeepsgoingandgoingandgoing           |" "caption: the window cuts the long word at 125"
has "$OUT" "  WARN  LINKS             2 link(s) in the caption, and captions are not clickable. Move it to the bio or the DM" "caption: links"
has "$OUT" "  PASS  EMOJI             5 emoji, 3.1 per 100 characters" "caption: emoji density 500/159"
has "$OUT" "  WARN  ONE ASK           no call to action. Decide what this post is for" "caption: no ask"
has "$OUT" "  PASS  HOOK IS CONCRETE  1 number(s)" "caption: a capital after a tab is not at line start"
eq "$CODE" "1" "caption: REVIEW exits 1"
run $CA cap3.txt --json
eq "$(j '[.characters,.emoji,.links]')" '[159,5,["example.com/x","https://a.b/c"]]' "caption json: bare-domain link and emoji (U+2600 block, arrows)"

run $CA blank.txt --json
eq "$(j '[.characters,.first_line_chars,.visible,.verdict]')" '[0,0,"","FIX"]' "caption: blank caption"
eq "$(j '.checks[1].detail')" '"the caption opens on a blank line"' "caption: blank first line"
eq "$(j '.checks[2].detail')" '"0 number(s) or name(s) in the visible window - nothing checkable before the tap"' "caption: nothing concrete"

run $CA ready.txt
eq "$CODE" "0" "caption: READY exits 0"
has "$OUT" "  PASS  TAG PLACEMENT     tags are below the fold" "caption: tags below the fold"
has "$OUT" "  PASS  HASHTAGS          2 tag(s): #contracts #freelance" "caption: tags listed"
has "$OUT" "  PASS  HOOK IS CONCRETE  2 number(s)" "caption: a name opening the line is not counted"
has "$OUT" "  PASS  ONE ASK           one call to action: save this" "caption: one ask"
has "$OUT" "  VERDICT  READY" "caption: READY"

run $CA cap1.txt --keywords "contract,proposal software, ,SARAH"
has "$OUT" "  WARN  SEARCH TERMS      2/3 present, 2 in the visible window. Missing: proposal software" "caption: keywords (case-insensitive, blanks dropped)"
run $CA cap1.txt --keywords "pricing"
has "$OUT" "  FAIL  SEARCH TERMS      0/1 present. Missing: pricing" "caption: no keyword found"
run $CA cap1.txt --keywords "contract,clause"
has "$OUT" "  PASS  SEARCH TERMS      2/2 present, 2 in the visible window" "caption: all keywords"

run $CA cap1.txt --truncate 40 --json
eq "$(j '[.visible,.truncate_at]')" '["I lost $18,000 because Sarah never signe",40]' "caption: --truncate 40"
run $CA cap1.txt --truncate=-5 --json
eq "$(j '[(.visible | length), (.visible | endswith(" #ex")), .truncated, .truncate_at]')" '[286,true,true,-5]' "caption: negative --truncate slices from the end, as Python does"
run $CA - < cap1.txt
eq "$CODE" "1" "caption: - reads stdin"
has "$OUT" "291 / 2200 chars" "caption: stdin text"
run $CA < ready.txt
eq "$CODE" "0" "caption: no input means stdin"
run $CA nope.txt
eq "$CODE|$ERR" "1|ig-caption/caption: OSError: [Errno 2] No such file or directory: 'nope.txt'" "caption: missing file"
run $CA --truncate x
eq "$CODE" "2" "caption: --truncate must be an int"
has "$ERR" "argument --truncate: invalid int value: 'x'" "caption: argparse wording"
run $CA -h
has "$OUT" "characters shown before '... more' (default 125)" "caption: help"

# ================================================================ ig-reel/beats
run $BE script.txt --target 30
eq "$CODE" "0" "beats: exit 0"
has "$OUT" "BEAT SHEET  ·  106 words  ·  ~38.5s at 165 wpm  ·  target 30.0s" "beats: header (wpm stays an int, target is a float)"
has "$OUT" "  0:00.0   2.5s  HOOK    I lost \$18,000 on one missing contract." "beats: beat 1 (7 words at 2.75 words/s)"
has "$OUT" "  0:02.5   1.4s          Here is what happened." "beats: beat 2"
has "$OUT" "  0:07.3   8.7s          No signature meant no leverage," "beats: one long sentence stays one beat"
has "$OUT" "                         ^ 8.7s on one beat" "beats: long beat flag"
has "$OUT" "  0:16.0   2.5s          This is the clause I use now." "beats: a paragraph split at sentence ends"
has "$OUT" "  0:18.6   8.7s  MID     It names the payment date," "beats: MID at half time"
has "$OUT" "  0:27.3   0.7s          Copy it." "beats: short sentence"
has "$OUT" "  0:35.3   3.3s  CTA     Save this before your next contract costs you \$18,000." "beats: CTA"
has "$OUT" "  - Beat(s) 5, 7, 9 run past 4s. Either split the line or change what is on screen inside it. A static frame is where people leave." "beats: long beats note"
has "$OUT" "  - Beats 2-4 have nothing concrete in them. Put a number, a name or a price in one of them." "beats: abstract run (a capital at line start does not count)"
has "$OUT" '  - Loops: the last beat repeats "$18,000, contract" from the hook. Second watches are free reach.' "beats: loop words, thousands put back"
has "$OUT" "  - 8.5s over target. Cut about 23 words." "beats: over target"
eq "$(grep -c 'Beats .* have nothing concrete' <<<"$OUT")" "1" "beats: one note per abstract run"
run $BE script.txt --json
eq "$(j '[.wpm,.target,.total_seconds,.total_words,(.beats|length)]')" '[165,null,38.55,106,10]' "beats json: totals"
eq "$(j '.beats[0]')" '{"n":1,"start":0.0,"dur":2.55,"words":7,"text":"I lost $18,000 on one missing contract.","concrete":2,"label":"HOOK","flags":[]}' "beats json: a row (start stays a float)"
eq "$(j '[.beats[].label]')" '["HOOK","","","","","","MID","","","CTA"]' "beats json: labels"
run $BE script.txt --target 40
has "$OUT" "  - Length is on target (38.5s against 40.0s)." "beats: on target (within 10%)"
run $BE script.txt --wpm 185 --target 45
has "$OUT" "at 185.0 wpm  ·  target 45.0s" "beats: --wpm given is a float"
run $BE script.txt --target 0
hasnt "$OUT" "target" "beats: --target 0 is no target"
run $BE abstract.txt
has "$OUT" "  - Beat 1 has no number and no name in it." "beats: hook with nothing concrete"
has "$OUT" "  - Beats 1-3 have nothing concrete in them." "beats: run from beat 1"
has "$OUT" "  - No loop. The last beat shares no word with the hook, so the video ends flat." "beats: no loop"
run $BE longhook.txt
has "$OUT" "HOOK/CTA" "beats: one beat is hook and CTA"
has "$OUT" "^ hook runs 5.5s, past the 3s mark" "beats: hook too long (15 words)"
has "$OUT" "  - Beat 1 takes 5.5s to say. Cut it to 8 words or fewer, or the hook lands after the decision has been made." "beats: cut-to words int(3 x 2.75)"
has "$OUT" "  - Beat(s) 1 run past 4s." "beats: long hook is a long beat too"
has "$OUT" '  - Loops: the last beat repeats "building, buy, dollars" from the hook.' "beats: a single beat loops on itself (sorted, first three)"
run $BE blank.txt
eq "$CODE|$ERR|$OUT" "2|empty script|" "beats: empty script"
run $BE script.txt --wpm 0
eq "$CODE|$ERR" "1|ig-reel/beats: ZeroDivisionError: float division by zero" "beats: --wpm 0 divides by zero, as Python does"
run $BE script.txt --wpm=-60
has "$OUT" "  -1:53.0  -4.0s  HOOK    Here is what happened." "beats: negative rate"
has "$OUT" "  -1:53.0" "beats: float divmod floors (-7 -> -1:53.0)"
run $BE - < script.txt
eq "$CODE" "0" "beats: stdin"

# ================================================================ ig-reel/hookscore
run $HS hooks.txt
eq "$CODE" "0" "hookscore: best >= 70 exits 0"
has "$OUT" "HOOK RANKING
==============================================================================" "hookscore: ranking header"
has "$OUT" "->  87.0 STRONG  nobody tells you your first 30 reels flop
        weakest: SPECIFICITY (75)" "hookscore: top"
has "$OUT" "    81.4 STRONG  I lost \$18,000 on one missing contract.
        weakest: ADDRESS (70)" "hookscore: second"
has "$OUT" "    46.5 WEAK    Three clients. Zero contracts. One expensive lesson that I ...
        weakest: LENGTH (11)" "hookscore: long hook clipped at 59 + ..."
has "$OUT" "     9.6 WEAK    Hey guys, welcome back to my channel
        weakest: FRONTLOAD (0)
        dealbreaker: Greeting. Nobody came to the feed to be greeted." "hookscore: greeting"
has "$OUT" "     0.0 WEAK    In this video" "hookscore: clamped at 0"
has "$OUT" "        dealbreaker: Emoji in the hook. On-screen text at hook size has room for words or for an emoji, not both." "hookscore: emoji dealbreaker"
has "$OUT" "Shoot the top one. If the top one is under 50, none of these are the hook." "hookscore: footer"
hasnt "$OUT" "FORMULA" "hookscore: no formula line (that is the master tool)"
run $HS hooks.txt --json
eq "$(j 'length')" "6" "hookscore json: a list for several hooks, in input order"
eq "$(j '.[0] | keys_unsorted')" '["hook","checks","weakest","flags","score","verdict"]' "hookscore json: keys"
eq "$(j '.[3].checks.SPECIFICITY.score')" "75.0" "hookscore json: scores are floats"
eq "$(j '[.[].score]')" '[81.4,9.6,54.6,87.0,0.0,46.5]' "hookscore json: scores"
eq "$(j '[.[].checks | keys_unsorted][0]')" '["LENGTH","SPECIFICITY","STAKES","FRONTLOAD","ADDRESS"]' "hookscore json: check order"
run $HS --hook "Stop sending proposals without this clause"
eq "$CODE" "1" "hookscore: single below 70 exits 1"
has "$OUT" "  \"Stop sending proposals without this clause\"" "hookscore: single quoted"
has "$OUT" "  HOOK SCORE    #############...........  54.6   OK" "hookscore: overall bar"
has "$OUT" "  Weakest property: SPECIFICITY. Fix that one and re-run." "hookscore: weakest"
run $HS --hook "  hey guys 🚀 #fyp  "
has "$OUT" '  "hey guys 🚀 #fyp"' "hookscore: hook stripped for display"
has "$OUT" "  LENGTH        ##############..........  60.0
                3 words, 15 chars, ~1.1s spoken (want 5-12 words)" "hookscore: length check"
eq "$(grep -c DEALBREAKER <<<"$OUT")" "3" "hookscore: three dealbreakers"
run $HS --hook "  hey guys 🚀 #fyp  " --json
eq "$CODE" "0" "hookscore: --json always exits 0"
eq "$(j '[.hook,.score,.verdict]')" '["  hey guys 🚀 #fyp  ",0.0,"WEAK"]' "hookscore json: one object, the hook as given"
run $HS --hook "I lost \$18,000 on one missing contract."
eq "$CODE" "0" "hookscore: single STRONG exits 0"
hasnt "$OUT" "Weakest property" "hookscore: no weakest line when STRONG"
run $HS --hook "" < hooks.txt
eq "$CODE" "0" "hookscore: an empty --hook falls back to the input (stdin)"
has "$OUT" "HOOK RANKING" "hookscore: ranked from stdin"
run $HS blank.txt
eq "$CODE|$ERR" "2|nothing to score" "hookscore: nothing to score"

# ================================================================ ig-viral/swipe
sw cap.tsv
eq "$CODE" "0" "swipe: exit 0"
has "$OUT" "SWIPE FILE  ·  6 reels  ·  5 accounts  ·  baseline: account median" "swipe: header"
has "$OUT" "   100.0x  hook  55  #2  Negative Command       @c                 400,000
           \"Stop sending proposals without this clause\"" "swipe: top row"
has "$OUT" "    37.5x  hook  87  #3  Nobody Tells You       @a                 412,000" "swipe: 412000 / 11000 median"
has "$OUT" "     4.0x  hook  28  -   unclassified           @g                   1,200" "swipe: Arabic-Indic digits are digits"
has "$OUT" "     0.2x  hook   3  -   unclassified           @b                 400,000" "swipe: commas stripped from counts"
has "$OUT" "        ?  hook  81  #1  Cost Confession        @d                   5,000" "swipe: no baseline"
has "$OUT" "  top third by outlier:  Negative Command x1, Nobody Tells You x1" "swipe: top formulas"
has "$OUT" "  median hook score:     top 71  vs bottom 42" "swipe: hook score medians"
has "$OUT" "  median hook length:    top 6.5 words  vs bottom 6.0 words" "swipe: even medians are floats"
has "$OUT" "  unclassified:          2 of 6." "swipe: unclassified"
has "$OUT" "  forty across six accounts shows you something. Collect more before you believe it." "swipe: footer"
hasnt "$OUT" "zero views" "swipe: zero views dropped"
eq "$ERR" "" "swipe: no notes"
sw cap.tsv --json
eq "$(j '[.baseline,.n,.accounts,.unclassified,.top_hook_score,.bottom_hook_score,.top_words,.bottom_words]')" '["account median",6,5,2,70.8,42.2,6.5,6.0]' "swipe json: summary"
eq "$(j '.top_formulas')" '[["Negative Command",1],["Nobody Tells You",1]]' "swipe json: top formulas as pairs"
eq "$(j '[.reels[] | .outlier]')" '[100.0,37.45,4.0,0.82,0.2,null]' "swipe json: outliers (2 dp)"
eq "$(j '.reels[0] | [.followers,.median,.baseline,.formula_id,.words,.hook_score,.hook_verdict]')" '[4000,null,4000,2,6,54.6,"OK"]' "swipe json: a reel"
eq "$(j '.reels[2] | [.followers,.views]')" '[300,1200]' "swipe json: Unicode digits"
eq "$(j '.reels[0] | keys_unsorted')" '["account","followers","median","views","hook","baseline","outlier","formula_id","formula","words","hook_score","hook_verdict"]' "swipe json: header column order kept"
sw nohead.tsv --json
eq "$(j '.reels[0] | keys_unsorted')" '["account","followers","views","hook","median","baseline","outlier","formula_id","formula","words","hook_score","hook_verdict"]' "swipe: no header -> account followers views hook"
eq "$(j '[.baseline,.n,.top_words,.bottom_words]')" '["follower count",2,5,6]' "swipe: odd medians keep the int"
sw nohead.tsv
has "$OUT" "  median hook length:    top 5 words  vs bottom 6 words" "swipe: int medians print without .0"
sw cap.tsv --hooks "$TMP/none.json"
eq "$ERR" "note: hooks.json not found, formulas not named. Pass --hooks." "swipe: no hooks file"
has "$OUT" "  top third by outlier:  unclassified x2" "swipe: everything unclassified"
printf '{"hooks": [' > broken.json
sw cap.tsv --hooks broken.json
eq "$CODE|$ERR" "0|note: hooks.json not found, formulas not named. Pass --hooks." "swipe: broken hooks file is a note"
printf '{"version": 1}' > nohooks.json
sw cap.tsv --hooks nohooks.json
eq "$CODE|$ERR" "1|ig-viral/swipe: KeyError: 'hooks'" "swipe: a hooks file without hooks fails as Python does"
printf '{"hooks": [{"id": 7, "name": "Mine", "match": "clause"}]}' > mine.json
sw cap.tsv --hooks mine.json --json
eq "$(j '[.reels[] | .formula_id]')" '[7,null,null,null,null,null]' "swipe: custom hooks, sorted ids when there is no classify_order"
mkdir -p empty/ig-viral
run --skill-dir "$TMP/empty/ig-viral" $SW cap.tsv
eq "$CODE|$ERR" "0|note: hooks.json not found, formulas not named. Pass --hooks." "swipe: copied on its own"
sw cap.tsv --out "$TMP/o/deep/swipe.md"
eq "$CODE|$ERR" "0|wrote $TMP/o/deep/swipe.md" "swipe: --out makes the folders"
MD=$(cat "$TMP/o/deep/swipe.md")
has "$MD" "# Swipe file

6 reels across 5 accounts. Ranked by multiple over account median.

## 100.0x  Negative Command  (@c)
- views: 400,000  baseline: 4,000
- hook score: 54.6  words: 6
- hook: \"Stop sending proposals without this clause\"
" "swipe: markdown"
has "$MD" "## ?  Cost Confession  (@d)
- views: 5,000  baseline: 0
- hook score: 81.4  words: 8" "swipe: markdown without a baseline"
eq "$(tail -c 2 "$TMP/o/deep/swipe.md" | od -An -c | tr -d ' ')" '\n\n' "swipe: markdown ends with a blank line"
sw cap.tsv --out swipe.md
eq "$CODE|$ERR" "1|ig-viral/swipe: OSError: [Errno 2] No such file or directory: ''" "swipe: a bare file name fails in os.makedirs, as Python does"
has "$OUT" "SWIPE FILE" "swipe: the report was printed first"
[ -e swipe.md ] && bad "swipe: nothing written" || ok
HOME="$TMP/h" sw cap.tsv --out "~/sw/s.md"
eq "$CODE|$ERR" "0|wrote $TMP/h/sw/s.md" "swipe: --out expands ~"
sw blank.txt
eq "$CODE|$ERR" "2|no usable rows. Need a tab-separated file with at least views and hook." "swipe: no rows"
sw - < cap.tsv
has "$OUT" "6 reels" "swipe: stdin"

# ================================================================ ig-human/humanize
hu() { run --skill-dir "$PK/ig-human" ig-human/humanize "$@"; }
printf "Let's face it, pricing is hard. Here's the thing: most people guess.\nIt's no secret that we delve into numbers — and that matters — also, it's proof.\n  Let that sink in. Wait... really?! Let that sink in.\n\nWe've all been there. we guess. Picture this — a client says yes.\nVisit https://example.com/Let's-face-it now.\n" > h1.txt
printf "let's face it, i write in lower case. here's the thing: it stays that way.\nlet that sink in. we delve.\n" > h2.txt
printf "Systems fail. Let that sink in. . And then\n, it's no secret that it works!.\n" > h3.txt
hu h1.txt
eq "$OUT" "Pricing is hard.: most people guess.
We look at numbers, and that matters. Also, it's proof.
Wait... Really?!

We guess., a client says yes.
Visit https://example.com/Let's-face-it now." "humanize: capitals restored, orphaned punctuation and a deletion's leading space removed, URL untouched"
hu h2.txt
eq "$OUT" "i write in lower case.: it stays that way.
we look." "humanize: a lower-case voice stays lower case"
hu h3.txt
eq "$OUT" "Systems fail... And then
It works!" "humanize: line opening on a comma, '!.' and stray dots cleaned"
run --skill-dir "$PK/../../astro-linkedin/skills/li-human" li-human/humanize h3.txt
eq "$OUT" "Systems fail... And then
it works!." "humanize: li-human keeps its own (lighter) clean-up"
hu h1.txt --json
eq "$(j '.report.lexical | length > 0')" "true" "humanize json: lexical report"
eq "$(j '.text | endswith("now.\n")')" "true" "humanize json: text ends with one newline"
hu h1.txt --report
has "$ERR" "HUMANIZE REPORT" "humanize: report on stderr"
hu - < h2.txt
eq "$OUT" "i write in lower case.: it stays that way.
we look." "humanize: stdin"

done_ "instagram"

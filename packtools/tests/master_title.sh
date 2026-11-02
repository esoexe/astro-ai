#!/usr/bin/env bash
# master-virality/title: the case of master-skills/skills/virality/tools/test_virality.py (test_title_pairing) plus
# hand-scored titles, run against the C++ tool. score = max(0, min(100, 100 - 14 * issues + 4 * good)).
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
TI=(--skill-dir "$SK" master-virality/title)
j() { echo "$OUT" | jq -c "$1"; }

# ---- upstream: the cover repeats the title
run "${TI[@]}" --title "I feed four people on \$62 a week" --cover '$62 A WEEK' --json
eq "$CODE" 0 "exit 0"
# 1 issue (duplicate on 62, week), 2 good (length, figure) -> 94
eq "$(j '.')" '[{"title":"I feed four people on $62 a week","chars":32,"score":94,"issues":[["duplicate","thumbnail repeats the title on 62, week - the thumbnail should say what the title does not"]],"good":["32 characters, inside the 60-character desktop cut","carries a concrete figure (62)"]}]' "the pairing result"
has "$OUT" $'[\n {\n  "title"' "indent=1"
run "${TI[@]}" --title "I feed four people on \$62 a week" --thumb 'COOK ONCE' --json
eq "$(j '.[0].score')" 100 "different words: 0 issues, 3 good -> capped at 100"
eq "$(j '.[0].good[2]')" '"thumbnail and title carry different words"' "different words is good"

# ---- a file, ranked
printf 'The ULTIMATE GUIDE TO EVERYTHING you need for amazing results in 2026 and beyond, honestly\nhow to cook?\n\n   \n' > "$TMP/t.txt"
run "${TI[@]}" "$TMP/t.txt" --thumb "COOK THIS NOW PLEASE OK"
eq "$CODE" 0 "file exit 0"
# how to cook?: no-number, duplicate (cook), thumb-length (5 words); good: length, question -> 100 - 42 + 8 = 66
# the long one: length (90 > 60), mobile, shouting (ULTIMATE GUIDE EVERYTHING), vague, thumb-length; good: 2026, different -> 38
eq "$OUT" "$(cat <<'EOF'

  "how to cook?"
  12 chars   score 66/100
    x  no-number    no number, date or name - the most reliable single fix
    x  duplicate    thumbnail repeats the title on cook - the thumbnail should say what the title does not
    x  thumb-length 5 words on the thumbnail - three is the ceiling at feed size
    ok              12 characters, inside the 60-character desktop cut
    ok              open question in the title

  "The ULTIMATE GUIDE TO EVERYTHING you need for amazing results in 2026 and beyond, honestly"
  90 chars   score 38/100
    x  length       90 characters - desktop search cuts near 60
    x  mobile       a mobile feed shows about "The ULTIMATE GUIDE TO EVERYTHING you..." - check the subject survives
    x  shouting     3 all-caps words - two is the ceiling before it reads as spam
    x  vague        amazing, everything, ultimate - swap for a number, a name or a date
    x  thumb-length 5 words on the thumbnail - three is the ceiling at feed size
    ok              carries a concrete figure (2026)
    ok              thumbnail and title carry different words
EOF
)" "ranked file report"

# ---- single checks
A101=$(printf 'a%.0s' $(seq 101)); A40=$(printf 'a%.0s' $(seq 40))
run "${TI[@]}" --title "$A101" --json
eq "$(j '[.[0].chars, .[0].score, [.[0].issues[][0]]]')" '[101,58,["length","mobile","no-number"]]' "over the hard limit: 3 issues -> 58"
eq "$(j '.[0].issues[0][1]')" "\"101 characters - YouTube's hard limit is 100\"" "hard limit message"
eq "$(j '.[0].issues[1][1]')" "\"a mobile feed shows about \\\"$A40...\\\" - check the subject survives\"" "no space: the whole 40-character head"
run "${TI[@]}" --title "I tried the NEW iPhone 15 for a week" --json
eq "$(j '[.[0].chars, .[0].score, .[0].good]')" '[36,100,["36 characters, inside the 60-character desktop cut","1 all-caps word for emphasis","carries a concrete figure (15)"]]' "one caps word is emphasis"
run "${TI[@]}" --title "This is how I cook" --json
eq "$(j '[.[0].score, [.[0].issues[][0]]]')" '[76,["no-number","front-load"]]' "first three words all filler -> 76"
run "${TI[@]}" --title "ÉTÉ ÀLA ÜBER plans for 3 kids" --json
eq "$(j '[.[0].issues[][0]]')" '["shouting"]' "isupper is Unicode-aware"
run "${TI[@]}" --title "  Is 1,250.5% real?  " --json
eq "$(j '[.[0].title, .[0].chars]')" '["Is 1,250.5% real?",17]' "stripped before counting"
eq "$(j '.[0].good')" '["17 characters, inside the 60-character desktop cut","carries a concrete figure (1,250.5%)","open question in the title"]' "figure keeps commas, dot and %"
run "${TI[@]}" --title "Cook 5 meals" --thumb "" --json
eq "$(j '.[0].good | length')" 2 "an empty thumbnail is no thumbnail"

# ---- argument quirks of the original
run "${TI[@]}" --json --title "Cook 5 meals"
eq "$CODE" 0 "--json anywhere"
run "${TI[@]}" --title
eq "$CODE" 1 "--title with no value: IndexError, exit 1"
run "${TI[@]}" --title x --thumb
eq "$CODE" 1 "--thumb with no value: IndexError, exit 1"
run "${TI[@]}" "$TMP/nope.txt"
eq "$CODE" 1 "a missing file prints the doc and exits 1"
has "$OUT" "title - lint a YouTube title and thumbnail pairing" "doc on bad input"
run "${TI[@]}"
eq "$CODE" 1 "no arguments: exit 1"
run "${TI[@]}" --help
eq "$CODE" 0 "--help exits 0"
has "$OUT" "    astro-server tool master-virality/title titles.txt            # one per line, ranked" "help names the Astro command"
hasnt "$OUT" "python" "no python in the help"
done_ "master-virality/title"

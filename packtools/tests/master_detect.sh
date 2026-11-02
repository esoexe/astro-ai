#!/usr/bin/env bash
# master-virality/detect: the case of master-skills/skills/virality/tools/test_virality.py (test_detect_panel) plus
# hand-scored drafts, run against the C++ tool with the converted pack's tools/slop.json. Each score is worked by hand in
# the comments (scale(v, human, machine) = clamp((v - machine) / (human - machine) * 100)).
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
DE=(--skill-dir "$SK" master-virality/detect)
j() { echo "$OUT" | jq -c "$1"; }
jr() { echo "$OUT" | jq -r "$1"; }

printf "I sent 400 cold DMs in March. Eleven replied. Two became clients, and one of them paid \$8,000 for a site I built in nine days. I'm not doing that again without a contract. Honestly? Worth it, but barely.\n" > "$TMP/h.txt"
run "${DE[@]}" "$TMP/h.txt" --json
eq "$CODE" 0 "--json always exits 0"
eq "$(j 'keys_unsorted')" '["source","checks","human_score","verdict"]' "keys"
eq "$(j '.checks | keys_unsorted')" '["BURSTINESS","SPECIFICITY","SLOP DENSITY","FINGERPRINT","VOICE"]' "the five checks, in order"
# sentences over 2 words: 7, 17, 8, 4 -> mean 9, pstdev sqrt(94 / 4) = 4.848 -> cv 0.539 -> (0.539 - 0.22) / 0.48 = 66.4
eq "$(jr '.checks.BURSTINESS.detail')" "variation 0.54 across 4 sentences (want 0.55+)" "burstiness detail"
eq "$(j '.checks.BURSTINESS.score')" 66.4 "burstiness score"
# 37 words; 400, \$8, 000 + March (Eleven, Two follow a full stop) -> 4 * 100 / 37 = 10.8
eq "$(jr '.checks.SPECIFICITY.detail')" "4 concrete markers, 10.8 per 100 words (want 4+)" "specificity detail"
eq "$(jr '.checks["SLOP DENSITY"].detail')" "0 stock terms, 0.0 per 100 words" "no slop"
eq "$(jr '.checks.FINGERPRINT.detail')" "0 invisible, 0 em dash, 0 curly quote, 0 ellipsis, 0 hard space" "no fingerprint"
# I'm -> 2.7 contractions (90.1 * .35); I, I, I('m) -> 8.1 pronouns (100 * .35); no tells (100 * .3) -> 96.5
eq "$(jr '.checks.VOICE.detail')" "2.7 contractions, 8.1 personal pronouns per 100 words, 0 structural tell(s)" "voice detail"
eq "$(j '[.checks[].score]')" "[66.4,100.0,100.0,100.0,96.5]" "scores"
# mean 92.58 * 0.6 + 66.38 * 0.4 = 82.1
eq "$(j '[.human_score,.verdict,.source]')" "[82.1,\"PASS\",\"$TMP/h.txt\"]" "overall"
run "${DE[@]}" "$TMP/h.txt"
eq "$CODE" 0 "PASS exits 0"

# ---- before / after
printf 'In this video we delve into a robust \xe2\x80\x94 seamless workflow.\xe2\x80\x8b Let that sink in.\n' > "$TMP/d.txt"
run "${DE[@]}" "$TMP/h.txt" "$TMP/d.txt"
eq "$CODE" 1 "the second draft decides: FLAGGED exits 1"
# 14 words; delve, delve into, in this video, let that sink in, robust, seamless -> 6 * 100 / 14 = 42.9 -> 0
# mean(50, 50, 0, 0, 50) = 30 * 0.6 + 0 = 18.0
eq "$OUT" "$(cat <<'EOF'

AI DETECTION PANEL  -  h.txt
==============================================================
  BURSTINESS    ################........  66.4
                variation 0.54 across 4 sentences (want 0.55+)
  SPECIFICITY   ######################## 100.0
                4 concrete markers, 10.8 per 100 words (want 4+)
  SLOP DENSITY  ######################## 100.0
                0 stock terms, 0.0 per 100 words
  FINGERPRINT   ######################## 100.0
                0 invisible, 0 em dash, 0 curly quote, 0 ellipsis, 0 hard space
  VOICE         #######################.  96.5
                2.7 contractions, 8.1 personal pronouns per 100 words, 0 structural tell(s)
--------------------------------------------------------------
  HUMAN SCORE   ####################....  82.1   PASS


AI DETECTION PANEL  -  d.txt
==============================================================
  BURSTINESS    ############............  50.0
                too short to judge
  SPECIFICITY   ############............  50.0
                too short to judge
  SLOP DENSITY  ........................   0.0
                6 stock terms, 42.9 per 100 words (delve, delve into, in this video, let that sink in, ...)
  FINGERPRINT   ........................   0.0
                1 invisible, 1 em dash, 0 curly quote, 0 ellipsis, 0 hard space
  VOICE         ############............  50.0
                too short to judge
--------------------------------------------------------------
  HUMAN SCORE   ####....................  18.0   FLAGGED

  Weakest signal: SLOP DENSITY. Fix that first.

  82.1 PASS  ->  18.0 FLAGGED   (-64.1)
EOF
)" "the before/after report"
run "${DE[@]}" "$TMP/h.txt" "$TMP/d.txt" --json
eq "$(j '[.[].human_score]')" "[82.1,18.0]" "compare JSON is a list"

# ---- voice: a structural tell and uniform bullets
printf -- "- we ship code\n- we test code\n- we fix bugs\nIt's not just a tool, it's a habit we keep. Honestly we like it and you will too because it saves us time every day.\n" > "$TMP/v.txt"
run "${DE[@]}" "$TMP/v.txt" --json
# 34 words: It's, it's -> 5.9 (100 * .35); we x5, you, us -> 20.6 (100 * .35); not-just -> 78 * .3; bullets 3,3,3 -> -12 = 81.4
eq "$(jr '.checks.VOICE.detail')" "5.9 contractions, 20.6 personal pronouns per 100 words, 1 structural tell(s) [not-just, uniform-bullets]" "voice detail"
eq "$(j '.checks.VOICE.score')" 81.4 "voice score"
# sentences 4 4 4 10 15 -> mean 7.4, pstdev sqrt(496) / 5 = 4.454 -> cv 0.602 -> 79.6
eq "$(jr '.checks.BURSTINESS.detail')" "variation 0.60 across 5 sentences (want 0.55+)" "bullets count as sentences"
eq "$(j '.checks.BURSTINESS.score')" 79.6 "burstiness score"
eq "$(jr '.checks.SPECIFICITY.detail')" "0 concrete markers, 0.0 per 100 words (want 4+)" "Honestly after a full stop is not a name"

# ---- fingerprint
printf '\xe2\x80\x9cHi\xe2\x80\x9d\xe2\x80\xa6 it\xe2\x80\x99s\xc2\xa0fine \xe2\x80\x94 ok\xe2\x80\x8d' > "$TMP/f.txt"
run "${DE[@]}" "$TMP/f.txt" --json
eq "$(jr '.checks.FINGERPRINT.detail')" "1 invisible, 1 em dash, 3 curly quote, 1 ellipsis, 1 hard space" "fingerprint counts"
eq "$(j '.checks.FINGERPRINT.score')" 0.0 "11 * 1000 / 21 per thousand is far past 12"

# ---- edges
: > "$TMP/empty.txt"
run "${DE[@]}" "$TMP/empty.txt" --json
# 50, 50, 50 (empty), 100 (nothing typed), 50 -> mean 60 * 0.6 + 50 * 0.4 = 56.0
eq "$(j '[.human_score,.verdict]')" '[56.0,"REVIEW"]' "an empty draft"
run "${DE[@]}" "$TMP/empty.txt"
eq "$CODE" 1 "REVIEW exits 1"
OUT=$(printf 'Short one.' | $T "${DE[@]}" --json); eq "$(j '.source')" '"-"' "stdin is the default input"
echo '{"words":[{"find":"synergy"}],"phrases":[],"structures":[{"id":"broken","regex":"("},{"id":"ok","regex":"(?i)synergy"}]}' > "$TMP/lex.json"
printf 'Synergy matters. synergy wins.\n' > "$TMP/s.txt"
run "${DE[@]}" "$TMP/s.txt" --json --lexicon "$TMP/lex.json"
eq "$(jr '.checks["SLOP DENSITY"].detail')" "2 stock terms, 50.0 per 100 words (synergy)" "a custom lexicon"
run "${DE[@]}" "$TMP/missing.txt"
eq "$CODE" 1 "missing file: exit 1"
run "${DE[@]}" -h
has "$OUT" "usage: detect [-h] [--json] [--lexicon LEXICON] [input] [compare]" "usage"
eq "$(echo "$OUT" | sed -n '/^options:/,$p')" "$(printf 'options:\n  -h, --help         show this help message and exit\n  --json\n  --lexicon LEXICON')" "options without help print bare"
done_ "master-virality/detect"

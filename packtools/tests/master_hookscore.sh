#!/usr/bin/env bash
# master-virality/hookscore: the cases of master-skills/skills/virality/tools/test_virality.py (TestHookscore) plus
# hand-scored hooks, run against the C++ tool. The library is the converted pack's tools/hook-formulas.json.
# Panel arithmetic for each hand-scored hook is in the comments: overall = mean(5 scores) * 0.6 + min * 0.4 - 15 * flags.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
LIB="$SK/tools/hook-formulas.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
H=(--skill-dir "$SK" master-virality/hookscore)
j() { echo "$OUT" | jq -c "$1"; }
jr() { echo "$OUT" | jq -r "$1"; }

# ---- the library classifies its own examples (upstream test_library_self_classifies)
eq "$(jq '.hooks | length' "$LIB")" 26 "26 formulas"
eq "$(jq '[.hooks[] | select((.on_screen | split(" ") | length) > 6)] | length' "$LIB")" 0 "on-screen text is 6 words or fewer"
miss=0
while IFS=$'\t' read -r id ex wr; do
  for t in "$ex" "$wr"; do
    got=$($T "${H[@]}" --hook "$t" --json | jq -r .formula.id)
    [ "$got" = "$id" ] || { miss=$((miss+1)); echo "    $id: <$t> -> $got"; }
  done
done < <(jq -r '.hooks[] | [.id, .example, .written] | @tsv' "$LIB")
eq "$miss" 0 "every example and written hook classifies as its own formula"

# ---- panel, payoff, dealbreakers (upstream test_panel_payoff_and_dealbreakers)
# 10 words / 51 chars -> 100; $400 -> 1 marker -> 75; saved + a price -> 100; payload word 2 -> 100; first person -> 70
# mean 89 * 0.6 + 70 * 0.4 = 81.4 STRONG
run "${H[@]}" --hook "I saved \$400 on flights last month with one prompt."
eq "$CODE" 0 "a STRONG hook exits 0"
eq "$OUT" "$(cat <<'EOF'

HOOK SCORE
==============================================================
  "I saved $400 on flights last month with one prompt."

  LENGTH        ######################## 100.0
                10 words, 51 chars, ~3.6s spoken (want 5-12 words)
  SPECIFICITY   ##################......  75.0
                1 concrete marker(s): $400
  STAKES        ######################## 100.0
                2 tension marker(s): saved, a price
  FRONTLOAD     ######################## 100.0
                payload at word 2
  ADDRESS       #################.......  70.0
                first person, no viewer named
--------------------------------------------------------------
  HOOK SCORE    ####################....  81.4   STRONG
  FORMULA       The Payoff  (1 pattern matched)
  PAYOFF        LEADS    money ($400) at word 3
EOF
)" "the full single-hook report"
run "${H[@]}" --hook "I saved \$400 on flights last month with one prompt." --json
eq "$(j 'keys_unsorted')" '["hook","checks","weakest","flags","score","verdict","formula","payoff"]' "analysis keys"
eq "$(j '.checks | keys_unsorted')" '["LENGTH","SPECIFICITY","STAKES","FRONTLOAD","ADDRESS"]' "check order"
eq "$(j '[.checks[].score]')" "[100.0,75.0,100.0,100.0,70.0]" "check scores"
eq "$(j '[.score,.verdict,.weakest]')" '[81.4,"STRONG","ADDRESS"]' "score, verdict, weakest"
eq "$(j '.formula')" '{"id":"the-payoff","name":"The Payoff","matched":1,"payoff_first":true}' "formula"
eq "$(j '.payoff')" '{"kind":"money","token":"$400","word":3,"status":"LEADS"}' "payoff"
has "$OUT" '  "hook": "I saved' "indent=2"
# 9 words -> 100; nothing concrete -> 15; no stakes -> 20; weak opener "hey", no payload -> 30 - 30 = 0; "i" -> 70
# mean 41 * 0.6 + 0 - 15 (greeting) = 9.6 WEAK
run "${H[@]}" --hook "Hey guys, today I want to talk about productivity." --json
eq "$CODE" 1 "a WEAK hook exits 1"
eq "$(j '[.score,.verdict,.weakest,.payoff.status]')" '[9.6,"WEAK","FRONTLOAD","MISSING"]' "the greeting hook"
eq "$(j '.flags')" '["Greeting. Nobody came to the feed to be greeted."]' "greeting dealbreaker"
eq "$(jr '.checks.FRONTLOAD.detail')" 'no payload word anywhere in the line; weak opener "hey"' "frontload detail"
eq "$(j '.payoff')" '{"kind":null,"token":null,"word":null,"status":"MISSING"}' "missing payoff"
run "${H[@]}" --hook "Here is something I learned the hard way after spending \$9,000" --json
eq "$(j '.payoff')" '{"kind":"money","token":"$9,000","word":11,"status":"LATE"}' "late payoff"
# 6 words -> 100; $10 (Google at position 0 is not a proper noun) -> 75; a price -> 70; $10 at word 4 -> 100; you -> 100
# mean 89 * 0.6 + 70 * 0.4 = 81.4
run "${H[@]}" --hook "Google pays you \$10 to switch" --json
eq "$(j '[.checks[].score]')" "[100.0,75.0,70.0,100.0,100.0]" "Google hook scores"
eq "$(jr '.checks.SPECIFICITY.detail')" '1 concrete marker(s): $10' "a capital at the start is not a name"
eq "$(jr '.checks.STAKES.detail')" '1 tension marker(s): a price' "a price is a stake"
eq "$(jr '.checks.LENGTH.detail')" '6 words, 29 chars, ~2.2s spoken (want 5-12 words)' "length detail"
eq "$(j '[.score,.verdict]')" '[81.4,"STRONG"]' "Google hook verdict"
# 6 words -> 100; "30 days" + Claude -> 2 markers -> 100; no stakes -> 20; payload "30" at word 5 -> 70; "i" -> 70
# (PROPER_RE.match on a single word never fires, so Claude is not the payload)  mean 72 * 0.6 + 20 * 0.4 = 51.2 OK
run "${H[@]}" --hook "I tested Claude for 30 days" --json
eq "$(j '[.checks[].score]')" "[100.0,100.0,20.0,70.0,70.0]" "Claude hook scores"
eq "$(jr '.checks.SPECIFICITY.detail')" '2 concrete marker(s): 30 days, Claude' "number with unit, then the name"
eq "$(jr '.checks.FRONTLOAD.detail')" 'payload at word 5, could move forward' "payload at 5"
eq "$(jr '.checks.STAKES.detail')" '0 tension marker(s) - nothing is at stake in this line' "no stakes"
eq "$(j '[.score,.verdict,.weakest]')" '[51.2,"OK","STAKES"]' "OK verdict"
eq "$(j '.payoff')" '{"kind":"number","token":"30","word":5,"status":"LEADS"}' "a bare number payoff"
# 6 words; no markers -> 15; no stakes -> 20; "so" opener and no payload -> 0; third person -> 35: mean 34 * 0.6 = 20.4
run "${H[@]}" --hook "So um this is a thing" --json
eq "$CODE" 1 "upstream: So um... exits 1"
eq "$(j '[.score,.verdict]')" '[20.4,"WEAK"]' "So um... is WEAK"
eq "$(jr '.checks.ADDRESS.detail')" 'third person, nobody in the room' "third person"
run "${H[@]}" --hook "I made \$500 today 🔥 #money" --json
eq "$(j '.flags')" '["Hashtag in the hook. Hashtags belong at the bottom of the caption, if anywhere.","Emoji in the hook. On-screen text at hook size has room for words or for an emoji, not both."]' "hashtag and emoji, in panel order"
run "${H[@]}" --hook "Stop scrolling. In this video I'll show you how to save \$50" --json
eq "$(j '.flags | length')" 2 "stop scrolling + preamble"
has "$(j '.flags[0]')" 'Opens with \"stop scrolling\"' "stop scrolling first"
has "$(j '.flags[1]')" 'Video preamble.' "preamble second"
run "${H[@]}" --hook "Don’t buy this \$40 tool" --json
has "$OUT" '"hook": "Don’t buy this $40 tool"' "ensure_ascii=False keeps the curly apostrophe"
eq "$(jr '.checks.STAKES.detail')" "1 tension marker(s): a price" "don’t (curly, inside the word) is not the STAKES word don't"

# ---- CLI (upstream test_cli)
printf 'Hey guys, welcome back.\n$18,000 is what one missing clause cost me.\n' > "$TMP/h.txt"
run "${H[@]}" "$TMP/h.txt"
eq "$CODE" 0 "a file with one STRONG hook exits 0"
# $18,000 hook: 8 words -> 100; 75; cost + a price -> 100; payload word 1 -> 100; "me" -> 70 -> 81.4
# greeting: 4 words -> 80; 15; 20; 0; 35 -> mean 30 * 0.6 - 15 = 3.0
eq "$OUT" "$(cat <<'EOF'

HOOK RANKING
==============================================================================
->  81.4 STRONG  $18,000 is what one missing clause cost me.
        The Cost · payoff LEADS · weakest ADDRESS (70)
     3.0 WEAK    Hey guys, welcome back.
        Unclassified · payoff MISSING · weakest FRONTLOAD (0)
        dealbreaker: Greeting. Nobody came to the feed to be greeted.

Shoot the top one. If the top one is under 50, none of these is the hook yet.
EOF
)" "ranking table"
run "${H[@]}" "$TMP/h.txt" --json
eq "$(j 'type')" '"array"' "several hooks -> a JSON list, in input order"
eq "$(j '[.[].score]')" "[3.0,81.4]" "input order kept in JSON"
printf '# a comment\n\n   \n  I lost $18,000 on one missing contract.  \n' > "$TMP/one.txt"
run "${H[@]}" "$TMP/one.txt" --json
eq "$(j 'type')" '"object"' "one hook -> a JSON object"
eq "$(jr '.hook')" 'I lost $18,000 on one missing contract.' "comments and blanks skipped, line stripped"
OUT=$(printf 'Stop buying pre-cut vegetables.\n' | $T "${H[@]}" - --json); eq "$(jr '.hook')" "Stop buying pre-cut vegetables." "stdin"
run "${H[@]}" --hook "   " --hook ""
eq "$CODE" 2 "only blank hooks: exit 2"
eq "$ERR" "nothing to score" "nothing to score"
run "${H[@]}" "$TMP/missing.txt"
eq "$CODE" 2 "missing file: exit 2"
eq "$ERR" "no such file: $TMP/missing.txt" "missing message"
run "${H[@]}"
eq "$CODE" 2 "no input: exit 2"
has "$ERR" "usage: hookscore [-h] [--hook HOOK] [--json] [--list] [input]" "help on stderr"
run "${H[@]}" --list
eq "$CODE" 0 "--list exits 0"
has "$OUT" "26 HOOK FORMULAS  (P = payoff-first)" "list header"
has "$OUT" "  P The Payoff             \$400 SAVED ON FLIGHTS        virality" "list row"
eq "$(echo "$OUT" | grep -c '^  P ')" "$(jq '[.hooks[] | select(.payoff_first)] | length' "$LIB")" "P marks every payoff-first formula"
run "${H[@]}" --list --json
eq "$(j '.')" "$(jq -c '.hooks' "$LIB")" "--list --json is the library, unchanged"
done_ "master-virality/hookscore"

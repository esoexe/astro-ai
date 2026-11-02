#!/usr/bin/env bash
# hunter-interview/star: the assertions of hunter-skill/tests/test_interview.py, run against the C++ tool, plus values
# checked outside it: word / I / we counts with grep, --seed orders from an independent JavaScript MT19937 that
# reproduces CPython's Random(42).random() = 0.6394267984578837. Fixtures: the skill's questions.json and
# example-answer.md, unmodified.
source "$(dirname "$0")/lib.sh"
F="$FIX/hunter-interview"; BANK="$F/questions.json"; ANSWER="$F/example-answer.md"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
st() { run --skill-dir "$F" hunter-interview/star "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
chk() { st check --text "$1" --json; }
SESS="$TMP/hunter/interviews/google.json"

# ---- the bank (the data file itself)
eq "$(jq '[.questions[].id] | (length == (unique | length))' "$BANK")" true "question ids are unique"
eq "$(jq '[.questions[].basis] | unique' -c "$BANK")" '["community","official"]' "basis is official or community"
eq "$(jq '[.questions[] | select(.basis == "official") | .source as $s | (.source != "community") and ($s as $k | input_filename | true)] | all' "$BANK")" true "official questions cite a page"
eq "$(jq '. as $b | [.questions[] | select(.basis == "official") | $b.sources[.source].url | startswith("https://")] | all' "$BANK")" true "official sources are https"
eq "$(jq '[.questions[] | select(.basis == "community") | .source == "community"] | all' "$BANK")" true "community questions cite community"
eq "$(jq '. as $b | [.questions[] | (.question | gsub("\\s"; "") | length > 0) and (.company as $c | $b.companies | has($c))] | all' "$BANK")" true "every question has text and a known company"
eq "$(jq '.sources.community.url' "$BANK")" null "community has no url"
eq "$(jq -r '.sources | to_entries | map(select(.key != "community")) | map(.value as $s | ({"Google":"google.com/about/careers/","Meta":"metacareers.com/","NVIDIA":"nvidia.com/","Netflix":"jobs.netflix.com/","Amazon":"amazon.jobs/"}[$s.publisher]) as $own | ($s.url | contains($own)) and $s.checked == "2026-10-15") | all' "$BANK")" true "official sources are the companies' own pages"
for k in google meta nvidia netflix amazon; do
  eq "$(jq --arg k $k '[.questions[] | select(.company == $k)] | length >= 8' "$BANK")" true "$k: 8+ questions"
  eq "$(jq --arg k $k '. as $b | .companies[$k].loop | (any(.basis == "official")) and (all(.source as $s | $b.sources | has($s)))' "$BANK")" true "$k: an official round, every round sourced"
done
has "$(jq -c '[.companies.amazon.loop[].round]' "$BANK")" '"bar-raiser"' "Amazon has a bar-raiser round"
eq "$(jq -c '[.questions[] | select(.round == "leadership-principles") | .tests] | sort' "$BANK")" "$(printf '%s\n' "Customer Obsession" "Ownership" "Invent and Simplify" "Are Right, A Lot" "Learn and Be Curious" "Hire and Develop the Best" "Insist on the Highest Standards" "Think Big" "Bias for Action" "Frugality" "Earn Trust" "Dive Deep" "Have Backbone; Disagree and Commit" "Deliver Results" "Strive to be Earth's Best Employer" "Success and Scale Bring Broad Responsibility" | jq -R . | jq -sc 'sort')" "one question per leadership principle"

# ---- labels
st ask --company google --round behavioral --count 2 --json
has "$(j '.[1].label')" "https://www.google.com/about/careers/applications/interview-tips/" "google-behavioral-02 cites Google's page"
eq "$(j '.[1].label')" '"official: Google says behavioral rounds cover this (https://www.google.com/about/careers/applications/interview-tips/)"' "the official label"
eq "$(j '.[0] | keys_unsorted | .[-1]')" '"label"' "label is added last"
CQ=$(jq -r '[.questions[] | select(.basis == "community")][0] | "\(.company) \(.round) \(.id)"' "$BANK"); read -r cc cr cid <<< "$CQ"
st ask --company "$cc" --round "$cr" --count 100 --json
eq "$(j "map(select(.id == \"$cid\"))[0].label")" '"community-reported, not confirmed by the company"' "a community label"

# ---- the STAR check
st check --file "$ANSWER" --json
eq "$CODE" 0 "check exits 0"
eq "$(j '.score')" 5 "Jordan's answer is complete"
has "$(j '.found.N')" "38%" "the number"
eq "$(j '.found.N == .found.R')" true "the number sits in the result"
eq "$(j '.found.A | length >= 3')" true "3+ actions"
eq "$(j '.found.A')" '["I decided","I wrote","I proposed","I rebuilt"]' "the actions (I was, I could are not)"
eq "$(j '[.words, .minutes, .i, .we, .of]')" '[136,0.9,5,2,5]' "136 words (grep agrees), I 5, we 2"
eq "$(j 'keys_unsorted')" '["score","of","found","words","minutes","i","we","notes"]' "result keys"
eq "$(j '.found | keys_unsorted')" '["S","T","A","R","N"]' "found keys"
eq "$(j '.found.T')" '"My job was to make it fast without freezing payments work for a quarter."' "the task"
eq "$(j '.notes')" '["136 words (about 0.9 minutes spoken): short; aim for 1 to 3 minutes (add the why behind one action)."]' "only the length note"
st check --file "$ANSWER"
eq "$OUT" "STAR 5/5   136 words, about 0.9 minutes spoken
  S Situation yes: At Brightline Labs last year, our payments API was the slowest service we ran, and nobo...
  T Task      yes: My job was to make it fast without freezing payments work for a quarter.
  A Action    yes: I decided; I wrote; I proposed
  R Result    yes: As a result, p99 latency dropped 38% across 4M requests a day, and design reviews becam...
  N Number    yes: As a result, p99 latency dropped 38% across 4M requests a day, and design reviews becam...
  - 136 words (about 0.9 minutes spoken): short; aim for 1 to 3 minutes (add the why behind one action)." "the report"
chk "We worked on a project and it went well. We improved things a lot. Everyone was happy."
eq "$(j '.score <= 2')" true "a vague answer"; eq "$(j '.found.A')" null "no actions"
for part in Situation Task Action Number; do has "$(j '.notes')" "$part" "vague: $part note"; done
eq "$(j '[.score, .found.R]')" '[1,"We improved things a lot."]' "an outcome verb late in the answer is the result"
chk "At Brightline Labs we had a slow API. We needed to fix it. We rewrote it, we tested it, we shipped it, and we cut latency 38%."
has "$(j '.notes')" 'say what YOU did' "we drowns out I"
has "$(j '.notes')" '\"we\" 6 times, \"I\" 0 times: say what YOU did.' "the we note"
chk "I thought about it and I wanted to help, so I felt good. I knew it."; eq "$(j '.found.A')" null "cognition is not action"
chk "I wrote the doc and I then ran the review."; eq "$(j '.found.A')" '["I wrote","I then ran"]' "I wrote, I then ran"
chk "I fed the cat. I led it. I biked home. I Wrote it."; eq "$(j '.found.A')" '["I led","I biked"]' "-ed needs 4+ letters; lower-case verbs only"
chk "Ipsum I  personally  shipped it and I also cut it."; eq "$(j '.found.A')" '["I  personally  shipped","I also cut"]' "adverbs between, spacing kept"
chk "In 2019 I had to rebuild it. So we shipped. Revenue grew 2x."
eq "$(j '[.found.S, .found.T, .found.R, .found.N]')" '["In 2019 I had to rebuild it.","In 2019 I had to rebuild it.","So we shipped.","In 2019 I had to rebuild it."]' "the first numbered sentence when the result has none"
has "$(j '.notes')" "Put the number in the result sentence" "number not in the result"
chk "Our team was slow. Twenty people waited. As a result we doubled output."
eq "$(j '[.found.R, .found.N, (.notes | map(select(test("Put the number"))) | length)]')" '["As a result we doubled output.","Twenty people waited.",0]' "no digits: N is the first number-word sentence; the result has one, so no note"
chk "A2B is a code. Version v3 shipped."; eq "$(j '.found.N')" null "a digit after a letter is no number"
chk "Our team grew 3x."; eq "$(j '.found.N')" '"Our team grew 3x."' "a digit after a space"
# Unicode: Python's \b, \d and re.I are Unicode-aware
chk $'\xc3\xa9while it ran.'; eq "$(j '.found.S')" null "e-acute is a word character: no boundary before while"
chk $'\xe2\x80\x94while it ran.'; eq "$(j '.found.S')" $'"\xe2\x80\x94while it ran."' "an em dash is not: while matches"
chk $'We grew \xd9\xa3x.'; eq "$(j '.found.N')" $'"We grew \xd9\xa3x."' "an Arabic-Indic digit is \\d"
chk $'\xc4\xb1 needed to fix it.'; eq "$(j '.found.T')" $'"\xc4\xb1 needed to fix it."' "re.I: dotless i matches i"
chk $'At \xc3\x89cole we built it.'; eq "$(j '.found.S')" null "At + a non-ASCII capital is not [A-Z]"
chk $'I\xc2\xa0wrote it and I ran it.'; eq "$(j '.found.A')" $'["I\xc2\xa0wrote","I ran"]' "a no-break space is \\s"
chk "x"; eq "$(j '[.score, .words, .minutes, .found]')" '[0,1,0.0,{"S":null,"T":null,"A":null,"R":null,"N":null}]' "one word"
chk "it's 50% of \$5 -- e-mail"; eq "$(j '.words')" 5 "words with ' % \$ -"
NINE='I wrote nine words of text here for you. '   # 9 words
LONG=$(for i in $(seq 1 51); do printf '%s' "$NINE"; done)
chk "$LONG"; eq "$(j '[.words, .minutes]')" '[459,3.1]' "459 words, round(3.06, 1)"; has "$(j '.notes')" "459 words (about 3.1 minutes spoken): too long; cut the setup, keep the actions." "too long"
st check --text "$LONG" --written --json; has "$(j '.notes')" "459 words: long for a written answer; aim under 300." "written: long"; hasnt "$(j '.notes')" "spoken" "written: no spoken note"
L50=$(for i in $(seq 1 50); do printf '%s' "$NINE"; done)
chk "$L50"; hasnt "$(j '.notes')" "words (about" "450 spoken words: no length note"
L16=$(for i in $(seq 1 16); do printf '%s' "$NINE"; done)
chk "$L16"; eq "$(j '.notes | map(select(test("words"))) | length')" 1 "144 spoken words: short"
L17=$(for i in $(seq 1 17); do printf '%s' "$NINE"; done)
chk "$L17"; hasnt "$(j '.notes')" "words (about" "153 spoken words: fine"
st check --text "$L17" --written --json; hasnt "$(j '.notes')" "words" "153 written words: no length note"
L34=$(for i in $(seq 1 34); do printf '%s' "$NINE"; done)
st check --text "$L34" --written --json; has "$(j '.notes')" "306 words: long for a written answer" "306 written words"
SIT="$(printf 'At Acme we ran it %.0s' {1..10})."
st check --text "$SIT"; has "$OUT" "  S Situation yes: $(printf '%s' "$SIT" | cut -c1-87)..." "cut at 87 characters"
S89=$(printf '%s' "$SIT" | cut -c1-89)   # ends in a letter (the sentence split strips a trailing space)
st check --text "$S89"; has "$OUT" "  S Situation yes: $S89" "89 characters fit"
hasnt "$(echo "$OUT" | grep '  S ')" "..." "no ellipsis under 90"

# ---- pick, sessions
st ask --company Google --round behavioral --count 3 --seed 7 --json; A1=$(j '[.[].id]')
st ask --company google --round behavioral --count 3 --seed 7 --json; eq "$(j '[.[].id]')" "$A1" "the same seed picks the same questions"
eq "$A1" '["google-behavioral-07","google-behavioral-08","google-behavioral-03"]' "seed 7 (independent MT19937)"
st ask --company google --round behavioral --count 3 --seed 1 --json; eq "$(j '[.[].id]')" '["google-behavioral-04","google-behavioral-07","google-behavioral-02"]' "seed 1"
st ask --company google --round behavioral --count 3 --seed -7 --json; eq "$(j '[.[].id]')" "$A1" "a negative seed is its absolute value"
st ask --company google --round behavioral --count 3 --seed 12345678901 --json; eq "$(j '[.[].id]')" '["google-behavioral-05","google-behavioral-02","google-behavioral-08"]' "a seed over 32 bits"
st ask --company google --round behavioral --count 2 --json; eq "$(j '[.[].id]')" '["google-behavioral-01","google-behavioral-02"]' "the first two"
st ask --company "Brightline Labs" --count 3 --json; eq "$(j 'map(.company == "any") | all')" true "an unknown company gets the general questions"; eq "$(j 'length')" 3 "three"
st ask --company "G.o-o g!le" --count 1 --json; eq "$(j '.[0].id')" '"google-behavioral-01"' "the company key keeps letters only"
NG=$(jq '[.questions[] | select(.company == "google")] | length' "$BANK"); NA=$(jq '[.questions[] | select(.company == "any")] | length' "$BANK")
st ask --company google --include-any --count 1000 --json; eq "$(j 'length')" $((NG + NA)) "--include-any adds the general questions"
st ask --company google --count -$((NG - 1)) --json; eq "$(j 'length')" 1 "a negative count slices like Python"
st ask --company google --round behavioral --count 0; eq "$CODE" 1 "nothing to ask exits 1"
eq "$OUT" "No questions left for google / behavioral. star.py review shows how it went." "no questions left"
st ask --company google --round nope; eq "$OUT" "No questions left for google / nope. star.py review shows how it went." "an unknown round"
st ask --company google --round behavioral --count 1 --session "$SESS" --today 2026-10-20
eq "$CODE" 0 "ask with a session"; has "$OUT" "google-behavioral-01" "the first question"
eq "$(echo "$OUT" | head -1)" "1. [google-behavioral-01] $(jq -r '.questions[] | select(.id == "google-behavioral-01") | .question' "$BANK")" "the question line"
eq "$(jq -c . "$SESS")" '{"company":"google","started":"2026-10-20","asked":["google-behavioral-01"],"attempts":[]}' "the new session"
st log --session "$SESS" --question google-behavioral-01 --answer-file "$ANSWER" --today 2026-10-20
eq "$CODE" 0 "log exits 0"; has "$OUT" "STAR 5/5" "logged 5/5"; eq "$(echo "$OUT" | tail -1)" "Logged attempt 1 in $SESS" "logged line"
st log --session "$SESS" --question google-behavioral-02 --text "  We worked on it together and it went fine.  " --today 2026-10-20
eq "$(jq -r '.started' "$SESS")" 2026-10-20 "started"
eq "$(jq -c '[.attempts[].date]' "$SESS")" '["2026-10-20","2026-10-20"]' "attempt dates"
eq "$(jq -c '.attempts[1] | [keys_unsorted, .answer, .check.score]' "$SESS")" '[["date","question_id","question","answer","check"],"We worked on it together and it went fine.",0]' "an attempt, answer stripped"
st review --session "$SESS" --json
eq "$(j '[.attempts, .questions]')" '[2,2]' "2 answers to 2 questions"
eq "$(j '.below_4')" '["google-behavioral-02"]' "redo"
eq "$(j '.not_practiced | index("google-behavioral-01")')" null "answered is practiced"
eq "$(j '.not_practiced | length')" $((NG - 2)) "the rest of Google's questions"
eq "$(j '[.average, .missing_most]')" '[2.5,[["A",1],["N",1],["R",1],["S",1],["T",1]]]' "average and gaps (ties by letter)"
st review --session "$SESS"
eq "$OUT" "2 answers to 2 questions, average STAR 2.50 / 5
Most often missing: Action (1), Number (1), Result (1), Situation (1), Task (1)
Redo (best under 4/5): google-behavioral-02
Not practiced yet: $((NG - 2)) questions (star.py ask --session ...)" "the review"
st ask --company google --round behavioral --count 1 --session "$SESS" --today 2026-10-21; has "$OUT" "google-behavioral-03" "answered ones are skipped"
eq "$(jq -c '.asked' "$SESS")" '["google-behavioral-01","google-behavioral-03"]' "asked grows"
st log --session "$SESS" --question google-behavioral-02 --answer-file "$ANSWER" --today 2026-10-21
st review --session "$SESS" --json; eq "$(j '[.attempts, .questions, .below_4, .average]')" '[3,2,[],3.33]' "the best attempt counts"
st review --session "$TMP/none.json"; eq "$OUT" "No answers logged yet." "an empty review"
st review --session "$TMP/none.json" --json; eq "$(j .)" '{"attempts":0}' "empty review json"
st log --session "$TMP/u.json" --question google-behavioral-01 --text $'At \xc3\x89cole I wrote it.' --today " 2026-1-5 "
eq "$(jq -r '.started' "$TMP/u.json")" 2026-01-05 "--today takes int() parts"
grep -q $'\xc3\x89cole' "$TMP/u.json" && ok || bad "the session keeps UTF-8 (ensure_ascii off)"
st log --session "$TMP/v.json" --question google-behavioral-01 --text "x" --today 2028-02-29; eq "$CODE" 0 "a leap day"

# ---- loop
st loop --company Amazon; eq "$CODE" 0 "loop exits 0"
has "$OUT" "Bar Raiser" "Bar Raiser"; has "$OUT" "https://www.amazon.jobs/content/en/how-we-hire/interview-loop" "the loop page"
eq "$(echo "$OUT" | head -1)" "Amazon interview loop (from questions.json, checked $(jq -r .as_of "$BANK"))" "the loop header"
eq "$(echo "$OUT" | tail -1)" "Rule: $(jq -r .companies.amazon.rules "$BANK")" "the rule"
eq "$(echo "$OUT" | grep -c '^  \[\|^                         \[')" "$(jq '.companies.amazon.loop | length' "$BANK")" "one tag line per round"
st loop --company "Brightline Labs"; eq "$CODE" 1 "no loop exits 1"
eq "$OUT" "No sourced loop for Brightline Labs. Use /hunter-research for the company's own careers pages; general questions: star.py ask --company any" "no loop"

# ---- errors
st check; eq "$CODE" 2 "no text"; eq "$ERR" "star: give --text or --file" "give --text"
st check --text ""; eq "$ERR" "star: give --text or --file" "empty text"
st check --file "$TMP/no.md"; eq "$CODE" 2 "missing file"; eq "$ERR" "star: [Errno 2] No such file or directory: '$TMP/no.md'" "OSError"
st log --session "$SESS" --question google-behavioral-01 --today 2026-10-20; eq "$ERR" "star: give --text or --answer-file" "log needs text"
st log --session "$SESS" --question nope --text hi; eq "$CODE" 2 "unknown question"; eq "$ERR" "star: no question 'nope' in questions.json" "no question"
for d in 2026-13-01 2026-02-29 2026-10 2026-10-20-1 x 2026-1_0-1_ 0-1-1; do
  st ask --company google --today "$d"; eq "$ERR" "star: --today must be YYYY-MM-DD, got '$d'" "bad --today $d"
done
st ask --company google --today 2026-1_0-0_1 --json; eq "$CODE" 0 "underscores between digits are fine"
echo '{' > "$TMP/bad.json"; st review --session "$TMP/bad.json"; eq "$CODE" 2 "a broken session"
eq "$ERR" "star: Expecting property name enclosed in double quotes: line 2 column 1 (char 2)" "JSONDecodeError"
mkdir -p "$TMP/bank"; jq '.questions[0].source = "nope"' "$BANK" > "$TMP/bank/questions.json"
run --skill-dir "$TMP/bank" hunter-interview/star loop --company google; eq "$CODE" 2 "a bank citing an unknown source"
eq "$ERR" "star: question 'google-behavioral-01' cites unknown source 'nope'" "unknown source"
run --skill-dir "$TMP/none" hunter-interview/star loop --company google; eq "$CODE" 2 "no bank"; has "$ERR" "star: [Errno 2] No such file or directory" "no bank file"
st ask; eq "$CODE" 2 "--company is required"
st; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: star" "usage"
done_ "hunter-interview/star"

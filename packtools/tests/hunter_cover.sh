#!/usr/bin/env bash
# hunter-cover/cover: the assertions of hunter-skill/tests/test_cover.py, run against the C++ tool. The fixtures are the
# repo's own files (example-cover.md, hunter-resume/examples/jordan-after.md, hunter-tracker/pipeline.example.json and
# tests/fixtures/hunter-cover/*), unmodified. Word counts are cross-checked with grep -E, outside the tool.
source "$(dirname "$0")/lib.sh"
F="$FIX/hunter-cover"
EX="$F/example-cover.md"; RES="$F/jordan-after.md"; PIPE="$F/pipeline.example.json"; GEN="$F/generic-letter.md"; META="$F/meta-copy-letter.md"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
C=hunter-cover/cover
GOOGLE=(--company Google --role "Software Engineer" --team "Google Cloud, Infrastructure")
j() { echo "$OUT" | jq -c "$1"; }
st() { echo "$OUT" | jq -r ".checks[] | select(.check == \"$1\") | .status"; }
det() { echo "$OUT" | jq -r ".checks[] | select(.check == \"$1\") | .detail"; }
wc_grep() { sed -E '/^\s*(#|[Ss][Uu][Bb][Jj][Ee][Cc][Tt]:)/d' "$1" | grep -oE "[A-Za-z0-9][A-Za-z0-9'%\$.,-]*" | wc -l | tr -d ' '; }
letter() { printf '%s\n' "$@" > "$TMP/l.md"; }

# ---- Jordan's letter
run $C check "$EX" "${GOOGLE[@]}" --resume "$RES" --pipeline "$PIPE" --json
eq "$CODE" 0 "ready exits 0"
eq "$(j '.ready')" true "ready"
eq "$(j '[.checks[].status] | unique')" '["PASS"]' "every check passes"
eq "$(j '[.checks[].check]')" '["length","greeting","company","role","team","proof","cliches","placeholders","other names","numbers","reading","ask","openers"]' "all thirteen checks, in order"
eq "$(j '.words')" "$(wc_grep "$EX")" "the word count agrees with grep"
eq "$(j '.words >= 150 and .words <= 400')" true "150 to 400 words"
eq "$(j '.reading.grade <= 12')" true "grade 12 or lower"
eq "$(j 'keys_unsorted')" '["ready","fails","warns","words","reading","checks"]' "result keys"
eq "$(j '.reading | keys_unsorted')" '["grade","ease","words_per_sentence"]' "reading keys"
eq "$(j '.checks[0] | keys_unsorted')" '["check","status","detail"]' "check keys"
eq "$(det greeting)" "Hi Maya," "the greeting"
eq "$(det proof)" "4 proof point(s), e.g. I rebuilt the payments API, cutting p99 latency 38% across 4M requests a day." "a % proof point first"
run $C check "$EX" "${GOOGLE[@]}" --resume "$RES" --pipeline "$PIPE"
eq "$CODE" 0 "the CLI: exit 0"; has "$OUT" "READY" "READY"
eq "$(echo "$OUT" | head -2)" $'Cover letter check (example-cover.md): READY\n  PASS  length        217 words' "the header and first row"
has "$OUT" '  PASS  openers       6 of 13 sentences start with "I"' "openers row"
run $C check "$GEN" --company Google --role "Software Engineer"; eq "$CODE" 1 "the generic letter exits 1"; has "$OUT" "FIX" "FIX"
eq "$(echo "$OUT" | head -1)" "Cover letter check (generic-letter.md): FIX 7 THINGS" "seven things without --team"

# ---- bad letters
run $C check "$GEN" "${GOOGLE[@]}" --json
for k in length greeting company role team proof cliches placeholders; do eq "$(st "$k")" FAIL "generic: $k fails"; done
eq "$(j '[.fails, .warns, .ready, .words]')" "[8,2,false,$(wc_grep "$GEN")]" "8 fails, 2 warnings"
for phrase in "to whom it may concern" "team player" "passionate about" "proven track record"; do has "$(det cliches)" "$phrase" "cliche: $phrase"; done
has "$(det placeholders)" "[Company]" "the [Company] placeholder"
eq "$(det placeholders)" "left in: [Company], [Position]" "placeholders, sorted"
eq "$(det role)" "the role title is not named (missing: Software, Engineer)" "role words missing"
run $C check "$META" "${GOOGLE[@]}" --resume "$RES" --pipeline "$PIPE" --json
eq "$(st 'other names')" FAIL "a letter copied from another application"; has "$(det 'other names')" "Meta" "names Meta"
eq "$(st numbers)" FAIL "45% is not in the resume"; has "$(det numbers)" "45" "45"
eq "$(det numbers)" "not in your resume, confirm or cut: 45" "the numbers detail"
# other companies come from the pipeline, in order
letter "Hi team," "I liked Meta, NVIDIA, Netflix and Amazon and Google."
run $C check "$TMP/l.md" --company Google --pipeline "$PIPE" --json
eq "$(det 'other names')" "names Meta, NVIDIA, Netflix, Amazon: copied from another letter?" "Meta, NVIDIA, Netflix, Amazon"
run $C check "$TMP/l.md" --company Google --other "Apple, Meta,,Stripe" --json
eq "$(det 'other names')" "names Meta: copied from another letter?" "--other"
run $C check "$TMP/l.md" --company google --pipeline "$PIPE" --other Netflix --json
eq "$(det 'other names')" "names Meta, NVIDIA, Netflix, Amazon: copied from another letter?" "--other does not repeat a pipeline name; the company match ignores case"

# ---- helpers, through small letters
letter "I ran the team. We shipped it. It was fast."
run $C check "$TMP/l.md" --json; eq "$(j '.reading')" '{"grade":-2.5,"ease":118.9,"words_per_sentence":3.3}' "an easy reading level (worked by hand)"
eq "$(j '.reading.grade < 4')" true "easy is under grade 4"
letter "Organizational transformation necessitates comprehensive interdepartmental collaboration, institutionalized accountability and multidimensional evaluation."
run $C check "$TMP/l.md" --json; eq "$(j '.reading.grade > 15')" true "a hard reading level"; eq "$(st reading)" WARN "over grade 12 warns"
printf '' > "$TMP/empty.md"; run $C check "$TMP/empty.md" --json
eq "$(j '[.reading, .words, (.checks[] | select(.check == "openers") | .detail)]')" '[{"grade":0.0,"ease":100.0,"words_per_sentence":0.0},0,"0 of 0 sentences start with \"I\""]' "an empty letter"
eq "$(det greeting)" "no greeting found (e.g. \"Hi Maya,\" or \"Dear Google Cloud team,\"); first line: " "no greeting"
# the sign-off is not the ask
run $C check "$EX" "${GOOGLE[@]}" --json; eq "$(st ask)" PASS "the example asks"
ASKLINE="Would you be open to a short call in the next few weeks about what the team is building next? I would value your view on where someone with my background could help first."
grep -qF "$ASKLINE" "$EX" && ok || bad "the ask line is in the example"
sed "s|$ASKLINE|I look forward to it.|" "$EX" > "$TMP/no-ask.md"
run $C check "$TMP/no-ask.md" "${GOOGLE[@]}" --json; eq "$(st ask)" WARN "without the ask it warns"
eq "$(det ask)" "the last paragraph does not ask for anything (a chat, a call?)" "the ask detail"
run $C check "$EX" --company Google --role "Site Reliability Engineer" --json; eq "$(st role)" FAIL "role words"
eq "$(det role)" "the role title is not named (missing: Site)" "missing role words (the letter says reliability)"
run $C check "$EX" --company Google --role "Engineer, Software" --json; eq "$(det role)" "names the role's words (Engineer, Software)" "the words, not the title"
run $C check "$EX" --company Google --role "SWE II" --json; eq "$(det role)" "the role title is not named (missing: SWE, II)" "short upper-case words count"
run $C check "$EX" --company Google --team "Ads / Search; Cloud" --json; eq "$(det team)" "names Search, Cloud" "any one part of the team"
run $C check "$EX" --company Google --team "Ads; Maps" --json; eq "$(det team)" "never names the team (Ads; Maps)" "no part named"
run $C check "$EX" --company Googl --json; eq "$(det company)" "never names Googl" "a whole-word match"
# greetings, placeholders, numbers
letter "" "  " "  dear Google Cloud team,  " "Body."; run $C check "$TMP/l.md" --json; eq "$(det greeting)" "dear Google Cloud team," "a greeting after blank lines"
letter "Dear Sir or Madam," "x"; run $C check "$TMP/l.md" --json; eq "$(st greeting)" FAIL "a generic greeting"
letter "# Title" "Subject: hello there" "<!-- note: Hi there -->" "Hiya," "Hello world"; run $C check "$TMP/l.md" --json
eq "$(det greeting)" "Hello world" "titles, subjects and comments are skipped; Hiya is not hi"
letter "Hi {{name}}, at <Company Name> for {role} XXX TODO TBD [fill: a number] [Team lead]."
run $C check "$TMP/l.md" --json; eq "$(det placeholders)" "left in: <Company Name>, TBD, TODO, XXX, [Team lead], [fill: a number], {role}, {{name}}" "every placeholder kind"
printf 'I cut costs 1,200 dollars, A123 and v2.5 and 3.0.1 in 2024.\n' > "$TMP/r.md"
letter "Hi Maya," "I cut costs 1,200 dollars, B7 and B77 and 9.9 and 23 and 2024."
run $C check "$TMP/l.md" --resume "$TMP/r.md" --json; eq "$(det numbers)" "not in your resume, confirm or cut: 7, 9.9" "B7 is no number; B77 gives 7, like the lookbehind"
letter "Hi team," "I built a tool. It runs. I also wrote docs. We moved fast."
run $C check "$TMP/l.md" --json; eq "$(det openers)" '1 of 3 sentences start with "I"' "the greeting joins the first sentence; two-word sentences do not count"
letter "I built a tool today. It runs well now. I also wrote docs. We moved fast."
run $C check "$TMP/l.md" --json; eq "$(det openers)" '2 of 4 sentences start with "I"' "half the sentences start with I"; eq "$(st openers)" WARN "half warns"
eq "$(st proof)" FAIL "no number, no proof"
letter "Hi team," "We grew sales 2x last year. We cut costs by 5. Revenue went up \$3M."
run $C check "$TMP/l.md" --json; eq "$(det proof)" "3 proof point(s), e.g. Hi team, We grew sales 2x last year." "a 2x proof point sorts first (the greeting is part of the first sentence)"
letter "Hi Maya," "Thanks," "Jordan"; run $C check "$TMP/l.md" --json; eq "$(st ask)" WARN "only a sign-off"

# ---- CLI
run $C cliches; eq "$CODE" 0 "cliches exits 0"; eq "$(echo "$OUT" | wc -l | tr -d ' ')" 52 "52 banned phrases"; eq "$(echo "$OUT" | head -1)" "to whom it may concern" "the first one"
run $C check "$TMP/nope.md"; eq "$CODE" 2 "a missing letter exits 2"; eq "$ERR" "cover: no file at $TMP/nope.md" "no file"
run $C check "$EX" --resume "$TMP/nope.md"; eq "$ERR" "cover: no file at $TMP/nope.md" "no resume"
echo '{' > "$TMP/bad.json"; run $C check "$EX" --pipeline "$TMP/bad.json"; eq "$CODE" 2 "a broken pipeline exits 2"; eq "$ERR" "cover: Expecting property name enclosed in double quotes: line 2 column 1 (char 2)" "a JSONDecodeError is a ValueError"
run $C check "$EX" --pipeline "$TMP/none.json" --company Google; eq "$CODE" 0 "a missing pipeline is fine"
run $C; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: cover" "usage"
done_ "hunter-cover/cover"

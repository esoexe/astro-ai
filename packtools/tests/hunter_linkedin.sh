#!/usr/bin/env bash
# hunter-linkedin/profile: the assertions of hunter-skill/tests/test_linkedin.py, run against the C++ tool, plus values
# worked out by hand from the fixture JD (term counts, zones, weights: 8 must x3 + 8 body x2 + 11 nice x1 = 51).
# Fixtures: tests/fixtures/hunter-linkedin/swe-infra-jd.txt and the skill's own linkedin.example.json, limits.json and
# hunter-apply/profile.example.json, unmodified.
source "$(dirname "$0")/lib.sh"
F="$FIX/hunter-linkedin"
JD="$F/swe-infra-jd.txt"; EX="$F/linkedin.example.json"; LIM="$F/limits.json"; PROF="$F/profile.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
P=hunter-linkedin/profile
j() { echo "$OUT" | jq -c "$1"; }
jr() { echo "$OUT" | jq -r "$1"; }
job() { printf '%s\n' "$@" > "$TMP/jd.txt"; }
terms() { job "$@"; run $P keywords --job "$TMP/jd.txt" --json; j '[.[].term]'; }
cp "$EX" "$TMP/linkedin.json"

# ---- keywords: zones and weights
run $P keywords --job "$JD" --json
eq "$CODE" 0 "keywords exits 0"
for t in Go Python Java "C++" "Distributed systems" "System design" Algorithms; do
  eq "$(j "map(select(.term == \"$t\")) | .[0] | [.zone, .weight]")" '["must",3]' "$t is required, weight 3"
done
for t in Kubernetes Kafka PostgreSQL "Google Cloud"; do eq "$(jr "map(select(.term == \"$t\")) | .[0].zone")" nice "$t is preferred"; done
eq "$(jr 'map(select(.term == "Design reviews")) | .[0].zone')" body "Design reviews is in the body"
eq "$(jr '.[0].term')" "Distributed systems" "required and named twice comes first"
eq "$(j '.[0] | keys_unsorted')" '["term","weight","zone","count"]' "keyword keys"
eq "$(j 'length')" 27 "27 terms"
eq "$(j '[.[] | select(.zone == "must")] | length')" 8 "8 required"
eq "$(j '[.[] | select(.zone == "body")] | length')" 8 "8 in the body"
eq "$(j '[.[] | select(.zone == "nice")] | length')" 11 "11 preferred"
eq "$(j 'map(select(.term == "Infrastructure"))[0] | [.zone, .weight, .count]')" '["body",2,4]' "Infrastructure: the title, the body and two preferred lines, weight of the body"
eq "$(j '[.[] | select(.count == 2) | .term]')" '["Distributed systems","Backend","Performance","Design reviews"]' "named twice"
eq "$(j '[.[] | select(.zone == "nice") | .term]')" '["APIs","gRPC","Kubernetes","Containers","Google Cloud","Kafka","PostgreSQL","Machine learning","ML infrastructure","Observability","SRE"]' "preferred terms in vocabulary order"
run $P keywords --job "$JD"
eq "$(echo "$OUT" | head -1)" "Distributed systems    weight 3  (must, 2x)" "the text row"
eq "$(echo "$OUT" | sed -n 9p)" "Infrastructure         weight 2  (body, 4x)" "the first body row"
run $P keywords --job "$JD" --top 3; eq "$(echo "$OUT" | wc -l | tr -d ' ')" 3 "--top 3"
run $P keywords --job "$JD" --top -25 --json; eq "$(j '[.[].term]')" '["Distributed systems","Go"]' "--top -25 slices like Python"
run $P keywords --job "$JD" --top 0; eq "$CODE" 1 "no keywords exits 1"

# ---- terms: Go is a language, not a verb
eq "$(terms "ready to go")" '[]' "ready to go"
run $P keywords --job "$TMP/jd.txt"; eq "$CODE" 1 "nothing found exits 1"; eq "$OUT" "" "and prints nothing"
has "$(terms "services in Go")" '"Go"' "services in Go"
has "$(terms "golang services")" '"Go"' "golang"
has "$(terms "modern c++ and Rust")" '"C++"' "c++"; has "$(terms "modern c++ and Rust")" '"Rust"' "Rust"
has "$(terms "an ML ranking service")" '"Machine learning"' "ML"
eq "$(terms "an ml ranking service; GO now; Google")" '[]' "ML and Go are case-sensitive; Google is not Go"
eq "$(terms "Go-to tools, Go+ and Go# and Gopher")" '["Go"]' "Go- is Go, Go+ and Go# are not"
eq "$(terms "RUST, Rust#, rust-lang, trust")" '["Rust"]' "case-insensitive, whole word"
eq "$(terms "K8S and AWS and Postgres, mysql")" '["Kubernetes","AWS","PostgreSQL","MySQL"]' "aliases"
eq "$(terms $'K\xe2\x80\xaaubernetes')" '[]' "a format character is not a letter"
eq "$(terms $'\xe2\x84\xaaubernetes and \xc5\xbfql')" '["SQL","Kubernetes"]' "re.I folds the Kelvin sign and long s (Python's 4 extra letters)"
eq "$(terms $'\xc4\xb0python and \xc4\xb1java and \xc3\xa9java')" '["Java"]' "a dotted or dotless i before is a letter; e-acute is not"
eq "$(terms "rest apis, restful; restaurant")" '["APIs"]' "APIs aliases"
eq "$(terms "on call, on-call, CI/CD and unit tests")" '["On-call","CI/CD","Testing"]' "phrases with punctuation"

# ---- zones
job "Requirements" "Python" "Nice to have:" "Go" "Bonus Points" "Rust" "What you’ll need" "Java" "About Us" "Kafka"
run $P keywords --job "$TMP/jd.txt" --json
eq "$(j '[.[] | [.term, .zone]]')" '[["Python","must"],["Java","must"],["Kafka","body"],["Go","nice"],["Rust","nice"]]' "headings switch zones (curly apostrophe too)"
job "Preferred" "- Required: Python" "* minimum Go" "• Basic Rust" "Responsibilities include building large systems with more than four words: Java"
run $P keywords --job "$TMP/jd.txt" --json
eq "$(j '[.[] | [.term, .zone]]')" '[["Go","nice"],["Python","nice"],["Java","nice"],["Rust","nice"]]' "bullets and long lines are not headings (ties in vocabulary order)"
job "Qualifications we would love to see in someone who joins our very large platform team" "Python"
run $P keywords --job "$TMP/jd.txt" --json; eq "$(j '.[0].zone')" '"body"' "a heading is under 60 characters"
job "must have this, and other words here" "Python"
run $P keywords --job "$TMP/jd.txt" --json; eq "$(j '.[0].zone')" '"body"' "not a title, not 4 words, no colon"
job "must have this and more:" "Python"
run $P keywords --job "$TMP/jd.txt" --json; eq "$(j '.[0].zone')" '"must"' "a colon makes a heading"
job "Must Have Experience With Things Like" "Python"
run $P keywords --job "$TMP/jd.txt" --json; eq "$(j '.[0].zone')" '"must"' "title case makes a heading"

# ---- before and after
run $P compare --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$PROF" --limits "$LIM" --json
eq "$CODE" 0 "compare exits 0"
eq "$(j '.before | [.coverage, .must_covered, .must_total]')" '[22,2,8]' "before: 22%, 2/8 (11 of 51)"
eq "$(j '.after | [.coverage, .must_covered, .must_total]')" '[86,7,8]' "after: 86%, 7/8 (44 of 51)"
eq "$(j '.after.missing_must')" '["C++"]' "C++ still missing"
eq "$(j '.after.missing_other')" '["Code review","Containers","SRE"]' "other missing terms"
eq "$(j '.before | keys_unsorted')" '["coverage","must_covered","must_total","missing_must","missing_other","limits","advice","lengths","terms"]' "before has no confirm"
eq "$(j '.after | keys_unsorted')" '["coverage","must_covered","must_total","missing_must","missing_other","limits","advice","lengths","terms","confirm"]' "after has confirm"
eq "$(j '.before.lengths')" '{"headline":36,"about":0,"skills":6}' "before lengths"
eq "$(j '.before.terms[0]')" '{"term":"Distributed systems","weight":3,"zone":"must","count":2,"where":[]}' "a term with where"
eq "$(j '.before.terms | map(select(.where != [])) | map([.term, .where])')" '[["Python",["skills"]],["Java",["skills"]],["Backend",["experience"]],["On-call",["experience"]],["Machine learning",["experience"]]]' "where each term is on the current profile"
eq "$(j '.before.missing_must')" '["Distributed systems","Go","C++","System design","Data structures","Algorithms"]' "before: required terms missing"
eq "$(jr '.before.advice[]')" "Headline reads like the default \"title at company\": add what you build and your strongest skills from the posting.
None of the top required terms (Distributed systems, Go, Python) is in the headline.
About is empty: write a short story of what you build, with one or two real results.
Software Engineer at Brightline Labs has no numbers: add a result you can prove.
On the profile but not in Skills (add them if true): Backend, On-call.
Required by the posting and missing everywhere: Distributed systems, Go, C++, System design, Data structures, Algorithms. Add only what the user really has." "before: every piece of advice"
has "$(j '.before.advice')" "default" "the default headline"; has "$(j '.before.advice')" "About is empty" "About is empty"; has "$(j '.before.advice')" "no numbers" "no numbers"
eq "$(j '.after.confirm')" '["System design","Data structures","Algorithms","ML infrastructure","Observability","Reliability","Scalability","Performance","Design reviews","Infrastructure"]' "claims to confirm, vocabulary order"
eq "$(j '.after.limits[0]')" '{"field":"headline","value":82,"max":220,"unit":"characters","ok":true,"source":"reported limit (the editor'"'"'s counter is the authority)"}' "a limit row"
eq "$(j '[.after.limits[].field]')" '["headline","about","skills","position_title","position_description"]' "limit rows"

run $P compare --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$PROF" --limits "$LIM"
eq "$CODE" 0 "the compare report exits 0"
has "$OUT" "Keyword coverage              22%        86%" "coverage row"
has "$OUT" "Required terms                2/8        7/8" "required row"
has "$OUT" "Gained: Distributed systems, Go" "gained"
has "$OUT" "Still missing: C++" "still missing"
has "$OUT" "Paste the draft into LinkedIn yourself" "paste it yourself"
hasnt "$OUT" $'\xe2\x80\x94' "no em dash"
eq "$(echo "$OUT" | head -1)" "                           before      after" "the header row"
has "$OUT" "About characters                0        706" "the about row"
has "$OUT" "Still missing: C++, Code review, Containers, SRE" "every missing term"
has "$OUT" "  ok   about                   706 / 2,600 characters  (reported limit (the editor's counter is the authority))" "thousands separator"
has "$OUT" "  ok   skills                   18 / 100 skills  (https://www.linkedin.com/help/linkedin/answer/35265)" "a sourced limit"
has "$OUT" "  - On the profile but not in Skills (add them if true): Infrastructure, Backend, Performance, Design reviews, Reliability, Scalability, On-call." "not in skills"
hasnt "$OUT" "Lost:" "nothing lost"

# ---- truth check
printf 'Go Python Java Kubernetes\n' > "$TMP/resume.md"
run $P check --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$TMP/none.json" --resume "$TMP/resume.md" --limits "$LIM" --json
has "$(j '.confirm')" '"Kafka"' "Kafka is not backed"; hasnt "$(j '.confirm')" '"Go"' "Go is backed"
hasnt "$(j '.confirm')" '"Backend"' "the current profile backs Backend"
printf 'Go Python Java Kubernetes Kafka\n' > "$TMP/resume.md"
run $P check --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$TMP/none.json" --resume "$TMP/resume.md" --limits "$LIM" --json
hasnt "$(j '.confirm')" '"Kafka"' "the resume backs Kafka"
run $P check --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$TMP/none.json" --limits "$LIM" --which current --json
eq "$(j '[.coverage, .confirm]')" '[22,["Python","Java","SQL","Backend","Machine learning","On-call"]]' "checking current: nothing backs it but itself"
run $P check --linkedin "$TMP/linkedin.json" --job "$JD" --profile "$TMP/none.json" --limits "$LIM" --which current
eq "$(echo "$OUT" | head -1)" "Current profile vs the job description: keyword coverage 22%, required terms 2/8." "Current, capitalised"
has "$OUT" "  - Confirm these are true before publishing (not in the resume, profile skills or current LinkedIn): Python, Java, SQL, Backend, Machine learning, On-call." "the confirm line"

# ---- limits
jq '.draft.headline = ("x" * 221) | .draft.skills = [range(101) | "s\(.)"]' "$EX" > "$TMP/long.json"
run $P check --linkedin "$TMP/long.json" --job "$JD" --limits "$LIM" --json
eq "$CODE" 1 "over a limit exits 1"
eq "$(j '.limits | map(select(.field == "headline"))[0].ok')" false "headline over"
eq "$(j '.limits | map(select(.field == "skills"))[0] | [.ok, .value]')" '[false,101]' "skills over"
eq "$(j '.limits | map(select(.field == "about"))[0].ok')" true "about fine"
eq "$(jr '.limits | map(select(.field == "skills"))[0].source')" "https://www.linkedin.com/help/linkedin/answer/35265" "skills source"
has "$(jr '.limits | map(select(.field == "headline"))[0].source')" "reported" "headline source"
run $P check --linkedin "$TMP/long.json" --job "$JD" --limits "$LIM"
has "$OUT" "  OVER headline                221 / 220 characters" "OVER"
eq "$(jq '.fields | [.headline.max, .about.max, .skills.max, .invitation_note.max]' -c "$LIM")" '[220,2600,100,200]' "the limits file"
eq "$(jq -r '.fields.invitation_note.source' "$LIM")" "https://www.linkedin.com/help/linkedin/answer/46662" "invitation note source"
has "$(jq -r '.automation.rule' "$LIM")" "8.2" "section 8.2"
jq '.draft.about = ("a" * 2601)' "$EX" > "$TMP/about.json"
run $P check --linkedin "$TMP/about.json" --job "$JD" --limits "$LIM" --json
eq "$CODE" 1 "about over"; eq "$(j '.limits | map(select(.field == "about"))[0] | [.value, .ok]')" '[2601,false]' "2601 characters"
jq '.draft.about = ("é" * 2600)' "$EX" > "$TMP/about.json"
run $P check --linkedin "$TMP/about.json" --job "$JD" --limits "$LIM" --json
eq "$CODE" 0 "characters, not bytes"; eq "$(j '.lengths.about')" 2600 "2600 characters"
has "$(j '.advice')" "About has no numbers" "a long About with no digits"
run $P limits --limits "$LIM"; eq "$CODE" 0 "limits exits 0"
has "$OUT" "invitation_note          200 characters" "the invitation note row"
has "$OUT" "about                  2,600 characters reported: the editor's counter is the authority" "the about row"
eq "$(echo "$OUT" | tail -1 | cut -c1-37)" "Automation: LinkedIn's User Agreement" "the automation line"
OUT=$(cd "$F" && $T $P limits 2>&1 | head -1); eq "$OUT" "headline                 220 characters reported: the editor's counter is the authority" "limits.json next to the tool by default"

# ---- advice on small drafts
mk() { jq -n --arg h "$1" --argjson s "$2" --arg a "$3" '{current: {}, draft: {headline: $h, skills: $s, about: $a, experience: [{title: "SWE", company: "Acme", description: "built things"}]}}' > "$TMP/d.json"; }
adv() { run $P check --linkedin "$TMP/d.json" --job "$JD" --limits "$LIM" --json; j '.advice'; }
mk "Engineer at Acme" '["Go","go","GO","Rust","rust"]' "short"
A=$(adv)
has "$A" 'Headline reads like the default' "title at company"
has "$A" 'Duplicate skills: go, rust.' "duplicates, lower-cased and sorted"
has "$A" 'About is short (5 characters)' "a short About"
has "$A" 'SWE at Acme has no numbers' "experience without numbers"
hasnt "$A" 'Only ' "five skills is enough"
for h in "Senior  Engineer   AT   Big Co" "Go Engineer at Acme Platform"; do mk "$h" '[]' ""; has "$(adv)" "default" "default: $h"; done
for h in "A at Acme" "Engineer at X" "Engineer at Acme | Go" "Engineer · at Acme" "Engineer, at Acme" "Engineeratacme" "Engineer at"; do mk "$h" '[]' ""; hasnt "$(adv)" "default" "not the default: $h"; done
mk "$(printf 'x%.0s' {1..60}) at Acme" '[]' ""; has "$(adv)" "default" "60 characters before at"
mk "$(printf 'x%.0s' {1..61}) at Acme" '[]' ""; hasnt "$(adv)" "default" "61 characters before at"
mk "Engineer at Acme at Big Co" '[]' ""; has "$(adv)" "default" "the second at can be the split"
mk "" '["a"]' ""; A=$(adv); has "$A" "Headline is empty." "empty headline"; has "$A" "Only 1 skills listed." "one skill"
has "$A" "None of the top required terms (Distributed systems, Go, Python) is in the headline." "top terms"
mk "Go and Python engineer" '[]' ""; hasnt "$(adv)" "None of the top" "a top term in the headline"

# ---- errors
run $P check --linkedin "$TMP/no.json" --job "$JD"; eq "$CODE" 2 "no linkedin.json"
eq "$ERR" "error: no $TMP/no.json. Copy skills/hunter-linkedin/linkedin.example.json there and paste the user's real profile into \"current\"." "the message"
echo '{}' > "$TMP/empty.json"; run $P compare --linkedin "$TMP/empty.json" --job "$JD"; eq "$CODE" 2 "an empty linkedin.json"
run $P keywords --job "$TMP/no.txt"; eq "$CODE" 2 "no job file"; eq "$ERR" "error: no file $TMP/no.txt" "no file"
run $P check --linkedin "$TMP/linkedin.json" --job "$TMP/no.txt"; eq "$CODE" 2 "no job file for check"
run $P keywords; eq "$CODE" 2 "--job is required"; has "$ERR" "the following arguments are required: --job" "required"
run $P check --linkedin "$TMP/linkedin.json" --job "$JD" --which both; eq "$CODE" 2 "bad --which"
run $P; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: profile" "usage"
done_ "hunter-linkedin/profile"

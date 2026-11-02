#!/usr/bin/env bash
# hunter-resume/ats: the assertions of hunter-skill/tests/test_resume.py, run against the C++ tool, plus cases worked
# out by hand from the rules in ats.py. Fixtures: the skill's terms.json (skill/), its examples and the upstream test
# files in tests/fixtures/hunter-resume, unmodified. The resumes and postings written below are synthetic.
source "$(dirname "$0")/lib.sh"
D="$FIX/hunter-resume"; SK="$D/skill"
BEFORE="$D/jordan-before.md"; AFTER="$D/jordan-after.md"; JD="$D/google-swe-jd.md"; INV="$D/jordan-after-invented.md"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
A() { run --skill-dir "$SK" hunter-resume/ats "$@"; }
j() { echo "$OUT" | jq -c "$1"; }

# ---- the reel: Jordan Reyes vs the Google posting, 54 -> 96 and nothing invented
A score "$BEFORE" --jd "$JD" --json --today 2026-10-15
eq "$(j .score)" 54 "before scores 54"
eq "$(j .parts)" '{"R":0.625,"P":0.5,"B":0.2857,"S":0.75}' "before parts"
eq "$(j .raw)" 54.46 "raw"; eq "$(j .weights)" '{"R":50,"P":20,"B":20,"S":10}' "weights"
eq "$(j .points)" '{"R":31.25,"P":10.0,"B":5.71,"S":7.5}' "points"
eq "$(j .formula)" '"score = 50 x R + 20 x P + 20 x B + 10 x S, rounded half up"' "formula"
for pair in '["Golang","Go"]' '["k8s","Kubernetes"]' '["GCP","Google Cloud Platform"]' '["distributed computing","distributed systems"]'; do
  has "$(j '[.reword[]|[.you_wrote,.posting_says]]')" "$pair" "reword $pair"; done
eq "$(j '[.missing[].term]')" '["C++","Java","master'"'"'s degree","PhD","mentoring","documentation"]' "before is missing"
eq "$(j .years)" '{"resume":3.0,"asked":2}' "years"
eq "$(j 'keys_unsorted')" '["score","raw","formula","weights","parts","points","must","preferred","reword","missing","bullets","sections","years"]' "key order"
eq "$(j '.bullets[0]|keys_unsorted')" '["section","text","number","verb","length","words","points","notes"]' "bullet keys"
eq "$(j '.must[1].groups[1]')" '{"choice":true,"covered":false,"terms":[{"term":"Go","status":"reword","resume_says":"Golang"},{"term":"C++","status":"no","resume_says":null},{"term":"Java","status":"no","resume_says":null}]}' "a choice group in full"
eq "$(j '[.must[].coverage]')" '[1.0,0.5,0.0,1.0]' "coverage per line"
has "$OUT" '"points": 0.6666666666666666' "two of three checks"
A score "$AFTER" --jd "$JD" --json --today 2026-10-15
eq "$(j .score)" 96 "after scores 96"
eq "$(j .parts)" '{"R":1.0,"P":0.85,"B":0.9444,"S":1.0}' "after parts"
eq "$(j '[.missing[].term]')" '["master'"'"'s degree","PhD","mentoring","documentation"]' "after is missing"
eq "$(j .reword)" '[]' "nothing to reword"; eq "$(j .years)" '{"resume":3.0,"asked":2}' "years after"
A score "$AFTER" --jd "$JD" --json --today 2031-01-01; eq "$(j '[.score,.years.resume]')" '[96,8.0]' "--today only changes the years"
A compare "$BEFORE" "$AFTER" --jd "$JD"; eq "$CODE" 0 "compare is clean"
has "$OUT" "Before 54 -> after 96" "compare line"; has "$OUT" "Truth check: CLEAN" "clean"
eq "$(echo "$OUT" | sed -n 2,5p | tr '\n' '|')" "  R  0.62 -> 1.00|  P  0.50 -> 0.85|  B  0.29 -> 0.94|  S  0.75 -> 1.00|" "parts, %.2f (0.625 rounds to even)"
has "$OUT" "ATS match: 96 / 100   jordan-after.md" "then the after report"
A compare "$BEFORE" "$AFTER" --jd "$JD" --json
eq "$(j '[.before,.after,.trace.clean,.trace.new_names]')" '[54,96,true,[]]' "compare --json"
eq "$(j 'keys_unsorted')" '["before","after","before_parts","after_parts","trace"]' "compare keys"
for b in "Rebuilt the payments API in Go, cutting p99 latency 38% across 4M requests a day" "Shipped an ML ranking service that lifted search click-through 12%" "Led on-call for 9 services and cut pages 60% with automated runbooks"; do
  has "$(cat "$AFTER")" "- $b" "the reel bullet: $b"; done

# the text report
A score "$BEFORE" --jd "$JD" --today 2026-10-15
eq "$(echo "$OUT" | head -1)" "ATS match: 54 / 100   jordan-before.md" "headline"
eq "$(echo "$OUT" | sed -n 4p)" "  R  minimum qualifications covered         0.62 x 50 = 31.25" "R line"
eq "$(echo "$OUT" | sed -n 6p)" "  B  bullet strength                        0.29 x 20 =  5.71" "B line"
eq "$(echo "$OUT" | sed -n 8p)" "  total 54.46, rounded 54" "total line"
has "$OUT" "  [~] 2 years of experience with software development in Go, C++ or Java.
        missing Go or C++ or Java: you wrote \"Golang\"" "a partly covered line"
has "$OUT" "  [ ] Mentor newer engineers and contribute to documentation.
        missing mentoring
        missing documentation" "an uncovered line"
has "$OUT" '  "k8s" -> "Kubernetes"' "reword line"
has "$OUT" "Missing (add only if it is true; otherwise leave it out and cover it in the cover letter or interview):
  C++, Java, master's degree, PhD, mentoring, documentation" "missing list"
has "$OUT" "   4. Wrote design docs and ran design reviews for the payments rewrite w...
      no number: how much, how many, how fast?" "long bullets are cut at 67 characters"
has "$OUT" '   5. Did code reviews and unit tests for the team
      no number: how much, how many, how fast?; weak opening "Did code": start with what you did (Built, Cut, Led, Shipped)' "a weak opening"
has "$OUT" "Sections: experience yes, education yes, skills MISSING, contact yes" "sections"
eq "$(echo "$OUT" | tail -1)" "Years: the posting asks for 2; the experience dates add up to about 3 (information, not scored)." "years line"

# ---- the truth check
A trace "$BEFORE" "$INV" --json
eq "$(j .)" '{"invented_numbers":["5"],"invented_terms":["Kafka","data pipelines"],"new_names":["Senior"],"clean":false}' "an invented number and skills"
A trace "$BEFORE" "$INV"; eq "$CODE" 1 "trace exits 1"
eq "$OUT" "Truth check: INVENTED content. Remove it or confirm it with the user and add it to the original first.
  numbers not in the original: 5
  skills or terms not in the original: Kafka, data pipelines
  new capitalized words to double-check: Senior" "trace text"
echo "Confirmed by Jordan on 2026-10-14: led a team of 5 on Kafka data pipelines." > facts.txt
A trace "$BEFORE" "$INV" --facts facts.txt --json; eq "$(j '[.invented_numbers,.invented_terms,.clean]')" '[[],[],true]' "confirmed facts are allowed"
eq "$CODE" 0 "clean with the facts"
A compare "$BEFORE" "$INV" --jd "$JD"; eq "$CODE" 1 "compare exits 1 on invented content"; has "$OUT" "INVENTED" "says so"
printf 'Built APIs for 1,200 users at Acme\nTeam lead for the payments team\n' > b.md
printf -- '- Spearheaded APIs for 1200 users at Acme with Stripe\n- Teams of 3\n## Skills\nTools: Rust\n' > a.md
A trace b.md a.md --json
eq "$(j .)" '{"invented_numbers":["3"],"invented_terms":["Rust"],"new_names":["Stripe","Tools"],"clean":false}' "commas, opening verbs, plurals, headings, known terms"
printf '3 Rust\n' > f2.txt; A trace b.md a.md --facts f2.txt; eq "$CODE" 0 "facts clear it"
eq "$OUT" "Truth check: CLEAN. Every number and skill in the rewrite is in the original.
  new capitalized words to double-check: Stripe, Tools" "clean, with names to check"
printf 'x\n' > e.md; printf '10 and 9 and 100 and 2.5 and 1,000.5\n' > n.md
A trace e.md n.md --json; eq "$(j .invented_numbers)" '["9","10","100","2.5","1000.5"]' "numbers sort by length, then text"
printf 'Cut costs \xd9\xa3\xd9\xa0%%\n' > u.md; A trace e.md u.md --json; has "$OUT" '"\u0663\u0660"' "Unicode digits are numbers (written as JSON escapes, like Python)"

# ---- matching (the posting has no headings, so every line is a minimum qualification)
cat > m.txt <<EOF
we go to the office
Services in Go, Python
Google
debugging production issues
the oncall rotation
a data structure
C++ and Rust
Bachelor’s degree
Go or Rust or Java
Python, e.g. Django
Python such as Django
SQL and/or NoSQL
$(printf '\xe2\x84\xaa')ubernetes
$(printf '\xc5\xbf')QL
JAVA and Javascript
back  end
back end
machine$(printf '\t')learning
rest of the team
node.js and Node
the APIes
mentors and mentored
machine learning (ML) or deep learning
Go/Rust
PYTHON
EOF
A terms --jd m.txt --json
G() { j "[.must[$1].groups[]|if .choice then {or:.terms} else .terms[0] end]"; }
eq "$(G 0)" '[]' "Go is case-sensitive"
eq "$(G 1)" '["Go","Python"]' "Go in a list"
eq "$(G 2)" '[]' "Go is not Google"
eq "$(G 3)" '["debugging"]' "inflections"
eq "$(G 4)" '["on-call"]' "a hyphen is optional"
eq "$(G 5)" '["data structures"]' "singular of a plural form"
eq "$(G 6)" '["C++","Rust"]' "C++ and a case-sensitive Rust"
eq "$(G 7)" '["bachelor'"'"'s degree"]' "curly apostrophe"
eq "$(G 8)" '[{"or":["Go","Rust","Java"]}]' "an or-list is a choice"
eq "$(G 9)" '["Python","Django"]' "e.g. followed by a space is not filler (no word boundary after the dot)"
eq "$(G 10)" '[{"or":["Python","Django"]}]' "such as is a choice"
eq "$(G 11)" '[{"or":["SQL","NoSQL"]}]' "and/or"
eq "$(G 12)" '["Kubernetes"]' "the Kelvin sign is a K when case is ignored"
eq "$(G 13)" '["SQL"]' "long s is an s when case is ignored"
eq "$(G 14)" '["JavaScript"]' "JAVA is not Java; Javascript is JavaScript"
eq "$(G 15)" '[]' "a hyphen stands for one space at most"
eq "$(G 16)" '["backend"]' "back end"
eq "$(G 17)" '["machine learning"]' "a tab is whitespace"
eq "$(G 18)" '[]' "rest is not REST"
eq "$(G 19)" '["Node.js"]' "two forms, one term, one group"
eq "$(G 20)" '["APIs"]' "plural es"
eq "$(G 21)" '["mentoring"]' "mentors and mentored"
eq "$(G 22)" '[{"or":["machine learning","deep learning"]}]' "parentheses are filler"
eq "$(G 23)" '["Go","Rust"]' "a slash is filler but not a choice"
eq "$(G 24)" '["Python"]' "case ignored"
eq "$(j '.preferred')" '[]' "no preferred lines"

# ---- headings in the posting
cat > h.txt <<'EOF'
## About us
We use Python everywhere.
**Nice to have:** Kafka
Experience with large scale bonus
Experience with large scale bonus: Redis
<!-- Go -->
What you’ll do
1) Ship Go services
• Write SQL
Requirements of the role that we have for the person in this team
- Rust
EOF
A terms --jd h.txt --json
eq "$(j '.must|length')" 0 "nothing required"
eq "$(j '[.preferred[]|[.line,.section,[.groups[].terms[]]]]')" '[["** Kafka","preferred",["Kafka"]],["Experience with large scale bonus","preferred",[]],["Redis","preferred",["Redis"]],["Ship Go services","responsibilities",["Go"]],["Write SQL","responsibilities",["SQL"]],["Requirements of the role that we have for the person in this team","responsibilities",[]],["Rust","responsibilities",["Rust"]]]' "inline headings, long lines, comments, bullets"
A terms --jd h.txt
eq "$(echo "$OUT" | head -6)" "Minimum qualifications

Preferred and responsibilities
  - ** Kafka
      terms: Kafka
  - Experience with large scale bonus" "terms text"
has "$OUT" "      terms: none known" "none known"
A terms --jd "$D/jd-inline.txt" --json; eq "$(j '[(.must|length),(.preferred|length)]')" '[3,1]' "jd-inline: 3 minimum, 1 preferred"
A terms --jd "$JD"; has "$OUT" "  - 2 years of experience with software development in Go, C++ or Java.
      terms: software development, (Go or C++ or Java)" "terms text with a choice"

# ---- scoring rules
printf 'Experience\n- Built payments APIs in Python and PostgreSQL\nEducation\nBS, Computer Science\n' > r1.md
A score r1.md --jd "$D/jd-inline.txt" --json; eq "$(j '[.missing[].term]')" '["Redis","Kubernetes"]' "the About section is not scored"
printf 'Skills\nSQL, Python, Airflow\n' > r2.md
A score r2.md --jd "$D/jd-plain.txt" --json
eq "$(j .weights)" '{"R":70,"P":0,"B":20,"S":10}' "no sections: R carries 70"; eq "$(j '[.missing[].term]')" '["data pipelines","BigQuery"]' "jd-plain missing"
printf 'Education\nB.S. in Computer Science\n' > r3.md; printf "Minimum qualifications:\n- Bachelor's degree.\n" > j3.md
A score r3.md --jd j3.md --json; eq "$(j .parts.R)" 1.0 "degrees count by meaning"
cat > s1.jd <<'EOF'
Requirements:
- 3+ years with Python or Go
- 5 years of experience with SQL
Nice to have: Kafka
EOF
cat > s1.md <<'EOF'
Jordan Lee
jordan@example.com

EXPERIENCE
Data Engineer, Acme, 2019 - 2021
- Built 12 Python ETL jobs that cut nightly load time from 6 hours to 40 minutes
Analyst, Beta, 2022 to Present
- Helped with dashboards

TECHNICAL SKILLS
Python, Postgres
EOF
A score s1.md --jd s1.jd --json --today 2026-03-01
eq "$(j '[.score,.raw,.parts,.points]')" '[43,42.5,{"R":0.5,"P":0.0,"B":0.5,"S":0.75},{"R":25.0,"P":0.0,"B":10.0,"S":7.5}]' "a worked example: 42.5 rounds half up to 43"
eq "$(j .missing)" '[{"term":"SQL","where":"must","choice":false},{"term":"Kafka","where":"preferred","choice":false}]' "missing, with where"
eq "$(j .sections)" '{"experience":true,"education":false,"skills":true,"contact":true}' "an uppercase heading, TECHNICAL SKILLS, an email"
eq "$(j .years)" '{"resume":6.0,"asked":5}' "2019 - 2021 and 2022 to Present; the most years asked"
A score s1.md --jd s1.jd --today 2026-03-01
L="Built 12 Python ETL jobs that cut nightly load time from 6 hours to 40 minutes"
has "$OUT" "   1. ${L:0:67}...
      ok" "a full-marks bullet"
has "$OUT" "  [x] 3+ years with Python or Go" "covered by one of a choice"
has "$OUT" "  [ ] Kafka
        missing Kafka" "preferred"
has "$OUT" "Sections: experience yes, education MISSING, skills yes, contact yes" "sections line"
eq "$(echo "$OUT" | tail -1)" "Years: the posting asks for 5; the experience dates add up to about 6 (information, not scored)." "years line"
printf 'Preferred qualifications:\n- Docker\n' > s2.jd; printf 'Skills\nDocker\n' > s2.md
A score s2.md --jd s2.jd --json
eq "$(j '[.score,.weights,.parts.R,.must[0].section,.preferred]')" '[73,{"R":70,"P":0,"B":20,"S":10},1.0,"preferred",[]]' "only preferred lines: they become the minimum"
A score s2.md --jd s2.md --today 2026-01-01; eq "$CODE" 0 "Docker is a term"
printf 'We like kind people.\n' > s3.jd; A score s2.md --jd s3.jd; eq "$CODE" 2 "no terms"
eq "$ERR" "ats: no known terms in the job description: add the posting's skills with --terms extra.json" "says so"
cat > s6.md <<'EOF'
Experience
- Reduced build time 40% for 12 services across three product teams
- Mentored 3 interns
- In charge of 4 releases a quarter for the platform team
- Containerized 5 apps and moved them to a shared cluster for the team
- 2x faster builds after moving the monorepo to remote caching for everyone
- Cut cloud costs ٣٠% by rightsizing clusters across all regions
- Handled k8s upgrades
- Led a team that shipped one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty twentyone twentytwo twentythree twentyfour twentyfive more
EOF
printf 'Requirements:\n- Kubernetes\n' > s6.jd
A score s6.md --jd s6.jd --json
eq "$(j '[.bullets[]|[.number,.verb,.length,.words]]')" '[[true,true,true,11],[true,true,false,3],[true,false,true,11],[true,true,true,13],[true,false,true,12],[true,true,true,10],[false,false,false,3],[false,true,false,31]]' "bullet checks"
eq "$(j '.bullets[1].notes')" '["3 words: too short to show the result"]' "too short"
eq "$(j '.bullets[2].notes')" '["weak opening \"In charge\": start with what you did (Built, Cut, Led, Shipped)"]' "in charge"
eq "$(j '.bullets[4].notes')" '["weak opening \"2x\": start with what you did (Built, Cut, Led, Shipped)"]' "the first word is the first ASCII word"
eq "$(j '.bullets[6].notes')" '["no number: how much, how many, how fast?","weak opening \"Handled k8s\": start with what you did (Built, Cut, Led, Shipped)","3 words: too short to show the result"]' "k8s is not a number"
eq "$(j '.bullets[7].notes')" '["no number: how much, how many, how fast?","31 words: too long, one achievement per bullet"]' "too long"
eq "$(j '[.parts.B,.score,.reword]')" '[0.6667,16,[{"term":"Kubernetes","you_wrote":"k8s","posting_says":"Kubernetes","where":"must"}]]' "B, the score, a reword"
printf 'Experience\nAcme, 2021\xe2\x80\x932019\nBeta, 2019-2021\nGamma, 2018 until NOW\nDelta 1999 - 2001\n' > y.md
A score y.md --jd s6.jd --json --today 2026-05-05; eq "$(j .years.resume)" 12.0 "en dash, reversed, no spaces, until now, the 1990s"
printf 'Experience\nx\n+1 312 555 0147\n' > p1.md; A score p1.md --jd s6.jd --json; eq "$(j .sections.contact)" true "a phone number"
printf 'Experience\nCall 555 0147 or jo@x\n' > p2.md; A score p2.md --jd s6.jd --json; eq "$(j .sections.contact)" false "too short for a phone, no dot for an email"
printf 'Work History:\n- Shipped 3 things for the team every single week\n## Projects\n- Built 2 tools used by 40 people each day\nPYTHON\n- Wrote 9 scripts that nobody ever used at all\nInterests\n' > sec.md
A score sec.md --jd s6.jd --json
eq "$(j '[.bullets[].section]')" '["experience","projects"]' "Work History:, ## Projects, and an all-caps line that is no known section"
printf 'Experience\n- Wrote Zig services for 3 teams across the whole company\n' > z.md; printf 'Requirements:\n- Zig\n' > z.jd
printf '{"terms": [{"term": "Zig", "forms": ["Zig"]}]}' > extra.json
A score z.md --jd z.jd --terms extra.json --json; eq "$(j .parts.R)" 1.0 "--terms adds terms"
A score z.md --jd z.jd; eq "$CODE" 2 "without them, Zig is unknown"

# ---- errors
A score missing.md --jd "$JD"; eq "$CODE" 2 "no resume"; eq "$ERR" "ats: no file at missing.md" "no file"
A score "$AFTER" --jd nope.md; eq "$ERR" "ats: no file at nope.md" "no posting"
A score "$AFTER" --jd "$JD" --terms nope.json; eq "$CODE" 1 "a missing --terms file is an OSError, like Python"
printf 'nope' > bad.json; A score "$AFTER" --jd "$JD" --terms bad.json; eq "$CODE" 1 "bad JSON"; has "$ERR" "JSONDecodeError" "JSONDecodeError"
printf '{"terms": [{"forms": ["X"]}]}' > bad2.json; A score "$AFTER" --jd "$JD" --terms bad2.json; eq "$CODE" 1 "a term without a name"; has "$ERR" "KeyError" "KeyError"
for d in 2026-13-01 2026-10 x 2026-10-15-1; do A score "$AFTER" --jd "$JD" --today "$d"; eq "$CODE" 1 "--today $d"; done
A; eq "$CODE" 2 "no command"; has "$OUT" "usage: ats" "usage"
A score "$AFTER"; eq "$CODE" 2 "--jd is required"
A trace "$BEFORE" "$AFTER" --facts nope.txt; eq "$ERR" "ats: no file at nope.txt" "facts are read first"
done_ "hunter-resume/ats"

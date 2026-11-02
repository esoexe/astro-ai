#!/usr/bin/env bash
# hunter-scout/jobs: the assertions of hunter-skill/tests/test_scout.py, run against the C++ tool, plus cases worked
# out by hand from the rules in jobs.py. Fixtures: the upstream tests/fixtures/hunter-scout feeds and the skill's
# boards.json (skill/), unmodified; the profile is hunter-apply's profile.example.json. The Acme, Beta and Delta
# boards written below are synthetic.
source "$(dirname "$0")/lib.sh"
SF="$FIX/hunter-scout"; SK="$SF/skill"; PROF="$FIX/hunter-apply/profile.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
A() { run --skill-dir "$SK" hunter-scout/jobs "$@"; }
S() { A search --offline "$SF" --profile "$PROF" --today 2026-10-15 --out "$TMP/hunter/jobs.json" "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
d() { jq -c "$1" "$TMP/hunter/jobs.json"; }
EM=$'\xe2\x80\x94'

# ---- test_search_ranks_filters_and_writes
S --company Discord --company Ramp --company Palantir --company NVIDIA --company Netflix --company Google
eq "$CODE" 0 "search rc"
eq "$(d .as_of)" '"2026-10-15"' "as_of"
eq "$(d .scanned)" 18 "scanned"
eq "$(d .matched)" 13 "matched"
eq "$(d '[.jobs[].score] == ([.jobs[].score] | sort | reverse)')" true "scores ranked"
eq "$(d '[.jobs[].title] | index("Software Engineering Intern, Backend")')" null "excluded title word"
eq "$(d '[.jobs[].title] | index("Commercial Policy Lead")')" null "not a target title (Lead)"
eq "$(d '[.jobs[].title] | index("Machine Learning Engineer")')" null "not a target title (ML)"
eq "$(d '[.jobs[] | (["id","company","title","location","url","source","posted","score","why"] - keys) | length] | add')" 0 "public keys"
eq "$(d '[.jobs[] | has("_text") or has("_sid")] | any')" false "no private keys"
eq "$(d '.jobs[0] | keys_unsorted')" '["id","company","title","location","url","source","posted","score","why","remote","pay"]' "key order"
eq "$(d '.boards')" '["Discord","Ramp","Palantir","NVIDIA","Netflix"]' "boards"
eq "$(d '.manual[0].company')" '"Google"' "manual Google"
eq "$(d '.manual[0] | keys_unsorted')" '["company","careers","route"]' "manual keys"
has "$OUT" "Manual: Google" "Manual line"; has "$OUT" "robots.txt" "why manual"
has "$OUT" "not a prediction of an offer" "disclaimer"; hasnt "$OUT" "$EM" "no em dash"
eq "$(echo "$OUT" | head -1)" "Scanned 18 listings on 5 boards on 2026-10-15: 13 match your titles and filters." "summary line"
eq "$(d '[.jobs[] | "\(.score) \(.id)"]')" '["75 netflix-software-engineer-l6-compute-runtime-17912743","75 netflix-software-engineer-l5-ai-observability-18510397","75 nvidia-software-engineer-openshell-jr2020825","74 discord-senior-software-engineer-ads-8697883002","73 netflix-software-engineer-4-tv-web-player-18469189","71 palantir-backend-software-engineer-infrastructure-a425f5e7","71 ramp-software-engineer-backend-growth-f7fc7a27","69 nvidia-senior-software-engineer-networking-jr2015068","62 nvidia-software-engineer-nvlink-jr2025935","61 discord-senior-full-stack-software-engineer-8856467002","56 nvidia-senior-software-engineer-jr2016506","56 nvidia-senior-software-engineer-jr1998124","54 netflix-senior-software-engineer-5-validation-18581246"]' "ranking (worked by hand)"
ADS='.jobs[] | select(.id=="discord-senior-software-engineer-ads-8697883002")'
eq "$(d "$ADS | .pay")" '{"min":196000,"max":245000,"currency":"USD","period":"year"}' "Ads pay"
eq "$(d "$ADS | .posted")" '"2026-08-12"' "Ads posted (first_published)"
eq "$(d "$ADS | .url")" '"https://job-boards.greenhouse.io/discord/jobs/8697883002"' "Ads url"
eq "$(d "$ADS | [.location, .source, .remote]")" '["San Francisco Bay Area","greenhouse",false]' "Ads location"
eq "$(d "$ADS | .why")" '"title matches Software Engineer; 3 of your skills in the posting: Python, APIs, Machine learning; in San Francisco Bay Area, one of your locations; pays $196,000 to $245,000, clears your $160,000 floor; note: asks for 5+ years, you have 3; note: posted 64 days ago"' "Ads why"
has "$OUT" " 4.  74  Discord, Senior Software Engineer, Ads  (San Francisco Bay Area)  2026-08-12" "Ads line"
has "$OUT" "         https://job-boards.greenhouse.io/discord/jobs/8697883002   id: discord-senior-software-engineer-ads-8697883002" "url line"
has "$OUT" " 3.  75  NVIDIA, Software Engineer, OpenShell  (US, Remote)  30+ days ago" "posted_note shown"
has "$OUT" "Saved to $TMP/hunter/jobs.json. Add the ones you want to the pipeline: jobs.py pick <id> ..." "saved line"
eq "$(echo "$OUT" | tail -1)" "Fit scores rank listings against your profile; they are not a prediction of an offer." "last line"
eq "$(d '.jobs[] | select(.company=="Ramp") | [.pay, .posted, .location]')" '[{"min":200000,"max":310000,"currency":"USD","period":"year"},"2026-05-13","New York, NY (HQ) (+1 more)"]' "Ramp (Ashby compensation)"
eq "$(d '.jobs[] | select(.company=="Palantir") | [.location, .pay.min, .pay.max, .posted]')" '["New York, NY",135000,200000,"2025-08-06"]' "Palantir (Lever)"
eq "$(d '.jobs[] | select(.company=="Palantir") | .url | startswith("https://jobs.lever.co/palantir/")')" true "Lever url"
eq "$(d '[.jobs[] | select(.company=="NVIDIA") | .posted_note // empty]')" '["30+ days ago","30+ days ago"]' "posted_note only when 30+"

# ---- the feeds, every title kept (--any-title)
S --company Ramp --company NVIDIA --company Netflix --any-title --limit 100 --json
eq "$CODE" 0 "any-title rc"
NV() { j ".jobs[] | select(.url | endswith(\"_$1\")) | $2"; }
eq "$(NV JR2016506 .posted)" '"2026-10-15"' "Posted Today"
eq "$(NV JR2025935 .posted)" '"2026-10-14"' "Posted Yesterday"
eq "$(NV JR1998124 .posted)" '"2026-10-08"' "Posted 7 Days Ago"
eq "$(NV JR2020825 '[.posted, .posted_note, .remote]')" '["2026-09-15","30+ days ago",true]' "Posted 30+ Days Ago, US Remote"
eq "$(NV JR2016506 .location)" '"Israel, Raanana (+1 more)"' "N Locations from the path"
eq "$(NV JR2015068 '.url | startswith("https://nvidia.wd5.myworkdayjobs.com/NVIDIAExternalCareerSite/job/")')" true "Workday url"
eq "$(j '[.jobs[] | select(.company=="Netflix") | .title] | index("Senior Software Engineer 5 - Validation & Safety Platform") != null')" true "Eightfold title cleaned"
eq "$(j "[.jobs[] | select(.company==\"Netflix\") | .title | test(\"$EM|  \")] | any")" false "no em dash or double space"
eq "$(j '.jobs[] | select(.title=="Software Engineer (L6) - Compute Runtime") | [.remote, .posted]')" '[true,"2026-09-25"]' "Eightfold remote, epoch in UTC"
eq "$(j '.jobs[] | select(.title=="Software Engineer 4 - TV & Web Player Platform") | .remote')" false "remote_local is not remote"
eq "$(j '[.jobs[] | select(.title=="Software Engineering Intern, Backend")] | length')" 0 "Intern still excluded with --any-title"
eq "$(j '.jobs[] | select(.title=="Machine Learning Engineer") | [.score, .pay.max]')" '[48,330000]' "ML Engineer scored, not dropped (10+5+15+10+5+3)"

# ---- Ashby intern pay is monthly (a profile with no exclude_titles)
jq 'del(.exclude_titles)' "$PROF" > noex.json
A search --offline "$SF" --profile noex.json --today 2026-10-15 --out "" --company Ramp --json
eq "$(j '.jobs[] | select(.title=="Software Engineering Intern, Backend") | .pay')" '{"min":12500,"max":12500,"currency":"USD","period":"month"}' "intern pay monthly"
has "$(j '.jobs[] | select(.title=="Software Engineering Intern, Backend") | .why')" "pays \$12,500 to \$12,500 a month, under your \$160,000 floor" "monthly pay annualised to 150,000"

# ---- test_days_and_remote_filters
S --company Netflix --company NVIDIA --remote --json
eq "$CODE" 0 "remote rc"
eq "$(j '[.jobs[] | [.remote, .title]]')" '[[true,"Software Engineer (L6) - Compute Runtime"],[true,"Software Engineer, OpenShell"]]' "remote only"
S --company NVIDIA --days 7 --json
eq "$(j '[.jobs[].posted]')" '["2026-10-14","2026-10-15","2026-10-08"]' "days 7"
eq "$(j '.notes')" '[]' "notes key in --json"

# ---- test_dream_companies_by_default_and_unknown_company
S --json
eq "$CODE" 0 "dream rc"
eq "$(j .boards)" '["NVIDIA","Netflix"]' "dream boards"
eq "$(j '[.manual[].company] | sort')" '["Amazon","Google","Meta"]' "dream manual"
S --company "Acme Rockets" --json
eq "$CODE" 1 "nothing found rc"
eq "$(j '.notes[0]')" "\"Acme Rockets is not in boards.json: add it with --board 'Name=greenhouse:token' (or lever/ashby)\"" "unknown note"
eq "$(j '[.boards, .jobs, .matched]')" '[[],[],0]' "unknown: empty"
S --company "acme rockets" --company NETFLIX --company netflix --json
eq "$(j '[.boards, .notes]')" "[[\"Netflix\"],[\"acme rockets is not in boards.json: add it with --board 'Name=greenhouse:token' (or lever/ashby)\"]]" "company names are case-blind, once each"

# ---- test_pick_adds_roles_once
S --company Discord
FIRST=$(jq -r '.jobs[0].id' "$TMP/hunter/jobs.json")
printf '%s' '{"roles": [], "contacts": [{"id": "c1", "name": "Maya C."}], "threads": []}' > "$TMP/hunter/pipeline.json"
P() { A pick "$@" --jobs "$TMP/hunter/jobs.json" --pipeline "$TMP/hunter/pipeline.json" --today 2026-10-15; }
P "$FIRST"
eq "$CODE" 0 "pick rc"
eq "$OUT" "added $FIRST (stage: saved)" "pick output"
pp() { jq -c "$1" "$TMP/hunter/pipeline.json"; }
eq "$(pp '.roles[0] | keys_unsorted')" '["id","company","title","url","stage","applied","next_step","next_date","notes"]' "role keys"
eq "$(pp '.roles[0] | [.id, .company, .stage, .applied, .next_date]')" "[\"$FIRST\",\"Discord\",\"saved\",null,\"2026-10-15\"]" "role fields"
eq "$(pp '.roles[0].next_step')" '"Research the company (/hunter-research) and tailor the resume (/hunter-resume)"' "next step"
eq "$(pp '.roles[0].notes')" '"2026-10-15: Scout fit 74: title matches Software Engineer; 3 of your skills in the posting: Python, APIs, Machine learning; in San Francisco Bay Area, one of your locations; pays $196,000 to $245,000, clears your $160,000 floor; note: asks for 5+ years, you have 3; note: posted 64 days ago"' "role notes"
eq "$(pp '.contacts[0].name')" '"Maya C."' "other data kept"
eq "$(sed -n 2p "$TMP/hunter/pipeline.json")" '  "roles": [' "pipeline written with indent 2"
P "$FIRST" nope
eq "$CODE" 1 "pick again rc"
eq "$OUT" "skipped $FIRST: already in the pipeline
skipped nope: not in jobs.json" "skipped lines"
eq "$(pp '.roles | length')" 1 "added once"
# a pipeline that does not exist yet starts empty; the same url counts as already there
P2="$TMP/p2.json"
A pick "$FIRST" --jobs "$TMP/hunter/jobs.json" --pipeline "$P2" --today 2026-10-15
eq "$(jq -c 'keys_unsorted' "$P2")" '["roles","contacts","threads"]' "empty pipeline"
URL=$(d '.jobs[0].url')
printf '{"roles":[{"id":"other","url":%s}]}' "$URL" > "$P2"
A pick "$FIRST" --jobs "$TMP/hunter/jobs.json" --pipeline "$P2" --today 2026-10-15
eq "$CODE|$OUT" "1|skipped $FIRST: already in the pipeline" "same url"
eq "$(jq -c 'keys_unsorted' "$P2")" '["roles"]' "nothing saved when nothing is added"
A pick x --jobs "$TMP/nojobs.json"
eq "$CODE|$ERR" "2|pick: no $TMP/nojobs.json yet. Run \`jobs.py search\` first." "pick needs jobs.json"

# ---- test_needs_a_profile
A search --offline "$SF" --profile "$TMP/none.json"
eq "$CODE" 2 "no profile rc"
eq "$ERR" "search: no $TMP/none.json. Copy skills/hunter-apply/profile.example.json to hunter/profile.json and fill in your own details first." "no profile message"

# ---- test_check_offline and the boards listing
A check --offline "$SF" --company Ramp --company NVIDIA --company Netflix --company Google --company Zzz
eq "$CODE" 1 "check rc (unknown company)"
eq "$OUT" "ok  ashby ramp         3 postings
ok  workday nvidia       1731 postings
ok  eightfold netflix    154 postings
manual Google: no feed to check, search https://www.google.com/about/careers/applications/jobs/results
unknown company Zzz: add it with --board 'Name=greenhouse:token'" "check lines"
A check --offline "$SF" --company Discord --company Palantir
eq "$CODE" 1 "check rc (failures)"
eq "$OUT" "FAIL Discord greenhouse discord: offline: no file greenhouse-board-discord.json
ok  lever palantir     has postings" "check FAIL line"
A check --offline "$SF" --board "Ramp2=ashby:ramp"
eq "$CODE|$OUT" "0|ok  ashby ramp         3 postings
manual Google: no feed to check, search https://www.google.com/about/careers/applications/jobs/results
manual Meta: no feed to check, search https://www.metacareers.com/jobs
manual Amazon: no feed to check, search https://www.amazon.jobs/en/search?base_query=software+engineer" "check only the --board (manual boards still listed)"
A boards
eq "$CODE" 0 "boards rc"
eq "$(echo "$OUT" | grep -c '^auto    ')" 41 "41 automatic boards"
eq "$(echo "$OUT" | grep -c '^manual  ')" 3 "3 manual boards"
has "$OUT" "auto    NVIDIA      workday    nvidia" "auto line"
has "$OUT" "manual  Google      https://www.google.com/about/careers/applications/jobs/results" "manual line"
eq "$(echo "$OUT" | tail -1)" 'Verified 2026-10-15. Re-check any time with `jobs.py check`.' "verified line"
A boards --boards "$TMP/none.json"
eq "$CODE|$OUT" '0|Verified None. Re-check any time with `jobs.py check`.' "no boards file"

# ---- test_board_flag
S --board bad
eq "$CODE|$ERR" "2|error: --board wants Company=greenhouse|lever|ashby:token, got 'bad'" "bad --board"
S --board "Acme=workday:acme"
eq "$CODE" 2 "workday is not a --board type"
A
eq "$CODE" 2 "no command"; has "$OUT" "usage:" "help shown"

# ---- a synthetic Greenhouse board, every score worked out by hand
mkdir -p off
cat > off/greenhouse-acme.json <<'EOF'
{"jobs": [
 {"id": 101, "title": "Software Engineer, Payments", "location": {"name": "San Francisco Bay Area"}, "offices": [{"name": "SF"}, {"name": null}],
  "absolute_url": "https://x/101", "first_published": "2026-10-12T09:00:00-04:00",
  "content": "Go, Python, gRPC, Kubernetes, PostgreSQL and Kafka. 2+ years of experience. Pay $170,000 - $210,000."},
 {"id": 102, "title": "Software Engineer, Warsaw", "location": {"name": "Warsaw, Poland"}, "absolute_url": "https://x/102",
  "first_published": "2026-06-01", "content": "8+ years with Go. $120,000 to $150,000"},
 {"id": 103, "title": "Staff Software Engineer", "location": {"name": "Remote"}, "absolute_url": "https://x/103", "content": "Go"},
 {"id": 104, "title": "Software Engineer, Staffing Tools", "location": {"name": "Remote - US"}, "absolute_url": "https://x/104",
  "updated_at": "2026-10-01T00:00:00Z", "content": "good to go"},
 {"id": 105, "title": "High School Internship, Software Engineering", "location": null, "absolute_url": "https://x/105", "content": "x"},
 {"id": 106, "title": "Software Engineer (Android)", "location": {"name": "Mountain View, CA"}, "absolute_url": "https://x/106",
  "first_published": "2026-10-15", "content": "Kotlin. range is $211.4K - $290.6K"},
 {"id": 107, "title": "Backend Engineer", "location": {"name": "Chicago, IL"}, "absolute_url": "https://x/107",
  "first_published": "2026-09-01", "content": "$45 to $60 per hour"},
 {"id": 1234567890123, "title": "Infrastructure  Engineer \u2014 Platform", "location": {"name": "Seattle,\u00a0WA"}, "absolute_url": "https://x/108",
  "first_published": "2026-10-14", "content": "&lt;p&gt;competitive pay.&lt;/p&gt;&lt;ul&gt;&lt;li&gt;Kubernetes and AWS on k8s.&lt;/li&gt;&lt;/ul&gt;"},
 {"id": 109, "title": "Software Engineer, Ing\u00e9nieur", "location": {"name": "Paris, France"}, "absolute_url": "https://x/109",
  "first_published": "2026-10-13", "content": "Salaire $\u0662\u0660\u0660,\u0660\u0660\u0660 - $\u0663\u0660\u0660,\u0660\u0660\u0660"},
 {"id": 110, "title": "Senior Backend Engineer", "location": {"name": "New York, NY"}, "absolute_url": "https://x/110",
  "first_published": "2026-08-01", "content": "We use Go and Python. 3-5 years experience. $500 - $800"},
 {"id": 111, "title": "Product Designer", "location": {"name": "San Francisco Bay Area"}, "absolute_url": "https://x/111",
  "first_published": "2026-10-15", "content": "Figma"},
 {"id": 112, "title": "Software Engineer, Payments", "location": {"name": "San Francisco Bay Area"}, "absolute_url": "https://x/112",
  "first_published": "2026-10-14"}
]}
EOF
G() { A search --offline off --profile "$PROF" --today 2026-10-15 --company Acme --board "Acme=greenhouse:acme" "$@"; }
G
eq "$CODE" 0 "Acme rc"
eq "$(echo "$OUT" | head -1)" "Scanned 12 listings on 1 board on 2026-10-15: 8 match your titles and filters." "one board, singular"
AJ() { jq -c "$1" hunter/jobs.json; }
eq "$(AJ '[.jobs[] | "\(.score) \(.id)"]')" '["100 acme-software-engineer-payments-101","77 acme-infrastructure-engineer-platform-67890123","70 acme-senior-backend-engineer-110","70 acme-software-engineer-android-106","65 acme-software-engineer-staffing-tools-104","61 acme-backend-engineer-107","55 acme-software-engineer-ing-nieur-109","44 acme-software-engineer-warsaw-102"]' "Acme ranking, duplicate dropped"
W() { AJ ".jobs[] | select(.url==\"https://x/$1\") | .why"; }
eq "$(W 101)" '"title matches Software Engineer; 6 of your skills in the posting: Go, Python, gRPC, Kubernetes, PostgreSQL, Kafka; in San Francisco Bay Area, one of your locations; pays $170,000 to $210,000, clears your $160,000 floor; posted 3 days ago"' "full marks (test_score_formula)"
eq "$(W 102)" '"title matches Software Engineer; 1 of your skills in the posting: Go; note: Warsaw, Poland is outside your locations; note: asks for 8+ years, you have 3; note: pays $120,000 to $150,000, under your $160,000 floor; note: posted 136 days ago"' "every warning (test_score_formula)"
eq "$(W 104)" '"title matches Software Engineer; remote (Remote - US); posted 14 days ago; note: none of your skills appear in the posting"' "Staffing is not Staff; go is not Go"
eq "$(W 106)" '"title matches Software Engineer; in San Francisco Bay Area, one of your locations; pays $211,400 to $290,600, clears your $160,000 floor; posted today; note: none of your skills appear in the posting"' "K amounts, area match"
eq "$(W 107)" '"title matches Backend Engineer; in Chicago, IL, one of your locations; note: none of your skills appear in the posting; note: pays $45 to $60 a hour, under your $160,000 floor; note: posted 44 days ago"' "hourly pay annualised"
eq "$(W 108)" '"title matches Infrastructure Engineer; 2 of your skills in the posting: Kubernetes, AWS; in Seattle, WA, one of your locations; posted yesterday"' "double-escaped HTML, short skill names"
eq "$(W 109)" '"title matches Software Engineer; pays $200,000 to $300,000, clears your $160,000 floor; posted 2 days ago; note: none of your skills appear in the posting; note: Paris, France is outside your locations"' "Arabic-Indic digits in pay"
eq "$(W 110)" '"title matches Backend Engineer; 2 of your skills in the posting: Go, Python; in New York, NY, one of your locations; pays $500 to $800 a hour, clears your $160,000 floor; note: senior title, you have 3 years; note: posted 75 days ago"' "under 1000 is hourly; 3-5 years"
eq "$(AJ '.jobs[] | select(.url=="https://x/108") | [.title, .location, .pay]')" '["Infrastructure Engineer - Platform","Seattle, WA",null]' "title and location cleaned, no pay"
eq "$(AJ '.jobs[] | select(.url=="https://x/106") | .pay')" '{"min":211400,"max":290600,"currency":"USD","period":"year"}' "pay from text"
eq "$(AJ '.jobs[] | select(.url=="https://x/107") | .pay.period')" '"hour"' "per hour"
eq "$(AJ '.jobs[] | select(.url=="https://x/104") | [.posted, .remote]')" '["2026-10-01",true]' "updated_at fallback, remote"
grep -q '"title": "Software Engineer, Ingénieur"' hunter/jobs.json && ok || bad "jobs.json keeps UTF-8"
eq "$(echo "$OUT" | sed -n 2,4p)" " 1. 100  Acme, Software Engineer, Payments  (San Francisco Bay Area)  2026-10-12
         title matches Software Engineer; 6 of your skills in the posting: Go, Python, gRPC, Kubernetes, PostgreSQL, Kafka; in San Francisco Bay Area, one of your locations; pays \$170,000 to \$210,000, clears your \$160,000 floor; posted 3 days ago
         https://x/101   id: acme-software-engineer-payments-101" "rendered entry"
has "$OUT" "Saved to hunter/jobs.json. Add" "default --out"
G --json --out ""
has "$OUT" '"title": "Software Engineer, Ing\u00e9nieur"' "--json is ASCII"
eq "$(j '.jobs | length')" 8 "--json jobs"
G --days 30 --json; eq "$(j .matched)" 5 "--days 30"
G --min-score 70 --json; eq "$(j '[.matched, [.jobs[].score]]')" '[4,[100,77,70,70]]' "--min-score"
G --limit 2 --json; eq "$(j '[.matched, (.jobs | length)]')" '[8,2]' "--limit"
G --limit -1 --json; eq "$(j '[.jobs[-1].id]')" '["acme-software-engineer-ing-nieur-109"]' "--limit -1 is ranked[:-1]"
G --limit 0; eq "$CODE" 1 "--limit 0 finds nothing to show"; has "$OUT" "No listing passed the filters. Widen --title, --location or --days, or add boards." "empty message"
G --remote --json; eq "$(j '[.jobs[].id]')" '["acme-software-engineer-staffing-tools-104"]' "--remote"
G --location "Seattle, WA" --json
eq "$(j '[.jobs[] | [.id, .why]]')" '[["acme-infrastructure-engineer-platform-67890123","title matches Infrastructure Engineer; 2 of your skills in the posting: Kubernetes, AWS; in Seattle, WA, one of your locations; posted yesterday"],["acme-software-engineer-staffing-tools-104","title matches Software Engineer; remote (Remote - US); posted 14 days ago; note: none of your skills appear in the posting"]]' "--location replaces the profile places"
G --title "Backend Engineer" --json; eq "$(j '[.jobs[].id]')" '["acme-senior-backend-engineer-110","acme-backend-engineer-107"]' "--title"
G --any-title --json
eq "$(j '.matched')" 9 "--any-title keeps the designer"
eq "$(j '.jobs[] | select(.url=="https://x/111") | [.score, .why]')" '[32,"in San Francisco Bay Area, one of your locations; posted today; note: none of your skills appear in the posting"]' "no title points"
G --today 2026-13-01; eq "$CODE" 1 "a bad --today date fails like Python"
G --today soon; eq "$CODE" 1 "an unparsable --today fails like Python"

# ---- Greenhouse details: newest title matches first, --details caps them (Python slice)
cat > off/greenhouse-beta.json <<'EOF'
{"jobs": [
 {"id": 201, "title": "Software Engineer A", "absolute_url": "https://b/201", "first_published": "2026-10-10"},
 {"id": 202, "title": "Software Engineer B", "absolute_url": "https://b/202", "first_published": "2026-10-12"},
 {"id": 203, "title": "Software Engineer C", "absolute_url": "https://b/203"},
 {"id": 204, "title": "Staff Software Engineer", "absolute_url": "https://b/204", "first_published": "2026-10-13"}
]}
EOF
for n in 201 202 203 204; do printf '{"id": %s, "title": "ignored", "content": "&lt;p&gt;Kubernetes. $300,000 - $400,000&lt;/p&gt;"}' "$n" > "off/greenhouse-beta-$n.json"; done
B() { A search --offline off --profile "$PROF" --today 2026-10-15 --company Beta --board "Beta=greenhouse:beta" --out "" --json "$@"; }
BP='[.jobs[] | [.title, .pay.max, .location]] | sort'
B; eq "$(j "$BP")" '[["Software Engineer A",400000,"not listed"],["Software Engineer B",400000,"not listed"],["Software Engineer C",400000,"not listed"]]' "details fetched"
B --details 1; eq "$(j "$BP")" '[["Software Engineer A",null,"not listed"],["Software Engineer B",400000,"not listed"],["Software Engineer C",null,"not listed"]]' "--details 1: the newest"
B --details -1; eq "$(j "$BP")" '[["Software Engineer A",400000,"not listed"],["Software Engineer B",400000,"not listed"],["Software Engineer C",null,"not listed"]]' "--details -1: all but the undated"
B --details 0; eq "$(j '[.jobs[].pay] | unique')" '[null]' "--details 0"
eq "$(j '.jobs[] | select(.title=="Software Engineer A") | [.score, .why]')" '[62,"title matches Software Engineer; posted 5 days ago; note: the feed has no description, skills not checked: open the posting; note: not listed is outside your locations"]' "no description"
rm off/greenhouse-beta-202.json
B --details 1; eq "$(j "$BP")" '[["Software Engineer A",null,"not listed"],["Software Engineer B",null,"not listed"],["Software Engineer C",null,"not listed"]]' "a missing detail is skipped quietly"
eq "$(j .notes)" '[]' "no note for a missing detail"

# ---- Lever: salaryRange, epoch in milliseconds (number or string), and a missing feed
cat > off/lever-delta.json <<'EOF'
[{"id": "d-1", "text": "Software Engineer", "categories": {"location": "Remote"}, "hostedUrl": "https://jobs.lever.co/delta/d-1",
  "createdAt": 1760486400000, "descriptionPlain": "Python",
  "salaryRange": {"min": 150000, "max": 170000, "currency": "USD", "interval": "per-year-salary"}},
 {"id": "d-2", "text": "Backend Engineer", "categories": {"location": "Austin, TX"}, "hostedUrl": "https://jobs.lever.co/delta/d-2",
  "createdAt": "1760486400000", "workplaceType": "onsite", "descriptionPlain": "Java",
  "salaryRange": {"min": 70, "max": 90, "currency": "USD", "interval": "per-hour-wage"}}]
EOF
L() { A search --offline off --profile "$PROF" --today 2026-10-15 --out "" --json "$@"; }
L --company Delta --board "Delta=lever:delta"
eq "$(j '[.jobs[] | [.score, .id, .posted, .remote, .pay]]')" '[[71,"delta-software-engineer-d1","2025-10-15",true,{"min":150000,"max":170000,"currency":"USD","period":"year"}],[64,"delta-backend-engineer-d2","2025-10-15",false,{"min":70,"max":90,"currency":"USD","period":"hour"}]]' "Lever jobs"
eq "$(j '[.jobs[].why]')" '["title matches Software Engineer; 1 of your skills in the posting: Python; remote (Remote); pays $150,000 to $170,000, clears your $160,000 floor; note: posted 365 days ago","title matches Backend Engineer; 1 of your skills in the posting: Java; in Austin, TX (you are open to relocating); pays $70 to $90 a hour, clears your $160,000 floor; note: posted 365 days ago"]' "Lever why (relocation by state code)"
L --company Gamma --board "Gamma=lever:gamma"
eq "$CODE" 1 "missing feed rc"
eq "$(j .notes)" '["Gamma (lever gamma): offline: no file lever-gamma.json"]' "missing feed note"
A search --offline off --profile "$PROF" --today 2026-10-15 --out "" --company Gamma --board "Gamma=lever:gamma"
has "$OUT" "note: Gamma (lever gamma): offline: no file lever-gamma.json" "note line"
printf '{not json' > off/ashby-broken.json
L --company Broken --board "Broken=ashby:broken"
eq "$CODE" 1 "a broken offline file fails like Python"

# ---- online: no network in the suites, so the fetch error becomes a note
A search --profile "$PROF" --today 2026-10-15 --out "" --company Delta --board "Delta=lever:delta" --json
eq "$CODE" 1 "offline engine rc"
has "$(j '.notes[0]')" "Delta (lever delta): could not reach https://api.lever.co/v0/postings/delta?mode=json: " "network error note"

done_ "hunter-scout"

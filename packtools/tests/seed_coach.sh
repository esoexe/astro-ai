#!/usr/bin/env bash
# seed-coach/coach: the assertions of seed-skill/tests/test_coach.py, run against the C++ tool, plus hand-worked edge
# cases (money rounding and suffixes, dict.get presence, format escapes, the DST boundary, folded invite lines).
# seed/ is the synthetic Leashly folder from the upstream tests (fictional).
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
CF="$FIX/seed/seed-coach"; SKD="$CF/skill"; BANKF="$SKD/questions.json"
C() { run --skill-dir "$SKD" seed-coach/coach "$@"; }
STRONG="15% flat, no owner fee. We keep \$3.75 of every \$25 walk and \$2.23 after card fees and the backup walker. Rover keeps 20% of sitter bookings and adds an owner fee."
mkdir -p "$TMP/w"; cp -r "$CF/seed" "$TMP/w/"; cd "$TMP/w" || exit 1

# ---- the bank
eq "$(jq -c .categories "$BANKF")" '["market","competition","traction","unit economics","team","why now","risks","the ask"]' "categories"
eq "$(jq '[.questions[].id]|length == (unique|length)' "$BANKF")" "true" "ids unique"

# ---- the reel questions (Tailoring.test_the_reel_questions, Cli round trip)
C drill --dir seed --customers owners --provider walker; eq "$CODE" 0 "drill"
has "$OUT" " 2. [competition] Rover has 515,771 ratings. Why will owners switch?" "reel line 2"
has "$OUT" "What does your 15% take rate leave after walker pay?" "take rate question"
eq "$(head -1 <<<"$OUT")" "Pitch drill: 10 questions, 10 tailored to your numbers" "drill header"
has "$OUT" "    Your numbers: Per walk: \$25 price, you keep \$3.75, \$2.23 after costs." "ue-take hint (2.225 -> \$2.23 half up)"
has "$OUT" "    They are checking: whether your edge is big enough to beat habit. Strong: the pain, with evidence, and the moment someone switches." "checking line"
eq "$(tail -1 <<<"$OUT")" 'Answer out loud in under 60 seconds, then: coach.py grade --answer "..." --question ID' "drill footer"
[ -e seed/coach ] && bad "nothing written without --write" || ok
C drill --dir seed --customers owners --provider walker --json
eq "$(jq '[.questions[].tailored]|all' <<<"$OUT")" "true" "all tailored"
eq "$(jq '[.questions[:5][].category]|unique|length' <<<"$OUT")" "5" "one per category before seconds"
eq "$(jq -r '.questions[]|select(.id=="ue-take").hint' <<<"$OUT")" 'Per walk: $25 price, you keep $3.75, $2.23 after costs.' "json hint"
eq "$(jq -c '.questions[0]|keys_unsorted' <<<"$OUT")" '["id","category","q","tests","strong","tailored","hint"]' "item key order"
eq "$(jq -r '.questions[]|select(.id=="market-som").q' <<<"$OUT")" "Your SOM is 2.5% of a \$960M SAM in 3 years. What gets you the first 1,000 customers?" "som question"
eq "$(jq -r '.questions[]|select(.id=="market-som").hint' <<<"$OUT")" 'TAM $17.04B, SAM $960M, SOM $24M.' "17040000000 -> \$17.04B"
eq "$(jq -r '.questions[]|select(.id=="ue-ltv").hint' <<<"$OUT")" 'CAC $60, LTV $254.29, paid back in 3.37 months.' "ltv hint"
eq "$(jq -r '.questions[]|select(.id=="ue-breakeven").hint' <<<"$OUT")" 'Peak burn $82,801; year 1 revenue $467,400, year 3 $2.99M.' "breakeven hint"
eq "$(jq -r '.questions[]|select(.id=="ask-funds").q' <<<"$OUT")" 'You are raising $1.5M. Walk me through where it goes and what it gets you.' "raise"
eq "$(jq -r '.questions[]|select(.id=="ask-funds").hint' <<<"$OUT")" 'Biggest line: Product and engineering ($600,000).' "top fund"
eq "$(jq -r '.questions[]|select(.id=="market-assumption").q' <<<"$OUT")" 'Your market size rests on an assumption (share that pays for dog walking: 10%). Where does that number come from?' "assumption"
C drill --dir seed; has "$OUT" "Why will users switch?" "customers default users"
has "$OUT" "What does your 15% take rate leave after costs?" "ue-take still asked without a provider"; eq "$CODE" 0 "drill default code"
C drill --dir seed --n 50 --json; eq "$(jq -r '.questions[]|select(.id=="ue-take").q' <<<"$OUT")" "What does your 15% take rate leave after costs?" "provider_pay default costs"
eq "$(jq '.questions|length' <<<"$OUT")" "$(jq '[.questions[]|select(.id!="ask-investor")]|length' "$BANKF")" "every question but the investor one"
eq "$(jq '[.questions[]|select(.tailored)]|length' <<<"$OUT")" "10" "ten tailored without an investor"

# ---- the investor (context with maya-chen)
C drill --dir seed --investor maya-chen --category "the ask" --json
eq "$(jq -r '.questions[]|select(.id=="ask-investor").q' <<<"$OUT")" "Maya's record: led the second of her two pet-care seed rounds. How is Leashly different, and do you want Northlight Ventures to lead?" "investor question"
eq "$(jq -r '.questions[]|select(.id=="ask-investor").hint' <<<"$OUT")" 'Maya Chen writes $250,000-$1M checks.' "inv_check"
eq "$(jq -r '[.questions[].category]|unique|join(",")' <<<"$OUT")" "the ask" "category filter"
C drill --dir seed --investor "MAYA CHEN" --category "the ask"; eq "$CODE" 0 "investor by name, any case"
eq "$(head -1 <<<"$OUT")" "Pitch drill for Maya Chen (Northlight Ventures): 5 questions, 2 tailored to your numbers" "header for an investor"
has "$OUT" "Maya's record:" "record line"

# ---- missing files fall back to generic
mkdir -p "$TMP/e"; C drill --dir "$TMP/e" --n 8 --json; eq "$CODE" 0 "empty dir"
eq "$(jq -c '[.questions[].category]' <<<"$OUT")" "$(jq -c .categories "$BANKF")" "one generic per category, in order"
eq "$(jq '[.questions[].tailored]|any' <<<"$OUT")" "false" "nothing tailored"
eq "$(jq '[.questions[].hint]|unique' -c <<<"$OUT")" "[null]" "no hints"
C drill --dir "$TMP/e" --n 3 --json; eq "$(jq -r '[.questions[].id]|join(",")' <<<"$OUT")" "market-1,comp-1,traction-1" "first three generic in bank order"

# ---- --seed shuffles repeatably within a category
C drill --dir seed --n 5 --category team --seed 7 --json; A1=$OUT
C drill --dir seed --n 5 --category team --seed 7 --json; eq "$OUT" "$A1" "seed repeatable"
eq "$(jq -r '[.questions[].category]|unique|join(",")' <<<"$A1")" "team" "all team"
eq "$(jq -r '[.questions[].id]|sort|join(",")' <<<"$A1")" "$(jq -r '[.questions[]|select(.category=="team").id][:5]|sort|join(",")' "$BANKF")" "a shuffle of the team questions"
C drill --dir seed --n 50 --seed 3 --json; S3=$OUT
eq "$(jq '[.questions[:10][].tailored]|all' <<<"$S3")" "true" "seeded: tailored still first"
eq "$(jq -r '[.questions[].id]|sort|join(",")' <<<"$S3")" "$(C drill --dir seed --n 50 --json; jq -r '[.questions[].id]|sort|join(",")' <<<"$OUT")" "seeded: same set"
C drill --dir seed --category vibes; eq "$CODE" 2 "bad category"; eq "$ERR" "coach.py: --category is one of: market, competition, traction, unit economics, team, why now, risks, the ask" "category message"

# ---- fill: every placeholder or nothing, {{ }} escapes (a custom --bank)
cat > "$TMP/bank.json" <<'EOF'
{"categories": ["x", "y"], "questions": [
 {"id": "x1", "category": "x", "q": "{{literal}} {startup} at {customers}", "tests": "t", "strong": "s", "hint": "{nope} hint"},
 {"id": "x2", "category": "x", "q": "Needs {nope}", "tests": "t", "strong": "s"},
 {"id": "x3", "category": "x", "q": "Plain}} and {{", "tests": null, "strong": "s"},
 {"id": "y1", "category": "y", "q": "Y {startup}", "tests": "t", "strong": "s", "hint": "{provider_pay}"},
 {"id": "z1", "category": "z", "q": "never", "tests": "t", "strong": "s"}]}
EOF
C drill --dir seed --bank "$TMP/bank.json" --json; eq "$CODE" 0 "custom bank"
eq "$(jq -c '[.questions[]|[.id,.q,.tailored,.hint]]' <<<"$OUT")" '[["x1","{literal} Leashly at users",true,null],["y1","Y Leashly",true,"costs"],["x3","Plain} and {",false,null]]' "fill, escapes, a missing hint"
C drill --dir seed --bank "$TMP/bank.json"; has "$OUT" " 3. [x] Plain} and {" "generic last"; has "$OUT" "    They are checking: None. Strong: s." "None prints as None"
echo '{"categories": ["x"], "questions": []}' > "$TMP/nob.json"; C drill --dir seed --bank "$TMP/nob.json"; eq "$CODE" 2 "empty bank"; eq "$ERR" "coach.py: no question bank at $TMP/nob.json" "bank message"
echo '{"categories": ["x"], "questions": [' > "$TMP/badb.json"; C drill --dir seed --bank "$TMP/badb.json"; eq "$CODE" 2 "bad bank JSON"; has "$ERR" "coach.py: $TMP/badb.json is not valid JSON: " "JSON message"
echo '{"categories": ["q"], "questions": [{"id": "q1", "category": "q", "q": "x", "tests": "t", "strong": "s"}]}' > "$TMP/qb.json"
C drill --dir seed --bank "$TMP/qb.json" --category x; eq "$CODE" 2 "category not in a custom bank"
echo '{"categories": ["q"], "questions": [{"id": "q1", "category": "r", "q": "x", "tests": "t", "strong": "s"}]}' > "$TMP/qn.json"
C drill --dir seed --bank "$TMP/qn.json"; eq "$CODE" 1 "nothing to ask"; eq "$OUT" "No questions to ask." "nothing message"

# ---- money, num and dict.get presence (a crafted seed folder)
mkdir -p "$TMP/m/seed"
cat > "$TMP/m/seed/config.json" <<'EOF'
{"startup": {"name": "Zed", "raise": "about 2M"}}
EOF
cat > "$TMP/m/seed/model.json" <<'EOF'
{"summary": {"breakeven_month": 0, "peak_burn": 0, "revenue_y1": 1000000000, "revenue_y3": 1000000.0, "raise": 2000000,
             "use_of_funds": {"A": 5, "B": "7", "C": 7.0}},
 "unit_economics": {"price": "12", "revenue_per_order": 999999.995, "contribution_per_order": -5.5, "cac": null, "ltv": 10.0,
                    "ltv_cac": 3, "cac_payback_months": 2.5},
 "assumptions": {"take_rate_pct": 12.345, "unit": "ride", "price": 99, "cac": 7}}
EOF
cat > "$TMP/m/seed/market.json" <<'EOF'
{"tam": 5e9, "sam": 1234567.891, "som": 999.999, "som_share_pct": 0.5, "som_years": 5,
 "inputs": [{"kind": "assumption", "level": "som", "name": "no", "value": 1}, {"kind": "input", "level": "sam", "name": "no", "value": 2},
            {"kind": "assumption", "level": "sam", "name": "share", "value": 33.333, "unit": "%"}]}
EOF
cat > "$TMP/m/seed/competitors.json" <<'EOF'
{"apps": [{"name": "Small", "ratings": null, "weak_point": "x", "weak_point_count": 1, "low_reviews": 2},
          {"name": "Big", "ratings": 2000.7, "weak_point": "bugs", "top_theme": "bugs", "themes": {"bugs": 12}, "low_reviews": 40},
          {"name": "Mid", "ratings": 1500, "weak_point": "price", "weak_point_count": 3, "low_reviews": 9}]}
EOF
C drill --dir "$TMP/m/seed" --n 50 --json; eq "$CODE" 0 "crafted drill"; M=$OUT
qh() { jq -r --arg i "$1" '.questions[]|select(.id==$i)|"\(.q) | \(.hint)"' <<<"$M"; }
eq "$(qh ue-take)" 'What does your 12.35% take rate leave after costs? | Per ride: $12 price, you keep $1,000,000.00, $-5.50 after costs.' "take from assumptions (12.345 is a hair above, so 12.35), unit fallback, price present wins, half-up carry, negative"
eq "$(qh ue-ltv)" 'Your LTV:CAC is 3x. What CAC did you assume, and what have you actually paid? | null' "cac: a present null does not fall back"
eq "$(qh ue-breakeven)" 'You break even in month 0. What has to be true for that? | null' "0 is kept, peak burn 0 is not"
eq "$(qh risk-runway)" '' "no peak burn, no runway question"
eq "$(qh ask-funds)" 'You are raising about 2M. Walk me through where it goes and what it gets you. | Biggest line: B ($7).' "raise kept as text; first max by float"
eq "$(qh market-som)" 'Your SOM is 0.5% of a $1.23M SAM in 5 years. What gets you the first 1,000 customers? | TAM $5B, SAM $1.23M, SOM $1,000.00.' "market money"
eq "$(qh market-assumption)" 'Your market size rests on an assumption (share: 33.33%). Where does that number come from? | Every assumption is listed by market.py show.' "first tam/sam assumption"
eq "$(qh comp-switch)" 'Big has 2,000 ratings. Why will users switch? | 12 of 40 recent 1-2 star Big reviews mention bugs.' "sorted by ratings, int() truncates, count from themes"
eq "$(qh comp-second)" 'Mid users complain about price. How do you make sure that never happens to you? | 3 of 9 recent 1-2 star Mid reviews mention price.' "second app"
echo '{"startup": {"name": "Zed", "raise": "$1,500,000"}}' > "$TMP/m/seed/config.json"
C drill --dir "$TMP/m/seed" --n 50 --json; eq "$(jq -r '.questions[]|select(.id=="ask-funds").q' <<<"$OUT")" 'You are raising $1.5M. Walk me through where it goes and what it gets you.' "raise text parsed"
echo '{"startup": {"name": "Zed", "raise": 0}}' > "$TMP/m/seed/config.json"
C drill --dir "$TMP/m/seed" --n 50 --json; eq "$(jq -r '.questions[]|select(.id=="ask-funds").q' <<<"$OUT")" 'You are raising $0. Walk me through where it goes and what it gets you.' "raise 0"
echo '{"startup": {"name": "Zed"}}' > "$TMP/m/seed/config.json"
C drill --dir "$TMP/m/seed" --n 50 --json; eq "$(jq -r '.questions[]|select(.id=="ask-funds").q' <<<"$OUT")" 'You are raising $2M. Walk me through where it goes and what it gets you.' "raise from the model"
cat > "$TMP/m/seed/model.json" <<'EOF'
{"unit_economics": {"take_rate_pct": null, "price": 1000000, "revenue_per_order": 1500000000, "contribution_per_order": 0.005, "unit": "u"},
 "assumptions": {"take_rate_pct": 20}}
EOF
C drill --dir "$TMP/m/seed" --n 50 --json; eq "$(jq '[.questions[]|select(.id=="ue-take")]|length' <<<"$OUT")" "0" "take: a present null does not fall back"
echo '{"startup": {"name": "Zed"}}' > "$TMP/m/seed/config.json"
cat > "$TMP/bank2.json" <<'EOF'
{"categories": ["x"], "questions": [{"id": "x", "category": "x", "q": "{price}|{revenue_per_order}|{contribution_per_order}", "tests": "t", "strong": "s"}]}
EOF
C drill --dir "$TMP/m/seed" --bank "$TMP/bank2.json" --json; eq "$(jq -r '.questions[0].q' <<<"$OUT")" '$1M|$1.5B|$0.01' "1e6 -> \$1M, 1.5e9 -> \$1.5B, 0.005 -> \$0.01"

# ---- grade (Grade tests)
C grade --answer "$STRONG" --json; eq "$CODE" 0 "strong"; eq "$(jq -c '[.score,.passed,.words,.seconds,.question]' <<<"$OUT")" "[100,true,32,13,null]" "strong numbers"
eq "$(jq -c '.|keys_unsorted' <<<"$OUT")" '["score","passed","words","seconds","checks","question"]' "grade key order"
C grade --answer "I think we could maybe disrupt the market" --json; eq "$CODE" 1 "weak"
eq "$(jq .score <<<"$OUT")" "20" "weak score"
eq "$(jq -r '.checks[]|select(.check=="no hedging").note' <<<"$OUT")" "hedging: i think, maybe. Say it straight" "hedges"
eq "$(jq -r '.checks[]|select(.check=="specific").note' <<<"$OUT")" "buzzwords: disrupt" "buzz"
eq "$(jq -r '.checks[]|select(.check=="a number")|"\(.ok) \(.note)"' <<<"$OUT")" "false no number: add the one that proves it" "no number"
eq "$(jq -r '.checks[]|select(.check=="length").note' <<<"$OUT")" "8 words: too thin, give the reason and one number" "thin"
LONG=$(for i in $(seq 30); do printf 'Rover charges 20%% and we charge 15%%. '; done)
C grade --answer "$LONG" --json; eq "$(jq -r '.checks[]|select(.check=="length")|"\(.ok) \(.note)"' <<<"$OUT")" "false 210 words, about 84 seconds: over a minute, cut it to 150 words" "too long"
C grade --answer "Well so the thing about our company and how we got here and what we learned along the way over many months of work with our customers is that we charge 15% to Rover users." --json
eq "$(jq -r '.checks[]|select(.check=="answer first")|"\(.ok) \(.note)"' <<<"$OUT")" "false the first sentence is 35 words: lead with the answer in under 25" "rambling"
C grade --answer "We charge 15% to owners in Austin and Denver, and 340 of them booked twice in the first month."; eq "$CODE" 0 "specific answer passes"
C grade --answer "Mighty Paws pays 15% to walkers in Austin, Denver and Boise every single week, and 40 of them stayed a year." --json
eq "$(jq -r '.checks[]|select(.check=="no hedging").ok' <<<"$OUT")" "true" "Mighty is not might"
C grade --answer "We might grow. Perhaps." --json; eq "$(jq -r '.checks[]|select(.check=="no hedging").note' <<<"$OUT")" "hedging: might, perhaps. Say it straight" "hedges in bank order"
C grade --answer "I am here. I run it." --json; eq "$(jq -r '.checks[]|select(.check=="specific").note' <<<"$OUT")" "only 0 specifics: name the customer, the competitor, the number" "first token and I are not names"
C grade --answer "We sell to Ann, I'm sure; Bob too." --json; eq "$(jq -r '.checks[]|select(.check=="specific").note' <<<"$OUT")" "3 specifics (numbers and names)" "I'm counts, punctuation stripped"
C grade --answer "   "; eq "$CODE" 2 "blank answer"; eq "$ERR" 'coach.py: grade needs an answer: --answer "..." or --file FILE' "blank message"
C grade; eq "$CODE" 2 "no answer"
C grade --answer "maybe soon"; eq "$CODE" 1 "fails"; has "$OUT" "Fix the unchecked lines" "fix line"
eq "$(sed -n 2p <<<"$OUT")" "  [ ] length        2 words: too thin, give the reason and one number" "check row format"
eq "$(sed -n 6p <<<"$OUT")" "  [x] answer first  the first sentence is 2 words" "%-13s of a 12-char name"
C grade --answer "$STRONG" --question ue-take; eq "$CODE" 0 "with question"
eq "$(sed -n 1,3p <<<"$OUT")" "Question ue-take: What does your {take_rate} take rate leave after {provider_pay}?
  A strong answer has: revenue and contribution per order, in dollars.
Score 100/100" "question header (raw template, as upstream)"
eq "$(tail -1 <<<"$OUT")" "Strong. Say it out loud twice more." "strong footer"
C grade --answer "$STRONG" --question nope; eq "$CODE" 2 "bad question"; eq "$ERR" "coach.py: no question 'nope' in the bank" "question message"
printf '%s' "$STRONG" > "$TMP/a.txt"; C grade --file "$TMP/a.txt" --json; eq "$CODE $(jq .score <<<"$OUT")" "0 100" "grade a file"
C grade --file "$TMP/none.txt"; eq "$CODE" 2 "missing file"; eq "$ERR" "coach.py: no file $TMP/none.txt" "file message"
C grade --file "$TMP"; eq "$CODE" 2 "a directory is no file"
C grade --answer "Ünïcode 15% Zürich and Österreich, Ünder 40." --json; eq "$(jq -r '.checks[]|select(.check=="specific").note' <<<"$OUT")" "5 specifics (numbers and names)" "Unicode capitals"
eq "$(jq .words <<<"$OUT")" "9" "ASCII-only word pattern splits at accented letters"

# ---- agenda (Agenda and Cli tests)
C agenda --dir seed --investor maya-chen --write; eq "$CODE" 0 "agenda"
eq "$(sed -n 1,2p <<<"$OUT")" "First call: Maya Chen, Partner, Northlight Ventures (20 minutes)
When: Thu Nov 5 2026, 10:00 AM ET, https://meet.google.com/abc-defg-hij" "agenda head"
has "$OUT" " 0-2  min  Hello and why them" "first block"
has "$OUT" "   2-5  min  The problem        Rover: 106 of 341 recent 1-2 star reviews mention fees and pricing. Wag!: 81 of 324 recent 1-2 star reviews mention cancellations and no-shows." "problem block"
has "$OUT" "  19-20 min  Next step          Ask for the next step and a date" "last block"
eq "$(tail -2 <<<"$OUT" | head -1)" "Before: coach.py drill --investor maya-chen, answered out loud. After: log it in /seed-closer and follow up within 24 hours." "before line"
eq "$(tail -1 <<<"$OUT")" "Wrote seed/coach/agenda-maya-chen.md." "wrote"
[ -f seed/coach/agenda-maya-chen.md ] && ok || bad "agenda file"
eq "$(head -3 seed/coach/agenda-maya-chen.md)" "# First call: Maya Chen, Partner, Northlight Ventures (20 minutes)

- When: Thu Nov 5 2026, 10:00 AM ET, https://meet.google.com/abc-defg-hij" "agenda md"
has "$(cat seed/coach/agenda-maya-chen.md)" "- 0-2  min  Hello and why them" "md rows stripped"
C agenda --dir seed --investor maya-chen --json; AG=$OUT
eq "$(jq -c '[.blocks[].minutes]' <<<"$AG")" "[2,3,4,4,4,2,1]" "20-minute blocks"
eq "$(jq '.blocks[-1].end' <<<"$AG")" "20" "ends at 20"
eq "$(jq -c '[.name,.firm,.status,.investor,.minutes]' <<<"$AG")" '["Maya Chen","Northlight Ventures","booked","maya-chen",20]' "agenda fields"
eq "$(jq -c '.|keys_unsorted' <<<"$AG")" '["investor","name","firm","role","minutes","call","status","why","blocks"]' "agenda key order"
eq "$(jq '.why|length' <<<"$AG")" "3" "three reasons"
has "$AG" "Saw you led two pet-care seed rounds" "personal line"
has "$AG" "Rover: 106 of 341 recent 1-2 star reviews mention fees and pricing." "problem"
has "$AG" "TAM \$17.04B, SAM \$960M, SOM \$24M" "market"
has "$AG" "\$1.5M post-money SAFE" "ask"
eq "$(jq -r '.blocks[3].plan' <<<"$AG")" 'TAM $17.04B, SAM $960M, SOM $24M; 15% take rate, $2.23 contribution a walk, LTV:CAC 4.24x, breakeven in month 29' "market and model"
eq "$(jq -r '.blocks[5].plan' <<<"$AG")" '$1.5M post-money SAFE; the biggest use: Product and engineering ($600,000). Ask who else they would want in the round.' "ask block"
eq "$(jq -r '.blocks[4].plan' <<<"$AG")" "Let them ask. Be ready for: $(jq -r '[.questions[:3][].q]|join(" / ")' <<<"$(C drill --dir seed --n 3 --investor maya-chen --json; echo "$OUT")")" "likely questions are pick(ctx, 3)"
C agenda --dir seed --investor nobody; eq "$CODE" 1 "no investor"; eq "$ERR" "coach.py: no investor 'nobody' in seed/investors.json" "investor message"
C agenda --dir "$TMP/empty" --investor maya-chen; eq "$CODE" 1 "no investors file"; eq "$ERR" "coach.py: no $TMP/empty/investors.json: rank investors with /seed-investors first" "investors file message"
C agenda --dir seed --investor maya-chen --minutes 5; eq "$CODE" 2 "minutes too few"; eq "$ERR" "coach.py: --minutes is 10 to 60" "minutes message"
C agenda --dir seed --investor maya-chen --minutes 61; eq "$CODE" 2 "minutes too many"
C agenda --dir seed --investor nobody --minutes 5; eq "$CODE" 1 "the investor is checked before the minutes"
for m in 10 11 13 30 60; do C agenda --dir seed --investor maya-chen --minutes $m --json; MS[$m]=$(jq -c '[.blocks[].minutes]' <<<"$OUT"); done
eq "${MS[10]}" "[1,1,2,2,2,1,1]" "10"; eq "${MS[11]}" "[1,2,2,2,2,1,1]" "11"; eq "${MS[13]}" "[1,2,3,2,2,1,2]" "13"
eq "${MS[30]}" "[3,5,6,6,6,3,1]" "30"; eq "${MS[60]}" "[6,9,12,12,12,6,3]" "60"
okall=1; for m in $(seq 10 60); do C agenda --dir seed --investor maya-chen --minutes $m --json; [ "$(jq "[.blocks[].minutes]|(add == $m) and all(. >= 1) and (.[-1] as \$x|true)" <<<"$OUT")" = true ] || okall=0; [ "$(jq '.blocks[-1].end' <<<"$OUT")" = "$m" ] || okall=0; done
eq "$okall" 1 "10..60 always add up"

# ---- the invite: Z times in New York time, TZID kept, folded lines, no invite
mkdir -p "$TMP/i/seed/calls"; cp seed/investors.json seed/config.json "$TMP/i/seed/"
ics() { printf "$2" > "$TMP/i/seed/calls/maya-chen.ics"; C agenda --dir "$TMP/i/seed" --investor maya-chen; eq "$(sed -n 2p <<<"$OUT")" "$3" "$1"; }
ics "the clocks go back: 06:30 UTC is 1:30 AM EST" 'BEGIN:VEVENT\nDTSTART:20261101T063000Z\nEND:VEVENT\n' "When: Sun Nov 1 2026, 1:30 AM ET"
ics "05:30 UTC is still 1:30 AM EDT" 'BEGIN:VEVENT\nDTSTART:20261101T053000Z\nEND:VEVENT\n' "When: Sun Nov 1 2026, 1:30 AM ET"
ics "spring forward: 07:00 UTC is 3:00 AM EDT" 'DTSTART:20260308T070000Z\n' "When: Sun Mar 8 2026, 3:00 AM ET"
ics "06:59 UTC is 1:59 AM EST" 'DTSTART:20260308T065900Z\n' "When: Sun Mar 8 2026, 1:59 AM ET"
ics "midnight UTC is the evening before" 'DTSTART:20270101T000000Z\n' "When: Thu Dec 31 2026, 7:00 PM ET"
ics "noon and minutes" 'DTSTART:20260704T162500Z\n' "When: Sat Jul 4 2026, 12:25 PM ET"
ics "TZID kept as the label" 'BEGIN:VEVENT\nDTSTART;TZID=America/New_York:20261105T100000\nEND:VEVENT\n' "When: Thu Nov 5 2026, 10:00 AM (America/New_York)"
ics "a floating time" 'DTSTART;VALUE=DATE-TIME:20261105T0930\n' "When: Thu Nov 5 2026, 9:30 AM"
ics "a folded LOCATION" 'DTSTART:20261105T150000Z\r\nLOCATION:Zoom https://zoom.exa\r\n mple/j/123, room 4\r\n' "When: Thu Nov 5 2026, 10:00 AM ET, https://zoom.example/j/123"
ics "links only from URL, LOCATION, DESCRIPTION" 'SUMMARY:see http://no.example\nDESCRIPTION;LANGUAGE=en:Join http://yes.example/a;b\n' "When: see the invite, http://yes.example/a"
ics "a date without a time: no when" 'DTSTART;VALUE=DATE:20261105\nURL:https://u.example\n' "When: see the invite, https://u.example"
printf 'NOTHING\n' > "$TMP/i/seed/calls/maya-chen.ics"; C agenda --dir "$TMP/i/seed" --investor maya-chen
eq "$(sed -n 2p <<<"$OUT")" "Not booked yet: /seed-closer books the call and writes the invite." "empty invite, no pipeline"
cp seed/pipeline.json "$TMP/i/seed/"; C agenda --dir "$TMP/i/seed" --investor maya-chen; eq "$(sed -n 2p <<<"$OUT" | cut -c1-9)" "Why them:" "booked without an invite: no when line"
rm "$TMP/i/seed/calls/maya-chen.ics"; C agenda --dir "$TMP/i/seed" --investor maya-chen --json; eq "$(jq -c '[.call,.status]' <<<"$OUT")" '[null,"booked"]' "no invite file"
printf 'DTSTART:20261305T150000Z\n' > "$TMP/i/seed/calls/maya-chen.ics"; C agenda --dir "$TMP/i/seed" --investor maya-chen; [ "$CODE" -ne 0 ] && ok || bad "month 13 is not a date"
printf 'DTSTART:20260230T150000Z\n' > "$TMP/i/seed/calls/maya-chen.ics"; C agenda --dir "$TMP/i/seed" --investor maya-chen; [ "$CODE" -ne 0 ] && ok || bad "Feb 30 is not a date"
rm "$TMP/i/seed/calls/maya-chen.ics"

# ---- an investor without a personal line, firm or competitors: the generic plans
cat > "$TMP/i/seed/investors.json" <<'EOF'
{"investors": [{"id": "sam", "name": "Sam Lee", "reasons": ["a (+0)", "b (+5)", "c (+1)", "d (+0)", "e (+2)", "f (+3)"]}]}
EOF
echo '{}' > "$TMP/i/seed/config.json"; rm -f "$TMP/i/seed/pipeline.json"
C agenda --dir "$TMP/i/seed" --investor sam --minutes 10 --json; eq "$CODE" 0 "bare investor"
eq "$(jq -c '.why' <<<"$OUT")" '["b (+5)","c (+1)","e (+2)"]' "why: no (+0), first three"
eq "$(jq -r '[.blocks[:4][].plan]|join("|")' <<<"$OUT")" "Thank them. Say why them. Ask what they are looking at right now.|The problem in one sentence, with the evidence.|your company: the product in one line. Show it, do not describe it.|The market size and the model, two numbers each." "generic plans"
eq "$(jq -r '.blocks[5].plan' <<<"$OUT")" "The raise round. Ask who else they would want in the round." "generic ask"
C agenda --dir "$TMP/i/seed" --investor sam --minutes 10; eq "$(head -2 <<<"$OUT")" "First call: Sam Lee (10 minutes)
Not booked yet: /seed-closer books the call and writes the invite." "bare head"
has "$OUT" "   0-1  min  Hello and why them " "%2d-%-2d"

# ---- exit codes and argparse
C drill --dir seed --n 0; eq "$CODE" 2 "n 0"; eq "$ERR" "coach.py: --n is 1 to 50" "n message"
C drill --dir seed --n 51; eq "$CODE" 2 "n 51"
C drill --dir seed --investor nobody; eq "$CODE" 1 "drill no investor"
C drill --dir seed --n x; eq "$CODE" 2 "n not an int"; has "$ERR" "argument --n: invalid int value: 'x'" "int message"
C; eq "$CODE" 2 "no command"; has "$OUT" "usage: coach.py" "help on stdout"; has "$OUT" "{drill,grade,agenda}" "subcommands"
C drill -h; eq "$CODE" 0 "drill help"; hasnt "$OUT" "--bank" "--bank is hidden"; has "$OUT" "--write" "write listed"
C agenda --dir seed; eq "$CODE" 2 "agenda needs --investor"; has "$ERR" "the following arguments are required: --investor" "required message"
C drill --dir seed --n 3 --json --write; eq "$(jq '.questions|length' <<<"$OUT")" "3" "json write"
eq "$(head -1 seed/coach/drill.md)" "# Pitch drill" "drill md"
eq "$(sed -n 3,6p seed/coach/drill.md)" "1. **Your SOM is 2.5% of a \$960M SAM in 3 years. What gets you the first 1,000 customers?** (market)
   - Checking: whether the market slide connects to a real go-to-market
   - Strong: one channel, its cost per customer, and how many customers it has already produced
   - Your numbers: TAM \$17.04B, SAM \$960M, SOM \$24M." "drill md rows"
hasnt "$OUT" "Wrote" "json: no Wrote line"

done_ "seed-coach/coach"

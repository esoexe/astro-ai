#!/usr/bin/env bash
# founder-cfo/unit_economics, founder-pricing/van_westendorp, founder-plan/compile, founder-consumer/panel: the contract
# of founder-skill/tests (test_unit_economics, test_pricing, test_compile, test_panel) plus the error paths and
# formatting rules of the Python originals. Expected numbers were worked out by hand from the example files.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
UE="founder-cfo/unit_economics"; VW="founder-pricing/van_westendorp"; PL="founder-plan/compile"; PN="founder-consumer/panel"
j() { jq -c "$1" <<<"$OUT"; }

# ---------------------------------------------------------------- fixtures (the skill's example files)
cat > numbers.json <<'EOF'
{
  "business": "Moss Matcha Bar (an example: a walk-in matcha café)",
  "unit": "cup",
  "price": 6.50,
  "variable": [
    {"label": "Matcha (ceremonial, 2g)", "cost": 0.92},
    {"label": "Oat milk", "cost": 0.38},
    {"label": "Cup, lid, straw", "cost": 0.24},
    {"label": "Card fees", "cost": 0.20}
  ],
  "fixed_monthly": [
    {"label": "Labour", "cost": 15500},
    {"label": "Rent + utilities", "cost": 11040}
  ],
  "startup": [
    {"label": "Lease deposit", "cost": 8400},
    {"label": "Build-out", "cost": 6900},
    {"label": "Equipment", "cost": 4250},
    {"label": "Signage", "cost": 1850},
    {"label": "Opening inventory", "cost": 2380},
    {"label": "Launch ads", "cost": 1500}
  ],
  "days_per_month": 30,
  "capacity_per_day": 600,
  "plan_per_day": 383,
  "ramp_per_day": [140, 170, 195, 215, 235, 250, 262, 272, 280, 287, 290, 292]
}
EOF
cat > customer.json <<'EOF'
{
  "business": "Moss Matcha Bar (an example: a walk-in matcha café)",
  "target": "Urban professionals, 24 to 38, who buy coffee 4+ times a week",
  "age": [24, 38],
  "segments": [
    {"name": "Early-career professional", "share": 0.45, "income": [38000, 85000]},
    {"name": "Mid-career professional", "share": 0.40, "income": [85000, 150000]},
    {"name": "High earner", "share": 0.15, "income": [150000, 210000]}
  ],
  "behaviours": [
    {"name": "Daily regular", "share": 0.20, "how": "Buys the same drink at the same place most mornings."},
    {"name": "Health-first", "share": 0.18, "how": "Reads labels, swaps coffee for anything that feels cleaner."},
    {"name": "Trend chaser", "share": 0.15, "how": "Tries whatever is new and posts about it."},
    {"name": "Weekend treat", "share": 0.15, "how": "Cheap coffee on weekdays, something nicer on Saturday."},
    {"name": "Deal hunter", "share": 0.12, "how": "Waits for an offer, uses loyalty apps."},
    {"name": "Loyal to their café", "share": 0.10, "how": "Has a favourite spot and the staff know their order."},
    {"name": "Reads every review", "share": 0.10, "how": "Checks Google ratings before trying anywhere new."}
  ],
  "objections": ["$6.50 is steep for a drink", "My coffee place is closer", "Does matcha actually taste good?",
                 "Is it really healthier than coffee?", "I need somewhere to sit and work", "I'd try it once, not daily"],
  "habits": ["you walk past King St W on the way to the office", "you work from home three days a week",
             "you meet friends for coffee on Saturdays", "you go to a gym near Spadina before work"]
}
EOF
printf '\n  Moss Matcha Bar, a new matcha café opening on King St W in Toronto.\nA cup is $6.50.\n\n' > pitch.md
spec() { jq "$1" numbers.json > s.json; }   # s.json = the example changed by a jq filter

# ================================================================ unit_economics
run $UE numbers.json
eq "$CODE" 0 "ue: rc"
has "$OUT" "# Unit economics: Moss Matcha Bar (an example: a walk-in matcha café)" "ue: title"
has "$OUT" "| line | per cup |" "ue: unit header"
has "$OUT" "| Price | \$6.50 |" "ue: price"
has "$OUT" "| Matcha (ceremonial, 2g) | -\$0.92 |" "ue: variable line"
has "$OUT" "| Card fees | -\$0.20 |" "ue: last variable line"
has "$OUT" "| **Contribution** (what each cup leaves to pay the fixed costs) | **\$4.76** (73%) |" "ue: contribution"
has "$OUT" "Fixed costs: \$26,540 a month (Labour \$15,500, Rent + utilities \$11,040)." "ue: fixed"
has "$OUT" "- **Break-even: 186 cups a day.** Below that you lose money every month." "ue: break-even"
has "$OUT" "- **Profit margin at your plan** (383 a day): **38%** of every sale, after every cost." "ue: margin"
has "$OUT" "- Capacity: 600 a day." "ue: capacity"
has "$OUT" "| month | cups a day | revenue | profit | cumulative (after \$25,280 startup) |" "ue: month header"
has "$OUT" "| 1 | 140 | \$27,300 | -\$6,548 | -\$31,828 |" "ue: month 1"
has "$OUT" "| 2 | 170 | \$33,150 | -\$2,264 | -\$34,092 |" "ue: month 2 (the deepest hole)"
has "$OUT" "| 8 | 272 | \$53,040 | \$12,302 | \$10,729 |" "ue: month 8 (paid back)"
has "$OUT" "| 12 | 292 | \$56,940 | \$15,158 | \$68,646 |" "ue: month 12"
has "$OUT" "- **Year 1 operating profit: \$93,926** on \$563,160 of revenue." "ue: year 1"
has "$OUT" "- After the \$25,280 startup spend: \$68,646." "ue: after startup"
has "$OUT" "- Startup money earned back: month 8." "ue: payback"
has "$OUT" "- Cash you need before it pays for itself: **\$34,092**." "ue: cash"
has "$OUT" "| Base plan | 38% | 186 | \$93,926 |" "ue: what-if base"
has "$OUT" "| Price -10% | 31% | 216 | \$37,610 |" "ue: what-if price"
has "$OUT" "| Volume -20% | 29% | 186 | \$11,445 |" "ue: what-if volume"
has "$OUT" "| Unit costs +15% | 34% | 197 | \$71,313 |" "ue: what-if costs"
has "$OUT" "No red flags in these numbers." "ue: no flags"
eq "$(printf '%s' "$OUT" | tail -n1)" "No red flags in these numbers. They are only as good as the inputs: check every cost against a real quote." "ue: last line"

run $UE numbers.json --json
eq "$CODE" 0 "ue json: rc"
eq "$(j '[.contribution, .payback_month, .cash_needed, .startup, .flags]')" '[4.76,8,34092.0,25280.0,[]]' "ue json: numbers"
eq "$(j '.breakeven_per_day * 1000 | floor')" "185854" "ue json: break-even"
eq "$(j '.margin_at_plan * 1000 | floor')" "376" "ue json: margin"
eq "$(j '.year1_profit * 10 | round')" "939264" "ue json: year 1"
eq "$(j 'keys_unsorted | join(",")')" '"business,unit,price,variable_per_unit,contribution,contribution_pct,fixed_monthly,startup,plan_per_day,margin_at_plan,breakeven_per_day,capacity_per_day,year1_revenue,year1_profit,year1_after_startup,payback_month,cash_needed,months,flags"' "ue json: keys (no *_items)"
eq "$(j '.months | length')" "12" "ue json: 12 months"
eq "$(j '.months[0] | keys_unsorted | join(",")')" '"month,per_day,units,revenue,variable,fixed,profit,cumulative"' "ue json: month keys"
has "$OUT" '"business": "Moss Matcha Bar (an example: a walk-in matcha caf\u00e9)"' "ue json: ensure_ascii"
has "$OUT" ' "fixed_monthly": 26540.0,' "ue json: floats keep .0"
has "$OUT" ' "capacity_per_day": 600,' "ue json: capacity as given"
has "$OUT" ' "per_day": 140.0,' "ue json: ramp scaled to floats"
eq "$(printf '%s' "$OUT" | head -n2 | tail -n1)" ' "business": "Moss Matcha Bar (an example: a walk-in matcha caf\u00e9)",' "ue json: indent 1"

# what-ifs
run $UE numbers.json --price 1
has "$OUT" "| **Contribution** (what each cup leaves to pay the fixed costs) | **-\$0.74** (-74%) |" "ue price 1: contribution"
has "$OUT" "- **Break-even: never (each unit loses money) cups a day.**" "ue price 1: never"
has "$OUT" "| Base plan | -305% | never | -\$382,594 |" "ue price 1: what-if"
has "$OUT" "- Each unit loses money before rent and salaries: the price is below the variable cost." "ue price 1: flag 1"
has "$OUT" "- Break-even needs inf cups a day but capacity is 600." "ue price 1: flag 2 (%.0f of inf)"
has "$OUT" "- Year 1 loses money on operations (-\$382,594)." "ue price 1: flag 3"
has "$OUT" "- The startup spend is not earned back within year 1." "ue price 1: flag 4"
run $UE numbers.json --price 1 --json
eq "$(j '[.breakeven_per_day, .payback_month, (.flags | length)]')" '[null,null,4]' "ue price 1 json: null break-even and payback"
run $UE numbers.json --volume 0.5
has "$OUT" "| Price -10% | -9% | 216 | -\$140,435 |" "ue volume 0.5: what-if"
has "$OUT" "- Year 1 loses money on operations (-\$112,277)." "ue volume 0.5: flag"
run $UE numbers.json --volume 0.5 --json
eq "$(j '[.plan_per_day, .months[0].per_day]')" '[191.5,70.0]' "ue volume 0.5: plan and ramp scaled"
spec '.capacity_per_day = 300'
run $UE s.json
has "$OUT" "- The plan (383 a day) is above capacity (300)." "ue capacity: plan above capacity"
spec '.capacity_per_day = 150 | .plan_per_day = 0'
run $UE s.json --json
eq "$(j '.flags[0]')" '"Break-even needs 186 cups a day but capacity is 150."' "ue capacity: break-even above capacity"
eq "$(j '.plan_per_day')" "292.0" "ue: no plan -> the last ramp month"
spec 'del(.ramp_per_day) | .plan_per_day = 0'
run $UE s.json --json
has "$OUT" '"margin_at_plan": -Infinity,' "ue: no volume -> margin -inf"
run $UE s.json
has "$OUT" "**-inf%** of every sale" "ue: -inf%"
spec 'del(.ramp_per_day)'
run $UE s.json --json
eq "$(j '[.months[0].per_day, .months[11].per_day, .payback_month]')" '[383.0,383.0,1]' "ue: flat plan every month (paid back in month 1)"
spec 'del(.capacity_per_day) | del(.unit) | del(.business) | .fixed_monthly = []'
run $UE s.json
has "$OUT" "# Unit economics: your business" "ue: default business"
has "$OUT" "| line | per unit |" "ue: default unit"
has "$OUT" "Fixed costs: \$0 a month (none given)." "ue: none given"
hasnt "$OUT" "Capacity:" "ue: no capacity line"
has "$OUT" "- **Break-even: 0 units a day.**" "ue: break-even 0"
spec '.price = "7.25" | .days_per_month = "26"'
run $UE s.json --json
eq "$(j '[.price, (.months[0].units)]')" '[7.25,3640.0]' "ue: numbers given as strings"
spec '.variable[0].cost = 1.005 | .price = 2.675'
run $UE s.json
has "$OUT" "| Matcha (ceremonial, 2g) | -\$1.00 |" "ue: money rounds the exact binary value (1.005 -> 1.00)"
has "$OUT" "| Price | \$2.67 |" "ue: 2.675 -> 2.67"
spec '.fixed_monthly = [{"label": "Rent", "cost": 2500.5}, {"label": "Fees", "cost": 3501.5}]'
run $UE s.json
has "$OUT" "(Rent \$2,500, Fees \$3,502)" "ue: whole dollars round half to even"

# errors
run $UE nope.json
eq "$CODE|$ERR" "2|error: no such file: nope.json" "ue: missing file"
printf '{"price": 3,}' > bad.json
run $UE bad.json
eq "$CODE|$ERR" "2|error: bad.json is not valid JSON: Illegal trailing comma before end of object: line 1 column 12 (char 11)" "ue: bad JSON (Python 3.13 wording)"
spec 'del(.price)'
run $UE s.json
eq "$CODE|$ERR" "2|error: 'price' is missing" "ue: no price"
run $UE s.json --price 5 --json
eq "$CODE|$(j '.price')" "0|5.0" "ue: --price fills a missing price"
spec '.price = 0'
run $UE s.json
eq "$CODE|$ERR" "2|error: price must be above zero" "ue: price 0"
run $UE numbers.json --price=-2
eq "$CODE|$ERR" "2|error: price must be above zero" "ue: negative --price"
spec '.variable[1] = {"label": "x"}'
run $UE s.json
eq "$CODE|$ERR" "2|error: variable[1] needs a 'cost'" "ue: item without cost"
spec '.startup[2] = 5'
run $UE s.json
eq "$CODE|$ERR" "2|error: startup[2] needs a 'cost'" "ue: item not an object"
spec '.fixed_monthly[0].cost = "abc"'
run $UE s.json
eq "$CODE|$ERR" "2|error: fixed_monthly[0].cost is not a number: 'abc'" "ue: cost not a number"
spec '.fixed_monthly[0].cost = [1]'
run $UE s.json
eq "$CODE|$ERR" "2|error: fixed_monthly[0].cost is not a number: [1]" "ue: cost a list"
spec 'del(.plan_per_day) | del(.ramp_per_day)'
run $UE s.json
eq "$CODE|$ERR" "2|error: give 'plan_per_day' or 'ramp_per_day' (12 numbers)" "ue: no volume"
spec '.ramp_per_day = [1, 2, 3]'
run $UE s.json
eq "$CODE|$ERR" "2|error: 'ramp_per_day' needs 12 numbers, one per month (got 3)" "ue: short ramp"
spec '.days_per_month = 0'
run $UE s.json
eq "$CODE|$ERR" "1|founder-cfo/unit_economics: ZeroDivisionError: float division by zero" "ue: 0 days (uncaught, like Python)"
spec '.days_per_month = "x"'
run $UE s.json
eq "$CODE|$ERR" "1|founder-cfo/unit_economics: ValueError: could not convert string to float: 'x'" "ue: bad days"
spec '.variable = null'
run $UE s.json
eq "$CODE" "1" "ue: variable null breaks the report, as in Python"
run $UE numbers.json --out cfo.md
eq "$CODE|$OUT" "0|wrote cfo.md" "ue --out: message"
eq "$(sed -n 1p cfo.md)" "# Unit economics: Moss Matcha Bar (an example: a walk-in matcha café)" "ue --out: file"
run $UE
eq "$CODE" "2" "ue: no argument"
has "$ERR" "the following arguments are required: numbers" "ue: usage error"
run $UE -h
has "$OUT" "Unit economics for founder-cfo. Standard library only, no network." "ue: help"

# ================================================================ van_westendorp
mkcsv() {   # test_pricing.rows(n) as a CSV, plus extra raw lines
    local n=$1 f=$2; shift 2
    { echo "too_cheap,cheap,expensive,too_expensive"
      for ((i = 0; i < n; i++)); do jq -rn --argjson i $i --argjson n $n '($i / ($n - 1)) as $k | [2.5 + $k, 4 + $k, 6 + 1.5 * $k, 7.5 + 2 * $k] | map(tostring) | join(",")'; done
      for l in "$@"; do echo "$l"; done; } > "$f"
}
mkcsv 40 r40.csv
run $VW r40.csv --json
eq "$CODE" 0 "vw: rc"
eq "$(j '[.n, .dropped]')" '[40,0]' "vw: n and dropped"
eq "$(j '.points | [.PMC, .PME, .OPP, .IPP] | map(. != null and . >= 2.5 and . <= 9.5) | all')" "true" "vw: points inside the answers"
eq "$(j '.points | (.PMC < .PME) and (.PMC <= .OPP) and (.OPP <= .PME)')" "true" "vw: points ordered"
eq "$(j '.range == [.points.PMC, .points.PME]')" "true" "vw: range"
eq "$(j '.median')" '{"too_cheap":3.0128205128205128,"cheap":4.512820512820513,"expensive":6.769230769230769,"too_expensive":8.525641025641026}' "vw: medians (sorted[n//2])"
eq "$(j 'keys_unsorted | join(",")')" '"n,points,range,median,dropped"' "vw json: keys"
run $VW r40.csv --price 6.5
has "$OUT" "Your current price, \$6.50, is **inside** the acceptable range." "vw: inside"
has "$OUT" "40 complete answers. Simulated buyers: use this to pick what to test, not as the final word." "vw: header without dropped"
has "$OUT" "| PMC | \$" "vw: PMC row"
run $VW r40.csv --price 40
has "$OUT" "**above** the acceptable range" "vw: above"
run $VW r40.csv --price 1
has "$OUT" "Your current price, \$1.00, is **below** the acceptable range." "vw: below"
run $VW r40.csv --price=-1
has "$OUT" "Your current price, \$-1.00, is **below** the acceptable range." "vw: a negative price is still placed (price is not None)"
run $VW r40.csv
hasnt "$OUT" "Your current price" "vw: no price, no line"
mkcsv 10 r12.csv "9,4,6,8" "3,,6,8"
run $VW r12.csv --json
eq "$(j '[.n, .dropped]')" '[10,2]' "vw: out of order and incomplete rows dropped"
run $VW r12.csv
has "$OUT" "10 complete answers, 2 dropped (incomplete or out of order)." "vw: dropped in the header"
mkcsv 4 r4.csv
run $VW r4.csv
eq "$CODE|$ERR" "2|error: need at least 5 complete answers (got 4)" "vw: too few"
printf 'cheap,too_cheap,too_expensive,expensive,extra\r\n"$4.00"," $3 ",10,"7",x\r\n4,3,10,7\r\n4,3,10,7,1,2\r\n\r\n4,3,"1,000",7\r\n4,3,12,7\r\n4,3,10,0\r\n4,3,10,nan\r\n4,3,10,inf\r\n' > money.csv
run $VW money.csv --json
eq "$(j '[.n, .dropped, .median.too_expensive]')" '[5,3,10.0]' "vw: \$ and , stripped, extra columns ignored; 0, nan and out of order dropped (blank lines skipped)"
printf '\xef\xbb\xbftoo_cheap,cheap,expensive,too_expensive\n1,2,3,4\n1,2,3,4\n1,2,3,4\n1,2,3,4\n1,2,3,4\n' > bom.csv
run $VW bom.csv
eq "$CODE|$ERR" "2|error: need at least 5 complete answers (got 0)" "vw: a BOM stays in the first column name (utf-8, not utf-8-sig)"
run $VW missing.csv
eq "$CODE|$ERR" "2|error: [Errno 2] No such file or directory: 'missing.csv'" "vw: missing CSV"
run $VW prices.txt
eq "$CODE|$ERR" "2|error: give the panel's answers folder or a .csv" "vw: not a CSV"
mkdir -p ans
for i in 1 2 3 4 5 6; do printf '{"id": "P%03d", "price_too_cheap": %d, "price_cheap": "$%d.00", "price_expensive": %d, "price_too_expensive": "%d"}' $i $((1 + i)) $((3 + i)) $((6 + i)) $((8 + i)) > ans/P00$i.json; done
printf '{"price_too_cheap": 2}' > ans/P007.json
printf 'not json' > ans/P008.json
printf '{"price_too_cheap": 1, "price_cheap": 1, "price_expensive": 1, "price_too_expensive": 1}' > ans/.hidden.json
printf '{"price_too_cheap": 1, "price_cheap": 1, "price_expensive": 1, "price_too_expensive": 1}' > ans/notes.txt
run $VW ans --json
eq "$(j '[.n, .dropped, .median.cheap]')" '[6,2,7.0]' "vw folder: incomplete and broken files dropped, dot files and other names skipped"
eq "$(j '.points.PMC != null')" "true" "vw folder: points"
printf '[1, 2]' > ans/P009.json
run $VW ans
eq "$CODE" 1 "vw folder: a JSON list fails like Python's a.get"
has "$ERR" "AttributeError: 'list' object has no attribute 'get'" "vw folder: AttributeError"
rm ans/P009.json
run $VW ans --out curve.md
eq "$CODE|$OUT" "0|wrote curve.md" "vw --out"
eq "$(sed -n 1p curve.md)" "# Price sensitivity" "vw --out: file"

# ================================================================ panel
run $PN --dir pnl init --customer customer.json --pitch pitch.md --seed 3
eq "$CODE|$OUT" "0|dealt 100 buyers (seed 3) into pnl. Next: panel.py prompts" "panel init: message"
eq "$(jq -c '[group_by(.segment)[] | {(.[0].segment): length}] | add' pnl/personas.json)" '{"Early-career professional":45,"High earner":15,"Mid-career professional":40}' "panel: segments by share"
eq "$(jq -c '[group_by(.behaviour)[] | {(.[0].behaviour): length}] | add' pnl/personas.json)" '{"Daily regular":20,"Deal hunter":12,"Health-first":18,"Loyal to their café":10,"Reads every review":10,"Trend chaser":15,"Weekend treat":15}' "panel: behaviours by share"
eq "$(jq '[.[].name] | unique | length' pnl/personas.json)" "100" "panel: 100 different names"
eq "$(jq -c '[.[0].id, .[99].id]' pnl/personas.json)" '["P001","P100"]' "panel: ids"
eq "$(jq -c '[.[] | select(.age < 24 or .age > 38)] | length' pnl/personas.json)" "0" "panel: ages inside the range"
eq "$(jq -c --slurpfile c customer.json '[.[] as $p | $c[0].segments[] | select(.name == $p.segment) | select($p.income < .income[0] - 500 or $p.income > .income[1] + 500)] | length' pnl/personas.json)" "0" "panel: incomes inside the segment"
eq "$(jq -c '[.[] | .income % 1000] | unique' pnl/personas.json)" "[0]" "panel: incomes rounded to thousands"
eq "$(jq -c --slurpfile c customer.json '[.[] | select((.objection as $o | $c[0].objections | index($o)) == null)] | length' pnl/personas.json)" "0" "panel: objections from the list"
eq "$(jq -c 'keys_unsorted' pnl/personas.json | head -c0)$(jq -c '.[0] | keys_unsorted' pnl/personas.json)" '["id","name","age","segment","income","behaviour","behaviour_how","objection","habit"]' "panel: card keys"
# CPython's random.Random(3): the first cards are the ones the Python tool deals
eq "$(jq -c '.[0] | [.name, .age, .segment, .income, .behaviour]' pnl/personas.json)" '["Idris",27,"Early-career professional",51000,"Daily regular"]' "panel: seed 3, card 1 (CPython Mersenne Twister)"
eq "$(jq -c '.[1] | [.name, .age, .income]' pnl/personas.json)" '["Pablo",24,60000]' "panel: seed 3, card 2"
eq "$(jq -c '[.n, .seed, .pitch]' pnl/panel.json)" '[100,3,"Moss Matcha Bar, a new matcha café opening on King St W in Toronto.\nA cup is $6.50."]' "panel.json: n, seed, stripped pitch"
eq "$(jq -c '.customer.age' pnl/panel.json)" '[24,38]' "panel.json: the customer"
has "$(cat pnl/panel.json)" '"business": "Moss Matcha Bar (an example: a walk-in matcha café)"' "panel.json: ensure_ascii=False"
eq "$(sed -n 2p pnl/panel.json)" ' "n": 100,' "panel.json: indent 1"
eq "$(tail -c 2 pnl/personas.json | od -An -c | tr -s ' ')" ' ] \n' "personas.json: a final newline"
[ -d pnl/briefs ] && [ -d pnl/answers ] && ok || bad "panel init: briefs/ and answers/"
run $PN --dir pnl2 init --customer customer.json --pitch pitch.md --seed 3
cmp -s pnl/personas.json pnl2/personas.json && ok || bad "panel: same seed, same cards"
run $PN --dir pnl3 init --customer customer.json --pitch pitch.md --seed 4
cmp -s pnl/personas.json pnl3/personas.json && bad "panel: another seed, other cards" || ok
run $PN --dir big init --customer customer.json --pitch pitch.md --seed 9 --n 250
eq "$(jq '[.[].name] | unique | length' big/personas.json)" "250" "panel n 250: names stay different"
eq "$(jq '[.[].name | select(endswith(" 2"))] | length' big/personas.json)" "120" "panel n 250: the second round adds ' 2'"
eq "$(jq '[.[].name | select(endswith(" 3"))] | length' big/personas.json)" "10" "panel n 250: the third round adds ' 3'"
eq "$(jq -r '.[249].id' big/personas.json)" "P250" "panel n 250: last id"
run $PN --dir rnd init --customer customer.json --pitch pitch.md --n 3
eq "$CODE" 0 "panel: no seed"
eq "$(jq '.seed >= 1 and .seed < 1000000' rnd/panel.json)" "true" "panel: a random seed in 1..999999"
jq 'del(.habits)' customer.json > c2.json
run $PN --dir nh init --customer c2.json --pitch pitch.md --seed 1 --n 5
eq "$(jq -c '[.[].habit] | unique' nh/personas.json)" '[""]' "panel: no habits -> empty"
jq '.segments = [{"name": "A", "income": [1000, 2000]}, {"name": "B", "income": [1000, 2000]}, {"name": "C", "income": [1000, 2000]}]' customer.json > c3.json
run $PN --dir q init --customer c3.json --pitch pitch.md --seed 1 --n 10
eq "$(jq -c '[group_by(.segment)[] | length]' q/personas.json)" '[4,3,3]' "panel quota: equal shares, the remainder goes to the first"
# init errors
run $PN --dir e init --customer nope.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: no such file: nope.json" "panel init: missing customer"
run $PN --dir e init --customer customer.json --pitch nope.md
eq "$CODE|$ERR" "2|error: no such file: nope.md (write the pitch first: what you sell, the price, where)" "panel init: missing pitch"
printf ' \n\t\n' > empty.md
run $PN --dir e init --customer customer.json --pitch empty.md
eq "$CODE|$ERR" "2|error: the pitch is empty" "panel init: empty pitch"
jq 'del(.segments)' customer.json > bad1.json
run $PN --dir e init --customer bad1.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: customer.json needs 'segments'" "panel init: no segments"
jq '.objections = []' customer.json > bad2.json
run $PN --dir e init --customer bad2.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: customer.json needs 'objections'" "panel init: empty objections"
jq '.target = ""' customer.json > bad3.json
run $PN --dir e init --customer bad3.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: customer.json needs 'target'" "panel init: empty target"
jq '.segments[1].income = [1]' customer.json > bad4.json
run $PN --dir e init --customer bad4.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: every segment needs a name and an income [low, high]" "panel init: bad income"
jq '.behaviours[0] = {"how": "x"}' customer.json > bad5.json
run $PN --dir e init --customer bad5.json --pitch pitch.md
eq "$CODE|$ERR" "2|error: every behaviour needs a name" "panel init: behaviour without a name"
run $PN --dir e init --customer customer.json --pitch pitch.md --n 0
eq "$CODE|$ERR" "2|error: --n must be at least 1" "panel init: n 0"
jq '.age = [40, 30]' customer.json > bad6.json
run $PN --dir e init --customer bad6.json --pitch pitch.md --seed 1
eq "$CODE|$ERR" "1|founder-consumer/panel: ValueError: empty range in randrange(40, 31)" "panel init: an empty age range"
printf '{"business": 1,' > bad7.json
run $PN --dir e init --customer bad7.json --pitch pitch.md
eq "$CODE" 1 "panel init: broken customer JSON is not caught (as in Python)"
has "$ERR" "JSONDecodeError" "panel init: JSONDecodeError"
run $PN init --customer customer.json
eq "$CODE" 2 "panel init: --pitch required"
run $PN
eq "$CODE" 2 "panel: a command is required"
has "$ERR" "{init,prompts,check,save,tally,status}" "panel: usage lists the commands"

# a 20-buyer panel, like test_panel.py
run $PN --dir p init --customer customer.json --pitch pitch.md --n 20 --seed 5
run $PN --dir p prompts
eq "$CODE" 0 "prompts: rc"
eq "$(printf '%s\n' "$OUT" | head -n1)" "20 briefs in 2 waves of up to 10. Launch one wave at a time:" "prompts: summary"
eq "$(printf '%s\n' "$OUT" | grep -c '^wave ')" "2" "prompts: two waves"
has "$OUT" "wave 1: P001=$TMP/p/briefs/P001.md P002=" "prompts: id=absolute brief path"
eq "$(ls p/briefs | wc -l | tr -d ' ')" "20" "prompts: 20 briefs"
B1=$(cat p/briefs/P001.md)
has "$B1" "Moss Matcha Bar" "brief: the pitch"
has "$B1" '"id": "P001"' "brief: the id"
has "$B1" "Do not be agreeable" "brief: the instructions"
has "$B1" "Create the file $TMP/p/answers/P001.json with your file-writing tool" "brief: the absolute answer path"
has "$B1" "$(jq -r '.[0] | "\(.name), \(.age). \(.segment). You earn about $\(.income / 1000 | floor),\(.income % 1000 | tostring | ("00" + .)[-3:]) a year."' p/personas.json)" "brief: who (income with commas)"
has "$B1" "Your day: $(jq -r '.[0].habit' p/personas.json)." "brief: habit line"
has "$B1" "You are one of these people: Urban professionals, 24 to 38, who buy coffee 4+ times a week." "brief: target"
has "$B1" '"reason_code": one of price / habit / need / trust / quality / convenience / timing / values / other,' "brief: reasons"
eq "$(tail -c 3 p/briefs/P001.md | od -An -c | tr -s ' ')" ' \n } \n' "brief: ends with the closing brace"
run $PN --dir p prompts --wave 7
eq "$(printf '%s\n' "$OUT" | head -n1)" "20 briefs in 3 waves of up to 7. Launch one wave at a time:" "prompts --wave 7"
run $PN --dir p prompts --wave 0
eq "$CODE|$ERR" "1|founder-consumer/panel: ValueError: range() arg 3 must not be zero" "prompts --wave 0"
run $PN --dir p prompts --wave=-1
eq "$CODE|$OUT" "0|every buyer has a valid answer. Next: panel.py tally" "prompts: a negative wave lists nothing (range is empty)"
run $PN --dir p check
eq "$CODE" 1 "check: rc 1 with problems"
eq "$(printf '%s\n' "$OUT" | head -n1)" "P001: missing" "check: missing"
# 19 answers: P001-P008 buy, P009-P019 pass (7 for price); P020 stays missing
ans() { printf '%s' "$2" > "p/answers/$1.json"; }
for i in $(seq 1 19); do
    id=$(printf 'P%03d' $i)
    if [ $i -le 8 ]; then b=true; rc=quality; else b=false; rc=$([ $i -le 15 ] && echo price || echo habit); fi
    ans $id "{\"id\": \"$id\", \"buys\": $b, \"purchases_first_month\": $((i % 3)), \"reason_code\": \"$rc\", \"reason\": \"because $i | so\", \"would_change_my_mind\": \"$([ $i = 10 ] && echo 'a smaller cup')\", \"price_too_cheap\": 3, \"price_cheap\": 4.5, \"price_expensive\": 7, \"price_too_expensive\": 9}"
done
run $PN --dir p check
eq "$CODE|$OUT" "1|P020: missing" "check: only P020"
run $PN --dir p status
eq "$OUT" "20 buyers dealt (seed 5), 19 answered, 1 to go" "status"
run $PN --dir p prompts
eq "$OUT" "1 briefs in 1 waves of up to 10. Launch one wave at a time:
wave 1: P020=$TMP/p/briefs/P020.md" "prompts: only the missing buyer"
run $PN --dir p tally
eq "$CODE|$OUT" "0|8 buy · 11 pass (42%). Wrote p/results.md" "tally: summary"
R=$(cat p/results.md)
has "$R" "# Buyer panel: Moss Matcha Bar (an example: a walk-in matcha café)" "results: title"
has "$R" "**8 buy · 11 pass** (42% buy) out of 19 simulated buyers. Seed 5, so the same cards can be dealt again." "results: headline"
has "$R" "1 of 20 buyers have no valid answer and are left out: P020 (missing)." "results: skipped"
has "$R" "not customers" "results: the caveat"
has "$R" "| price | 7 | \"because 9 / so\" (P009) · \"because 10 / so\" (P010) |" "results: why they pass (pipes replaced)"
has "$R" "| habit | 4 |" "results: second reason"
has "$R" "| quality | 8 | \"because 1 / so\" (P001) · \"because 2 / so\" (P002) |" "results: why they buy"
has "$R" "## What would flip a no

- a smaller cup" "results: flip (blank answers skipped)"
has "$R" "Buyers say they would buy **1.1 times** in the first month on average." "results: purchases (1,2,0,1,2,0,1,2 -> 1.125)"
has "$R" "19 buyers gave all four price answers. Run founder-pricing's van_westendorp.py on the answers folder." "results: price answers"
has "$R" "(thin)" "results: thin groups"
eq "$(jq -c '[.buys, .passes, .answered, .dealt, .seed, .buy_rate, .price_answers]' p/results.json)" '[8,11,19,20,5,0.42105263157894735,19]' "results.json: counts"
eq "$(jq -c '.why_not[0][:2]' p/results.json)" '["price",7]' "results.json: why_not first"
eq "$(jq -c '.skipped' p/results.json)" '[["P020","missing"]]' "results.json: skipped"
eq "$(jq -c '.purchases_first_month_per_buyer' p/results.json)" "1.125" "results.json: purchases"
eq "$(jq -c 'keys_unsorted | join(",")' p/results.json)" '"business,seed,dealt,answered,skipped,buys,passes,buy_rate,by_segment,by_behaviour,by_income,why_not,why_yes,flip,purchases_first_month_per_buyer,price_answers"' "results.json: keys"
eq "$(jq -c '[.by_segment[] | .[2]] | . == (sort | reverse)' p/results.json)" "true" "results.json: groups by buy rate"
eq "$(jq -c '.by_income | length' p/results.json)" "3" "results.json: three income bands"
has "$(cat p/results.json)" '"price_answers": 19' "results.json: price answers"
run $PN --dir p tally --out mine.md
eq "$CODE|$OUT" "0|8 buy · 11 pass (42%). Wrote mine.md" "tally --out"
# save
printf 'Here is my answer:\r\n{"id": "P020", "buys": false, "reason_code": "trust", "reason": "never {heard} of it", "x": "é"} thanks {bye}' > reply.txt
OUT=$($T $PN --dir p save P020 < reply.txt 2>&1); CODE=$?
eq "$CODE|$OUT" "0|saved p/answers/P020.json" "save: the first JSON object of the reply"
eq "$(jq -c '[.id, .reason, .x]' p/answers/P020.json)" '["P020","never {heard} of it","é"]' "save: filed as written"
has "$(cat p/answers/P020.json)" '"x": "é"' "save: ensure_ascii=False"
run $PN --dir p check
eq "$CODE|$OUT" "0|all answers present and valid" "check: all valid"
OUT=$($T $PN --dir p save P999 < reply.txt 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: no buyer P999 on this panel" "save: unknown buyer"
OUT=$(printf 'no json here' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: no JSON object in the reply" "save: no object"
OUT=$(printf 'x {"id": "P020" "buys": true}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: the reply's JSON does not parse: Expecting ',' delimiter: line 1 column 15 (char 14)" "save: broken JSON"
cp p/answers/P020.json keep.json
OUT=$(printf '{"id": "P020", "buys": "no", "reason_code": "price", "reason": "x"}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: P020's answer is not valid ('buys' must be true or false); re-run its brief instead" "save: invalid answer"
[ ! -e p/answers/P020.json ] && ok || bad "save: the invalid answer is removed"
OUT=$(printf '{"id": "P003", "buys": true, "reason_code": "price", "reason": "x"}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: P020's answer is not valid (id is 'P003', expected P020); re-run its brief instead" "save: wrong id"
OUT=$(printf '{"id": "P020", "buys": true, "reason_code": "cost", "reason": "x"}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: P020's answer is not valid (reason_code 'cost' is not one of price, habit, need, trust, quality, convenience, timing, values, other); re-run its brief instead" "save: bad reason_code"
OUT=$(printf '{"buys": true}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: P020's answer is not valid (missing id, reason, reason_code); re-run its brief instead" "save: missing fields, sorted"
OUT=$(printf '{"id": "P020", "buys": true, "reason_code": null, "reason": "x"}' | $T $PN --dir p save P020 2>&1); CODE=$?
eq "$CODE|$OUT" "2|error: P020's answer is not valid (reason_code None is not one of price, habit, need, trust, quality, convenience, timing, values, other); re-run its brief instead" "save: reason_code None (repr)"
cp keep.json p/answers/P020.json
printf '{"id": "P005"' > p/answers/P005.json
printf '\xff\xfe' > p/answers/P006.json
run $PN --dir p check
eq "$OUT" "P005: not valid JSON
P006: not valid JSON" "check: broken and undecodable files"
# no panel
run $PN --dir none status
eq "$CODE|$ERR" "2|error: no panel in none: run init first" "status: no panel"
run $PN --dir none tally
eq "$CODE|$ERR" "2|error: no panel in none: run init first" "tally: no panel"
run $PN --dir pnl tally
eq "$CODE|$ERR" "2|error: no valid answers yet: run the briefs first (panel.py prompts)" "tally: nothing answered"

# ================================================================ compile
mkdir -p fd/panel
cp numbers.json fd/numbers.json
cp p/results.json fd/panel/results.json
printf '# Moss Matcha\n\nA matcha bar on King St W.\n## Why now\nbecause\n' > fd/idea.md
printf '# What the board said\r\n\r\n## Lens 1\r\nfine\r\n# Another h1\r\n' > fd/board.md
printf '  \n\n' > fd/ops.md
printf 'Plain summary.\n' > fd/summary.md
run $PL --dir fd
eq "$CODE|$OUT" "0|Verdict: Profitable. Wrote fd/business-plan.md" "compile: verdict"
BP=$(cat fd/business-plan.md)
eq "$(printf '%s\n' "$BP" | head -n1)" "# Moss Matcha · Business plan" "compile: title from idea.md"
has "$BP" "**Verdict: Profitable**" "compile: verdict line"
has "$BP" "- ✓ Each cup earns \$4.76 before fixed costs (73% contribution)." "compile: check 1"
has "$BP" "- ✓ Year 1 operating profit: \$93,926." "compile: check 2"
has "$BP" "- ✓ Break-even is 186 cups a day against a capacity of 600." "compile: check 3"
has "$BP" "- ✓ 8 of 19 simulated buyers buy (42%, the bar is 25%)." "compile: check 4"
has "$BP" "| Price | \$6.50 a cup |" "compile: price"
has "$BP" "| Profit margin at plan | 38% per cup |" "compile: margin"
has "$BP" "| Break-even | 186 cups a day |" "compile: break-even"
has "$BP" "| Year 1 operating profit | \$93,926 |" "compile: year 1"
has "$BP" "| Startup spend | \$25,280 |" "compile: startup"
has "$BP" "| Cash needed before it pays for itself | \$34,092 |" "compile: cash"
has "$BP" "| Startup money earned back | month 8 |" "compile: payback"
has "$BP" "| Buyer panel | 8 buy · 11 pass |" "compile: panel row"
has "$BP" "## The idea

A matcha bar on King St W.
### Why now
because" "compile: the idea, its h1 dropped and headings demoted"
has "$BP" "## What the board said

### Lens 1
fine
## Another h1" "compile: board section (CRLF read as lines)"
has "$BP" "## Summary

Plain summary." "compile: summary"
has "$BP" "## Not done yet

- The competition: run /founder-competitors
- The buyer panel: run /founder-consumer" "compile: missing sections in order"
has "$BP" "- Operations: run /founder-ops" "compile: a blank file counts as missing"
eq "$(printf '%s\n' "$BP" | tail -n1)" "_The panel is simulated buyers and the numbers are projections from your inputs. Confirm demand with real customers and costs with real quotes before you spend. Not financial, legal or tax advice._" "compile: disclaimer"
run $PL --dir fd --check
eq "$CODE" 0 "compile --check: rc 0 when Profitable"
eq "$OUT" "Verdict: Profitable
  ✓ Each cup earns \$4.76 before fixed costs (73% contribution).
  ✓ Year 1 operating profit: \$93,926.
  ✓ Break-even is 186 cups a day against a capacity of 600.
  ✓ 8 of 19 simulated buyers buy (42%, the bar is 25%)." "compile --check: output"
run $PL --dir fd --check --min-buy-rate 0.5
eq "$CODE" 1 "compile --check: rc 1 when not yet"
has "$OUT" "Verdict: Not yet" "compile: not yet"
has "$OUT" "  ✗ 8 of 19 simulated buyers buy (42%, the bar is 50%)." "compile: failed check"
run $PL --dir fd --out plan.md
eq "$OUT" "Verdict: Profitable. Wrote plan.md" "compile --out"
jq '.price = 1 | .capacity_per_day = 100' numbers.json > fd/numbers.json
run $PL --dir fd --check
eq "$CODE" 1 "compile: losing money"
has "$OUT" "  ✗ Each cup LOSES \$0.74 before fixed costs." "compile: loses"
has "$OUT" "  ✗ Year 1 operating LOSS: \$382,594." "compile: loss"
has "$OUT" "  ✗ Break-even is never reached cups a day against a capacity of 100." "compile: never reached"
run $PL --dir fd
has "$(cat fd/business-plan.md)" "| Break-even | never cups a day |" "compile: never in the table"
has "$(cat fd/business-plan.md)" "| Startup money earned back | not in year 1 |" "compile: not in year 1"
has "$(cat fd/business-plan.md)" "| Year 1 operating profit | \$-382,594 |" "compile: a negative profit keeps its sign after the \$"
jq 'del(.price)' numbers.json > fd/numbers.json
run $PL --dir fd
eq "$CODE|$ERR" "1|founder-plan/compile: founder_unit_economics.NumbersError: 'price' is missing" "compile: bad numbers are not caught (as in Python)"
cp numbers.json fd/numbers.json
echo '{"buys": 3}' > fd/panel/results.json
run $PL --dir fd
eq "$CODE|$ERR" "1|founder-plan/compile: KeyError: 'buy_rate'" "compile: results without buy_rate"
rm fd/panel/results.json fd/idea.md
run $PL --dir fd --check
eq "$CODE" 0 "compile: no panel, still Profitable"
run $PL --dir fd
eq "$(head -n1 fd/business-plan.md)" "# Moss Matcha Bar (an example: a walk-in matcha café) · Business plan" "compile: title from the numbers"
mkdir -p bare
run $PL --dir bare
eq "$CODE|$OUT" "0|Verdict: Incomplete. Wrote bare/business-plan.md" "compile: incomplete"
has "$(cat bare/business-plan.md)" "# Your business · Business plan" "compile: default title"
has "$(cat bare/business-plan.md)" "- No numbers yet. Run /founder-cfo, then compile again." "compile: incomplete hint"
run $PL --dir bare --check
eq "$CODE|$OUT" "1|Verdict: Incomplete" "compile --check: incomplete"
run $PL --dir nowhere
eq "$CODE|$ERR" "2|error: no nowhere/ folder here. Run the other founder skills first." "compile: no folder"

done_ "founder"

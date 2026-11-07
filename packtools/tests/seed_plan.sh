#!/usr/bin/env bash
# seed-plan/plan: the assertions of seed-skill/tests/test_plan.py, run against the C++ tool through its command line,
# plus hand-worked edge cases. fixtures/seed/seed-plan/leashly is the upstream fixture (config.json, plan.json with the
# nine sections written, seed-model's model.json, market.json and competitors.json with the reel's numbers); the golden
# output is fixtures/seed/seed-deck/leashly/plan.json, which upstream generated with plan.py build --today 2026-10-26.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
LF="$FIX/seed/seed-plan/leashly"
GOLD="$FIX/seed/seed-deck/leashly/plan.json"
P() { run seed-plan/plan "$@"; }
J() { jq -c "$@"; }
fresh() { cd "$TMP" && rm -rf w && mkdir w && cd w && { [ "${1:-}" = empty ] || cp -r "$LF" seed; }; }
ed() { jq "$2" "seed/$1" > seed/e.tmp && mv seed/e.tmp "seed/$1"; }
flags() { P check --json; FL=$(J .flags <<<"$OUT"); NT=$(J .notes <<<"$OUT"); }
SECTION_TITLES='["Executive summary","Problem","Solution","Market","Competition","Business model","Go-to-market","Financial plan","Use of funds"]'

# ---- the reel (test_build_the_reel): the build IS the deck's fixture, byte for byte
fresh; P build --today 2026-10-26; eq "$CODE" 0 "build"
eq "$OUT" "Leashly: business plan, 9 of 9 sections written.
  Per walk     price \$25.00, take 15% = \$3.75, contribution \$2.23
  LTV:CAC      4.24x, CAC payback 3.37 months
Every check passed: no empty sections, and the numbers agree between files.
Wrote seed/plan.md and seed/plan.json.
Next: /seed-deck turns plan.json into the 10-slide deck." "the console output in SKILL.md"
cmp -s seed/plan.json "$GOLD" && ok || bad "plan.json is byte for byte the deck's fixture"
M=$(cat seed/plan.md)
while IFS= read -r l; do has "$M" "$l" "plan.md: $l"; done <<'EOF'
# Leashly: business plan
Raising a $1,500,000 seed round on a post-money SAFE. Built Oct 26, 2026
| Market (TAM / SAM / SOM) | $8.5B / $228M / $22.8M |
| Revenue, years 1 to 3 | $467,400 / $1,586,640 / $2,993,520 |
| Breakeven | month 29 (May 2029) |
- Rover: 106 of 341 recent 1 and 2 star reviews mention fees and pricing.
- Wag!: 81 of 324 recent 1 and 2 star reviews mention cancellations and no-shows.
| Rover | 4.9 | 515,771 | 341 of 500 read | Fees and pricing | 106 of 341 mention fees and pricing |
| US pet industry spend, 2025 | $158,000,000,000 | APPA 2026 State of the Industry report
| **Contribution per walk** | **$2.23** |
| CAC payback | 3.37 months (CAC / contribution per customer a month) |
| **LTV:CAC** | **4.24x** |
| Revenue | $467,400 | $1,586,640 | $2,993,520 |
| Net | -$680,476 | -$525,714 | $96,275 |
| City launches (3 cities) | $450,000 | 30% |
Adds up to the $1,500,000 raise.
**Team.** Your Name, Founder and CEO. Ran the Austin pilot with 210 owners.
Not investment advice.
| Runway | the cash lasts all 36 months (low point $252,553 in month 28) |
| Peak burn | $82,801 a month (month 5) |
| LTV:CAC | 4.24x (LTV $254.29, CAC $60.00) |
| Take rate | 15% of a $25.00 walk |
| US homes with a dog | 71,000,000 homes | 
| Share of dog homes that pay for walks | 10% | 
| Walking spend per paying home a year | $1,200 | 
| TAM, the whole market | $8,520,000,000 ($8.5B) |
Method: top-down. The inputs and where each comes from:
- SOM = 10% of the SAM.
| **Total** | **$1,500,000** | **100%** |
- Breakeven: month 29 (May 2029), and every month after it.
- Peak burn: $82,801 in month 5.
- Runway: the cash lasts all 36 months (low point $252,553 in month 28).
_From seed/model.json, Jan 2027 to Dec 2029: projections from your own assumptions, not a forecast._
| Walks per customer a month | 8 |
What the competitors' own reviews say (seed/competitors.json, 2026-10-25):
EOF
for i in 1 2 3 4 5 6 7 8 9; do t=$(J -r ".[$((i-1))]" <<<"$SECTION_TITLES"); has "$M" "## $i. $t" "section $i heading"; done
eq "${M: -1}" "_" "plan.md ends with one newline (\$(...) strips it)"; eq "$(tail -c 2 seed/plan.md | od -An -c | tr -d ' ')" '_\n' "one newline"
eq "$(sed -n 3p seed/plan.md)" "**A dog-walking app with flat prices and a backup walker on every walk.**" "the one-liner"
eq "$(J '.flags, .notes, .built' seed/plan.json | tr '\n' ' ')" '[] [] "2026-10-26" ' "no flags, no notes, built"
eq "$(J '.numbers.use_of_funds_pct' seed/plan.json)" '{"Product and engineering":40,"City launches (3 cities)":30,"Marketing":20,"Operations and insurance":10}' "use of funds pct"
eq "$(J '[.numbers.competitors[]|[.name,.top_count,.low_reviews]]' seed/plan.json)" '[["Rover",106,341],["Wag!",81,324]]' "competitors"
eq "$(J '[.numbers.unit_economics|.price,.take_rate_pct,.revenue_per_order,.cost_per_order,.contribution_per_order,.contribution_per_customer_month,.ltv,.ltv_cac,.cac_payback_months]' seed/plan.json)" \
   "[25,15,3.75,1.525,2.225,17.8,254.29,4.24,3.37]" "unit economics"
eq "$(J '[.numbers.unit_economics.costs[].amount]' seed/plan.json)" "[1.025,0.5]" "cost amounts"
eq "$(J 'keys_unsorted' seed/plan.json)" '["created_by","tagline","brand","sections","team","numbers","flags","notes","built","startup","founder"]' "dict(plan, ...) key order"
P check; eq "$CODE" 0 "check clean"; eq "$OUT" "Every check passed: no empty sections, and the numbers agree between files." "check text"
P check --json; eq "$OUT" '{
  "ok": true,
  "flags": [],
  "notes": []
}' "check --json"
P build --today 2026-10-26 --json; eq "$CODE" 0 "build --json"; eq "$(J '.wrote, .sections_written' <<<"$OUT" | tr '\n' ' ')" '["seed/plan.md","seed/plan.json"] 9 ' "build json"
eq "$(J 'keys_unsorted' <<<"$OUT")" '["wrote","sections_written","flags","notes","numbers"]' "build json keys"
eq "$(J '.numbers' <<<"$OUT")" "$(J '.numbers' "$GOLD")" "build json numbers"
cmp -s seed/plan.json "$GOLD" && ok || bad "a second build is the same"
P --dir seed build --today 2026-10-26; eq "$CODE" 0 "--dir before"; mv seed other; P build --dir other --today 2026-10-26; eq "$CODE" 0 "--dir after"
eq "$(head -5 <<<"$OUT" | tail -1)" "Wrote other/plan.md and other/plan.json." "the dir in the paths"

# ---- dates: date.fromisoformat (3.11+), and the day without a leading zero
fresh; P build --today 20261005; has "$(cat seed/plan.md)" "Built Oct 5, 2026 from" "YYYYMMDD, no zero"; eq "$(J -r .built seed/plan.json)" "2026-10-05" "built iso"
P build --today 2026-W43-1; eq "$(J -r .built seed/plan.json)" "2026-10-19" "ISO week date"
P build --today 2026-02-30; eq "$CODE" 2 "Feb 30"; eq "$ERR" "plan.py: --today wants YYYY-MM-DD (got '2026-02-30')" "bad day message"
P build --today 26/10/2026; eq "$CODE" 2 "bad today"; has "$ERR" "--today wants YYYY-MM-DD" "bad today message"

# ---- init (test_init_writes_the_shared_config_and_the_skeleton)
fresh empty
P init --name Leashly --one-liner "A dog-walking app" --raise '$1.5M' --instrument "post-money SAFE" --founder "Your Name" --email you@leashly.example \
  --tagline "Dog walks without the surprise fees." --brand "#112233"
eq "$CODE" 0 "init"; has "$OUT" "Wrote seed/config.json." "wrote config"; has "$OUT" "Wrote seed/plan.json." "wrote plan"; hasnt "$OUT" "no raise yet" "has a raise"
eq "$(tail -1 <<<"$OUT")" "Next: write the nine sections into seed/plan.json with the founder (text, a one-line pitch, up to 3 points), run /seed-scout, /seed-market and /seed-model, then plan.py build." "next"
C1=$(cat seed/config.json)
eq "$(J '.startup|keys' seed/config.json)" '["geography","instrument","name","one_liner","raise","sector","stage"]' "startup keys"
eq "$(J '.startup.raise, .startup.stage, .sec_user_agent' seed/config.json | tr '\n' ' ')" '1500000 "seed" "Your Name you@leashly.example" ' "raise, stage, user agent"
eq "$(J '.founder|keys' seed/config.json)" '["company_address","email","name"]' "founder keys"
eq "$(J '.outreach' seed/config.json)" '{"daily_cap":20,"followup_days":[3,7],"max_touches":3,"business_days":true}' "outreach"
EX="$FIX/seed/seed-plan/config.example.json"
eq "$(J '[keys[]|select(startswith("_")|not)]' "$EX")" "$(J 'keys' seed/config.json)" "the example's keys"; eq "$(J '.startup|keys' "$EX")" "$(J '.startup|keys' seed/config.json)" "the example's startup keys"
eq "$(J '.outreach' "$EX")" "$(J '.outreach' seed/config.json)" "the example's outreach"
eq "$(J '[.sections[].title]' seed/plan.json)" "$SECTION_TITLES" "section titles"
eq "$(J '[.sections[]|.text == "" and .pitch == "" and .points == []]|all' seed/plan.json)" true "empty sections"
eq "$(J '.brand, .team' seed/plan.json | tr '\n' ' ')" '{"brand":"#112233","ink":"#0E1B33","accent":"#FFB020"} [{"name":"Your Name","role":"Founder","line":""}] ' "brand and team"
eq "$(J 'keys_unsorted' seed/plan.json)" '["created_by","tagline","brand","sections","team","numbers","flags","notes","built"]' "skeleton keys"
eq "$(sed -n '1,4p' seed/plan.json)" '{
  "created_by": "seed-plan",
  "tagline": "Dog walks without the surprise fees.",
  "brand": {' "json.dump indent=2"
eq "$(grep -c '"points": \[\]' seed/plan.json)" 9 "empty lists inline"; eq "$(tail -c 2 seed/plan.json | od -An -c | tr -d ' ')" '}\n' "a newline at the end"
P init --name X --one-liner Y; eq "$CODE" 1 "a plan is there"; eq "$ERR" "plan.py: seed/plan.json already exists. Edit it, or use --force to start again (that erases its sections)." "already exists"
rm seed/plan.json; P init; eq "$CODE" 0 "an existing config is kept"; has "$OUT" "Kept the existing seed/config.json (use --force to replace it)." "kept"
eq "$(cat seed/config.json)" "$C1" "config untouched"
P init --force --name Other --one-liner Z; eq "$CODE" 0 "--force"; eq "$(J -r .startup.name seed/config.json)" Other "replaced"; has "$OUT" "no raise yet" "the new config has no raise"
eq "$(J '.team' seed/plan.json)" '[{"name":"","role":"Founder","line":""}]' "no founder: an empty name"
# the fixture's own config, from init with its values (less the _about note)
fresh empty
P init --name Leashly --one-liner "A dog-walking app with flat prices and a backup walker on every walk" --sector "pet care marketplace" --geography "United States" \
  --raise 1500000 --instrument "post-money SAFE" --founder "Your Name" --email you@leashly.example --address "100 Example Street, Austin, TX 78701"
eq "$(J . seed/config.json)" "$(J 'del(._about)' "$LF/config.json")" "init writes the fixture's config, in its key order"
# init refusals (test_init_refusals)
fresh empty; P init; eq "$CODE" 2 "init needs a name"; eq "$ERR" "plan.py: init needs --name and --one-liner (the startup's name and what it does, in one line)" "init message"
P init --name A --one-liner B --brand blue; eq "$CODE" 2 "bad brand"; eq "$ERR" "plan.py: --brand wants a colour like #2F6FEB (got 'blue')" "bad brand message"
P init --name A --one-liner B --accent "#12345g"; eq "$ERR" "plan.py: --accent wants a colour like #2F6FEB (got '#12345g')" "bad accent message"
P init --name A --one-liner B --raise lots; eq "$CODE" 2 "bad raise"; eq "$ERR" "plan.py: --raise wants an amount like 1500000, \$1.5M or 750k (got 'lots')" "bad raise message"
[ -e seed/plan.json ] && bad "no plan on a bad raise" || ok; [ -e seed/config.json ] && bad "no config on a bad raise" || ok
P init --name A --one-liner B; eq "$CODE" 0 "no raise"; has "$OUT" "Note: config.json has no raise yet. Set startup.raise (a number in dollars)." "no raise note"
eq "$(J '.startup.raise, .sec_user_agent' seed/config.json | tr '\n' ' ')" 'null "" ' "raise null, no user agent"
# parse_money (test_parse_money), through init --raise
for c in "1500000=1500000" "1,500,000=1500000" '$1.5M=1500000' "750k=750000" "2B=2000000000" ' $250K =250000' "1.2345k=1234.5" "2.5m=2500000" '$ 3M=3000000'; do
  fresh empty; P init --name A --one-liner B --raise "${c%=*}"; eq "$(J .startup.raise seed/config.json)" "${c##*=}" "parse_money ${c%=*}"
done
for b in "lots" "1.5 million dollars" "-5" "1.5 M M" "1e6"; do fresh empty; P init --name A --one-liner B --raise "$b"; eq "$CODE" 2 "parse_money refuses $b"; done
fresh empty; P init --name A --one-liner B --raise ""; eq "$CODE" 0 "an empty --raise is no raise"; eq "$(J .startup.raise seed/config.json)" null "empty raise"
fresh empty; P init --name A --one-liner B --brand $'#112233\n'; eq "$CODE" 0 "re \$ matches before a final newline"; eq "$(J .brand.brand seed/plan.json)" '"#112233\n"' "kept as given"
fresh empty; P init --name A --one-liner B --stage "" --tagline ""; eq "$(J .startup.stage seed/config.json)" '"seed"' "an empty stage is seed"

# ---- flags (test_flags_disagreements)
while IFS='|' read file filt want; do
  fresh; ed "$file" "$filt"; flags
  [ "$(jq --arg w "$want" '[.[]|contains($w)]|any' <<<"$FL")" = true ] && ok || bad "flag: $want (got $FL)"
  eq "$CODE" 1 "flag exit: $want"
done <<'EOF'
config.json|.startup.raise = 1200000|The raise disagrees: config.json says $1,200,000, model.json says $1,500,000.
model.json|.summary.use_of_funds.Marketing = 250000|The use of funds adds up to $1,450,000, not the $1,500,000 raise.
market.json|.som = 300000000|The SOM ($300M) is bigger than the SAM ($228M).
market.json|.sam = 9000000000|The SAM ($9B) is bigger than the TAM ($8.5B).
market.json|.som = 15000000|Year 3 GMV ($20M) is above your SOM ($15M): the model sells more
market.json|.currency = "EUR"|market.json is in EUR but model.json is in USD.
model.json|.summary.revenue_y2 = 1|model.json says year 2 revenue is $1 but its months add up to $1,586,640
model.json|.assumptions.price = 30|model.json's unit economics (revenue_per_order, contribution_per_order, ltv, ltv_cac, cac_payback_months) no longer match its assumptions
model.json|.assumptions.take_rate_pct = 5|Each walk loses money before opex: contribution is -$0.28.
plan.json|.sections[2].text = ""|Solution is empty: write it in seed/plan.json (sections[2].text).
config.json|.startup.raise = null|config.json has no startup.raise
config.json|.startup.name = ""|config.json has no startup.name.
market.json|del(.sam)|market.json needs tam, sam and som: run /seed-market.
model.json|.summary.use_of_funds = {} \| .assumptions.use_of_funds = {}|No use of funds in model.json: add use_of_funds to seed/assumptions.json.
config.json|.startup.raise = 0|config.json has no startup.raise: how much are you raising?
EOF
fresh; rm seed/model.json seed/market.json seed/competitors.json; flags
for f in "No seed/model.json: run /seed-model first, then build again." "No seed/market.json: run /seed-market first, then build again." "No seed/competitors.json: run /seed-scout first, then build again."; do
  [ "$(jq --arg w "$f" 'index($w) != null' <<<"$FL")" = true ] && ok || bad "missing: $f"
done
[ "$(J '[.[]|startswith("Problem text: 106, 341, 81, 324 are in none")]|any' <<<"$NT")" = true ] && ok || bad "untraced problem numbers: $NT"
has "$NT" "Problem text: 106, 341, 81, 324 are in none of the seed/ files. Check them." "the whole note"
P build --today 2026-10-26; eq "$CODE" 0 "build still writes"; has "$OUT" "  FIX   No seed/model.json" "FIX line"; has "$OUT" "fix the items marked FIX" "next: fix"
has "$(cat seed/plan.md)" "- **Fix:** No seed/model.json" "Fix in plan.md"; has "$(cat seed/plan.md)" "- Note: Problem text: 106" "Note in plan.md"
P check; eq "$CODE" 1 "check exit 1"; has "$OUT" "  note  Problem text: 106" "note line"
eq "$(J '.numbers|keys_unsorted' seed/plan.json)" '["currency","raise","instrument","stage"]' "only the config's numbers"
eq "$(grep -c '^| ' seed/plan.md)" 3 "no market, no model: the key numbers are the raise row alone"; has "$(cat seed/plan.md)" "| The raise | \$1,500,000 seed |" "the raise row"
# a missing price is a refusal with the fix (test_unit_economics)
fresh; ed model.json '.assumptions = {"price": 25}'; P check; eq "$CODE" 2 "assumptions with only a price"
eq "$ERR" "plan.py: model.json's assumptions have no 'take_rate_pct': run model.py run --write again" "missing key message"
fresh; ed model.json '.assumptions.cac = "lots"'; P check; eq "$ERR" "plan.py: cac is not a number: 'lots'" "a named bad number"
fresh; ed model.json '.months[3].revenue = null'; P check; eq "$ERR" "plan.py: a number is not a number: None" "the default name, as in plan.py"

# ---- notes (test_notes): a weaker model from seed-model's own tool
fresh; jq '.assumptions | .cac = 250 | .raise = 300000 | .use_of_funds = {"All of it": 300000}' seed/model.json > seed/assumptions.json
run seed-model/model run --write >/dev/null; eq "$CODE" 0 "seed-model wrote the weaker model"
ed config.json '.startup.raise = 300000'
ed plan.json '.tagline = "" | .team = [] | .sections[1].pitch = "" | .sections[3].text += " It grows 12% a year."'
flags; eq "$FL" "[]" "notes only"; eq "$CODE" 0 "notes exit 0"
for w in "LTV:CAC is 1.02x, under the 3x" "CAC payback is 14.04 months, over the 12" "The cash runs out in month" "No breakeven inside the 36 months" \
         "Market text: 12% is in none of the seed/ files." "No tagline" "No team" "Problem has no pitch line"; do has "$NT" "$w" "note: $w"; done
CO=$(jq .summary.cash_out_month seed/model.json); RW=$(jq .summary.runway_months seed/model.json); DT=$(jq -r ".months[$((CO-1))].date" seed/model.json)
MN=$(echo "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec" | cut -d' ' -f$((10#${DT#*-}))); YR=${DT%-*}
has "$NT" "The cash runs out in month $CO ($MN $YR): the raise covers $RW months. Say what it buys by then, or raise more." "the cash-out note, in full"
P build --today 2026-10-26; has "$(cat seed/plan.md)" "| Runway | $RW months: the cash runs out in month $CO |" "runway row"; has "$(cat seed/plan.md)" "| Breakeven | not inside the model |" "no breakeven row"
has "$(cat seed/plan.md)" "- Breakeven: not inside the model." "no breakeven line"

# ---- missing files and bad JSON (test_missing_files_and_bad_json)
fresh empty; P build; eq "$CODE" 2 "no seed"; eq "$ERR" "plan.py: no seed/ folder here: run plan.py init first" "no folder message"
mkdir seed; P check; eq "$ERR" "plan.py: no config (run plan.py init first) at seed/config.json" "no config message"
fresh; printf '{not json' > seed/market.json; P check; eq "$CODE" 2 "bad JSON"
eq "$ERR" "plan.py: seed/market.json is not valid JSON: Expecting property name enclosed in double quotes: line 1 column 2 (char 1)" "bad JSON message"
fresh; P; eq "$CODE" 2 "no command"; has "$OUT" "usage: plan.py [-h] [--dir DIR] {init,build,check} ..." "help"
P check --today 2026-10-26; eq "$CODE" 2 "check has no --today"

# ---- market as objects, a missing top theme (test_market_as_objects_and_a_missing_top_theme)
fresh; ed market.json '.tam = {"value": 8520000000} | .sam = {"value": 228000000} | .som = {"value": 22800000}'
ed competitors.json '.apps |= map(del(.top_theme, .weak_point_count)) | del(.theme_labels)'
P build --today 2026-10-26 --json; eq "$(J .flags <<<"$OUT")" "[]" "no flags"; eq "$(J .numbers.market.som <<<"$OUT")" 22800000 "som from an object"
eq "$(J -r '.numbers.competitors[0].top_theme' <<<"$OUT")" fees "top theme from the counts"
eq "$(J '[.numbers.competitors[]|[.top_count, .top_label]]' <<<"$OUT")" '[[106,"fees and pricing"],[81,"cancellations and no-shows"]]' "count from the themes, label from weak_point"
fresh; ed competitors.json '.apps[0] |= (del(.top_theme, .weak_point_count, .weak_point) | .themes = {"b": 5, "a": 5, "c": 2})'
P build --today 2026-10-26 --json; eq "$(J '.numbers.competitors[0]|[.top_theme,.top_count,.top_label,.weak_point]' <<<"$OUT")" '["a",5,"a","a"]' "ties go to the first name"
fresh; ed market.json '.som = "lots"'; P check --json; has "$(J .flags <<<"$OUT")" "market.json needs tam, sam and som" "a som that is not a number is missing"

# ---- hand-worked edges
fresh; ed config.json '.startup.stage = "" | .startup.instrument = ""'; P build --today 2026-10-26; M=$(cat seed/plan.md)
has "$M" "Raising a \$1,500,000 round. Built Oct 26, 2026 from the files in seed/." "no stage: one space, no instrument"; has "$M" "| The raise | \$1,500,000 |" "the row is stripped"
fresh; ed config.json '.startup.raise = null'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "The raise is not set yet on a post-money SAFE. Built" "no raise line"
fresh; ed config.json '.startup.raise = "$1.5M"'; flags; eq "$FL" "[]" "a raise written as money"; eq "$(J .numbers.raise <<<"$(P build --json --today 2026-10-26; echo "$OUT")")" 1500000 "parsed"
fresh; ed config.json '.startup.raise = "lots"'; P check; eq "$ERR" "plan.py: --raise wants an amount like 1500000, \$1.5M or 750k (got 'lots')" "a bad config raise uses the --raise message"
fresh; ed market.json '.inputs = [{"name":"A | B","value":"1,000","unit":"count"},{"name":"C","value":10.5,"unit":"% of homes"},{"name":"D","value":2.5,"unit":""},
  {"name":"E","value":1234.5,"unit":"count"},{"name":"F","value":158,"unit":"USD per year"},{"name":"G","value":"abc","unit":"homes"},{"name":"H","value":7,"unit":"pct"},
  {"name":"I","value":3,"unit":"$","source":"x"},{"name":"J","value":4,"unit":"usd"},{"name":"K","value":5,"unit":"EUR"}] | .notes = ["a","","b"]'
P build --today 2026-10-26; M=$(cat seed/plan.md)
for l in "| A \\| B | 1,000 | MISSING: add a source |" "| C | 10.5% of homes |" "| D | 2.5 |" "| E | 1,234.5 |" "| F | \$158 per year |" "| G | abc |" "| H | 7% |" \
         "| I | \$3 | x |" "| J | \$4 |" "| K | 5 EUR |"; do has "$M" "$l" "input: $l"; done
has "$M" "- a
- b

## 5. Competition" "notes as a list"
fresh; ed market.json '.notes = "One line."'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "One line.

## 5. Competition" "notes as a string"
fresh; ed competitors.json '.low_stars = 1'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "106 of 341 recent 1 star reviews" "1 star"
ed competitors.json '.low_stars = 3'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "recent 1 to 3 star reviews" "1 to 3 star"
fresh; ed competitors.json '.apps[0].ratings = 515771.0 | .apps[0].rating = null | del(.apps[0].reviews_read) | .apps[1].weak_point = "ünïcode"'
P build --today 2026-10-26; M=$(cat seed/plan.md); has "$M" "| Rover |  |  |  | Fees and pricing | 106 of 341 mention fees and pricing |" "no rating, a float count, no reads"
has "$M" "| Ünïcode |" "cap: the first letter, Unicode"
fresh; ed model.json '.summary.breakeven_month = 29.7'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "| Breakeven | month 29 (May 2029) |" "%d truncates a float"
fresh; ed model.json '.summary.breakeven_month = "29"'; P build --today 2026-10-26; eq "$CODE" 1 "%d of a str is a TypeError"
fresh; ed model.json '.months = .months[0:30]'; P build --today 2026-10-26 --json; eq "$(J '[.numbers.financials.years[].year]' <<<"$OUT")" "[1,2]" "only whole years"
has "$(cat seed/plan.md)" "| | Year 1 | Year 2 |
| --- | ---: | ---: |" "a two-year table"
fresh; ed plan.json '.sections |= map(select(.key != "market")) | .sections += [{"key":"market","title":"The market","text":"Big.","pitch":"Big.","points":["", "Huge"]}]'
P build --today 2026-10-26; M=$(cat seed/plan.md); has "$M" "## 4. The market

Big.

- Huge
" "sections in the standard order, own title, no empty points"
fresh; ed plan.json '.sections[0].points = ["$9B"] | .sections[0].pitch = "$22.8M SOM, 4.2x."'; flags
eq "$NT" '["Executive summary points: $9B is in none of the seed/ files. Check it."]' "points are checked, rounded numbers trace"
fresh; ed plan.json '.sections[0].points = [5]'; P check; eq "$CODE" 1 "a non-str point is a TypeError"
fresh; ed model.json '.unit_economics.ltv = null'; flags; eq "$FL" "[]" "a None in the model's unit economics is not stale"
fresh; ed model.json '.summary.currency = null | .summary.use_of_funds = null'; P build --today 2026-10-26 --json; eq "$(J .numbers.currency <<<"$OUT")" '"USD"' "currency falls back"
eq "$(J '.numbers.use_of_funds|keys|length' <<<"$OUT")" 4 "use of funds from the assumptions"
fresh; ed config.json '._secret = 777777'; ed plan.json '.sections[0].pitch = "777,777 owners."'; flags; has "$NT" "777,777 is in none" "_ keys in config.json are notes, not sources"
fresh; ed model.json '.months[0].orders = 1234567.5'; P build --today 2026-10-26; has "$(cat seed/plan.md)" "| Walks | 1," "orders with a fraction"
grep -q '^| Walks | [0-9,]*\.[0-9]* |' seed/plan.md && ok || bad "{:,} of a float keeps the fraction"
fresh; ed config.json '.startup.name = null'; P build --today 2026-10-26; has "$OUT" "Your startup: business plan, 9 of 9 sections written." "no name"; has "$(head -1 seed/plan.md)" "# Your startup: business plan" "no name heading"

done_ "seed-plan/plan"

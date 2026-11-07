#!/usr/bin/env bash
# seed-model/model: the assertions of seed-skill/tests/test_model.py, run against the C++ tool, plus hand-worked edge
# cases. leashly.model.json is the upstream tool's own output for assumptions.example.json (it is the seed-plan
# fixture), so the 36 months must match it byte for byte; describe.txt is the example block from the upstream SKILL.md.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
MF="$FIX/seed/seed-model"; SKD="$MF/skill"; EX="$SKD/assumptions.example.json"; TINY="$MF/tiny.json"; BADF="$MF/bad.json"
M() { run --skill-dir "$SKD" seed-model/model "$@"; }
J() { jq -c "$@"; }
mkdir -p "$TMP/w"; cd "$TMP/w" || exit 1

# ---- init, run, write: the reel, byte for byte
M init; eq "$CODE" 0 "init"
eq "$OUT" "Wrote seed/assumptions.json (the Leashly example: replace every number with yours).
Next: edit it with the founder, then model.py run --write." "init message"
eq "$(jq -S . seed/assumptions.json)" "$(jq -S 'walk(if type=="number" then .+0 else . end)' "$EX" | jq -S .)" "init writes the example"
eq "$(grep -c '"fixed": 0.3$' seed/assumptions.json)" "1" "re-serialized by json.dump (0.30 -> 0.3)"
M init; eq "$CODE" 1 "init again"; eq "$ERR" "model.py: seed/assumptions.json already exists. Edit it, or use --force to start again from the example." "already exists"
M init --force; eq "$CODE" 0 "init --force"
M run; eq "$CODE" 0 "run"; [ -e seed/model.json ] && bad "run writes nothing" || ok
eq "$(tail -1 <<<"$OUT")" "Nothing written. Add --write to save seed/model.json." "nothing written"
okd=1; while IFS= read -r l; do [ -z "$l" ] && continue; grep -qxF -- "$l" <<<"$OUT" || { okd=0; echo "  missing: [$l]"; }; done < "$MF/describe.txt"; eq "$okd" 1 "the SKILL.md example block is the real output"
eq "$(sed -n 1,17p <<<"$OUT")" "$(cat "$MF/describe.txt")" "the block in order"
M run --write; eq "$CODE" 0 "run --write"; has "$OUT" "Wrote seed/model.json." "wrote"; has "$OUT" "Next: /seed-plan pulls these numbers into the business plan, then /seed-deck." "next"
cmp -s seed/model.json "$MF/leashly.model.json" && ok || bad "model.json is byte-identical to the upstream output"
MJ=seed/model.json
eq "$(J '.unit_economics|[.revenue_per_order,.cost_per_order,.contribution_per_order,.contribution_per_customer_month,.ltv,.ltv_cac,.cac_payback_months]' $MJ)" "[3.75,1.525,2.225,17.8,254.29,4.24,3.37]" "unit economics"
eq "$(J '.months[0]|[.date,.churned,.customers,.orders,.gmv,.revenue,.cost_of_revenue,.gross_profit,.payroll,.marketing,.other_opex,.net,.cash]' $MJ)" '["2027-01",15,345,2760,69000,10350,4209,6141,28800,9000,7000,-38659,1461341]' "month one by hand"
eq "$(J '[.months[1].payroll,.months[4].other_opex,.months[4].payroll,.months[5].other_opex]' $MJ)" "[42000,32000,54600,7000]" "hires and one-offs"
eq "$(J '[.months[11].new_customers,.months[12].new_customers,.months[24].new_customers]' $MJ)" "[428,449,784]" "growth per year (150 x 1.1^11 = 427.97)"
eq "$(J '.summary|[.revenue_y1,.revenue_y2,.revenue_y3,.gmv_y3,.breakeven_month,.breakeven_date,.peak_burn,.peak_burn_month,.runway_months,.cash_out_month,.cash_low,.cash_low_month,.customers_end,.funds_match,.use_of_funds_total]' $MJ)" '[467400,1586640,2993520,19956800,29,"2029-05",82801,5,36,null,252553,28,9999,true,1500000]' "the reel summary"
eq "$(J 'keys_unsorted' $MJ)" '["created_by","assumptions","months","summary","unit_economics","note"]' "top-level order"
M run --json; eq "$(J 'keys_unsorted' <<<"$OUT")" '["created_by","assumptions","summary","unit_economics","note"]' "json without months"
eq "$(J .summary.breakeven_month <<<"$OUT")" "29" "json breakeven"
eq "$(jq -S . <<<"$OUT")" "$(jq -S 'del(.months)' $MJ)" "json = model.json without months"
M run --json --months; eq "$(J '.months|length' <<<"$OUT")" "36" "json months"
M run --months; has "$OUT" "     29  2029-05" "months table"
has "$OUT" "  month     date customers     walks          GMV     revenue gross profit        opex         net          cash" "months header"
has "$OUT" "      1  2027-01       345     2,760      \$69,000     \$10,350       \$6,141     \$44,800    -\$38,659    \$1,461,341" "months row 1"

# ---- csv
M csv; eq "$CODE" 0 "csv"; eq "$OUT" "Wrote seed/model.csv (36 months, 16 columns)." "csv message"
eq "$(wc -l < seed/model.csv | tr -d ' ')" "37" "37 rows"
eq "$(head -1 seed/model.csv)" "month,date,new_customers,churned,customers,orders,gmv,revenue,cost_of_revenue,gross_profit,payroll,marketing,other_opex,opex,net,cash" "csv header"
eq "$(sed -n 2p seed/model.csv | cut -d, -f1-8)" "1,2027-01,150,15,345,2760,69000.00,10350.00" "csv row 1"
eq "$(sed -n 3p seed/model.csv | cut -d, -f10)" "8650.80" "csv cents"
eq "$(sed -n 2p seed/model.csv | cut -d, -f15)" "-38659.00" "csv negative"
grep -q $'\r' seed/model.csv && bad "csv lines end in \\n" || ok
jq '.price = 30' seed/assumptions.json > "$TMP/a.json" && mv "$TMP/a.json" seed/assumptions.json
M csv; has "$OUT" "Note: seed/assumptions.json changed since seed/model.json was written. Run model.py run --write to refresh it." "stale note"
M csv --out -; eq "$(head -1 <<<"$OUT")" "month,date,new_customers,churned,customers,orders,gmv,revenue,cost_of_revenue,gross_profit,payroll,marketing,other_opex,opex,net,cash" "csv to stdout"
hasnt "$OUT" "Note:" "no note on stdout csv"
M csv --out "$TMP/x/y.csv"; eq "$CODE" 0 "csv --out makes dirs"; [ -f "$TMP/x/y.csv" ] && ok || bad "csv out file"
# 1 == 1.0 and key order do not make it stale; _about notes are ignored
jq '.price = 25.0 | ._note = "x" | to_entries | reverse | from_entries' seed/assumptions.json > "$TMP/a.json" && mv "$TMP/a.json" seed/assumptions.json
M csv; hasnt "$OUT" "changed since" "25.0 == 25, order and _notes ignored"
echo '{' > seed/assumptions.json; M csv; eq "$CODE" 0 "unreadable assumptions: not stale"; hasnt "$OUT" "changed since" "bad JSON is not stale"
jq '.months[0].date = "Jan, 2027" | .months[1].gmv = "$1,234.5" | .months[2].churned = null | .months[3].orders = 2.5' "$MF/leashly.model.json" > seed/model.json
M csv --out -; eq "$(sed -n 2p <<<"$OUT" | cut -c1-15)" '1,"Jan, 2027",1' "a comma is quoted"
eq "$(sed -n 3p <<<"$OUT" | cut -d, -f7)" "1234.50" "money cells go through D()"
eq "$(sed -n 4p <<<"$OUT" | cut -d, -f4)" "" "None is empty"
eq "$(sed -n 5p <<<"$OUT" | cut -d, -f6)" "2.5" "a float as str()"
jq '.months[0].gmv = null' "$MF/leashly.model.json" > seed/model.json; M csv; eq "$CODE" 2 "a null money cell"; eq "$ERR" "model.py: a number is not a number: None" "null money message"
jq '.months |= .[:35]' "$MF/leashly.model.json" > seed/model.json; M csv; eq "$CODE" 2 "35 months"; eq "$ERR" "model.py: seed/model.json has no 36 months: run model.py run --write again" "months message"
echo '[1]' > seed/model.json; M csv; eq "$CODE" 1 "a list is not a model (AttributeError)"

# ---- tiny: by hand
mkdir -p "$TMP/t"; cd "$TMP/t" || exit 1
M --assumptions "$TINY" run --json --months; T1=$OUT
eq "$(J '[.months[:3][].customers]' <<<"$T1")" "[10,19,27]" "tiny customers"
eq "$(J '[.months[:3][].net]' <<<"$T1")" "[-1420,-1348,-1284]" "tiny net"
eq "$(J '[.months[:3][].cash]' <<<"$T1")" "[8580,7232,5948]" "tiny cash"
eq "$(J '[.months[1].date,.months[2].date]' <<<"$T1")" '["2027-12","2028-01"]' "across the year end"
eq "$(J '.unit_economics|[.contribution_per_order,.ltv,.ltv_cac,.cac_payback_months]' <<<"$T1")" "[8,80,1.6,6.25]" "tiny unit economics"
cust=0; cash=0; out=""; for k in $(seq 1 36); do cust=$((cust - (cust + 5) / 10 + 10)); cash=$((cash + cust * 8 - 1500 + (k == 1 ? 10000 : 0))); [ -z "$out" ] && [ $cash -lt 0 ] && out=$k; done
eq "$(J '.summary|[.cash_out_month,.runway_months,.breakeven_month,.peak_burn,.peak_burn_month,.funds_match]' <<<"$T1")" "[$out,$((out - 1)),null,1420,1,true]" "tiny summary (cash out in month $out)"
eq "$(J '[.months[].cash]|.[35]' <<<"$T1")" "$cash" "tiny cash end in integers"
eq "$(J '.assumptions|has("_about")' <<<"$T1")" "false" "_about cleaned"
jq '.raise_month = 3' "$TINY" > late.json; M --assumptions late.json run --json
eq "$(J '.summary|[.cash_out_month,.runway_months]' <<<"$OUT")" "[1,0]" "broke before the money lands"
jq '.payroll = [] | .cac = 0' "$TINY" > rich.json; M --assumptions rich.json run --json
eq "$(J '.summary|[.breakeven_month,.peak_burn,.peak_burn_month]' <<<"$OUT")" "[1,0,null]" "money from month one"
M --assumptions rich.json run; has "$OUT" "  Peak burn          none: every month makes money" "no burn line"; has "$OUT" "  Breakeven          month 1 (Nov 2027), and every month after it" "breakeven month 1"
M --assumptions "$TINY" run; has "$OUT" "  Breakeven          not within 36 months" "never breaks even"
has "$OUT" "  Runway             $((out - 1)) months: the cash runs out in month $out" "runway line"
has "$OUT" "  Orders " "units default to unit+s, capitalized"
has "$OUT" "  Per customer       LTV \$80.00 on a \$50.00 CAC: LTV:CAC 1.6x, CAC paid back in 6.25 months" "per customer line"
has "$OUT" "  Per order          price \$100.00, you keep \$10.00 (10%), costs \$2.00, contribution \$8.00" "per order line"

# ---- refusals: every problem listed, in order
M --assumptions "$BADF" run; eq "$CODE" 2 "bad"
eq "$ERR" "model.py: the assumptions need fixing:
  - start_month wants YYYY-MM, like 2027-01 (got '2027-13')
  - price must be above 0 (got -5)
  - take_rate_pct must be 100 or less (got 120)
  - raise is not a number: 'lots'
  - customers.churn_monthly_pct must be under 100 (got 100)
  - customers.new_growth_pct wants one number or one a year (up to 3)
  - use_of_funds['Hiring'] must be above 0" "bad.json, every problem"
echo '{"start_month": "2027-01"}' > miss.json; M --assumptions miss.json run; eq "$CODE" 2 "missing"
eq "$ERR" "model.py: the assumptions need fixing:
  - price is missing
  - take_rate_pct is missing
  - raise is missing
  - cac is missing
  - customers is missing: {starting, new_month_1, new_growth_pct, churn_monthly_pct, orders_per_customer_month}" "missing fields"
jq 'del(.customers.churn_monthly_pct)' "$TINY" > nochurn.json; M --assumptions nochurn.json run; has "$ERR" "  - customers.churn_monthly_pct is missing" "churn missing"
echo '[1, 2]' > list.json; M --assumptions list.json run; eq "$CODE $ERR" "2 model.py: the assumptions must be a JSON object" "not an object"
bad1() { jq "$1" "$TINY" > e.json; M --assumptions e.json run; eq "$CODE" 2 "$3 (code)"; eq "$ERR" "model.py: the assumptions need fixing:
  - $2" "$3"; }
bad1 '.price = true' "price is not a number: True" "bool is not a number"
bad1 '.price = []' "price is not a number: []" "a list is not a number"
bad1 '.price = "abc"' "price is not a number: 'abc'" "text is not a number"
bad1 '.price = 0' "price must be above 0 (got 0)" "price 0"
bad1 '.price = 0.0' "price must be above 0 (got 0.0)" "price 0.0 prints as a float"
bad1 '.take_rate_pct = -1' "take_rate_pct must be 0 or more (got -1)" "take below 0"
bad1 '.raise_month = 0' "raise_month must be 1 or more (got 0)" "raise month 0"
bad1 '.raise_month = 37' "raise_month must be 36 or less (got 37)" "raise month 37"
bad1 '.payroll_overhead_pct = 201' "payroll_overhead_pct must be 200 or less (got 201)" "overhead"
bad1 '.customers.new_growth_pct = null' "customers.new_growth_pct[0] is not a number: None" "growth null"
bad1 '.customers.new_growth_pct = [1, -100]' "customers.new_growth_pct[1] must be above -100" "growth -100"
bad1 '.customers.new_growth_pct = []' "customers.new_growth_pct wants one number or one a year (up to 3)" "growth empty list"
bad1 '.cost_per_order = {"a": 1}' "cost_per_order[0] must be an object" "a dict iterates its keys"
bad1 '.payroll = ["x"]' "payroll[0] must be an object" "payroll item"
bad1 '.payroll = [{"monthly": 5, "start_month": 0, "end_month": 40}]' "payroll[0].start_month must be 1 or more (got 0)
  - payroll[0].end_month must be 36 or less (got 40)" "payroll months"
bad1 '.payroll = [{"start_month": 2}]' "payroll[0].monthly is missing" "payroll monthly"
bad1 '.opex = [{}]' "opex[0].monthly is missing" "opex monthly"
bad1 '.one_off = [{"cost": 1}]' "one_off[0].month is missing" "one-off month"
bad1 '.use_of_funds = [1]' "use_of_funds must be an object of {what: amount}" "funds list"
bad1 '.use_of_funds = {"It'"'"'s": "x"}' "use_of_funds[\"It's\"] is not a number: 'x'" "repr of a key with a quote"
bad1 '.start_month = 202701' "start_month wants YYYY-MM, like 2027-01 (got 202701)" "start month as a number"
bad1 '.start_month = null' "start_month wants YYYY-MM, like 2027-01 (got None)" "start month null"
bad1 '.customers.churn_monthly_pct = 100.0' "customers.churn_monthly_pct must be under 100 (got 100.0)" "churn 100.0"
jq '.cost_per_order = 5' "$TINY" > e.json; M --assumptions e.json run; eq "$CODE" 1 "a number is not iterable (TypeError)"
jq '.price = "NaN"' "$TINY" > e.json; M --assumptions e.json run; eq "$CODE" 1 "NaN cannot be compared (InvalidOperation)"
jq '.raise_month = "Infinity"' "$TINY" > e.json; M --assumptions e.json run; eq "$CODE" 1 "int(Infinity) (OverflowError)"

# ---- what D() accepts
ok1() { jq "$1" "$TINY" > o.json; M --assumptions o.json run --json --months; eq "$CODE" 0 "$3 (code)"; eq "$(J "$2" <<<"$OUT")" "$4" "$3"; }
ok1 '.price = " $$1,000.50 "' '.unit_economics.price' "strip, every leading \$, commas" "1000.5"
ok1 '.price = "1_000"' '.unit_economics.price' "underscores between digits" "1000"
ok1 '.price = "١٠٠"' '.unit_economics.price' "Arabic-Indic digits" "100"
ok1 '.price = "1e2"' '.unit_economics.price' "exponent" "100"
ok1 '.raise_month = 1.9' '.summary.raise_month' "int() truncates" "1"
ok1 '.customers.new_growth_pct = [10, 5]' '[.months[11].new_customers,.months[12].new_customers,.months[24].new_customers]' "two rates: the last repeats (28.53 x 1.05^13 = 53.8)" "[29,30,54]"
ok1 '.start_month = "٢٠٢٧-٠١"' '.summary.start_month' "Unicode digits in the start month" '"2027-01"'
ok1 '.start_month = "2027-01\n"' '.summary.start_month' "\$ matches before a final newline" '"2027-01"'
ok1 '.start_month = "9999-12"' '.summary.end_month' "past year 9999" '"10002-11"'
ok1 '.payroll = [{"monthly": 1000, "start_month": 2, "end_month": 3, "count": 2}]' '[.months[0:4][].payroll]' "start and end months, count" "[0,2000,2000,0]"
ok1 '.payroll_overhead_pct = 12.5 | .payroll[0].monthly = 1000.01' '.months[0].payroll' "overhead, to the cent half up (1125.01125)" "1125.01"
ok1 '.opex = [{"monthly": 10, "start_month": 3}] | .one_off = [{"month": 2, "cost": 0.005}]' '[.months[0:3][].other_opex]' "opex from its month, one-off cents half up" "[0,0.01,10]"
ok1 '.cash_at_start = -500' '.months[0].cash' "a negative start" "8080"
ok1 '.customers.starting = 2.5' '.months[0].customers' "starting half up (3), churn 0.3 -> 0" "13"
ok1 '.price = 33.33 | .take_rate_pct = 7.5' '.unit_economics|[.revenue_per_order,.contribution_per_order]' "exact decimals" "[2.49975,0.49975]"
ok1 '.customers.churn_monthly_pct = 0' '.unit_economics|[.ltv,.ltv_cac]' "no churn: no LTV" "[null,null]"
ok1 '.cac = 0' '.unit_economics|[.ltv,.ltv_cac,.cac_payback_months]' "no CAC: no LTV:CAC" "[80,null,0]"
ok1 '.cost_per_order = [{"fixed": 20}]' '.unit_economics|[.contribution_per_order,.cac_payback_months,.ltv]' "a loss per order" "[-10,null,-100]"
ok1 '.company = 5 | .units = "WALK RIDES"' '.summary.company' "company as str()" '"5"'
M --assumptions o.json run; has "$OUT" "  Walk rides " "units capitalized, the rest lower"
has "$OUT" "5: 36-month model, Nov 2027 to Oct 2030 (USD)" "describe head"
jq '.units = 5' "$TINY" > o.json; M --assumptions o.json run; eq "$CODE" 1 "units not a str (AttributeError)"
M --assumptions o.json run --json; eq "$CODE" 0 "units only matter for the text"

# ---- funds
M --assumptions "$EX" funds; eq "$CODE" 0 "funds example"
eq "$OUT" "Use of funds for the \$1,500,000 raise
  Product and engineering       \$600,000   40.0%
  City launches (3 cities)      \$450,000   30.0%
  Marketing                     \$300,000   20.0%
  Operations and insurance      \$150,000   10.0%
  Total                       \$1,500,000  100.0%
Adds up to the raise." "funds table"
jq '.use_of_funds.Marketing = 250000' "$EX" > short.json; M --assumptions short.json funds; eq "$CODE" 1 "short"
eq "$(tail -1 <<<"$OUT")" "Short by \$50,000: the use of funds adds up to \$1,450,000, the raise is \$1,500,000. Every dollar of the raise needs a job." "short line"
jq '.use_of_funds.Marketing = 300000.5' "$EX" > over.json; M --assumptions over.json funds; eq "$CODE" 1 "over"
eq "$(tail -1 <<<"$OUT")" "Over by \$0.50: the use of funds adds up to \$1,500,001, more than the \$1,500,000 raise." "over line (total half up)"
jq 'del(.use_of_funds)' "$EX" > none.json; M --assumptions none.json funds; eq "$CODE" 1 "no funds"
eq "$OUT" 'Use of funds for the $1,500,000 raise
  (none yet) Add use_of_funds to the assumptions: {"what": amount}, adding up to the raise.' "none yet"
M --assumptions none.json run --json; eq "$(J .summary.funds_match <<<"$OUT")" "false" "no funds, no match"
M --assumptions none.json run; has "$OUT" "  Use of funds       not set yet: add use_of_funds, then run model.py funds" "not set line"
M --assumptions short.json run; has "$OUT" "  Use of funds       adds up to \$1,450,000, not the \$1,500,000 raise: run model.py funds" "mismatch line"
jq '.use_of_funds.Marketing = 1' "$EX" > s1.json; M --assumptions s1.json funds --json; eq "$CODE" 1 "funds json code"; eq "$(J .ok <<<"$OUT")" "false" "funds json"
eq "$(J 'keys_unsorted' <<<"$OUT")" '["ok","lines"]' "funds json keys"
jq '.raise = 400 | .use_of_funds = {"A": 49, "Bé": 351}' "$TINY" > hu.json; M --assumptions hu.json funds; eq "$CODE" 0 "half-up pct"
eq "$OUT" 'Use of funds for the $400 raise
  A               $49   12.3%
  Bé             $351   87.8%
  Total          $400  100.0%
Adds up to the raise.' "12.25 -> 12.3 half up, widths in code points"
jq '.raise = 0 | .use_of_funds = {"A": 10}' "$TINY" > z.json; M --assumptions z.json funds; eq "$CODE" 1 "raise 0"
eq "$OUT" 'Use of funds for the $0 raise
  A               $10      0%
  Total           $10      0%
The raise is $0: set raise in the assumptions.' "raise 0 lines"
jq '.raise = 10 | .use_of_funds = {"A": 9.995}' "$TINY" > g.json; M --assumptions g.json funds; eq "$(tail -1 <<<"$OUT")" "Short by \$0.01: the use of funds adds up to \$10, the raise is \$10. Every dollar of the raise needs a job." "a fractional gap in cents"

# ---- exit codes, flags, argparse
mkdir -p "$TMP/x"; cd "$TMP/x" || exit 1
M run; eq "$CODE" 2 "run without assumptions"; eq "$ERR" "model.py: no assumptions (run model.py init first) at seed/assumptions.json" "init first"
M csv; eq "$CODE" 2 "csv without a model"; eq "$ERR" "model.py: no model (run model.py run --write first) at seed/model.json" "run first"
M funds --assumptions "$EX"; eq "$CODE" 0 "--assumptions after the command"
mkdir seed; echo '{"a": }' > seed/assumptions.json; M run; eq "$CODE" 2 "bad JSON"; has "$ERR" "model.py: seed/assumptions.json is not valid JSON: Expecting value: line 1 column 7 (char 6)" "JSON message"
rm -rf seed
M; eq "$CODE" 2 "no command"; has "$OUT" "usage: model.py [-h] [--dir DIR] [--assumptions PATH]" "usage"; has "$OUT" "{init,run,funds,csv}" "commands"
M run -h; eq "$CODE" 0 "run help"; has "$OUT" "--assumptions PATH" "sub --assumptions listed"; has "$OUT" "--months" "months listed"
M init --dir elsewhere; eq "$CODE" 0 "init --dir"; [ -f elsewhere/assumptions.json ] && ok || bad "elsewhere/assumptions.json"
M --dir elsewhere run --write; eq "$CODE" 0 "--dir before the command"; [ -f elsewhere/model.json ] && ok || bad "elsewhere/model.json"
M --dir nowhere run --dir elsewhere; eq "$CODE" 0 "the sub-command --dir wins"
M --assumptions elsewhere/assumptions.json run --write; eq "$CODE" 0 "assumptions elsewhere, model in ./seed"; [ -f seed/model.json ] && ok || bad "seed/model.json from --assumptions"
cmp -s seed/model.json "$MF/leashly.model.json" && ok || bad "same model from another folder"
M run --bogus; eq "$CODE" 2 "unknown flag"

done_ "seed-model/model"

#!/usr/bin/env bash
# trader-screener/screen: the assertions of trader-skill/tests/test_screener.py, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
D="$FIX/trader-screener"; M="$D/metrics.json"; P="$D/prices.json"; R="$D/rules.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
pass_all() { printf '{"rules":[],"rank":[]}' > "$TMP/r0.json"; run trader-screener/screen "$1" --rules "$TMP/r0.json" --json; }

# Prices: pe and earnings yield (pe = price / eps, rounded to 1 place; yield 100*eps/price to 2)
printf '{"rules":[]}' > "$TMP/none.json"
run trader-screener/screen "$M" --rules "$TMP/none.json" --prices "$P" --json --out "$TMP/a.json"
eq "$CODE" 0 "prices run"
cat > "$TMP/pe.json" <<'J'
{"rules":[{"metric":"pe","op":"==","value":39.2}]}
J
run trader-screener/screen "$M" --rules "$TMP/pe.json" --prices "$P" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["AAPL"]' "AAPL pe 39.2"
echo '{"rules":[{"metric":"pe","op":"==","value":17.8}]}' > "$TMP/pe2.json"
run trader-screener/screen "$M" --rules "$TMP/pe2.json" --prices "$P" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["GOOGL"]' "GOOGL pe 17.8"
echo '{"rules":[{"metric":"earnings_yield","op":"==","value":8.04}]}' > "$TMP/ey.json"
run trader-screener/screen "$M" --rules "$TMP/ey.json" --prices "$P" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["BAC"]' "BAC earnings_yield 8.04"
echo '{"rules":[{"metric":"price","op":"==","value":90.11}]}' > "$TMP/kp.json"
run trader-screener/screen "$M" --rules "$TMP/kp.json" --prices "$P" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["KO"]' "KO price 90.11"
# no pe for losses; earnings_yield -10.0
echo '[{"ticker":"LOSS","eps_ttm":-1.0}]' > "$TMP/loss.json"; echo '{"prices":{"LOSS":10}}' > "$TMP/lp.json"
echo '{"rules":[{"metric":"pe","op":">","value":0}],"missing":"fail"}' > "$TMP/lr.json"
run trader-screener/screen "$TMP/loss.json" --rules "$TMP/lr.json" --prices "$TMP/lp.json"; has "$OUT" "FAIL LOSS   pe is missing" "no pe for a loss"
echo '{"rules":[{"metric":"earnings_yield","op":"==","value":-10.0}]}' > "$TMP/ly.json"
run trader-screener/screen "$TMP/loss.json" --rules "$TMP/ly.json" --prices "$TMP/lp.json"; eq "$CODE" 0 "loss yield -10.0"

# The example screen: GOOGL 0.75, KO 0.25; exact fail reasons
run trader-screener/screen "$M" --rules "$R" --prices "$P" --json
eq "$(echo "$OUT" | jq -c '[.passed[] | [.ticker, .score]]')" '[["GOOGL",0.75],["KO",0.25]]' "example screen scores"
eq "$(echo "$OUT" | jq -c '.failed[] | select(.ticker=="AAPL") | .why')" '["pe 39.2, needs <= 35"]' "AAPL reason"
eq "$(echo "$OUT" | jq -c '.failed[] | select(.ticker=="BAC") | .why')" '["operating_margin is missing"]' "BAC reason"
eq "$(echo "$OUT" | jq -c '.failed[] | select(.ticker=="AXP") | .why')" '["revenue_growth_yoy 8.6, needs >= 10","operating_margin is missing"]' "AXP reasons"
# missing can be skipped
jq '.missing="skip"' "$R" > "$TMP/skip.json"
run trader-screener/screen "$M" --rules "$TMP/skip.json" --prices "$P"
has "$OUT" "PASS BAC" "BAC passes with skip"; has "$OUT" "skip  operating_margin is missing" "BAC skipped line"
has "$(echo "$OUT" | sed -n '/PASS BAC/,/PASS\|FAIL/p')" "not ranked on: operating_margin" "BAC rank gap"

# every op on {"ticker":"KO","sector":"Consumer Staples","pe":28.3}
echo '[{"ticker":"KO","sector":"Consumer Staples","pe":28.3}]' > "$TMP/ko.json"
op() { printf '{"rules":[%s]}' "$1" > "$TMP/op.json"; run trader-screener/screen "$TMP/ko.json" --rules "$TMP/op.json"; eq "$CODE" "$2" "op $1"; }
op '{"metric":"pe","op":"between","value":[10,30]}' 0
op '{"metric":"pe","op":"between","value":[10,20]}' 1
op '{"metric":"pe","op":"<","value":28.3}' 1
op '{"metric":"pe","op":"!=","value":1}' 0
op '{"metric":"ticker","op":"in","value":["ko","pep"]}' 0
op '{"metric":"ticker","op":"not in","value":["KO"]}' 1
op '{"metric":"sector","op":"==","value":"consumer staples"}' 0
op '{"metric":"sector","op":">","value":3}' 1
op '{"metric":"beta","op":"<","value":1}' 1
printf '{"rules":[{"metric":"sector","op":">","value":3}]}' > "$TMP/op.json"; run trader-screener/screen "$TMP/ko.json" --rules "$TMP/op.json"
has "$OUT" "sector is 'Consumer Staples', not a number" "non-number reason"
printf '{"rules":[{"metric":"pe","op":"between","value":[10,20]}]}' > "$TMP/op.json"; run trader-screener/screen "$TMP/ko.json" --rules "$TMP/op.json"
has "$OUT" "pe 28.3 is outside 10 to 20" "between reason"

# bad rules are refused (exit 2, "screen: ...")
for bad in '{"rules":[{"metric":"pe","op":"~","value":1}]}' '{"rules":[{"metric":"pe","op":"between","value":3}]}' '{"rules":[{"metric":"pe","op":"<"}]}' '{"rules":[],"rank":[{"metric":"pe","order":"up"}]}' '{"rules":[],"missing":"maybe"}'; do
  echo "$bad" > "$TMP/bad.json"; run trader-screener/screen "$M" --rules "$TMP/bad.json"; eq "$CODE" 2 "refused $bad"; has "$ERR" "screen: " "refusal message $bad"
done
echo '{"rules":[{"metric":"pe","op":"~","value":1}]}' > "$TMP/bad.json"; run trader-screener/screen "$M" --rules "$TMP/bad.json"
eq "$ERR" "screen: rule #1: unknown op '~' (use one of >, >=, <, <=, ==, !=, between, in, not in)" "unknown op wording"
echo '{"rules":[{"metric":"pe","op":"<"}]}' > "$TMP/bad.json"; run trader-screener/screen "$M" --rules "$TMP/bad.json"
eq "$ERR" "screen: rule #1 needs metric, op and value: {'metric': 'pe', 'op': '<'}" "missing value wording (repr)"
echo '{"rules": [1,]}' > "$TMP/bad.json"; run trader-screener/screen "$M" --rules "$TMP/bad.json"
eq "$ERR" "screen: $TMP/bad.json is not valid JSON: Illegal trailing comma before end of array: line 1 column 13 (char 12)" "json error wording"

# CLI: text report and shortlist
run trader-screener/screen "$M" --rules "$R" --prices "$P" --out "$TMP/trader/shortlist.json"
eq "$CODE" 0 "text report exit"; has "$OUT" "2 of 5 companies pass" "count line"; has "$OUT" "Prices as of 2026-10-13" "as of"
has "$OUT" "not a recommendation" "disclaimer"; hasnt "$OUT" "—" "no em dash"; has "$OUT" "wrote $TMP/trader/shortlist.json" "wrote line"
eq "$(jq -c '[.passed[].ticker]' "$TMP/trader/shortlist.json")" '["GOOGL","KO"]' "shortlist passed"
eq "$(jq '.failed | length' "$TMP/trader/shortlist.json")" 3 "shortlist failed"
has "$OUT" " 1. PASS GOOGL  Alphabet Inc.  score 0.75" "ranked line format"
# CSV with units, nothing passing
run trader-screener/screen "$D/companies.csv" --rules "$R"; eq "$CODE" 1 "csv nothing passes"; has "$OUT" "pe is missing" "csv pe missing"
pass_all "$D/companies.csv"; eq "$CODE" 0 "csv loads"
echo '{"rules":[{"metric":"eps_ttm","op":"==","value":8.71},{"metric":"revenue_growth_yoy","op":"==","value":16.4}]}' > "$TMP/c.json"
run trader-screener/screen "$D/companies.csv" --rules "$TMP/c.json" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["AAPL"]' "csv units parsed (\$8.71, 16.4%)"
echo '{"rules":[{"metric":"operating_margin","op":">","value":0}]}' > "$TMP/c2.json"
run trader-screener/screen "$D/companies.csv" --rules "$TMP/c2.json"; has "$OUT" "FAIL AXP    operating_margin is missing" "empty csv cell is None"
# bad input exit 2
run trader-screener/screen /nonexistent.json --rules "$R"; eq "$CODE" 2 "missing file exit"; eq "$ERR" "screen: no such file: /nonexistent.json" "missing file wording"
run trader-screener/screen "$M" --rules /nonexistent-rules.json; eq "$ERR" "screen: [Errno 2] No such file or directory: '/nonexistent-rules.json'" "OSError wording"
# --top limits the shortlist
run trader-screener/screen "$M" --rules "$R" --prices "$P" --top 1 --json; eq "$CODE" 0 "top exit"; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["GOOGL"]' "top 1"
run trader-screener/screen "$M" --rules "$R" --prices "$P" --top 1; has "$OUT" "    ... 1 more pass" "more pass line"
# default rules come from the skill folder
run --skill-dir "$D" trader-screener/screen "$M" --prices "$P" --json; eq "$(echo "$OUT" | jq -c '[.passed[].ticker]')" '["GOOGL","KO"]' "--skill-dir default rules"
run trader-screener/screen; eq "$CODE" 2 "usage exit"; has "$ERR" "screen: error: the following arguments are required: metrics" "usage error"
done_ "trader-screener/screen"

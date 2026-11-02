#!/usr/bin/env bash
# trader-analyst/facts: the assertions of trader-skill/tests/test_analyst.py, run against the C++ tool (--offline).
source "$(dirname "$0")/lib.sh"
X="$FIX/trader-analyst"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
F() { run trader-analyst/facts "$@"; }
j() { echo "$OUT" | jq -c "$1"; }

# Apple, 8 quarters
F AAPL --offline "$X" --json --quarters 8
eq "$CODE" 0 "AAPL exit"
eq "$(j '.[0].result.rows[0] | [.end, .fiscal, .revenue, .net_income, .eps_diluted, .revenue_yoy, .eps_yoy, .gross_margin, .operating_margin, .net_margin, .derived]')" \
   '["2026-06-27","Q3 2026",109417000000,29789000000,2.02,16.4,28.7,50.1,32.6,27.2,false]' "latest quarter"
eq "$(j '[.[0].result.rows[:6][].fiscal]')" '["Q3 2026","Q2 2026","Q1 2026","Q4 2025","Q3 2025","Q2 2025"]' "labels from the first filing"
eq "$(j '[.[0].result.rows[] | select(.end=="2025-09-27") | .derived, .revenue, .revenue_yoy]')" '[true,102466000000,7.9]' "Q4 derived from the 10-K"
eq "$(j '.[0].result.rows[] | select(.end=="2024-09-28") | .fiscal')" '"Q4 2024"' "derived Q4 keeps the first filing's year"
eq "$(j '.[0].result.ttm')" '{"revenue":466823000000,"net_income":128930000000,"eps_diluted":8.71}' "ttm"
eq "$(j '.[0].result.rows[0] | keys_unsorted')" '["end","start","fiscal","derived","revenue","gross_profit","operating_income","net_income","eps_diluted","gross_margin","operating_margin","net_margin","revenue_yoy","net_income_yoy","eps_yoy","filed","form"]' "row keys"
eq "$(j '.[0] | keys_unsorted')" '["ticker","result","filings","changes","note"]' "result keys"
has "$(j '.[0].changes[0]')" 'revenue $109.42B (+16.4% vs a year earlier)' "changes revenue"
has "$(j '.[0].changes[0]')" 'diluted EPS $2.02 (+28.7%)' "changes eps"
eq "$(j '[.[0].changes[] | select(startswith("Gross margin 50.1% (+0.8 points"))] | length')" 1 "gross margin change"
# filings: newest 10-Q and the earnings 8-K exhibit
eq "$(j '.[0].filings.report | [.form, .period]')" '["10-Q","2026-06-27"]' "latest 10-Q"
has "$(j '.[0].filings.report.url')" '/000032019326000020/aapl-20260627.htm"' "10-Q url"
eq "$(j '.[0].filings.earnings_8k | [.filed, .exhibit]')" '["2026-07-30","https://www.sec.gov/Archives/edgar/data/320193/000032019326000018/a8-kex991q3202606272026.htm"]' "8-K exhibit"
eq "$(j '.[0].note')" 'null' "AAPL facts are current"
F AAPL --offline "$X" --json --quarters 4
eq "$(j '.[0].result.rows | length')" 4 "--quarters 4"
eq "$(j '.[0].result.concepts.revenue')" '["RevenueFromContractWithCustomerExcludingAssessedTax"]' "concepts used"
F AAPL --offline "$X" --json --quarters -6
eq "$(j '.[0].result.rows | length')" "$(F AAPL --offline "$X" --json --quarters 100; j '.[0].result.rows | length - 6')" "negative --quarters slices like Python"

# text report
F AAPL --offline "$X"
eq "$CODE" 0 "text exit"
has "$OUT" "Apple Inc. (AAPL), CIK 0000320193. US\$ millions except EPS." "header"
has "$OUT" "2026-06-27  Q3 2026      109,417   +16.4%   50.1%   32.6%   27.2%     29,789     2.02  +28.7%" "table row"
has "$OUT" "2025-09-27  Q4 2025      102,466    +7.9%" "derived row"
has "$OUT" "Last 4 quarters: revenue \$466,823M, net income \$128,930M, diluted EPS \$8.71." "ttm line"
has "$OUT" "Latest earnings 8-K (item 2.02): filed 2026-07-30  https://www.sec.gov/Archives/edgar/data/320193/000032019326000018/a8-kex991q3202606272026.htm" "8-K line"
has "$OUT" "Derived quarters are the 10-K year minus the three 10-Qs" "derived note"
hasnt "$OUT" $'\u2014' "no em dash"

# lagging data
F KO --offline "$X"
eq "$CODE" 0 "KO exit"
has "$OUT" "COCA COLA CO (KO)" "KO name"
has "$OUT" "NOTE: SEC's XBRL facts stop at 2026-04-03, but a 10-Q for the period ending 2026-07-03 was filed 2026-07-29" "stale note"

# metrics file for the screener (merges, sorted)
F AAPL KO --offline "$X" --metrics "$TMP/trader/metrics.json"
eq "$CODE" 0 "metrics exit"
has "$OUT" "wrote $TMP/trader/metrics.json (2 companies)" "wrote"
hasnt "$OUT" $'\u2014' "no em dash in output"
M() { jq -c "$1" "$TMP/trader/metrics.json"; }
eq "$(M '[.companies[].ticker]')" '["AAPL","KO"]' "metrics tickers"
eq "$(M '.companies[0] | [.eps_ttm, .revenue_growth_yoy, .source]')" '[8.71,16.4,"SEC XBRL company facts"]' "AAPL metrics"
eq "$(M '.companies[1].period_end')" '"2026-04-03"' "KO period end"
eq "$(M '.as_of')" "\"$(date +%F)\"" "as_of today"
echo '{"companies": [{"ticker": "ZZ", "x": 1}, {"ticker": "AAPL", "old": true}]}' > "$TMP/m.json"
F KO AAPL --offline "$X" --metrics "$TMP/m.json" --json
eq "$(jq -c '[.companies[] | [.ticker, .old]]' "$TMP/m.json")" '[["AAPL",null],["KO",null],["ZZ",null]]' "merge replaces, keeps others"
hasnt "$OUT" "wrote" "no wrote line with --json"

# unknown ticker, user agent, usage
F ZZZZ --offline "$X"
eq "$CODE" 2 "unknown ticker exit"
has "$ERR" "no SEC CIK for ZZZZ" "unknown ticker"
F AAPL ZZZZ --offline "$X" --no-filings
eq "$CODE" 1 "partial failure is 1"
OUT=$(env -u SEC_USER_AGENT $T trader-analyst/facts AAPL --config /nonexistent.json 2>&1); CODE=$?
eq "$CODE" 2 "no UA exit"; has "$OUT" "User-Agent" "UA help"
echo '{"sec_user_agent": "Your Name you@example.com"}' > "$TMP/cfg.json"
OUT=$(env -u SEC_USER_AGENT $T trader-analyst/facts AAPL --config "$TMP/cfg.json" 2>&1); CODE=$?
eq "$CODE" 2 "placeholder UA refused"
OUT=$(env SEC_USER_AGENT="no email here" $T trader-analyst/facts AAPL --config /nonexistent.json 2>&1); CODE=$?
eq "$CODE" 2 "UA without @ refused"
F
eq "$CODE" 2 "no tickers exit"; has "$OUT" "usage: facts" "help printed"

# generated SEC files: restatements, one-offs, exhibit names, titles
O="$TMP/o"; mkdir -p "$O"
cp "$X/company_tickers.json" "$O/"
fact() { printf '{"start":"%s","end":"%s","val":%s,"fy":%s,"fp":"%s","form":"%s","filed":"%s","accn":"%s"}' "$@"; }
cf() { printf '{"cik": %s, "entityName": "TEST CO", "facts": {"us-gaap": {%s}}}' "$1" "$2" > "$O/companyfacts-CIK$(printf %010d "$1").json"; }
rev="\"Revenues\": {\"units\": {\"USD\": [$(fact 2025-01-01 2025-03-31 100 2025 Q1 10-Q 2025-05-01 a),$(fact 2025-01-01 2025-03-31 90 2026 Q1 10-Q 2026-05-01 b),$(fact 2025-01-01 2025-03-31 1 2025 Q1 8-K 2025-04-20 c)]}}"
cf 1 "$rev"
F --cik 1 --offline "$O" --no-filings --json
eq "$(j '.[0].result.rows[0] | [.revenue, .fiscal]')" '[90,"Q1 2025"]' "restated value wins, label stays"
eq "$(j '.[0].result | [.entity, .cik, .concepts.revenue]')" '["TEST CO","0000000001",["Revenues"]]' "entity name and zero-filled CIK"
q() { fact "$1" "$2" "$3" 2026 Q2 10-Q 2026-08-01 x; }
cf 2 "\"Revenues\": {\"units\": {\"USD\": [$(q 2026-04-01 2026-06-30 100)]}}, \"NetIncomeLoss\": {\"units\": {\"USD\": [$(q 2026-04-01 2026-06-30 90)]}}, \"OperatingIncomeLoss\": {\"units\": {\"USD\": [$(q 2026-04-01 2026-06-30 30)]}}"
F --cik 2 --offline "$O" --no-filings --json
eq "$(j '[.[0].changes[] | select(contains("above operating income"))] | length')" 1 "one-offs flagged"
eq "$(j '.[0].result.rows[0] | [.net_margin, .operating_margin, .gross_margin]')" '[90.0,30.0,null]' "margins"
cf 3 "\"Revenues\": {\"units\": {\"USD\": [$(q 2026-04-01 2026-06-30 100.5),$(fact 2025-04-01 2025-06-30 50 2025 Q2 10-Q 2025-08-01 y)]}}, \"CostOfRevenue\": {\"units\": {\"USD\": [$(q 2026-04-01 2026-06-30 40)]}}"
F --cik 3 --offline "$O" --no-filings --json
eq "$(j '.[0].result.rows[0] | [.gross_profit, .gross_margin, .revenue_yoy]')" '[60.5,60.2,101.0]' "gross profit from cost of revenue, float yoy"
F --cik 999 --offline "$O" --no-filings
eq "$CODE" 2 "missing facts file exit"; has "$ERR" "facts: 999: offline: no file" "missing facts message"
printf '{"cik": 4, "facts": {"us-gaap": {}}}' > "$O/companyfacts-CIK0000000004.json"
F --cik 4 --offline "$O" --no-filings
has "$ERR" "no quarterly revenue or net income in the XBRL facts" "no facts message"
cp "$X/companyfacts-CIK0000021344.json" "$O/companyfacts-CIK0000070858.json"
F BAC --offline "$O" --no-filings
has "$OUT" "BANK OF AMERICA CORP (BAC), CIK 0000021344." "/DE/ stripped from the title"
# exhibit choice (edgar names); the index folder is the accession without dashes
ex() {   # ex PRIMARY NAMES... -> the 8-K link the report prints
    local primary=$1; shift
    printf '{"filings": {"recent": {"form": ["8-K"], "accessionNumber": ["0000000005-26-000001"], "filingDate": ["2026-07-30"], "reportDate": [""], "primaryDocument": ["%s"], "items": ["2.02,9.01"]}}}' "$primary" > "$O/submissions-CIK0000000005.json"
    mkdir -p "$O/000000000526000001"; printf '{"directory": {"item": [%s]}}' "$(printf '{"name":"%s"},' "$@" | sed 's/,$//')" > "$O/000000000526000001/index.json"
    F --cik 5 --offline "$O" --json; j '.[0].filings.earnings_8k.exhibit // .[0].filings.earnings_8k.index' | sed 's|.*/||; s/"$//'
}
cp "$O/companyfacts-CIK0000000001.json" "$O/companyfacts-CIK0000000005.json"
eq "$(ex axp-20260724.htm axp-20260724.htm q226exhibit991.htm q226exhibit992.htm R1.htm)" q226exhibit991.htm "axp exhibit"
eq "$(ex goog-20260722.htm goog-20260722.htm googexhibit991q22026.htm)" googexhibit991q22026.htm "goog exhibit"
eq "$(ex ko-20260728.htm a2026q2earningsreleaseex-9.htm ko-20260728.htm)" a2026q2earningsreleaseex-9.htm "ko release"
eq "$(ex main.htm bac-06302026ex993.htm bac-06302026ex991.htm)" bac-06302026ex991.htm "bac 99.1 over 99.3"
eq "$(ex only-8k.htm only-8k.htm)" 0000000005-26-000001-index.htm "no exhibit falls back to the index"
eq "$(ex x.htm EX-99.1.HTM ex-99.10.htm)" EX-99.1.HTM "case-insensitive, 99.10 is not 99.1"
# day-trader-skill ships the same facts.py: daytrade-analyst/facts is the same tool
A1=$($T trader-analyst/facts --help 2>&1); A2=$($T daytrade-analyst/facts --help 2>&1); eq "$A2" "$A1" "daytrade-analyst alias"
done_ "trader-analyst"

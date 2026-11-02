#!/usr/bin/env bash
# hunter-salary/offer: the assertions of hunter-skill/tests/test_salary.py, run against the C++ tool, plus cases worked
# out by hand from the stated rules. Fixtures: the skill's offer/competing/bench examples and the real BLS response in
# tests/fixtures/hunter-salary, unmodified. Every other offer and benchmark below is synthetic, made up for the tests.
source "$(dirname "$0")/lib.sh"
F="$FIX/hunter-salary"; G="$F/offer.example.json"; AMZ="$F/competing.example.json"; BEN="$F/bench.example.json"; BLS="$F/bls-oews-15-1252-2025.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
O() { run hunter-salary/offer "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
mk() { echo "$2" > "$1"; }

# ---- total compensation
O total "$G" --json
eq "$(j '[.years[].tc]')" '[282750.0,262750.0,262750.0,262750.0]' "even vesting"
eq "$(j .total)" 1071000.0 "total"; eq "$(j .one_time)" 10000.0 "relocation stays out of TC"
eq "$(j 'keys_unsorted')" '["company","title","currency","years","total","average","grant","schedule","cliff_months","one_time"]' "key order"
eq "$(j '.years[0]|keys_unsorted')" '["year","base","bonus","equity","sign_on","tc"]' "year keys"
has "$OUT" '"tc": 282750.0' "floats print like Python"
has "$OUT" '"schedule": [
    25,
    25,' "a named schedule keeps its ints"
O total "$AMZ" --json
eq "$(j '[.years[].equity]')" '[10000.0,30000.0,80000.0,80000.0]' "back-loaded equity"
eq "$(j '[.years[].tc]')" '[230000.0,240000.0,255000.0,255000.0]' "two-year sign-on"
eq "$(j .total)" 980000.0 "Amazon total"; has "$OUT" '"one_time": 0' "no other: sum() of nothing is the int 0"
O total "$G"; eq "$CODE" 0 "total exits 0"
eq "$(echo "$OUT" | head -1)" 'Google, Software Engineer III: $1,071,000 over 4 years (average $267,750 a year)' "headline"
eq "$(echo "$OUT" | sed -n 3p)" '     1    $185,000     $27,750     $50,000     $20,000    $282,750' "year 1 row"
eq "$(echo "$OUT" | sed -n 7p)" '  equity: $200,000 granted, vesting 25/25/25/25, 12-month cliff (leave before it and the equity is gone)' "equity line"
eq "$(echo "$OUT" | sed -n 8p)" '  one-time (not in TC): $10,000' "one-time line"
eq "$(echo "$OUT" | tail -1)" '  bonus is a target, equity is valued at grant; neither is guaranteed' "the caveat"

# schedules
sch() { mk o.json "{\"base\": 100000, \"equity\": {\"grant_value\": 100000, \"vesting\": $1}}"; O total o.json; }
sch '"front-loaded"'; has "$OUT" "vesting 33/33/22/12" "front-loaded"
sch '[10, 20, 30, 40]'; has "$OUT" "vesting 10/20/30/40" "a list of percents"
O total o.json --json; eq "$(j .schedule)" '[10.0,20.0,30.0,40.0]' "as floats"; has "$OUT" '    10.0,' "float() of each"
sch '"even-3"'; has "$OUT" "vesting 33.3/33.3/33.3" "even-3"; has "$OUT" '$33,333' "a third of the grant"
O total o.json --json; has "$OUT" '33.333333333333336' "100/3 as Python prints it"
sch '[25, 25, 25]'; eq "$CODE" 2 "not 100"; eq "$ERR" "offer: vesting percents add up to 75.0, not 100" "adds up to 75"
sch '"mystery"'; eq "$ERR" "offer: unknown vesting 'mystery' (use back-loaded, back-loaded-5-15-40-40, even-3, even-4, even-5, front-loaded, front-loaded-33-33-22-12, or a list of yearly percents)" "unknown name"
sch '[-5, 105]'; eq "$ERR" "offer: vesting must be a name or a list of yearly percents" "negative percent"
sch '[]'; eq "$ERR" "offer: vesting must be a name or a list of yearly percents" "empty list"
sch '[33.3, 33.3, 33.4]'; eq "$CODE" 0 "33.3 + 33.3 + 33.4 is 100"
sch '[50, 49.6]'; eq "$CODE" 0 "within half a point"
sch '[50, 49.4]'; eq "$ERR" "offer: vesting percents add up to 99.4, not 100" "outside half a point"
sch 'null'; eq "$ERR" "offer: vesting must be a name or a list of yearly percents" "null vesting"
mk o.json '{"base": 100000, "equity": {"shares": 100, "share_price": 250.5}}'; O total o.json --json; eq "$(j .grant)" 25050.0 "shares x price"
mk o.json '{"base": 100000, "equity": {"shares": 100}}'; O total o.json; eq "$ERR" "offer: equity has shares but no share_price: ask the recruiter for the value or the price used" "shares without a price"
mk o.json '{"base": 100000, "raise_pct": 10, "equity": {"grant_value": 0, "vesting": "even-4"}}'; O total o.json --json
has "$OUT" '"base": 110000.00000000001' "a 10% raise in year 2 (as Python's float prints it)"
mk o.json '{"company": "X"}'; O total o.json; eq "$CODE" 2 "no base"; eq "$ERR" "offer: the offer needs a base salary" "needs a base"
mk o.json '{"base": "120000", "bonus": 5000, "sign_on": [1000, "2000"], "currency": "EUR", "other": {"relocation": 3000, "note": "x", "flag": true}}'
O total o.json; eq "$(echo "$OUT" | head -1)" '?, : 503,000 EUR over 4 years (average 125,750 EUR a year)' "strings that float() reads, other currencies"
has "$OUT" "  one-time (not in TC): 3,001 EUR" "numbers and bools in other"
O total o.json --json
eq "$(j '[.years[].sign_on]')" '[1000.0,2000.0,0.0,0.0]' "a sign-on list, then 0"
mk o.json '{"base": 100000, "raise_pct": 100000}'; O total o.json; eq "$CODE" 0 "a big raise is still a number"
mk o.json '{"base": 100000, "raise_pct": 1e300, "equity": {"grant_value": 1, "vesting": "even-5"}}'; O total o.json; eq "$CODE" 1 "pow overflows, like Python"; has "$ERR" "OverflowError" "OverflowError"
mk o.json '{"base": 100000, "equity": {"grant_value": 1, "cliff_months": 6.9}}'; O total o.json; has "$OUT" ", 6-month cliff" "%d truncates"
mk o.json '{"base": -5}'; O total o.json; has "$OUT" '-$5' "a negative amount"
mk o.json 'nope'; O total o.json; eq "$ERR" "offer: o.json is not valid JSON: Expecting value: line 1 column 1 (char 0)" "not JSON"
O total missing.json; eq "$CODE" 2 "no file"; eq "$ERR" "offer: no file at missing.json" "no file"
mkdir adir; O total adir; eq "$ERR" "offer: [Errno 21] Is a directory: 'adir'" "a folder is an OSError"

# ---- compare
O compare "$G" "$AMZ"
has "$OUT" "Highest in year 1: Google. Highest over the full schedule: Google." "who is highest"
has "$OUT" "Amazon changes a lot by year" "calls out the back-loaded offer"; has "$OUT" "5/15/40/40" "its schedule"
has "$OUT" "  Amazon changes a lot by year: \$230,000 in year 1, \$255,000 in year 4 (vesting 5/15/40/40, sign-on \$45,000/\$35,000)." "the whole line"
hasnt "$OUT" "Google changes" "Google is even"
eq "$(echo "$OUT" | sed -n 2p)" "  year          Google      Amazon" "header"
eq "$(echo "$OUT" | sed -n 7p)" "  total     \$1,071,000    \$980,000" "totals"
mk short.json '{"company": "A Very Long Company Name Inc", "base": 100000, "equity": {"grant_value": 0, "vesting": "even-3"}}'
O compare "$G" short.json
eq "$(echo "$OUT" | sed -n 2p)" "  year                            Google  A Very Long Company Name Inc" "columns grow with the longest name"
eq "$(echo "$OUT" | sed -n 6p)" "  4                             \$262,750                           n/a" "a shorter schedule shows n/a"
O compare "$G" "$AMZ" --json; eq "$(j 'map(.company)')" '["Google","Amazon"]' "compare --json"
mk n.json '{"company": 7, "base": 1}'; O compare n.json "$G"; eq "$CODE" 1 "a company that is not text"
O compare; eq "$CODE" 2 "compare needs offers"

# ---- BLS
B() { O bls --soc 15-1252 --offline "$BLS" "$@"; }
B --area msa:41940; eq "$CODE" 0 "bls exits 0"
eq "$(j '[.p10,.p25,.p50,.p75,.p90,.mean]')" '[138420.0,173650.0,213110.0,226080.0,289150.0,221710.0]' "San Jose percentiles"
eq "$(j .label)" '"BLS OEWS May 2025, Software Developers (SOC 15-1252), San Jose-Sunnyvale-Santa Clara, CA"' "label"
eq "$(j .url)" '"https://api.bls.gov/publicAPI/v2/timeseries/data/OEUM004194000000015125213"' "url ends with the median series"
eq "$(j .series.p50)" '"OEUM004194000000015125213"' "msa series"
eq "$(j 'keys_unsorted')" '["label","kind","source","url","as_of","series","occupation","area","cite","note","p10","p25","p50","p75","p90","mean"]' "key order"
B --area national; eq "$(j .p50)" 135980.0 "national median"; eq "$(j .series.mean)" '"OEUN000000000000015125204"' "national mean series"
B --area msa:16980 --area-name "Chicago metro area"; eq "$(j .p50)" 134380.0 "Chicago"; has "$(j .label)" "Chicago metro area" "area name"
B --area msa:16980; has "$(j .label)" "MSA 16980" "an unnamed metro (a[3:] of M0016980)"
B --area msa:41940 --year 2024; eq "$CODE" 2 "no number is made up"
eq "$ERR" "offer: BLS has no 2024 OEWS wages for 15-1252 in msa:41940 (check the codes, or try the year before)" "not in the file"
B --area msa:41940 --occupation "Coders"; has "$(j .label)" "Coders (SOC 15-1252)" "occupation name"
for bad in "--soc 15125 --area national" "--soc 15-1252 --area msa:419" "--soc 15-1252 --area state:ZZ" "--soc 15-1252 --area moon" "--soc 15-1252 --area foo:bar"; do
  # shellcheck disable=SC2086
  O bls $bad --offline "$BLS"; eq "$CODE" 2 "refused: $bad"; done
O bls --soc 15125 --area national --offline "$BLS"; eq "$ERR" "offer: --soc must look like 15-1252" "soc"
O bls --soc 15-1252 --area msa:419 --offline "$BLS"; eq "$ERR" "offer: msa: needs the 5-digit metro code (CBSA), e.g. msa:41940" "msa"
O bls --soc 15-1252 --area state:ZZ --offline "$BLS"; eq "$ERR" "offer: state: needs a 2-letter state or a 2-digit FIPS code" "state"
O bls --soc 15-1252 --area moon --offline "$BLS"; eq "$ERR" "offer: --area must be national, state:XX or msa:NNNNN" "area"
O bls --soc 15-1252 --area $'msa:4194\xc2\xb2' --offline "$BLS"; eq "$ERR" "offer: BLS has no 2025 OEWS wages for 15-1252 in msa:4194"$'\xc2\xb2'" (check the codes, or try the year before)" "str.isdigit() takes a superscript two"
mk resp.json '{"status": "REQUEST_NOT_PROCESSED", "message": ["daily limit", "try tomorrow"]}'
O bls --soc 15-1252 --area national --offline resp.json; eq "$ERR" "offer: BLS answered REQUEST_NOT_PROCESSED: daily limit; try tomorrow" "BLS refusal"
# a state response written here: CA, with a value BLS did not release and one with a comma
cat > ca.json <<'JSON'
{"status": "REQUEST_SUCCEEDED", "Results": {"series": [
 {"seriesID": "OEUS060000000000015125213", "data": [{"year": "2025", "value": "1,234"}]},
 {"seriesID": "OEUS060000000000015125215", "data": [{"year": "2025", "value": "-"}]},
 {"seriesID": "OEUS060000000000015125204", "data": [{"year": 2025, "value": 99}]},
 {"seriesID": "OEUS060000000000015125212", "data": [{"year": "2024", "value": "5"}]}]}}
JSON
O bls --soc 15-1252 --area state:ca --offline ca.json
eq "$(j '[.p50, .mean, .missing, .series.p75]')" '[1234.0,99.0,["p10","p25","p75","p90"],"OEUS060000000000015125214"]' "commas, missing values, the state series"
eq "$(j .label)" '"BLS OEWS May 2025, Software Developers (SOC 15-1252), the state of CA"' "the state of CA"
O bls --soc 15-1252 --area state:06 --offline ca.json; eq "$(j .area)" '"the state of FIPS 06"' "a FIPS code"
O bls --soc 15-1252 --area state:CA --area-name California --offline ca.json; eq "$(j .area)" '"California"' "--area-name wins"
OUTB="$TMP/hunter/offers/bench.json"
B --area msa:41940 --out "$OUTB"; eq "$CODE" 0 "--out exits 0"
eq "$(echo "$OUT" | head -1)" "Added to $OUTB: BLS OEWS May 2025, Software Developers (SOC 15-1252), San Jose-Sunnyvale-Santa Clara, CA" "added line"
eq "$(jq '.benchmarks[0].p50' "$OUTB")" 213110.0 "written"
B --area msa:41940 --out "$OUTB"; eq "$(jq '.benchmarks|length' "$OUTB")" 1 "the same label is replaced, not duplicated"
B --area national --out "$OUTB"; eq "$(jq '.benchmarks|length' "$OUTB")" 2 "a second benchmark"
for k in p10 p25 p50 p75 p90 mean url label; do
  eq "$(jq -c ".benchmarks[0].$k" "$OUTB")" "$(jq -c ".benchmarks[0].$k" "$BEN")" "the shipped bench matches the BLS file: $k"
  eq "$(jq -c ".benchmarks[1].$k" "$OUTB")" "$(jq -c ".benchmarks[1].$k" "$BEN")" "national: $k"
done
O bls --soc 15-1252 --area national; eq "$CODE" 2 "network off is an OSError"; has "$ERR" "offer: <urlopen error network is off" "urlopen error"

# ---- benchmarks
for bad in '{"label": "a friend said", "kind": "base", "p50": 250000}' '{"label": "posting", "kind": "posting_range", "source": "the posting", "min": 1, "max": 2}' '{"label": "x", "kind": "vibes", "source": "me", "url": "https://example.com"}'; do
  mk b.json "{\"benchmarks\": [$bad]}"; O bench "$G" --bench b.json; eq "$CODE" 2 "refused: $bad"; done
mk b.json '{"benchmarks": [{"label": "a friend said", "kind": "base", "p50": 250000}]}'; O bench "$G" --bench b.json
eq "$ERR" "offer: benchmark 'a friend said' has no source: every number needs one (never invent a market number)" "no source, no number"
mk b.json '{"benchmarks": [{"label": "posting", "kind": "posting_range", "source": "the posting", "min": 1, "max": 2}]}'; O bench "$G" --bench b.json
eq "$ERR" "offer: benchmark 'posting' needs the URL it came from" "needs a URL"
mk b.json '{"benchmarks": [{"label": "x", "kind": "vibes", "source": "me", "url": "https://example.com"}]}'; O bench "$G" --bench b.json
eq "$ERR" "offer: benchmark kind must be one of base, total, posting_range, got 'vibes'" "bad kind"
mk b.json '[{"label": "x"}]'; O bench "$G" --bench b.json; eq "$CODE" 1 "a bare list is not read (data.get on a list)"
O bench "$G" --bench "$BEN"
has "$OUT" "your base is between the 25th and the median" "San Jose position"
has "$OUT" "your base is between the 75th and the 90th percentile" "national position"
has "$OUT" "    p10 \$138,420, p25 \$173,650, p50 \$213,110, p75 \$226,080, p90 \$289,150, mean \$221,710" "numbers line"
pos() { mk p.json "{\"company\": \"P\", \"base\": $1}"; O bench p.json --bench "$2"; }
pos 300000 "$BEN"; has "$OUT" "above the 90th percentile" "above p90"
pos 100000 "$BEN"; has "$OUT" "your base is below the 10th percentile" "below p10"
mk rng.json '{"benchmarks": [{"label": "test range", "kind": "posting_range", "source": "test", "url": "https://example.com/job", "min": 150000, "max": 250000}]}'
pos 200000 rng.json; has "$OUT" "your base is 50% of the way up the posted range" "half way up"
eq "$(echo "$OUT" | sed -n 3p)" "    min \$150,000, max \$250,000" "range numbers"
pos 162500 rng.json; has "$OUT" "12% of the way" "12.5 rounds half to even"
pos 187500 rng.json; has "$OUT" "38% of the way" "37.5 rounds half to even"
pos 140000 rng.json; has "$OUT" "below the posted range" "below"
pos 260000 rng.json; has "$OUT" "above the posted range" "above"
mk rng2.json '{"benchmarks": [{"label": "r", "kind": "posting_range", "source": "s", "url": "u", "max": 2}]}'; pos 1 rng2.json; has "$OUT" "range incomplete" "no min"
mk pp.json '{"benchmarks": [{"label": "q", "kind": "total", "source": "s", "url": "u", "p25": 10, "p75": 30}]}'
pos 5 pp.json; has "$OUT" "below the lowest percentile given" "no p10"
pos 20 pp.json; has "$OUT" "between the 25th and the 75th percentile" "skips missing marks"
pos 40 pp.json; eq "$CODE" 1 "past the last mark without a p90: IndexError, like Python"
mk np.json '{"benchmarks": [{"label": "q", "kind": "base", "source": "s", "url": "u", "mean": 10}]}'; pos 5 np.json; has "$OUT" "no percentiles" "only a mean"
O bench "$G"; has "$OUT" "  none given. Run: offer.py bls" "no benchmarks"

# ---- the counter
C() { O counter "$G" --profile none.json "$@"; }
C --bench "$BEN" --competing "$AMZ" --floor 180000 --json
eq "$(j .ask)" 213000.0 "15% cap on 185,000 is 212,750, rounded up"
eq "$(j .middle)" 199000.0 "middle"; has "$(j .capped_by)" "15%" "capped by the 15% rule"
eq "$(j '[.evidence[].value] | sort')" '[213110.0,226080.0]' "evidence"
eq "$(j .below_floor)" false "above the floor"; eq "$(j .sign_on_ask)" null "the Amazon offer is lower in year 1"
eq "$(j .gaps)" '{"year1":-52750.0,"total":-91000.0}' "gaps"
eq "$(j 'keys_unsorted')" '["base","ask","middle","floor","below_floor","capped_by","evidence","support","sign_on_ask","gaps","script"]' "--json keys"
eq "$(j '.evidence[0]|keys_unsorted')" '["value","why","kind","stat"]' "no bench reference in the JSON"
eq "$(j '[.support[].value]')" '[226080.0,213110.0]' "nothing at or below the ask: all the evidence, highest first"
SCRIPT=$(echo "$OUT" | jq -r .script)
has "$SCRIPT" "BLS wage data (OEWS, May 2025)" "the script cites BLS (written before the bench references go, unlike the Python, which raises KeyError here)"
eq "$(echo "$SCRIPT" | grep -oE '[0-9]+(,[0-9]{3})*' | sort -u | tr '\n' ' ')" "2025 213,000 213,110 226,080 75 " "the script only uses cited numbers"
hasnt "$SCRIPT" "Amazon" "not shared unless the user says so"; hasnt "$SCRIPT" "other offer" "no other offer"
eq "$(echo "$SCRIPT" | tail -1)" "[your name]" "--me missing"
C --bench "$BEN" --competing "$AMZ" --floor 180000 --me "Jordan Reyes"; eq "$CODE" 0 "counter exits 0"
has "$OUT" 'Ask:    $213,000' "the ask"; has "$OUT" "No outcome is guaranteed" "no guarantee"
eq "$(echo "$OUT" | tail -3 | head -1)" "Jordan" "signed with the first name"
eq "$(echo "$OUT" | head -1)" 'Counter for Google, Software Engineer III (base $185,000)' "header"
has "$OUT" '      $226,080  BLS OEWS May 2025, Software Developers (SOC 15-1252), San Jose-Sunnyvale-Santa Clara, CA 75th percentile' "evidence line"
has "$OUT" "  Ask:    \$213,000  (capped by the 15% cap on a base ask (change with --max-bump))" "ask line"
has "$OUT" "  Floor:  \$180,000" "floor line"
has "$OUT" "  Against Amazon: year 1 -\$52,750, full schedule -\$91,000 (positive = the other offer is higher)." "against line"
has "$OUT" "BLS wage data (OEWS, May 2025) for software developers in the San Jose-Sunnyvale-Santa Clara, CA area puts the median at \$213,110 and the 75th percentile at \$226,080, so I would like to ask for a base salary of \$213,000." "the cited ask"
mk rngc.json '{"benchmarks": [{"label": "test range", "kind": "posting_range", "source": "test", "url": "https://example.com/job", "min": 160000, "max": 200000}]}'
C --bench "$BEN" --bench rngc.json --json
eq "$(j '[.ask, .capped_by, .middle]')" '[200000.0,"the top of the posted range",193000.0]' "a posted range caps the ask"
has "$(echo "$OUT" | jq -r .script)" "sign-on" "ask for the rest as sign-on"
has "$(echo "$OUT" | jq -r .script)" "The posted range for this role goes up to \$200,000, so I would like to ask for a base salary of \$200,000." "the posted range, capitalised"
mk other.json '{"company": "Other Co", "base": 230000}'
C --competing other.json --json; eq "$(j '[.ask, .capped_by]')" '[230000.0,null]' "a real competing base is not capped"
eq "$(j .gaps)" '{"year1":-52750.0,"total":-151000.0}' "gaps against a bare offer"
C --competing other.json --share-competing --json; has "$(echo "$OUT" | jq -r .script)" "I also have an offer with a base of \$230,000, so I would like to ask for a base salary of \$230,000." "shared"
C --competing other.json --json; hasnt "$(echo "$OUT" | jq -r .script)" "I also have an offer" "not shared"
mk rich.json '{"base": 300000, "company": "Google"}'
O counter rich.json --bench "$BEN" --profile none.json --json; eq "$(j .ask)" null "no evidence, no number"
has "$(echo "$OUT" | jq -r .script)" "flexibility" "asks about flexibility"
eq "$(echo "$OUT" | jq -r .script | grep -c '\$[0-9]')" 0 "no dollar figure"
O counter rich.json --bench "$BEN" --profile none.json; has "$OUT" "  No cited number is above your base." "no evidence line"
hasnt "$OUT" "Ask:" "no ask line"
C --bench "$BEN" --floor 190000; has "$OUT" "below your floor" "below the floor"; has "$OUT" "  WARNING: the base is below your floor of \$190,000." "warning"
C --bench "$BEN" --max-bump 0.1 --json; eq "$(j '[.ask, .capped_by]')" '[204000.0,"the 10% cap on a base ask (change with --max-bump)"]' "--max-bump"
C --bench "$BEN" --max-bump 0.125 --json; has "$(j .capped_by)" "the 12% cap" "round(12.5) is 12"
mk big.json '{"company": "Big", "base": 190000, "sign_on": 120000}'
C --bench "$BEN" --competing big.json --share-competing --json
eq "$(j '[.ask, .sign_on_ask, [.support[].kind]]')" '[213000.0,28000.0,["competing"]]' "sign-on to close a first-year gap"
has "$(echo "$OUT" | jq -r .script)" "I also have an offer with a base of \$190,000, so I would like to ask for a base salary of \$213,000." "supported by the competing base"
has "$(echo "$OUT" | jq -r .script)" "My other offer is \$27,250 higher in the first year, so a sign-on bonus of \$28,000 would close that gap." "the sign-on line"
C --bench "$BEN" --competing big.json --json; hasnt "$(echo "$OUT" | jq -r .script)" "other offer" "only when shared"
C --bench "$BEN" --recruiter Sam --me "Pat"; has "$OUT" "Hi Sam," "recruiter"
C --bench "$BEN" --me "   "; eq "$CODE" 1 "a blank name: IndexError, like Python"
mk prof.json '{"salary_floor": {"base": 190000}}'; O counter "$G" --bench "$BEN" --profile prof.json; has "$OUT" "below your floor of \$190,000" "the profile floor"
mk prof.json '{"salary_floor": {"amount": "170000"}}'; O counter "$G" --bench "$BEN" --profile prof.json; has "$OUT" "  Floor:  \$170,000" "amount, as text"
mk prof.json '{"salary_floor": "soon"}'; O counter "$G" --bench "$BEN" --profile prof.json; has "$OUT" "yours to set (--floor); the tool never picks it" "a floor that is not a number"
mk prof.json 'broken'; O counter "$G" --bench "$BEN" --profile prof.json; eq "$CODE" 0 "a broken profile is ignored"
O counter "$G" --bench "$BEN" --profile prof.json --floor 1; has "$OUT" "  Floor:  \$1" "--floor wins"
O counter "$G" --profile none.json --competing missing.json; eq "$ERR" "offer: no file at missing.json" "missing competing offer"

# ---- CLI
O; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: offer" "usage"
O counter "$G" --floor x; eq "$CODE" 2 "--floor is a number"
O bls; eq "$CODE" 2 "--soc is required"
done_ "hunter-salary/offer"

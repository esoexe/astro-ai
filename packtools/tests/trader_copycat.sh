#!/usr/bin/env bash
# trader-copycat/edgar13f: the assertions of trader-skill/tests/test_copycat.py, run against the C++ tool (--offline,
# Berkshire's real Q2/Q1 2026 13F fixtures), plus amendments, the pre-2023 thousands rule and the User-Agent rules.
source "$(dirname "$0")/lib.sh"
X="$FIX/trader-copycat"; SK="$FIX/trader-copycat-skill"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
unset SEC_USER_AGENT OPENFIGI_API_KEY
E() { run --skill-dir "$SK" trader-copycat/edgar13f "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
NOMAP=(--map /nonexistent.json); UA=(--user-agent "Test Runner test@example.org")

# the real numbers
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --combine --json
eq "$CODE" 0 "holdings exit"
eq "$(j '.meta | [.period, .filed, .accession, .form, .cik, .filer, .total_check]')" '["2026-06-30","2026-08-14","0001193125-26-352200","13F-HR","0001067983","BERKSHIRE HATHAWAY INC","Matches the cover page total."]' "latest filing is Q2 2026"
eq "$(j '.total')" 299253556246.0 "total"
eq "$(j '.meta.info_table_url')" '"https://www.sec.gov/Archives/edgar/data/1067983/000119312526352200/56757.xml"' "info table url"
eq "$(j '.meta.cover_total')" 299253556246.0 "cover total"
eq "$(j '[.positions[:5][] | [.ticker, .pct_of_13f]]')" '[["AAPL",22.04],["AXP",17.14],["GOOGL",12.62],["KO",10.86],["BAC",9.2]]' "top five as % of the full 13F"
eq "$(j '.positions[2] | [.tickers, .shares, .value, .classes, .cusips]')" '[["GOOGL","GOOG"],105979600.0,37764088383.0,["CAP STK CL A","CAP STK CL C"],["02079K305","02079K107"]]' "Alphabet combined"
eq "$(j '.positions | length')" 26 "26 combined positions"
eq "$(j '[.positions[] | .rows] | add')" 89 "89 rows"
eq "$(j '.positions[0] | keys_unsorted')" '["issuer","title","cusip","figi","put_call","share_type","value","shares","rows","pct_of_13f","cusips","issuer_cik","ticker","ticker_source","ticker_check","name","tickers","classes"]' "position keys"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --json
eq "$(j '.positions | length')" 29 "29 separate positions"
eq "$(j '[.positions[3], .positions[9]] | map([.ticker, .pct_of_13f])')" '[["GOOGL",9.41],["GOOG",3.21]]' "Alphabet splits by CUSIP"
eq "$(j '[.positions[] | {(.cusip): .ticker}] | add')" '{"037833100":"AAPL","025816109":"AXP","191216100":"KO","02079K305":"GOOGL","060505104":"BAC","166764100":"CVX","674599105":"OXY","H1467J104":"CB","615369105":"MCO","02079K107":"GOOG","500754106":"KHC","23918K108":"DVA","247361702":"DAL","829933100":"SIRI","92343E102":"VRSN","501044101":"KR","02005N100":"ALLY","526057104":"LEN","530909308":"LLYVK","650111107":"NYT","14040H105":"COF","530909100":"LLYVA","546347105":"LPX","670346105":"NUE","55616P104":"M","62944T105":"NVR","526057302":"LEN.B","47233W109":"JEF","23331A109":"DHI"}' "every CUSIP gets the right ticker"
eq "$(j '[.positions[] | select(.ticker_check) | .ticker] | sort')" '["JEF","NYT"]' "guesses are flagged"
eq "$(j '[.positions[] | select(.ticker_check) | .ticker_source] | unique')" '["name-prefix"]' "flagged by name prefix"
eq "$(j '[.positions[] | select(.ticker == "LEN.B" or .ticker == "GOOG") | .ticker_source] | unique')" '["override"]' "share classes from the bundled table"
eq "$(j '[.positions[] | select(.ticker == "AAPL" or .ticker == "BAC") | .name]')" '["Apple","Bank of America"]' "pretty names"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --combine --top 5
eq "$CODE" 0 "text exit"; has "$OUT" "filed 2026-08-14" "filed"; has "$OUT" "Matches the cover page total." "cover check"
has "$OUT" "up to 45 days late" "how old"; has "$OUT" "GOOGL+GOOG" "classes joined"; hasnt "$OUT" $'\u2014' "no em dash"
has "$OUT" "  3  GOOGL+GOOG  ALPHABET INC                          105,979,600    \$37,764,088,383   12.62" "row format"
has "$OUT" "  #  ticker      issuer                                     shares              value       %" "header"
has "$OUT" "     ... 21 more" "more"; has "$OUT" "Total \$299,253,556,246 across 26 positions, share classes combined. Weights are % of that total." "total line"
E holdings 1067983 --offline "$X" "${NOMAP[@]}"; has "$OUT" " 20  NYT         NEW YORK TIMES CO MTN BE               15,700,000     \$1,098,686,000    0.37 *" "NYT row"; has "$OUT" "* ticker matched by name or share class guess: confirm it before you copy, or run with --figi." "guess note"
has "$(echo "$OUT" | grep '^ *[0-9]*  NYT')" " *" "flag on the guess"

# copying
E copy 1067983 --top 5 --offline "$X" "${NOMAP[@]}" --dry-run
eq "$(j '[.weights[].ticker]')" '["AAPL","AXP","GOOGL","KO","BAC"]' "copy top five"
eq "$(j '[.weights[].weight]')" '[30.67,23.85,17.56,15.11,12.8]' "renormalised, 2 decimals"
eq "$(j '[.weights[].weight] | add | (. - 100 | fabs) < 0.05')" true "sum to 100"
eq "$(j '.weights[2].classes')" '["GOOGL","GOOG"]' "classes"
eq "$(j '.skipped | length')" 21 "21 skipped"; eq "$(j 'all(.skipped[]; .why == "outside the top 5")')" true "why"
eq "$(j 'keys_unsorted')" '["source","as_of","filed","cik","method","weights","skipped","caveats"]' "targets keys"
eq "$(j '.weights[0] | keys_unsorted')" '["ticker","name","weight","pct_of_13f","value","shares","cusips"]' "weight keys"
eq "$(j '.method')" '"top 5 stock positions by value, share classes combined, pct_of_13f scaled to 100"' "method"
[ -e trader/targets.json ] && bad "dry run must not write" || ok
OUTF="$TMP/trader/targets.json"
E copy 1067983 --top 5 --offline "$X" --map "$TMP/none.json" --out "$OUTF"
eq "$CODE" 0 "copy exit"; has "$OUT" "wrote $OUTF" "wrote"; has "$OUT" "Holdings as of 2026-06-30" "as of"
has "$OUT" "  AAPL    Apple                         30.67%  (22.04% of the 13F)" "weight line"
has "$OUT" "Next: trader-budget sizes these to your budget." "next"
eq "$(jq -c '[.as_of, (.source | contains("filed 2026-08-14")), (.source | contains("0001193125-26-352200")), [.weights[:3][] | [.ticker, .name]], [.weights[].weight], any(.caveats[]; contains("45 days"))]' "$OUTF")" \
   '["2026-06-30",true,true,[["AAPL","Apple"],["AXP","American Express"],["GOOGL","Alphabet"]],[30.67,23.85,17.56,15.11,12.8],true]' "targets file"
eq "$(tail -c 2 "$OUTF" | od -A n -t x1 | tr -d ' ')" "7d0a" "ends with a newline"
E copy 1067983 --offline "$X" "${NOMAP[@]}" --dry-run --exclude aapl --min-pct 1 --separate
eq "$(j '[.weights[].ticker][:3]')" '["AXP","KO","GOOGL"]' "excluded and separate"
eq "$(j '[.skipped[] | select(.ticker == "AAPL") | .why]')" '["excluded by you"]' "excluded why"
eq "$(j '[.skipped[] | .why] | map(select(startswith("below"))) | unique')" '["below --min-pct 1.00"]' "min pct why"
eq "$(j '.method')" '"all stock positions by value, pct_of_13f scaled to 100"' "method all"
E copy 1067983 --offline "$X" "${NOMAP[@]}" --dry-run --min-pct 99; eq "$CODE" 2 "nothing left exit"; eq "$ERR" "edgar13f: nothing left to copy" "nothing left"

# options and bonds are never copied
cat > "$TMP/mixed.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<informationTable xmlns="http://www.sec.gov/edgar/document/thirteenf/informationtable">
  <infoTable><nameOfIssuer>APPLE INC</nameOfIssuer><titleOfClass>COM</titleOfClass><cusip>037833100</cusip><value>600</value><shrsOrPrnAmt><sshPrnamt>3</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt></infoTable>
  <infoTable><nameOfIssuer>APPLE INC</nameOfIssuer><titleOfClass>CALL</titleOfClass><cusip>037833100</cusip><value>300</value><shrsOrPrnAmt><sshPrnamt>100</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt><putCall>Call</putCall></infoTable>
  <infoTable><nameOfIssuer>SOME CO</nameOfIssuer><titleOfClass>NOTE 2.5% 2030</titleOfClass><cusip>123456ab7</cusip><value>1,00</value><shrsOrPrnAmt><sshPrnamt>100</sshPrnamt><sshPrnamtType>prn</sshPrnamtType></shrsOrPrnAmt></infoTable>
</informationTable>
XML
E copy --xml "$TMP/mixed.xml" --tickers "$X/company_tickers.json" "${NOMAP[@]}" --dry-run
eq "$CODE" 0 "mixed exit"; eq "$(j '[.weights[] | [.ticker, .weight]]')" '[["AAPL",100.0]]' "only the stock"
eq "$(j '[.skipped[].why] | sort')" '["CALL row, not a stock you can copy","PRN row, not a stock you can copy"]' "options and bonds skipped"
eq "$(j '[.source, .as_of, .filed, .cik]')" '["mixed.xml 13F-HR, quarter ending ?, filed ? (accession ?)","?","?",null]' "xml meta"
E holdings --xml "$TMP/mixed.xml" --tickers "$X/company_tickers.json" "${NOMAP[@]}" --json
eq "$(j '[.total, [.positions[] | [.cusip, .put_call, .share_type, .pct_of_13f]]]')" '[1000.0,[["037833100","","SH",60.0],["037833100","CALL","SH",30.0],["123456AB7","","PRN",10.0]]]' "aggregate by CUSIP and kind"
E holdings --xml "$TMP/mixed.xml" --tickers "$X/company_tickers.json" "${NOMAP[@]}"; has "$OUT" "APPLE INC [CALL]" "kind shown"; has "$OUT" "SOME CO [PRN]" "bond shown"
E holdings --xml "$TMP/mixed.xml" --tickers "$X/company_tickers.json" "${NOMAP[@]}" --thousands --json; eq "$(j '.total')" 1000000.0 "--thousands"
printf '<a><b>' > "$TMP/bad.xml"; E holdings --xml "$TMP/bad.xml" --no-tickers "${UA[@]}"; eq "$CODE" 2 "bad xml exit"; has "$ERR" "edgar13f: information table is not valid XML: no element found" "bad xml"
printf '<a><b>1</b></a>' > "$TMP/empty.xml"; E holdings --xml "$TMP/empty.xml" --no-tickers "${UA[@]}"; eq "$ERR" "edgar13f: no infoTable rows found. Is this a 13F information table?" "no rows"
sed 's|<value>600</value>|<value>six</value>|' "$TMP/mixed.xml" > "$TMP/nan.xml"; E holdings --xml "$TMP/nan.xml" --no-tickers "${UA[@]}"; eq "$ERR" "edgar13f: bad number in row for APPLE INC" "bad number"
E holdings --xml "$TMP/mixed.xml" --no-tickers; eq "$CODE" 2 "--no-tickers still needs a User-Agent (as in edgar13f.py)"; has "$ERR" "SEC requires a User-Agent" "UA for --xml without --tickers"
# the xml file needs no network and no User-Agent
E holdings --xml "$X/000119312526352200/56757.xml" --tickers "$X/company_tickers.json" --combine "${NOMAP[@]}" --json --config "$TMP/none.json"
eq "$CODE" 0 "xml exit"; eq "$(j '[.total, .positions[0].ticker, .meta.filer]')" '[299253556246.0,"AAPL","56757.xml"]' "xml file without network"
E holdings --xml "$X/000119312526352200/56757.xml" "${NOMAP[@]}" --config "$TMP/none.json"; eq "$CODE" 2 "tickers need SEC"; has "$ERR" "User-Agent" "UA asked"

# changes
E diff 1067983 --offline "$X" "${NOMAP[@]}" --json
eq "$(j '.meta.prior_period')" '"2026-03-31"' "prior quarter"
eq "$(j '[.changes[] | select(.status == "NEW") | .ticker]')" '["DHI"]' "NEW"
eq "$(j '[.changes[] | select(.status == "SOLD OUT") | .ticker]')" '["STZ"]' "SOLD OUT"
eq "$(j '[.changes[] | select(.status == "ADDED") | .ticker] | sort')" '["DAL","GOOG","GOOGL","LEN","LEN.B","M","NYT"]' "ADDED"
eq "$(j '[.changes[] | select(.status == "TRIMMED") | .ticker] | sort')" '["ALLY","BAC","COF","DVA","KR","NUE"]' "TRIMMED"
eq "$(j '[.changes[] | select(.status == "UNCHANGED")] | length')" 15 "UNCHANGED"
eq "$(j '.changes[] | select(.ticker == "BAC") | [.change, .change_pct]')" '[-30230150.0,-5.9]' "BAC change"
eq "$(j '.changes[] | select(.ticker == "STZ") | [.value_before, .change_pct, .shares_after]')" '[94933500.0,-100.0,0.0]' "STZ"
eq "$(j '.changes[] | select(.ticker == "DHI") | [.change_pct, .shares_before]')" '[null,0.0]' "DHI"
eq "$(j '.changes[0] | keys_unsorted')" '["status","issuer","title","cusip","ticker","put_call","shares_before","shares_after","change","change_pct","value_after","value_before"]' "change keys"
E diff 1067983 --offline "$X" "${NOMAP[@]}"
eq "$CODE" 0 "diff text exit"; has "$OUT" "2026-06-30 vs 2026-03-31" "periods"; has "$OUT" "SOLD OUT (1)" "sold out"; has "$OUT" "not in a 13F" "note"
has "$OUT" "  STZ     CONSTELLATION BRANDS INC CL A            632,890 shares (\$94,933,500 at the prior quarter end)" "sold line"
has "$OUT" "  BAC     BANK OF AMER CORP COM                    513,624,165 -> 483,394,015 (-5.9%)" "trim line"
has "$OUT" "  GOOG    ALPHABET INC CAP STK CL C                3,585,215 -> 27,188,433 (+658.3%)" "add line"
has "$OUT" "  AAPL    APPLE INC COM                            227,917,808 shares" "unchanged line"
E diff --xml "$X/000119312526352200/56757.xml" --no-tickers "${UA[@]}"; eq "$ERR" "edgar13f: with --xml, give the prior quarter as --prior-xml" "prior xml needed"
E diff --xml "$X/000119312526352200/56757.xml" --prior-xml "$X/000119312526226661/53405.xml" --no-tickers "${UA[@]}" --json
eq "$(j '[.meta.prior_period, ([.changes[] | select(.status == "SOLD OUT")] | length), ([.changes[] | select(.status == "UNCHANGED")] | length)]')" '["prior file",1,15]' "diff two files"

# your own map wins; without the share-class table the class guess is flagged; OpenFIGI
echo '{"650111107": "NYT", "47233W109": "jef", "bad": "X"}' > "$TMP/tickers.json"
E holdings 1067983 --offline "$X" --map "$TMP/tickers.json" --json; eq "$(j '[.positions[] | select(.ticker_check)] | length')" 0 "your own map wins"
eq "$(j '[.positions[] | select(.cusip == "47233W109") | [.ticker, .ticker_source]]')" '[["JEF","override"]]' "map normalised"
mkdir -p "$TMP/noskill"; run --skill-dir "$TMP/noskill" trader-copycat/edgar13f holdings 1067983 --offline "$X" "${NOMAP[@]}" --json
eq "$(j '[.positions[] | select(.cusip == "530909100" or .cusip == "02079K305") | [.ticker, .ticker_check]]')" '[["GOOGL",true],["LLYVA",true]]' "class letter guesses (no -A suffix for GOOGL)"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --figi --json
eq "$(j '[.positions[] | select(.ticker_source == "openfigi") | .ticker] | sort')" '["JEF","NYT"]' "OpenFIGI fills the guesses"
eq "$(j '[.positions[] | select(.ticker_check)] | length')" 0 "nothing left to check"
# amendments and the thousands rule, on a copy of the submissions file
mkdir -p "$TMP/off"; cp -r "$X"/* "$TMP/off/"
jq '.filings.recent.form[1] = "13F-HR/A" | .filings.recent.reportDate[1] = "2026-06-30"' "$X/CIK0001067983.json" > "$TMP/off/CIK0001067983.json"
A1=$(jq -r '.filings.recent | [.accessionNumber[1], .filingDate[1]] | join(" on ")' "$TMP/off/CIK0001067983.json")
E holdings 1067983 --offline "$TMP/off" "${NOMAP[@]}" --json
eq "$(j '.meta.notes')" "[\"Also filed for this quarter: $A1. An amendment can restate the table or add holdings that were kept confidential. Read it with --accession.\"]" "amendment note"
I0=$(jq -r '.filings.recent.form | index("13F-HR")' "$X/CIK0001067983.json")
jq ".filings.recent.filingDate[$I0] = \"2022-11-14\"" "$X/CIK0001067983.json" > "$TMP/off/CIK0001067983.json"
E holdings 1067983 --offline "$TMP/off" "${NOMAP[@]}" --accession 0001193125-26-352200 --json
eq "$(j '[.total, .meta.cover_total, .meta.total_check]')" '[299253556246000.0,299253556246000.0,"Matches the cover page total."]' "filed before 2023: thousands"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --period 2025-03-31
eq "$CODE" 2 "period exit"; has "$ERR" "/000095012325005701/index.json for https://www.sec.gov/Archives/edgar/data/1067983/000095012325005701/index.json" "the original 13F-HR for a period"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --accession 0000000000-00-000000; eq "$ERR" "edgar13f: accession 0000000000-00-000000 is not in this filer's 13F list" "unknown accession"
E holdings 1067983 --offline "$X" "${NOMAP[@]}" --period 1999-12-31; eq "$ERR" "edgar13f: no 13F-HR found for period 1999-12-31" "no filing for period"
E holdings --offline "$X"; eq "$ERR" "edgar13f: give a CIK, or --xml FILE" "cik needed"
E holdings 1 --offline "$X"; eq "$CODE" 2 "offline miss exit"
eq "$ERR" "edgar13f: offline: no file $X/CIK0000000001.json for https://data.sec.gov/submissions/CIK0000000001.json" "offline never touches the network"
E filings abc --offline "$X"; eq "$ERR" "edgar13f: not a CIK: 'abc'" "not a CIK"

# filers
E find buffett --json; eq "$(j '[.[].cik]')" '["0001067983"]' "bundled buffett"; eq "$(j '.[0].source')" '"bundled"' "source"
E find michael burry --json; eq "$(j '.[0] | [.cik, (.status | contains("2025-11-03"))]')" '["0001649339",true]' "burry"
E find 1067983 --json; eq "$(j '.[0].name')" '"Berkshire Hathaway Inc"' "find by CIK"
E find nobody at all; eq "$CODE" 1 "nobody"; eq "$OUT" "No filer matched 'nobody at all'. Try --sec to search every EDGAR filer, or a shorter name." "nobody text"
eq "$(jq '[.filers[].cik | test("^[0-9]{10}$")] | all' "$SK/filers.json")" true "bundled CIKs are 10 digits"
E find ackman; eq "$CODE" 0 "ackman exit"; has "$OUT" "0001336528" "ackman"
E find scion asset --sec --offline "$X" --json
eq "$(j '[.[] | select(.cik == "0001649339")] | length')" 1 "a bundled hit is not repeated"
eq "$(j '[.[] | select(.source == "sec") | {cik, name}]')" '[{"cik":"0001768021","name":"Scion Asset Partners, LP"}]' "search every filer"
E filings 1067983 --offline "$X" --json
eq "$(j '[.filer, .cik, (.filings | length), .filings[0].period, (all(.filings[]; .form == "13F-HR" or .form == "13F-HR/A"))]')" '["BERKSHIRE HATHAWAY INC","0001067983",8,"2026-06-30",true]' "filings list"
E filings 1067983 --offline "$X" --limit 2; has "$OUT" "  13F-HR    quarter 2026-03-31  filed 2026-05-15  0001193125-26-226661" "filings line"
E filings 1067983 --offline "$X" --limit 0; eq "$CODE" 1 "no filings exit"; has "$OUT" "  no 13F filings" "no filings"
E filings 1067983 --offline "$X" --limit 100 --json; eq "$(j '[.filings[] | select(.period == "2025-03-31") | [.form, .accession]]')" '[["13F-HR/A","0000950123-25-008361"],["13F-HR","0000950123-25-005701"]]' "amendment listed"

# the User-Agent rules
echo '{"sec_user_agent": "Your Name you@example.com"}' > "$TMP/cfg.json"
E filings 1067983 --config "$TMP/cfg.json"; eq "$CODE" 2 "placeholder refused"; has "$ERR" "User-Agent" "placeholder message"
E filings 1067983 --config "$TMP/missing.json"; eq "$CODE" 2 "missing config"; has "$ERR" "  2. export SEC_USER_AGENT=\"Your Name you@example.com\"" "how to set it"
E filings 1067983 --config "$TMP/cfg.json" --user-agent "no email here"; has "$ERR" "SEC requires a User-Agent" "no email"
echo '{"sec_user_agent": "Ada Lovelace ada@example.org"}' > "$TMP/cfg.json"
E filings 1067983 --config "$TMP/cfg.json"; eq "$CODE" 2 "UA accepted (network off in the suites)"; eq "$ERR" "edgar13f: could not reach https://data.sec.gov/submissions/CIK0001067983.json: network is off (ASTRO_TOOL_OFFLINE=1)" "it tried SEC"
SEC_USER_AGENT="Grace Hopper grace@example.org" E filings 1067983 --config "$TMP/missing.json"; has "$ERR" "could not reach" "env UA"
echo 'not json' > "$TMP/cfg.json"; E filings 1067983 --config "$TMP/cfg.json"; has "$ERR" "SEC requires a User-Agent" "broken config is no UA"
E; eq "$CODE" 2 "no command exit"; has "$OUT" "    copy                write trader/targets.json from the top positions" "help"
E copy --help; has "$OUT" "skip positions under this % of the 13F" "percent in help"
done_ "trader-copycat"

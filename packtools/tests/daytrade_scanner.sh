#!/usr/bin/env bash
# daytrade-scanner/scan: the assertions of day-trader-skill/tests/test_scanner.py, run against the C++ tool. Unit calls
# (classify, scan_daily, scan_orb, orb_window) become CLI runs: one-row symbol directories per classify case, synthetic v8
# charts built with jq for bars_series(), and `--now`/`--at` for the clock. The networked tests (the fake yahoo_get for
# query2 -> query1, the 429 back-off and the stage-2 cap through a fake Yahoo) can't be reached offline and are not ported.
source "$(dirname "$0")/lib.sh"
S="$FIX/daytrade-scanner"; SYMDIR="$S/symdir"; DAILY="$S/daily-spark-2026-10-19.json"; WALL="$S/wall5m-2026-10-19.json.gz"
LIVE="$S/spark-3mo-live-2026-10-20.json"; SPARK5="$S/spark-5m-live-2026-10-20-1001.json"; TSLA5="$S/TSLA-5m-live-2026-10-20-1001.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
P=daytrade-scanner/scan
j() { echo "$OUT" | jq -c "$1"; }
ets() { TZ=America/New_York date -d "$1" +%s; }   # New York wall clock -> unix seconds
# bars SYM DAY CLOSES_JSON [HIGHS_JSON] [START]: a v8 chart of 5-minute bars from START (09:30) on DAY, like bars_series()
bars() { local t0; t0=$(ets "$2 ${5:-09:30}")
  jq -n --arg s "$1" --argjson t0 "$t0" --argjson c "$3" --argjson h "${4:-null}" '
    {chart: {result: [{meta: {symbol: $s}, timestamp: [range(0; $c|length) | $t0 + 300 * .],
      indicators: {quote: [{close: $c, open: null,
        high: ($h // ($c | map(if . == null then null else . + 0.1 end))),
        low: ($c | map(if . == null then null else . - 0.1 end)), volume: ($c | map(1000))}]}}], error: null}}'; }
# daily SYM FIRST_DAY N CLOSES_JSON [META_JSON]: N daily bars at 9:30 on consecutive days
daily() { local t0; t0=$(ets "$2 09:30")
  jq -n --arg s "$1" --argjson t0 "$t0" --argjson c "$4" --argjson m "${5:-{\}}" '
    {chart: {result: [{meta: ($m + {symbol: $s}), timestamp: [range(0; $c|length) | $t0 + 86400 * .],
      indicators: {quote: [{close: $c}]}}], error: null}}'; }

# ---- universe: the kept list
run $P universe --offline "$SYMDIR" --out "$TMP/u/universe.txt"; eq "$CODE" 0 "universe exits 0"
has "$OUT" "Kept 17 US-listed" "kept 17"
has "$OUT" "Nasdaq Trader symbol directories, files of 2026-10-19 10:01 ET: 36 rows." "rows and file time"
has "$OUT" "Wrote $TMP/u/universe.txt (one SYMBOL|Name a line)." "wrote line"
eq "$(cut -d'|' -f1 "$TMP/u/universe.txt" | tr '\n' ' ')" "A AACG AACI AAPL AME AMX ARLP BEP BF.B BRK.B IRS KO OBAI ORGO PFBC PRT TSLA " "the kept list"
eq "$(wc -l < "$TMP/u/universe.txt" | tr -d ' ')" 17 "17 lines"
eq "$(grep -c '^BRK.B|Berkshire Hathaway Inc. New Common Stock$' "$TMP/u/universe.txt")" 1 "BRK.B line"
eq "$(grep '^TSLA|' "$TMP/u/universe.txt")" "TSLA|Tesla, Inc. - Common Stock" "TSLA name"
run $P universe --offline "$SYMDIR" --no-write --json
eq "$(j '[.count, .rows, .files_created, .out]')" '[17,36,"2026-10-19 10:01",null]' "universe json"
eq "$(j '.dropped | add')" 19 "19 dropped"
eq "$(j '.dropped | [.ETF, ."test issue", .coupon, .unit, .right]')" '[2,2,2,2,2]' "dropped counts"
[ -e trader/universe.txt ] && bad "--no-write wrote a file" || ok
has "$(cd "$TMP" && $T $P universe --offline "$SYMDIR" --no-write)" "Dropped: 2 ETF, 2 unit, 2 right, 2 test issue, 2 unit / warrant / right suffix, 2 coupon, 1 warrant, 1 depositary," "dropped line, most first"

# ---- classify: one case per rule, each as a one-row symbol directory
why() {   # why SYM -> the rule (or keep)
  local d="$TMP/c/$1" f; mkdir -p "$d"
  for f in nasdaqlisted.txt otherlisted.txt; do head -1 "$SYMDIR/$f" > "$d/$f"; awk -F'|' -v s="$1" 'NR > 1 && $1 == s' "$SYMDIR/$f" >> "$d/$f"; done
  OUT=$($T $P universe --offline "$d" --no-write --json 2>&1)
  echo "$OUT" | jq -r 'if .count == 1 then "keep" else (.dropped | keys[0]) end'
}
while read -r sym want; do eq "$(why "$sym")" "${want//_/ }" "classify $sym"; done <<'CASES'
ZAZZT test_issue
ZZZTX test_issue
AAAP ETF
SPY ETF
ABR$D not_a_plain_ticker
AAC.U unit_/_warrant_/_right_suffix
AAC.W unit_/_warrant_/_right_suffix
AACIW warrant
HAVAR unit_or_right
ATMP note
GJH coupon
EAI coupon
CDZIP depositary
AACIU unit
ARCIU unit
AACPR right
GENVR right
BHK bond
CHTRP preferred
AAPL keep
AACI keep
AACG keep
PFBC keep
OBAI keep
AMX keep
IRS keep
ARLP keep
BEP keep
PRT keep
BRK.B keep
BF.B keep
AME keep
ORGO keep
CASES
mkdir -p "$TMP/ns"; head -1 "$SYMDIR/otherlisted.txt" > "$TMP/ns/otherlisted.txt"
printf 'Symbol|Security Name|Market Category|Test Issue|Financial Status|Round Lot Size|ETF|NextShares\nZXYZ|Some Fund - NextShares|G|N|N|100|N|Y\n' > "$TMP/ns/nasdaqlisted.txt"
run $P universe --offline "$TMP/ns" --no-write --json; eq "$(j '[.count, .dropped]')" '[0,{"NextShares":1}]' "NextShares rule"; eq "$CODE" 1 "nothing kept exits 1"
mkdir -p "$TMP/html"; echo "<html>maintenance</html>" > "$TMP/html/nasdaqlisted.txt"; cp "$SYMDIR/otherlisted.txt" "$TMP/html/"
run $P universe --offline "$TMP/html" --no-write; eq "$ERR" "scan: not a Nasdaq Trader symbol directory" "not a symbol file"; eq "$CODE" 1 "not a symbol file exits 1"
run $P universe --offline "$TMP/nowhere" --no-write; eq "$ERR" "scan: no nasdaqlisted.txt in $TMP/nowhere" "missing symdir"

# ---- time
run $P daily --offline "$DAILY" --now tomorrow; eq "$ERR" 'scan: --now wants a time like "2026-10-19 10:15" (New York) or ...Z (UTC)' "bad --now"
run $P orb --offline "$WALL" --at 25:00; eq "$ERR" "scan: --at wants a New York time like 10:15" "bad --at"
run $P orb --offline "$WALL" --at 10:15 --until 9.30; eq "$ERR" "scan: --until wants a New York time like 10:15" "bad --until"
run $P orb --offline "$WALL" --now 2026-10-19T14:15:00Z --min-price 0 --no-write --json; eq "$(j '[.created_at, .as_of]')" '["2026-10-19T14:15:00Z","2026-10-19 10:15 ET"]' "UTC --now"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --min-price 0 --no-write --json; eq "$(j '.created_at')" '"2026-10-19T14:15:00Z"' "New York --now in EDT"

# ---- spark: a real answer
run $P daily --offline "$LIVE" --now "2026-10-20 09:14" --min-price 0 --min-volume 0 --no-write --json
eq "$(j '.universe')" 19 "19 of 20 came back (ZZZZQX dropped)"
eq "$(j '.as_of')" '"2026-10-19 close"' "closes through Monday the next morning"
run $P daily --offline "$LIVE" --symbols BRK.B,ZZZZQX,TSLA --now "2026-10-20 09:14" --min-price 0 --min-volume 0 --no-write --json
eq "$(j '[.universe, .counts.fetched, .failed]')" '[3,2,1]' "BRK-B comes back as BRK.B, ZZZZQX missing"
run $P daily --offline "$LIVE" --symbols TSLA --now "2026-10-19 12:00" --min-price 0 --min-volume 0 --no-write --json
eq "$(j '.as_of')" '"2026-10-16 close"' "Monday's bar is in progress at noon"
run $P daily --offline "$LIVE" --symbols AMC --now "2026-10-20 09:14" --min-price 3 --min-volume 0 --no-write --json
eq "$(j '.counts.illiquid')" 1 "AMC at 2.98 is under a 3 floor"
run $P daily --offline "$LIVE" --symbols AMC --now "2026-10-20 09:14" --min-price 2.98 --min-volume 0 --no-write --json
eq "$(j '.counts.illiquid')" 0 "AMC at 2.98 passes a 2.98 floor"
run $P daily --offline "$LIVE" --symbols TSLA --now "2026-10-20 09:14" --min-price 0 --min-volume 38651174 --no-write --json
eq "$(j '.counts.illiquid')" 1 "TSLA volume 38651173 is under 38651174"
run $P daily --offline "$LIVE" --symbols TSLA --now "2026-10-20 09:14" --min-price 0 --min-volume 38651173 --no-write --json
eq "$(j '.counts.illiquid')" 0 "TSLA volume 38651173 meets 38651173"
echo '{"spark":{"result":null,"error":{"code":"Bad Request","description":"Number of symbols needs to be less than or equal to 20"}}}' > toomany.json
run $P daily --offline toomany.json; eq "$ERR" "scan: Yahoo spark: Number of symbols needs to be less than or equal to 20" "spark error"
echo '{"spark":{"result":[],"error":null}}' > nospark.json
run $P daily --offline nospark.json; eq "$ERR" "scan: no symbols to scan" "empty spark"
echo '{"spark":"x"}' > badspark.json
run $P daily --offline badspark.json; eq "$ERR" "scan: Yahoo's spark answer is not the shape this tool knows" "spark of the wrong shape"
mkdir -p ch; echo '{"chart":{}}' > ch/ZZZ.json; echo '{"chart":{"result":null,"error":{"code":"Not Found","description":"No data found, symbol may be delisted"}}}' > ch/QQQX.json
run $P daily --offline ch --now "2026-10-19 09:00" --no-write --json; eq "$(j '[.universe, .failed, .counts.fetched]')" '[2,2,0]' "v8 charts with no result are failures"; eq "$CODE" 1 "no hits exits 1"
echo '[1,2]' > list.json; run $P daily --offline list.json; eq "$ERR" "scan: offline file  is not a spark answer, a v8 chart or a bundle" "a list is not a chart"
echo '{"A": [1]}' > bundle-bad.json; run $P daily --offline bundle-bad.json; eq "$ERR" "scan: offline file A is not a spark answer, a v8 chart or a bundle" "a bad bundle entry"
echo 'nope' > notjson.json; run $P daily --offline notjson.json; has "$ERR" "scan: notjson.json is not valid JSON: " "not JSON"
echo '{"chart":{"result":[{"meta":{},"timestamp":[],"indicators":{"quote":[{"close":[]}]}}]}}' > anon.json
run $P daily --offline "$TMP/anon.json"; eq "$ERR" "scan: an offline v8 chart without a symbol: name the file <TICKER>.json" "a v8 chart without a symbol"
mkdir -p emptydir; run $P daily --offline emptydir; eq "$ERR" "scan: no .json files in emptydir" "empty folder"

# ---- daily: the reel's setups before the open
run $P daily --offline "$DAILY" --now "2026-10-19 09:00" --min-price 0 --min-volume 0 --no-write --json
eq "$CODE" 0 "daily exits 0"
eq "$(j '[.hits[] | {(.symbol): .setups}] | add')" '{"BAC":["oversold"],"XOM":["breakout_20d"],"JPM":["reclaim_ma20"],"KO":["breakout_20d"],"GE":["reclaim_ma20"],"HD":["breakout_20d"],"MS":["oversold"],"BLK":["breakout_20d"],"RCL":["bull_flag"],"CCL":["bull_flag"],"RIVN":["oversold"],"PNC":["oversold_bounce"],"F":["reclaim_ma20"],"CI":["breakout_20d"],"PLUG":["oversold"]}' "the 15 setups, most traded first"
eq "$(j '.counts')" '{"requested":19,"fetched":19,"illiquid":0,"short":0,"scanned":19,"hits":15}' "daily counts"
eq "$(j '.as_of')" '"2026-10-16 close"' "closes through Friday"
eq "$(j '.hits[] | select(.symbol == "BLK") | [.price, .rsi, .close_date]')" '[1134.38,63.7,"2026-10-16"]' "BLK"
eq "$(j '.hits[] | select(.symbol == "PNC") | .rsi == 34')" true "PNC bounced to RSI 34.0"
eq "$(j '.hits[] | select(.symbol == "BAC") | .rsi == 24')" true "BAC RSI 24.0"
eq "$(j '.hits[] | select(.symbol == "KO") | .volume')" 3680307 "KO volume"
eq "$(j '[.created_by, .mode, .universe, .scanned, .failed, .source, (.filters | .min_price == 0 and .min_volume == 0), .offline]')" '["trader-scanner","daily",19,19,0,"yahoo",true,"daily-spark-2026-10-19.json"]' "daily document"
eq "$(j '.note')" '"A scan is a list of charts that match a rule, not a list of stocks to buy."' "the note"
eq "$(j 'keys_unsorted')" '["created_by","created_at","mode","as_of","universe","scanned","failed","source","filters","note","hits","offline","counts","seconds"]' "daily keys in order"
eq "$(j '.hits[0] | keys_unsorted')" '["symbol","setups","price","close_date","rsi","volume"]' "daily hit keys"
BEFORE=$(j '[.hits, .counts, .as_of]')
run $P daily --offline "$DAILY" --now "2026-10-19 10:50" --min-price 0 --min-volume 0 --no-write --json
eq "$(j '[.hits, .counts, .as_of]')" "$BEFORE" "the in-progress daily bar is dropped"
run $P daily --offline "$DAILY" --symbols TSLA --now "2026-10-19 15:59" --min-price 0 --min-volume 0 --no-write --json; eq "$(j '.as_of')" '"2026-10-16 close"' "15:59: Monday not done"
run $P daily --offline "$DAILY" --symbols TSLA --now "2026-10-19 16:00" --min-price 0 --min-volume 0 --no-write --json; eq "$(j '.as_of')" '"2026-10-19 close"' "16:00: Monday is done"
run $P daily --offline "$DAILY" --now "2026-10-19 10:50" --no-write --json
eq "$(j '[.hits[].symbol] | sort')" '["BAC","CCL","F","JPM","KO","RIVN","XOM"]' "the liquidity filter"
eq "$(j '[.counts.illiquid, .counts.scanned]')" '[8,11]' "8 illiquid, 11 scanned"
eq "$(j '.filters | .min_price == 5 and .min_volume == 1000000')" true "daily default floors"
# half day: the bar of Fri Nov 27 completes at 1:00 PM
C30=$(jq -nc '[range(0; 31) | 10 + . * 0.01]')
daily X 2026-10-26 31 "$C30" '{"regularMarketPrice":10,"regularMarketVolume":5000000}' > hd.json
jq '.chart.result[0] |= (.timestamp += [('"$(ets "2026-11-27 09:30")"')] | .indicators.quote[0].close += [11.0])' hd.json > hd2.json
run $P daily --offline hd2.json --now "2026-11-27 12:59" --no-write --json; eq "$(j '.as_of')" '"2026-11-25 close"' "half day 12:59: not done"
run $P daily --offline hd2.json --now "2026-11-27 13:00" --no-write --json; eq "$(j '.as_of')" '"2026-11-27 close"' "half day 13:00: done"
# short and empty charts
daily X 2026-10-01 10 "$(jq -nc '[range(0;10) | 10.0]')" '{"regularMarketPrice":10,"regularMarketVolume":5000000}' > x.json
jq -n --slurpfile x x.json '{X: $x[0], E: {chart: {result: [{meta: {}, timestamp: [], indicators: {quote: [{close: []}]}}]}}}' > se.json
run $P daily --offline se.json --symbols X,E,GONE --now "2026-10-20 09:00" --no-write --json
eq "$(j '[.counts.fetched, .counts.short, .counts.illiquid, .counts.scanned, .hits, .as_of, .failed]')" '[2,1,1,0,[],null,1]' "short and empty charts"
eq "$CODE" 1 "no setups exits 1"
# RSI: an all-up run is 100 (and a 20-day closing high)
daily UP 2026-08-01 39 "$(jq -nc '[range(1;40) | . * 1.0]')" '{"regularMarketPrice":39,"regularMarketVolume":5000000}' > up.json
run $P daily --offline up.json --now "2026-10-20 09:00" --no-write --json; eq "$(j '.hits[0] | [.setups, .rsi == 100]')" '[["breakout_20d"],true]' "RSI of an all-up run"
# an all-down run is RSI 0 (oversold), and an up day after it is an oversold bounce
DOWN=$(jq -nc '[range(0;35) | 100 - . * 1.0] + [67.0]')
daily DN 2026-08-01 36 "$DOWN" '{"regularMarketPrice":66,"regularMarketVolume":5000000}' > dn.json
run $P daily --offline dn.json --now "2026-10-20 09:00" --no-write --json; eq "$(j '.hits[0].setups')" '["oversold","oversold_bounce"]' "an all-down run, then an up day"

# ---- orb: the reel's 10:15 scan
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --min-price 0 --no-write --json
eq "$CODE" 0 "orb exits 0"
eq "$(j '[.universe, .scanned, (.hits | length), .counts.unusable, .failed]')" '[234,231,42,2,1]' "234 charts, 231 usable, 42 hits"
eq "$(j '.hits[] | select(.symbol == "TSLA")')" '{"symbol":"TSLA","setups":["orb"],"price":364.18,"at":"10:15 AM","bar_start":"10:10","bar_index":8,"or_high":362.18,"or_low":354.47,"pct_over":0.55}' "TSLA"
eq "$(j '.hits[0].symbol')" '"AMC"' "sorted by time, then symbol"
eq "$(j '[.hits[].bar_index] | . == sort')" true "times in order"
eq "$(j '[.hits[] | select(.at == "10:15 AM")] | length')" 11 "11 broke out at 10:15"
eq "$(j '.hits[] | select(.symbol == "LLY") | [.price == 1255, .or_high, .pct_over == 0]')" '[true,1254.99,true]' "LLY by a cent"
eq "$(j 'keys_unsorted')" '["created_by","created_at","mode","as_of","universe","scanned","failed","unconfirmed","source","or_minutes","interval","filters","note","hits","offline","counts","seconds"]' "orb keys in order"
eq "$(j '[.unconfirmed, .or_minutes, .interval, .counts.candidates == .counts.confirmed]')" '[0,15,"5m",true]' "offline confirms every candidate"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --no-write --json
eq "$(j '[(.hits | length), .counts.illiquid, ([.hits[].symbol] | index("AMC"))]')" '[41,9,null]' "the price floor drops AMC"
eq "$(j '.filters | .min_price == 5 and .min_volume == 0')" true "orb default floors"
run $P orb --offline "$WALL" --now "2026-10-19 10:14" --symbols TSLA --no-write --json; eq "$(j '.hits')" '[]' "10:14: the 10:10 bar is not done"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --symbols TSLA --no-write --json; eq "$(j '.hits[0].at')" '"10:15 AM"' "10:15: done"
run $P orb --offline "$WALL" --now "2026-10-19 11:00" --symbols TSLA --no-write --json; eq "$(j '.hits[0].price')" 364.18 "the live tick is dropped, still the first close above"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --symbols SQ,WBD,PARA,TSLA --min-price 0 --no-write --json
eq "$(j '[.counts.fetched, .counts.unusable, .failed, .scanned]')" '[3,2,1,1]' "SQ failed; WBD and PARA unusable"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --min-price 0 --min-volume 100000000 --no-write --json
eq "$(j '.counts.illiquid > 200')" true "a volume floor counts as illiquid"
# a missing bar makes a chart unusable
bars X 2026-10-20 '[10, 10.2, 10.1, null, 10.5, 10.6, 10.7, 10.8, 10.9]' > miss.json
run $P orb --offline miss.json --now "2026-10-20 10:15" --min-price 0 --no-write --json; eq "$(j '[.counts.unusable, .scanned]')" '[1,0]' "a missing bar"
bars X 2026-10-20 '[10, 10.2, 10.1, 10.0, 10.5, 10.6, 10.7, 10.8, 10.9]' > full.json
run $P orb --offline full.json --now "2026-10-20 10:15" --min-price 0 --no-write --json; eq "$(j '.hits[0] | [.at, .bar_index, .or_high, .price]')" '["9:55 AM",4,10.3,10.5]' "a synthetic breakout: 10.0 is under the 10.3 high, 10.5 is over"
# winter time, a half day and a 30-minute range (Fri Nov 27 2026, UTC-5, 1:00 PM close)
WC=$(jq -nc '[100, 101, 100.5, 100.8, 101.2, 100.9, 101.5] + [range(0;40) | 101.0]')
bars X 2026-11-27 "$WC" "$(echo "$WC" | jq -c 'map(. + 0.2)')" > nov27.json
run $P orb --offline nov27.json --now "2026-11-27 12:00" --or-minutes 30 --min-price 0 --no-write --json
eq "$(j '[.counts.scanned, .hits[0].at, .hits[0].or_high, .or_minutes]')" '[1,"10:05 AM",101.4,30]' "a 30-minute range on a half day"
run $P orb --offline nov27.json --now "2026-11-27 12:59" --or-minutes 30 --min-price 0 --no-write --json; eq "$(j '.counts.unusable')" 0 "12:59: 41 bars, the ones after 1:00 dropped"
run $P orb --offline nov27.json --now "2026-11-27 13:10" --no-write; has "$ERR" "closed at 1:00 PM" "half day close"
eq "$ERR" "scan: The market closed at 1:00 PM ET on Fri 2026-11-27. Replay the session with --at 10:15." "closed message"
# window messages
msg() { run $P orb --offline "$WALL" --now "$1" "${@:2}" --no-write; echo "$ERR"; }
has "$(msg "2026-10-17 10:15")" "No session on Sat 2026-10-17 (weekend): the market is closed. Run orb on a trading day after the opening range, or replay a past day with --offline." "weekend"
has "$(msg "2026-11-26 10:15")" "Thanksgiving" "Thanksgiving"
eq "$(msg "2026-10-19 09:40")" "scan: It is 9:40 AM ET: the 15-minute opening range runs 9:30 to 9:45 AM and the first bar after it closes at 9:50 AM. Run orb again after 9:50 AM." "9:40"
has "$(msg "2026-10-19 09:47")" "Run orb again after 9:50 AM" "9:47"
eq "$(msg "2026-10-19 15:45")" "scan: It is 3:45 PM ET, after 15:30, your last time for new entries (no_entries_after): an opening-range breakout found now is expired. Run orb tomorrow after 9:50 AM, or replay today with --at 10:15." "15:45 expired"
has "$(msg "2026-10-19 11:30" --until 11:00)" "expired" "--until 11:00"
has "$(msg "2026-10-19 16:30")" "closed at 4:00 PM" "after the close"
eq "$(msg "2026-10-19 10:00" --at 10:15)" "scan: --at 10:15 is later than now (10:00 AM ET)." "later than now"
eq "$(msg "2026-10-19 17:00" --at 16:30)" "scan: --at 16:30 is after the 4:00 PM close on Mon 2026-10-19." "after the close"
eq "$(msg "2026-10-19 10:15" --or-minutes 7)" "scan: --or-minutes wants a multiple of 5 from 5 to 120 (15 is the classic)" "--or-minutes 7"
has "$(msg "2026-10-19 10:15" --or-minutes 125)" "multiple of 5" "--or-minutes 125"
run $P orb --offline "$WALL" --now "2026-10-19 16:30" --at 10:15 --min-price 0 --no-write --json; eq "$(j '[.as_of, (.hits | length)]')" '["2026-10-19 10:15 ET",42]' "--at after the day"
run $P orb --offline "$WALL" --now "2026-10-19 09:50" --no-write --json; eq "$(j '.as_of')" '"2026-10-19 09:50 ET"' "9:50 is the first cutoff"
run $P orb --offline "$WALL" --now "2026-10-19 10:15:59" --no-write --json; eq "$(j '.as_of')" '"2026-10-19 10:15 ET"' "the cutoff drops the seconds"

# ---- a live 5-minute answer fetched at 10:01:47 AM ET on Tue Oct 20
run $P orb --offline "$SPARK5" --now 2026-10-20T14:01:48Z --min-price 0 --no-write --json
eq "$(j '[.universe, .as_of, .counts.unusable, .hits]')" '[20,"2026-10-20 10:01 ET",0,[]]' "spark: 20 usable to 10:01, no highs so no hits offline"
run $P orb --offline "$SPARK5" --symbols BRK.B --now 2026-10-20T14:01:48Z --min-price 0 --no-write --json; eq "$(j '.counts.fetched')" 1 "BRK.B in the spark answer"
run $P orb --offline "$SPARK5" --symbols TSLA,AMC --now 2026-10-20T14:01:48Z --no-write --json; eq "$(j '[.counts.illiquid, .counts.scanned]')" '[1,1]' "AMC under the floor"
run $P orb --offline "$TSLA5" --now 2026-10-20T14:01:48Z --no-write --json
eq "$(j '.hits')" '[{"symbol":"TSLA","setups":["orb"],"price":370.58,"at":"10:00 AM","bar_start":"09:55","bar_index":5,"or_high":370.49,"or_low":363.64,"pct_over":0.03}]' "TSLA broke its range at ten"
run $P orb --offline "$TSLA5" --now 2026-10-20T14:01:00Z --no-write --json; eq "$(j '.hits[0].at')" '"10:00 AM"' "10:01 still sees the 9:55 bar"
run $P orb --offline "$TSLA5" --now 2026-10-20T13:59:59Z --no-write --json; eq "$(j '.hits')" '[]' "9:59: the 9:55 bar is in progress"
run $P orb --offline "$TSLA5" --at 10:01 --no-write --json; eq "$(j '[.as_of, .hits[0].price]')" '["2026-10-20 10:01 ET",370.58]' "offline --at uses the chart's day"

# ---- CLI: universe, daily, orb end to end
run $P universe --offline "$SYMDIR" --out trader/universe.txt; eq "$CODE" 0 "universe to the default place"
run $P daily --offline "$DAILY" --now "2026-10-19 09:00" --out trader/watchlist.json --min-volume 0; eq "$CODE" 0 "daily over a list"
eq "$OUT" "$(cat <<'X'
Daily scan, Mon 2026-10-19 09:00 ET, closes through 2026-10-16 (Yahoo spark, daily closes only).
Fetched 19 of 19 charts. 18 passed the liquidity filter ($5+ and 0+ shares); 18 scanned.
14 charts matched a setup: 5 20-day closing-high breakout, 2 bull flag, 3 reclaimed the 20-day average, 3 oversold (RSI under 30), 1 oversold bounce.
Most traded first (price x volume):
  BAC        $51.23  RSI  24.0  oversold (RSI under 30)
  XOM       $178.98  RSI  73.0  20-day closing-high breakout
  JPM       $339.72  RSI  51.2  reclaimed the 20-day average
  KO         $93.90  RSI  75.3  20-day closing-high breakout
  GE        $313.83  RSI  47.3  reclaimed the 20-day average
  HD        $309.63  RSI  60.2  20-day closing-high breakout
  MS        $180.57  RSI  25.5  oversold (RSI under 30)
  BLK     $1,134.38  RSI  63.7  20-day closing-high breakout
  RCL       $277.68  RSI  55.2  bull flag
  CCL        $25.90  RSI  56.8  bull flag
  RIVN       $12.42  RSI  22.7  oversold (RSI under 30)
  PNC       $217.39  RSI  34.0  oversold bounce
  F          $12.43  RSI  45.2  reclaimed the 20-day average
  CI        $302.30  RSI  73.0  20-day closing-high breakout
Wrote trader/watchlist.json (mode daily, 14 hits).
A scan is a list of charts that match a rule, not a list of stocks to buy. Next: pick one chart, then /trader-news TICKER.
X
)" "daily text"
run $P daily --offline "$DAILY" --now "2026-10-19 09:00" --show 2 --no-write --min-volume 0 --min-price 0
has "$OUT" "  ... and 13 more in the watchlist (--show N for more)." "--show 2"; hasnt "$OUT" "Wrote" "--no-write says nothing about a file"
run $P daily --universe trader/universe.txt --offline "$DAILY" --now "2026-10-19 09:00" --out trader/watchlist.json --min-volume 0
eq "$CODE" 0 "daily over the universe"; has "$OUT" "Fetched 2 of 17 charts (15 failed or had no data)" "partial"
eq "$(jq -c '[.mode, .universe, .scanned, .failed, .as_of, .hits]' trader/watchlist.json)" '["daily",17,2,15,"2026-10-16 close",[{"symbol":"KO","setups":["breakout_20d"],"price":93.9,"close_date":"2026-10-16","rsi":75.3,"volume":3680307}]]' "the watchlist"
has "$OUT" "1 chart matched a setup: 1 20-day closing-high breakout." "one chart"
has "$OUT" "Wrote trader/watchlist.json (mode daily, 1 hit)." "1 hit"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --min-price 0 --out trader/watchlist.json
eq "$CODE" 0 "orb text exits 0"
has "$OUT" "42 of 231 charts closed above their opening-range high by 10:15 AM" "orb summary"
has "$OUT" "  10:15 AM  TSLA      \$364.18  range 354.47 to 362.18  +0.55% over" "the TSLA line"
has "$OUT" "not a list of stocks to buy" "the note"
eq "$(echo "$OUT" | sed -n 1p)" "Opening-range breakout scan, Mon 2026-10-19 10:15 AM ET: 15-minute range (9:30 to 9:45 AM), 5-minute bars, Yahoo." "orb title"
eq "$(echo "$OUT" | sed -n 2p)" "Fetched 233 of 234 charts (1 failed or had no data); 231 usable; 95 candidates, 95 confirmed with real highs and lows." "orb counts line"
has "$OUT" "   9:55 AM  LLY     \$1,255.00  range 1,240.23 to 1,254.99  +0.00% over" "LLY line with commas"
eq "$(echo "$OUT" | tail -2 | head -1)" "Wrote trader/watchlist.json (mode orb, 42 hits)." "orb wrote"
eq "$(jq -c '[.created_by, .created_at, .mode, .as_of, .universe, .scanned, .source, (.hits | length)]' trader/watchlist.json)" '["trader-scanner","2026-10-19T14:15:00Z","orb","2026-10-19 10:15 ET",234,231,"yahoo",42]' "the orb watchlist"
run $P orb --offline "$WALL" --at 10:15 --watchlist trader/watchlist.json --min-price 0 --json --no-write
eq "$CODE" 0 "the hits read back"; eq "$(j '[.universe, .scanned, (.hits | length)]')" '[42,42,42]' "watchlist with --at"
run $P orb --offline "$WALL" --at 10:15 --symbols TSLA --watchlist trader/watchlist.json; eq "$CODE" 2 "--symbols and --watchlist exclude each other"
has "$ERR" "argument --watchlist: not allowed with argument --symbols" "the argparse message"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --min-price 0 --show 3 --no-write; has "$OUT" "  ... and 39 more in the watchlist (--show N for more)." "orb --show 3"

# ---- plain messages and exit codes
rm -f wl.json
run $P orb --offline "$WALL" --now "2026-10-17 10:15" --out wl.json; eq "$CODE" 1 "weekend exits 1"
has "$ERR" "No session on Sat 2026-10-17 (weekend)" "weekend message"; hasnt "$ERR" "Traceback" "no traceback"
[ -e wl.json ] && bad "nothing is written on an error" || ok
run $P orb --offline "$WALL" --now "2026-10-19 09:44" --out wl.json; has "$ERR" "after 9:50 AM" "too early"
echo '{"no_entries_after": "10:00"}' > trader/config.json
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --out wl.json; eq "$CODE" 1 "config cut"; has "$ERR" "expired" "no_entries_after from the config"
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --out wl.json --until 15:30 --no-write --json; eq "$CODE" 0 "--until wins over the config"
rm trader/config.json
echo '{"no_entries_after": "10:00"}' > cfg.json
run $P orb --offline "$WALL" --now "2026-10-19 10:15" --config cfg.json; has "$ERR" "expired" "--config"
echo '{' > badcfg.json; run $P orb --offline "$WALL" --now "2026-10-19 10:15" --config badcfg.json; has "$ERR" "scan: badcfg.json is not valid JSON: " "a broken config"
run $P orb --offline "$WALL"; eq "$ERR" 'scan: offline replay: give --at HH:MM or --now "YYYY-MM-DD HH:MM"' "offline wants a time"
run $P orb --offline "$WALL" --now "2026-10-19 10:10" --symbols TSLA --out wl.json
eq "$CODE" 1 "no breakout yet exits 1"; has "$OUT" "No chart closed above its opening-range high by 10:10 AM." "no breakout yet"
eq "$(jq -c '.hits' wl.json)" '[]' "the empty list is written"
run $P daily --universe "$TMP/nope.txt"; eq "$CODE" 1 "no universe exits 1"
eq "$ERR" "scan: no $TMP/nope.txt yet: run \`scan universe\` first, or pass --symbols" "run scan universe"
run $P daily --offline "$TMP/nothing-here"; eq "$ERR" "scan: no offline file or folder at $TMP/nothing-here" "no offline file"
echo '{}' > empty.json; run $P daily --offline empty.json; eq "$CODE" 1 "empty exits 1"; eq "$ERR" "scan: no symbols to scan" "no symbols"
run $P; eq "$CODE" 1 "no command exits 1"; has "$OUT" "usage: scan [-h] {universe,daily,orb} ..." "usage"
run $P orb --nope; eq "$CODE" 2 "a bad flag exits 2"

# ---- an offline folder of v8 files
mkdir -p charts; for s in TSLA AAPL KO SQ; do gzip -dc "$WALL" | jq -c --arg s "$s" '.[$s]' > "charts/$s.json"; done
run $P orb --offline charts --now "2026-10-19 10:15" --no-write --json
eq "$CODE" 0 "folder exits 0"; eq "$(j '[.universe, .scanned, .failed, [.hits[].symbol], .offline]')" '[4,3,1,["AAPL","TSLA"],"charts"]' "a folder of v8 files"
run $P orb --offline charts/ --now "2026-10-19 10:15" --no-write --json; eq "$(j '.offline')" '"charts"' "a trailing slash"
gzip -c charts/TSLA.json > charts/MSFT.json.gz
run $P orb --offline charts --now "2026-10-19 10:15" --no-write --json; eq "$(j '.universe')" 5 "a .json.gz file is named by its stem"

# ---- symbol lists
printf '# my list\nTSLA|Tesla, Inc. - Common Stock\nko\n\nTSLA\n' > list.txt
run $P daily --offline "$DAILY" --watchlist list.txt --now "2026-10-19 09:00" --json --no-write --min-volume 0
eq "$(j '[.universe, .scanned]')" '[2,2]' "a text watchlist (deduped)"
echo '{"hits": [{"symbol": "tsla"}, {"symbol": "KO"}]}' > wl2.json
run $P daily --offline "$DAILY" --watchlist wl2.json --now "2026-10-19 09:00" --json --no-write --min-volume 0; eq "$(j '[.universe, .scanned]')" '[2,2]' "a JSON watchlist"
echo '["ko", null, {"symbol": null}, "tsla"]' > wl3.json
run $P daily --offline "$DAILY" --watchlist wl3.json --now "2026-10-19 09:00" --json --no-write --min-volume 0; eq "$(j '[.universe, .scanned, .failed]')" '[3,2,1]' "a JSON list; a null symbol is NONE"
run $P daily --offline "$DAILY" --watchlist nofile.txt; eq "$ERR" "scan: no file at nofile.txt" "a missing watchlist"
run $P daily --offline "$DAILY" --limit 3 --now "2026-10-19 09:00" --json --no-write --min-volume 0; eq "$(j '.universe')" 3 "--limit 3"
run $P daily --offline "$DAILY" --symbols "ko, tsla,,KO" --now "2026-10-19 09:00" --json --no-write --min-volume 0; eq "$(j '.universe')" 2 "--symbols split on commas and spaces"
run $P daily --offline "$DAILY" --now "2026-10-19 09:00" --json --no-write --min-volume 0 --min-price 0
eq "$CODE" 0 "daily json exits 0"; eq "$(j '.hits | length')" 15 "15 hits"
done_ "daytrade-scanner/scan"

#!/usr/bin/env bash
# daytrade-entry/entry: the assertions of day-trader-skill/tests/test_entry.py, run against the C++ tool. evaluate(...) unit
# calls become `check --offline FILE --now ... --json` runs; the test's fixture edits (no_breakout, shift_to, the empty chart,
# the lagging cut) are rebuilt with jq. The Watch class drives watch_loop with fake providers, which a CLI can't reach: here
# the same loop runs through `watch --offline --now` (the simulated clock), and the network fakes are not ported.
source "$(dirname "$0")/lib.sh"
E="$FIX/daytrade-entry"; TSLA="$E/TSLA-2026-10-19-5m.json"; DEMO="$E/DEMO-1m-synthetic.json"; OPEN_TS=1792416600
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
j() { echo "$OUT" | jq -c "$1"; }
# at NOW [FILE] [args]: check TSLA as of NOW (New York) with --json
at() { local now=$1 f=${2:-$TSLA}; shift; [ $# -gt 0 ] && shift; run daytrade-entry/entry check TSLA --offline "$f" --now "$now" --config "$TMP/none" --json "$@"; }
NB="$TMP/nb.json"   # no_breakout: every bar after the range stays under the 362.18 high
jq 'def cap(v): [range(0; length) as $i | if $i >= 3 then ([.[$i], v] | min) else .[$i] end];
    .chart.result[0].indicators.quote[0] |= (.close |= cap(362.00) | .high |= cap(362.10) | .open |= cap(362.00))' "$TSLA" > "$NB"
# shift SRC DAY EDT OUT: the same session moved to another date (9:30 New York is 13:30 UTC in EDT, 14:30 in EST)
shift_to() { local d; d=$(( $(date -u -d "$2 $([ "$3" = 1 ] && echo 13:30 || echo 14:30)" +%s) - OPEN_TS ))
  jq --argjson d "$d" '.chart.result[0] |= (.timestamp |= map(. + $d) | .meta.regularMarketTime += $d)' "$1" > "$4"; }

# ---- the reel
at "2026-10-19 10:15"; eq "$CODE" 0 "triggered exits 0"
eq "$(j '[.status, .opening_range.high, .opening_range.low, .opening_range.bars, .level, .prev_close]')" '["triggered",362.18,354.47,3,362.18,354.72]' "triggers at 10:15"
eq "$(j '.trigger | [.time, .bar_start, .bar_index, .price, .close, .pct_over, .close_time]')" '["10:15 AM","10:10",8,364.18,364.1759948730469,0.55,"10:15"]' "the 10:10 bar"
eq "$(j 'has("_bars")')" false "no internal bars in the JSON"
eq "$(j 'keys_unsorted')" '["symbol","setup","side","date","interval","or_minutes","now","status","phase","reason","opening_range","level","trigger","last","forming","min_volume_x","prev_close","calendar_known","data_time","opens","closes","early_close","no_entries_before","no_entries_after","bars_closed","minutes_since"]' "result keys in order"
at "2026-10-19 10:12"; eq "$CODE" 1 "waiting exits 1"
eq "$(j '[.status, .trigger, .last.bar_start, .last.close, .forming.bar_start]')" '["waiting",null,"10:05",361.68,"10:10"]' "at 10:12 the 10:10 bar is still forming"
has "$(j .reason)" '$0.50 under the range high $362.18' "under the high"
at "2026-10-19 10:14:59"; eq "$(j .status)" '"waiting"' "10:14:59 still waits"
at "2026-10-19 09:20"; eq "$(j '[.status, .phase, .reason]')" '["waiting","pre-open","the NYSE opens at 9:30 and the 15-minute range is set at 9:45"]' "pre-open"
at "2026-10-19 09:44"; eq "$(j '[.status, .phase, .opening_range.bars, .opening_range.complete, .reason]')" '["waiting","forming",2,false,"2 of 3 range bars closed; the range is set at 9:45"]' "range forming"
at "2026-10-19 09:46"; eq "$(j '[.status, .phase, .opening_range.bars]')" '["waiting","open",3]' "range set"
# the live tick and the in-progress bar are dropped
run daytrade-entry/entry check TSLA --offline "$TSLA" --config "$TMP/none" --json
eq "$(j '[.now, .last.bar_start, .forming.bar_start, .bars_closed, .data_time]')" '["2026-10-19 10:49:45","10:40","10:45",15,"2026-10-19 10:49:45"]' "read as of the file's own time"
at "2026-10-19 10:50"; eq "$(j '[.last.bar_start, .bars_closed]')" '["10:40",15]' "10:50: closed by the clock only"
has "$(j .lag_note)" "Yahoo's last trade is 10:49:45" "lag note"
at "2026-10-19 10:51"; eq "$(j '[.last.bar_start, .bars_closed]')" '["10:45",16]' "a minute later it counts; the tick never becomes a bar"
# after the close it stays triggered and says it is late
at "2026-10-19 16:05"; eq "$(j '[.status, .phase, .trigger.time, .minutes_since]')" '["triggered","after-close","10:15 AM",350]' "after the close"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 16:05" --config "$TMP/none"
has "$OUT" "minutes ago" "late"; hasnt "$OUT" "This is the moment" "not the moment"; has "$OUT" "Nothing was placed." "nothing placed"
has "$OUT" ", data to 10:49:45" "data time in the header"

# ---- rules
at "2026-10-19 10:49" "" --min-volume-x 1; eq "$(j '[.opening_range.avg_volume, .trigger.time, .trigger.bar_index, .trigger.price, .trigger.volume_x]')" '[1658945,"10:35 AM",12,365.75,7.98]' "volume confirmation moves the trigger"
at "2026-10-19 10:16" "" --min-volume-x 1; eq "$(j .status)" '"waiting"' "thin trigger bar waits"; has "$(j .reason)" "0.60x the range's volume, under your 1.00x" "thin reason"
at "2026-10-19 10:30" "" --setup level --level 365.5; eq "$(j '.trigger | [.time, .bar_index, .price]')" '["10:25 AM",10,365.75]' "level setup"
at "2026-10-19 10:30" "" --setup level --level 350; eq "$(j '.trigger | [.time, .bar_index]')" '["9:45 AM",2]' "a level under the open waits for no_entries_before"
at "2026-10-19 10:30" "" --setup level; eq "$CODE" 1 "level needs a level"; has "$ERR" "needs --level" "level message"
at "2026-10-19 10:49" "$NB"; eq "$(j .status)" '"waiting"' "no breakout waits"
at "2026-10-19 15:29" "$NB"; eq "$(j .status)" '"waiting"' "still waiting at 15:29"
at "2026-10-19 15:30" "$NB"; eq "$(j '[.status, .phase]')" '["expired","after-cutoff"]' "expires at the cutoff"; has "$(j .reason)" "by 15:30" "expiry reason"
eq "$(j .reason)" '"no close above the range high $362.18 by 15:30, the last time for a new entry"' "expiry text"
at "2026-10-19 16:30" "$NB"; eq "$(j .phase)" '"after-close"' "after the close"
run daytrade-entry/entry check TSLA --offline "$NB" --now "2026-10-19 15:30" --config "$TMP/none"; has "$OUT" "No entry on this setup today. Nothing was placed." "expired words"
# the bar closing at the cutoff still counts when Yahoo lags
jq --argjson o "$OPEN_TS" '.chart.result[0] |= ( [.timestamp | to_entries[] | select(.value <= $o + 14 * 300) | .key] as $k
    | .timestamp |= [.[$k[]]] | .indicators.quote[0] |= with_entries(.value |= [.[$k[]]]) | .meta.regularMarketTime = $o + 74 * 60 + 40)' "$NB" > "$TMP/lag.json"
echo '{"no_entries_before": "09:45", "no_entries_after": "10:45"}' > "$TMP/c1045.json"
run daytrade-entry/entry check TSLA --offline "$TMP/lag.json" --now "2026-10-19 10:45:10" --config "$TMP/c1045.json" --json
eq "$(j .status)" '"waiting"' "the 10:40 bar closed by the clock only"; has "$(j .lag_note)" "Yahoo's last trade is 10:44:40" "lag note at the cutoff"
run daytrade-entry/entry check TSLA --offline "$TMP/lag.json" --now "2026-10-19 10:45:10" --config "$TMP/c1045.json"
has "$OUT" "Yahoo's last trade is 10:44:40, before the 10:40 bar's close: it counts once Yahoo catches up (a minute at most)." "lag line"
run daytrade-entry/entry check TSLA --offline "$TMP/lag.json" --now "2026-10-19 10:46" --config "$TMP/c1045.json" --json; eq "$(j .status)" '"expired"' "then it expires"
echo '{"no_entries_before": "09:45", "no_entries_after": "10:10"}' > "$TMP/c.json"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:15" --config "$TMP/c.json" --json; eq "$(j .status)" '"expired"' "config cutoff"
echo '{"no_entries_before": "10:20", "no_entries_after": "15:30"}' > "$TMP/c.json"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:30" --config "$TMP/c.json" --json; eq "$(j .trigger.time)" '"10:20 AM"' "the 10:15 bar, first to close at or after 10:20"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:17" --config "$TMP/c.json"; has "$OUT" "but no entries before 10:20 (config)" "too early reason"
at "2026-10-19 10:15" "" --or-minutes 7; eq "$CODE" 1 "or-minutes 7"; eq "$ERR" "entry: --or-minutes must be a whole number of 5m bars" "or-minutes message"
at "2026-10-19 10:15" "" --interval 15m; eq "$CODE" 2 "interval choices"
at "tomorrow"; eq "$ERR" 'entry: --now wants a New York time like "2026-10-19 10:15" (or ...Z for UTC)' "bad --now"
echo '{"no_entries_after": "25:00"}' > "$TMP/c.json"; run daytrade-entry/entry check TSLA --offline "$TSLA" --config "$TMP/c.json"; eq "$ERR" "entry: no_entries_after wants a New York time like 15:30, not '25:00'" "bad config time"
for now in "2026-10-19 10:15" "2026-10-19T14:15:00Z" "2026-10-19 10:15 ET"; do
  run daytrade-entry/entry check TSLA --offline "$TSLA" --now "$now" --config "$TMP/none" --write "$TMP/w.json"; eq "$(jq -c .checked_at "$TMP/w.json")" '"2026-10-19T14:15:00Z"' "parse_now $now"
done
at "2026-10-19 10:49" "" --or-minutes 30; eq "$(j '[.opening_range.bars, .opening_range.high, .trigger.time]')" '[6,362.92,"10:15 AM"]' "a 30-minute range"

# ---- one-minute bars (synthetic)
run daytrade-entry/entry check DEMO --offline "$DEMO" --interval 1m --config "$TMP/none" --json
eq "$(j .now)" '"2026-10-19 10:06:30"' "demo as of its own time"
eq "$(j .opening_range)" '{"high":100.5,"low":99.0,"minutes":15,"bars":15,"of":15,"complete":true,"avg_volume":10667}' "demo range"
eq "$(j '.trigger | [.time, .bar_start, .bar_index, .price]')" '["9:59 AM","09:58",28,100.62]' "a wick and a close AT the high do not trigger"
run daytrade-entry/entry check DEMO --offline "$DEMO" --interval 1m --now "2026-10-19 09:58:59" --config "$TMP/none" --json
eq "$(j '[.status, .forming.bar_start, .last.close]')" '["waiting","09:58",100.5]' "the 9:57 bar closed AT the high"
run daytrade-entry/entry check DEMO --offline "$DEMO" --interval 1m --min-volume-x 2 --config "$TMP/none" --json
eq "$(j '[.trigger.time, .trigger.bar_index, .bars_closed, .forming.bar_start]')" '["10:02 AM",31,35,"10:06"]' "the null 9:49 bar and the 10:06:30 tick are dropped"
run daytrade-entry/entry check DEMO --offline "$DEMO"; eq "$CODE" 1 "file and interval must agree"; has "$ERR" "1m bars, not 5m" "granularity"
eq "$ERR" "entry: DEMO has 1m bars, not 5m: use --interval 1m" "granularity message"

# ---- calendar
for spec in "2026-12-07 0" "2026-11-02 0" "2026-10-30 1" "2026-03-09 1"; do
  read -r day edt <<< "$spec"; shift_to "$TSLA" "$day" "$edt" "$TMP/s.json"
  at "$day 10:15" "$TMP/s.json"; eq "$(j '[.status, .trigger.time, .trigger.bar_index]')" '["triggered","10:15 AM",8]' "DST $day"
  at "$day 10:12" "$TMP/s.json"; eq "$(j .status)" '"waiting"' "DST $day 10:12"
done
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-12-07 10:15" --config "$TMP/none" --write "$TMP/w.json"; eq "$(jq -c .checked_at "$TMP/w.json")" '"2026-12-07T15:15:00Z"' "Dec 7 10:15 New York is 15:15 UTC"
shift_to "$NB" 2026-11-27 0 "$TMP/half.json"
at "2026-11-27 12:29" "$TMP/half.json"; eq "$(j '[.status, .closes, .no_entries_after, .early_close]')" '["waiting","13:00","12:30","day after Thanksgiving"]' "half day moves the cutoff up"
at "2026-11-27 12:30" "$TMP/half.json"; eq "$(j .status)" '"expired"' "half-day expiry"; has "$(j .reason)" "early close" "early close reason"
shift_to "$TSLA" 2026-11-27 0 "$TMP/half2.json"
at "2026-11-27 10:15" "$TMP/half2.json"; eq "$(j .trigger.time)" '"10:15 AM"' "half-day trigger"
run daytrade-entry/entry check TSLA --offline "$TMP/half2.json" --now "2026-11-27 10:15" --config "$TMP/none"; has "$OUT" "Early close today at 1:00 p.m. (day after Thanksgiving): no new entries after 12:30." "early close line"
echo '{"no_entries_before": "09:45", "no_entries_after": "12:00"}' > "$TMP/c.json"
run daytrade-entry/entry check TSLA --offline "$TMP/half.json" --now "2026-11-27 11:00" --config "$TMP/c.json" --json; eq "$(j .no_entries_after)" '"12:00"' "a cutoff before the early close stays"
at "2026-10-17 10:30"; eq "$(j '[.status, .phase, .next_open, .reason]')" '["no-data","closed","2026-10-19 09:30","the NYSE is closed today (weekend)"]' "weekend"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-17 10:30" --config "$TMP/none"; has "$OUT" "MARKET CLOSED  the NYSE is closed today (weekend)." "closed label"; has "$OUT" "Next open: 2026-10-19 09:30 New York." "next open line"
at "2026-11-26 10:30"; has "$(j .reason)" "Thanksgiving" "holiday"
jq '.chart.result[0] |= (del(.timestamp) | .indicators = {quote: [{}]})' "$TSLA" > "$TMP/empty.json"
at "2026-10-19 10:15" "$TMP/empty.json"; eq "$(j '[.status, .phase]')" '["no-data","open"]' "empty data"
at "2026-10-20 10:15"; eq "$(j '[.status, .reason]')" '["no-data","Yahoo sent no bars for the opening range"]' "yesterday's bars only"
at "2026-10-20 10:15" "" --setup level --level 300; eq "$(j '[.status, .reason]')" '["no-data","Yahoo sent no bars for 2026-10-20 yet"]' "a level setup with no bars today"
at "2028-03-01 10:30"; eq "$(j .calendar_known)" false "calendar years"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2028-03-01 10:30" --config "$TMP/none"; has "$OUT" "Holiday list covers 2026 and 2027 only: check nyse.com for this date." "calendar line"
echo '{"chart": {"result": null, "error": {"code": "Not Found", "description": "No data found, symbol may be delisted"}}}' > "$TMP/ZZZZ.json"
run daytrade-entry/entry check ZZZZ --offline "$TMP/ZZZZ.json"; eq "$ERR" "entry: Yahoo has no data for ZZZZ: No data found, symbol may be delisted" "yahoo error JSON"
echo '<html>' > "$TMP/html.json"; run daytrade-entry/entry check TSLA --offline "$TMP/html.json"; eq "$ERR" "entry: Yahoo returned something unexpected for TSLA" "html"
run daytrade-entry/entry check TSLA --now "2026-10-19 10:15"; eq "$CODE" 1 "no network in the suite"; has "$ERR" "Yahoo did not answer (could not connect" "offline network error"; hasnt "$ERR" "Traceback" "plain error"

# ---- CLI
mkdir -p "$TMP/c1" && cd "$TMP/c1" || exit 1
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:15" --write; eq "$CODE" 0 "check --write"
has "$OUT" "TRIGGERED 10:15 AM at \$364.18" "trigger line"; has "$OUT" "/trader-exit" "next step"
eq "$(echo "$OUT" | sed -n 1p)" "TSLA  Mon 2026-10-19 10:15 ET  opening-range breakout (15-minute range) on 5-minute bars (Yahoo, free, can lag)" "header"
eq "$(echo "$OUT" | sed -n 2p)" "Opening range 9:30-9:45: high \$362.18, low \$354.47 (3 of 3 bars)." "range line"
eq "$(echo "$OUT" | sed -n 3p)" "TRIGGERED 10:15 AM at \$364.18: the 10:10 bar (bar 8) closed above the range high \$362.18 (+0.55%)." "triggered line"
eq "$(echo "$OUT" | tail -1)" "Wrote trader/entry.json (status triggered)." "wrote line"
eq "$(jq -c 'del(.reason)' trader/entry.json)" "$(jq -c . "$FIX/daytrade-exit/entry-reel.json")" "trader/entry.json is the contract shape"
eq "$(jq -c .reason trader/entry.json)" '"the 10:10 bar closed above the range high"' "plus the reason"
run daytrade-exit/exit plan --now "2026-10-19 10:16" --stop or-mid --target 1R --json; eq "$(j '[.stop, .target, .entry_bar]')" '[358.32,370.04,8]' "entry.json feeds the exit planner"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:12" --json; eq "$CODE" 1 "waiting json"; eq "$(j '[.status, has("_bars")]')" '["waiting",false]' "waiting JSON"
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:12" --write e.json; eq "$(jq -c .status e.json)" '"waiting"' "e.json waiting"; has "$OUT" "still forming" "still forming"
has "$OUT" "The 10:10 bar is still forming (\$364.18 so far, closes 10:15 AM); it does not count until it closes." "forming line"
eq "$(echo "$OUT" | sed -n 3p)" "WAITING  last close \$361.68 (10:05 bar), \$0.50 under the range high \$362.18 (-0.14%)." "waiting line"
mkdir charts && gzip -c "$TSLA" > charts/TSLA.json.gz
run daytrade-entry/entry check tsla --offline charts --now "2026-10-19 10:15"; eq "$CODE" 0 "gz in a folder"
run daytrade-entry/entry check NVDA --offline charts; eq "$ERR" "entry: no offline file for NVDA in charts (want NVDA.json)" "folder without the ticker"
run daytrade-entry/entry check NVDA --offline "$TSLA"; eq "$CODE" 1 "mismatch"; has "$ERR" "holds TSLA, not NVDA" "mismatch message"
cp "$TSLA" charts/BRK-B.json; run daytrade-entry/entry check brk.b --offline charts; eq "$ERR" "entry: the chart file holds TSLA, not BRK.B" "BRK.B finds BRK-B.json"
run daytrade-entry/entry check TSLA --offline "$TSLA" --side short; eq "$CODE" 1 "short"; has "$ERR" "v2 plans longs only" "longs only"
run daytrade-entry/entry check TSLA --offline "$TSLA" --level 300; has "$ERR" "--level is for --setup level" "orb with a level"
run daytrade-entry/entry check TSLA --offline "$TSLA" --setup level; has "$ERR" "needs --level" "level without a price"
run daytrade-entry/entry check TSLA --offline "$TSLA" --min-volume-x 0; eq "$ERR" "entry: --min-volume-x must be above 0" "volume x"
run daytrade-entry/entry check TSLA --offline "$TSLA" --setup level --level -1; eq "$ERR" "entry: --level must be a price above 0" "level above 0"
mkdir -p trader && echo '{"no_entries_after": "10:10"}' > trader/config.json
run daytrade-entry/entry check TSLA --offline "$TSLA" --now "2026-10-19 10:15"; eq "$CODE" 1 "config is read"; has "$OUT" "EXPIRED" "expired by the config"
echo '{nope' > trader/config.json; run daytrade-entry/entry check TSLA --offline "$TSLA"; has "$ERR" "not valid JSON" "bad config"
rm trader/config.json trader/entry.json
# watch replays the file on a simulated clock
run daytrade-entry/entry watch TSLA --offline "$TSLA" --now "2026-10-19 09:50" --every 30; eq "$CODE" 0 "watch triggers"
has "$OUT" "10:10 bar  closed \$364.18  TRIGGERED" "watch trigger line"; eq "$(jq -c .bar_index trader/entry.json)" 8 "watch writes the entry"
eq "$(echo "$OUT" | grep -c TRIGGERED)" 1 "TRIGGERED once"; eq "$(echo "$OUT" | tail -1)" "Wrote trader/entry.json." "watch wrote line"
run daytrade-entry/entry watch TSLA --offline "$TSLA" --now "2026-10-19 09:50" --until 10:00 --out x.json; eq "$CODE" 1 "--until stops"; [ ! -e x.json ] && ok || bad "--until writes nothing"
has "$(echo "$OUT" | tail -1)" "Stopped at --until 10:00 without a trigger." "until line"
run daytrade-entry/entry watch TSLA --offline "$TSLA" --now "2026-10-19 09:00" --every 30 --out y.json; eq "$CODE" 0 "from before the open"
has "$(echo "$OUT" | sed -n 1p)" "not open yet" "pre-open line"; eq "$(echo "$OUT" | sed -n 1p)" "09:00 ET  not open yet: watching from the first bar's close at 9:35." "pre-open text"
has "$OUT" " 9:40 bar  closed \$360.39  opening range set: high \$362.18, low \$354.47" "range set line"
eq "$(echo "$OUT" | grep -c ' bar  closed ')" 9 "9:30 ... 10:10, one line each"
has "$OUT" " 9:30 bar  closed \$359.18  range so far \$354.47 to \$362.18" "range so far"; has "$OUT" "10:05 bar  closed \$361.68  \$0.50 under \$362.18" "under line"
run daytrade-entry/entry watch TSLA --offline "$NB" --now "2026-10-19 15:00" --every 300 --out z.json; eq "$CODE" 1 "watch expires"; [ ! -e z.json ] && ok || bad "expiry writes nothing"
eq "$(echo "$OUT" | tail -1)" "EXPIRED  no close above the range high \$362.18 by 15:30, the last time for a new entry." "expired line"
run daytrade-entry/entry watch TSLA --offline "$NB" --now "2026-10-19 10:30" --every 300 --until 11:00 --out z.json; eq "$CODE" 1 "until"; has "$(echo "$OUT" | tail -1)" "--until 11:00" "until 11:00"
run daytrade-entry/entry watch TSLA --offline "$TSLA" --now "2026-10-17 10:00"; eq "$CODE" 1 "closed day"; eq "$OUT" "2026-10-17: market closed today (weekend). Nothing to watch." "weekend watch"
run daytrade-entry/entry watch TSLA --offline "$TMP/missing.json" --now "2026-10-19 10:00" --every 30; eq "$CODE" 1 "repeated errors"
eq "$(echo "$OUT" | tail -1)" "Stopping after 5 failed reads in a row." "stops after 5"; eq "$(echo "$OUT" | grep -c 'could not read bars')" 5 "five failed reads"
has "$OUT" "10:00:00 ET  could not read bars: no offline file $TMP/missing.json" "error line"
run daytrade-entry/entry watch TSLA --offline "$TSLA" --every 0; eq "$ERR" "entry: --every must be above 0 seconds" "every"
# watchlist
mkdir w && cp "$TSLA" w/TSLA.json && cp "$DEMO" w/DEMO.json
echo '{"created_by": "trader-scanner", "hits": [{"symbol": "TSLA", "setups": ["orb"], "or_high": 362.18, "or_low": 354.47}, {"symbol": "ZZZZ", "setups": ["orb"]}, {"symbol": "DEMO", "setups": ["orb"]}, "junk", {"setups": []}]}' > trader/watchlist.json
run daytrade-entry/entry watchlist --offline w --now "2026-10-19 10:15"; eq "$CODE" 0 "watchlist"
eq "$(echo "$OUT" | sed -n 1p)" "SYMBOL  STATUS     TIME      PRICE     LEVEL      LAST       VS LEVEL" "watchlist header"
has "$(echo "$OUT" | sed -n 2p)" "TSLA    triggered  10:15 AM  \$364.18   \$362.18" "TSLA row"
eq "$(echo "$OUT" | sed -n 2p)" "TSLA    triggered  10:15 AM  \$364.18   \$362.18    \$364.18    +0.55%" "TSLA row in full"
has "$(echo "$OUT" | sed -n 3p)" "ZZZZ    error: no offline file" "ZZZZ row"; has "$(echo "$OUT" | sed -n 4p)" "DEMO    error: DEMO has 1m bars" "DEMO row"
has "$OUT" "1 of 3 triggered" "count"; has "$OUT" "entry check <TICKER> --write" "next"
run daytrade-entry/entry watchlist --offline w --now "2026-10-19 10:12" --json; eq "$CODE" 1 "none triggered"; eq "$(j '.[0].result.status')" '"waiting"' "json watchlist"
eq "$(j '[.[] | [.symbol, (.result != null), (.error != null)]]')" '[["TSLA",true,false],["ZZZZ",false,true],["DEMO",false,true]]' "json rows"
run daytrade-entry/entry watchlist --offline w --now "2026-10-19 09:20"; has "$(echo "$OUT" | sed -n 2p)" "TSLA    not open" "pre-open row"
run daytrade-entry/entry watchlist --offline w --now "2026-10-19 10:30" --setup level --json; eq "$(j '.[0].error')" '"needs a level: --level PRICE or a \"level\" in the watchlist"' "level without a level"
run daytrade-entry/entry watchlist --watchlist missing.json; has "$ERR" "run /trader-scanner first" "missing watchlist"
run daytrade-entry/entry watchlist --offline "$TSLA"; eq "$ERR" "entry: watchlist --offline wants a folder of <TICKER>.json files" "watchlist folder"
echo '{"hits": []}' > trader/watchlist.json; run daytrade-entry/entry watchlist; eq "$ERR" "entry: no hits in trader/watchlist.json: the scanner found nothing to watch" "no hits"
run daytrade-entry/entry; eq "$CODE" 1 "no command"; has "$OUT" "usage: entry [-h] {check,watch,watchlist} ..." "usage"
done_ "daytrade-entry/entry"

#!/usr/bin/env bash
# daytrade-timemachine/timemachine: the assertions of day-trader-skill/tests/test_timemachine.py, run against the C++ tool.
# The reel's fingerprints are sha256(json.dumps(x))[:16]: jq -c with Python's ", " and ": " separators put back gives the same
# text (the tool prints Python float reprs and jq 1.7 keeps number literals), so the bands, the 160 sample paths and the
# history trades are checked bit for bit. The fixture edits (keep_bars, a hole, the half day, the Nov 27 early close) are
# rebuilt with jq; every fixture day is in daylight time (UTC-4). The Network class fakes urllib, which a CLI can't reach, and
# the verdict-threshold unit cases on hand-made mc dicts are covered through the fixture's 1R and 2R runs instead.
source "$(dirname "$0")/lib.sh"
TSLA="$FIX/daytrade-timemachine/TSLA-60d5m.json.gz"; NOW="2026-10-19 10:50"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
P=daytrade-timemachine/timemachine
j() { echo "$OUT" | jq -c "$1"; }
fp() { sed 's/,/, /g; s/":/": /g' | tr -d '\n' | sha256sum | cut -c1-16; }   # sha(json.dumps(x))[:16]
ets() { TZ=America/New_York date -d "$1" +%s; }
gzip -dc "$TSLA" > tsla.json
cat > plan.json <<'X'
{"created_by": "trader-exit", "symbol": "TSLA", "side": "buy", "date": "2026-10-19", "setup": "orb", "entry": 364.18, "entry_time": "10:15 AM", "entry_bar": 8, "interval": "5m", "opening_range": {"high": 362.18, "low": 354.47, "minutes": 15}, "stop": 358.32, "stop_rule": "or_mid", "stop_label": "opening-range midpoint", "target": 370.04, "target_r": 1.0, "target_label": "1R (one times the risk)", "risk_per_share": 5.86, "reward_risk": 1.0, "flat_by": "15:55"}
X
# plan JQ_EDIT -> plan-N.json (the reel's plan, changed); echoes the file name
N=0; plan() { N=$((N+1)); jq "$1" plan.json > "plan-$N.json"; echo "plan-$N.json"; }
# m PLANFILE [args]: run --json on the fixture at 10:50
m() { local f=$1; shift; run $P run --plan "$f" --offline "$TSLA" --now "$NOW" --json "$@"; }
# edit JQ_ON_RESULT -> edited-N.json (the fixture's chart result changed); edt(ts) = New York wall clock in daylight time
E=0; edit() { E=$((E+1)); jq -c 'def day: (. - 14400 | strftime("%Y-%m-%d")); def hour: ((. - 14400) % 86400 / 3600 | floor);
  def keep(f): (.chart.result[0]) |= ([.timestamp | to_entries[] | select(.value | f) | .key] as $ix
    | .timestamp |= [$ix[] as $i | .[$i]] | .indicators.quote[0] |= with_entries(.value |= [$ix[] as $i | .[$i]]));
  '"$1" tsla.json > "edited-$E.json"; echo "edited-$E.json"; }

# ---- the reel
run $P run --plan plan.json --offline "$TSLA" --now "$NOW" --write "$TMP/trader/timemachine.json"
eq "$CODE" 0 "the reel exits 0"
DOC=$(cat trader/timemachine.json)
d() { echo "$DOC" | jq -c "$1"; }
eq "$(d '.mc | [.replays, .seed, .window, .until, .pool_sessions, .bars_left, .pool_from, .pool_to]')" '[10000,19,2,"16:00",59,69,"2026-07-27","2026-10-16"]' "the reel's setup"
eq "$(d '.mc | [.p_target, .p_stop, .p_neither, .p_green, .median_close, .p5_close, .p95_close, .avg_R, .r_at_target]')" '[0.2938,0.2548,0.4514,0.5168,364.36,355.54,373.54,0.034,1.0]' "the reel odds"
eq "$(d '.mc.bands' | fp)" b9b7c006a733c68b "bands bit for bit with the reel's sim.py"
eq "$(d '.mc.sample_paths' | fp)" 90f313ef40c576a0 "sample paths bit for bit"
eq "$(d '.mc.sample_paths | length')" 160 "160 sample paths"
eq "$(d '.mc.hist')" '{"lo":351.75,"hi":377.39,"bins":[107,109,128,199,252,331,465,571,689,694,757,811,761,732,686,558,503,452,327,244,160,125,102,78]}' "the histogram"
eq "$(d '.mc.bands[-1]')" '[355.42,360.91,364.46,368.12,373.65]' "the last band"
eq "$(d '.history | [.sessions, .triggered, .target, .stop, .close, .green, .avg_R]')" '[59,33,13,15,5,15,-0.062]' "the history replay"
eq "$(d '.history.rule')" '{"setup":"orb","or_minutes":15,"stop_rule":"or_mid","target_r":1.0}' "the history rule"
eq "$(d '[.history.trades[] | {date, entry, outcome, R}]' | fp)" 0058bef72b0dcdee "history trades bit for bit"
eq "$(d '.history.trades[0] | [.date, .entry, .stop, .target, .outcome]')" '["2026-07-28",307.97,304.89,311.05,"target"]' "the first trade"
eq "$(d '.verdict')" '"Close to a coin flip: target first 29.4 % vs stop first 25.5 %."' "the verdict"
eq "$(d '.verdict_notes')" '["45.1 % of the replays hit neither by 4:00 PM; the price at 4:00 PM decides those."]' "the verdict note"
eq "$(d '.mc.bands[0]')" '[364.18,364.18,364.18,364.18,364.18]' "bands start at the entry"
eq "$(d '.mc | [(.bands | length), .band_times[0], .band_times[-1], (.band_times | length)]')" '[70,"10:15","16:00",70]' "70 band times"
eq "$(d '[.mc.sample_paths[] | .[0] == 364.18 and length == 70] | all')" true "every path starts at the entry"
eq "$(d '.plan | [.symbol, .entry, .stop, .target, .entry_bar, .entry_time, .risk_per_share, .target_r, .flat_by, .stop_rule]')" '["TSLA",364.18,358.32,370.04,8,"10:15 AM",5.86,1.0,"15:55","or_mid"]' "the plan carried through"
eq "$(d '.plan.opening_range')" '{"high":362.18,"low":354.47,"minutes":15}' "the opening range keeps its keys"
eq "$(d '.plan | keys_unsorted')" '["symbol","side","date","setup","entry","entry_time","entry_bar","stop","target","risk_per_share","target_r","opening_range","stop_rule","interval","flat_by","from"]' "plan keys in order"
eq "$(d '.warnings')" '[]' "no warnings"
eq "$(d 'keys_unsorted')" '["created_by","created_at","source","data","plan","mc","history","verdict","verdict_notes","warnings","note"]' "document keys in order"
eq "$(d '[.created_by, .created_at, .source]')" '["trader-timemachine","2026-10-19T14:50:00Z","offline"]' "the header fields"
eq "$(d '.plan.from')" '"plan.json"' "from = the plan path"
eq "$(d '.mc | keys_unsorted')" '["replays","seed","window","bars_left","p_target","p_stop","p_neither","p_green","median_close","p5_close","p95_close","avg_R","r_at_target","band_paths","bands","sample_paths","hist","until","pool_sessions","pool_from","pool_to","band_times"]' "mc keys in order"
eq "$(head -2 trader/timemachine.json | tail -1)" ' "created_by": "trader-timemachine",' "written with indent 1"
# the text report
for line in "TSLA buy, Monday 2026-10-19: entry 364.18 at 10:15 AM (bar 8), stop 358.32, target 370.04 (1R, risk 5.86 a share)." \
            "10,000 replays of the rest of the session, 10:15 AM to 4:00 PM (69 five-minute bars), seed 19." "59 full sessions (2026-07-27 to 2026-10-16)" \
            "  target first   29.4 %" "  stop first     25.5 %" "  neither        45.1 %   still open at 4:00 PM" \
            "  green          51.7 %   target first, or above the entry at 4:00 PM" "  Price at 4:00 PM: median 364.36, 90 % of replays between 355.54 and 373.54." \
            "  Average R: +0.034 (R = the 5.86 risk: +1R at the target, -1R at the stop)." \
            "History: the same rule (15-min opening range, stop at the opening-range midpoint, 1R target) on each of those 59 real sessions fired 33 times: 13 target, 15 stop, 5 neither. Average R -0.062." \
            $'Close to a coin flip: target first 29.4 % vs stop first 25.5 %.\n45.1 % of the replays hit neither by 4:00 PM; the price at 4:00 PM decides those.' \
            "not a prediction" "Repeat this run with --seed 19." "Wrote $TMP/trader/timemachine.json." "Next: /trader-budget sizes it to your budget and your max loss per trade."; do
  has "$OUT" "$line" "report: ${line:0:40}"; done
for word in "profit" "P&L" "guarantee" "real time" "tick by tick"; do hasnt "$OUT" "$word" "no \"$word\""; done

# ---- seeds
m plan.json --replays 2000 --seed 5; A=$(j '.mc'); m plan.json --replays 2000 --seed 5; eq "$(j '.mc')" "$A" "same seed, same numbers"
m plan.json --seed 7
eq "$(j '.mc | [.p_target, .p_stop, .median_close] != [0.2938, 0.2548, 364.36]')" true "another seed, other numbers"
eq "$(j '.mc | (.p_target + .p_stop + .p_neither - 1 | fabs) < 1e-6')" true "the shares add up"
eq "$(j '.mc.bands' | fp | grep -c b9b7c006a733c68b)" 0 "other bands"
m plan.json --replays 500 --window 0; eq "$(j '.mc | [.replays, .window]')" '[500,0]' "window 0"; W0=$(j '.mc.bands')
m plan.json --replays 500; [ "$(j '.mc.bands')" != "$W0" ] && ok || bad "window 0 draws other bands"
m plan.json --replays 0; eq "$ERR" "timemachine: --replays must be between 1 and 1,000,000" "replays 0"
m plan.json --window 11; eq "$ERR" "timemachine: --window must be between 0 and 10 bars" "window 11"
m plan.json --replays 1 --seed -19; eq "$CODE" 0 "a negative seed (abs, like Python)"
m plan.json --replays 300 --seed 19; S19=$(j '.mc.p_target'); m plan.json --replays 300 --seed -19; eq "$(j '.mc.p_target')" "$S19" "seed -19 is seed 19"
m plan.json --replays 300 --seed 4294967317; eq "$CODE" 0 "a seed over 32 bits"

# ---- bars
m plan.json --replays 200
eq "$(j '.data.dropped')" '{"no_price":0,"off_grid":1,"not_complete":1,"outside_hours":0}' "the live tick and the 10:45 bar are dropped"
eq "$(j '.data.last_bar')" '"2026-10-19 10:40 ET"' "the last complete bar"
run $P run --plan plan.json --offline "$TSLA" --json --replays 200; eq "$(j '.data.last_bar')" '"2026-10-19 10:40 ET"' "the data's own time caps the real clock"
m plan.json --now "2026-10-19 10:12"; has "$ERR" "not in the data" "--now 10:12"; has "$ERR" "10:05 bar" "the data stops at 10:05"
eq "$ERR" "timemachine: the entry is not in the data: no complete 10:10 bar (closing 10:15 AM) on 2026-10-19 for TSLA (the data stops at the 10:05 bar)" "the whole message"
m plan.json --now "2026-10-19 10:15" --replays 200; eq "$(j '.mc.pool_sessions')" 59 "10:15 has the entry bar"
# daylight saving: 9:30 EDT on Oct 30, 9:30 EST on Nov 2, 8:30 EST (before the open), 15:00 EST
jq -n '{chart: {result: [{meta: {symbol: "X"}, timestamp: [1793367000, 1793629800, 1793626200, 1793649600],
  indicators: {quote: [{open: [1,1,1,1], high: [1,1,1,1], low: [1,1,1,1], close: [1,1,1,1], volume: [0,0,0,0]}]}}]}}' > dst.json
dst() { run $P run --symbol X --date "$1" --entry 1 --stop 0.5 --target 2 --entry-time "$2" --offline dst.json --now 2026-11-03T00:00Z; }
dst 2026-11-02 "9:40 AM"; has "$ERR" "(the data stops at the 15:00 bar)" "15:00 EST is slot 66"
dst 2026-10-30 "9:40 AM"; has "$ERR" "(the data stops at the 09:30 bar)" "9:30 EDT"
dst 2026-11-02 "3:05 PM"; eq "$ERR" "timemachine: only 0 full session(s) of X before 2026-11-02 in the data; the time machine needs at least 20" "8:30 EST is outside the session"
# a half day is left out (2026-09-04 cut at 1:00 PM: 42 bars)
F=$(edit 'keep((day != "2026-09-04") or (hour < 13))')
m plan.json --replays 300 --offline "$F"
eq "$(j '[.mc.pool_sessions, .data.short_sessions_left_out, .history.sessions, ([.history.trades[].date] | index("2026-09-04"))]')" '[58,[{"date":"2026-09-04","bars":42}],58,null]' "a half day is left out"
run $P run --plan plan.json --offline "$F" --now "$NOW" --replays 300; has "$OUT" "Left out 1 short session(s) (half days or gaps): 2026-09-04." "the report says so"
m plan.json --replays 200; eq "$(j '[.mc.pool_to, ([.history.trades[].date] | index("2026-10-19"))]')" '["2026-10-16",null]' "today is not in the pool"
F=$(edit '.chart.result[0].indicators.quote[0].close[5] = null')
m plan.json --replays 200 --offline "$F"; eq "$(j '[.data.dropped.no_price, .mc.pool_sessions]')" '[1,58]' "a missing price"
echo '{"chart": {"result": [{"meta": {"symbol": "TSLA"}, "timestamp": [], "indicators": {"quote": [{}]}}], "error": null}}' > empty.json
m plan.json --offline empty.json; eq "$ERR" "timemachine: no complete 5-minute bars for TSLA in the data" "empty data"
echo '{"chart": {"result": null, "error": {"code": "Not Found", "description": "No data found, symbol may be delisted"}}}' > gone.json
m plan.json --offline gone.json; eq "$ERR" "timemachine: Yahoo has no data for TSLA: No data found, symbol may be delisted" "a ticker with no data"
echo '<html>rate limited</html>' > html.json; m plan.json --offline html.json; eq "$ERR" "timemachine: Yahoo returned something that is not JSON for TSLA" "not JSON"
echo '{"spark": {}}' > spark.json; m plan.json --offline spark.json; eq "$ERR" "timemachine: Yahoo returned something unexpected for TSLA" "not a chart"
jq '.chart.result[0].meta.dataGranularity = "1m"' tsla.json > g1m.json; m plan.json --offline g1m.json; eq "$ERR" "timemachine: the data has 1m bars; the time machine needs 5-minute bars" "1-minute data"
F=$(edit 'keep(day >= "2026-09-22")'); m plan.json --offline "$F"
eq "$ERR" "timemachine: only 19 full session(s) of TSLA before 2026-10-19 in the data; the time machine needs at least 20" "fewer than 20 sessions"
F=$(edit 'keep(day >= "2026-09-21")'); m plan.json --offline "$F" --replays 200; eq "$(j '.mc.pool_sessions')" 20 "20 sessions are enough"
# an early close: the last 30 full sessions moved onto the 30 trading days up to Fri 2026-11-27 (1:00 PM close), across Nov 1
DAYS=(); dd=2026-11-27
while [ ${#DAYS[@]} -lt 30 ]; do [ "$(date -d "$dd" +%u)" -le 5 ] && [ "$dd" != 2026-11-26 ] && DAYS=("$dd" "${DAYS[@]}"); dd=$(date -d "$dd - 1 day" +%F); done
BASES=$(for x in "${DAYS[@]}"; do ets "$x 09:30"; done | jq -sc .)
jq -c --argjson bases "$BASES" '
  def edt_day: (. - 14400 | strftime("%Y-%m-%d")); def slot: (((. - 14400) % 86400) - 34200) / 300;
  .chart.result[0] as $r | $r.indicators.quote[0] as $q
  | [range(0; $r.timestamp | length) | . as $i | $r.timestamp[$i] as $t
     | select($t % 300 == 0 and ($t | slot) >= 0 and ($t | slot) < 78 and ($t | edt_day) < "2026-10-19"
              and $q.open[$i] != null and $q.high[$i] != null and $q.low[$i] != null and $q.close[$i] != null)
     | {d: ($t | edt_day), s: ($t | slot), o: $q.open[$i], h: $q.high[$i], l: $q.low[$i], c: $q.close[$i], v: $q.volume[$i]}]
  | group_by(.d) | map(select(length == 78) | sort_by(.s)) | .[-30:] as $sess
  | [range(0; 30) as $k | $sess[$k][] | select($k < 29 or .s < 42) | . + {t: ($bases[$k] + 300 * .s)}] as $bars
  | {chart: {result: [{meta: {symbol: "TSLA"}, timestamp: [$bars[].t],
      indicators: {quote: [{open: [$bars[].o], high: [$bars[].h], low: [$bars[].l], close: [$bars[].c], volume: [$bars[].v]}]}}]}}
  | {doc: ., e: ($sess[-1][8].c * 100 | round / 100)}' tsla.json > nov.json
jq -c .doc nov.json > nov27.json; EN=$(jq .e nov.json)
F=$(plan ".date = \"2026-11-27\" | .entry = $EN | .stop = ($EN - 5) | .target = ($EN + 5) | .stop_rule = \"pct\"")
run $P run --plan "$F" --offline nov27.json --now "2026-11-28 12:00" --replays 300 --json
eq "$(j '.mc | [.until, .bars_left, .pool_sessions]')" '["13:00",33,29]' "an early close ends the replay at 1:00 PM"
run $P run --plan "$F" --offline nov27.json --now "2026-11-28 12:00" --replays 300; has "$OUT" "to 1:00 PM" "the report says 1:00 PM"
run $P run --plan "$F" --offline nov27.json --now "2026-11-28 12:00" --replays 10 --until 15:55
eq "$ERR" "timemachine: --until must be on the 5-minute grid and no later than 1:00 PM (an early close)" "--until past an early close"
m "$(plan '.symbol = "AAPL"')"; eq "$ERR" "timemachine: the data is for TSLA but the plan is for AAPL" "the data must be the plan's ticker"

# ---- plans
m "$(plan '.date = "2026-10-20"')"; has "$ERR" "not in the data" "another day"; has "$ERR" "no bars for that day" "no bars that day"
m "$(plan '.entry_time = "10:55 AM" | .entry_bar = null')"; has "$ERR" "not in the data" "10:50 has not happened yet"
while IFS='#' read -r edit words; do m "$(plan "$edit")"; has "$ERR" "$words" "plan check: $edit"; eq "$CODE" 1 "plan check exits 1: $edit"; done <<'CASES'
.entry_time = "10:17 AM" | .entry_bar = null#5-minute grid
.entry_time = "4:00 PM" | .entry_bar = null#between 9:35 AM and 3:55 PM
.entry_bar = 9#disagrees with itself
.entry_time = null | .entry_bar = null#entry_time
.interval = "1m"#7 days of 1-min bars
.stop = 365.0#stop < entry < target
.side = "short"#buy or sell
.symbol = ""#ticker
.entry = "abc"#number for entry
.date = "Oct 19"#date like
.opening_range = {"minutes": 7}#multiple of 5
CASES
m "$(plan '.entry_bar = 9')"; eq "$ERR" "timemachine: the plan disagrees with itself: entry_bar 9 but entry_time 10:15 AM is bar 8" "disagree message"
m "$(plan '.stop = 365.0')"; eq "$ERR" "timemachine: a buy needs stop < entry < target (got stop 365.0, entry 364.18, target 370.04)" "the order message"
m "$(plan '.symbol = ""')"; eq "$ERR" "timemachine: the plan needs a ticker symbol, got ''" "symbol repr"
m "$(plan '.entry = "abc"')"; eq "$ERR" "timemachine: the plan needs a number for entry, got 'abc'" "entry repr"
m "$(plan '.entry = -1')"; eq "$ERR" "timemachine: the plan's entry must be a positive price, got -1" "a negative entry"
m "$(plan '.date = "Oct 19"')"; eq "$ERR" "timemachine: the plan needs a date like 2026-10-19, got 'Oct 19'" "date repr"
m "$(plan '.entry_bar = "x"')"; eq "$ERR" "timemachine: entry_bar must be a bar number, got 'x'" "entry_bar repr"
m "$(plan '.opening_range = {"minutes": "x"}')"; eq "$ERR" "timemachine: opening_range.minutes must be a number" "minutes not a number"
m "$(plan '.entry_time = "25:00"')"; eq "$ERR" "timemachine: entry_time '25:00' is not a clock time" "not a clock time"
m "$(plan '.entry_time = "noon" | .entry_bar = null')"; eq "$ERR" "timemachine: entry_time wants a New York time like 10:15 AM or 15:55, not 'noon'" "noon"
m "$(plan '.date = "20261019"')" --replays 50; eq "$(j '.plan.date')" '"2026-10-19"' "a compact ISO date"
m "$(plan '.date = "2026-W43-1"')" --replays 50; eq "$(j '.plan.date')" '"2026-10-19"' "an ISO week date"
m "$(plan 'del(.entry_time)')" --replays 50; eq "$(j '.plan.entry_time')" '"10:15 AM"' "entry_bar alone"
m "$(plan '.entry_bar = null')" --replays 50; eq "$(j '.plan.entry_bar')" 8 "entry_time alone"
m "$(plan '.entry_time = "10:15" | .entry_bar = null')" --replays 50; eq "$(j '.plan.entry_bar')" 8 "10:15 (24-hour)"
m "$(plan '.entry_time = "10:15am" | .entry_bar = null')" --replays 50; eq "$(j '.plan.entry_bar')" 8 "10:15am"
m plan.json --until 15:55 --replays 500
eq "$(j '.mc | [.until, .bars_left, .band_times[-1]]')" '["15:55",68,"15:55"]' "--until cuts the replay"; U=$(j '.mc')
run $P run --plan plan.json --offline "$TSLA" --now "$NOW" --until 15:55 --replays 500; has "$OUT" "still open at 3:55 PM" "the report says 3:55 PM"
m plan.json --until "3:55 PM" --replays 500; eq "$(j '.mc')" "$U" "3:55 PM is 15:55"
m plan.json --until 3:55 --replays 500; eq "$(j '.mc')" "$U" "a bare 3:55 is p.m."
while IFS='|' read -r bad words; do m plan.json --until "$bad" --replays 10; has "$ERR" "$words" "--until $bad"; done <<'CASES'
10:15 AM|nothing to replay
16:05|no later than 4:00 PM
15:57|5-minute grid
noon|New York time
CASES
m "$(plan '.entry = 368.0 | .stop = 362.0 | .target = 374.0')" --replays 200
has "$(j '.warnings[0]')" "outside that bar's range" "the warning"
run $P run --plan "$(plan '.entry = 368.0 | .stop = 362.0 | .target = 374.0')" --offline "$TSLA" --now "$NOW" --replays 200; has "$OUT" "Note: the plan's entry 368.00 is outside that bar's range" "the report notes it"
# the stop rule worked out when not given (cents_away rounds away from the entry)
m "$(plan '.stop_rule = null')" --replays 100; eq "$(j '.history.rule.stop_rule')" '"or_mid"' "auto finds or_mid"
m "$(plan '.stop_rule = null | .stop = 354.47 | .target = 373.89')" --replays 100; eq "$(j '.history.rule | [.stop_rule, .target_r]')" '["or_low",1.0]' "auto finds or_low"
m "$(plan '.stop_rule = "atr" | .stop = 360.0 | .target = 372.54')" --replays 100; eq "$(j '.history.rule | [.stop_rule, .stop_pct, .target_r]')" '["pct",1.1478,2.0]' "an unknown rule is pct"
m "$(plan '.stop_rule = "or_high"')" --replays 100; eq "$(j '.history.rule.stop_rule')" '"pct"' "or_high on a buy is pct"
m "$(plan '.setup = "flag"')" --replays 200
eq "$(j '[.history.rule, .mc.pool_sessions, .history.note]')" "[null,59,\"skipped: the history replay knows the opening-range breakout (orb) only, this plan's setup is 'flag'\"]" "a setup other than orb skips the history"
run $P run --plan "$(plan '.setup = "flag"')" --offline "$TSLA" --now "$NOW" --replays 200; has "$OUT" "History: skipped" "the report says skipped"
m "$(plan '.side = "sell" | .stop = 370.04 | .target = 358.32 | .stop_rule = "or_high"')" --replays 2000
eq "$(j '.mc | (.p_target + .p_stop + .p_neither - 1 | fabs) < 1e-6')" true "a sell adds up"
eq "$(j '[.mc.r_at_target == 1, .history.rule.stop_rule, ([.history.trades[] | .target < .entry and .entry < .stop] | all)]')" '[true,"or_high",true]' "a sell mirrors the rules"
run $P run --plan "$(plan '.side = "sell" | .stop = 370.04 | .target = 358.32 | .stop_rule = "or_high"')" --offline "$TSLA" --now "$NOW" --replays 2000; has "$OUT" "below the entry" "below the entry"
# 2R targets against their break-even
m "$(plan '.stop = 362.0 | .target = 368.54 | .stop_rule = "pct"')"
eq "$(j '.mc | [.r_at_target, .p_target, .p_stop, .avg_R]')" '[2.0,0.3345,0.6238,0.068]' "2R near break-even"
eq "$(j '.verdict')" '"Close to break-even: at 2R the target has to come first in 33 % of the replays that hit one side; it did in 35 % (target first 33.5 % vs stop first 62.4 %)."' "the 2R verdict"
eq "$(j '.verdict_notes')" '[]' "no notes"
m "$(plan '.target = 375.90')"
eq "$(j '.mc | [.r_at_target, .p_target, .p_stop, .p_neither, .avg_R]')" '[2.0,0.0397,0.2558,0.7045,0.042]' "2R, a clear lean"
eq "$(j '.verdict')" '"Strongly toward the stop: at 2R the target has to come first in 33 % of the replays that hit one side; it did in 13 % (target first 4.0 % vs stop first 25.6 %)."' "strongly toward the stop"
eq "$(j '.verdict_notes')" '["70.5 % of the replays hit neither by 4:00 PM; the price at 4:00 PM decides those.","Average R is +0.042 all the same: the replays that hit neither pull it the other way."]' "both notes"
run $P run --plan "$(plan '.target = 375.90')" --offline "$TSLA" --now "$NOW"
has "$OUT" $'(target first 4.0 % vs stop first 25.6 %).\n70.5 % of the replays hit neither by 4:00 PM; the price at 4:00 PM decides those.\nAverage R is +0.042 all the same: the replays that hit neither pull it the other way.' "the notes follow the verdict"
has "$OUT" "target 375.90 (2R, risk 5.86 a share)" "2R in the title"

# ---- CLI
m plan.json; eq "$CODE" 0 "json exits 0"; eq "$(j '.mc.p_stop')" 0.2548 "json p_stop"; eq "$(j '.mc | has("sample_paths")')" false "json has no sample paths"
eq "$(j '.verdict[:20]')" '"Close to a coin flip"' "json verdict"; eq "$(j '.mc | has("bands")')" true "json keeps the bands"
run $P run --symbol tsla --date 2026-10-19 --entry 364.18 --stop 358.32 --target 370.04 --entry-time "10:15 AM" --offline "$TSLA" --now "$NOW" --json
eq "$CODE" 0 "the plan as flags"
eq "$(j '[.mc.p_target, .mc.p_green, .plan.stop_rule, .history.target, .history.stop, .history.avg_R, .plan.from, .plan.symbol]')" '[0.2938,0.5168,"or_mid",13,15,-0.062,"flags","TSLA"]' "flags match the file"
eq "$(j '.plan | has("flat_by")')" false "no flat_by from flags"
run $P run --symbol TSLA --entry 364.18 --offline "$TSLA"; eq "$CODE" 1 "flags missing"; eq "$ERR" "timemachine: with --symbol also give --stop, --target, --entry-time" "which flags are missing"
mkdir -p dflt && cp "$TSLA" dflt/ && cd dflt || exit 1
mkdir -p trader && cp ../plan.json trader/plan.json
run $P run --offline . --now "$NOW" --replays 500 --write; eq "$CODE" 0 "an offline folder and the default paths"
[ -e trader/timemachine.json ] && ok || bad "default --write path"
run $P chart --from; eq "$CODE" 0 "chart --from the default"; has "$OUT" "* median" "the legend"
eq "$(jq .mc.replays trader/timemachine.json)" 500 "500 replays written"
run $P run --offline "$TMP" --now "$NOW"; eq "$ERR" "timemachine: no TSLA-60d5m.json(.gz) in $TMP" "a folder without the chart"
cd "$TMP" || exit 1
for args in "run --plan $TMP/nope.json" "run --plan plan.json --offline $TMP/missing.json.gz" "run --plan plan.json --offline $TSLA --until 9:00" "chart --from $TMP/nope.json"; do
  # shellcheck disable=SC2086
  run $P $args; eq "$CODE" 1 "error exits 1: $args"; eq "$(echo "$ERR" | wc -l | tr -d ' ')" 1 "one line: $args"; has "$ERR" "timemachine: " "prefix: $args"; hasnt "$ERR" "Traceback" "no traceback: $args"; done
run $P run --plan "$TMP/nope.json"; eq "$ERR" "timemachine: no plan at $TMP/nope.json: run /trader-exit first, or pass --symbol --date --entry --stop --target --entry-time" "no plan"
run $P run --plan plan.json --offline "$TMP/missing.json.gz"; eq "$ERR" "timemachine: no offline chart file $TMP/missing.json.gz" "no chart file"
run $P run --plan plan.json --offline "$TSLA" --now "$NOW" --until 9:00; eq "$ERR" "timemachine: --until 9:00 AM leaves nothing to replay after the 10:15 AM entry" "--until 9:00"
run $P chart --from "$TMP/nope.json"; eq "$ERR" "timemachine: no $TMP/nope.json: run \`timemachine run --write\` first" "chart without a result"
echo '{"mc": {}}' > notdoc.json; run $P chart --from notdoc.json; eq "$ERR" "timemachine: notdoc.json is not a time machine result" "not a result"
echo '[1]' > list.json; run $P run --plan list.json --offline "$TSLA"; eq "$ERR" "timemachine: list.json is not a plan" "not a plan"
echo '{' > broken.json; run $P run --plan broken.json --offline "$TSLA"; has "$ERR" "timemachine: broken.json is not valid JSON: " "a broken plan"
run $P; eq "$CODE" 1 "no command exits 1"; has "$OUT" "usage: timemachine [-h] {run,chart} ..." "usage"
run $P run --side short; eq "$CODE" 2 "a bad choice exits 2"
# the chart
run $P chart --from trader/timemachine.json; eq "$CODE" 0 "chart exits 0"
eq "$(echo "$OUT" | sed -n 1p)" "TSLA 2026-10-19, 2,000 replay paths from the 10:15 AM entry (seed 19)" "chart title"
eq "$(echo "$OUT" | grep -c '^   370.04 |.*  target$')" 1 "the target row"
eq "$(echo "$OUT" | grep -c '^   358.32 |.*  stop$')" 1 "the stop row"
eq "$(echo "$OUT" | grep -c '^   364.18 |\*.*  entry$')" 1 "the entry row starts on the median"
has "$OUT" "10:15" "the first time"; has "$OUT" "4:00" "the last time"
eq "$(echo "$OUT" | tail -2)" $'Close to a coin flip: target first 29.4 % vs stop first 25.5 %.\n45.1 % of the replays hit neither by 4:00 PM; the price at 4:00 PM decides those.' "the verdict under the chart"
eq "$(echo "$OUT" | wc -l | tr -d ' ')" 23 "17 rows plus title, axis and legend"
eq "$(echo "$OUT" | sed -n 20p)" "           10:15                              1:10                           4:00" "the time axis"
CH=$OUT; run $P chart --plan plan.json --offline "$TSLA" --now "$NOW"; eq "$OUT" "$CH" "chart again from the plan is the same"
done_ "daytrade-timemachine/timemachine"

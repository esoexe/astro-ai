#!/usr/bin/env bash
# daytrade-exit/exit: the assertions of day-trader-skill/tests/test_exit.py, run against the C++ tool. compute(...) unit
# calls become plan runs with --symbol/--entry/--or-high/--or-low flags and --json; the entry-to-plan run starts from the
# entry.json trader-entry writes for the reel (tests/fixtures/daytrade-exit/entry-reel.json).
source "$(dirname "$0")/lib.sh"
X="$FIX/daytrade-exit"; TSLA="$FIX/daytrade-entry/TSLA-2026-10-19-5m.json"; REEL="$X/plan-reel.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1; mkdir trader
j() { echo "$OUT" | jq -c "$1"; }
# c ENTRY STOP [TARGET] [OH OL]: a flags plan for 2026-10-19 (OH/OL "-" = no opening range)
c() {
  local e=$1 s=$2 t=${3:-2R} oh=${4:-362.18} ol=${5:-354.47} orr=()
  [ "$oh" != "-" ] && orr=(--or-high "$oh" --or-low "$ol")
  run daytrade-exit/exit plan --symbol TSLA --entry "$e" "${orr[@]}" --stop "$s" --target "$t" --date 2026-10-19 --now "2026-10-19 10:16" --config "$TMP/none" --json
}
P() { run daytrade-exit/exit plan --now "2026-10-19 10:16" "$@"; }
reel_ok() { echo "$1" | jq --slurpfile e "$REEL" '. as $p | $e[0] | to_entries | all(.value == $p[.key])'; }

# ---- the math
c 364.18 or-mid 1R; eq "$CODE" 0 "reel exit"
eq "$(j '[.stop, .risk_per_share, .target, .reward_risk == 1, .target_label]')" '[358.32,5.86,370.04,true,"1R (one times the risk)"]' "the reel: or-mid, 1R"
has "$(j .stop_detail)" "rounded down" "floor(358.325)"; eq "$(j .stop_detail)" '"($362.18 + $354.47) / 2 = $358.325, rounded down"' "stop detail"
c 364.18 or-mid; eq "$(j '[.target, .target_label, .target_r == 2]')" '[375.9,"2R (two times the risk)",true]' "the default 2R"
c 364.1759948730469 or-mid 1R 362.17999267578125 354.4700012207031; eq "$(j '[.entry, .stop, .target]')" '[364.18,358.32,370.04]' "raw Yahoo floats give the same cents"
c 364.18 or-low; eq "$(j '[.stop, .risk_per_share, .target, .stop_label]')" '[354.47,9.71,383.6,"opening-range low"]' "or-low"
c 364.18 price:358.329; eq "$(j '[.stop, .stop_label, .stop_detail]')" '[358.32,"your price","$358.329 rounded down"]' "a price stop rounds away from the entry"
c 364.18 pct:1; eq "$(j '[.stop, .risk_per_share, .target, .stop_label]')" '[360.53,3.65,371.48,"1% under the entry"]' "pct:1"
eq "$(j .stop_detail)" '"$364.18 x (1 - 1%) = $360.5382, rounded down"' "pct detail"
c 100 price:99.01 1.5R - -; eq "$(j '[.target, .target_label, .setup, .opening_range]')" '[101.49,"1.5R (1.5 times the risk)","level",null]' "target rounds half up (100 + 1.485)"
c 364.18 or-mid price:372; eq "$(j '[.reward_risk, .target_label, .target_r]')" '[1.33,"your price (1.33R)",1.33]' "a price target"
c 364.18 or-mid 10R; eq "$(j .target_label)" '"10R (10 times the risk)"' "words stop at five"
c 364.18 or-mid 3.0R; eq "$(j .target_label)" '"3R (three times the risk)"' "3.0R is three"
# refusals say why
for case in "price:364.18|2R|at or above the entry" "price:365|2R|at or above the entry" "or-mid|price:364|at or below the entry" \
            "price:364.01|2R|too tight to survive the spread" "pct:0.04|2R|too tight to survive the spread"; do
  IFS='|' read -r s t why <<< "$case"; c 364.18 "$s" "$t"; eq "$CODE" 1 "refuse $s $t"; has "$ERR" "$why" "refusal $s"
done
c 364.18 price:363.99; eq "$(j .risk_per_share)" 0.19 "0.05% of 364.18 is 0.182: \$0.19 risk is allowed"
c 364.18 price:364.00; eq "$CODE" 1 "\$0.18 is not"; eq "$ERR" 'exit: the stop is $0.18 under the entry, less than 0.05% of the price ($0.18 on $364.18): too tight to survive the spread, normal wiggle would stop you out. Widen it.' "too tight message"
c 358.00 or-mid; eq "$CODE" 1 "the midpoint is above this entry"; eq "$ERR" "exit: the stop \$358.32 (opening-range midpoint) is at or above the entry \$358.00: a long's stop goes under the entry" "above message"
for bad in mid-ish pct:100 price:0 atr:-1; do c 364.18 "$bad"; eq "$CODE" 1 "bad stop $bad"; done
c 364.18 mid-ish; eq "$ERR" "exit: --stop is or-mid, or-low, price:X, pct:X or atr:K (got 'mid-ish')" "bad stop message"
c 364.18 pct:100; eq "$ERR" "exit: --stop pct:X needs X under 100" "pct cap"; c 364.18 price:0; eq "$ERR" "exit: --stop price needs a number above 0" "price 0"
for bad in 0R R lots price:x; do c 364.18 or-mid "$bad"; eq "$CODE" 1 "bad target $bad"; done
c 364.18 or-mid 0R; eq "$ERR" "exit: --target needs an R multiple above 0, like 1R or 2R" "0R message"
c 364.18 or-mid lots; eq "$ERR" "exit: --target is 2R, 1R, 1.5R ... or price:X (got 'lots')" "bad target message"
c 364.18 or-mid 2R - -; eq "$CODE" 1 "or-mid needs the range"; eq "$ERR" "exit: an or-mid stop needs the opening range (--or-high and --or-low)" "range message"
c 364.18 or-mid 2R 354.47 362.18; eq "$CODE" 1 "high under low"; eq "$ERR" 'exit: the opening-range high $354.47 must be above its low $362.18' "high/low message"
c 1234.5 pct:2 2R - -; eq "$(j '[.entry, .stop, .target]')" '[1234.5,1209.81,1283.88]' "four-digit prices"
run daytrade-exit/exit plan --symbol TSLA --entry 1234.5 --stop pct:2 --date 2026-10-19 --now "2026-10-19 10:16"; has "$OUT" 'Entry         $1,234.50' "money has commas"

# ---- the ATR stop on the session's bars
cp "$X/entry-reel.json" trader/entry.json
P --stop atr:1.5 --offline "$TSLA" --json; eq "$CODE" 0 "atr exit"
eq "$(j '[.stop, .risk_per_share, .target, .stop_rule, .atr_bars, .atr]')" '[359.35,4.83,373.84,"atr",9,3.2182]' "atr:1.5 over the 9 bars to the entry"
eq "$(j '[.stop_label, .stop_detail]')" '["1.5 x ATR(9) under the entry","ATR $3.2182 over the 9 bars to the entry (fewer than 14 so far), rounded down"]' "atr labels"
P --stop atr:1.5 --offline "$TSLA" --entry-bar 1; eq "$CODE" 1 "two bars is not enough"; has "$ERR" "at least 3 closed" "atr minimum"
eq "$ERR" "exit: an atr stop needs at least 3 closed 5m bars of TSLA on 2026-10-19 by the entry; found 2" "atr minimum message"
# later in the day it uses the last 14 bars; the file's in-progress 10:45 bar and tick never count
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --stop atr:1 --offline "$TSLA" --date 2026-10-19 --json
eq "$(j .atr_bars)" 14 "the last 14 of the 15 closed bars"
mkdir -p "$TMP/dir" && gzip -c "$TSLA" > "$TMP/dir/TSLA.json.gz"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --stop atr:1 --offline "$TMP/dir" --date 2026-10-19 --json; eq "$(j .atr_bars)" 14 "--offline DIR finds TSLA.json.gz"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --stop atr:1 --offline "$FIX/daytrade-entry" --date 2026-10-19; eq "$ERR" "exit: no offline file $FIX/daytrade-entry/TSLA.json.gz" "--offline DIR without TSLA.json"
run daytrade-exit/exit plan --symbol NVDA --entry 190 --stop atr:1 --offline "$TSLA" --date 2026-10-19; eq "$CODE" 1 "the wrong file"; eq "$ERR" "exit: the chart file holds TSLA, not NVDA" "wrong file message"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --stop atr:1 --offline "$TMP/nope.json" --date 2026-10-19; eq "$ERR" "exit: no offline file $TMP/nope.json" "missing offline file"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --stop atr:1 --date 2026-10-19; eq "$CODE" 1 "no network in the suite"; has "$ERR" "Yahoo did not answer (could not connect" "offline network message"

# ---- entry.json -> plan
P --stop or-mid --target 1R --write; eq "$CODE" 0 "entry to plan exit"
eq "$(reel_ok "$(cat trader/plan.json)")" true "trader/plan.json is the contract"
eq "$(jq -c '[.created_at, .note, .stop_detail]' trader/plan.json)" '["2026-10-19T14:16:00Z","A stop is a plan, not a guarantee: a gap or a fast market can fill past it.","($362.18 + $354.47) / 2 = $358.325, rounded down"]' "plan extras"
has "$OUT" 'Stop          $358.32  opening-range midpoint' "stop line"; has "$OUT" 'Target        $370.04  1R (one times the risk)' "target line"
has "$OUT" 'Risk a share  $5.86 (1.61% of the entry)' "risk line"; has "$OUT" 'Reward:risk   1.0' "rr line"; has "$OUT" 'Flat by       15:55' "flat line"
has "$OUT" 'not a guarantee' "note"; has "$OUT" '/trader-timemachine' "next step"; hasnt "${OUT,,}" "pro""fit" "never on screen"
eq "$(echo "$OUT" | sed -n 1p)" 'Exit plan: TSLA long, Mon 2026-10-19, opening-range breakout on 5-minute bars' "header"
eq "$(echo "$OUT" | sed -n 2p)" '  Entry         $364.18 at 10:15 AM (bar 8)' "entry line"
has "$OUT" "Wrote trader/plan.json." "wrote line"
P --write; eq "$(jq -c '[.target, .target_r == 2]' trader/plan.json)" '[375.9,true]' "the defaults: or-mid, 2R"
run daytrade-exit/exit show --now "2026-10-19 10:20"; eq "$CODE" 0 "show"; has "$OUT" '$375.90' "show target"; hasnt "$OUT" "not today" "same day"
run daytrade-exit/exit show --now "2026-10-20 10:20"; eq "$(echo "$OUT" | tail -1)" "Note: this plan is for 2026-10-19, not today (2026-10-20)." "another day"
run daytrade-exit/exit show --json; eq "$(j '[.target, .created_by]')" '[375.9,"trader-exit"]' "show --json"
P --write "$TMP/x/y/plan.json" --json; eq "$(jq -c .stop "$TMP/x/y/plan.json")" 358.32 "--write PATH makes the folders"; eq "$(j .warnings)" '[]' "no warnings"
P --stop or-mid --target 1R --json; eq "$(j 'has("warnings") and (keys_unsorted[-1] == "warnings")')" true "json = the plan + warnings"

# ---- manual flags
P --symbol tsla --entry 364.18 --or-high 362.18 --or-low 354.47 --time 10:15 --entry-bar 8 --date 2026-10-19 --target 1R --json
eq "$CODE" 0 "flags exit"; eq "$(reel_ok "$OUT")" true "flags give the same plan"; eq "$(j .warnings)" '[]' "no warnings for flags"
P --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --time "3:05 pm" --date 2026-10-19 --json; eq "$(j .entry_time)" '"3:05 PM"' "pm clock"
P --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --time "13:05 PM" --date 2026-10-19; eq "$ERR" "exit: the entry time wants a time like 10:15 AM, not '13:05 PM'" "bad clock"
P --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --date 2026-13-01; eq "$ERR" "exit: date wants YYYY-MM-DD, not '2026-13-01'" "bad date"
P --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --date 2026-10-20 --json; eq "$(j .warnings)" '["This plan is for 2026-10-20, not today (2026-10-19)."]' "another day warns"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --now "2026-10-19 14:16Z" --json; eq "$(j '[.date, .created_at]')" '["2026-10-19","2026-10-19T14:16:00Z"]' "--now in UTC"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --or-high 362.18 --or-low 354.47 --now "2026-10-19 10:16 ET" --json; eq "$(j .created_at)" '"2026-10-19T14:16:00Z"' "--now ... ET"
run daytrade-exit/exit plan --symbol TSLA --entry 364.18 --now "soon"; eq "$ERR" 'exit: --now wants a New York time like "2026-10-19 10:15" (or ...Z for UTC)' "bad --now"

# ---- refusals from the CLI
jq '.status = "waiting" | .time = null | .price = null | .bar_index = null | .bar_start = null' "$X/entry-reel.json" > trader/entry.json
P; eq "$CODE" 1 "waiting"; has "$ERR" "TSLA is waiting, not triggered" "not triggered"
P --entry 362.19 --json; eq "$CODE" 0 "plan ahead"; eq "$(j '[.entry, .stop, .entry_time, .entry_bar]')" '[362.19,358.32,null,null]' "plan ahead at the level"
jq '.setup = "level" | .level = 362.0' "$X/entry-reel.json" > trader/entry.json
P; eq "$CODE" 1 "level default"; has "$ERR" "level setup has no opening-range stop" "level needs a stop"
P --stop pct:1; eq "$CODE" 0 "level with pct"; has "$OUT" "close above a level" "level wording"
P --side sell; has "$ERR" "v2 plans longs only" "--side sell"
jq '.side = "sell"' "$X/entry-reel.json" > trader/entry.json
P; has "$ERR" "longs only" "a sell entry"; eq "$ERR" "exit: trader/entry.json is a sell: v2 plans longs only" "sell message"
P --stop price:364.10; has "$ERR" "longs only" "a sell entry with a stop"
rm trader/entry.json
P; eq "$CODE" 1 "no entry"; eq "$ERR" "exit: no entry (run /trader-entry first, or give --symbol and --entry) at trader/entry.json" "no entry message"
P --symbol TSLA; has "$ERR" "needs --entry" "--symbol alone"
P --symbol TSLA --from x.json --entry 1; has "$ERR" "not both" "--from and --symbol"
P --symbol TSLA --entry 364.18 --or-high 362.18; has "$ERR" "both --or-high and --or-low" "half a range"
echo '{"symbol": "TSLA", "status": "triggered"' > trader/entry.json; P; has "$ERR" "trader/entry.json is not valid JSON" "bad JSON"
echo '{"status": "triggered", "price": 1}' > trader/entry.json; P; eq "$ERR" "exit: trader/entry.json has no symbol or price" "no symbol"
jq '.interval = "15m"' "$X/entry-reel.json" > trader/entry.json; P; eq "$ERR" "exit: interval is 5m or 1m" "bad interval"
hasnt "$ERR" "Traceback" "no tracebacks"

# ---- flat-by: the config, half days, late entries
cp "$X/entry-reel.json" trader/entry.json
echo '{"flat_by": "15:45", "min_reward_risk": 1.5}' > trader/config.json
P --target 1R --json; eq "$(j .flat_by)" '"15:45"' "config flat_by"; has "$(j '.warnings[0]')" "under your min_reward_risk 1.5" "min_reward_risk warns"
eq "$(j '.warnings[0]')" '"Reward:risk 1.0 is under your min_reward_risk 1.5 in the config: trader-risk checks that."' "warning text"
P --target 1R; has "$OUT" "Note: Reward:risk 1.0 is under" "warning in text"
echo '{"min_reward_risk": "lots"}' > trader/config.json; P --target 1R --json; eq "$(j .warnings)" '[]' "a bad min_reward_risk is ignored here"
echo '{"flat_by": "3pm"}' > trader/config.json; P; eq "$ERR" "exit: flat_by wants a New York time like 15:55, not '3pm'" "bad config flat_by"
rm trader/config.json
jq '.date = "2026-11-27"' "$X/entry-reel.json" > trader/entry.json
run daytrade-exit/exit plan --now "2026-11-27 10:16"; eq "$CODE" 0 "half day"
has "$OUT" "Flat by       12:55 ET" "flat by moves up"; has "$OUT" "early close" "says why"
has "$OUT" "Note: Flat-by 12:55: moved up for the 1:00 p.m. early close (day after Thanksgiving)." "half-day note"
run daytrade-exit/exit plan --now "2026-11-27 10:16" --flat-by 14:00; eq "$CODE" 1 "explicit flat-by after a half-day close"; has "$ERR" "after the 1:00 PM close" "half-day refusal"
cp "$X/entry-reel.json" trader/entry.json
P --flat-by 16:30; has "$ERR" "after the 4:00 PM close" "after the close"
P --flat-by 25:00; eq "$ERR" "exit: --flat-by wants a New York time like 15:55, not '25:00'" "bad --flat-by"
P --flat-by 10:00; eq "$ERR" "exit: the entry at 10:15 AM is at or after your flat-by 10:00: there is no time left to hold it" "flat-by before the entry"
jq '.time = "3:58 PM"' "$X/entry-reel.json" > trader/entry.json
P; eq "$CODE" 1 "late entry"; has "$ERR" "no time left to hold it" "late entry refused"

# ---- show
rm -f trader/plan.json
run daytrade-exit/exit show; eq "$CODE" 1 "show without a plan"; has "$OUT" "No plan" "no plan"; eq "$OUT" "No plan at trader/plan.json yet. Make one: exit plan --write" "no plan message"
echo '{"symbol": "TSLA", "date": "2026-10-19"}' > trader/plan.json; run daytrade-exit/exit show; eq "$ERR" "exit: trader/plan.json is missing 'entry': make a new plan with exit plan --write" "a broken plan"
jq '{symbol, date, entry, stop, target, risk_per_share, reward_risk, flat_by}' "$REEL" > trader/plan.json
run daytrade-exit/exit show --now "2026-10-19 10:20"; eq "$CODE" 0 "a bare plan shows"
eq "$(echo "$OUT" | sed -n 1p)" "Exit plan: TSLA long, Mon 2026-10-19, opening-range breakout on 5-minute bars" "bare plan header"
eq "$(echo "$OUT" | sed -n 3p)" "  Stop          \$358.32  " "bare stop line"

# ---- help
run daytrade-exit/exit; eq "$CODE" 1 "no command"; has "$OUT" "usage: exit [-h] {plan,show} ..." "usage"
run daytrade-exit/exit plan --help; eq "$CODE" 0 "plan --help"; has "$OUT" "--write [PATH]" "write metavar"; has "$OUT" "{buy,long,sell,short}" "side choices"
run daytrade-exit/exit plan --setup x; eq "$CODE" 2 "bad choice"
done_ "daytrade-exit/exit"

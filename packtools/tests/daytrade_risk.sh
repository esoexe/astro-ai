#!/usr/bin/env bash
# daytrade-risk/check: the assertions of day-trader-skill/tests/test_risk.py, run against the C++ tool through files.
# check.check(..., price_times=None) (every price fresh) maps to a prices file whose as_of is the pinned --now.
source "$(dirname "$0")/lib.sh"
R="$FIX/daytrade-risk"; CFG0="$R/config.example.json"; TSLA_F="$R/orders-tsla.json"; REEL="$R/orders-reel.json"
NOW="2026-10-19 10:15"; TODAY=2026-10-19; V1_TODAY=2026-10-13
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
V1='{"budget": 5000, "cash_buffer_pct": 10, "max_position_pct": 25, "max_order_usd": 2000, "min_order_usd": 5, "max_orders_per_day": 10, "require_stop": false, "no_entries_before": null, "no_entries_after": null, "max_trades_per_day": 0, "max_daily_loss_usd": 0, "max_loss_per_trade_usd": 0, "min_reward_risk": 0}'
PRICES='{"AAPL": 341.27, "AXP": 309.84, "GOOGL": 353.78, "KO": 90.11, "BAC": 53.96}'
DEFS='def placed($s; $side; $day; $ml; $kind; $st): {symbol: $s, side: $side, status: $st, date: $day, at: ($day + "T10:20:00-04:00")} + (if $ml == null then {} else {max_loss: $ml} end) + (if $kind == null then {} else {kind: $kind} end);
      def p($s): placed($s; "buy"; "2026-10-19"; 46.88; null; "placed");'
tsla() { jq -c ".orders[0] | ${1:-.}" "$TSLA_F"; }
cfg() { jq -c "${1:-.}" "$CFG0"; }
v1() { echo "$V1" | jq -c "${1:-.}"; }
state() { jq -nc "$DEFS {orders: [$1]}"; }
# ck ORDERS(json list) CONFIG(json) [extra args]: env PF, PXJ (prices doc json), ST (state json), NOW_ (or ON_DAY for --today)
ck() {
  echo "{\"orders\": $1}" > "$TMP/o.json"; echo "$2" > "$TMP/c.json"; shift 2
  local pf="$TMP/none" px="$TMP/none" st="$TMP/none" clock=(--now "${NOW_:-$NOW}")
  [ -n "$PF" ] && { echo "$PF" > "$TMP/pf.json"; pf="$TMP/pf.json"; }
  [ -n "$PXJ" ] && { echo "$PXJ" > "$TMP/px.json"; px="$TMP/px.json"; }
  [ -n "$ST" ] && { echo "$ST" > "$TMP/st.json"; st="$TMP/st.json"; }
  [ -n "$ON_DAY" ] && clock=(--today "$ON_DAY")
  run daytrade-risk/check --orders "$TMP/o.json" --config "$TMP/c.json" --portfolio "$pf" --prices "$px" --state "$st" "${clock[@]}" --no-write --json "$@"
}
fresh_px() { echo "{\"as_of\": \"$2\", \"prices\": $1}"; }   # prices that count as fresh at the pinned clock
j() { echo "$OUT" | jq -c "$1"; }
V() { j '[.results[].verdict]'; }
reasons() { echo "$OUT" | jq -r ".results[${1:-0}].reasons | join(\" | \")"; }
warns() { echo "$OUT" | jq -r ".results[${1:-0}].warnings | join(\" | \")"; }

# the reel: the TSLA bracket passes every check
ck "[$(tsla)]" "$(cfg)"; eq "$CODE" 0 "reel passes"
eq "$(j '.results[0] | [.verdict, .reasons, .warnings, .kind, .entry_ref, .stop, .target]')" '["PASS",[],[],"entry",364.18,358.32,370.04]' "reel verdict and levels"
eq "$(j '.results[0] | [.risk_per_share == 5.86, .max_loss == 46.88, .reward_risk == 1, .value_usd == 2913.44]')" '[true,true,true,true]' "risk, max loss, reward:risk, value"
eq "$(j '[.cash_after == 2086.56, .date, .paper]')" "[true,\"$TODAY\",true]" "cash after, date, paper"
ck "[$(tsla '.qty = "9"')]" "$(cfg)"; eq "$(V)" '["BLOCK"]' "9 shares blocked"
eq "$(j '.results[0].reasons')" '["loss: 9 shares x 5.86 risk a share = $52.74 at the stop, over your $50 max loss per trade"]' "only the max loss"
# fingerprint: shared with the broker; why and plan are not part of it
ck "[$(tsla), $(tsla '.why = "anything" | .plan = {}'), $(tsla '.qty = "9"'), $(tsla '.stop_loss_stop_price = "358.00"')]" "$(cfg '.max_trades_per_day = 0 | .max_daily_loss_usd = 0 | .require_stop = false | .min_reward_risk = 0')"
eq "$(j '[.results[].fingerprint] | [.[0] == .[1], .[0] != .[2], .[0] != .[3]]')" '[true,true,true]' "fingerprint covers the Alpaca fields only"
FP=$(j '.results[0].fingerprint' | tr -d '"')
echo '{"orders": [], "x": 1}' > /dev/null
run daytrade-broker/ticket show --json --orders "$TSLA_F" --config "$TMP/none" --risk "$TMP/none" --state "$TMP/none" --now "$NOW"
eq "$(echo "$OUT" | jq -r '.[0].fingerprint')" "$FP" "same fingerprint in the broker"
# the CLI
run daytrade-risk/check --orders "$TSLA_F" --config "$CFG0" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/none" --now "$NOW" --out "$TMP/state/risk.json"
eq "$CODE" 0 "cli exit 0"; has "$OUT" "PASS  #1 buy TSLA 8 shares, entry" "cli header"
has "$OUT" "entry 364.18 (the plan's entry), stop 358.32, target 370.04: risk 5.86 a share" "cli info entry"
has "$OUT" "max loss \$46.88 (limit \$50), reward:risk 1.00 (min 1)" "cli info max loss"
has "$OUT" "1 passed, 0 blocked at 2026-10-19 10:15 New York" "cli footer"; has "$OUT" "not financial advice" "not advice"
eq "$(jq -c "[.date, .results[0].kind, .results[0].fingerprint, (.checked_at | test(\"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$\")), .orders_file]" "$TMP/state/risk.json")" "[\"$TODAY\",\"entry\",\"$FP\",true,\"$TSLA_F\"]" "risk.json record"
eq "$(jq -c '[.now_new_york, .today]' "$TMP/state/risk.json")" '["2026-10-19 10:15",{"orders_placed":0,"entries_placed":0,"loss_at_risk":0.0,"day_trades_5d":null}]' "today block" 

# day-trade checks: a stop is required
NOSTOP=$(tsla 'del(.order_class, .take_profit_limit_price, .stop_loss_stop_price, .plan) | .plan = {"entry": 364.18}')
ck "[$NOSTOP]" "$(cfg)"; has "$(reasons)" "stop: no stop" "stop required"
ck "[$NOSTOP]" "$(cfg '.require_stop = false | .min_reward_risk = 0')"; eq "$(V)" '["PASS"]' "no stop allowed when off"; has "$(warns)" "no stop" "no stop warns"
PLANSTOP=$(tsla 'del(.order_class, .take_profit_limit_price, .stop_loss_stop_price)')
ck "[$PLANSTOP]" "$(cfg)"; eq "$(V)" '["PASS"]' "plan stop counts"; has "$(warns)" "in the plan only" "plan stop warns"; eq "$(j '.results[0].max_loss == 46.88')" true "plan stop max loss"
has "$(warns)" "set a stop alert (trader-alerts: alerts add --from-plan)" "plan stop points at alerts"
# stop and target on the right side; reward:risk
ck "[$(tsla '.stop_loss_stop_price = "365.00"')]" "$(cfg)"; has "$(reasons)" "must be below the entry 364.18" "stop below entry"
has "$(warns)" "the bracket stop 365.00 differs from the plan's stop 358.32" "bracket vs plan stop"
ck "[$(tsla '.take_profit_limit_price = "364.00"')]" "$(cfg)"; has "$(reasons)" "target: the target 364.00 must be above the entry 364.18" "target above entry"
ck "[$(tsla '.take_profit_limit_price = "368.00"')]" "$(cfg)"; has "$(reasons)" "reward:risk: 0.65" "reward:risk 0.65"
has "$(reasons)" "reward:risk: 0.65 (target 368.00, entry 364.18, stop 358.32) is under your minimum 1" "reward:risk wording"
ck "[$(tsla '.take_profit_limit_price = "368.00"')]" "$(cfg '.min_reward_risk = 0.5')"; eq "$(V)" '["PASS"]' "min 0.5 passes"
# prices: a fresh higher price is the entry; stale or future prices are not used
PXT='{"as_of": "2026-10-19T14:55:00Z", "prices": {"TSLA": 366.475}, "detail": {"TSLA": {"time": "2026-10-19T14:52:13Z"}}}'
PXJ="$PXT" NOW_="2026-10-19 10:55" ck "[$(tsla)]" "$(cfg)"
eq "$(j '.results[0] | [.entry_ref, .entry_ref_from, .risk_per_share]')" '[366.48,"price",8.16]' "fresh higher price"
has "$(reasons)" "loss: 8 shares x 8.16 risk a share = \$65.28" "loss from the fresh price"
eq "$(j '.results[0].info[0]')" '"TSLA last 366.48 is above the plan'"'"'s entry 364.18: the risk is measured from 366.48"' "above-the-plan note"
for n in "2026-10-19 11:30" "2026-10-19 10:15"; do
  PXJ="$PXT" NOW_="$n" ck "[$(tsla)]" "$(cfg)"; eq "$(V)" '["PASS"]' "stale/future price: pass at $n"
  eq "$(j '.results[0].entry_ref')" 364.18 "plan entry at $n"; has "$(warns)" "older than 15 minutes" "old price warned at $n"
done
PXJ="$(fresh_px '{"TSLA": 360.00}' 2026-10-19T14:15:00Z)" ck "[$(tsla)]" "$(cfg)"; eq "$(j '.results[0].entry_ref')" 364.18 "a lower price does not shrink the risk"
PXJ="$(fresh_px '{"TSLA": 366.475}' 2026-10-19T14:15:00Z)" ck "[$(tsla '.type = "limit" | .limit_price = "364.00"')]" "$(cfg)"
eq "$(j '.results[0] | [.entry_ref == 364, .max_loss == 45.44, .verdict, .entry_ref_from]')" '[true,true,"PASS","limit"]' "limit price is the entry"
# daily loss and trades per day
ST="$(state 'p("AAPL"), p("AMD")')" ck "[$(tsla)]" "$(cfg)"; eq "$(V)" '["PASS"]' "two placed: pass"
has "$(j '.results[0].info[-1]')" "today before this: 2 of 3 trades, \$93.76 of \$150 daily loss" "today line"
ST="$(state 'p("AAPL"), p("AMD"), p("NVDA")')" ck "[$(tsla)]" "$(cfg)"
has "$(reasons)" "trades: 3 entries placed today" "trades limit"
has "$(reasons)" "daily loss: \$140.64 already at risk today plus \$46.88 for this one = \$187.52, over your \$150" "daily loss limit"
ST="$(state 'placed("AAPL"; "buy"; "2026-10-16"; 46.88; null; "placed"), placed("AMD"; "buy"; "2026-10-16"; 46.88; null; "placed"), placed("NVDA"; "buy"; "2026-10-16"; 46.88; null; "placed")')" ck "[$(tsla)]" "$(cfg)"
eq "$(V)" '["PASS"]' "yesterday does not count"
ST="$(state 'p("AAPL"), placed("AAPL"; "sell"; "2026-10-19"; null; "exit"; "placed"), placed("AMD"; "buy"; "2026-10-19"; 46.88; null; "declined"), placed("NVDA"; "buy"; "2026-10-19"; 46.88; null; "failed")')" ck "[$(tsla)]" "$(cfg)"
eq "$(V)" '["PASS"]' "exits and declines are not trades"; eq "$(j '.today.entries_placed')" 1 "one entry placed"
ST="$(state 'placed("AMD"; "buy"; "2026-10-19"; null; null; "placed")')" ck "[$(tsla)]" "$(cfg)"
eq "$(j '.warnings')" '["an entry placed today (buy AMD) has no max loss in the log; not counted in the daily loss"]' "entry without max loss warns"
# the batch counts toward the daily limits
br() { jq -nc --arg s "$1" '{symbol: $s, side: "buy", qty: "1", type: "market", time_in_force: "day", order_class: "bracket", take_profit_limit_price: "90.00", stop_loss_stop_price: "10.00", plan: {entry: 50.0, stop: 10.0, target: 90.0}}'; }
ck "[$(br AAA), $(br BBB), $(br CCC), $(br DDD)]" "$(cfg)"; eq "$(V)" '["PASS","PASS","PASS","BLOCK"]' "fourth entry blocked"
has "$(reasons 3)" "trades: 0 entries placed today plus 3 passed earlier" "batch trades"; has "$(reasons 3)" "daily loss: \$120 already at risk today plus \$40 for this one = \$160" "batch daily loss"
# entry window, New York time
while IFS='|' read -r n want; do
  NOW_="$n" ck "[$(tsla)]" "$(cfg)"; has "$(reasons)" "window: $want" "window at $n"
done <<'CASES'
2026-10-19 09:44|no new entries before 09:45 New York (it is 09:44)
2026-10-19 15:31|no new entries after 15:30 New York (it is 15:31)
2026-10-19 08:00|the market is closed (Mon 2026-10-19 08:00
2026-10-19 16:05|the market is closed
2026-10-17 10:15|the market is closed today (weekend
2026-11-26 10:15|the market is closed today (NYSE holiday: Thanksgiving Day
2026-11-27 12:31|no new entries after 12:30 New York
2026-11-27 13:05|the market is closed
2026-11-02T14:40:00Z|no new entries before 09:45 New York (it is 09:40)
CASES
NOW_="2026-10-19 08:00" ck "[$(tsla)]" "$(cfg)"; has "$(reasons)" "window: the market is closed (Mon 2026-10-19 08:00 New York; the session is 09:30 to 16:00)" "closed wording"
for n in "2026-10-19 09:45" "2026-10-19 15:30" "2026-11-27 12:30" "2026-11-02T15:15:00Z" "2026-10-30T14:40:00Z" "2026-03-09T13:50:00Z"; do
  NOW_="$n" ck "[$(tsla)]" "$(cfg)"; eq "$(V)" '["PASS"]' "window open at $n"
done
NOW_="2026-11-27 12:30" ck "[$(tsla)]" "$(cfg)"; eq "$(j '.warnings')" '["early close at 1:00 p.m. today (day after Thanksgiving): no new entries after 12:30"]' "early close warning"
NOW_="2026-10-17 10:15" ck "[$(tsla)]" "$(cfg '.no_entries_before = null | .no_entries_after = null')"; eq "$(V)" '["PASS"]' "window off"
ck "[$(tsla)]" "$(cfg '.no_entries_before = "9.45am"')"; eq "$CODE" 1 "bad window time"; eq "$ERR" 'check: no_entries_before must be a New York time like "09:45", got '"'9.45am'" "bad window message"
NOW_="2028-03-01 10:15" ck "[$(tsla)]" "$(cfg)"; has "$(j '.warnings')" "the holiday list covers 2026 and 2027 only: check nyse.com for 2028-03-01" "calendar years warning"
# New York time, not the machine's
NOW_="2026-12-01 10:15" ck "[$(tsla)]" "$(cfg)"; eq "$(j '.now_new_york')" '"2026-12-01 10:15"' "December: EST"
NOW_="2026-10-19T14:15:00Z" ck "[$(tsla)]" "$(cfg)"; eq "$(j '.now_new_york')" '"2026-10-19 10:15"' "UTC to New York"
# pdt guard
trade() { echo "placed(\"X${1: -2}\"; \"buy\"; \"$1\"; 46.88; null; \"placed\"), placed(\"X${1: -2}\"; \"sell\"; \"$1\"; null; \"exit\"; \"filled\")"; }
ST3="$(state "$(trade 2026-10-13), $(trade 2026-10-14), $(trade 2026-10-15)")"
ST="$ST3" ck "[$(tsla)]" "$(cfg '.pdt_guard = true')"; has "$(reasons)" "pdt: 3 day trades in the last 5 business days" "pdt blocks"
has "$(reasons)" "(2026-10-13 X13, 2026-10-14 X14, 2026-10-15 X15); a 4th trips the pattern-day-trader rule" "pdt list"; eq "$(j '.today.day_trades_5d')" 3 "day_trades_5d"
ST="$ST3" ck "[$(tsla)]" "$(cfg)"; eq "$(V)" '["PASS"]' "pdt off by default"
ST="$(state "$(trade 2026-10-12), $(trade 2026-10-14), $(trade 2026-10-15)")" ck "[$(tsla)]" "$(cfg '.pdt_guard = true')"; eq "$(V)" '["PASS"]' "older than 5 business days"
ST="$(state "$(trade 2026-10-14), $(trade 2026-10-15), p(\"AMD\")")" ck "[$(tsla)]" "$(cfg '.pdt_guard = true')"; has "$(reasons)" "pdt: 3 day trades" "a pending entry counts"
ST="$(state 'placed("AMD"; "buy"; "2026-10-15"; 46.88; null; "placed"), placed("NVDA"; "buy"; "2026-10-16"; 46.88; null; "placed")')" ck "[$(tsla)]" "$(cfg '.pdt_guard = true')"; eq "$(j '.today.day_trades_5d')" 0 "buys only: no day trades"
# a short entry
SHORT='{"symbol": "TSLA", "side": "sell", "qty": "8", "type": "market", "time_in_force": "day", "order_class": "bracket", "take_profit_limit_price": "358.32", "stop_loss_stop_price": "370.04", "plan": {"entry": 364.18}}'
ck "[$SHORT]" "$(cfg)"; has "$(reasons)" "short: selling more TSLA than you hold" "short off"
ck "[$SHORT]" "$(cfg '.allow_short = true')"; eq "$(j '.results[0] | [.verdict, .kind, .max_loss == 46.88, .reward_risk == 1]')" '["PASS","entry",true,true]' "short allowed"
ck "[$(echo "$SHORT" | jq -c '.stop_loss_stop_price = "360.00"')]" "$(cfg '.allow_short = true')"; has "$(reasons)" "must be above the entry 364.18 for a sell" "short stop above"
# bracket format
ck "[$(tsla 'del(.qty) | .notional = "2900"')]" "$(cfg)"; has "$(reasons)" "whole-share qty" "bracket notional"
ck "[$(tsla '.qty = "8.5"')]" "$(cfg)"; has "$(reasons)" "format: a bracket order takes whole shares only, got qty '8.5'" "bracket whole shares"
ck "[$(tsla '.time_in_force = "ioc"')]" "$(cfg)"; has "$(reasons)" "time_in_force day or gtc, got 'ioc'" "bracket tif"
ck "[$(tsla '.time_in_force = "gtc"')]" "$(cfg)"; eq "$(V)" '["PASS"]' "bracket gtc"
ck "[$(tsla 'del(.take_profit_limit_price)')]" "$(cfg)"; has "$(reasons)" "needs both take_profit_limit_price and stop_loss_stop_price" "bracket legs"
ck "[$(tsla '.stop_loss_stop_price = null | .stop_loss_limit_price = "357.00"')]" "$(cfg)"; has "$(reasons)" "needs a stop_loss_stop_price" "stop limit needs stop"
ck "[$(tsla '.stop_loss_stop_price = 358.32')]" "$(cfg)"; has "$(warns)" 'send stop_loss_stop_price as a string ("358.32")' "as a string"
ck "[$(tsla '.order_class = "oco"')]" "$(cfg)"; has "$(warns)" "advanced order (oco): check every leg" "advanced order warns"
# extended hours
LIM='{"symbol": "KO", "side": "buy", "qty": "1", "type": "limit", "limit_price": "90", "time_in_force": "gtc", "extended_hours": true}'
ON_DAY=$V1_TODAY PXJ="{\"prices\": $PRICES}" ck "[$LIM]" "$(v1)"; eq "$(V)" '["PASS"]' "extended hours gtc limit"
ON_DAY=$V1_TODAY PXJ="{\"prices\": $PRICES}" ck "[$(echo "$LIM" | jq -c '.type = "market" | .limit_price = null')]" "$(v1)"; has "$(reasons)" "extended hours needs a limit order with time_in_force day or gtc" "extended hours market"

# exits: getting out is never blocked by the day limits
PF0='{"cash": 2086.56, "positions": [{"ticker": "TSLA", "shares": 8, "lots": [{"date": "2026-10-19", "shares": 8, "cost": 2913.44}]}]}'
SELL='{"symbol": "TSLA", "side": "sell", "qty": "8", "type": "market", "time_in_force": "day"}'
FULL="$(state 'p("TSLA"), p("AMD"), p("NVDA"), placed("T0"; "buy"; "2026-10-19"; 1; null; "placed"), placed("T1"; "buy"; "2026-10-19"; 1; null; "placed"), placed("T2"; "buy"; "2026-10-19"; 1; null; "placed"), placed("T3"; "buy"; "2026-10-19"; 1; null; "placed"), placed("T4"; "buy"; "2026-10-19"; 1; null; "placed"), placed("T5"; "buy"; "2026-10-19"; 1; null; "placed"), placed("TSLA"; "sell"; "2026-10-19"; null; "exit"; "placed")')"
PF="$PF0" ST="$FULL" NOW_="2026-10-19 15:58" ck "[$SELL]" "$(cfg '.blocked = ["TSLA"] | .max_order_usd = 100 | .max_daily_loss_usd = 50')"
eq "$(j '.results[0] | [.verdict, .kind]')" '["PASS","exit"]' "exit passes"
for w in "blocked:" "duplicate:" "daily limit:"; do has "$(warns)" "exit, not blocked: $w" "exit warns $w"; done
hasnt "$(warns)" "price" "no price complaint for an exit"
PF="$PF0" NOW_="2026-10-17 12:00" ck "[$SELL]" "$(cfg)"; eq "$(V)" '["PASS"]' "exit on a weekend"
HALF=$(echo "$SELL" | jq -c '.qty = "4"')
PF="$PF0" PXJ="$(fresh_px '{"TSLA": 366.0}' 2026-10-19T14:15:00Z)" ck "[$HALF, $HALF]" "$(cfg)"; eq "$(V)" '["PASS","PASS"]' "two partial exits"
eq "$(j '[.results[].kind]')" '["exit","exit"]' "both exits"; has "$(warns 1)" "exit, not blocked: duplicate" "second exit duplicate warning"
PF="$PF0" PXJ="$(fresh_px '{"TSLA": 366.0}' 2026-10-19T14:15:00Z)" ck "[$HALF, $HALF, $HALF]" "$(cfg)"; eq "$(V)" '["PASS","PASS","BLOCK"]' "third is a short"; has "$(reasons 2)" "short:" "third short reason"
PF="$PF0" ck "[$(echo "$SELL" | jq -c '.type = "limit"')]" "$(cfg)"; has "$(reasons)" "needs a limit_price" "exit format"
PF="$PF0" ck "[$(echo "$SELL" | jq -c '.qty = "9"')]" "$(cfg)"; has "$(reasons)" "short: selling more TSLA than you hold (8 shares)" "oversell"
ck "[$SELL]" "$(cfg)"; has "$(reasons)" "record the fill first (ticket fill)" "record the fill first"
ck "[$(tsla), $SELL]" "$(cfg)"; eq "$(V)" '["PASS","PASS"]' "buy then sell"; eq "$(j '.results[1].kind')" '"exit"' "sell is an exit"

# v1's long-term checks (day-trade checks off)
buy() { echo "{\"symbol\": \"$1\", \"side\": \"buy\", \"notional\": \"$2\", \"type\": \"market\", \"time_in_force\": \"day\"}"; }
V1O=$(jq -c '.orders' "$REEL")
ON_DAY=$V1_TODAY ck "$V1O" "$(v1)"; eq "$(V)" '["PASS","PASS","PASS","PASS","PASS"]' "five buys pass"
eq "$(j '[.cash_after == 500, .results[0].value_usd == 1250, .results[0].amount]')" '[true,true,"$1,250"]' "cash after 500"
ON_DAY=$V1_TODAY ck "$(echo "$V1O" | jq -c ". + [$(buy AAPL 100)]")" "$(v1)"; eq "$(V)" '["PASS","PASS","PASS","PASS","PASS","BLOCK"]' "sixth blocked"
for w in "cash:" "position:" "duplicate:"; do has "$(reasons 5)" "$w" "sixth: $w"; done
ck "$V1O" "$(cfg)"; eq "$(j '.results[0].verdict')" '"BLOCK"' "v2 config blocks"; has "$(reasons)" "stop: no stop" "v2 needs a stop"
one() { ON_DAY=$V1_TODAY PXJ="{\"prices\": $PRICES}" ck "[$1]" "$(v1 "${2:-.}")"; }
one "$(buy KO 2500)" '.max_position_pct = 100'; has "$(reasons)" 'over your $2,000 max order' "max order"
one "$(buy KO 2)"; has "$(reasons)" 'under your $5 minimum' "min order"
one "$(buy KO 100)" '.blocked = ["KO"]'; eq "$(V)" '["BLOCK"]' "blocked"; has "$(j '.results[0].reasons[0]')" "blocked list" "blocked first"
OPT='{"symbol": "BE270617C00100000", "side": "buy", "qty": "1", "type": "limit", "limit_price": "12.50", "time_in_force": "day"}'
one "$OPT"; eq "$(V)" '["BLOCK"]' "option blocked"; has "$(reasons)" "option" "option reason"
one "$OPT" '.allow_options = true'; eq "$(V)" '["PASS"]' "option allowed"; eq "$(j '.results[0].warnings | length > 0')" true "option warns"
one '{"symbol": "BTC/USD", "side": "buy", "notional": "50", "type": "market", "time_in_force": "day"}'; has "$(reasons)" "stocks and ETFs only" "crypto"
one "$(buy KO 100 | jq -c '.qty = "1"')"; has "$(reasons)" "exactly one of qty or notional" "both qty and notional"
one "$(buy KO 100 | jq -c '.type = "limit" | .limit_price = "90"')"; has "$(reasons)" "market order with time_in_force day" "notional limit"
one '{"symbol": "KO", "side": "buy", "qty": "1", "type": "limit", "time_in_force": "day"}'; has "$(reasons)" "needs a limit_price" "limit price"
one '{"symbol": "KO", "side": "hold", "notional": "100"}'; has "$(reasons)" "side must be buy or sell, got 'hold'" "bad side"
one '{"symbol": "KO", "side": "buy", "notional": 100, "type": "market", "time_in_force": "day"}'; eq "$(V)" '["PASS"]' "number notional passes"; has "$(j '.results[0].warnings[0]')" "as a string" "number warns"
one '{"symbol": "ZZZZ", "side": "buy", "qty": "2", "type": "market", "time_in_force": "day"}'; has "$(reasons)" "no price for ZZZZ" "no price"
has "$(reasons)" "(trader-alerts: alerts quote ZZZZ --write)" "no price points at alerts"
one '{"symbol": "KO", "side": "buy", "qty": "2", "type": "market", "time_in_force": "day"}'; eq "$(j '.results[0] | [.verdict, .value_usd == 180.22, .entry_ref_from]')" '["PASS",true,"stale price"]' "qty with a price"
PF='{"cash": 1000, "positions": []}' ON_DAY=$V1_TODAY ck "[$(buy KO 1500)]" "$(v1 '.cash_buffer_pct = 0 | .max_position_pct = 100')"; has "$(reasons)" "margin" "margin"
PFKO='{"cash": 500.0, "positions": [{"ticker": "KO", "shares": 7.8571, "lots": [{"date": "2026-10-13", "shares": 7.8571, "cost": 708.0}]}]}'
PF="$PFKO" one '{"symbol": "KO", "side": "sell", "qty": "10", "type": "market", "time_in_force": "day"}'; has "$(reasons)" "short:" "v1 short"
PF="$PFKO" one '{"symbol": "KO", "side": "sell", "qty": "3", "type": "market", "time_in_force": "day"}'; eq "$(V)" '["PASS"]' "v1 sell 3"
PF="$PFKO" ON_DAY=$V1_TODAY PXJ="{\"prices\": $PRICES}" ck "[{\"symbol\": \"KO\", \"side\": \"sell\", \"qty\": \"5\", \"type\": \"market\", \"time_in_force\": \"day\"}, $(buy BAC 450)]" "$(v1 '.max_position_pct = 100')"
eq "$(V)" '["PASS","PASS"]' "a sell frees cash"
PF="$PFKO" ON_DAY=$V1_TODAY PXJ="{\"prices\": $PRICES}" ck "[$(buy BAC 450)]" "$(v1 '.max_position_pct = 100')"; eq "$(V)" '["BLOCK"]' "without the sell"
TEN=$(jq -nc '{orders: [range(10) | {symbol: "T\(.)", side: "buy", status: "placed", at: "2026-10-13T10:00:00"}]}')
ST="$TEN" ON_DAY=$V1_TODAY ck "[$(buy KO 100)]" "$(v1)"; has "$(reasons)" "daily limit" "daily limit"
ST="$TEN" ON_DAY=2026-10-14 ck "[$(buy KO 100)]" "$(v1)"; eq "$(V)" '["PASS"]' "next day"
ST='{"orders": [{"symbol": "KO", "side": "buy", "status": "placed", "at": "2026-10-13T10:00:00"}]}' ON_DAY=$V1_TODAY ck "[$(buy KO 100)]" "$(v1)"; has "$(reasons)" "already placed today" "already placed"
ST='{"orders": [{"symbol": "KO", "side": "buy", "status": "declined", "at": "2026-10-13T10:00:00"}]}' ON_DAY=$V1_TODAY ck "[$(buy KO 100)]" "$(v1)"; eq "$(V)" '["PASS"]' "declined does not count"
ON_DAY=$V1_TODAY ck "[$(buy KO 100)]" "$(v1 '.paper = false')"; has "$(j '.warnings[0]')" "LIVE" "live warns"

# CLI: prices file freshness, v1 files, one-line errors
echo '{"as_of": "2026-10-19T14:55:00Z", "prices": {"TSLA": 366.475}, "detail": {"TSLA": {"time": "2026-10-19T14:52:13Z", "fetched": "2026-10-19T14:55:00Z"}}}' > "$TMP/prices.json"
base=(--orders "$TSLA_F" --config "$CFG0" --portfolio "$TMP/none" --prices "$TMP/prices.json" --state "$TMP/none" --no-write)
run daytrade-risk/check "${base[@]}" --now "2026-10-19 10:55"; eq "$CODE" 1 "fresh price blocks"; has "$OUT" "8 shares x 8.16 risk a share = \$65.28" "fresh price loss"
run daytrade-risk/check "${base[@]}" --now "2026-10-19 11:30"; eq "$CODE" 0 "old price passes"
v1 > "$TMP/v1.json"
run daytrade-risk/check --orders "$REEL" --config "$TMP/v1.json" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/none" --today $V1_TODAY --out "$TMP/s2/risk.json"
eq "$CODE" 0 "v1 cli"; has "$OUT" "5 passed, 0 blocked" "v1 cli footer"
eq "$(jq -c '[.date, ([.results[].fingerprint | length == 16] | all)]' "$TMP/s2/risk.json")" "[\"$V1_TODAY\",true]" "v1 record"
jq -c ".orders += [$(buy AAPL 100)]" "$REEL" > "$TMP/bad.json"
run daytrade-risk/check --orders "$TMP/bad.json" --config "$TMP/v1.json" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/none" --today $V1_TODAY --no-write; eq "$CODE" 1 "v1 sixth blocked cli"
run daytrade-risk/check --orders /nonexistent.json --no-write; eq "$CODE" 1 "no orders"; has "$ERR" "no orders file" "no orders message"
run daytrade-risk/check --orders "$TSLA_F" --now noon --no-write; eq "$CODE" 1 "bad now"; has "$ERR" "--now wants" "bad now message"; hasnt "$ERR" "Traceback" "no traceback"
run daytrade-risk/check --orders "$TSLA_F" --today monday --no-write; eq "$CODE" 1 "bad today"; has "$ERR" "--today wants" "bad today message"
done_ "daytrade-risk/check"

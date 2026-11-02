#!/usr/bin/env bash
# daytrade-budget/size: the assertions of day-trader-skill/tests/test_budget.py, run against the C++ tool.
# size_trade(...) calls become CLI runs with --json (plan, config, prices and portfolio files built with jq); the weights-mode
# unit calls that pass a bare dict get "fractional": true, because the CLI merges the v2 defaults (whole shares).
source "$(dirname "$0")/lib.sh"
B="$FIX/daytrade-budget"; CFG="$FIX/daytrade-risk/config.example.json"; PLAN="$B/plan-tsla.json"; TARGETS="$B/targets.example.json"; TOP5="$B/berkshire-q2-2026-top5.json"
NOW="2026-10-19 10:15"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
V1='{"budget": 5000, "cash_buffer_pct": 10, "max_position_pct": 25, "max_order_usd": 2000, "min_order_usd": 5, "fractional": true, "paper": true, "blocked": []}'
j() { echo "$OUT" | jq -c "$1"; }
# st PLAN_JQ CFG_JQ [args]: size the plan; env PRICE (a fresh price at NOW), CASH (portfolio cash), AT (--now)
st() {
  jq "${1:-.}" "$PLAN" > "$TMP/plan.json"; jq "${2:-.}" "$CFG" > "$TMP/cfg.json"; shift 2
  local px=() pf="$TMP/none"
  [ -n "$PRICE" ] && { jq -n --argjson p "$PRICE" '{as_of: "2026-10-19T14:15:00Z", prices: {TSLA: $p}}' > "$TMP/px.json"; px=(--prices "$TMP/px.json"); }
  [ -n "$CASH" ] && { echo "{\"cash\": $CASH, \"positions\": []}" > "$TMP/pf.json"; pf="$TMP/pf.json"; }
  run daytrade-budget/size --plan "$TMP/plan.json" --config "$TMP/cfg.json" --portfolio "$pf" "${px[@]}" --now "${AT:-$NOW}" --json "$@"
}
S() { echo "$OUT" | jq -c ".sizing | $1"; }
W() { echo "$1" > "$TMP/wc.json"; shift; run daytrade-budget/size --config "$TMP/wc.json" --json "$@"; }   # weights mode with a config
dollars() { echo "$OUT" | jq -c '[.result.positions[] | {(.ticker): (.dollars + 0)}] | add'; }

# the reel: $5,000, 10% buffer, $50 max loss, TSLA risk 5.86 -> 8 shares, $2,913.44, max loss $46.88
eq "$(jq -c 'del(.sec_user_agent) | keys_unsorted' "$CFG")" '["budget","cash_buffer_pct","max_position_pct","max_order_usd","min_order_usd","max_loss_per_trade_usd","max_daily_loss_usd","max_trades_per_day","max_orders_per_day","min_reward_risk","require_stop","no_entries_before","no_entries_after","flat_by","fractional","broker","paper","blocked","allow_options","allow_margin","allow_short","pdt_guard"]' "config example is the contract"
st . .; eq "$CODE" 0 "reel exit"
eq "$(S '[.qty, .bound_by, .risk_per_share, .cost, .max_loss, .reward_risk == 1, .cash_buffer == 500, .refusal]')" '[8,"max loss",5.86,2913.44,46.88,true,true,null]' "8 shares bound by the max loss"
eq "$(S '[.limits[] | [.name, .shares]]')" '[["max loss",8],["position cap",10],["max order",10],["cash",12]]' "limits"
eq "$(S '[.limits[] | select(.binds) | .name]')" '["max loss"]' "binds"
eq "$(j '.orders[0] | del(.why)')" '{"symbol":"TSLA","side":"buy","qty":"8","type":"market","time_in_force":"day","order_class":"bracket","take_profit_limit_price":"370.04","stop_loss_stop_price":"358.32","plan":{"entry":364.18,"stop":358.32,"target":370.04,"risk_per_share":5.86,"max_loss":46.88,"setup":"orb","entry_time":"10:15 AM","flat_by":"15:55"}}' "the bracket order"
has "$(j '.orders[0].why')" 'bound by your $50 max loss' "why: max loss"; has "$(j '.orders[0].why')" '$46.88' "why: amount"
eq "$(j '.orders[0]')" "$(jq -c '.orders[0]' "$FIX/daytrade-risk/orders-tsla.json")" "the fixture orders file is what it writes"
# a limit order
st . . --limit 364.18; eq "$(j '.orders[0] | [.type, .limit_price, .qty]')" '["limit","364.18","8"]' "limit at the entry"
st . . --limit 364.00; eq "$(S '[.risk_per_share, .qty, .max_loss]')" '[5.68,8,45.44]' "limit 364.00"; eq "$(j '.orders[0].limit_price')" '"364.00"' "limit price string"
st . . --limit; eq "$(j '.orders[0].limit_price')" '"364.18"' "--limit alone is the plan's entry"
st . . --limit 0; eq "$CODE" 1 "bad limit"; eq "$ERR" "size: --limit wants a price, like --limit 364.00" "bad limit message"
# the CLI writes one bracket order
run daytrade-budget/size --plan "$PLAN" --config "$CFG" --portfolio "$TMP/none" --now "$NOW" --write "$TMP/trader/orders.json"
eq "$CODE" 0 "cli exit"; has "$OUT" 'Size: 8 shares of TSLA = $2,913.44 at 364.18' "size line"; has "$OUT" '8 x 5.86 = $46.88' "max loss line"
has "$OUT" "<- binds" "binds marker"; has "$OUT" "not financial advice" "advice"; has "$OUT" "Wrote 1 proposed bracket order to $TMP/trader/orders.json" "wrote line"
eq "$(jq -c '[.created_by, .status, .paper, .created_at, (.orders | length), .orders[0].plan.max_loss, .source]' "$TMP/trader/orders.json")" '["trader-budget","proposed",true,"2026-10-19T14:15:00Z",1,46.88,"trader/plan.json (TSLA orb 2026-10-19)"]' "orders file"
eq "$(jq -c 'keys_unsorted' "$TMP/trader/orders.json")" '["created_by","created_at","status","paper","note","source","orders"]' "orders file keys"
eq "$(sed -n 2p <<< "$OUT")" "Stop 358.32: opening-range midpoint. Target 370.04: 1R (one times the risk)." "stop and target line"
eq "$(sed -n 1p <<< "$OUT")" "TSLA buy, opening-range breakout, entry 364.18 at 10:15 AM" "first line"
eq "$(sed -n 6p <<< "$OUT")" '  max loss      $50 max loss / 5.86 risk a share                             8 shares  <- binds' "limit row"
eq "$(sed -n 9p <<< "$OUT")" '  cash          budget $5,000 minus the $500 cash buffer = $4,500 / 364.18  12 shares' "cash row"
has "$OUT" "Flat by 15:55 New York: anything still open then gets closed (trader-alerts rings, trader-broker flatten)." "flat by line"
st . . --limit; eq "$(S .qty)" 8 "json and limit"

# plan rules
st '.stop = 300.0' .; eq "$CODE" 1 "zero shares exit"; eq "$(S .qty)" 0 "zero shares"; has "$(S .refusal)" 'more than your $50 max loss per trade' "refusal"; eq "$(j .orders)" '[]' "no order"
eq "$(S .refusal)" '"one share risks $64.18 (entry 364.18 minus stop 300.00), more than your $50 max loss per trade. No order. Skip this trade, or plan a closer stop with trader-exit."' "refusal wording"
jq '.stop = 300.0' "$PLAN" > "$TMP/p300.json"; run daytrade-budget/size --plan "$TMP/p300.json" --config "$CFG" --portfolio "$TMP/none" --now "$NOW" --write "$TMP/o300.json"
eq "$CODE" 1 "refusal cli exit"; has "$OUT" "No order:" "No order line"; [ -e "$TMP/o300.json" ] && bad "refusal wrote orders" || ok
st . '.max_order_usd = 1000'; eq "$(S .bound_by)" '"max order"' "max order binds"
st . '.max_position_pct = 20'; eq "$(S '[.qty, .bound_by]')" '[2,"position cap"]' "position cap binds"
CASH=1200 st . .; eq "$(S '[.qty, .bound_by, .cash_available == 700]')" '[1,"cash",true]' "cash binds"
has "$(S '.limits[3].how')" 'cash $1,200 in trader/portfolio.json minus the $500 buffer = $700 / 364.18' "cash note"
st . '.max_loss_per_trade_usd = 100'; eq "$(S '[.qty, .bound_by]')" '[10,"position cap"]' "first of the ties binds"
st . '.max_position_pct = 0.1'; eq "$(S '.refusal')" '"one share of TSLA costs $364.18, more than your position cap limit (0.10% of $5,000 = $5 / 364.18). No order."' "one share too dear"
# a fresh higher price means a bigger risk
PRICE=366.475 st . '.min_reward_risk = 0'; eq "$(S '[.entry, .entry_from, .risk_per_share, .qty, .max_loss, .reward_risk, .refusal]')" '[366.48,"price",8.16,6,48.96,0.44,null]' "fresh higher price"
PRICE=366.475 st . .; has "$(S .refusal)" "reward:risk 0.44 (target 370.04, entry 366.48, stop 358.32) is under your minimum 1" "reward:risk refusal"; eq "$(j .orders)" '[]' "no order"
PRICE=363.00 st . .; eq "$(S '[.entry, .qty]')" '[364.18,8]' "lower price ignored"
# the prices file counts only when fresh
jq -n '{as_of: "2026-10-19T14:52:13Z", source: "yahoo", prices: {TSLA: 366.475}, detail: {TSLA: {prev_close: 354.72, time: "2026-10-19T14:52:13Z", source: "yahoo"}}}' > "$TMP/prices.json"
base=(--plan "$PLAN" --config "$CFG" --portfolio "$TMP/none" --prices "$TMP/prices.json" --json)
run daytrade-budget/size "${base[@]}" --now "2026-10-19 10:55"; eq "$CODE$(j '[.sizing.qty, .orders]')" '1[6,[]]' "fresh at 10:55"
eq "$(j '.notes[0]')" "\"TSLA last 366.48 (as of 10:52 New York) is above the plan's entry 364.18, so the risk is bigger: sized at 366.48.\"" "fresh note"
run daytrade-budget/size "${base[@]}" --now "2026-10-19 11:30"; eq "$(j '.sizing.qty')" 8 "stale at 11:30"
eq "$(j '.notes[0]')" "\"TSLA in $TMP/prices.json is from 2026-10-19 10:52 New York: not used (older than 15 minutes). Sized at the plan's entry.\"" "stale note"
run daytrade-budget/size "${base[@]}" --now "2026-10-19 10:51:30"; eq "$(j '.sizing.entry_from')" '"price"' "43 seconds in the future still counts"
run daytrade-budget/size "${base[@]}" --now "2026-10-19 10:51"; eq "$(j '.sizing.entry_from')" '"plan"' "73 seconds in the future does not"
echo '{"prices": {"TSLA": 366.475}}' > "$TMP/undated.json"; run daytrade-budget/size --plan "$PLAN" --config "$CFG" --portfolio "$TMP/none" --prices "$TMP/undated.json" --now "$NOW" --json
has "$(j '.notes[0]')" "is undated: not used" "undated price"
echo '{"as_of": "2026-10-19T14:00:00Z", "prices": {"TSLA": 366.475}, "detail": {"TSLA": {"fetched": "2026-10-19T14:52:00Z"}}}' > "$TMP/fetched.json"; run daytrade-budget/size --plan "$PLAN" --config "$CFG" --portfolio "$TMP/none" --prices "$TMP/fetched.json" --now "2026-10-19 10:55" --json
eq "$(j '.sizing.entry_from')" '"price"' "detail.fetched before as_of"
run daytrade-budget/size "${base[@]}" --now "2026-10-19 10:55" --limit; eq "$(j '[.sizing.entry_from, .notes]')" '["limit",[]]' "a limit order ignores prices"
# the portfolio cash caps the size
echo '{"cash": 1200, "positions": []}' > "$TMP/pf.json"; run daytrade-budget/size --plan "$PLAN" --config "$CFG" --portfolio "$TMP/pf.json" --now "$NOW" --json
eq "$(j '.sizing | [.qty, .bound_by]')" '[1,"cash"]' "portfolio cash"
# bad plans
while IFS='|' read -r q word; do st "$q" .; eq "$CODE" 1 "bad plan $q"; has "$ERR" "$word" "bad plan $q: $word"; done <<'CASES'
.stop = 365.0|not below the entry
.target = 360.0|already happened
.target = null|positive target
.side = "sell"|long (buy) day trades only
.symbol = ""|no symbol
CASES
st '.side = "sell"' .; eq "$ERR" "size: this pack sizes long (buy) day trades only; the plan says 'sell'" "side wording"
PRICE=371.00 st . .; has "$ERR" "already happened" "price past the target"
st . '.blocked = ["tsla"]'; has "$ERR" "TSLA is on your blocked list" "blocked"
AT="2026-10-20 10:15" st . .; eq "$ERR" "size: the plan is for 2026-10-19 and today is 2026-10-20 in New York: make a new plan with trader-exit" "another day's plan"
st . '.budget = null'; has "$ERR" "no budget" "no budget"; st . . --budget 1000; eq "$(S '[.budget == 1000, .qty]')" '[true,2]' "--budget"
st . '.cash_buffer_pct = 100'; has "$ERR" "cash_buffer_pct must be between 0 and 100" "buffer range"
echo '[1]' > "$TMP/list.json"; run daytrade-budget/size --plan "$TMP/list.json" --config "$CFG" --portfolio "$TMP/none" --now "$NOW"; has "$ERR" "the plan is not a JSON object" "plan not an object"
# under the minimum order
st '.symbol = "ABC" | .entry = 4.0 | .stop = 3.0 | .target = 5.0' '.max_loss_per_trade_usd = 1'; eq "$(S .qty)" 1 "one cheap share"
eq "$(S .refusal)" '"1 share of ABC is $4, under your $5 minimum order. No order."' "minimum order refusal"
# missing plan, weights-only flags, a bad --now
run daytrade-budget/size --plan /nonexistent/plan.json --config "$CFG" --now "$NOW"; eq "$CODE" 1 "no plan exit"
eq "$ERR" "size: no plan (run trader-exit first, or use --weights for a split) at /nonexistent/plan.json" "no plan message"
run daytrade-budget/size --plan "$PLAN" --config "$CFG" --now "$NOW" --top 2; eq "$CODE" 1 "--top alone"; has "$ERR" "--weights" "--top needs weights"
run daytrade-budget/size --plan "$PLAN" --config "$CFG" --now tomorrow; eq "$CODE" 1 "bad now"; eq "$ERR" 'size: --now wants a time like "2026-10-19 10:15" (New York) or ...Z (UTC)' "bad now message"

# weights mode (v1's split): $5,000, 10% cash, 25% cap
REEL='{"AAPL":1250,"AXP":1119,"GOOGL":823,"KO":708,"BAC":600}'
reel() { eq "$(dollars)" "$REEL" "$1: dollars"; eq "$(j '.result | [.cash_buffer == 500, .invested == 4500, .idle == 0, .cap == 1250, [.positions[] | select(.capped) | .ticker]]')" '[true,true,true,true,["AAPL"]]' "$1: buffer, invested, idle, cap"; }
W "$V1" --targets "$TOP5"; reel "raw weights"
W "$V1" --targets "$TARGETS"; reel "weights file"
W "$V1" --weights AAPL=22.04,AXP=17.14,GOOGL=12.62,KO=10.86,BAC=9.20; reel "inline weights"
W "$V1" --targets "$TOP5"; eq "$(j '[.result.positions[] | {(.ticker): (.weight * 10 | round / 10)}] | add')" '{"AAPL":30.7,"AXP":23.9,"GOOGL":17.6,"KO":15.1,"BAC":12.8}' "renormalised weights"
W "$V1" --weights AAPL=30.7,AXP=23.9,GOOGL=17.6,KO=15.1,BAC=12.8; eq "$(j '[.result.invested == 4500, (.result.positions[0].dollars == 1250)]')" '[true,true]' "one-decimal weights"
eq "$(echo "$OUT" | jq --argjson r "$REEL" '[.result.positions[] | ((.dollars - $r[.ticker]) | fabs) <= 1] | all')" true "within a dollar"
echo "{\"weights\": $(jq -c '.weights' "$TOP5"), \"source\": \"a 13F\"}" > "$TMP/src.json"; W "$V1" --targets "$TMP/src.json"
eq "$(j '.orders[0] | del(.why)')" '{"symbol":"AAPL","side":"buy","notional":"1250","type":"market","time_in_force":"day"}' "order shape"
has "$(j '.orders[0].why')" "capped at 25%" "capped why"; has "$(j '.orders[0].why')" "% of the split, weights from a 13F" "why wording"
eq "$(j '[.orders[].notional]')" '["1250","1119","823","708","600"]' "notionals"; eq "$(j '[.orders[] | has("qty")] | any')" false "no qty"
W "$V1" --targets "$TOP5" --cents; eq "$(j '[.result.positions[0].dollars == 1250, ([.result.positions[].dollars] | add * 100 | round) == 450000, .result.positions[1].dollars, .orders[1].notional]')" '[true,true,1118.13,"1118.13"]' "cents mode"
W '{"budget": 1000, "cash_buffer_pct": 0, "max_position_pct": 40, "min_order_usd": 1, "fractional": true}' --weights A=60,B=30,C=10
eq "$(dollars)" '{"A":400,"B":400,"C":200}' "cap redistributes"; eq "$(j '[.result.positions[] | select(.capped) | .ticker] | sort')" '["A","B"]' "two capped"
W "$V1" --weights AAPL=50,KO=50; eq "$(dollars)" '{"AAPL":1250,"KO":1250}' "everything capped"; eq "$(j '.result.idle == 2000')" true "idle 2000"
W "$V1" --weights AAPL=50,KO=50 --json; run daytrade-budget/size --config "$TMP/wc.json" --weights AAPL=50,KO=50; has "$OUT" "every stock is at its cap" "idle reason"
W '{"budget": 100, "cash_buffer_pct": 10, "max_position_pct": 100, "min_order_usd": 5, "fractional": true}' --weights A=90,B=7,C=3
eq "$(j '[(.result.positions | map(.ticker) | index("C")), ([.result.positions[].dollars] | add), ([.result.positions[].dollars >= 5] | all), .result.dropped[0].ticker]')" '[null,90,true,"C"]' "small slices dropped"
W "$(echo "$V1" | jq -c '.blocked = ["ko"]')" --targets "$TOP5"; eq "$(j '[(.result.positions | map(.ticker) | index("KO")), .result.invested == 4500, (.result.dropped[0].reason | test("blocked"))]')" '[null,true,true]' "blocked dropped first"
W "$(echo "$V1" | jq -c '.max_position_pct = 100')" --targets "$TOP5" --top 2; eq "$(j '[.result.positions[].ticker] | sort')" '["AAPL","AXP"]' "--top 2"
W "$(echo "$V1" | jq -c '.fractional = false')" --targets "$TOP5" --prices "$TMP/none.json"; eq "$CODE" 1 "whole shares need prices"
run daytrade-budget/size --config "$CFG" --targets "$TOP5" --prices "$TMP/none.json"; eq "$CODE" 1 "the v2 example config is whole shares"
echo '{"prices": {"AAPL": 341.27, "AXP": 309.84, "GOOGL": 353.78, "KO": 90.11, "BAC": 53.96}}' > "$TMP/p5.json"
W "$(echo "$V1" | jq -c '.fractional = false')" --targets "$TOP5" --prices "$TMP/p5.json"
eq "$(echo "$OUT" | jq --slurpfile p "$TMP/p5.json" '[.result.whole_shares, ([.result.positions[] | (.shares | floor) == .shares and ((.dollars - .shares * $p[0].prices[.ticker]) | fabs) < 0.005 and .dollars <= 1250] | all), (((.result.invested + .result.idle) - 4500) | fabs) < 0.005, .result.idle < 107.92, ([.orders[] | has("qty") and (has("notional") | not)] | all)]' | jq -c .)" '[true,true,true,true,true]' "whole shares with prices"
W '{"cash_buffer_pct": 10, "fractional": true}' --targets "$TOP5"; eq "$CODE" 1 "no budget"
echo '{"weights": [{"ticker": "KO", "weight": 1}, {"ticker": "KO", "weight": 2}]}' > "$TMP/dup.json"; W "$V1" --targets "$TMP/dup.json"; eq "$CODE" 1 "duplicates"
# the weights CLI
echo "$V1" > "$TMP/v1.json"
run daytrade-budget/size --targets "$TARGETS" --config "$TMP/v1.json" --write "$TMP/w/trader/orders.json"; eq "$CODE" 0 "weights cli"
has "$OUT" "left idle" "left idle row"; has "$OUT" "require_stop" "no-stop warning"; has "$OUT" "Weights from: " "source line"
has "$OUT" '  buy AAPL notional "1250", market, day' "order line"; has "$OUT" "Wrote 5 proposed orders to $TMP/w/trader/orders.json" "wrote line"
eq "$(jq -c '[.orders[].symbol]' "$TMP/w/trader/orders.json")" '["AAPL","AXP","GOOGL","KO","BAC"]' "weights orders file"
eq "$(jq -r '.created_at | test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")' "$TMP/w/trader/orders.json")" true "created_at in UTC"
run daytrade-budget/size --targets "$TARGETS" --config "$TMP/v1.json" --now "2026-10-19 10:15" --write "$TMP/w2.json"; eq "$(jq -r .created_at "$TMP/w2.json")" "2026-10-19T14:15:00Z" "weights --now"
run daytrade-budget/size --targets "$TARGETS" --config "$TMP/v1.json" --json; eq "$(j '.result.idle == 0')" true "json idle"
run daytrade-budget/size --targets /nonexistent/targets.json --config "$TMP/v1.json"; eq "$CODE" 1 "no weights file"; has "$ERR" "no weights file" "no weights file message"
done_ "daytrade-budget/size"

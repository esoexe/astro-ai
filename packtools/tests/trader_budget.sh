#!/usr/bin/env bash
# trader-budget/size: the assertions of trader-skill/tests/test_budget.py, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
D="$FIX/trader-budget"; CFG="$D/config.example.json"; TARGETS="$D/targets.example.json"; TOP5="$D/berkshire-q2-2026-top5.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
REEL='{"AAPL":1250.0,"AXP":1119.0,"GOOGL":823.0,"KO":708.0,"BAC":600.0}'
echo '{"prices": {"AAPL": 341.27, "AXP": 309.84, "GOOGL": 353.78, "KO": 90.11, "BAC": 53.96}}' > "$TMP/px.json"
cfgw() { jq "${1:-.}" "$CFG" > "$TMP/c.json"; echo "$TMP/c.json"; }
sz() { run trader-budget/size --json "$@"; }
dollars() { echo "$OUT" | jq -c '[.result.positions[] | {(.ticker): .dollars}] | add'; }
check_reel() {
  eq "$(dollars)" "$REEL" "$1: dollars"
  eq "$(echo "$OUT" | jq -c '[.result.cash_buffer, .result.invested, .result.idle, .result.cap]')" '[500.0,4500.0,0.0,1250.0]' "$1: buffer/invested/idle/cap"
  eq "$(echo "$OUT" | jq -c '[.result.positions[] | select(.capped) | .ticker]')" '["AAPL"]' "$1: AAPL capped"
}
sz --targets "$TOP5" --config "$CFG"; check_reel "raw 13F weights"
sz --targets "$TARGETS" --config "$CFG"; check_reel "renormalised targets"
sz --weights AAPL=22.04,AXP=17.14,GOOGL=12.62,KO=10.86,BAC=9.20 --config "$CFG"; check_reel "inline weights"
sz --targets "$TOP5" --config "$CFG"
eq "$(echo "$OUT" | jq -c '[.result.positions[] | {(.ticker): ((.weight * 10 | round) / 10)}] | add')" '{"AAPL":30.7,"AXP":23.9,"GOOGL":17.6,"KO":15.1,"BAC":12.8}' "weights match the reel labels"
sz --weights AAPL=30.7,AXP=23.9,GOOGL=17.6,KO=15.1,BAC=12.8 --config "$CFG"
eq "$(echo "$OUT" | jq '.result.invested')" 4500.0 "1-decimal weights invest 4500"
eq "$(echo "$OUT" | jq --argjson r "$REEL" '[.result.positions[] | (.dollars - $r[.ticker]) | fabs <= 1] | all')" true "within a dollar"
# orders are place_stock_order shaped
sz --targets "$TOP5" --config "$CFG"
eq "$(echo "$OUT" | jq -c '.orders[0] | del(.why)')" '{"symbol":"AAPL","side":"buy","notional":"1250","type":"market","time_in_force":"day"}' "order shape"
has "$(echo "$OUT" | jq -r '.orders[0].why')" "capped at 25%" "capped why"
eq "$(echo "$OUT" | jq -c '[.orders[].notional]')" '["1250","1119","823","708","600"]' "notionals are strings"
eq "$(echo "$OUT" | jq '[.orders[] | has("qty")] | any')" false "no qty"
eq "$(echo "$OUT" | jq -r '.orders[1].why')" "23.85% of the stock sleeve, copying $(jq -r .source "$TOP5")" "why with source"
# cents keep the total
sz --targets "$TOP5" --config "$CFG" --cents
eq "$(echo "$OUT" | jq -c '[.result.positions[0].dollars, ([.result.positions[].dollars] | add * 100 | round), (.result.positions[] | select(.ticker=="AXP") | .dollars), .orders[1].notional]')" '[1250.0,450000,1118.13,"1118.13"]' "cents mode"
# rules
echo '{"budget": 1000, "cash_buffer_pct": 0, "max_position_pct": 40, "min_order_usd": 1}' > "$TMP/r.json"
sz --weights A=60,B=30,C=10 --config "$TMP/r.json"; eq "$(dollars)" '{"A":400.0,"B":400.0,"C":200.0}' "cap redistributes"
eq "$(echo "$OUT" | jq -c '[.result.positions[] | select(.capped) | .ticker] | sort')" '["A","B"]' "A and B capped"
sz --weights AAPL=50,KO=50 --config "$CFG"; eq "$(dollars)" '{"AAPL":1250.0,"KO":1250.0}' "all capped"; eq "$(echo "$OUT" | jq .result.idle)" 2000.0 "idle 2000"
run trader-budget/size --weights AAPL=50,KO=50 --config "$CFG"; has "$OUT" "every stock is at its cap" "idle explained"; has "$OUT" "Left idle \$2,000: every stock is at its cap; add stocks or raise max_position_pct" "idle line"
echo '{"budget": 100, "cash_buffer_pct": 10, "max_position_pct": 100, "min_order_usd": 5}' > "$TMP/r.json"
sz --weights A=90,B=7,C=3 --config "$TMP/r.json"
eq "$(echo "$OUT" | jq '.result.positions | map(.ticker) | index("C")')" null "C dropped"
eq "$(echo "$OUT" | jq '[.result.positions[].dollars] | add')" 90 "sum 90"
eq "$(echo "$OUT" | jq '[.result.positions[].dollars >= 5] | all')" true "all >= 5"
eq "$(echo "$OUT" | jq -r '.result.dropped[0].ticker + ": " + .result.dropped[0].reason')" 'C: its slice ($3) is under your $5 minimum order' "drop reason"
sz --targets "$TOP5" --config "$(cfgw '.blocked = ["ko"]')"
eq "$(echo "$OUT" | jq '.result.positions | map(.ticker) | index("KO")')" null "KO blocked"; eq "$(echo "$OUT" | jq .result.invested)" 4500.0 "blocked still invests 4500"
has "$(echo "$OUT" | jq -r '.result.dropped[0].reason')" "blocked" "blocked reason"
sz --targets "$TOP5" --config "$(cfgw '.max_position_pct = 100')" --top 2; eq "$(echo "$OUT" | jq -c '[.result.positions[].ticker] | sort')" '["AAPL","AXP"]' "top 2"
has "$(echo "$OUT" | jq -r '.result.dropped[0].reason')" "outside the top 2" "top reason"
sz --targets "$TOP5" --config "$(cfgw '.fractional = false')" --prices "$TMP/none.json"; eq "$CODE" 1 "whole shares need a prices file"
echo '{"prices": {}}' > "$TMP/empty.json"
sz --targets "$TOP5" --config "$(cfgw '.fractional = false')" --prices "$TMP/empty.json"; eq "$CODE" 1 "whole shares need prices"; has "$ERR" "size: whole shares need a price for: AAPL, AXP, GOOGL, KO, BAC" "whole shares message"
sz --targets "$TOP5" --config "$(cfgw '.fractional = false')" --prices "$TMP/px.json"
eq "$(echo "$OUT" | jq .result.whole_shares)" true "whole_shares flag"
eq "$(echo "$OUT" | jq --slurpfile px "$TMP/px.json" '[.result.positions[] | (.shares | type == "number" and . == floor) and ((.dollars - .shares * $px[0].prices[.ticker]) | fabs < 0.005) and .dollars <= 1250] | all')" true "whole share positions"
eq "$(echo "$OUT" | jq '((.result.invested + .result.idle) - 4500 | fabs) < 0.005 and .result.idle < 53.96 * 2')" true "whole shares total and idle"
eq "$(echo "$OUT" | jq '[.orders[] | has("qty") and (has("notional") | not)] | all')" true "whole share orders use qty"
echo '{"cash_buffer_pct": 10}' > "$TMP/nb.json"; sz --targets "$TOP5" --config "$TMP/nb.json"; eq "$CODE" 1 "no budget"; has "$ERR" "size: no budget" "no budget message"
echo '[{"ticker": "KO", "name": "", "weight": 1}, {"ticker": "KO", "name": "", "weight": 2}]' > "$TMP/dup.json"
sz --targets "$TMP/dup.json" --config "$CFG"; eq "$CODE" 1 "duplicates"; eq "$ERR" "size: KO is listed twice in the weights" "duplicate message"
# CLI
run trader-budget/size --targets "$TARGETS" --config "$CFG" --write "$TMP/trader/orders.json"
eq "$CODE" 0 "write exit 0"; has "$OUT" "left idle" "left idle row"; has "$OUT" "not financial advice" "disclaimer"
has "$OUT" "Wrote 5 proposed orders to $TMP/trader/orders.json" "wrote line"
eq "$(jq -c '[.status, .paper, [.orders[].symbol], .created_by]' "$TMP/trader/orders.json")" '["proposed",true,["AAPL","AXP","GOOGL","KO","BAC"],"trader-budget"]' "orders file"
sz --targets "$TARGETS" --config "$CFG"; eq "$CODE" 0 "json exit"; eq "$(echo "$OUT" | jq .result.idle)" 0.0 "json idle 0"
run trader-budget/size --targets /nonexistent/targets.json --config "$CFG"; eq "$CODE" 1 "missing targets"; has "$ERR" "no targets file" "missing targets message"
run trader-budget/size --weights AAPL --config "$CFG"; eq "$ERR" "size: --weights wants TICKER=WEIGHT pairs, got 'AAPL'" "bad --weights"
run trader-budget/size --weights AAPL=x --config "$CFG"; eq "$ERR" "size: weight for AAPL is not a number: 'x'" "non-number weight"
done_ "trader-budget/size"

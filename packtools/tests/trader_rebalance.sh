#!/usr/bin/env bash
# trader-rebalance/rebalance: the assertions of trader-skill/tests/test_rebalance.py, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
D="$FIX/trader-rebalance"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
# setup CASH_ADD [prices-jq] [config-jq] [portfolio-jq]  -> writes $TMP/{pf,px,cfg}.json
setup() {
  jq ".cash += ${1:-0} | ${4:-.}" "$D/portfolio.json" > "$TMP/pf.json"
  jq "${2:-.}" "$D/prices.json" > "$TMP/px.json"
  jq "${3:-.}" "$D/config.json" > "$TMP/cfg.json"
}
rb() { run trader-rebalance/rebalance --portfolio "$TMP/pf.json" --prices "$TMP/px.json" --targets "$D/targets.json" --config "$TMP/cfg.json" --json "$@"; }
orders() { echo "$OUT" | jq -c '[.orders[] | [.side, .symbol, (.notional // .qty)]]'; }

# on the day it was bought nothing trades
setup; rb
eq "$(echo "$OUT" | jq -c .orders)" '[]' "day 0: no orders"; eq "$CODE" 1 "day 0 exit 1"
eq "$(echo "$OUT" | jq -c '[.rows[].ticker]')" '["AAPL","AXP","GOOGL","KO","BAC"]' "row order by weight"
eq "$(echo "$OUT" | jq '[.rows[].drift | fabs < 0.1] | all')" true "drift under 0.1"
eq "$(echo "$OUT" | jq '.rows[0].target_pct')" 25.0 "AAPL target 25 (capped)"
# new cash buys only what left the band
setup 2000; rb
eq "$(echo "$OUT" | jq .cap)" 1750.0 "cap 1750"
eq "$(orders)" '[["buy","AAPL","499"],["buy","AXP","446"]]' "2000 deposit buys"
eq "$(echo "$OUT" | jq -c '[.rows[] | select(.action=="hold") | .ticker]')" '["GOOGL","KO","BAC"]' "the rest hold"
# bigger deposit buys everything and keeps the buffer
setup 5000; rb
eq "$(echo "$OUT" | jq -c '[.orders[].symbol]')" '["AAPL","AXP","GOOGL","KO","BAC"]' "5000 deposit buys all"
eq "$(echo "$OUT" | jq '.cash_after >= .cash_buffer')" true "buffer kept"
run trader-rebalance/rebalance --portfolio "$TMP/pf.json" --prices "$TMP/px.json" --targets "$D/targets.json" --config "$TMP/cfg.json" --write "$TMP/o5000.json"
run trader-risk/check --orders "$TMP/o5000.json" --config "$TMP/cfg.json" --portfolio "$TMP/pf.json" --prices "$TMP/px.json" --state "$TMP/none" --today 2026-10-13 --no-write --json
eq "$(echo "$OUT" | jq .block)" 0 "trader-risk passes every rebalance order"
# sells come before buys
setup 0 '.prices.AAPL = 341.27 * 1.6'; rb
eq "$(echo "$OUT" | jq -r '.orders[0].side + " " + .orders[0].symbol')" "sell AAPL" "sell first"
eq "$(echo "$OUT" | jq -c '[.orders[].side] == ([.orders[].side] | sort_by(. != "sell"))')" true "sells before buys"
eq "$(echo "$OUT" | jq '(.orders[0].qty | tonumber) <= 3.6628')" true "sell qty <= held"
has "$(echo "$OUT" | jq -r '.orders[0].why')" "drifted" "why says drifted"
# band
setup 2000; rb --band 8; eq "$(echo "$OUT" | jq '.orders | length')" 0 "band 8: none"
rb --band 3; eq "$(echo "$OUT" | jq '.orders | length')" 5 "band 3: five"
# untracked holdings are left alone unless asked
setup 0 '.prices.DHI = 150.0' . '.positions += [{"ticker":"DHI","shares":2}]'; rb
eq "$(echo "$OUT" | jq -r '.rows[] | select(.ticker=="DHI") | .action')" "untracked, left alone" "DHI left alone"
eq "$(echo "$OUT" | jq '[.orders[].symbol] | index("DHI")')" null "no DHI order"
rb --sell-untracked; eq "$(orders | jq -c 'index([["sell","DHI","2"]]) != null')" true "sell-untracked sells DHI 2"
# missing price is an error
setup 0 'del(.prices.KO)'; rb; eq "$CODE" 1 "missing price exit 1"; has "$ERR" "rebalance: no price for KO" "missing price message"
# whole shares
setup 5000 . '.fractional = false'; rb
eq "$(echo "$OUT" | jq '[.orders[] | (.qty | test("^[0-9]+$")) and (has("notional") | not)] | all')" true "whole shares only"
# max order and scaling
setup 20000; rb
eq "$(echo "$OUT" | jq '[.notes[] | test("max order")] | any')" true "max order note"
run trader-rebalance/rebalance --portfolio "$TMP/pf.json" --prices "$TMP/px.json" --targets "$D/targets.json" --config "$TMP/cfg.json"
eq "$(echo "$OUT" | grep -o 'about \$[0-9,.]*)' | tr -d '$,)' | sed 's/about //' | awk '$1>2000' | wc -l)" 0 "no order over 2000"
setup 5000 '.prices.DHI = 150.0' . '.positions += [{"ticker":"DHI","shares":10}]'; rb
eq "$(echo "$OUT" | jq '[.notes[] | test("buys scaled")] | any')" true "buys scaled note"
eq "$(echo "$OUT" | jq '.orders | length')" 5 "five scaled orders"
eq "$(echo "$OUT" | jq '.cash_after >= .cash_buffer - 1')" true "buffer kept after scaling"
# CLI: write and exit codes
setup 2000
run trader-rebalance/rebalance --portfolio "$TMP/pf.json" --write "$TMP/out/orders.json" --prices "$D/prices.json" --targets "$D/targets.json" --config "$D/config.json"
eq "$CODE" 0 "write exit 0"; has "$OUT" "not financial advice" "disclaimer"; has "$OUT" "Wrote 2 proposed order(s) to $TMP/out/orders.json." "wrote line"
eq "$(jq -r .created_by "$TMP/out/orders.json")" trader-rebalance "created_by"
eq "$(jq '[.orders[] | keys[] | startswith("_")] | any' "$TMP/out/orders.json")" false "no private keys written"
eq "$(jq -r '.created_at | test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}$")' "$TMP/out/orders.json")" true "created_at iso"
eq "$(jq -c '[.status, .paper, .prices_as_of]' "$TMP/out/orders.json")" '["proposed",true,"2026-10-13T20:00:01Z"]' "status/paper/as_of"
has "$OUT" "  buy AAPL notional \"499\", market, day  (about \$499)" "order line format"
has "$OUT" "Account \$6,999.99 (cash \$2,500, buffer \$700, cap \$1,750 per stock), band 5 points" "account line"
run trader-rebalance/rebalance --portfolio "$D/portfolio.json" --prices "$D/prices.json" --targets "$D/targets.json" --config "$D/config.json"; eq "$CODE" 1 "nothing to do exit 1"
run trader-rebalance/rebalance --portfolio "$TMP/none.json" --prices "$D/prices.json" --targets "$D/targets.json" --config "$D/config.json"
eq "$CODE" 1 "no portfolio exit 1"; has "$ERR" "no portfolio" "no portfolio message"
echo '{"weights": [],}' > "$TMP/bad.json"
run trader-rebalance/rebalance --portfolio "$D/portfolio.json" --prices "$D/prices.json" --targets "$TMP/bad.json" --config "$D/config.json"
eq "$ERR" "rebalance: $TMP/bad.json is not valid JSON: Illegal trailing comma before end of object: line 1 column 15 (char 14)" "bad json wording"
done_ "trader-rebalance/rebalance"

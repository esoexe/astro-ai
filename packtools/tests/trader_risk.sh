#!/usr/bin/env bash
# trader-risk/check: the assertions of trader-skill/tests/test_risk.py, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
D="$FIX/trader-risk"; CFG="$D/config.example.json"; REEL="$D/orders-reel.json"; TODAY=2026-10-13
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
PRICES='{"AAPL": 341.27, "AXP": 309.84, "GOOGL": 353.78, "KO": 90.11, "BAC": 53.96}'
buy() { printf '{"symbol":"%s","side":"buy","notional":"%s","type":"market","time_in_force":"day"}' "$1" "$2"; }
# chk ORDERS-JSON-ARRAY [config-jq] [portfolio-json|-] [prices-json|-] [state-json|-] [today]
chk() {
  echo "{\"orders\": $1}" > "$TMP/o.json"; jq "${2:-.}" "$CFG" > "$TMP/c.json"
  local pf="$TMP/none" px="$TMP/none" st="$TMP/none"
  [ "${3:--}" != - ] && { echo "$3" > "$TMP/pf.json"; pf="$TMP/pf.json"; }
  [ "${4:--}" != - ] && { echo "{\"prices\": $4}" > "$TMP/px.json"; px="$TMP/px.json"; }
  [ "${5:--}" != - ] && { echo "$5" > "$TMP/st.json"; st="$TMP/st.json"; }
  run trader-risk/check --orders "$TMP/o.json" --config "$TMP/c.json" --portfolio "$pf" --prices "$px" --state "$st" --today "${6:-$TODAY}" --no-write --json
}
verdicts() { echo "$OUT" | jq -c '[.results[].verdict]'; }
why() { echo "$OUT" | jq -r ".results[${1:-0}].reasons | join(\" | \")"; }
REELO=$(jq -c .orders "$REEL")

# the reel: five buys pass on a fresh 5000 paper account
chk "$REELO"
eq "$(verdicts)" '["PASS","PASS","PASS","PASS","PASS"]' "reel passes"; eq "$CODE" 0 "reel exit 0"
eq "$(echo "$OUT" | jq -c '[.cash_after, .paper, .results[0].value_usd, .results[0].amount]')" '[500.0,true,1250.0,"$1,250"]' "reel numbers"
# a sixth buy is blocked three ways
chk "$(echo "$REELO" | jq -c ". + [$(buy AAPL 100)]")"
eq "$(verdicts)" '["PASS","PASS","PASS","PASS","PASS","BLOCK"]' "sixth blocked"; eq "$CODE" 1 "block exit 1"
for k in "cash:" "position:" "duplicate:"; do has "$(why 5)" "$k" "sixth: $k"; done
# fingerprint = sha256 of the sorted compact JSON of the Alpaca fields (independent oracle: jq -cS + sha256sum)
chk "$REELO"
want=$(jq -cS '.orders[0] | with_entries(select(.key as $k | ["symbol","side","qty","notional","type","time_in_force","limit_price","stop_price","trail_price","trail_percent","extended_hours","client_order_id","order_class","take_profit_limit_price","stop_loss_stop_price","stop_loss_limit_price","advanced_instructions"] | index($k)) | select(.value != null))' "$REEL" | tr -d '\n' | sha256sum | cut -c1-16)
eq "$(echo "$OUT" | jq -r '.results[0].fingerprint')" "$want" "fingerprint matches sha256(sorted compact JSON)"
f0=$(echo "$OUT" | jq -r '.results[0].fingerprint')
chk "$(echo "$REELO" | jq -c '.[0].why = "anything" | [.[0]]')"; eq "$(echo "$OUT" | jq -r '.results[0].fingerprint')" "$f0" "fingerprint ignores why"
chk "$(echo "$REELO" | jq -c '.[0].notional = "1251" | [.[0]]')"; [ "$(echo "$OUT" | jq -r '.results[0].fingerprint')" != "$f0" ] && ok || bad "fingerprint changes with notional"
# limits
chk "[$(buy KO 2500)]" '.max_position_pct = 100' - "$PRICES"; has "$(why)" 'over your $2,000 max order' "max order"
chk "[$(buy KO 2)]" . - "$PRICES"; has "$(why)" 'under your $5 minimum' "min order"
chk "[$(buy KO 100)]" '.blocked = ["KO"]' - "$PRICES"; eq "$(verdicts)" '["BLOCK"]' "blocked verdict"; has "$(echo "$OUT" | jq -r '.results[0].reasons[0]')" "blocked list" "blocked reason first"
OPT='{"symbol": "BE270617C00100000", "side": "buy", "qty": "1", "type": "limit", "limit_price": "12.50", "time_in_force": "day"}'
chk "[$OPT]" . - "$PRICES"; eq "$(verdicts)" '["BLOCK"]' "option blocked"; has "$(why)" "option" "option reason"
chk "[$OPT]" '.allow_options = true' - "$PRICES"; eq "$(verdicts)" '["PASS"]' "option allowed"
eq "$(echo "$OUT" | jq -r '.results[0].warnings[0]')" "option order: options can lose 100%% of what you pay" "option warning (verbatim)"
chk '[{"symbol": "BTC/USD", "side": "buy", "notional": "50", "type": "market", "time_in_force": "day"}]' . - "$PRICES"; has "$(why)" "stocks and ETFs only" "crypto refused"
chk "[$(buy KO 100 | jq -c '.qty = "1"')]" . - "$PRICES"; has "$(why)" "exactly one of qty or notional" "qty and notional"
chk "[$(buy KO 100 | jq -c '.type = "limit" | .limit_price = "90"')]" . - "$PRICES"; has "$(why)" "market order with time_in_force day" "notional limit"
chk '[{"symbol": "KO", "side": "buy", "qty": "1", "type": "limit", "time_in_force": "day"}]' . - "$PRICES"; has "$(why)" "needs a limit_price" "limit price needed"
chk '[{"symbol": "KO", "side": "hold", "notional": "100"}]' . - "$PRICES"; has "$(why)" "side must be buy or sell, got 'hold'" "bad side (repr)"
chk '[{"symbol": "KO", "side": "buy", "notional": 100, "type": "market", "time_in_force": "day"}]' . - "$PRICES"
eq "$(verdicts)" '["PASS"]' "number notional passes"; eq "$(echo "$OUT" | jq -r '.results[0].warnings[0]')" 'send notional as a string ("100"), the way place_stock_order types it' "as a string warning"
chk '[{"symbol": "ZZZZ", "side": "buy", "qty": "2", "type": "market", "time_in_force": "day"}]' . - "$PRICES"; has "$(why)" "no price for ZZZZ to size a 2-share order" "share order needs price"
chk '[{"symbol": "KO", "side": "buy", "qty": "2", "type": "market", "time_in_force": "day"}]' . - "$PRICES"; eq "$(echo "$OUT" | jq -c '[.results[0].verdict, .results[0].value_usd]')" '["PASS",180.22]' "KO 2 shares = 180.22"
chk "[$(buy KO 1500)]" '.cash_buffer_pct = 0 | .max_position_pct = 100' '{"cash": 1000, "positions": []}'; has "$(why)" "margin" "margin"
# account
PF='{"cash": 500.0, "positions": [{"ticker": "KO", "shares": 7.8571, "lots": [{"date": "2026-10-13", "shares": 7.8571, "cost": 708.0}]}]}'
chk '[{"symbol": "KO", "side": "sell", "qty": "10", "type": "market", "time_in_force": "day"}]' . "$PF" "$PRICES"; has "$(why)" "short: selling more KO than you hold (7.8571 shares)" "short blocked"
chk '[{"symbol": "KO", "side": "sell", "qty": "3", "type": "market", "time_in_force": "day"}]' . "$PF" "$PRICES"; eq "$(verdicts)" '["PASS"]' "sell 3 passes"
chk "[{\"symbol\": \"KO\", \"side\": \"sell\", \"qty\": \"5\", \"type\": \"market\", \"time_in_force\": \"day\"}, $(buy BAC 450)]" '.max_position_pct = 100' "$PF" "$PRICES"; eq "$(verdicts)" '["PASS","PASS"]' "sell frees cash"
chk "[$(buy BAC 450)]" '.max_position_pct = 100' "$PF" "$PRICES"; eq "$(verdicts)" '["BLOCK"]' "buy alone blocked"
PLACED=$(jq -nc --arg t "$TODAY" '{orders: [range(10) | {symbol: "T\(.)", side: "buy", status: "placed", at: ($t + "T10:00:00")}]}')
chk "[$(buy KO 100)]" . - - "$PLACED"; has "$(why)" "daily limit: 10 placed today plus 0 passed earlier in this batch, your limit is 10 a day" "daily limit"
chk "[$(buy KO 100)]" . - - "$PLACED" 2026-10-14; eq "$(verdicts)" '["PASS"]' "next day passes"
chk "[$(buy KO 100)]" . - - "{\"orders\": [{\"symbol\": \"KO\", \"side\": \"buy\", \"status\": \"placed\", \"at\": \"${TODAY}T10:00:00\"}]}"; has "$(why)" "already placed today" "already placed"
chk "[$(buy KO 100)]" . - - "{\"orders\": [{\"symbol\": \"KO\", \"side\": \"buy\", \"status\": \"declined\", \"at\": \"${TODAY}T10:00:00\"}]}"; eq "$(verdicts)" '["PASS"]' "declined does not count"
chk "[$(buy KO 100)]" '.paper = false'; has "$(echo "$OUT" | jq -r '.warnings[0]')" "LIVE" "live warning"
# CLI
run trader-risk/check --orders "$REEL" --config "$CFG" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/none" --today $TODAY --out "$TMP/state/risk.json"
eq "$CODE" 0 "cli exit 0"; has "$OUT" "5 passed, 0 blocked. Cash after the passed orders: \$500 (paper account)." "summary line"; has "$OUT" "not financial advice" "disclaimer"
has "$OUT" "PASS  #1 buy AAPL \$1,250" "verdict line format"
eq "$(jq -c '[.date, (.results | length), ([.results[].fingerprint | length == 16] | all), .orders_file]' "$TMP/state/risk.json")" "[\"$TODAY\",5,true,\"$REEL\"]" "risk.json written"
eq "$(jq -c 'keys_unsorted' "$TMP/state/risk.json")" '["date","paper","results","pass","block","warnings","cash_after","checked_at","orders_file"]' "risk.json key order"
echo "{\"orders\": $(echo "$REELO" | jq -c ". + [$(buy AAPL 100)]")}" > "$TMP/bad.json"
run trader-risk/check --orders "$TMP/bad.json" --config "$CFG" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/none" --today $TODAY --no-write; eq "$CODE" 1 "blocked exit 1"
run trader-risk/check --orders /nonexistent.json --no-write; eq "$CODE" 1 "missing orders exit 1"; has "$ERR" "check: no orders file" "missing orders message"
done_ "trader-risk/check"

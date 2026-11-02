#!/usr/bin/env bash
# trader-broker/ticket: the assertions of trader-skill/tests/test_broker.py, run against the C++ tool.
# Wording adapted for A.S.T.R.O AI: the second confirmation is the Money action card (Allow / Deny), and IBKR orders
# are placed by the user by hand.
source "$(dirname "$0")/lib.sh"
R="$FIX/trader-risk"; CFG0="$R/config.example.json"; REEL="$R/orders-reel.json"; TODAY=2026-10-13
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fresh() { rm -rf "$TMP/w"; mkdir -p "$TMP/w"; cp "$REEL" "$TMP/w/orders.json"; jq "${1:-.}" "$CFG0" > "$TMP/w/config.json"; }
args() { echo --orders "$TMP/w/orders.json" --config "$TMP/w/config.json" --risk "$TMP/w/state/risk.json" --state "$TMP/w/state/broker.json" --today $TODAY; }
tk() { local c=$1; shift; run trader-broker/ticket "$c" "$@" $(args); }
risk() { run trader-risk/check --orders "$TMP/w/orders.json" --config "$TMP/w/config.json" --portfolio "$TMP/w/portfolio.json" --prices "$TMP/w/none" --state "$TMP/w/state/broker.json" --today "${1:-$TODAY}" --out "$TMP/w/state/risk.json"; }

# explicit yes, through real approve calls on a READY order (AAPL, notional 1250, batch AAPL AXP GOOGL KO BAC)
fresh; risk
yes_ok() { rm -f "$TMP/w/state/broker.json"; tk approve --index 1 --reply "$1"; }
for r in "yes" "Yes." "YES" "y" "yes place it" "Yes, buy it" "yes AAPL" 'yes $1,250 of AAPL' "yes 1250" "yes ١٢٥٠"; do
  yes_ok "$r"; eq "$CODE" 0 "counts as yes: [$r]"
done
for r in "" "sure" "ok" "okay" "go" "do it" "yeah" "yep" "absolutely" "send it" "yes to all" "yes, all of them" "yes to the rest" "yes both" \
         "yes AXP" "yes 1000" "yes but make it 1000" "yes?" "no" "not yes" "yes, wait" "yes if it is under 340" "yes ١٠٠٠"; do
  yes_ok "$r"; eq "$CODE" 1 "not a yes: [$r]"; has "$OUT" "Not recorded" "not recorded: [$r]"
done
yes_ok "yes AXP"; eq "$OUT" "Not recorded: the reply names AXP, but this order is for AAPL. Ask the user again with the ticket for order 1." "names another stock (wording)"
yes_ok "yes 1000"; has "$OUT" "the reply says 1000, but this order is BUY \$1,250 of AAPL, market order, good for today" "wrong amount (wording)"
# the amount must match this order (a KO sell of 3 shares)
fresh; echo '{"orders": [{"symbol": "KO", "side": "sell", "qty": "3", "type": "market", "time_in_force": "day"}]}' > "$TMP/w/orders.json"
echo '{"cash": 0, "positions": [{"ticker": "KO", "shares": 7.8571}]}' > "$TMP/w/portfolio.json"
echo '{"prices": {"KO": 90.11}}' > "$TMP/w/px.json"
run trader-risk/check --orders "$TMP/w/orders.json" --config "$TMP/w/config.json" --portfolio "$TMP/w/portfolio.json" --prices "$TMP/w/px.json" --state "$TMP/w/none" --today $TODAY --out "$TMP/w/state/risk.json"
yes_ok "yes sell 3 KO"; eq "$CODE" 0 "yes sell 3 KO"
yes_ok "yes sell 4"; eq "$CODE" 1 "yes sell 4 refused"
# ticket shape
fresh; echo '{"orders": [{"symbol": "AAPL", "side": "buy", "notional": "1250", "type": "market", "time_in_force": "day", "why": "copying Berkshire"}, {"symbol": "KO", "side": "buy", "qty": 3, "type": "limit", "limit_price": 89.5, "time_in_force": "day"}, {"symbol": "KO", "side": "sell", "qty": "1", "type": "limit", "limit_price": "95", "time_in_force": "gtc"}]}' > "$TMP/w/orders.json"
tk show --json
eq "$(echo "$OUT" | jq -c '.[0].tool_input')" '{"symbol":"AAPL","side":"buy","notional":"1250","type":"market","time_in_force":"day"}' "tool_input drops why"
eq "$(echo "$OUT" | jq -c '.[1].tool_input')" '{"symbol":"KO","side":"buy","qty":"3","type":"limit","time_in_force":"day","limit_price":"89.5"}' "numbers become strings"
eq "$(echo "$OUT" | jq -r '.[0].plain')" "BUY \$1,250 of AAPL, market order, good for today" "plain english buy"
eq "$(echo "$OUT" | jq -r '.[2].plain')" "SELL 1 share of KO, limit order at \$95, good until canceled" "plain english sell"
eq "$CODE" 1 "nothing ready without a risk check"
# the MCP example is paper with placeholder keys
M="$(dirname "$0")/../../library/pack-overrides/astro-trader/skills/trader-broker/alpaca.mcp.example.json"
eq "$(jq -c '.mcpServers.alpaca | [.command, .args, .env.ALPACA_PAPER_TRADE]' "$M" 2>/dev/null || jq -c '[.command, .args, .env.ALPACA_PAPER_TRADE]' "$M")" '["uvx",["alpaca-mcp-server"],"true"]' "alpaca example is paper"

# flow: nothing is ready before the risk check
fresh; tk show; eq "$CODE" 1 "show exit 1"; has "$OUT" "NOT READY" "NOT READY"; hasnt "$OUT" 'Reply "yes"' "no reply prompt"
tk approve --index 1 --reply yes; eq "$CODE" 1 "approve refused"; has "$ERR" "not ready" "not ready error"
# the full two-yes flow
fresh; risk; eq "$CODE" 0 "risk passes"
tk show --index 1; eq "$CODE" 0 "show ready"; has "$OUT" "READY: trader-risk PASS" "READY label"; has "$OUT" '"notional": "1250"' "exact JSON"
has "$OUT" "Money action card then asks again: Allow places it, Deny stops it." "Money card line"; has "$OUT" "PAPER account" "paper"
has "$OUT" "ORDER 1 of 5, PAPER account, Alpaca" "header"
tk approve --index 1 --reply sure; eq "$CODE" 1 "sure refused"; has "$OUT" "Not recorded" "sure not recorded"; [ ! -e "$TMP/w/state/broker.json" ] && ok || bad "no state written for sure"
tk approve --index 1 --reply yes; eq "$CODE" 0 "yes recorded"; has "$OUT" "place_stock_order" "call shown"; has "$OUT" "Recorded your yes for order 1: BUY \$1,250 of AAPL, market order, good for today." "recorded line"
tk approve --index 1 --reply yes; eq "$CODE" 1 "second yes refused"; has "$ERR" "APPROVED" "already approved"
tk result --index 1 --status placed --broker-id abc123 --filled-qty 3.6628 --filled-price 341.27 --portfolio "$TMP/w/portfolio.json"; eq "$CODE" 0 "result placed"
has "$OUT" "Added the fill to $TMP/w/portfolio.json." "fill added"
eq "$(jq -c '[[.orders[].status], .orders[0].reply, .orders[1].broker_order_id]' "$TMP/w/state/broker.json")" '[["approved","placed"],"yes","abc123"]' "state records"
eq "$(jq -c '[.cash, .positions[0].lots[0].cost, .positions[0].lots[0].id, .positions[0].shares]' "$TMP/w/portfolio.json")" '[3750.0,1250.0,"AAPL-1",3.6628]' "portfolio lot"
tk show --index 1; has "$OUT" "DONE: placed" "DONE placed"
run trader-broker/ticket log --state "$TMP/w/state/broker.json" --today $TODAY; has "$OUT" "1 placed today" "log count"; has "$OUT" '#1 approved  BUY $1,250 of AAPL, market order, good for today reply "yes"' "log line"
has "$OUT" "#1 placed    BUY \$1,250 of AAPL, market order, good for today id abc123" "log placed line"
echo '{"orders": [{"symbol": "AAPL", "side": "buy", "notional": "1250", "type": "market", "time_in_force": "day"}]}' > "$TMP/one.json"
echo '{"budget": 5000, "max_position_pct": 100}' > "$TMP/c100.json"
run trader-risk/check --orders "$TMP/one.json" --config "$TMP/c100.json" --portfolio "$TMP/none" --prices "$TMP/none" --state "$TMP/w/state/broker.json" --today $TODAY --no-write --json
has "$(echo "$OUT" | jq -r '.results[0].reasons[-1]')" "already placed today" "risk sees the placed order"
# an edited order is not ready
fresh; risk; jq '.orders[0].notional = "1300"' "$REEL" > "$TMP/w/orders.json"
tk show --index 1; eq "$CODE" 1 "edited not ready"; has "$OUT" "was not in the last risk check" "edited message"
# yesterday's check does not count
fresh; risk 2026-10-12; tk show --index 1; has "$OUT" "not today" "yesterday's check"
# blocked orders are never ready
fresh '.blocked = ["AAPL"]'; risk; eq "$CODE" 1 "risk blocks AAPL"
tk show --index 1; has "$OUT" "BLOCKED by trader-risk" "blocked label"; tk approve --index 1 --reply yes; eq "$CODE" 1 "blocked approve refused"
# result needs an approval; decline is logged
fresh; risk
tk result --index 2 --status placed; eq "$CODE" 1 "result without yes"; has "$ERR" "no yes recorded" "result message"
tk decline --index 2 --reply "no thanks"; eq "$CODE" 0 "decline"; has "$OUT" "Recorded: you said no to order 2 (BUY \$1,119 of AXP, market order, good for today). Nothing was sent." "decline line"
tk show --index 2; has "$OUT" "DONE: declined" "declined shown"
# ibkr: the user places it by hand
fresh '.broker = "ibkr"'; risk; tk show --index 1
has "$OUT" "Interactive Brokers" "ibkr named"; has "$OUT" "it only goes to the market after you submit it there" "by hand"; hasnt "$OUT" "place_stock_order" "no Alpaca call"
# fill: a sell relieves the oldest lot
fresh; echo '{"cash": 0, "positions": [{"ticker": "KO", "shares": 3, "lots": [{"id": "KO-1", "date": "2025-01-02", "shares": 2, "cost": 120.0}, {"id": "KO-2", "date": "2026-01-02", "shares": 1, "cost": 70.0}]}]}' > "$TMP/w/portfolio.json"
run trader-broker/ticket fill --symbol KO --side sell --qty 2.5 --price 90 --date $TODAY --portfolio "$TMP/w/portfolio.json" --config "$TMP/w/config.json"
eq "$CODE" 0 "fill sell"; eq "$OUT" "Recorded sell 2.5 KO at \$90 on $TODAY. Cash now \$225." "fill line"
eq "$(jq -c '[.cash, [.positions[0].lots[].id], .positions[0].lots[0].shares, .positions[0].lots[0].cost, .positions[0].shares]' "$TMP/w/portfolio.json")" '[225.0,["KO-2"],0.5,35.0,0.5]' "FIFO relief"
run trader-broker/ticket fill --symbol KO --side sell --qty 5 --price 90 --portfolio "$TMP/w/portfolio.json" --config "$TMP/w/config.json"
eq "$CODE" 1 "oversell refused"; eq "$ERR" "ticket: cannot record selling 5.0 KO: the portfolio holds 0.5" "oversell message"
run trader-broker/ticket; eq "$CODE" 1 "no subcommand prints help"; has "$OUT" "usage: ticket" "help shown"
done_ "trader-broker/ticket"

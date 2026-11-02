#!/usr/bin/env bash
# daytrade-broker/ticket: the assertions of day-trader-skill/tests/test_broker.py, run against the C++ tools
# (daytrade-broker/ticket + daytrade-risk/check). Wording adapted for A.S.T.R.O AI: the second confirmation is the Money
# action card (Allow / Deny), and IBKR orders are placed by the user by hand.
source "$(dirname "$0")/lib.sh"
R="$FIX/daytrade-risk"; CFG0="$R/config.example.json"; TSLA_F="$R/orders-tsla.json"; PLAN="$FIX/daytrade-budget/plan-tsla.json"
NOW="2026-10-19 10:15"; TODAY=2026-10-19
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
W="$TMP/w"; PF="$W/portfolio.json"; ST="$W/state/broker.json"; RK="$W/state/risk.json"
TSLA_CALL='{"symbol":"TSLA","side":"buy","qty":"8","type":"market","time_in_force":"day","order_class":"bracket","take_profit_limit_price":"370.04","stop_loss_stop_price":"358.32"}'
TSLA_PLAIN='BUY 8 TSLA, market, good for today / stop-loss 358.32 / take-profit 370.04 / max loss about $46.88 if the stop fills at 358.32 (it can fill worse in a fast market)'
fresh() { rm -rf "$W"; mkdir -p "$W"; cp "$TSLA_F" "$W/orders.json"; jq "${1:-.}" "$CFG0" > "$W/config.json"; }
args() { echo --orders "$W/orders.json" --config "$W/config.json" --risk "$RK" --state "$ST"; }
tk() { local c=$1; shift; run daytrade-broker/ticket "$c" "$@" $(args) --now "${AT:-$NOW}"; }
risk() { run daytrade-risk/check --orders "$W/orders.json" --config "$W/config.json" --portfolio "$PF" --prices "$W/none" --state "$ST" --now "${1:-$NOW}" --out "$RK"; }
# a hand-made PASS for every order in orders.json (the yes rules do not depend on the risk rules)
pass_all() { mkdir -p "$W/state"; run daytrade-broker/ticket show --json $(args) --now "$NOW"; echo "$OUT" | jq -c "{date: \"$TODAY\", results: [.[] | {fingerprint, verdict: \"PASS\"}]}" > "$RK"; }

# explicit yes: AAPL $1,250 in a batch of AAPL AXP GOOGL KO BAC
fresh; jq -c '.orders' "$R/orders-reel.json" | jq '{orders: .}' > "$W/orders.json"; pass_all
yes_ok() { rm -f "$ST"; tk approve --index 1 --reply "$1"; }
for r in "yes" "Yes." "YES" "y" "yes place it" "Yes, buy it" "yes AAPL" 'yes $1,250 of AAPL' "yes 1250"; do yes_ok "$r"; eq "$CODE" 0 "counts as yes: [$r]"; done
for r in "" "sure" "ok" "okay" "go" "do it" "yeah" "yep" "absolutely" "send it" "yes to all" "yes, all of them" "yes to the rest" "yes both" \
         "yes AXP" "yes 1000" "yes but make it 1000" "yes?" "no" "not yes" "yes, wait" "yes if it is under 340"; do
  yes_ok "$r"; eq "$CODE" 1 "not a yes: [$r]"; has "$OUT" "Not recorded" "not recorded: [$r]"
done
# the amount must match this order
echo '{"orders": [{"symbol": "KO", "side": "sell", "qty": "3", "type": "market", "time_in_force": "day"}]}' > "$W/orders.json"; pass_all
yes_ok "yes sell 3 KO"; eq "$CODE" 0 "yes sell 3 KO"; yes_ok "yes sell 4"; eq "$CODE" 1 "yes sell 4 refused"
# a bracket yes: any of its numbers (qty, stop, take-profit, max loss) matches; another number or stock does not
echo "{\"orders\": [$(jq -c '.orders[0]' "$TSLA_F"), {\"symbol\": \"AAPL\", \"side\": \"buy\", \"notional\": \"1250\", \"type\": \"market\", \"time_in_force\": \"day\"}]}" > "$W/orders.json"; pass_all
for r in "yes" "yes 8 TSLA" "yes 358.32" "yes max loss 46.88" "yes target 370.04"; do yes_ok "$r"; eq "$CODE" 0 "bracket yes: [$r]"; done
for r in "yes 9" "yes 10 TSLA" "yes but stop at 357" "yes, stop 358.32" "yes AAPL" "sure"; do yes_ok "$r"; eq "$CODE" 1 "bracket not a yes: [$r]"; done

# ticket shape
fresh; echo "{\"orders\": [{\"symbol\": \"AAPL\", \"side\": \"buy\", \"notional\": \"1250\", \"type\": \"market\", \"time_in_force\": \"day\", \"why\": \"from a list\"}, {\"symbol\": \"KO\", \"side\": \"buy\", \"qty\": 3, \"type\": \"limit\", \"limit_price\": 89.5, \"time_in_force\": \"day\"}, {\"symbol\": \"KO\", \"side\": \"sell\", \"qty\": \"1\", \"type\": \"limit\", \"limit_price\": \"95\", \"time_in_force\": \"gtc\"}, $(jq -c '.orders[0]' "$TSLA_F"), $(jq -c '.orders[0] | .type = "limit" | .limit_price = "364.00"' "$TSLA_F"), $TSLA_CALL]}" > "$W/orders.json"
tk show --json; eq "$CODE" 1 "nothing ready without a risk check"
eq "$(echo "$OUT" | jq -c '.[0].tool_input')" '{"symbol":"AAPL","side":"buy","notional":"1250","type":"market","time_in_force":"day"}' "tool_input drops why"
eq "$(echo "$OUT" | jq -c '.[1].tool_input')" '{"symbol":"KO","side":"buy","qty":"3","type":"limit","time_in_force":"day","limit_price":"89.5"}' "numbers become strings"
eq "$(echo "$OUT" | jq -c '.[3].tool_input')" "$TSLA_CALL" "the bracket call leaves out why and plan"
eq "$(echo "$OUT" | jq -c '[.[3].fingerprint == .[5].fingerprint]')" '[true]' "same fingerprint without why and plan"
eq "$(echo "$OUT" | jq -r '.[0].plain')" 'BUY $1,250 of AAPL, market order, good for today' "plain english buy"
eq "$(echo "$OUT" | jq -r '.[2].plain')" 'SELL 1 share of KO, limit order at $95, good until canceled' "plain english sell"
eq "$(echo "$OUT" | jq -r '.[3].plain')" "$TSLA_PLAIN" "plain english bracket"
eq "$(echo "$OUT" | jq -r '.[4].plain | split(" / ")[0]')" "BUY 8 TSLA, limit 364.00, good for today" "bracket limit entry"
eq "$(echo "$OUT" | jq -c '[.[4].max_loss, .[4].stop, .[5].max_loss, .[5].stop, .[3].bracket, .[0].bracket, .[0].max_loss, .[0].stop]')" '[45.44,358.32,null,358.32,true,false,null,null]' "max loss: limit, no plan, not a bracket"
# the MCP example is paper with placeholder keys
M="$(dirname "$0")/../../library/pack-overrides/astro-day-trader/skills/daytrade-broker/alpaca.mcp.example.json"
eq "$(jq -c '(.mcpServers.alpaca // .) | [.command, .args, .env.ALPACA_PAPER_TRADE]' "$M")" '["uvx",["alpaca-mcp-server"],"true"]' "alpaca example is paper"

# flow: nothing is ready before the risk check
fresh; tk show; eq "$CODE" 1 "show exit 1"; has "$OUT" "NOT READY" "NOT READY"; hasnt "$OUT" 'Reply "yes"' "no reply prompt"
tk approve --index 1 --reply yes; eq "$CODE" 1 "approve refused"; has "$ERR" "not ready" "not ready error"
run daytrade-broker/ticket flatten --symbol TSLA --qty 0 --orders "$W/orders.json"; eq "$CODE" 1 "flatten --qty 0 refused"
eq "$ERR" "ticket: --qty wants the number of shares you hold, like --qty 8" "flatten --qty 0 message"
# the full two-yes flow
fresh; risk; eq "$CODE" 0 "risk passes"
tk show --index 1; eq "$CODE" 0 "show ready"; has "$OUT" "ORDER 1 of 1, PAPER account, Alpaca" "header"
IFS='/' read -ra PARTS <<< "${TSLA_PLAIN// \/ //}"; for part in "${PARTS[@]}"; do has "$OUT
" "  $part
" "ticket line: $part"; done
has "$OUT" "READY: trader-risk PASS" "READY label"; has "$OUT" '"order_class": "bracket"' "bracket in the call"; has "$OUT" '"stop_loss_stop_price": "358.32"' "stop in the call"
hasnt "$OUT" '"plan"' "no plan in the call"; hasnt "$OUT" '"why"' "no why in the call"
has "$OUT" "Alpaca places the entry, then the stop-loss and the take-profit as two orders" "bracket explained"
has "$OUT" "Money action card then asks again: Allow places it, Deny stops it." "Money card line"
tk approve --index 1 --reply sure; eq "$CODE" 1 "sure refused"; has "$OUT" "Not recorded" "sure not recorded"; [ ! -e "$ST" ] && ok || bad "no state written for sure"
tk approve --index 1 --reply yes; eq "$CODE" 0 "yes recorded"; has "$OUT" "place_stock_order" "call shown"
eq "$(echo "$OUT" | sed -n '/^{/,/^}/p' | jq -c .)" "$TSLA_CALL" "the exact call"
tk approve --index 1 --reply yes; eq "$CODE" 1 "second yes refused"; has "$ERR" "APPROVED" "already approved"
AT="2026-10-19 10:16" tk result --index 1 --status placed --broker-id abc123 --filled-qty 8 --filled-price 364.20 --portfolio "$PF"; eq "$CODE" 0 "result placed"
has "$OUT" "Added the fill to $PF." "fill added"
eq "$(jq -c '[.orders[].status]' "$ST")" '["approved","placed","filled"]' "approved, placed, filled"
eq "$(jq -c '.orders[0] | [.reply, .kind, .max_loss, .date, .at, .qty, .stop]' "$ST")" "[\"yes\",\"entry\",46.88,\"$TODAY\",\"2026-10-19T10:15:00-04:00\",\"8\",358.32]" "approval record"
eq "$(jq -c '.orders[1] | [.broker_order_id, .max_loss, .kind, .at, (keys_unsorted)]' "$ST")" '["abc123",46.88,"entry","2026-10-19T10:16:00-04:00",["at","date","index","fingerprint","symbol","side","plain","status","qty","kind","max_loss","stop","broker_order_id"]]' "placed record"
eq "$(jq -c '.orders[2]' "$ST")" '{"at":"2026-10-19T10:16:00-04:00","date":"2026-10-19","status":"filled","symbol":"TSLA","side":"buy","qty":"8","price":364.2,"plain":"BUY 8 TSLA at 364.20"}' "fill record"
eq "$(jq -c '[.cash, .positions[0].shares, .positions[0].lots[0].cost]' "$PF")" '[2086.4,8.0,2913.6]' "portfolio after the fill"
tk show --index 1; has "$OUT" "DONE: placed" "DONE placed"
run daytrade-broker/ticket log --state "$ST" --now "$NOW"; has "$OUT" "1 placed today" "log count"; has "$OUT" 'max loss $46.88' "log max loss"
has "$OUT" '2026-10-19T10:16:00-04:00     filled    BUY 8 TSLA at 364.20' "log fill line has no index"
# trader-risk now counts it: a second TSLA buy today is a duplicate, and $46.88 is used
run daytrade-risk/check --orders "$TSLA_F" --config "$CFG0" --portfolio "$TMP/none" --prices "$TMP/none" --state "$ST" --now "2026-10-19 10:30" --no-write --json
has "$(echo "$OUT" | jq -r '.results[0].reasons | join(" ")')" "already placed today" "risk sees the placed order"
eq "$(echo "$OUT" | jq -c '[.today.entries_placed, .today.loss_at_risk]')" '[1,46.88]' "entries and loss at risk"
# the result line for a placed bracket entry without a fill
fresh; risk; tk approve --index 1 --reply yes; tk result --index 1 --status placed
has "$OUT" "When it fills, record it: ticket fill --symbol TSLA --side buy --qty Q --price P" "record it later"; has "$OUT" "When the stop-loss or the take-profit fills later, record that sell the same way." "bracket legs later"

# flatten by the close
fresh; risk; tk approve --index 1 --reply yes; tk result --index 1 --status placed --broker-id abc --filled-qty 8 --filled-price 364.20 --portfolio "$PF"
LATE="2026-10-19 15:55"
run daytrade-broker/ticket flatten --symbol TSLA --orders "$W/orders.json" --portfolio "$PF" --plan "$PLAN" --config "$W/config.json" --now "$LATE"
eq "$CODE" 0 "flatten"; has "$OUT" "SELL 8 shares of TSLA, market order, good for today (flat by 15:55 New York)" "flatten line"
has "$OUT" "replaced 1 earlier proposal" "replaced count"; has "$OUT" "cancel_order_by_id" "cancel the legs first"
has "$OUT" "The qty comes from $PF. Check it against get_open_position TSLA before you say yes." "qty source"
eq "$(jq -c '[.orders[0] | {symbol, side, qty, type, time_in_force}][0]' "$W/orders.json")" '{"symbol":"TSLA","side":"sell","qty":"8","type":"market","time_in_force":"day"}' "flatten ticket"
eq "$(jq -c '[.created_by, .created_at, .status, .paper, .source, .orders[0].why]' "$W/orders.json")" "[\"trader-broker\",\"2026-10-19T19:55:00Z\",\"proposed\",true,\"ticket flatten TSLA on $TODAY\",\"flat by 15:55 New York: closes the TSLA day trade before the close (qty from $PF)\"]" "flatten document"
risk "$LATE"; eq "$CODE" 0 "after the entry cutoff an exit still passes"; eq "$(jq -r '.results[0].kind' "$RK")" exit "risk sees an exit"
AT="$LATE" tk show; eq "$CODE" 0 "flatten ready"; has "$OUT" "READY" "READY"
AT="$LATE" tk approve --index 1 --reply "yes sell 8 TSLA"; eq "$CODE" 0 "yes sell 8 TSLA"
AT="2026-10-19 15:56" tk result --index 1 --status placed --filled-qty 8 --filled-price 366.10 --portfolio "$PF"; eq "$CODE" 0 "flatten placed"
eq "$(jq -c '[.cash, .positions]' "$PF")" '[5015.2,[]]' "flat again"
jq '.pdt_guard = true' "$CFG0" > "$W/pdt.json"
run daytrade-risk/check --orders "$TSLA_F" --config "$W/pdt.json" --portfolio "$TMP/none" --prices "$TMP/none" --state "$ST" --now "2026-10-19 10:30" --no-write --json
eq "$(echo "$OUT" | jq -c '.today.day_trades_5d')" 1 "one day trade: 2026-10-19 TSLA"
run daytrade-broker/ticket flatten --symbol TSLA --orders "$W/orders.json" --portfolio "$PF" --now "$LATE"; eq "$CODE" 1 "nothing to flatten"; has "$ERR" "nothing to flatten" "nothing to flatten message"
# flatten with the broker's qty
fresh; run daytrade-broker/ticket flatten --symbol tsla --qty 8 --orders "$W/orders.json" --portfolio "$W/none" --plan "$W/none" --config "$W/config.json" --now "$NOW"
eq "$CODE" 0 "flatten with --qty"; hasnt "$OUT" "Check it against get_open_position" "no qty source line"; has "$(jq -r '.orders[0].why' "$W/orders.json")" "(qty from the broker, get_open_position)" "why names the broker"
has "$OUT" "(flat by 15:55 New York)" "flat_by from the config"
jq '.flat_by = "15:45"' "$CFG0" > "$W/c2.json"; run daytrade-broker/ticket flatten --symbol TSLA --qty 2 --orders "$W/o2.json" --plan "$W/none" --config "$W/c2.json" --now "$NOW"
has "$OUT" "Flatten TSLA: SELL 2 shares of TSLA, market order, good for today (flat by 15:45 New York)." "config flat_by"; has "$OUT" "Wrote it to $W/o2.json." "no earlier proposal"

# an edited order is not ready; yesterday's check does not count; blocked orders are never ready
fresh; risk; jq '.orders[0].qty = "9"' "$TSLA_F" > "$W/orders.json"; tk show --index 1; eq "$CODE" 1 "edited not ready"; has "$OUT" "was not in the last risk check" "edited message"
fresh; risk "2026-10-16 10:15"; tk show --index 1; has "$OUT" "not today" "yesterday's check"
fresh; risk "2026-10-19 09:40"; eq "$CODE" 1 "risk blocks before 09:45"
AT="2026-10-19 09:40" tk show --index 1; has "$OUT" "BLOCKED by trader-risk: window: no new entries before 09:45" "blocked label"
AT="2026-10-19 09:40" tk approve --index 1 --reply yes; eq "$CODE" 1 "blocked approve refused"
# result needs an approval; decline is logged
fresh; risk; tk result --index 1 --status placed; eq "$CODE" 1 "result without yes"; has "$ERR" "no yes recorded" "result message"
tk decline --index 1 --reply "no thanks"; eq "$CODE" 0 "decline"; has "$OUT" "Recorded: you said no to order 1 (" "decline line"; has "$OUT" "). Nothing was sent." "nothing sent"
tk show --index 1; has "$OUT" "DONE: declined" "declined shown"
eq "$(jq -r '.orders[0].at' "$ST")" "2026-10-19T10:15:00-04:00" "decline time in New York"
# ibkr: the user places it by hand
fresh '.broker = "ibkr"'; risk; tk show --index 1
has "$OUT" "Interactive Brokers" "ibkr named"; has "$OUT" "it only goes to the market after you submit it there" "by hand"; hasnt "$OUT" "place_stock_order" "no Alpaca call"
# fills
fresh; echo '{"cash": 0, "positions": [{"ticker": "KO", "shares": 3, "lots": [{"id": "KO-1", "date": "2025-01-02", "shares": 2, "cost": 120.0}, {"id": "KO-2", "date": "2026-01-02", "shares": 1, "cost": 70.0}]}]}' > "$PF"
run daytrade-broker/ticket fill --symbol KO --side sell --qty 2.5 --price 90 --date $TODAY --portfolio "$PF" --config "$W/config.json" --state "$ST"
eq "$CODE" 0 "fill sell"; eq "$OUT" "Recorded sell 2.5 KO at \$90 on $TODAY in $PF. Cash now \$225." "fill line"
eq "$(jq -c '[.cash, [.positions[0].lots[].id], .positions[0].lots[0].shares, .positions[0].lots[0].cost]' "$PF")" '[225.0,["KO-2"],0.5,35.0]' "FIFO relief"
eq "$(jq -r '.orders[-1].status' "$ST")" filled "fill logged"
run daytrade-broker/ticket fill --symbol KO --side sell --qty 5 --price 90 --portfolio "$PF" --config "$W/config.json" --state "$ST"
eq "$CODE" 1 "oversell refused"; eq "$ERR" "ticket: cannot record selling 5 KO: the portfolio holds 0.5" "oversell message"
run daytrade-broker/ticket fill --symbol KO --side buy --qty 0 --price 90 --portfolio "$PF" --config "$W/config.json" --state "$ST"; eq "$ERR" "ticket: a fill needs a positive qty and price" "zero fill"
run daytrade-broker/ticket fill --symbol KO --side buy --qty 1 --price 90 --date 10/19 --portfolio "$PF" --state "$ST"; eq "$ERR" "ticket: --date wants a date like 2026-10-19" "bad date"
run daytrade-broker/ticket log --state "$ST" --today 19-10-2026; eq "$ERR" "ticket: --today wants a date like 2026-10-19" "bad today"
run daytrade-broker/ticket log --state "$ST" --now soon; has "$ERR" "ticket: --now wants a time like" "bad now"
run daytrade-broker/ticket; eq "$CODE" 1 "no subcommand prints help"; has "$OUT" "usage: ticket" "help shown"; has "$OUT" "flatten" "help lists flatten"
done_ "daytrade-broker/ticket"

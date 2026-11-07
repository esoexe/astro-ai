#!/usr/bin/env bash
# trader-tax/lots: the assertions of trader-skill/tests/test_tax.py, run against the C++ tool.
source "$(dirname "$0")/lib.sh"
X="$FIX/trader-tax"; PX="$X/prices.json"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fresh() { rm -rf "$TMP/w"; mkdir -p "$TMP/w"; cp "${1:-$X/portfolio.json}" "$TMP/w/portfolio.json"; }
C() { echo --portfolio "$TMP/w/portfolio.json" --prices "${PRICES:-$PX}" --state "$TMP/w/tax.json" --orders "$TMP/w/orders.json" --today "${DAY:-2026-10-13}"; }
lots() { local c=$1; shift; run trader-tax/lots "$c" "$@" $(C); }
j() { echo "$OUT" | jq -c "$1"; }

# term: long only if held MORE than one year; a leap-day purchase turns long on March 1
echo '{"positions": [{"ticker": "KO", "lots": [{"id": "KO-1", "date": "2025-10-13", "shares": 1, "cost": 50}]}, {"ticker": "XOM", "lots": [{"id": "XOM-1", "date": "2024-02-29", "shares": 1, "cost": 50}]}]}' > "$TMP/t.json"
fresh "$TMP/t.json"
DAY=2026-10-13 lots sell KO 1 --price 60 --json; eq "$(j '.lots[0].term')" '"short"' "one year exactly is short"
DAY=2026-10-14 lots sell KO 1 --price 60 --json; eq "$(j '.lots[0].term')" '"long"' "a day more is long"
DAY=2025-02-28 lots sell XOM 1 --price 60 --json; eq "$(j '.lots[0].term')" '"short"' "leap day short"
DAY=2025-03-01 lots sell XOM 1 --price 60 --json; eq "$(j '.lots[0].term')" '"long"' "leap day long"
echo '{"KO": 60, "XOM": 60}' > "$TMP/tp.json"; DAY=2025-01-02 PRICES="$TMP/tp.json" lots report --json --lots; eq "$(j '[.lots[] | .long_term_on]')" '["2026-10-14","2025-03-01"]' "long_term_on (leap)"

# unrealized by lot
fresh; lots report --json
eq "$(j '.missing_prices')" '[]' "no missing prices"
eq "$(j '[.lots[] | select(.id=="KO-1") | .term, .gain]')" '["long",-148.9]' "KO-1 long -148.9"
eq "$(j '.lots[] | select(.id=="BAC-1") | .term')" '"short"' "BAC-1 short"
eq "$(j '.lots[] | select(.id=="AAPL-1") | .long_term_on')" '"2027-10-14"' "AAPL long term on"
eq "$(j '.positions[] | select(.ticker=="AXP") | [.gain, .long, .short]')" '[209.36,239.52,-30.16]' "AXP totals"
eq "$(j '[.positions[].ticker]')" '["AAPL","AXP","BAC","KO"]' "sorted tickers"
eq "$(j '.positions[0] | keys_unsorted')" '["ticker","shares","cost","value","short","long","gain","pct"]' "position keys"
eq "$(j '.realized_ytd')" '{"short":0.0,"long":0.0,"disallowed":0.0,"count":0}' "ytd empty"
# fifo / hifo / specific
lots sell AXP 3 --price 309.84 --json; eq "$(j '[[.lots[].id], .gain, .long]')" '[["AXP-1"],239.52,239.52]' "fifo"
eq "$(j 'keys_unsorted')" '["proceeds","basis","short","long","gain","lots","wash","wash_disallowed"]' "sale keys"
lots sell AXP 3 --price 309.84 --method hifo --json; eq "$(j '[[.lots[].id], .short, .long]')" '[["AXP-2","AXP-1"],-30.16,159.68]' "hifo"
lots sell AXP 1 --price 309.84 --method specific --lots AXP-2 --json; eq "$(j '.gain')" '-30.16' "specific"
lots sell AXP 5 --price 309.84; eq "$CODE" 1 "oversell"; eq "$ERR" "lots: selling 5 shares but only 4 are held" "oversell message"
lots sell AXP 1 --price 309.84 --method specific --lots AXP-9; eq "$CODE" 1 "unknown lot"; eq "$ERR" "lots: no such lot: AXP-9 (lots: AXP-1, AXP-2)" "unknown lot message"
lots sell AXP 1 --method specific; eq "$ERR" "lots: --method specific needs --lots ID,ID" "specific needs lots"
# the example portfolio is the reel buy
fresh "$X/portfolio.example.json"; lots report --json
eq "$(j '[.positions[] | {(.ticker): (.cost|round)}] | add')" '{"AAPL":1250,"AXP":1119,"BAC":600,"GOOGL":823,"KO":708}' "example costs"
eq "$(jq '.cash' "$X/portfolio.example.json")" "500.0" "example cash"
# shares without lots are refused
echo '{"positions": [{"ticker": "KO", "shares": 3}]}' > "$TMP/n.json"; fresh "$TMP/n.json"; lots report
eq "$CODE" 1 "no lots refused"; has "$ERR" "KO has shares but no lots" "no lots message"

# harvest finds losses and skips wash sales
fresh; lots harvest --min-loss 100 --json
eq "$(j '[.candidates[].ticker]')" '["KO","BAC"]' "harvest KO BAC"
eq "$(j '.candidates[0] | [.wash, .rebuy_on_or_after, .long]')" '[[],"2026-11-13",-148.9]' "KO candidate"
eq "$(j '.candidates[1] | [.lots, .wash[0].date, .disallowed]')" '[["BAC-1"],"2026-10-01",22.08]' "BAC candidate"
eq "$(j '[.orders[] | del(.why)]')" '[{"symbol":"KO","side":"sell","qty":"10","type":"market","time_in_force":"day"}]' "only KO proposed"
has "$(j -r '.orders[0].why')" "Not tax advice" "why says not advice"
eq "$(echo "$OUT" | jq -r '.orders[0].why')" "tax-loss harvest proposal: sell lots KO-1 for an estimated -\$148.90 loss (\$0.00 short term, -\$148.90 long term). Do not buy KO again before 2026-11-13 (wash sale). Not tax advice." "why text"
lots harvest --min-loss 100 --include-wash --json
eq "$(j '[.orders[].symbol]')" '["KO","BAC"]' "include wash"; hasnt "$(echo "$OUT" | jq -r '.orders[1].why')" "not your oldest lots" "BAC-1 is the oldest"
# threshold
lots harvest --min-loss 200 --json; eq "$(j '.candidates')" '[]' "nothing at 200"; eq "$CODE" 1 "exit 1 when none"
lots harvest --min-loss 20 --json
eq "$(j '.candidates[] | select(.ticker=="AXP") | .fifo_same')" 'false' "AXP not fifo"
has "$(echo "$OUT" | jq -r '.orders[] | select(.symbol=="AXP") | .why')" "not your oldest lots" "not oldest note"
# a planned buy is a wash sale
echo '{"orders": [{"symbol": "KO", "side": "buy", "notional": "200", "type": "market", "time_in_force": "day"}]}' > "$TMP/w/orders.json"
lots harvest --min-loss 100 --json
eq "$(j '.candidates[] | select(.ticker=="KO") | .wash[0].source')" '"a planned buy in trader/orders.json"' "planned buy"
eq "$(j '[.orders[] | select(.symbol=="KO")]')" '[]' "planned buy blocks the proposal"
eq "$(j '.candidates[] | select(.ticker=="KO") | .wash[0].shares')" "$(jq -n '200/90.11')" "planned shares from notional"
# share classes are one family
echo '{"cash": 0, "positions": [{"ticker": "GOOGL", "lots": [{"id": "GOOGL-1", "date": "2026-01-05", "shares": 2, "cost": 800}]}, {"ticker": "GOOG", "lots": [{"id": "GOOG-1", "date": "2026-10-05", "shares": 1, "cost": 340}]}]}' > "$TMP/g.json"
echo '{"GOOGL": 353.78, "GOOG": 352.0}' > "$TMP/gp.json"
fresh "$TMP/g.json"; PRICES="$TMP/gp.json" lots harvest --min-loss 50 --json
eq "$(j '[.candidates[0].ticker, .candidates[0].wash[0].ticker]')" '["GOOGL","GOOG"]' "GOOG/GOOGL family"
lots wash brk.b; has "$OUT" "No recorded loss sale of BRK-A/BRK-B/BRK.A/BRK.B in the 30 days before 2026-10-13." "BRK family"

# record a sale, then the rebuy window
fresh; lots sell KO 4 --price 90.11 --record
eq "$CODE" 0 "sell record"; has "$OUT" "long term" "long term shown"
has "$OUT" "  lot KO-1     bought 2025-03-03  4 shares  basis \$420.00  proceeds \$360.44  -\$59.56 (long term)" "lot line"
has "$OUT" "Total: proceeds \$360.44, basis \$420.00, loss -\$59.56 (\$0.00 short term, -\$59.56 long term)." "total line"
has "$OUT" "No wash-sale conflict found. Do not buy KO again before 2026-11-13." "rebuy note"
has "$OUT" "Recorded: lots updated in $TMP/w/portfolio.json, sale logged in $TMP/w/tax.json." "recorded line"
eq "$(jq -c '.positions[] | select(.ticker=="KO") | [.shares, .lots[0].cost, .lots[0].shares]' "$TMP/w/portfolio.json")" '[6.0,630.0,6.0]' "KO lots relieved"
eq "$(jq '.cash' "$TMP/w/portfolio.json")" "860.44" "cash + proceeds"
eq "$(jq -c '.realized[0] | [.gain, .long, .shares, .price, .wash_disallowed]' "$TMP/w/tax.json")" '[-59.56,-59.56,4.0,90.11,0.0]' "realized logged"
lots wash KO --date 2026-10-20; eq "$CODE" 1 "wash inside window"; has "$OUT" "2026-11-13" "rebuy date"
eq "$(echo "$OUT" | head -1)" "WASH SALE: you sold 4 KO at a -\$59.56 loss on 2026-10-13. Buying KO before 2026-11-13 would disallow that loss." "wash line"
lots wash KO --date 2026-11-13; eq "$CODE" 0 "outside window"
lots report --json; eq "$(j '.realized_ytd | [.long, .count]')" '[-59.56,1]' "ytd long"
# CLI
fresh; lots report --lots; eq "$CODE" 0 "report"; has "$OUT" "KO-1" "lots listed"; has "$OUT" "Not tax advice" "not advice"
has "$OUT" "KO              10    \$1,050.00      \$901.10     -\$148.90  -14.18%" "report row"
has "$OUT" "BAC-2      2026-10-01           2     \$100.00       \$7.92  short 2027-10-02" "lot row"
lots harvest --min-loss 100 --write "$TMP/w/orders.json"; eq "$CODE" 0 "harvest write"; has "$OUT" "WASH SALE RISK" "wash risk shown"
has "$OUT" "Wrote 1 proposed sell order(s) to $TMP/w/orders.json. trader-risk checks them next." "wrote line"
eq "$(jq -c '[.created_by, .status, [.orders[].symbol]]' "$TMP/w/orders.json")" '["trader-tax","proposed",["KO"]]' "orders file"
lots harvest --min-loss 1000; eq "$CODE" 1 "harvest 1000"; has "$OUT" "No lot has a loss of at least \$1,000.00." "none line"
lots harvest --min-loss 1000 --min-pct 5; has "$OUT" "No lot has a loss of at least \$1,000.00 and 5.0%." "pct shown as python float"
run trader-tax/lots report --portfolio "$TMP/nope.json"; eq "$CODE" 1 "missing portfolio"; has "$ERR" "no portfolio" "missing message"
fresh; DAY=2026-13-01 lots report; eq "$ERR" "lots: not a date (YYYY-MM-DD): '2026-13-01'" "bad date"
run trader-tax/lots; eq "$CODE" 1 "no subcommand"; has "$OUT" "usage: lots" "help"
done_ "trader-tax/lots"

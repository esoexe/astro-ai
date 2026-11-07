#!/usr/bin/env bash
# trader-alerts/alerts: the assertions of trader-skill/tests/test_alerts.py, run against the C++ tool (--offline).
source "$(dirname "$0")/lib.sh"
Q="$FIX/trader-alerts"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
A() { run trader-alerts/alerts "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
asof() { A quote AAPL --offline "$Q" --write "$TMP/asof.json" --now "$1"; jq -r .as_of "$TMP/asof.json"; }

# calendar and clock
A clock --now 2026-10-13T10:00 --json
eq "$(j '[.open, .reason, .opens, .closes, .weekday, .calendar_known]')" '[true,"regular session","09:30","16:00","Tuesday",true]' "open Tuesday"
eq "$(j 'keys_unsorted')" '["now_new_york","date","weekday","calendar_known","open","reason","early_close","opens","closes","next_open"]' "status keys"
A clock --now 2026-10-13T09:29 --json; eq "$(j '[.open, .next_open, .reason]')" '[false,"2026-10-13 09:30","before the open"]' "before the open"
A clock --now 2026-10-13T16:00 --json; eq "$(j '[.open, .next_open, .reason]')" '[false,"2026-10-14 09:30","after the close"]' "after the close"
A clock --now 2026-07-03T11:00 --json; eq "$(j '[.open, .reason, .next_open]')" '[false,"NYSE holiday: Independence Day (observed)","2026-07-06 09:30"]' "July 3 holiday"
A clock --now 2026-11-27T12:59 --json; eq "$(j '[.open, .closes, .early_close]')" '[true,"13:00","day after Thanksgiving"]' "early close open"
A clock --now 2026-11-27T13:00 --json; eq "$(j '.open')" false "early close shut"
A clock --now 2027-12-24T10:00 --json; eq "$(j '[.open, .next_open]')" '[false,"2027-12-27 09:30"]' "Christmas 2027 observed"
A clock --now 2027-12-31T10:00 --json; eq "$(j '.open')" true "Dec 31 2027 open"
A clock --now 2026-10-17T10:00 --json; eq "$(j '.reason')" '"weekend"' "weekend"
A clock --now 2028-03-01T10:00 --json; eq "$(j '.calendar_known')" false "2028 unknown"
for d in 2026-01-01 2026-01-19 2026-02-16 2026-04-03 2026-05-25 2026-06-19 2026-07-03 2026-09-07 2026-11-26 2026-12-25 \
         2027-01-01 2027-01-18 2027-02-15 2027-03-26 2027-05-31 2027-06-18 2027-07-05 2027-09-06 2027-11-25 2027-12-24; do
    A clock --now "${d}T10:00" --json; eq "$(j '[.open, (.reason | startswith("NYSE holiday: ")), .weekday != "Saturday" and .weekday != "Sunday"]')" '[false,true,true]' "holiday $d"
done
for d in 2026-11-27 2026-12-24 2027-11-26; do A clock --now "${d}T12:00" --json; eq "$(j '[.open, .closes]')" '[true,"13:00"]' "early close $d"; done
A clock --now 2026-12-24T13:30 --json; eq "$(j '[.open, .early_close]')" '[false,"Christmas Eve"]' "Christmas Eve closed at 13:30"
# daylight saving switches (UTC in, New York out)
A clock --now 2026-03-08T06:59Z --json; eq "$(j '.now_new_york')" '"2026-03-08 01:59"' "EST until 7:00 UTC"
A clock --now 2026-03-08T07:00Z --json; eq "$(j '.now_new_york')" '"2026-03-08 03:00"' "EDT from 7:00 UTC"
A clock --now 2026-11-01T05:59Z --json; eq "$(j '.now_new_york')" '"2026-11-01 01:59"' "EDT until 6:00 UTC"
A clock --now 2026-11-01T06:00Z --json; eq "$(j '.now_new_york')" '"2026-11-01 01:00"' "EST from 6:00 UTC"
A clock --now 2027-03-14T07:00Z --json; eq "$(j '.now_new_york')" '"2027-03-14 03:00"' "2027 switch"
# parse_now: New York when no offset
eq "$(asof 2026-10-13T09:30)" "2026-10-13T13:30:00Z" "NY summer to UTC"
eq "$(asof 2026-12-01T09:30)" "2026-12-01T14:30:00Z" "NY winter to UTC"
eq "$(asof 2026-10-13T20:00:01Z)" "2026-10-13T20:00:01Z" "Z kept"
eq "$(asof 2026-10-13T22:00:00+02:00)" "2026-10-13T20:00:00Z" "offset applied"
A clock --now tomorrow; eq "$CODE" 1 "bad --now exit"; has "$ERR" "alerts: --now wants an ISO time like 2026-10-13T09:31" "bad --now message"
# clock text and exit codes
A clock --now 2026-10-13T10:00; eq "$CODE" 0 "clock open exit 0"
eq "$OUT" $'New York 2026-10-13 10:00 (Tuesday): NYSE OPEN, regular session.\nToday\'s session: 09:30 to 16:00.' "clock text"
A clock --now 2026-12-25T10:00; eq "$CODE" 1 "clock closed exit 1"; has "$OUT" "Christmas Day" "Christmas"; has "$OUT" "Next open: 2026-12-28 09:30 New York." "next open line"
A clock --now 2028-03-01T10:00; has "$OUT" "Holiday list covers 2026 and 2027 only: check nyse.com for this date." "unknown calendar line"

# quotes
A quote AAPL --offline "$Q" --json
eq "$(j '.quotes.AAPL | [.price, .prev_close, .time, .currency, .source, .high]')" '[341.27,340.97,"2026-10-13T20:00:01Z","USD","yahoo",348.15]' "yahoo fixture"
eq "$(j '.quotes.AAPL | keys_unsorted')" '["symbol","price","open","high","low","prev_close","time","currency","source"]' "yahoo keys"
A quote AAPL --offline "$Q" --source stooq --json
eq "$(j '.quotes.AAPL | [.price, .open, .high, .low, .prev_close, .time, .source]')" '[341.27,344.67,348.15,340.74,null,"2026-10-13 22:00:09","stooq"]' "stooq fixture"
A quote ZZZZ --offline "$Q" --source stooq --json; eq "$CODE" 1 "stooq N/D exit"; eq "$(j '.errors')" '["Stooq has no data for ZZZZ (check the ticker)"]' "stooq N/D"
A quote AAPL KO ZZZZ NOPE --offline "$Q" --json
eq "$(j '.quotes | keys')" '["AAPL","KO"]' "offline quotes"; eq "$(j '.quotes.KO.price')" 90.11 "KO price"
eq "$(j '.errors | length')" 2 "two errors"; has "$(j '.errors[0]')" "No data found" "Yahoo error text"
has "$(j '.errors[1]')" "no offline quote file $Q/yahoo-NOPE.json" "missing file error"
eq "$(j '.quotes.BAC.open')" 'null' "no BAC asked"
A quote BAC --offline "$Q" --json; eq "$(j '.quotes.BAC.open')" 54.439998626708984 "last non-null open"
printf 'Symbol,Date\n' > "$TMP/stooq-NONE.csv"; A quote NONE --offline "$TMP" --source stooq --json; eq "$(j '.errors')" '["Stooq returned no rows for NONE"]' "stooq no rows"
printf 'not json' > "$TMP/yahoo-BAD.json"; A quote bad --offline "$TMP" --json; eq "$(j '.errors')" '["Yahoo returned something unexpected for BAD"]' "yahoo garbage"
echo '{"chart": {"result": [{"meta": {}}]}}' > "$TMP/yahoo-NOP.json"; A quote NOP --offline "$TMP" --json; eq "$(j '.errors')" '["Yahoo has no price for NOP"]' "yahoo no price"
# text
A quote AAPL KO --offline "$Q" --write "$TMP/prices.json" --now 2026-10-13T20:00:01Z
eq "$CODE" 0 "quote exit"
has "$OUT" "AAPL   \$341.27 (+0.09% vs prev close \$340.97)  2026-10-13T20:00:01Z  [yahoo]" "quote line"
has "$OUT" "Wrote 2 price(s) to $TMP/prices.json." "wrote line"
has "$OUT" "Quotes are delayed and for alerts only." "delayed note"
A quote BAC --offline "$Q" --write "$TMP/prices.json"
eq "$(jq -c '.prices | keys' "$TMP/prices.json")" '["AAPL","BAC","KO"]' "write merges"
eq "$(jq -c '.detail.KO' "$TMP/prices.json")" '{"prev_close":89.79,"time":"2026-10-13T20:00:02Z","source":"yahoo"}' "detail"
eq "$(jq -c 'keys_unsorted' "$TMP/prices.json")" '["as_of","source","note","prices","detail"]' "prices doc keys"
echo '{"cash": 0, "positions": [{"ticker": "KO", "shares": 1}]}' > "$TMP/pf.json"
A quote --holdings --portfolio "$TMP/pf.json" --targets "$Q/targets.example.json" --offline "$Q" --json
eq "$CODE" 0 "holdings exit"; eq "$(j '.quotes | keys')" '["AAPL","AXP","BAC","GOOGL","KO"]' "holdings quotes"
eq "$(j '.quotes | keys_unsorted')" '["KO","AAPL","AXP","GOOGL","BAC"]' "holdings order, deduped"
A quote --offline "$Q"; eq "$CODE" 1 "no tickers exit"; has "$ERR" "which tickers?" "no tickers"
echo '{' > "$TMP/broken.json"; A quote --holdings --portfolio "$TMP/broken.json" --offline "$Q"
has "$ERR" "alerts: $TMP/broken.json is not valid JSON: Expecting property name enclosed in double quotes: line 2 column 1 (char 2)" "bad JSON message"

# rules (evaluate via check)
R="$TMP/r"; mkdir -p "$R"
C() { local rules=$1 now=$2; shift 2; echo "{\"rules\": $rules}" > "$R/rules.json"; A check --rules "$R/rules.json" --state "$R/state.json" --prices-out "$R/prices.json" --offline "${QD:-$Q}" --now "$now" --json "$@"; }
rm -f "$R/state.json"; C '[{"id": "aapl-340", "symbol": "AAPL", "type": "above", "price": 340}]' 2026-10-13T10:00
eq "$CODE" 0 "target rang exit"; eq "$(j '.rings | length')" 1 "target rings"
eq "$(j '.rings[0]')" '{"id":"aapl-340","symbol":"AAPL","message":"AAPL is $341.27 is at or above your above $340."}' "target message"
C '[{"id": "aapl-340", "symbol": "AAPL", "type": "above", "price": 340}]' 2026-10-13T10:00
eq "$CODE" 1 "rings once"; eq "$(j '.rings')" '[]' "no second ring"
eq "$(jq -c 'keys_unsorted' "$R/state.json")" '["rung","last_check"]' "state keys"
eq "$(jq -c '.rung' "$R/state.json")" '{"aapl-340":"2026-10-13T14:00:00Z"}' "rung stamp in UTC"
rm -f "$R/state.json"; C '[{"symbol": "AAPL", "type": "target", "price": 340.5, "note": "trim"}]' 2026-10-13T20:00
eq "$(j '.rings[0] | [.id, .message]')" '["AAPL-target","AAPL last $341.27 is at or above your target $340.50 (trim)."]' "closed market says last, default id"
# stop and rearm
QD="$TMP/q"; mkdir -p "$QD"; px() { jq ".chart.result[0].meta.regularMarketPrice = $1" "$Q/yahoo-BAC.json" > "$QD/yahoo-BAC.json"; }
STOP='[{"id": "bac-stop", "symbol": "BAC", "type": "stop", "price": 54, "rearm": true}]'
rm -f "$R/state.json"; px 53.96; C "$STOP" 2026-10-13T10:00; has "$(j '.rings[0].message')" "your stop \$54" "stop rings"
px 55.0; C "$STOP" 2026-10-13T10:00; eq "$(j '.rings')" '[]' "no ring above stop"; eq "$(jq -c '.rung | has("bac-stop")' "$R/state.json")" false "re-armed"
px 53.5; C "$STOP" 2026-10-13T10:00; eq "$(j '.rings | length')" 1 "rings again"
rm -f "$R/state.json"; px 53.96; C '[{"id": "s", "symbol": "BAC", "type": "stop", "price": 54}]' 2026-10-13T10:00; px 55.0; C '[{"id": "s", "symbol": "BAC", "type": "stop", "price": 54}]' 2026-10-13T10:00
eq "$(jq -c '.rung | has("s")' "$R/state.json")" true "no rearm without the flag"
unset QD
# percentage moves from the previous close
rm -f "$R/state.json"; C '[{"id": "bac-3", "symbol": "BAC", "type": "pct_move", "pct": 3}]' 2026-10-13T10:00; eq "$(j '.rings')" '[]' "3% not hit"
rm -f "$R/state.json"; C '[{"id": "bac-1", "symbol": "BAC", "type": "pct_move", "pct": 1, "direction": "down"}]' 2026-10-13T10:00
eq "$(j '.rings[0].message')" '"BAC moved -1.14% to $53.96 (from $54.58)."' "pct move message"
C '[{"id": "bac-1", "symbol": "BAC", "type": "pct_move", "pct": 1, "direction": "down"}]' 2026-10-13T11:00; eq "$(j '.rings')" '[]' "once a day"
C '[{"id": "bac-1", "symbol": "BAC", "type": "pct_move", "pct": 1, "direction": "down"}]' 2026-10-14T10:00; eq "$(j '.rings | length')" 1 "again the next day"
rm -f "$R/state.json"; C '[{"id": "bac-up", "symbol": "BAC", "type": "pct_move", "pct": 1, "direction": "up"}]' 2026-10-13T10:00; eq "$(j '.rings')" '[]' "wrong direction"
rm -f "$R/state.json"; C '[{"id": "r", "symbol": "BAC", "type": "pct_move", "pct": 5, "ref": 50}]' 2026-10-13T10:00; eq "$(j '.rings[0].message')" '"BAC moved +7.92% to $53.96 (from $50.00)."' "ref overrides prev close"
# opening bell, once per trading day
BELL='[{"id": "bell", "type": "opening_bell"}]'
rm -f "$R/state.json" "$R/prices.json"; C "$BELL" 2026-10-13T08:00; eq "$(j '.rings')" '[]' "no bell before the open"
eq "$(j '.quotes')" '{}' "no quotes for a bell"; [ -e "$R/prices.json" ] && bad "prices written without quotes" || ok
C "$BELL" 2026-10-13T10:00; eq "$(j '.rings[0].message')" '"Opening bell: the NYSE is open (2026-10-13 10:00 New York)."' "bell message"
C "$BELL" 2026-10-13T11:00; eq "$(j '.rings')" '[]' "bell once a day"
C "$BELL" 2026-10-14T09:31; eq "$(j '.rings | length')" 1 "bell next day"
rm -f "$R/state.json"; C "$BELL" 2026-11-27T09:45; eq "$(j '.rings[0].message')" '"Opening bell: the NYSE is open (2026-11-27 09:45 New York), early close at 1:00 p.m. today, day after Thanksgiving."' "early close bell"
# bad rules
C '[{"id": "x", "type": "moon"}]' 2026-10-13T10:00; eq "$CODE" 1 "unknown type exit"; has "$ERR" "alerts: rule x: unknown type 'moon'" "unknown type"
C '[{"id": "x", "type": "above", "symbol": "AAPL"}]' 2026-10-13T10:00; has "$ERR" "rule x needs a numeric price" "needs price"
C '[{"type": "pct_move", "symbol": "AAPL", "pct": "3"}]' 2026-10-13T10:00; has "$ERR" "rule None needs a numeric pct" "needs pct, id None"
C '[{"id": "x", "type": "below"}]' 2026-10-13T10:00; has "$ERR" "rule x needs a symbol" "needs symbol"
C '[{"id": "x", "type": null}]' 2026-10-13T10:00; has "$ERR" "rule x: unknown type None" "type None"
C '[{"id": "x", "symbol": "AAPL", "type": "above", "price": 1}]' 2026-10-13T10:00 --no-write; eq "$(j '.rings | length')" 1 "--no-write rings"
eq "$(jq -c '.rung | has("x")' "$R/state.json")" false "--no-write leaves state"

# the evening and morning of a real portfolio
D="$TMP/cli"; mkdir -p "$D"
for args in "--opening-bell" "--symbol AAPL --above 350" "--symbol BAC --stop 50"; do A add --rules "$D/alerts.json" $args; eq "$CODE" 0 "add $args"; done
eq "$OUT" 'Added bac-stop-50: {"id": "bac-stop-50", "symbol": "BAC", "type": "stop", "price": 50.0}' "added line"
A add --rules "$D/alerts.json" --symbol AAPL --above 350; eq "$CODE" 1 "duplicate exit"; has "$ERR" "a rule with id aapl-above-350 already exists" "duplicate"
A add --rules "$D/alerts.json" --symbol ko --pct 2.5 --direction up --rearm --note "big day" --id ko-up; eq "$CODE" 0 "add pct"
eq "$(jq -c '.rules[3]' "$D/alerts.json")" '{"id":"ko-up","symbol":"KO","type":"pct_move","pct":2.5,"direction":"up","rearm":true,"note":"big day"}' "pct rule"
A add --rules "$D/alerts.json" --symbol KO; eq "$CODE" 1 "no kind exit"; has "$ERR" "give one of --above, --below, --stop or --pct" "no kind"
A add --rules "$D/alerts.json" --above 3; has "$ERR" "--symbol is required (or --opening-bell)" "no symbol"
A add --rules "$D/alerts.json" --symbol KO --above 1 --below 2; eq "$CODE" 2 "exclusive exit"; has "$ERR" "argument --below: not allowed with argument --above" "exclusive"
CK() { A check --rules "$D/alerts.json" --state "$D/state/alerts.json" --prices-out "$D/prices.json" --offline "$Q" --now "$1"; }
CK 2026-10-13T20:00; eq "$CODE" 1 "evening exit"; has "$OUT" "Nothing hit. 4 rule(s) watched." "nothing hit"
has "$OUT" "NYSE closed (2026-10-13 20:00 New York, after the close)." "check header"
eq "$(jq -c '.prices' "$D/prices.json")" '{"AAPL":341.27,"BAC":53.96,"KO":90.11}' "prices written"
CK 2026-10-14T09:31; eq "$CODE" 0 "morning exit"; has "$OUT" "ALERT Opening bell" "bell"; has "$OUT" "not an order" "not an order"
CK 2026-10-14T09:45; eq "$CODE" 1 "quiet after"
A list --rules "$D/alerts.json" --state "$D/state/alerts.json"; eq "$CODE" 0 "list exit"
has "$OUT" 'opening-bell             {"type": "opening_bell"}  (rang 2026-10-14T13:31:00Z)' "list rang"
has "$OUT" 'aapl-above-350           {"symbol": "AAPL", "type": "above", "price": 350.0}' "list rule"
A reset opening-bell --state "$D/state/alerts.json"; eq "$CODE" 0 "reset"; eq "$OUT" "Re-armed opening-bell." "reset text"
A reset opening-bell --state "$D/state/alerts.json"; eq "$CODE" 1 "reset again"; eq "$OUT" "opening-bell has not rung (nothing to reset)." "nothing to reset"
A list --rules "$TMP/none.json" --state "$TMP/none.json"; eq "$CODE" 1 "empty list"; eq "$OUT" "No rules yet." "no rules yet"
A check --rules "$TMP/none.json" --offline "$Q"; eq "$CODE" 1 "check without rules"; has "$ERR" "no rules in $TMP/none.json" "no rules"
A; eq "$CODE" 1 "no command exit"; has "$OUT" "    clock               is the NYSE open?" "subcommand help listed"
done_ "trader-alerts"

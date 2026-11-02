#!/usr/bin/env bash
# daytrade-alerts/alerts: the assertions of day-trader-skill/tests/test_alerts.py, run against the C++ tool (--offline).
# (The FakeNet cases, query2 -> query1 fallback and the 429 retry, need a scripted network and are not run here.)
source "$(dirname "$0")/lib.sh"
Q="$FIX/daytrade-alerts"; PLAN="$FIX/daytrade-budget/plan-tsla.json"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
A() { run daytrade-alerts/alerts "$@"; }
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
A clock --now tomorrow; eq "$CODE" 1 "bad --now exit"; has "$ERR" 'alerts: --now wants a time like "2026-10-19 10:15" (New York) or ...Z (UTC)' "bad --now message"
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
A quote BAC --offline "$Q" --json; eq "$(j '.quotes.BAC.open')" "$(jq -c '[.chart.result[0].indicators.quote[0].open[] | select(. != null)][0]' "$Q/yahoo-BAC.json")" "first non-null open"
printf 'Symbol,Date\n' > "$TMP/stooq-NONE.csv"; A quote NONE --offline "$TMP" --source stooq --json; eq "$(j '.errors')" '["Stooq returned no rows for NONE"]' "stooq no rows"
printf 'not json' > "$TMP/yahoo-BAD.json"; A quote bad --offline "$TMP" --json; eq "$(j '.errors')" '["Yahoo returned something unexpected for BAD"]' "yahoo garbage"
echo '{"chart": {"result": [{"meta": {}}]}}' > "$TMP/yahoo-NOP.json"; A quote NOP --offline "$TMP" --json; eq "$(j '.errors')" '["Yahoo has no price for NOP"]' "yahoo no price"
# text
A quote AAPL KO --offline "$Q" --write "$TMP/prices.json" --now 2026-10-13T20:00:01Z
eq "$CODE" 0 "quote exit"
has "$OUT" "AAPL   \$341.27 (+0.09% vs prev close \$340.97)  2026-10-13T20:00:01Z  [yahoo]" "quote line"
has "$OUT" "Wrote 2 price(s) to $TMP/prices.json." "wrote line"
has "$OUT" "Quotes are free, delayed and unofficial: for alerts only." "delayed note"
A quote BAC --offline "$Q" --write "$TMP/prices.json"
eq "$(jq -c '.prices | keys' "$TMP/prices.json")" '["AAPL","BAC","KO"]' "write merges"
eq "$(jq -c '.detail.KO' "$TMP/prices.json")" '{"prev_close":89.79,"time":"2026-10-13T20:00:02Z","source":"yahoo","fetched":"2026-10-13T20:00:01Z"}' "detail with fetched"
eq "$(jq -c 'keys_unsorted' "$TMP/prices.json")" '["as_of","source","note","prices","detail"]' "prices doc keys"
echo '{"cash": 0, "positions": [{"ticker": "KO", "shares": 1}]}' > "$TMP/pf.json"
A quote --holdings --portfolio "$TMP/pf.json" --plan "$PLAN" --offline "$Q" --json
eq "$CODE" 0 "holdings exit"; eq "$(j '.quotes | keys_unsorted')" '["KO","TSLA"]' "holdings from portfolio and plan"
echo '{"cash": 0, "positions": [{"ticker": "TSLA", "shares": 8}]}' > "$TMP/pf2.json"; A quote --holdings --portfolio "$TMP/pf2.json" --plan "$PLAN" --offline "$Q" --json
eq "$(j '.quotes | keys_unsorted')" '["TSLA"]' "holdings deduped"
A quote --offline "$Q"; eq "$CODE" 1 "no tickers exit"; has "$ERR" "which tickers?" "no tickers"
echo '{' > "$TMP/broken.json"; A quote --holdings --portfolio "$TMP/broken.json" --offline "$Q"
has "$ERR" "alerts: $TMP/broken.json is not valid JSON: Expecting property name enclosed in double quotes: line 2 column 1 (char 2)" "bad JSON message"

# rules (evaluate via check)
R="$TMP/r"; mkdir -p "$R"
C() { local rules=$1 now=$2; shift 2; echo "{\"rules\": $rules}" > "$R/rules.json"; A check --rules "$R/rules.json" --state "$R/state.json" --prices-out "$R/prices.json" --offline "${QD:-$Q}" --now "$now" --json "$@"; }
rm -f "$R/state.json"; C '[{"id": "aapl-340", "symbol": "AAPL", "type": "above", "price": 340}]' 2026-10-13T10:00
eq "$CODE" 0 "target rang exit"; eq "$(j '.rings | length')" 1 "target rings"
eq "$(j '.rings[0]')" '{"id":"aapl-340","symbol":"AAPL","message":"AAPL is $341.27: at or above your alert level $340."}' "target message"
C '[{"id": "aapl-340", "symbol": "AAPL", "type": "above", "price": 340}]' 2026-10-13T10:00
eq "$CODE" 1 "rings once"; eq "$(j '.rings')" '[]' "no second ring"
eq "$(jq -c 'keys_unsorted' "$R/state.json")" '["rung","last_check"]' "state keys"
eq "$(jq -c '.rung' "$R/state.json")" '{"aapl-340":"2026-10-13T14:00:00Z"}' "rung stamp in UTC"
rm -f "$R/state.json"; C '[{"symbol": "AAPL", "type": "target", "price": 340.5, "note": "trim"}]' 2026-10-13T20:00
eq "$(j '.rings[0] | [.id, .message]')" '["AAPL-target","AAPL last $341.27: at or above your target $340.50 (trim)."]' "closed market says last, default id"
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
A add --rules "$D/alerts.json" --symbol KO; eq "$CODE" 1 "no kind exit"; has "$ERR" "give one of --above, --below, --stop, --target or --pct" "no kind"
A add --rules "$D/alerts.json" --above 3; has "$ERR" "--symbol is required (or --from-plan, --at, --opening-bell)" "no symbol"
A add --rules "$D/alerts.json" --symbol KO --above 1 --below 2; eq "$CODE" 2 "exclusive exit"; has "$ERR" "argument --below: not allowed with argument --above" "exclusive"
CK() { A check --rules "$D/alerts.json" --state "$D/state/alerts.json" --prices-out "$D/prices.json" --offline "$Q" --now "$1"; }
CK 2026-10-13T20:00; eq "$CODE" 1 "evening exit"; has "$OUT" "Nothing hit. 4 rule(s) watched today, AAPL \$341.27, BAC \$53.96, KO \$90.11." "nothing hit lists the quotes"
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

# ---- day-trade additions
A quote TSLA --offline "$Q" --json
eq "$(j '.quotes.TSLA | [.price, .prev_close, (.open * 100 | round / 100), .time]')" '[366.475,354.72,357.48,"2026-10-19T14:52:13Z"]' "TSLA fixture"
A clock --now "2026-10-19 10:15" --json; eq "$CODE" 0 "clock with a space"; eq "$(j '.now_new_york')" '"2026-10-19 10:15"' "space form is New York"
eq "$(asof '2026-10-19 10:15')" "2026-10-19T14:15:00Z" "2026-10-19 10:15 New York is 14:15 UTC"
# offline .json.gz
G="$TMP/gz"; mkdir -p "$G"; gzip -c "$Q/yahoo-TSLA.json" > "$G/yahoo-TSLA.json.gz"
A quote TSLA --offline "$G" --json; eq "$(j '[.quotes.TSLA.price, .errors]')" '[366.475,[]]' "gzipped offline quote"
printf 'junk' > "$G/yahoo-BAD.json.gz"; A quote BAD --offline "$G" --json; has "$(j '.errors[0]')" "yahoo-BAD.json.gz" "a broken .gz is an error, not a crash"
# a pct move on the real 10:52 quote
rm -f "$R/state.json"; C '[{"id": "tsla-3", "symbol": "TSLA", "type": "pct_move", "pct": 3}]' "2026-10-19 10:52"
has "$(j '.rings[0].message')" "TSLA moved +3.31% to \$366.48 (from \$354.72)" "TSLA +3.31%"
# the plan gives a stop, a target and flat-by-close, for the plan's day
P="$TMP/plan"; mkdir -p "$P"
A add --from-plan --plan "$PLAN" --rules "$P/alerts.json" --state "$P/state.json" --config "$P/none"; eq "$CODE" 0 "add --from-plan"
eq "$(jq -c '[.rules[] | [.id, .type]]' "$P/alerts.json")" '[["tsla-plan-stop","stop"],["tsla-plan-target","target"],["tsla-flat-by","time"]]' "plan rules"
eq "$(jq -c '[.rules[0].price, .rules[1].price, .rules[2].at, .rules[2].label, .rules[2].before_close, ([.rules[].date] | unique)]' "$P/alerts.json")" '[358.32,370.04,"15:55","flat by close",true,["2026-10-19"]]' "plan rule fields"
eq "$(jq -c '[.rules[] | select(.id | startswith("tsla-plan") or . == "tsla-flat-by")]' "$Q/alerts.example.json")" "$(jq -c '.rules' "$P/alerts.json")" "same as the example's plan rules"
has "$OUT" 'Added tsla-flat-by: {"id": "tsla-flat-by", "symbol": "TSLA", "type": "time", "at": "15:55", "label": "flat by close", "before_close": true, "date": "2026-10-19"}' "added line"
has "$OUT" "Free quotes are delayed; the broker's bracket orders are what actually stop you out." "delayed reminder"
PR=$(jq -c '.rules' "$P/alerts.json")
rm -f "$R/state.json"; C "$PR" "2026-10-19 10:52"; eq "$(j '.rings')" '[]' "366.475 rings nothing"
QD="$TMP/qt"; mkdir -p "$QD"; tpx() { jq ".chart.result[0].meta.regularMarketPrice = $1" "$Q/yahoo-TSLA.json" > "$QD/yahoo-TSLA.json"; }
tpx 358.10; C "$PR" "2026-10-19 11:30"; eq "$(j '[.rings[].id]')" '["tsla-plan-stop"]' "the stop rings"
has "$(j '.rings[0].message')" "TSLA is \$358.10: at or below your stop \$358.32 (plan stop: opening-range midpoint)" "stop message"
tpx 370.04; C "$PR" "2026-10-19 12:00"; eq "$(j '[.rings[].id]')" '["tsla-plan-target"]' "the target rings"
unset QD
# flat by close rings once per trading day, plan rules on their day only
FR=$(jq -c '.rules[2:]' "$P/alerts.json")
rm -f "$R/state.json"; C "$FR" "2026-10-19 15:54"; eq "$(j '.rings')" '[]' "not yet at 15:54"
C "$FR" "2026-10-19 15:55"; eq "$(j '.rings | length')" 1 "rings at 15:55"
has "$(j '.rings[0].message')" "Flat by close: it is 15:55 New York (set for 15:55), the market closes at 16:00." "flat message"
has "$(j '.rings[0].message')" "trader-broker flatten --symbol TSLA" "points at flatten"
C "$FR" "2026-10-19 15:59"; eq "$(j '.rings')" '[]' "once"
rm -f "$R/state.json"; C "$FR" "2026-10-19 16:10"; has "$(j '.rings[0].message')" "the market has closed." "after the close"
C "$FR" "2026-10-20 15:56"; eq "$(j '.rings')" '[]' "the plan's day only"
eq "$(jq -r '.rung["tsla-flat-by"]' "$R/state.json")" "2026-10-19T20:10:00Z" "stamp kept"
# a time alert moves with an early close
TR='[{"id": "flat", "type": "time", "at": "15:55", "label": "flat by close", "before_close": true}]'
rm -f "$R/state.json"; C "$TR" "2026-11-27 12:54"; eq "$(j '.rings')" '[]' "early close: not at 12:54"
C "$TR" "2026-11-27 12:55"; has "$(j '.rings[0].message')" "set for 12:55), the market closes at 13:00" "moved to 12:55"
rm -f "$R/state.json"; C '[{"id": "t", "type": "time", "at": "15:55", "label": "check"}]' "2026-11-27 12:55"; eq "$(j '.rings')" '[]' "without before_close it stays at 15:55"
for d in "2026-10-17 15:56" "2026-11-26 15:56"; do rm -f "$R/state.json"; C "$TR" "$d"; eq "$(j '.rings')" '[]' "no session $d"; done
rm -f "$R/state.json"; C "$TR" "2026-12-01 15:55"; eq "$(j '.rings | length')" 1 "standard time"; eq "$(jq -r '.rung.flat' "$R/state.json")" "2026-12-01T20:55:00Z" "standard time stamp"
# a rung stamp from a New York evening counts on its New York day
echo '{"rung": {"flat": "2026-10-20T00:30:00Z"}}' > "$R/state.json"; C "$TR" "2026-10-19 15:55"; eq "$(j '.rings')" '[]' "00:30Z on the 20th is the 19th in New York"
# the example rules are valid; bad time rules and plans
C "$(jq -c '.rules' "$Q/alerts.example.json")" "2026-10-19 10:53" --no-write; eq "$(j '.errors')" '[]' "example rules valid"
C '[{"id": "x", "type": "time", "at": "4pm"}]' "2026-10-19 10:00"; eq "$CODE" 1 "bad at"; has "$ERR" 'rule x needs "at" as a New York time like "15:55"' "bad at message"
C '[{"id": "x", "type": "opening_bell", "date": "19/10/2026"}]' "2026-10-19 10:00"; has "$ERR" "rule x: date must be YYYY-MM-DD" "bad date"
for t in "9:30" "09:30" "23:59" "0:00"; do C "[{\"id\": \"x\", \"type\": \"time\", \"at\": \"$t\"}]" "2026-10-19 10:00" --no-write; eq "$CODE$(j '.errors')" "$([ "$t" = 9:30 ] || [ "$t" = 09:30 ] || [ "$t" = 0:00 ] && echo 0 || echo 1)[]" "time $t valid"; done
for t in "24:00" "9:5" "9:60" "123:00" "aa:bb"; do C "[{\"id\": \"x\", \"type\": \"time\", \"at\": \"$t\"}]" "2026-10-19 10:00" --no-write; eq "$CODE" 1 "time $t invalid"; has "$ERR" "needs \"at\"" "time $t message"; done
echo '{"symbol": "TSLA", "stop": 358.32}' > "$P/half.json"; A add --from-plan --plan "$P/half.json" --rules "$P/x.json"; eq "$CODE" 1 "plan without target"; has "$ERR" "the plan has no target (run trader-exit)" "no target message"
echo '{"stop": 1}' > "$P/nosym.json"; A add --from-plan --plan "$P/nosym.json" --rules "$P/x.json"; has "$ERR" "the plan has no symbol (run trader-exit first)" "no symbol message"
echo '{"symbol": "TSLA", "stop": 358.32, "target": 370.04, "flat_by": "3:55pm"}' > "$P/badflat.json"; A add --from-plan --plan "$P/badflat.json" --rules "$P/x.json"; has "$ERR" "the plan's flat_by '3:55pm' is not a time like 15:55" "bad flat_by"
echo '{"symbol": "tsla", "stop": 358.32, "target": 370.04}' > "$P/nodate.json"; echo '{"flat_by": "15:45"}' > "$P/cfg.json"
A add --from-plan --plan "$P/nodate.json" --rules "$P/y.json" --config "$P/cfg.json"; eq "$(jq -c '[.rules[2].at, (.rules | map(has("date")) | any), .rules[0].note]' "$P/y.json")" '["15:45",false,"plan stop"]' "flat_by from the config, no date, no label"
# the reel trade from the plan, through the CLI
D2="$TMP/reel"; mkdir -p "$D2"
A add --from-plan --plan "$PLAN" --rules "$D2/alerts.json" --state "$D2/state.json" --config "$D2/none"; has "$OUT" "Added tsla-flat-by" "reel add"
A add --rules "$D2/alerts.json" --opening-bell; eq "$CODE" 0 "add bell"
CK2() { A check --rules "$D2/alerts.json" --state "$D2/state.json" --prices-out "$D2/prices.json" --offline "$Q" --now "$1"; }
CK2 "2026-10-19 10:52"; eq "$CODE" 0 "the opening bell rings first"
CK2 "2026-10-19 10:53"; eq "$CODE" 1 "quiet at 10:53"; has "$OUT" "Nothing hit. 4 rule(s) watched today, TSLA \$366.48." "watched today"
eq "$(jq -c '[.prices, .detail.TSLA.time, .detail.TSLA.fetched]' "$D2/prices.json")" '[{"TSLA":366.475},"2026-10-19T14:52:13Z","2026-10-19T14:53:00Z"]' "prices.json with fetched"
CK2 "2026-10-19 15:55"; eq "$CODE" 0 "flat by close"; has "$OUT" "ALERT Flat by close" "flat alert"; has "$OUT" "not an order" "not an order"
has "$OUT" "record any fill (ticket fill). To close a position yourself: ticket flatten, then trader-risk and your yes." "next steps"
CK2 "2026-10-19 15:57"; eq "$CODE" 1 "once"
CK2 "2026-10-20 10:00"; has "$OUT" "ALERT Opening bell" "next day's bell"
CK2 "2026-10-20 10:05"; has "$OUT" "Nothing hit. 1 rule(s) watched today." "plan rules gone the next day"
A add --from-plan --plan "$PLAN" --rules "$D2/alerts.json" --state "$D2/state.json"
eq "$(jq -c '[.rules[].id] | sort' "$D2/alerts.json")" '["opening-bell","tsla-flat-by","tsla-plan-stop","tsla-plan-target"]' "a new plan replaces the plan rules"
eq "$(jq -c '.rung | has("tsla-flat-by")' "$D2/state.json")" false "and re-arms them"; eq "$(jq -c '.rung | has("opening-bell")' "$D2/state.json")" true "other rungs kept"
# add a time alert and a target
D3="$TMP/add"; mkdir -p "$D3"
A add --rules "$D3/a.json" --at 15:55 --label "flat by close" --before-close --symbol tsla; eq "$CODE" 0 "add --at"
A add --rules "$D3/a.json" --symbol TSLA --target 370.04 --date 2026-10-19; eq "$CODE" 0 "add --target"
eq "$(jq -c '.rules[0]' "$D3/a.json")" '{"id":"time-1555","type":"time","at":"15:55","label":"flat by close","symbol":"TSLA","before_close":true}' "time rule"
eq "$(jq -c '.rules[1]' "$D3/a.json")" '{"id":"tsla-target-370.04","symbol":"TSLA","type":"target","price":370.04,"date":"2026-10-19"}' "target rule"
A add --rules "$D3/a.json" --at 4pm; eq "$CODE" 1 "bad --at"; has "$ERR" "--at wants a New York time like 15:55" "bad --at message"
A add --rules "$D3/a.json" --from-plan --plan "$D3/none"; eq "$CODE" 1 "no plan"; has "$ERR" "no plan at $D3/none (run trader-exit first)" "no plan message"
A add --rules "$D3/a.json" --at 15:00 --from-plan; eq "$CODE" 2 "--at and --from-plan exclusive"
A add --rules "$D3/a.json" --opening-bell --date 2026-13-01 --id b2; eq "$CODE" 0 "date shape only"
A clock --now "2026-10-19 10:15"; eq "$CODE" 0 "clock 10:15 open"
done_ "daytrade-alerts"

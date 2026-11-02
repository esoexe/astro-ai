#!/usr/bin/env bash
# daytrade-news/news: the assertions of day-trader-skill/tests/test_news.py, run offline against the C++ tool (no network).
source "$(dirname "$0")/lib.sh"
DIR="$FIX/daytrade-news"; NOW=2026-10-14T00:00:00+00:00; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
base() { echo --offline "${OFF:-$DIR}" --state "$TMP/state/news.json" --now "$NOW" --portfolio "${PF:-$TMP/none.json}" --watchlist "${WL:-$TMP/none.json}" --plan "${PL:-$TMP/none.json}"; }
news() { run daytrade-news/news "$@" $(base); }
j() { echo "$OUT" | jq -c "$1"; }
unset SEC_USER_AGENT

# feeds: Yahoo RSS
news --tickers AAPL --no-sec --all --hours 100000 --json --dry-run
eq "$(j '.items | length')" 6 "6 Apple headlines"
eq "$(j '.items[] | select(.title=="Apple may have a way to dominate the smart home space") | .time')" '"2026-10-13T19:46:52+00:00"' "RFC 822 time"
eq "$(j '[.items[] | (.link|startswith("https://finance.yahoo.com/") or startswith("https://app.moby.co/")), (.id|startswith("rss:"))] | all')" true "links and rss ids"
eq "$(j '.items[] | select(.title|startswith("Apple Hits")) | .link')" '"https://app.moby.co/home/news/news-apple-hits-fresh-all-time-high?utm_source=yahoo_finance&utm_medium=rss&.tsrc=rss"' "entities decoded"
eq "$(j '.items[] | select(.title|startswith("Apple Hits")) | .tags')" '["people"]' "New CEO tags people"
# feeds: SEC Atom (ISO-8859-1)
news --tickers AAPL --all --hours 100000 --json --dry-run
eq "$(j '[.items[] | select(.kind=="filing") | .form]')" '["8-K/A","8-K","8-K"]' "filing forms"
eq "$(j '[.items[] | select(.kind=="filing")][1] | [.items, .important, .title, .id]')" '[["2.02","9.01"],true,"Apple Inc. filed a 8-K: earnings results","sec:0000320193-26-000018"]' "earnings filing"
has "$(j '[.items[] | select(.kind=="filing")][1].link')" "000032019326000018" "filing link"
eq "$(j '[.items[] | select(.kind=="filing")][1].time')" '"2026-07-30T20:30:28+00:00"' "16:30:28 New York is 20:30:28 UTC"
eq "$(j '[.items[] | select(.kind=="filing")][0].title')" '"Apple Inc. filed a 8-K/A: director or officer change"' "8-K/A title"
eq "$(j '[.items[] | select(.kind=="filing")][0] | keys_unsorted')" '["id","source","kind","form","ticker","title","summary","items","important","link","time","touches","tags","new"]' "filing keys"
has "$OUT" '\u2019' "json escapes non-ASCII like Python"
# times
run daytrade-news/news --tickers KO --no-sec --hours 1 --json --dry-run --offline "$DIR" --state "$TMP/state/news.json" --portfolio "$TMP/none.json" --watchlist "$TMP/none.json" --plan "$TMP/none.json" --now "Tue, 13 Oct 2026 20:02:03 +0000"; eq "$(j '.checked')" '"2026-10-13T20:02:03+00:00"' "parse RFC 822 --now"
NOW="2026-09-01T16:30:35-04:00" news --tickers KO --no-sec --hours 1 --json --dry-run; eq "$(j '.checked')" '"2026-09-01T20:30:35+00:00"' "parse ISO --now"
run daytrade-news/news --tickers KO --no-sec --hours 1 --json --dry-run --offline "$DIR" --state "$TMP/state/news.json" --portfolio "$TMP/none.json" --watchlist "$TMP/none.json" --plan "$TMP/none.json" --now "Tue, 13 Oct 2026 16:02:03 EDT"; eq "$(j '.checked')" '"2026-10-13T20:02:03+00:00"' "named zone"
NOW=soon news --tickers KO --no-sec --dry-run; eq "$CODE" 1 "unparseable --now fails"
# the offline file is named by ticker; the URL uses Yahoo's symbol
news --tickers brk.b --no-sec --dry-run; has "$OUT" "note: headlines for BRK.B: offline: no file $DIR/yahoo-BRK.B.xml" "offline name"

# tagging: touches and tags
echo '{"positions": [{"ticker": "KO", "name": "Coca-Cola"}, {"ticker": "CVX", "name": "Chevron"}, {"ticker": "AAPL", "name": "Apple"}]}' > "$TMP/pf.json"
PF="$TMP/pf.json" news --tickers KO CVX AAPL --no-sec --all --hours 100000 --json --dry-run
eq "$(j '.items[] | select(.ticker=="KO" and (.title|startswith("Coca-Cola or Chevron"))) | .touches')" '["CVX","KO"]' "Chevron touches CVX and KO"
has "$(j '.items[] | select(.ticker=="KO" and (.title|startswith("Coca-Cola or Chevron"))) | .tags')" '"capital"' "dividend is capital"
eq "$(j '.items[] | select(.title|startswith("Is Coca-Cola (KO) St")) | .touches')" '["KO"]' "(KO) mention"
eq "$(j '.notes')" "[\"headlines for CVX: offline: no file $DIR/yahoo-CVX.xml\"]" "missing feed is a note"
# earnings filings are tagged
news --tickers KO --all --hours 100000 --json --dry-run
eq "$(j '[.items[] | select(.kind=="filing")][0].tags[0]')" '"earnings"' "2.02 tagged earnings"
eq "$(j '[.items[] | select(.kind=="filing")][1].important')" false "8.01 not important"
# dedupe merges the same story
mkdir -p "$TMP/dup"; cp "$DIR/yahoo-AAPL.xml" "$TMP/dup/yahoo-AAPL.xml"; cp "$DIR/yahoo-AAPL.xml" "$TMP/dup/yahoo-GOOGL.xml"
OFF="$TMP/dup" news --tickers AAPL GOOGL --no-sec --all --hours 100000 --json --dry-run
eq "$(j '.items | length')" 6 "deduped to 6"; eq "$(j '[.items[].touches] | unique')" '[["AAPL","GOOGL"]]' "touches merged"

# the check: new items once, newest first
news --tickers AAPL KO --hours 24 --json; eq "$CODE" 0 "new items exit 0"
eq "$(j '[.items[].time] == ([.items[].time] | sort | reverse)')" true "newest first"
eq "$(j '.items | length')" 10 "6 Apple + 4 Coca-Cola headlines"
eq "$(jq -r '.seen | has("'"$(printf %s 'rss:1fe6cc83-8d8f-3bbc-a80a-df68fd475409' | sha1sum | cut -c1-16)"'")' "$TMP/state/news.json")" true "sha1 fingerprint"
eq "$(jq -c '[.ciks.AAPL, .ciks.KO, .ciks["BRK.B"], .last_check, (keys_unsorted)]' "$TMP/state/news.json")" "[\"0000320193\",\"0000021344\",\"0001067983\",\"$NOW\",[\"ciks\",\"seen\",\"last_check\"]]" "state: ciks cached, BRK-B as BRK.B"
news --tickers AAPL KO --hours 24; eq "$CODE" 1 "second run exit 1"; has "$OUT" "Nothing new." "nothing new"
eq "$(echo "$OUT" | tail -1)" "Checked 2026-10-13 20:00 ET. Headlines are leads, not facts; open the story before acting. Not financial advice." "checked line"
# filings show in a wider window
rm -rf "$TMP/state"; news --tickers AAPL --hours 2400; eq "$CODE" 0 "wide window"
has "$OUT" "! 2026-07-30 16:30 ET  AAPL        SEC EDGAR     Apple Inc. filed a 8-K: earnings results" "filing line"
has "$OUT" "      tags: earnings  https://www.sec.gov/Archives/edgar/data/320193/000032019326000018/0000320193-26-000018-index.htm" "tags + link line"
has "$OUT" "Not financial advice" "not advice"; hasnt "$OUT" "—" "no em dash"
# tickers from the scanner's watchlist and the plan
rm -rf "$TMP/state"; echo '{"created_by": "trader-scanner", "hits": [{"symbol": "KO", "setups": ["orb"]}]}' > "$TMP/watchlist.json"; echo '{"created_by": "trader-exit", "symbol": "AAPL"}' > "$TMP/plan.json"
WL="$TMP/watchlist.json" PL="$TMP/plan.json" news --json --dry-run --hours 24; eq "$CODE" 0 "watchlist + plan"
eq "$(j '[.items[].touches[]] | unique')" '["AAPL","KO"]' "watchlist + plan give AAPL and KO"; [ ! -e "$TMP/state/news.json" ] && ok || bad "dry run writes nothing"
# no tickers
news; eq "$CODE" 2 "no tickers exit 2"; eq "$ERR" "news: no tickers. Pass --tickers, or run /trader-scanner first (it writes trader/watchlist.json)." "no tickers message"
# state forgets after 30 days
mkdir -p "$TMP/state" "$TMP/empty"; echo '{"seen": {"old": "2026-08-01T00:00:00+00:00", "recent": "2026-10-01T00:00:00+00:00"}}' > "$TMP/state/news.json"
OFF="$TMP/empty" news --tickers KO; eq "$CODE" 1 "empty check"
eq "$(jq -c '[.seen, .last_check]' "$TMP/state/news.json")" "[{\"recent\":\"2026-10-01T00:00:00+00:00\"},\"$NOW\"]" "old seen dropped"
has "$OUT" "note: SEC ticker list: offline: no file $TMP/empty/company_tickers.json" "ticker list note"
# XML details: Latin-1, character references, CDATA, bad XML
mkdir -p "$TMP/x"; printf '<?xml version="1.0" encoding="ISO-8859-1"?>\n<rss><channel><item><title>Nestl\xe9 &#8217;s <![CDATA[big & bold]]> deal</title><link>https://example.com/a</link><pubDate>Tue, 13 Oct 2026 20:00:00 GMT</pubDate></item></channel></rss>' > "$TMP/x/yahoo-NSRGY.xml"
printf '<rss><item><title>broken</item></rss>' > "$TMP/x/yahoo-BAD.xml"
OFF="$TMP/x" news --tickers NSRGY BAD --no-sec --json --dry-run
eq "$(j '.items[0] | [.title, .tags, .time]')" '["Nestlé ’s big & bold deal",["deal"],"2026-10-13T20:00:00+00:00"]' "latin-1 + refs + CDATA"
eq "$(j '.notes')" '["headlines for BAD: RSS for BAD is not valid XML: mismatched tag: line 1, column 26"]' "bad XML note"
# watch list order and names: hits, then the plan (kept if already there), then holdings (their name wins)
echo '{"hits": [{"ticker": "ko", "name": "Coke"}, {"symbol": "TSLA"}, {"name": "no symbol"}]}' > "$TMP/wl2.json"; echo '{"symbol": "ko", "name": "Plan"}' > "$TMP/pl2.json"
echo '{"positions": [{"symbol": "TSLA", "name": "Tesla"}, {"ticker": "KO"}]}' > "$TMP/pf2.json"
WL="$TMP/wl2.json" PL="$TMP/pl2.json" PF="$TMP/pf2.json" NOW="2026-10-19T10:15:00-04:00" news --no-sec --no-moves --hours 6 --json --dry-run; eq "$CODE" 0 "merged watch list"
eq "$(j '[.items[].touches[]] | unique')" '["TSLA"]' "TSLA headlines from the watchlist"; hasnt "$OUT" '"KO"' "KO has no headlines in 6 hours"

# the reel: TSLA headlines, Monday Oct 19 2026, checked at 10:15 AM ET
NOW="2026-10-19T10:15:00-04:00" news --tickers TSLA --hours 6 --no-sec --no-moves; eq "$CODE" 0 "reel run"
has "$OUT" "2026-10-19 08:04 ET  TSLA" "headline time in ET"; hasnt "$OUT" "10:32 ET" "nothing after the pinned clock"
has "$OUT" "Tesla-SpaceX Merger Mania Strikes Wall Street Ahead of Earnings" "merger headline"
TAGS=$(echo "$OUT" | grep -A1 "Merger Mania" | tail -1); has "$TAGS" "earnings" "merger: earnings tag"; has "$TAGS" "deal" "merger: deal tag"
has "$OUT" "Checked 2026-10-19 10:15 ET" "checked in ET"
eq "$(echo "$OUT" | grep -c '^  2026-10-19')" 16 "16 of 18 headlines (2 after 10:15)"
# the price line, from the chart fetched at 10:52
NOW="2026-10-19T10:52:00-04:00" news --tickers TSLA --hours 6 --no-sec --dry-run --all
eq "$(echo "$OUT" | head -1)" "TSLA   366.48 +3.31% vs yesterday's close 354.72 at 10:52 ET  MOVING" "move line"
eq "$(echo "$OUT" | sed -n 2p)" "" "blank line after the moves"; has "$(echo "$OUT" | sed -n 3p)" " ET  TSLA" "headlines follow"
NOW="2026-10-19T10:52:00-04:00" news --tickers TSLA --hours 6 --no-sec --dry-run --json
eq "$(j '.moves')" '[{"ticker":"TSLA","price":366.48,"prev_close":354.72,"pct":3.31,"time":"2026-10-19T14:52:13+00:00","moving":true}]' "moves in JSON"
eq "$(j 'keys_unsorted')" '["checked","moves","items","notes"]' "JSON key order"
NOW="2026-10-19T10:52:00-04:00" news --tickers TSLA --hours 6 --no-sec --dry-run --move-pct 4; eq "$(echo "$OUT" | head -1)" "TSLA   366.48 +3.31% vs yesterday's close 354.72 at 10:52 ET  " "under --move-pct: no flag"
NOW="2026-10-19T10:52:00-04:00" news --tickers TSLA --hours 6 --no-sec --dry-run --json --move-pct 3.31; eq "$(j '.moves[0].moving')" true "exactly --move-pct is moving"
# charts that do not work
mkdir -p "$TMP/c"; cp "$DIR/yahoo-TSLA.xml" "$TMP/c/"; echo '{"chart": {"result": null}}' > "$TMP/c/chart-ZZZZ.json"; echo '{"chart": {"result": [{"meta": {"regularMarketPrice": 0, "previousClose": 5}}]}}' > "$TMP/c/chart-NOPR.json"
echo 'not json' > "$TMP/c/chart-BAD.json"; echo '{"chart": {"result": [{"meta": {"regularMarketPrice": "10.5", "chartPreviousClose": 10, "regularMarketTime": null}}]}}' > "$TMP/c/chart-STR.json"
OFF="$TMP/c" news --tickers ZZZZ NOPR BAD STR --no-sec --dry-run --json
eq "$(j '[.notes[] | select(startswith("price"))]')" '["price for BAD: chart for BAD is not usable","price for NOPR: chart for NOPR has no price","price for ZZZZ: chart for ZZZZ is not usable"]' "chart errors are notes, sorted by ticker"
eq "$(j '.moves')" '[{"ticker":"STR","price":10.5,"prev_close":10.0,"pct":5.0,"time":null,"moving":true}]' "string price, no time"
OFF="$TMP/c" news --tickers STR --no-sec --dry-run; eq "$(echo "$OUT" | head -1)" "STR    10.50 +5.00% vs yesterday's close 10.00  MOVING" "no time: no 'at'"
# no chart file offline: no move line, no note
news --tickers KO --hours 24 --dry-run; hasnt "$OUT" "vs yesterday's close" "no chart, no move line"; hasnt "$OUT" "note:" "no chart, no note"
news --tickers KO --hours 24 --dry-run --no-moves --json; eq "$(j '.moves')" '[]' "--no-moves"

# New York time follows daylight saving
for c in "2026-01-15T15:00:00+00:00|2026-01-15 10:00" "2026-10-19T14:15:00+00:00|2026-10-19 10:15" "2026-11-01T06:30:00+00:00|2026-11-01 01:30" "2026-11-01T05:30:00+00:00|2026-11-01 01:30" "2026-03-08T06:59:00+00:00|2026-03-08 01:59" "2026-03-08T07:00:00+00:00|2026-03-08 03:00"; do
  NOW="${c%%|*}" news --tickers KO --no-sec --no-moves --dry-run; has "$OUT" "Checked ${c##*|} ET." "ET for ${c%%|*}"
done

# day-trading tags
mkdir -p "$TMP/t"; printf '<rss><channel><item><title>Trading halted in XYZ after FDA approval; futures rise before CPI</title><link>https://example.com/h</link><pubDate>Tue, 13 Oct 2026 20:00:00 GMT</pubDate></item><item><title>Shares in trading pause as FTC and DOJ weigh tariffs, Fed and jobs report</title><link>https://example.com/i</link><pubDate>Tue, 13 Oct 2026 19:00:00 GMT</pubDate></item></channel></rss>' > "$TMP/t/yahoo-TSLA.xml"
OFF="$TMP/t" news --tickers TSLA --no-sec --json --dry-run
eq "$(j '[.items[].tags]')" '[["halt","regulatory","macro"],["halt","regulatory","macro"]]' "halt, regulatory, macro tags"
OFF="$TMP/t" news --tickers TSLA --no-sec --dry-run --help; has "$OUT" "--move-pct" "help lists --move-pct"; has "$OUT" "default: watchlist + plan + portfolio" "tickers help"
done_ "daytrade-news/news"

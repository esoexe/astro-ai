#!/usr/bin/env bash
# trader-news/news: the assertions of trader-skill/tests/test_news.py, run offline against the C++ tool (no network).
source "$(dirname "$0")/lib.sh"
DIR="$FIX/trader-news"; NOW=2026-10-14T00:00:00+00:00; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
base() { echo --offline "${OFF:-$DIR}" --state "$TMP/state/news.json" --now "$NOW" --portfolio "${PF:-$TMP/none.json}" --targets "${TG:-$TMP/none.json}"; }
news() { run trader-news/news "$@" $(base); }
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
run trader-news/news --tickers KO --no-sec --hours 1 --json --dry-run --offline "$DIR" --state "$TMP/state/news.json" --portfolio "$TMP/none.json" --targets "$TMP/none.json" --now "Tue, 13 Oct 2026 20:02:03 +0000"; eq "$(j '.checked')" '"2026-10-13T20:02:03+00:00"' "parse RFC 822 --now"
NOW="2026-09-01T16:30:35-04:00" news --tickers KO --no-sec --hours 1 --json --dry-run; eq "$(j '.checked')" '"2026-09-01T20:30:35+00:00"' "parse ISO --now"
run trader-news/news --tickers KO --no-sec --hours 1 --json --dry-run --offline "$DIR" --state "$TMP/state/news.json" --portfolio "$TMP/none.json" --targets "$TMP/none.json" --now "Tue, 13 Oct 2026 16:02:03 EDT"; eq "$(j '.checked')" '"2026-10-13T20:02:03+00:00"' "named zone"
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
eq "$(echo "$OUT" | tail -1)" "Checked 2026-10-14 00:00 UTC. Headlines are leads, not facts; open the story before acting. Not financial advice." "checked line"
# filings show in a wider window
rm -rf "$TMP/state"; news --tickers AAPL --hours 2400; eq "$CODE" 0 "wide window"
has "$OUT" "! 2026-07-30 20:30 UTC  AAPL        SEC EDGAR     Apple Inc. filed a 8-K: earnings results" "filing line"
has "$OUT" "      tags: earnings  https://www.sec.gov/Archives/edgar/data/320193/000032019326000018/0000320193-26-000018-index.htm" "tags + link line"
has "$OUT" "Not financial advice" "not advice"; hasnt "$OUT" "—" "no em dash"
# tickers from targets
rm -rf "$TMP/state"; echo '{"weights": [{"ticker": "KO", "name": "Coca-Cola", "weight": 100}]}' > "$TMP/targets.json"
TG="$TMP/targets.json" news --json --dry-run --hours 24; eq "$CODE" 0 "targets"
eq "$(j '[.items[].touches[]] | unique')" '["KO"]' "targets give KO"; [ ! -e "$TMP/state/news.json" ] && ok || bad "dry run writes nothing"
# no tickers
news; eq "$CODE" 2 "no tickers exit 2"; has "$ERR" "news: no tickers" "no tickers message"
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
done_ "trader-news/news"

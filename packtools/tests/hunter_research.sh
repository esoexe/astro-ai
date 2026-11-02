#!/usr/bin/env bash
# hunter-research/brief: the assertions of hunter-skill/tests/test_research.py, run against the C++ tool, plus feeds,
# filings and dates written here with their expected values worked out by hand. Fixtures: tests/fixtures/hunter-research
# (the upstream offline files; skill/ holds the skill's own template, companies.json and loops.json, unmodified).
source "$(dirname "$0")/lib.sh"
D="$FIX/hunter-research"; SK="$D/skill"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
unset SEC_USER_AGENT
B() { run --skill-dir "$SK" hunter-research/brief "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
DAY=(--today 2026-10-15)

# ---- SEC facts and filings (Alphabet's submissions file)
B sec --company Google --offline "$D" "${DAY[@]}" --json
eq "$CODE" 0 "sec exits 0"
eq "$(j '.sec.name')" '"Alphabet Inc."' "name"
eq "$(j '.sec.tickers[:2]')" '["GOOGL","GOOG"]' "tickers"
eq "$(j '.sec.hq')" '"1600 Amphitheatre Parkway, Mountain View, CA"' "headquarters, title-cased"
eq "$(j '.sec.fiscal_year_end')" '"December 31"' "fiscal year end"
eq "$(j '.sec.annual.url')" '"https://www.sec.gov/Archives/edgar/data/1652044/000165204426000018/goog-20251231.htm"' "annual report"
eq "$(j '.sec.annual.period')" '"2025-12-31"' "annual period"
eq "$(j '.sec.quarterly.filed')" '"2026-07-23"' "latest 10-Q"
eq "$(j '.sec.proxy.form')" '"DEF 14A"' "proxy"
eq "$(j '[.sec.events[].filed][:3]')" '["2026-08-10","2026-07-22","2026-06-11"]' "events, newest first"
eq "$(j '.sec.events|length')" 9 "8-Ks in the 180 days to 2026-10-15"
eq "$(j '.sec.events[]|select(.filed=="2026-07-22")|.about')" '"earnings results"' "earnings"
eq "$(j '.sec.events[2].about')" '"director or officer change, shareholder vote results"' "two items"
eq "$(j '.sec.events[3].about')" '"material agreement, shareholder rights change, bylaws or fiscal year change"' "three items, 9.01 dropped"
eq "$(j '.sec.events[0].items')" '["8.01"]' "9.01 (exhibits) is left out"
eq "$(j '.sec.cik')" '"0001652044"' "cik"
eq "$(j '.sec.edgar')" '"https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK=0001652044"' "EDGAR page"
eq "$(j '.sec|keys_unsorted')" '["name","cik","tickers","exchanges","industry","hq","fiscal_year_end","edgar","annual","quarterly","proxy","events"]' "key order"
eq "$(j '.sec.events[0]|keys_unsorted')" '["form","filed","period","url","items","about"]' "event keys"
eq "$(j '.notes')" '[]' "no notes"
B sec --company Google --offline "$D" "${DAY[@]}" --days 70 --json; eq "$(j '[.sec.events[].filed]')" '["2026-08-10"]' "a 70-day window"
B sec --company Google --offline "$D" "${DAY[@]}" --days 30
has "$OUT" "- No 8-K filings in the last 30 days." "no 8-Ks in 30 days"
eq "$(echo "$OUT" | head -1)" "- Alphabet Inc. (GOOGL, GOOG on Nasdaq). Industry code: Services-Computer Programming, Data Processing, Etc. Headquarters on file with the SEC: 1600 Amphitheatre Parkway, Mountain View, CA. Fiscal year ends December 31." "the business line, trailing dot stripped once"
hasnt "$OUT" "(fill in) What this news means" "sec drops the last news line"
B sec --company Google --offline "$D" "${DAY[@]}"
has "$OUT" "- Latest results: the 8-K of 2026-07-22 (item 2.02) carries the press release as an exhibit." "latest results"
eq "$(echo "$OUT" | grep -c '^- 2026-.* 8-K: ')" 8 "eight 8-Ks listed"

# ---- tickers
B sec --company Whoever --ticker goog --offline "$D" "${DAY[@]}" --json; eq "$(j '[.sec.cik, .sec.name]')" '["0001652044","Alphabet Inc."]' "goog -> Alphabet"
B sec --company Whoever --ticker NFLX --offline "$D" "${DAY[@]}"; eq "$CODE" 1 "Netflix not in the fixtures"
has "$OUT" "note: SEC: offline: no file submissions-CIK0001065280.json" "NFLX -> 0001065280"
B sec --company Whoever --ticker ZZZZ --offline "$D" "${DAY[@]}"
has "$OUT" "note: SEC: no SEC filer with ticker ZZZZ (private companies do not file with the SEC)" "unknown ticker"
has "$OUT" "Private companies do not file with the SEC: use the company's own about" "no cik: the private line"
B sec --company Google --ticker zzzz --offline "$D" "${DAY[@]}"; has "$OUT" "ticker zzzz (private" "--ticker replaces the bundled cik"
B sec --company Whoever --cik 1652044 --offline "$D" "${DAY[@]}" --json; eq "$(j .sec.name)" '"Alphabet Inc."' "--cik is zero-filled"

# ---- the SEC User-Agent rules (network is off in the suites: an accepted agent reaches for the network)
REACH="note: SEC: could not reach https://data.sec.gov/submissions/CIK0001652044.json: network is off (ASTRO_TOOL_OFFLINE=1)"
SKIP="SEC filings skipped: pass --user-agent"
B sec --company Google "${DAY[@]}" --profile none.json; eq "$CODE" 1 "no agent exits 1"; has "$OUT" "$SKIP" "skipped without an agent"
has "$OUT" "(fill in) No SEC filings in this brief. Set an SEC User-Agent and run it again for the 10-K." "the set-an-agent line"
B sec --company Google "${DAY[@]}" --profile none.json --user-agent "Your Name you@example.com"; has "$OUT" "$SKIP" "the placeholder is refused"
B sec --company Google "${DAY[@]}" --profile none.json --user-agent "no email here"; has "$OUT" "$SKIP" "no @ is refused"
B sec --company Google "${DAY[@]}" --profile none.json --user-agent "Hunter Skill test admin@example.com"; has "$OUT" "$REACH" "admin@example.com is fine"
SEC_USER_AGENT="Env Person env@example.org" B sec --company Google "${DAY[@]}" --profile none.json; has "$OUT" "$REACH" "SEC_USER_AGENT"
echo '{"sec_user_agent": "  Prof Person prof@example.org "}' > prof.json
B sec --company Google "${DAY[@]}" --profile prof.json; has "$OUT" "$REACH" "sec_user_agent in the profile"
B sec --company Google "${DAY[@]}" --profile prof.json --user-agent "Your Name you@example.com"; has "$OUT" "$SKIP" "the flag wins over the profile"

# ---- a filer written here: fiscal years, headquarters, 8-K items
mkdir -p acme
cat > acme/submissions-CIK0000000042.json <<'JSON'
{"cik": 42, "name": "Acme Corp", "tickers": ["ACME"], "exchanges": [], "sicDescription": "Rockets.", "fiscalYearEnd": "0630",
 "addresses": {"business": {"street1": "1 O'FALLON WAY", "city": "NEW YORK", "stateOrCountry": "NY"}},
 "filings": {"recent": {"form": ["8-K", "10-K", "8-K", "10-K", "8-K"],
   "accessionNumber": ["0000000042-26-000003", "0000000042-26-000002", "0000000042-26-000001", "0000000042-25-000009", "0000000042-25-000001"],
   "filingDate": ["2026-10-01", "2026-08-01", "2026-09-01", "2025-08-01", "2025-01-01"], "reportDate": ["", "2026-06-30", "", "2025-06-30", ""],
   "primaryDocument": ["a.htm", "b.htm", "c.htm", "d.htm", "e.htm"], "items": ["9.01", "", "2.02,6.01,9.01", "", "8.01"]}}}
JSON
B sec --company Acme --cik 42 --offline acme "${DAY[@]}" --json
eq "$(j '[.sec.hq, .sec.fiscal_year_end, .sec.cik]')" "[\"1 O'Fallon Way, New York, NY\",\"June 30\",\"0000000042\"]" "title() and a two-letter state"
eq "$(j '[.sec.events[] | [.filed, .about, .items, .period]]')" '[["2026-10-01","see filing",[],null],["2026-09-01","earnings results, item 6.01",["2.02","6.01"],null]]' "items with no name, only 9.01"
eq "$(j '.sec.annual')" '{"form":"10-K","filed":"2026-08-01","period":"2026-06-30","url":"https://www.sec.gov/Archives/edgar/data/42/000000004226000002/b.htm"}' "the first 10-K"
B sec --company Acme --cik 42 --offline acme "${DAY[@]}"
eq "$(echo "$OUT" | head -1)" "- Acme Corp (ACME on ). Industry code: Rockets. Headquarters on file with the SEC: 1 O'Fallon Way, New York, NY. Fiscal year ends June 30." "no exchange"
eq "$(echo "$OUT" | sed -n 2p)" "- Annual report: 10-K for the year ended 2026-06-30, filed 2026-08-01: https://www.sec.gov/Archives/edgar/data/42/000000004226000002/b.htm. Item 1 (Business) says what the company does and how it makes money; Item 1A lists its risks." "annual line"
eq "$(echo "$OUT" | sed -n 3p)" "- (fill in) Two or three lines in your own words from Item 1, including the segment this team sits in." "no 10-Q or proxy lines"
eq "$(echo "$OUT" | sed -n 4p)" "- 2026-10-01 8-K: see filing https://www.sec.gov/Archives/edgar/data/42/000000004226000003/a.htm" "see filing"
eq "$(echo "$OUT" | sed -n 6p)" "- Latest results: the 8-K of 2026-09-01 (item 2.02) carries the press release as an exhibit. Quote revenue and growth from it, never from memory." "results"
fy() { jq "$1" acme/submissions-CIK0000000042.json > acme2/submissions-CIK0000000042.json; B sec --company Acme --cik 42 --offline acme2 "${DAY[@]}" --json; }
mkdir -p acme2
fy '.fiscalYearEnd = "0031"'; eq "$(j .sec.fiscal_year_end)" '"December 31"' "month 00 is MONTHS[-1], like Python"
fy '.fiscalYearEnd = "1331"'; eq "$CODE" 1 "month 13 raises"; has "$ERR" "IndexError" "IndexError"
fy '.fiscalYearEnd = "123"'; eq "$(j .sec.fiscal_year_end)" null "not four characters"
fy '.fiscalYearEnd = null | .addresses = null | .sicDescription = null'; eq "$(j '[.sec.hq, .sec.industry]')" '["",null]' "no address"
B sec --company Acme --cik 42 --offline acme2 "${DAY[@]}"; has "$OUT" "Industry code: n/a. Headquarters on file with the SEC: n/a. Fiscal year ends n/a." "n/a fields"
fy '.filings.recent.items[0] = "1.05 and 4.02"'; eq "$(j .sec.events[0].about)" '"cybersecurity incident, past financials no longer reliable"' "findall in free text"
fy '.filings.recent.filingDate[0] = "2026-02-30"'; eq "$CODE" 1 "an impossible filing date raises"; has "$ERR" "day is out of range for month" "ValueError"
fy '.filings.recent.primaryDocument = ["a"]'; eq "$CODE" 1 "a short column raises"; has "$ERR" "IndexError" "IndexError"
fy '.filings.recent.filingDate[2] = "2026-09-01T09:00:00"'; eq "$(j .sec.events[1].filed)" '"2026-09-01T09:00:00"' "re.match reads the date prefix and the filing keeps its text"

# ---- feeds: RSS and Atom from the fixtures
B feeds --company Google --offline "$D" "${DAY[@]}" --json
eq "$(j '.posts|length')" 3 "three posts"
eq "$(j '.posts[0] | [.title, .date, .feed]')" '["An intelligent new shopping cart","2026-10-15","News from Google"]' "RSS item"
[[ "$(j '.posts[0].link')" == '"https://blog.google/'* ]] && ok || bad "blog.google link"
eq "$(j '.posts[0]|keys_unsorted')" '["title","link","date","feed"]' "post keys"
eq "$(j '.notes')" '["Google Cloud Blog: offline: no file feed-google-cloud-blog.xml","Google Developers Blog: offline: no file feed-google-developers.xml"]' "missing feeds are notes"
B feeds --company nvidia --offline "$D" "${DAY[@]}" --json
eq "$(j '.posts|length')" 2 "two Atom entries"
eq "$(j '.posts[0].date')" '"2026-10-14"' "published, in UTC"
[[ "$(j '.posts[0].link')" == '"https://developer.nvidia.com/blog/'* ]] && ok || bad "Atom alternate link"
B feeds --company Google --offline "$D" --today 2027-01-01; eq "$CODE" 1 "nothing in the window exits 1"; has "$OUT" "No posts in the window." "no posts"
B feeds --company Google --offline "$D" --today 2026-10-15 --days 0; eq "$(echo "$OUT" | grep -c '^2026-10-15')" 3 "a 0-day window keeps today"

# ---- feeds written here: dates in any time zone, links, titles, undated feeds, ordering
mkdir -p feeds
cat > feeds/feed-extra-1.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel><title>T</title>
<item><title>  Late   night
 post — CST </title><link>https://a.example/1</link><pubDate>Wed, 14 Oct 2026 23:30:00 -0500</pubDate></item>
<item><title>ISO offset</title><guid>https://a.example/2</guid><dc:date>2026-10-14T23:30:00.123-05:00</dc:date></item>
<item><title>Zulu</title><link>https://a.example/3</link><pubDate>2026-10-14T16:10:45Z</pubDate></item>
<item><title>Soon</title><link>https://a.example/4</link><pubDate>soon</pubDate></item>
<item><title>Old</title><link>https://a.example/5</link><pubDate>Mon, 01 Jun 2026 10:00:00 GMT</pubDate></item>
<item><title>Future</title><link>https://a.example/6</link><pubDate>Fri, 16 Oct 2026 10:00:00 GMT</pubDate></item>
<item><title>East</title><link>https://a.example/7</link><pubDate>2026-10-15T01:00:00+05:00</pubDate></item>
</channel></rss>
XML
cat > feeds/feed-extra-2.xml <<'XML'
<feed xmlns="http://www.w3.org/2005/Atom"><entry><title>One</title><link rel="self" href="https://b.example/self"/><link href="https://b.example/1"/></entry>
<entry><title>Two</title><link rel="alternate" href="https://b.example/2"/><updated>soon</updated></entry><entry><title>Three</title></entry><entry><title>Four</title></entry></feed>
XML
F2=(--feed https://a.example/feed --feed https://news.a-very-long-feed-host.example/atom)
B feeds --company Nobody "${F2[@]}" --offline feeds "${DAY[@]}" --days 30 --json
eq "$(j '[.posts[] | [.title, .link, .date, .feed]]')" '[["Late night post , CST","https://a.example/1","2026-10-15","a.example"],["ISO offset","https://a.example/2","2026-10-15","a.example"],["Zulu","https://a.example/3","2026-10-14","a.example"],["East","https://a.example/7","2026-10-14","a.example"],["One","https://b.example/1",null,"news.a-very-long-feed-host.example"],["Two","https://b.example/2",null,"news.a-very-long-feed-host.example"],["Three","",null,"news.a-very-long-feed-host.example"]]' "UTC dates, window, sort, undated feed"
B feeds --company Nobody "${F2[@]}" --offline feeds "${DAY[@]}" --days 30
eq "$(echo "$OUT" | sed -n 1p)" "2026-10-15  a.example                Late night post , CST" "post line"
eq "$(echo "$OUT" | sed -n 2p)" "            https://a.example/1" "link line"
eq "$(echo "$OUT" | sed -n 9p)" "undated     news.a-very-long-feed-ho One" "feed name cut at 24"
eq "$CODE" 0 "posts exit 0"
mkdir -p empty; B feeds --company Nobody --feed plain-name --offline empty "${DAY[@]}"; eq "$OUT" "note: plain-name: offline: no file feed-extra-1.xml
No posts in the window." "a URL without // names itself"
mkdir -p bad
bf() { printf '%s' "$1" > bad/feed-extra-1.xml; B feeds --company Nobody --feed https://c.example/f --offline bad "${DAY[@]}"; }
bf '<rss><channel></rss>'; has "$OUT" "note: c.example: feed c.example is not valid XML: mismatched tag: line 1, column 16" "not XML"
bf '<rss><channel><item><title>x</title><pubDate>2026-13-14T10:00:00Z</pubDate></item></channel></rss>'
eq "$CODE" 1 "month 13 is not caught"; has "$ERR" "ValueError: time data '2026-13-14 10:00:00' does not match format '%Y-%m-%d %H:%M:%S'" "strptime's message"
bf '<rss><channel><item><title>x</title><pubDate>2026-02-30T10:00:00Z</pubDate></item></channel></rss>'; eq "$CODE" 1 "Feb 30"; has "$ERR" "day is out of range for month" "Feb 30 message"
bf '<rss><channel><item><title>x</title><pubDate>2026-10-14T24:00:00Z</pubDate></item></channel></rss>'; eq "$CODE" 1 "hour 24"
bf '<rss><channel><item><title>x</title><pubDate>2026-10-14T23:30:00+05</pubDate></item></channel></rss>'; has "$OUT" "2026-10-14  c.example" "a short offset is ignored (UTC)"
bf '<rss><channel><item><title>x</title><pubDate>2026-10-14 more</pubDate></item></channel></rss>'; has "$OUT" "2026-10-14  c.example" "a bare date prefix"
bf '<rss><channel><item><title>x</title><pubDate>Thu, 15 Oct 2026 00:30:00 +0100</pubDate></item></channel></rss>'; has "$OUT" "2026-10-14  c.example" "RFC 822 east of UTC"
bf '<rss><channel><item><title>x</title><pubDate>15 Oct 2026 10:00 EDT</pubDate></item></channel></rss>'; has "$OUT" "2026-10-15  c.example" "named zone"

# ---- loops
B loop --company amazon; eq "$CODE" 0 "loop exits 0"
has "$OUT" "four 55-minute interviews" "the Amazon loop"
has "$OUT" "[source: https://www.amazon.jobs/content/en/how-we-hire/sde-ii-interview-prep]" "official notes cite the source"
has "$OUT" "Community-reported, not confirmed by the company: The Bar Raiser" "community notes are marked"
eq "$(echo "$OUT" | head -1)" "Amazon interview loop (verified 2026-10-15)" "header"
B loop --company "Acme Rockets"; eq "$CODE" 1 "no loop exits 1"; has "$OUT" "(fill in)" "fill in"
B loop --company GOOGLE --json; eq "$(j '[.company, (.loop.notes|length)]')" '["Google",7]' "loop --json"
B loop --company Nope --json; eq "$(j .)" '{"company":"Nope","loop":null}' "no loop as JSON"
for c in Google Meta NVIDIA Netflix Amazon; do B loop --company "$c" --json; eq "$(j '[.loop.notes[].kind] | index("official") != null')" true "$c has an official note"; done

# ---- build
OUTF="$TMP/hunter/companies/google.md"
BASE=(build --company google --role "Software Engineer" --team "Google Cloud, Infrastructure" --candidate "Jordan Reyes" --offline "$D" "${DAY[@]}" --out "$OUTF")
B "${BASE[@]}"; eq "$CODE" 0 "build exits 0"
eq "$OUT" "Wrote $OUTF: 10 filings, 3 blog posts, 7 loop notes. Fill the (fill in) lines from the sources.
note: Google Cloud Blog: offline: no file feed-google-cloud-blog.xml
note: Google Developers Blog: offline: no file feed-google-developers.xml" "the summary"
TEXT=$(cat "$OUTF")
eq "$(echo "$TEXT" | head -1)" "# Google: research brief" "title"
has "$TEXT" "Prepared 2026-10-15 for Jordan Reyes" "prepared for"
has "$TEXT" "Headquarters on file with the SEC: 1600 Amphitheatre Parkway, Mountain View, CA" "hq"
has "$TEXT" "goog-20251231.htm" "10-K"
has "$TEXT" "- 2026-07-22 8-K: earnings results" "earnings 8-K"
has "$TEXT" "Latest results: the 8-K of 2026-07-22 (item 2.02)" "latest results"
has "$TEXT" "News from Google: An intelligent new shopping cart" "blog post"
has "$TEXT" "Team named in the posting: Google Cloud, Infrastructure" "team"
has "$TEXT" "AI tools are not permitted during Google interviews." "loop note"
has "$TEXT" "Community-reported, not confirmed by the company" "community"
has "$TEXT" "(fill in)" "fill-in lines"; has "$TEXT" "## Sources" "sources"
hasnt "$TEXT" "{{" "every placeholder filled"; hasnt "$TEXT" $'\xe2\x80\x94' "no em dash"
has "$TEXT" "Google Cloud Blog: offline: no file" "notes say what was missing"
has "$TEXT" "- What does a great first six months look like on Google Cloud, Infrastructure?" "team in the questions"
has "$TEXT" "- Read the company's own words: Our hiring process (https://www.google.com/about/careers/applications/how-we-hire/)" "culture page"
eq "$(echo "$TEXT" | grep -c "Read the company's own words")" 1 "only titles about culture or hiring"
has "$TEXT" "- SEC EDGAR filings for Alphabet Inc.: https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK=0001652044
- News from Google (feed): https://blog.google/rss/" "sources order"
has "$TEXT" "- Careers site: https://www.google.com/about/careers/applications/
- Investor relations: https://abc.xyz/investor/" "careers and investors"
eq "$(echo "$TEXT" | tail -4)" "## Notes from the build

- Google Cloud Blog: offline: no file feed-google-cloud-blog.xml
- Google Developers Blog: offline: no file feed-google-developers.xml" "notes at the end"
echo "my notes" >> "$OUTF"
B "${BASE[@]}"; eq "$CODE" 3 "an existing brief is kept"; has "$ERR" "--force" "--force hint"
eq "$ERR" "brief: $OUTF already exists (it may hold your edits). Use --stdout to compare or --force to rebuild it." "the message"
has "$(cat "$OUTF")" "my notes" "the edits survive"
B "${BASE[@]}" --force; eq "$CODE" 0 "--force rebuilds"; hasnt "$(cat "$OUTF")" "my notes" "rebuilt"
B "${BASE[@]}" --stdout; eq "$OUT" "$(cat "$OUTF")" "--stdout prints the same brief"

B build --company "Brightline Labs" --offline "$D" "${DAY[@]}" --stdout --profile none.json; eq "$CODE" 0 "a private company"
has "$OUT" "Private companies do not file with the SEC" "private line"; has "$OUT" "No bundled loop for this company" "no loop"
has "$OUT" "Role: (fill in) · Team: the team · Prepared 2026-10-15
" "defaults, no candidate"
has "$OUT" "- (fill in) The team, from the posting." "no team"
has "$OUT" "## Sources

(fill in)" "no sources"
has "$OUT" "(fill in) Nothing fetched. Check the company's newsroom and blog." "nothing fetched"
has "$OUT" "- Brightline Labs is not in companies.json and no --ticker was given: no SEC filings (private companies do not file)" "the note"
echo '{"name": "Pat Doe"}' > me.json
B build --company "Brightline Labs" --offline "$D" "${DAY[@]}" --profile me.json; eq "$CODE" 0 "default path"
eq "$(echo "$OUT" | head -1)" "Wrote hunter/companies/brightline-labs.md: 0 filings, 0 blog posts, 0 loop notes. Fill the (fill in) lines from the sources." "the default path"
has "$(cat hunter/companies/brightline-labs.md)" "Prepared 2026-10-15 for Pat Doe" "the profile name"
B build --company "{{role}}" --role Dev --offline "$D" "${DAY[@]}" --stdout --profile none.json
eq "$(echo "$OUT" | head -1)" "# Dev: research brief" "placeholders fill in order, like str.replace"
B build --company "  !!  " --offline "$D" "${DAY[@]}" --profile none.json; has "$OUT" "Wrote hunter/companies/company.md" "an empty slug is company"
B build --company Google --ticker GOOG --offline "$D" "${DAY[@]}" --stdout --profile none.json; has "$OUT" "SEC EDGAR filings for Alphabet Inc." "build with --ticker"
B build --company Acme --offline "$D" --today soon --stdout --profile none.json; eq "$CODE" 1 "a date that is not a date fails"
B build --company Acme --offline "$D" --today 2026-02-30 --stdout --profile none.json; eq "$CODE" 1 "Feb 30"; has "$ERR" "day is out of range for month" "ValueError"

# ---- CLI
B; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: brief" "usage"
B build --offline "$D"; eq "$CODE" 2 "--company is required"
B sec --company Google --days x; eq "$CODE" 2 "--days is an int"
B loop --company Google --ticker X; eq "$CODE" 2 "loop has no --ticker"
done_ "hunter-research/brief"

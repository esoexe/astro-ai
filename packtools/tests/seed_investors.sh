#!/usr/bin/env bash
# seed-investors/investors: the assertions of seed-skill/tests/test_investors.py, run against the C++ tool through its
# command line, plus hand-worked edge cases. fixtures/seed/seed-investors holds the upstream fixtures: the reel's six
# made-up investors (investors.example.csv, .example domains) and edgar/, made-up EDGAR answers (CIKs 0009990001-4)
# shaped like efts.sec.gov full-text search pages and Form D primary_doc.xml. Nothing here touches the network:
# lib.sh sets ASTRO_TOOL_OFFLINE=1, and the formd cases read the saved answers with --offline.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
IF="$FIX/seed/seed-investors"
EDGAR="$IF/edgar"
EXAMPLE="$IF/investors.example.csv"
unset SEC_USER_AGENT ASTRO_SEED_INVESTORS_PAGE
I() { run seed-investors/investors "$@"; }
J() { jq -c "$@"; }
LEASHLY=(--stage seed --sectors "pet care; marketplaces; local services; consumer" --geo US --ask 1.5M --today 2026-10-26)
fresh() { cd "$TMP" && rm -rf w && mkdir w && cd w || exit 1; }
day() { date -u -d "@$(( $(date -u -d 2026-10-26 +%s) - $1 * 86400 ))" +%F; }   # $1 days before 2026-10-26

# ---- the reel: import, rank, show (test_the_reel_six_import_rank_show)
fresh
I import "$EXAMPLE" --today 2026-10-26 --dir seed; eq "$CODE" 0 "import the reel"
has "$OUT" "Wrote" "import says it wrote"
eq "$(J '.investors|length' seed/investors.json)" 6 "six investors"
eq "$OUT" "Added Maya Chen (maya-chen), 2 sources.
Added Daniel Okafor (daniel-okafor), 2 sources.
Added Priya Raman (priya-raman), 2 sources.
Added Sam Whitaker (sam-whitaker), 2 sources.
Added Elena Brooks (elena-brooks), 1 source.
Added Marcus Lee (marcus-lee), 1 source.
Wrote seed/investors.json: 6 investors. Next: investors.py rank --write." "import output"
eq "$(J '.investors[0]|[.added,.fit,.reasons,.warm_intro,.stage,.check_min,.check_max]' seed/investors.json)" '["2026-10-26",null,[],true,["pre-seed","seed"],250000,1000000]' "a fresh row"
eq "$(J '.created_by,.generated' seed/investors.json | tr '\n' ' ')" '"seed-investors" "2026-10-26" ' "book header"
I rank "${LEASHLY[@]}" --dir seed; eq "$CODE" 0 "rank"
has "$OUT" "Not saved" "rank without --write"
eq "$(J '.investors[0].fit' seed/investors.json)" null "rank wrote nothing without --write"
eq "$(sed -n 1p <<<"$OUT")" 'Investor fit for a $1.5M seed round in US, sectors pet care, marketplaces, local services, consumer; checks of $75K-$750K wanted' "rank header"
eq "$(sed -n 2,4p <<<"$OUT")" '  fit  name             firm                   check        stage            id
   96  Maya Chen        Northlight Ventures    $250K-$1M    pre-seed, seed   maya-chen
   93  Daniel Okafor    Harbor Seed Fund       $100K-$500K  seed             daniel-okafor' "rank table"
has "$OUT" "  Daniel Okafor (93): invests at seed (+25); sectors match: marketplaces, local services (+25); checks of \$100K-\$500K fit the \$75K-\$750K you want (+20); backed a home-services marketplace at seed, 120 days ago (+13); invests in US (+10)" "reasons line"
eq "$(tail -n 1 <<<"$OUT")" "Not saved: add --write so /seed-closer can use the fits." "last line"
I rank --write "${LEASHLY[@]}" --dir seed; eq "$CODE" 0 "rank --write"
eq "$(tail -n 1 <<<"$OUT")" "Wrote seed/investors.json." "rank --write says so"
eq "$(J '[.investors[]|[.id,.fit]]' seed/investors.json)" '[["maya-chen",96],["daniel-okafor",93],["priya-raman",91],["sam-whitaker",89],["elena-brooks",87],["marcus-lee",85]]' "REEL_FITS"
eq "$(J .ask seed/investors.json)" 1500000 "ask saved"
eq "$(J .criteria seed/investors.json)" '{"stage":"seed","sectors":["pet care","marketplaces","local services","consumer"],"geo":["US"],"check":{"min":75000,"max":750000}}' "criteria saved"
eq "$(J .generated seed/investors.json)" '"2026-10-26"' "generated"
eq "$(J '.investors[0]|keys_unsorted' seed/investors.json)" '["id","name","firm","role","email","stage","check_min","check_max","sectors","geo","last_relevant_deal","sources","warm_intro","personal_line","notes","fit","reasons","added"]' "every field, in order"
eq "$(J '.investors[0]|[.email,.check_min,.check_max]' seed/investors.json)" '["maya@northlight.example",250000,1000000]' "maya's email and checks"
eq "$(J '.investors[0].reasons' seed/investors.json)" '["invests at seed (+25)","sectors match: pet care, marketplaces, consumer (+25)","checks of $250K-$1M fit the $75K-$750K you want (+20)","led the second of her two pet-care seed rounds, 196 days ago (+11)","invests in US (+10)","warm intro available (+5)"]' "maya's reasons"
has "$(J -r '.investors[0].personal_line' seed/investors.json)" "Saw you led two pet-care seed rounds" "personal line kept"
head -c 2 seed/investors.json | grep -q '^{$' && ok || bad "indent 2"
[ "$(tail -c 2 seed/investors.json | od -An -c | tr -d ' ')" = '}\n' ] && ok || bad "ends with a newline"
I rank --today 2026-10-26 --json --dir seed; eq "$CODE" 0 "a second rank reuses the saved criteria"
eq "$(J '[.investors[].fit]' <<<"$OUT")" '[96,93,91,89,87,85]' "same fits from the saved criteria"
I show --dir seed; eq "$CODE" 0 "show"
has "$OUT" "96  Maya Chen" "show lists maya"
eq "$(sed -n 1p <<<"$OUT")" "6 investors in seed/investors.json (ranked 2026-10-26)" "show header"
I show maya-chen --dir seed; eq "$CODE" 0 "show one"
has "$OUT" "source    https://northlight.example/team/maya-chen" "show source"
eq "$OUT" 'Maya Chen, Partner, Northlight Ventures (maya-chen)
  fit       96
            invests at seed (+25)
            sectors match: pet care, marketplaces, consumer (+25)
            checks of $250K-$1M fit the $75K-$750K you want (+20)
            led the second of her two pet-care seed rounds, 196 days ago (+11)
            invests in US (+10)
            warm intro available (+5)
  email     maya@northlight.example
  stage     pre-seed, seed
  checks    $250K-$1M
  sectors   pet care, consumer marketplaces
  geo       US
  last deal led the second of her two pet-care seed rounds (2026-04-13)
  warm      yes
  line      Saw you led two pet-care seed rounds, so you know the 20% fee problem better than anyone.
  source    https://northlight.example/team/maya-chen
  source    https://northlight.example/portfolio' "show maya, every field"
I show "MARCUS LEE" --dir seed; has "$OUT" "Marcus Lee, " "show finds a name, any case"
I show nobody --dir seed; eq "$CODE" 1 "show nobody"
eq "$OUT" "No investor 'nobody' in seed/investors.json." "show nobody text"
I show maya-chen --json --dir seed; eq "$(J .id <<<"$OUT")" '"maya-chen"' "show one --json"
I show --json --dir seed; eq "$(J '.investors|length' <<<"$OUT")" 6 "show --json is the book"
I show --top 2 --dir seed; eq "$(wc -l <<<"$OUT")" 4 "show --top 2: header, columns, two rows"
I show --top -5 --dir seed; eq "$(wc -l <<<"$OUT")" 3 "show --top -5 is rows[:-5]"
I show --top 0 --dir seed; eq "$(wc -l <<<"$OUT")" 8 "show --top 0 is every row"
I rank --today 2026-10-26 --reasons 2 --dir seed; eq "$(grep -c '^  [A-Z].*): ' <<<"$OUT")" 2 "--reasons 2"
I rank --today 2026-10-26 --reasons -1 --dir seed; eq "$(grep -c '^  [A-Z].*): ' <<<"$OUT")" 5 "--reasons -1 is ranked[:-1]"
I import "$EXAMPLE" --today 2026-10-26 --dir seed; eq "$CODE" 0 "the same file again"
has "$OUT" "Updated Maya Chen (maya-chen), 2 sources." "updates, never duplicates"
eq "$(J '.investors|length' seed/investors.json)" 6 "still six"
eq "$(J '.investors[0].fit' seed/investors.json)" null "an update clears the fit"

# ---- criteria from seed/config.json (test_criteria_from_config)
fresh; mkdir seed
echo '{"startup": {"name": "Leashly", "stage": "seed", "sector": "pet care, marketplaces, local services, consumer", "geography": "US", "raise": 1500000}}' > seed/config.json
I import "$EXAMPLE" --today 2026-10-26 --dir seed
I rank --today 2026-10-26 --json --dir seed; eq "$CODE" 0 "rank from config"
eq "$(J '[.investors[].fit]' <<<"$OUT")" '[96,93,91,89,87,85]' "same fits from config.json"
I rank --today 2026-10-26 --check 25K-100K --json --dir seed
eq "$(J .criteria.check <<<"$OUT")" '{"min":25000,"max":100000}' "--check overrides"
I rank --today 2026-10-26 --dir seed --ask 2M --write >/dev/null
I rank --today 2026-10-26 --json --dir seed
eq "$(J '.ask,.criteria.check' <<<"$OUT" | tr '\n' ' ')" '2000000 {"min":100000,"max":1000000} ' "a saved ask and its default check"
I rank --today 2026-10-26 --json --dir seed --check 50K-200K --write >/dev/null
I rank --today 2026-10-26 --json --dir seed
eq "$(J .criteria.check <<<"$OUT")" '{"min":50000,"max":200000}' "the saved check is kept"
I rank --today 2026-10-26 --json --dir seed --ask 3M
eq "$(J .criteria.check <<<"$OUT")" '{"min":150000,"max":1500000}' "a new --ask resets the check to 5%-50%"

# ---- add and refusals (test_add_and_refusals)
fresh
I add --name "Ada Example" --firm Fund --stage seed --check 100K-500K --sectors "pet care; consumer" --geo US \
  --source https://fund.example/ada --source https://press.example/ada --last-deal "led a seed round" --last-deal-date 2026-09-26 \
  --personal-line "Your note on pet care stuck with me." --today 2026-10-26 --dir seed
eq "$CODE" 0 "add"
eq "$OUT" "Added Ada Example (ada-example), 2 sources.
Wrote seed/investors.json: 1 investors. Next: investors.py rank --write." "add output"
eq "$(J '.investors[0].sources' seed/investors.json)" '["https://fund.example/ada","https://press.example/ada"]' "two --source"
eq "$(J '.investors[0]|[.stage,.check_min,.check_max,.sectors,.geo,.last_relevant_deal,.warm_intro]' seed/investors.json)" '[["seed"],100000,500000,["pet care","consumer"],["US"],{"what":"led a seed round","date":"2026-09-26"},false]' "add fields"
I add --name "No Source" --firm Fund --today 2026-10-26 --dir seed; eq "$CODE" 1 "add without a source"
has "$OUT" "Refused: --name (No Source) has no source" "refused text"
eq "$(J '.investors|length' seed/investors.json)" 1 "nothing added"
jq '.investors += [{"id":"hand","name":"Hand Edit","firm":"Test Fund","stage":["seed"],"check_min":250000,"check_max":1000000,"sectors":["pet care","marketplaces"],"geo":["US"],"sources":[],"last_relevant_deal":{"what":"a deal","date":"2026-10-26"},"warm_intro":false}]' seed/investors.json > e.tmp && mv e.tmp seed/investors.json
I rank --write "${LEASHLY[@]}" --dir seed; eq "$CODE" 0 "rank with a hand edit"
has "$OUT" "Refused: Hand Edit (hand)" "the hand edit is refused"
has "$OUT" "Refused: Hand Edit (hand): refused: no public source; add the page this came from, then rank again" "refusal reason"
eq "$(J '[.investors[].fit]' seed/investors.json)" '[94,null]' "25 + 25 + 20 + 14 (30 days ago) + 10, and the refused last"
printf 'name,firm,sources,phone,notes\r\nBea Example,Fund B,https://b.example/bea,415 555 0100,\r\nCal Example,Fund C,https://c.example/cal,,cell 415.555.0199\r\n' > rows.csv
I import rows.csv --today 2026-10-26 --dir seed; eq "$CODE" 1 "one row refused"
has "$OUT" "dropped the 'phone' column" "phone column dropped"
has "$OUT" "Refused: row 3 (Cal Example)" "cal refused"
eq "$(J '[.investors[].name]' seed/investors.json)" '["Ada Example","Hand Edit","Bea Example"]' "bea in, cal out"
grep -q 415 seed/investors.json && bad "no phone digits kept" || ok

# ---- formd (test_search_pages_details_and_dedupe, test_formd_cli_and_exit_codes)
fresh
I formd --q "pet care" --since 2025-01-01 --dir seed; eq "$CODE" 1 "formd without a User-Agent"
has "$ERR" "say who is asking" "refused before any request"
export ASTRO_SEED_INVESTORS_PAGE=3   # the fixtures hold two pages of 3, like upstream's PAGE = 3
I formd --q "pet care" --since 2025-01-01 --offline "$EDGAR" --today 2026-10-26 --write --dir seed; eq "$CODE" 0 "formd offline"
has "$OUT" "5 filings from 4 companies. Amount sold: median \$2,250,000, total \$17,400,000" "summary line"
has "$OUT" "Riley Samplewood" "the lead"
has "$OUT" "a venture capital fund raising money" "the fund line"
eq "$OUT" "SEC EDGAR Form D filings matching 'pet care', 2025-01-01 to 2026-10-26: 5 shown of 6
  filed      company                        where                      sold      offered  inv.  securities
  2026-08-03 Snoutwell Inc.                 Austin, TX             \$900,000   \$1,500,000    11  Simple Agreement for Future Equity
             directors who are not officers (look them up): Riley Samplewood
  2026-03-02 Pawtrail Labs, Inc.            San Francisco, CA    \$2,500,000   \$2,500,000    18  equity (amendment)
             directors who are not officers (look them up): Quinn Placeholder
  2025-11-14 Pawtrail Ventures Fund I, L.P. Menlo Park, CA      \$12,000,000   indefinite    40  fund interests
             a venture capital fund raising money: an investor to research, not a comparable startup
  2025-09-09 Leashwise Pet Care, Inc.       Brooklyn, NY                  ?            ?     ?  
  2025-06-12 Pawtrail Labs, Inc.            San Francisco, CA    \$2,000,000   \$2,500,000    14  equity
             directors who are not officers (look them up): Quinn Placeholder
5 filings from 4 companies. Amount sold: median \$2,250,000, total \$17,400,000 (the 4 that state it).
Form D names the company, not its investors. Search the press for \"<company> seed round\" to find who led, then add them with a source.
Wrote seed/formd.json." "the whole formd output"
D=seed/formd.json
eq "$(J '[.query,.since,.until,.total_hits]' $D)" '["pet care","2025-01-01","2026-10-26",6]' "formd.json header"
eq "$(J .source $D)" '"https://efts.sec.gov/LATEST/search-index?q=%22pet+care%22&dateRange=custom&startdt=2025-01-01&enddt=2026-10-26&forms=D"' "the efts URL: a quoted phrase"
eq "$(J '[.filings[].adsh]' $D)" '["0009990004-26-000002","0009990001-26-000001","0009990003-25-000004","0009990002-25-000007","0009990001-25-000003"]' "newest first, one per filing"
eq "$(J '.filings[0]|[.company,.amount_sold,.amount_offered,.investors_count]' $D)" '["Snoutwell Inc.",900000,1500000,11]' "snoutwell amounts"
eq "$(J '.filings[0].securities' $D)" '["Simple Agreement for Future Equity"]' "snoutwell securities"
eq "$(J '.filings[0].url' $D)" '"https://www.sec.gov/Archives/edgar/data/9990004/000999000426000002/0009990004-26-000002-index.htm"' "index url"
eq "$(J '.filings[0].doc_url' $D)" '"https://www.sec.gov/Archives/edgar/data/9990004/000999000426000002/primary_doc.xml"' "doc url"
eq "$(J '.filings[0]|[.first_sale,.location,.city,.state]' $D)" '["2026-07-15","Austin, TX","Austin","TX"]' "snoutwell place and date"
eq "$(J '.filings[0]|keys_unsorted' $D)" '["adsh","cik","company","form","filed","location","state_of_inc","url","doc_url","city","state","incorporated","year_of_inc","entity_type","industry","fund_type","amendment","first_sale","securities","amount_offered","amount_sold","amount_remaining","investors_count","minimum_investment","related_persons"]' "filing keys, in order"
eq "$(J '.filings[0].related_persons' $D)" '[{"name":"Morgan Mockridge","relationships":["Executive Officer","Director"],"clarification":""},{"name":"Riley Samplewood","relationships":["Director"],"clarification":""}]' "related persons: names and roles only"
eq "$(J '.filings[1]|[.amendment,.form]' $D)" '[true,"D/A"]' "the D/A is an amendment"
eq "$(J '.filings[2]|[.amount_offered,.fund_type,.industry]' $D)" '["indefinite","Venture Capital Fund","Pooled Investment Fund"]' "namespaced XML"
eq "$(J '.filings[3]|has("amount_sold")' $D)" false "a missing document: no guess"
eq "$(J '.filings[3]|keys_unsorted|length' $D)" 9 "a missing document: the search facts only"
eq "$(J .summary $D)" '{"filings":5,"companies":4,"with_amounts":4,"median_sold":2250000,"total_sold":17400000}' "summarise"
grep -q "Example Street" $D && bad "addresses are never kept" || ok
grep -q "555-010" $D && bad "phone numbers are never kept" || ok
I formd --q "pet care" --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --no-amendments --json --dir seed
eq "$(J '.filings|length' <<<"$OUT")" 4 "--no-amendments"
I formd --q "pet care" --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --no-details --max 2 --json --dir seed
eq "$(J '[.filings[].company]' <<<"$OUT")" '["Snoutwell Inc.","Pawtrail Labs, Inc."]' "--max 2 --no-details"
eq "$(J '.filings[0]|has("amount_sold")' <<<"$OUT")" false "--no-details reads no documents"
I formd --q "pet care" --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --max 4 --json --dir seed
eq "$(J '.filings|length' <<<"$OUT")" 4 "--max 4 keeps the first four hits"
I formd --q "pet care" --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --max 6 --json --dir seed
eq "$(J '.filings|length' <<<"$OUT")" 5 "--max 6: six hits, two of them the same filing"
mkdir gz && for f in "$EDGAR"/*; do gzip -c "$f" > "gz/$(basename "$f")"; done
I formd --q "pet care" --since 2025-01-01 --until 2026-10-26 --offline gz --json --dir seed
eq "$(J .summary <<<"$OUT")" '{"filings":5,"companies":4,"with_amounts":4,"median_sold":2250000,"total_sold":17400000}' "gzipped answers are read the same"
unset ASTRO_SEED_INVESTORS_PAGE
I formd --q pet --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --json --dir seed
eq "$(J '.total_hits,(.filings|length)' <<<"$OUT" | tr '\n' ' ')" "6 3 " "page size 100: one page of 3 is the last"
eq "$(J .source <<<"$OUT")" '"https://efts.sec.gov/LATEST/search-index?q=pet&dateRange=custom&startdt=2025-01-01&enddt=2026-10-26&forms=D"' "one word is not quoted"
I formd --q '"pet care" OR paw' --since 2025-01-01 --until 2026-10-26 --offline "$EDGAR" --json --dir seed
eq "$(J .source <<<"$OUT")" '"https://efts.sec.gov/LATEST/search-index?q=%22pet+care%22+OR+paw&dateRange=custom&startdt=2025-01-01&enddt=2026-10-26&forms=D"' "quotes already there: left alone"
mkdir empty
I formd --q pet --since 2025-01-01 --offline empty --dir seed; eq "$CODE" 1 "nothing found"
has "$OUT" "No Form D filings matched 'pet' from 2025-01-01 to " "nothing found text"
I formd --q pet --since 2025-13-01 --offline empty --dir seed; eq "$CODE" 2 "a bad --since"
eq "$ERR" "investors.py: --since wants YYYY-MM-DD or YYYY-MM, not '2025-13-01'" "bad --since text"
I formd --q pet --since 2026-01-01 --until 2025-01-01 --offline empty --dir seed; eq "$CODE" 2 "--until before --since"
eq "$ERR" "investors.py: --until is before --since" "until text"
I formd --q pet --since 2025-06 --until 2025-07 --offline empty --json --dir seed; eq "$(J '[.since,.until]' <<<"$OUT")" '["2025-06-01","2025-07-01"]' "YYYY-MM is the 1st"
I formd --q "  " --since 2025-01-01 --offline empty --dir seed; eq "$ERR" "investors.py: --q needs the words to search for" "blank --q"
I formd --q pet --since 2025-01-01 --max 501 --offline empty --dir seed; eq "$ERR" "investors.py: --max is 1 to 500" "--max 501"
I formd --q pet --since 2025-01-01 --max 0 --offline empty --dir seed; eq "$CODE" 2 "--max 0"
I formd --since 2025-01-01 --dir seed; eq "$CODE" 2 "--q is required"
has "$ERR" "the following arguments are required: --q" "argparse text"
mkdir bad && cp "$EDGAR/efts-0.json" bad/ && printf '<a><b></a>' > bad/0009990004-26-000002.xml
I formd --q pet --since 2025-01-01 --offline bad --dir seed; eq "$CODE" 2 "a broken Form D document"
has "$ERR" "investors.py: a Form D document is not valid XML: mismatched tag: line 1" "the XML error"
mkdir nj && echo 'not json' > nj/efts-0.json
I formd --q pet --since 2025-01-01 --offline nj --dir seed; eq "$ERR" "investors.py: EDGAR full-text search sent something that is not JSON" "not JSON"
eq "$CODE" 2 "not JSON exit"

# ---- the User-Agent SEC asks for (test_the_url_and_the_user_agent)
fresh
for ua in "Your Name you@example.com" "ada@leashly.co" "Leashly ops@leashly.example" "Ada Lovelace" "Ada name@leashly.co" "me@leashly.co Ada" "Ada email@leashly.co"; do
  I formd --q pet --since 2025-01-01 --ua "$ua" --dir seed
  eq "$CODE" 1 "UA refused: $ua"
  has "$ERR" "sec_user_agent must be your real name and email" "UA text: $ua"
done
I formd --q pet --since 2025-01-01 --ua "Your Name you@example.com" --dir seed
eq "$ERR" "investors.py: sec_user_agent must be your real name and email, like \"Ada Lovelace ada@yourstartup.com\" (got 'Your Name you@example.com')" "UA message"
I formd --q pet --since 2025-01-01 --ua "Ada Lovelace ada@leashly.co" --dir seed
eq "$CODE" 2 "a real UA goes to the network (off in the suite)"
has "$ERR" "investors.py: could not reach SEC EDGAR: " "network error text"
SEC_USER_AGENT="Grace Hopper grace@leashly.co" run seed-investors/investors formd --q pet --since 2025-01-01 --dir seed
has "$ERR" "could not reach SEC EDGAR" "SEC_USER_AGENT is used"
mkdir -p seed && echo '{"sec_user_agent": "Alan Turing alan@leashly.co"}' > seed/config.json
I formd --q pet --since 2025-01-01 --dir seed; has "$ERR" "could not reach SEC EDGAR" "config.json sec_user_agent is used"
echo '{"sec_user_agent": "you@leashly.co"}' > seed/config.json
I formd --q pet --since 2025-01-01 --dir seed; eq "$CODE" 1 "a placeholder in config.json"
I formd --q pet --since 2025-01-01 --dir seed --offline "$EDGAR" --json; eq "$CODE" 0 "--offline needs no User-Agent"

# ---- parsing (test_money, test_stages_sectors_geo), through the flags
fresh; mkdir seed
I import "$EXAMPLE" --today 2026-10-26 --dir seed
for pair in '1.5M 1500000' '$2.5MM 2500000' '1,500,000 1500000' '1b 1000000000' '750000 750000' '$250K 250000' '1.5m 1500000' '2bn 2000000000' '0.1k 100'; do
  set -- $pair
  I rank --stage seed --sectors pet --geo US --ask "$1" --today 2026-10-26 --json --dir seed
  eq "$(J .ask <<<"$OUT")" "$2" "money($1)"
done
for pair in '250K-1M [250000,1000000]' '$100K_to_$500K [100000,500000]' '$50K [50000,50000]' '$250K_–_$1M [250000,1000000]' '1M-1M [1000000,1000000]'; do
  set -- $pair; c=${1//_/ }
  I rank --stage seed --sectors pet --geo US --ask 2M --check "$c" --today 2026-10-26 --json --dir seed
  eq "$(J '[.criteria.check.min,.criteria.check.max]' <<<"$OUT")" "$2" "money_range($c)"
done
for c in lots 1M-250K 0 -5 -1M 1M-0.5K; do
  I rank --stage seed --sectors pet --geo US --ask 2M --check "$c" --today 2026-10-26 --dir seed
  eq "$CODE" 2 "money_range refuses $c"
done
I rank --stage seed --sectors pet --geo US --ask 2M --check 1M-250K --today 2026-10-26 --dir seed
eq "$ERR" "investors.py: --check wants a range like 250K-1M with the low end first, not '1M-250K'" "range text"
I rank --stage seed --sectors pet --geo US --ask lots --today 2026-10-26 --dir seed
eq "$ERR" "investors.py: --ask is not an amount like \$250K or 1.5M: 'lots'" "money text"
I rank --stage seed --sectors pet --geo US --ask 2M --check 12.5K-25K --today 2026-10-26 --dir seed
has "$OUT" 'checks of $12.5K-$25K wanted' "short_money 12500, 25000"
I rank --stage seed --sectors pet --geo US --ask 1M --today 2026-10-26 --dir seed
has "$OUT" 'Investor fit for a $1M seed round in US, sectors pet; checks of $50K-$500K wanted' "short_money 1000000, 50000, 500000"
I rank --stage seed --sectors pet --geo US --ask 999 --today 2026-10-26 --dir seed
has "$OUT" 'Investor fit for a $999 seed round' "short_money under 1000"
has "$OUT" 'checks of $50-$500 wanted' "short_money: half-even 49.95 -> 50, 499.5 -> 500"
I rank --stage mezzanine --sectors pet --geo US --ask 1M --today 2026-10-26 --dir seed; eq "$CODE" 2 "a bad stage"
eq "$ERR" "investors.py: stage 'mezzanine' is not one of: pre-seed, seed, series a, series b, growth" "stage text"
fresh
I add --name "Stage Test" --firm F --stage "Pre-Seed; preseed; Seed; Series A; a; growth; late; series_b" --source https://x.example/a --today 2026-10-26 --dir seed
eq "$(J '.investors[0].stage' seed/investors.json)" '["pre-seed","pre-seed","seed","series a","series a","growth","growth","series b"]' "norm_stage"
I add --name "URL Test" --firm F --source "https://a.example/x https://b.example/y; https://c.example/z" --source "https://d.example/w|https://a.example/x" --today 2026-10-26 --dir seed
eq "$(J '.investors[1].sources' seed/investors.json)" '["https://a.example/x","https://b.example/y","https://c.example/z","https://d.example/w"]' "split_urls, deduped"
eq "$(sed -n 1p <<<"$OUT")" "Note: --name (URL Test) has no personal_line yet: /seed-closer needs one before it drafts an email" "personal_line note"
I add --name "List Test" --firm F --sectors "pet care, consumer | local services;;" --geo "US, Canada" --warm-intro --source https://x.example/l --today 2026-10-26 --dir seed
eq "$(J '.investors[2]|[.sectors,.geo,.warm_intro]' seed/investors.json)" '[["pet care","consumer","local services"],["US","Canada"],true]' "split_list and --warm-intro"

# ---- fit: every part of the score (test_every_part_of_the_score, test_check_size, test_recency_exact, test_no_source...)
fresh; mkdir seed
BASE='{"name":"Test Person","firm":"Test Fund","stage":["seed"],"check_min":250000,"check_max":1000000,"sectors":["pet care","marketplaces"],"geo":["US"],"sources":["https://fund.example/team"],"last_relevant_deal":{"what":"a deal","date":"2026-10-26"},"warm_intro":false}'
ROWS=()
row() { ROWS+=("$(jq -c --arg id "$1" ". + {id: \$id} + $2" <<<"$BASE")"); }
row base '{}'
row warm '{"warm_intro":true}'
row preseed '{"stage":["pre-seed"]}'
row seriesb '{"stage":["series b"]}'
row nostage '{"stage":[]}'
row badstage '{"stage":["mezzanine","seed?"]}'
row onesector '{"sectors":["pet care"]}'
row fintech '{"sectors":["fintech"]}'
row pets '{"sectors":["Pets"]}'
row carpet '{"sectors":["carpet cleaning"]}'
row capitals '{"sectors":["Pet Care","Consumer Marketplaces"]}'
row europe '{"geo":["Europe"]}'
row usa '{"geo":["United States"]}'
row global '{"geo":["Global"]}'
row nogeo '{"geo":[]}'
row nodeal '{"last_relevant_deal":null}'
row c1 '{"check_min":750000,"check_max":2000000}'
row c2 '{"check_min":25000,"check_max":50000}'
row c3 '{"check_min":5000,"check_max":25000}'
row c4 '{"check_min":1000000,"check_max":3000000}'
row c5 '{"check_min":2000000,"check_max":10000000}'
row c6 '{"check_min":null,"check_max":null}'
row c7 '{"check_min":250000.0,"check_max":250000}'
row nosrc '{"sources":[]}'
row notes '{"sources":["see notes"]}'
row hand '{"stage":"seed","sectors":"pet care; marketplaces","geo":"US","sources":"https://fund.example/x"}'
for d in 0 1 24 25 120 196 200 290 390 480 705 706 730 900 -3; do row "d$d" "{\"last_relevant_deal\":{\"what\":\"a deal\",\"date\":\"$(day "$d")\"}}"; done
printf '%s\n' "${ROWS[@]}" | jq -s '{created_by:"seed-investors",generated:null,ask:null,criteria:{},investors:.}' > seed/investors.json
I rank --stage seed --sectors "pet care; marketplaces; local services; consumer" --geo US --ask 1.5M --check 75K-750K --today 2026-10-26 --json --dir seed
eq "$CODE" 0 "rank the score book"
F() { J --arg id "$1" '.investors[]|select(.id==$id)|.fit' <<<"$OUT"; }
R() { J -r --arg id "$1" --argjson n "$2" '.investors[]|select(.id==$id)|.reasons[$n]' <<<"$OUT"; }
eq "$(F base)" 95 "25 + 25 + 20 + 15 + 10"
eq "$(R base 0)" "invests at seed (+25)" "the stage reason"
eq "$(F warm)" 100 "warm intro"
eq "$(F preseed)" 82 "one stage off: 12"
eq "$(R preseed 0)" "invests at pre-seed, one stage off seed (+12)" "one stage off text"
eq "$(F seriesb)" 70 "series b"
eq "$(R seriesb 0)" "invests at series b, not seed (+0)" "far stage text"
eq "$(F nostage)" 70 "no stage"
eq "$(R nostage 0)" "stage unknown: check their site (+0)" "no stage text"
eq "$(F badstage)" 70 "stages that are not stages are skipped"
eq "$(F onesector)" 85 "one sector: 15"
eq "$(R onesector 1)" "sectors match: pet care (+15)" "one sector text"
eq "$(F fintech)" 70 "no sector"
eq "$(R fintech 1)" "no sector overlap with pet care, marketplaces, local services, consumer (+0)" "no sector text"
eq "$(R pets 1)" "sectors match: pet care (+15)" "plural and singular match"
eq "$(R carpet 1)" "no sector overlap with pet care, marketplaces, local services, consumer (+0)" "pet inside a word is not a match"
eq "$(R capitals 1)" "sectors match: pet care, marketplaces, consumer (+25)" "case and word subsets"
eq "$(F europe)" 85 "geo off"
eq "$(R europe 4)" "invests in Europe, not US (+0)" "geo off text"
eq "$(R usa 4)" "invests in US (+10)" "United States is US"
eq "$(R global 4)" "invests globally (+10)" "global"
eq "$(R nogeo 4)" "geography unknown (+0)" "no geo"
eq "$(F nodeal)" 80 "no deal"
eq "$(R nodeal 3)" "no relevant deal on record (+0)" "no deal text"
eq "$(F c1)" 95 "touches the top of the range"
eq "$(F c2)" 85 "within a factor of 2 of \$75K"
has "$(R c2 2)" "close to" "close to text"
eq "$(R c2 2)" 'checks of $25K-$50K are close to the $75K-$750K you want (+10)' "close to, whole"
eq "$(F c3)" 75 "far under"
eq "$(R c3 2)" 'checks of $5K-$25K are far from the $75K-$750K you want (+0)' "far text"
eq "$(F c4)" 85 "\$1M is within 2x of \$750K"
eq "$(F c5)" 75 "bigger than the round"
has "$(R c5 2)" 'bigger than your whole $1.5M round' "bigger text"
eq "$(R c5 2)" 'their smallest check, $2M, is bigger than your whole $1.5M round (+0)' "bigger, whole"
eq "$(F c6)" 75 "check unknown"
eq "$(R c6 2)" "check size unknown (+0)" "check unknown text"
eq "$(R c7 2)" 'checks of $250K fit the $75K-$750K you want (+20)' "one check size: 250000.0 == 250000"
eq "$(F nosrc)" null "no source: refused"
has "$(R nosrc 0)" "no public source" "refusal text"
eq "$(F notes)" null "a source that is not a URL"
eq "$(F hand)" 95 "strings from a hand edit still work"
eq "$(J '.investors[]|select(.id=="hand")|[.stage,.sectors,.geo,.sources]' <<<"$OUT")" '[["seed"],["pet care","marketplaces"],["US"],["https://fund.example/x"]]' "hand strings become lists"
want=(15 15 15 14 13 11 11 9 7 5 1 0 0 0 15); i=0
for d in 0 1 24 25 120 196 200 290 390 480 705 706 730 900 -3; do eq "$(( $(F "d$d") - 80 ))" "${want[$i]}" "recency_points($d)"; i=$((i+1)); done
eq "$(R d-3 3)" "a deal, 0 days ago (+15)" "a deal after --today counts as today"
eq "$(R d196 3)" "a deal, 196 days ago (+11)" "recency text"
eq "$(J '[.investors[]|.fit]|map(select(.==null))|length' <<<"$OUT")" 2 "two refused"
eq "$(J '.investors[-2:]|map(.id)' <<<"$OUT")" '["nosrc","notes"]' "the refused go last, in book order"
eq "$(J '.investors[0].id' <<<"$OUT")" '"warm"' "highest fit first"
eq "$(J '[.investors[]|select(.fit==95)|.id][0:3]' <<<"$OUT")" '["base","capitals","usa"]' "equal fits keep book order (equal names)"

# ---- rows: refusals, private columns, aliases, merge (class Rows)
fresh; mkdir seed
item() { jq -c ". + $1" <<<'{"name":"Ada Example","firm":"Fund","sources":"https://fund.example/ada","personal_line":"Hi"}'; }
refuse() {
  item "$1" | jq -s . > in.json
  I import in.json --today 2026-10-26 --dir seed
  eq "$CODE" 1 "refused: $1"; has "$OUT" "$2" "why: $1"
}
refuse '{"sources":""}' "no source"
refuse '{"sources":"linkedin"}' "public URL"
refuse '{"name":""}' "no name"
refuse '{"email":"not-an-email"}' "not an email"
refuse '{"notes":"call her on 415-555-0123"}' "phone number"
refuse '{"notes":"+1 (415) 555 0123"}' "phone number"
refuse '{"last_deal":"led a round","last_deal_date":"2027-01-01"}' "in the future"
refuse '{"last_deal":"led a round"}' "needs last_deal_date"
refuse '{"stage":"mezzanine"}' "is not one of"
refuse '{"check":"1M-250K"}' "low end first"
refuse '{"check_min":500000,"check_max":100000}' "check_max is under check_min"
eq "$(sed -n 1p <<<"$OUT")" "Refused: item 1 (Ada Example): check_max is under check_min" "refusal line"
[ -e seed/investors.json ] && bad "nothing written when every row is refused" || ok
item '{"sources":"linkedin"}' | jq -s . > in.json; I import in.json --today 2026-10-26 --dir seed
eq "$OUT" "Refused: item 1 (Ada Example): a source must be a public URL (https://...), not 'linkedin'" "URL refusal text"
item '{"name":"  "}' | jq -s . > in.json; I import in.json --today 2026-10-26 --dir seed
eq "$OUT" "Refused: item 1 has no name" "no name text"
item '{"last_deal":"x","last_deal_date":"2027-01-01"}' | jq -s . > in.json; I import in.json --today 2026-10-26 --dir seed
eq "$OUT" "Refused: item 1 (Ada Example): last_deal_date 2027-01-01 is in the future" "future text"
item '{"title":"call 415-555-0123"}' | jq -s . > in.json; I import in.json --today 2026-10-26 --dir seed
eq "$OUT" "Refused: item 1 (Ada Example): role holds what looks like a phone number; phone numbers are never collected. Remove it and try again." "phone in an aliased column"
item '{"phone":"415 555 0100","home_address":"1 Home Street","cell":"","city_home":"x","last_deal":"led a seed round","last_deal_date":"2026-04","notes":"met at the 2026-04-13 demo day, $1,500,000 fund"}' | jq -s . > in.json
I import in.json --today 2026-10-26 --dir seed; eq "$CODE" 0 "private columns dropped, row kept"
eq "$OUT" "Note: item 1: dropped the 'phone' column: phone numbers and home addresses are never collected
Note: item 1: dropped the 'home_address' column: phone numbers and home addresses are never collected
Note: item 1: ignored the unknown column 'city_home'
Added Ada Example (ada-example), 1 source.
Wrote seed/investors.json: 1 investors. Next: investors.py rank --write." "the notes, in column order"
eq "$(J '.investors[0]|[has("phone"),has("home_address"),has("cell"),has("city_home")]' seed/investors.json)" '[false,false,false,false]' "dropped"
eq "$(J '.investors[0].last_relevant_deal' seed/investors.json)" '{"what":"led a seed round","date":"2026-04-01"}' "YYYY-MM is the 1st"
grep -q "Home Street\|415" seed/investors.json && bad "nothing private kept" || ok
fresh; mkdir seed
item '{"email_address":"ada@fund.example","check_size":"$100K-$400K","sector":"pet care","Fund":"ignored: firm is set","Warm":"Yes","Last Deal On":"2026-01-02","last_deal":"led"}' | jq -s . > in.json
I import in.json --today 2026-10-26 --dir seed; eq "$CODE" 0 "aliases"
eq "$(J '.investors[0]|[.email,.check_min,.check_max,.sectors,.warm_intro,.last_relevant_deal.date]' seed/investors.json)" '["ada@fund.example",100000,400000,["pet care"],true,"2026-01-02"]' "aliases land on their fields"
has "$OUT" "ignored the unknown column 'fund'" "an alias whose field is set is unknown"
fresh; mkdir seed
item "{\"email\":\"ada@gmail.com\",\"personal_line\":\"\"}" | jq -s . > in.json
I import in.json --today 2026-10-26 --dir seed
has "$OUT" "Note: item 1 (Ada Example): ada@gmail.com is a personal-style address; use it only if they publish it for pitches" "freemail"
has "$OUT" "no personal_line" "no personal line"
fresh; mkdir seed
echo '[{"name":"Ada Example","firm":"Fund","sources":"https://a.example/1"}]' > a.json
echo '{"investors":[{"name":"ada example","firm":"FUND","sources":"https://a.example/2","email":"ada@fund.example"}]}' > b.json
echo '[{"name":"Ada Example","firm":"Other Fund","sources":"https://o.example/1"}]' > c.json
echo '[{"name":"Someone Else","firm":"X","sources":"https://x.example/1","email":"ADA@fund.example","role":"Partner"}]' > d.json
I import a.json --today 2026-10-26 --dir seed; has "$OUT" "Added Ada Example (ada-example), 1 source." "merge: added"
I import b.json --today 2026-10-27 --dir seed; has "$OUT" "Updated ada example (ada-example), 2 sources." "merge: updated by name and firm"
eq "$(J '.investors|length' seed/investors.json)" 1 "no duplicate"
eq "$(J '.investors[0]|[.sources,.email,.added,.firm]' seed/investors.json)" '[["https://a.example/1","https://a.example/2"],"ada@fund.example","2026-10-26","FUND"]' "sources merged, email set, added kept"
eq "$(J .generated seed/investors.json)" '"2026-10-27"' "generated moves"
I import c.json --today 2026-10-26 --dir seed; has "$OUT" "Added Ada Example (ada-example-2), 1 source." "same name, other firm: a new id"
I import d.json --today 2026-10-26 --dir seed; has "$OUT" "Updated Someone Else (ada-example)" "merge by email, any case"
eq "$(J '.investors[0]|[.name,.role,(.sources|length)]' seed/investors.json)" '["Someone Else","Partner",3]' "the email match took the new fields"
I import c.json --today 2026-10-26 --dir seed
eq "$(J '[.investors[].id]' seed/investors.json)" '["ada-example","ada-example-2"]' "re-import of c updates ada-example-2"

# ---- files, CSV and JSON shapes, exit codes
fresh
I rank --dir seed; eq "$CODE" 1 "nobody to rank yet"
eq "$OUT" "No investors in seed/investors.json yet. Add them: investors.py import FILE.csv, or investors.py add." "nobody text"
I show --dir seed; eq "$CODE" 1 "nothing to show"
eq "$OUT" "No seed/investors.json yet. Add investors: investors.py import FILE.csv" "nothing to show text"
I; eq "$CODE" 2 "no command"
has "$OUT" "usage: investors.py [-h] {formd,import,add,rank,show} ..." "help on stdout"
has "$OUT" "Investor Finder (seed-investors): public research, ranked by fit." "description"
I import nowhere-$$.csv --dir seed; eq "$CODE" 1 "a missing file"
eq "$ERR" "investors.py: no file nowhere-$$.csv" "missing file text"
I import "$EXAMPLE" --today 2026-10-26 --dir seed >/dev/null
I rank --today 2026-10-26 --dir seed; eq "$CODE" 2 "no criteria anywhere"
has "$ERR" "rank needs your stage" "criteria text"
eq "$ERR" "investors.py: rank needs your stage, sectors, geo, ask: pass --stage, or fill seed/config.json (startup.stage, sector, geography, raise)" "criteria text, whole"
I rank --stage seed --sectors pet --today 2026-10-26 --dir seed
eq "$ERR" "investors.py: rank needs your geo, ask: pass --geo, or fill seed/config.json (startup.stage, sector, geography, raise)" "only what is missing"
I rank "${LEASHLY[@]}" --today 2026-1-32 --dir seed; eq "$CODE" 2 "a bad --today"
eq "$ERR" "investors.py: --today wants YYYY-MM-DD, not '2026-1-32'" "bad --today text"
I rank "${LEASHLY[@]}" --today 2026-2-5 --json --dir seed; eq "$(J .generated <<<"$OUT")" '"2026-02-05"' "one-digit month and day"
echo '{"investors": "nope"}' > bad.json; I import bad.json --dir seed; eq "$CODE" 2 "a JSON file without a list"
eq "$ERR" 'investors.py: bad.json wants a list of investors (or {"investors": [...]})' "list text"
echo '[1, 2' > broken.json; I import broken.json --dir seed; eq "$CODE" 2 "broken JSON"
has "$ERR" "investors.py: broken.json is not valid JSON: " "broken JSON text"
cp seed/investors.json keep.json; echo '{' > seed/investors.json
I show --dir seed; eq "$CODE" 2 "a broken investors.json"
has "$ERR" "seed/investors.json is not valid JSON" "broken book text"
cp keep.json seed/investors.json
printf '\xef\xbb\xbfName , FIRM,Source URL,Check Size,Stages,Warm\nZed Example,Zed Fund,https://zed.example/team,$50K-$150K,"seed; series a",yes,extra\n\n' > bom.csv
I import bom.csv --today 2026-10-26 --dir seed; eq "$CODE" 0 "a CSV with a BOM, messy headers and a long row"
eq "$(J '.investors[-1]|[.name,.firm,.sources,.check_min,.check_max,.stage,.warm_intro]' seed/investors.json)" '["Zed Example","Zed Fund",["https://zed.example/team"],50000,150000,["seed","series a"],true]' "BOM and header names normalized"
has "$OUT" "Added Zed Example (zed-example), 1 source." "row added"
I add --name "Ünïcødé Ångel" --firm "Fønd" --source https://u.example/a --personal-line "Hej – tack" --today 2026-10-26 --dir seed
eq "$(J '.investors[-1].id' seed/investors.json)" '"n-c-d-ngel"' "slug keeps ASCII letters only"
grep -q 'Hej – tack' seed/investors.json && ok || bad "ensure_ascii=False: UTF-8 kept as is"
I show "ünïcødé ångel" --dir seed; has "$OUT" "Ünïcødé Ångel, role ?, Fønd (n-c-d-ngel)" "show by name, Unicode lower"
I add --name "!!!" --source https://u.example/b --today 2026-10-26 --dir seed
eq "$(J '.investors[-1].id' seed/investors.json)" '"investor"' "an empty slug is 'investor'"
I add --name "Range Ok" --check 1M- --source https://u.example/d --today 2026-10-26 --dir seed
eq "$(J '.investors[-1]|[.check_min,.check_max]' seed/investors.json)" '[1000000,1000000]' "'1M-' is 1M to 1M"
I add --name "Bad Date" --source https://u.example/c --last-deal x --last-deal-date 2026-02-30 --today 2026-10-26 --dir seed
eq "$OUT" "Refused: --name (Bad Date) last_deal_date wants YYYY-MM-DD or YYYY-MM, not '2026-02-30'" "an impossible date"
eq "$CODE" 1 "refused rows exit 1"

done_ "seed-investors"

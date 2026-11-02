#!/usr/bin/env bash
# trader-pelosi/ptr: the assertions of trader-skill/tests/test_pelosi.py, run against the C++ tool (--offline), plus
# the ZIP reader (deflate, stored, CRC) and the pdftotext path (a stand-in pdftotext on PATH).
source "$(dirname "$0")/lib.sh"
X="$FIX/trader-pelosi"; Z="$X/2026FD.zip"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
P() { run trader-pelosi/ptr "$@"; }
j() { echo "$OUT" | jq -c "$1"; }

# the index
P index --member pelosi --offline "$Z" --year 2026 --json
eq "$CODE" 0 "index exit"
eq "$(j '[.[].doc_id]')" '["20035553","20035143","20034836","20033725"]' "her four PTRs, newest first"
eq "$(j '[.[].filed]')" '["2026-10-02","2026-08-21","2026-06-23","2026-01-23"]' "filed dates"
eq "$(j '.[1].url')" '"https://disclosures-clerk.house.gov/public_disc/ptr-pdfs/2026/20035143.pdf"' "PTR url"
eq "$(j '.[0] | [.district, .name, .type_name, .year]')" '["CA11","Hon. Nancy Pelosi","Periodic Transaction Report",2026]' "row fields"
eq "$(j '.[0] | keys_unsorted')" '["doc_id","type","type_name","last","first","name","district","year","filed","url"]' "row keys"
for m in "pelosi" "Nancy Pelosi" "Pelosi, Nancy" "PELOSI" "  nancy   pelosi " "Pelosi,N"; do P index --member "$m" --offline "$Z" --year 2026 --json; eq "$(j 'length')" 4 "member [$m]"; done
P index --member "Paul Pelosi" --offline "$Z" --year 2026; eq "$CODE" 1 "Paul exit"; eq "$OUT" "No matching filings in the 2026 index." "Paul none"
P index --member pelosi --member gottheimer --offline "$Z" --year 2026 --json; eq "$(j 'length')" 5 "two members"
P index --offline "$Z" --year 2026 --json --limit 1000; eq "$(j 'all(.type == "P")')" true "only PTRs by default"; NP=$(j 'length')
P index --offline "$Z" --year 2026 --json --type '' --limit 1000; ALL=$(j 'length'); [ "$ALL" -gt "$NP" ] && ok || bad "--type '' lists more ($ALL vs $NP)"
eq "$(j '[.[] | select(.type == "C")][0].url | contains("/financial-pdfs/2026/")')" true "other filings use the FD url"
eq "$(j '[.[] | select(.type == "C")][0].type_name')" '"Candidate report"' "type name"
P index --offline "$Z" --year 2026 --json --since 2026-10-07
eq "$(j '[.[].filed] | unique - ["2026-10-07","2026-10-12"]')" '[]' "since filter"; eq "$(j '.[0].filed')" '"2026-10-12"' "newest first"
eq "$(j '[.[].last] | index("Salazar") != null')" true "Salazar"
P index --offline "$Z" --year 2026 --json --limit 2; eq "$(j 'length')" 2 "--limit"
P index --offline "$Z" --year 2026 --json --limit -2; eq "$(j 'length')" "$((NP - 2))" "negative --limit slices like Python"
P index --member pelosi --offline "$Z" --year 2026
has "$OUT" "2026-10-02  Hon. Nancy Pelosi            CA11  P    doc 20035553  https://disclosures-clerk.house.gov/public_disc/ptr-pdfs/2026/20035553.pdf" "index line"
# the XML inside, or alone
if command -v unzip >/dev/null; then
    unzip -q -o "$Z" -d "$TMP/x"; eq "$(head -c 3 "$TMP/x/2026FD.xml" | od -A n -t x1 | tr -d ' ')" "efbbbf" "the Clerk's XML has a BOM"
    P index --offline "$TMP/x/2026FD.xml" --year 2026 --json --type '' --limit 1000; eq "$(j 'length')" "$ALL" "XML alone = ZIP"
else ok; ok; fi
printf 'not xml' > "$TMP/bad.xml"; P index --offline "$TMP/bad.xml"; eq "$CODE" 2 "bad XML exit"; eq "$ERR" "ptr: index is not valid XML: syntax error: line 1, column 0" "bad XML message"
P index --offline "$TMP/nope.zip"; eq "$CODE" 2 "missing file exit"; eq "$ERR" "ptr: [Errno 2] No such file or directory: '$TMP/nope.zip'" "missing file message"
if command -v zip >/dev/null && command -v unzip >/dev/null; then
    (cd "$TMP/x" && zip -q -0 ../stored.zip 2026FD.xml); P index --offline "$TMP/stored.zip" --year 2026 --json --type '' --limit 1000; eq "$(j 'length')" "$ALL" "stored member"
    echo hi > "$TMP/a.txt"; (cd "$TMP" && zip -q noxml.zip a.txt); P index --offline "$TMP/noxml.zip"; eq "$ERR" "ptr: no XML inside the ZIP (files: a.txt)" "no XML inside"
    cp "$TMP/stored.zip" "$TMP/crc.zip"; off=$(grep -abo '<Member>' "$TMP/crc.zip" | head -1 | cut -d: -f1); printf 'X' | dd of="$TMP/crc.zip" bs=1 seek=$((off + 1)) conv=notrunc 2>/dev/null
    P index --offline "$TMP/crc.zip"; [ "$CODE" -ne 0 ] && ok || bad "bad CRC must fail"; has "$ERR" "Bad CRC-32 for file '2026FD.xml'" "bad CRC message"
    head -c 200 "$TMP/stored.zip" > "$TMP/cut.zip"; P index --offline "$TMP/cut.zip"; has "$ERR" "File is not a zip file" "truncated zip"
else for _ in 1 2 3 4 5; do ok; done; fi

# new since the last check
S="$TMP/state/pelosi.json"; NEW() { P new --member pelosi --offline "$Z" --year 2026 --state "$S" --portfolio "$TMP/none.json" "$@"; }
NEW --today 2026-10-13; eq "$CODE" 0 "first run exit"
has "$OUT" "doc 20035553" "first run: the last 45 days"; hasnt "$OUT" "doc 20035143" "older ones skipped"
has "$OUT" "NEW Periodic Transaction Report: Hon. Nancy Pelosi (CA11) filed 2026-10-02, doc 20035553" "NEW line"
has "$OUT" "    https://disclosures-clerk.house.gov/public_disc/ptr-pdfs/2026/20035553.pdf" "url line"
eq "$(jq -c .seen "$S")" '["20033725","20034836","20035143","20035553"]' "seen saved, sorted"
eq "$(jq -r .last_check "$S" | grep -cE '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}$')" 1 "last_check local ISO"
PREV_CHECK=$(jq -r .last_check "$S")   # the run rewrites last_check; read it first (no race at a second boundary)
NEW --today 2026-10-13; eq "$CODE" 1 "second run exit"; has "$OUT" "Nothing new since $PREV_CHECK." "nothing new"
echo '{"seen": ["20033725", "20034836", "20035553"], "last_check": "2026-08-01T09:00:00"}' > "$TMP/s2.json"
P new --member pelosi --offline "$Z" --year 2026 --state "$TMP/s2.json" --today 2026-10-13 --json --dry-run
eq "$(j '[.new[].doc_id]')" '["20035143"]' "a new doc id is new"; eq "$(j '.reports')" '[]' "no reports without --read"
eq "$(jq -c . "$TMP/s2.json")" '{"seen":["20033725","20034836","20035553"],"last_check":"2026-08-01T09:00:00"}' "dry run leaves state"
P new --offline "$Z" --state "$TMP/s3.json" --today 2026-10-13 --dry-run --portfolio "$TMP/none.json"; [ -e "$TMP/s3.json" ] && bad "dry run wrote state" || ok

# parsing a PTR
P parse "$X/ptr-20035143.txt" --json
eq "$(j '[.name, .status, .district, .doc_id, .filed, (.rows | length)]')" '["Hon. Nancy Pelosi","Member","CA11","20035143","2026-08-21",7]' "report head"
eq "$(j 'keys_unsorted')" '["name","status","district","doc_id","filed","rows"]' "report keys"
eq "$(j '[.rows[] | select(.ticker == "BE" and .asset_type == "OP")] | length')" 2 "two BE call rows"
eq "$(j '[.rows[] | select(.ticker == "BE" and .asset_type == "OP")][0] | [.owner, .owner_name, .type, .date, .notified, .amount, .amount_low, .amount_high, .days_to_disclose, .late, .asset, .status]')" \
   '["SP","spouse","P","2026-07-24","2026-07-24","$1,000,001 - $5,000,000",1000001,5000000,28,false,"Bloom Energy Corporation Class A Common Stock","New"]' "the big call row"
eq "$(j '[.rows[] | select(.ticker == "BE" and .asset_type == "OP")][0].option')" '{"contracts":100,"kind":"call","strike":100.0,"expires":"2027-06-17","exercised":false}' "option"
eq "$(j '[.rows[] | select(.ticker == "BE" and .asset_type == "ST") | .shares]')" '[10000,5000]' "shares"
eq "$(j '.rows[-1] | [.ticker, .asset_type, (.description | contains("luxury hotel"))]')" '[null,"AB",true]' "the LLC row"
eq "$(j '.rows[1] | keys_unsorted')" '["owner","owner_name","type","type_name","date","notified","amount","status","account","description","ticker","asset_type","asset","amount_low","amount_high","option","days_to_disclose","late"]' "row key order"
P parse "$X/ptr-20035143.txt" --tickers intc --portfolio /nonexistent
eq "$CODE" 0 "parse text exit"; has "$OUT" "PTR 20035143, filed 2026-08-21" "head line"
has "$OUT" "100 call options, \$100 strike, expires 2027-06-17" "option text"; has "$OUT" "INTC is on your list" "watch flag"
has "$OUT" "Not inside information" "public note"; has "$OUT" "45 days" "45 days"; hasnt "$OUT" $'\u2014' "no em dash"
has "$OUT" "  - spouse purchase, Bloom Energy Corporation Class A Common Stock (BE): 10,000 shares, \$1,000,001 - \$5,000,000, traded 2026-07-24, disclosed 28 days later" "describe line"
has "$OUT" "      Additional investment in LLC which is acquiring and restoring a luxury hotel property in San Francisco, CA." "description line"
echo '{"positions": [{"ticker": "be"}, {"shares": 1}]}' > "$TMP/pf.json"; P parse "$X/ptr-20035143.txt" --portfolio "$TMP/pf.json"; has "$OUT" "BE is on your list" "portfolio tickers flagged"
P parse "$X/ptr-20035590.txt" --json
eq "$(j '[.rows[] | [.ticker, .owner, .type]]')" '[["AMAT","JT","S"],["MCD","JT","S (partial)"]]' "joint owner rows"
eq "$(j '.rows[0] | [.account, .notified]')" '["Morgan Stanley - Select UMA Account # 1","2026-10-06"]' "account and notified"
P parse "$X/ptr-20035562-excerpt.txt" --json
eq "$(j '[.rows[].ticker]')" '["BRK.B","BIIB","BA","BEP","CVX","CB","GLW","IBM"]' "crowded columns tickers"
eq "$(j 'all(.rows[]; .owner == "" and .owner_name == "self")')" true "self owned"
eq "$(j '.rows[0] | [.type_name, .account]')" '["partial sale","UBS IRA Account"]' "partial sale"
eq "$(j '.rows[6] | [.type, .asset]')" '["P","Corning Incorporated Common Stock"]' "type touching the asset"
eq "$(j '.rows[7].asset')" '"International Business Machines Corporation Common Stock"' "asset over three lines"
P parse "$X/ptr-20033725-excerpt.txt" --json
eq "$(j '[.rows[].ticker] | sort')" '["NVDA","PYPL","TEM","VSNT","VST"]' "page break tickers"
eq "$(j '.rows[] | select(.ticker == "TEM") | [.asset, .amount, .option.exercised, .option.strike]')" '["Tempus AI, Inc. - Class A Common Stock","$50,001 - $100,000",true,20.0]' "row split by a page header"
eq "$(j '.rows[] | select(.ticker == "PYPL") | [.date, .shares]')" '["2025-12-30",5000]' "PYPL"
eq "$(j '.rows[] | select(.ticker == "VSNT") | .type_name')" '"exchange"' "exchange"
P parse "$X/ptr-20035590.txt" --filed 2026-11-15 --json; eq "$(j 'all(.rows[]; .late)')" true "late filings flagged"
P parse "$X/ptr-20035590.txt" --filed 2026-11-15; has "$OUT" "(LATE: over 45)" "late text"
P parse "$X/ptr-20035143.txt" --json; eq "$(j '.rows | length')" 7 "parse --json rows"
# dates: 2-digit years, impossible dates make no row
printf 'Name:   Test Member\n  SP   Foo Inc (FOO) [ST]   P   13/45/2026 08/01/2026   $1,001 - $15,000\n' > "$TMP/bad.txt"
P parse "$TMP/bad.txt"; eq "$CODE" 1 "no rows exit"; has "$OUT" "  no transaction rows found: read the PDF directly" "no rows text"; has "$OUT" "Test Member (?), ?. PTR ?, filed ?." "unknown head"
printf '  JT   Foo Inc (FOO) [OP]   S   08/01/2026 08/02/2026   $1,001 - $15,000\n   D : Sold 5 put options with a strike price of $1,250.5 and an expiration date of 1/2/28.\nDigitally Signed: Someone , 08/30/2026\n' > "$TMP/opt.txt"
P parse "$TMP/opt.txt" --json
eq "$(j '[.filed, .rows[0].option, .rows[0].days_to_disclose]')" '["2026-08-30",{"contracts":5,"kind":"put","strike":1250.5,"expires":"2028-01-02","exercised":false},29]' "filed from the signature, put option"
P parse "$TMP/opt.txt"; has "$OUT" "5 put options, \$1250.5 strike, expires 2028-01-02" "%g strike"
printf '  SP   Caf\xe9 Corp (CAF) [ST]   P   08/01/2026 08/02/2026   $1,001 - $15,000\n' > "$TMP/latin.txt"
P parse "$TMP/latin.txt" --json; eq "$(j '.rows[0].asset')" $'"Caf\xef\xbf\xbd Corp"' "invalid UTF-8 replaced"
printf '  SP\xc2\xa0Foo\xc2\xa0Inc (FOO) [ST]   P   08/01/2026 08/02/2026   $1,001 - $15,000\n' > "$TMP/nbsp.txt"
P parse "$TMP/nbsp.txt" --json; eq "$(j '.rows[0] | [.owner, .asset]')" '["SP","Foo Inc"]' "no-break spaces are spaces"
P parse "$TMP/none.txt"; eq "$CODE" 2 "missing text exit"; eq "$ERR" "ptr: [Errno 2] No such file or directory: '$TMP/none.txt'" "missing text message"

# fetch / new --read: a saved PDF and a stand-in pdftotext
mkdir -p "$TMP/bin" "$TMP/pdfs"; printf '%%PDF-1.7 test\n' > "$TMP/pdfs/20035143.pdf"; printf '%%PDF-1.7 test\n' > "$TMP/pdfs/20035553.pdf"
OLDPATH=$PATH
PATH="$TMP/nobin:/usr/bin:/bin" P fetch 20035143 --year 2026 --pdf-dir "$TMP/pdfs"
if command -v pdftotext >/dev/null; then ok; else eq "$OUT" "Saved $TMP/pdfs/20035143.pdf. pdftotext is not installed, so read the PDF directly, or install poppler (brew install poppler, or apt install poppler-utils)." "no pdftotext"; fi
printf '#!/bin/sh\n[ "$1" = "-layout" ] && [ "$3" = "-" ] || exit 9\necho "warning on stderr" >&2\ncat "%s"\n' "$X/ptr-20035143.txt" > "$TMP/bin/pdftotext"; chmod +x "$TMP/bin/pdftotext"
export PATH="$TMP/bin:$OLDPATH"
P fetch 20035143 --year 2026 --pdf-dir "$TMP/pdfs" --json
eq "$CODE" 0 "fetch exit"; eq "$(j '[.doc_id, (.rows | length), .pdf, .url]')" "[\"20035143\",7,\"$TMP/pdfs/20035143.pdf\",\"https://disclosures-clerk.house.gov/public_disc/ptr-pdfs/2026/20035143.pdf\"]" "fetch reads the saved PDF"
eq "$ERR" "" "pdftotext stderr is not passed through"
P fetch 20035143 --year 2026 --pdf-dir "$TMP/pdfs" --filed 2026-09-30 --tickers BE; has "$OUT" "disclosed 68 days later (LATE: over 45)" "--filed"; has "$OUT" "BE is on your list" "fetch watch"
P new --member pelosi --offline "$Z" --year 2026 --state "$TMP/s4.json" --today 2026-10-13 --read --pdf-dir "$TMP/pdfs" --json --portfolio "$TMP/none.json"
eq "$(j '[.reports[] | [.filing.doc_id, .pdf, (.report.rows | length)]]')" "[[\"20035553\",\"$TMP/pdfs/20035553.pdf\",7]]" "new --read"
printf '#!/bin/sh\nexit 1\n' > "$TMP/bin/pdftotext"
P new --member pelosi --offline "$Z" --year 2026 --state "$TMP/s5.json" --today 2026-10-13 --read --pdf-dir "$TMP/pdfs" --portfolio "$TMP/none.json"
has "$OUT" "    read $TMP/pdfs/20035553.pdf (pdftotext not installed)" "failed pdftotext"
export PATH=$OLDPATH
P fetch 99999999 --year 2026 --pdf-dir "$TMP/pdfs"; eq "$CODE" 2 "download failure exit"; has "$ERR" "ptr: " "download failure message"
P; eq "$CODE" 2 "no command exit"; has "$OUT" "    index               list filings from the yearly index" "help lists commands"
# day-trader-skill ships the same ptr.py: daytrade-pelosi/ptr is the same tool
run daytrade-pelosi/ptr index --member pelosi --offline "$Z" --year 2026 --json; eq "$(j '[.[].doc_id]')" '["20035553","20035143","20034836","20033725"]' "daytrade-pelosi alias"
done_ "trader-pelosi"

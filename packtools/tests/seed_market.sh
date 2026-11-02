#!/usr/bin/env bash
# seed-market/market: the assertions of seed-skill/tests/test_market.py, run against the C++ tool. The worked example
# is the reel's made-up startup Leashly: the only real numbers are APPA's; every other input is a labelled assumption.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
BADF="$FIX/seed/seed-market/market-bad.json"
APPA="APPA 2026 State of the Industry report (americanpetproducts.org)"
TOP=(top-down --population 71M --population-name "US homes with a dog" --population-source "$APPA"
     --share 10% --share-name "share that pays for dog walking"
     --share-source "assumption: about 1 in 10 dog homes pays for walks; check it with an owner survey"
     --spend 2400 --spend-name "spend on walks a year per paying home"
     --spend-source "assumption: 8 walks a month at the \$25 flat price, as in seed/model.json")
BOTTOM=(bottom-up --customers 400K --customers-name "paying dog homes in the 3 launch cities"
        --customers-source "assumption: launch-city dog homes x the same 1 in 10; replace with city data"
        --frequency 96 --frequency-source "assumption: 8 walks a month, as in seed/model.json"
        --price 25 --price-source "Leashly pricing: \$25 flat per 30-minute walk" --unit walk)
REASON="Owners leaving Rover over fees and Wag! over cancellations switch to flat \$25 walks with a backup walker"
SOM=(som --share 2.5% --years 3 --reason "$REASON")
SEEDD=""; MJ=""
fresh() { SEEDD="$TMP/$1/seed"; MJ="$SEEDD/market.json"; mkdir -p "$TMP/$1"; }
M() { run seed-market/market "$@" --dir "$SEEDD" --today 2026-10-26; }
swap() { local from="$1" to="$2"; shift 2; local o=(); for x in "$@"; do [ "$x" == "$from" ] && o+=("$to") || o+=("$x"); done; SW=("${o[@]}"); }

# ---- numbers: 71M, 400K, 1.5B, 2,400, $25, 12.5 (read through cite --json)
fresh nums
for pair in "71M=71000000" "400K=400000" "1.5B=1500000000" "2,400=2400" "\$25=25" "12.5=12.5" "3mm=3000000" "2bn=2000000000"; do
  M cite --name n --value "${pair%%=*}" --source "$APPA" --json; eq "$CODE" 0 "num ${pair%%=*} exit"
  eq "$(jq -c '.market.inputs[-1].value' <<<"$OUT")" "${pair#*=}" "num ${pair%%=*}"
done
for b in lots 0 -3 ""; do M cite --name n --value "$b" --source "$APPA"; eq "$CODE" 2 "num refuses [$b]"; done
M cite --name n --value lots --source "$APPA"; has "$ERR" "market.py: --value is not a number like 71M, 400K or 2400: 'lots'" "num message"
M cite --name n --value 0 --source "$APPA"; has "$ERR" "--value must be above 0" "num above 0"
M cite --name n --value "٧١M" --source "$APPA" --json; eq "$(jq -c '.market.inputs[-1].value' <<<"$OUT")" "71000000" "Arabic-Indic digits, like Python's Decimal"

# ---- shares: 10% / 0.1 / 10 all mean a tenth; a bare number above 1 is a percent
for pair in "10%=10" "0.1=10" "10=10" "100%=100" "2.5%=2.5" "1=100"; do
  swap 10% "${pair%%=*}" "${TOP[@]}"; M "${SW[@]}" --json; eq "$CODE" 0 "share ${pair%%=*} exit"
  eq "$(jq -c '.market.inputs[1].value' <<<"$OUT")" "${pair#*=}" "share ${pair%%=*}"
done
for b in 0% 150% abc -5% nan; do swap 10% "$b" "${TOP[@]}"; M "${SW[@]}"; eq "$CODE" 2 "share refuses $b"; done
swap 10% abc "${TOP[@]}"; M "${SW[@]}"; has "$ERR" "--share wants a percent like 10% or a fraction like 0.1, not 'abc'" "share message"
swap 10% 150% "${TOP[@]}"; M "${SW[@]}"; has "$ERR" "--share must be above 0% and at most 100%, not '150%'" "share range message"

# ---- big(): $17.04B, $960M, $24M, $158B, $950
for pair in "17040000000=\$17.04B" "960000000=\$960M" "24000000=\$24M" "158000000000=\$158B" "950=\$950" "1500=\$1.5K" "2000000000000=\$2T"; do
  M cite --name f --value "${pair%%=*}" --source "$APPA"; has "$OUT" "Fact: f = ${pair#*=}" "big ${pair%%=*}"
done

# ---- sources: publication, url, assumption; vague ones are refused with exit 1
for pair in "$APPA=publication" "https://americanpetproducts.org/industry-trends=url" "Assumption: 1 in 10 homes pays=assumption"; do
  M cite --name s --value 5 --source "${pair%=*}" --json; eq "$(jq -r '.market.inputs[-1].kind' <<<"$OUT")" "${pair##*=}" "source kind ${pair##*=}"
done
for b in "" google TBD ChatGPT assumption "assumption: guess" Statista "Astro AI" "  google. "; do
  M cite --name s --value 5 --source "$b"; eq "$CODE" 1 "source refused [$b]"
done
M cite --name s --value 5 --source google; has "$ERR" "--source needs a real source: a URL, a named publication, or \"assumption: <why>\" (got 'google')" "vague message"
M cite --name s --value 5 --source Statista; has "$ERR" "--source: 'Statista' is too vague; name the publication or paste the URL" "one-word message"
M cite --name s --value 5 --source "assumption: guess"; has "$ERR" "an assumption needs its reason in a few words" "assumption message"

# ---- the reel: TAM, SAM, SOM and two facts
fresh reel
M "${TOP[@]}"; eq "$CODE" 0 "top-down dry"; has "$OUT" "Not saved" "dry run says so"; [ -e "$MJ" ] && bad "wrote without --write" || ok
M "${TOP[@]}" --write; eq "$CODE" 0 "top-down write"
has "$OUT" "TAM (top-down): \$17.04B a year" "TAM line"
has "$OUT" "71,000,000 US homes with a dog x 10% x \$2,400 = \$17,040,000,000" "TAM formula"
has "$OUT" "Assumptions in it" "assumptions flagged"; has "$OUT" "Not investment advice" "note"
has "$OUT" "Wrote $MJ." "wrote line"
M "${BOTTOM[@]}" --write; eq "$CODE" 0 "bottom-up write"
has "$OUT" "400,000 paying dog homes in the 3 launch cities x 96 walks a year x \$25 = \$960,000,000" "SAM formula"
M "${SOM[@]}" --write; eq "$CODE" 0 "som write"
has "$OUT" "SOM: \$24M a year within 3 years (2.5% of SAM \$960M)" "SOM line"
M cite --name "US pet industry spend, 2025" --value 158B --source "$APPA" --write
M cite --name "Share of US homes with a dog" --value 53 --unit "% of US homes" --source "$APPA" --write
eq "$(jq -c '[.tam,.sam,.som,.currency]' "$MJ")" '[17040000000,960000000,24000000,"USD"]' "the three numbers"
eq "$(jq -r .method "$MJ")" "top-down TAM, bottom-up SAM, SOM 2.5% of SAM in 3 years" "method"
eq "$(jq -c '[.som_share_pct,.som_years]' "$MJ")" "[2.5,3]" "som share and years"
eq "$(jq -r '.som_reason[0:20]' "$MJ")" "Owners leaving Rover" "som reason"
eq "$(jq -c '.inputs[]|select(.level=="tam" and .name=="US homes with a dog")|[.value,.kind]' "$MJ")" '[71000000,"publication"]' "population input"
eq "$(jq -c '.inputs[]|select(.level=="tam" and .name=="share that pays for dog walking")|[.value,.kind]' "$MJ")" '[10,"assumption"]' "share input"
eq "$(jq -c '.inputs[]|select(.level=="sam" and .name=="price per walk")|.value' "$MJ")" "25" "price input"
eq "$(jq -c '.inputs[]|select(.level=="context" and .name=="US pet industry spend, 2025")|.value' "$MJ")" "158000000000" "fact input"
eq "$(jq '[.inputs[]|select(has("name") and has("value") and has("unit") and has("source"))]|length' "$MJ")" "$(jq '.inputs|length' "$MJ")" "every input complete"
eq "$(jq -r 'keys_unsorted|join(",")' "$MJ")" "created_by,generated,method,tam,sam,som,currency,inputs,notes,methods,formulas,som_share_pct,som_years,som_reason" "key order"
eq "$(tail -c 2 "$MJ" | od -An -c | tr -d ' ')" '}\n' "trailing newline"
M show; eq "$CODE" 0 "show exit"
has "$OUT" "Fact: US pet industry spend, 2025 = \$158B" "show fact"
has "$OUT" "Fact: Share of US homes with a dog = 53% of US homes" "show percent fact"
has "$OUT" "  TAM  \$17.04B    71,000,000" "show padded column"
has "$OUT" "  Assumption (TAM): share that pays for dog walking = 10%. assumption: about 1 in 10" "show assumption"
M check; eq "$CODE" 0 "check ok"; has "$OUT" "OK: SOM <= SAM <= TAM" "check message"
M check --json; eq "$(jq -c . <<<"$OUT")" '{"ok":true,"problems":[]}' "check json"
M cite --name "US pet industry spend, 2025" --value 158B --source "$APPA" --write
eq "$(jq '[.inputs[]|select(.level=="context")]|length' "$MJ")" "2" "citing again replaces"

# ---- SOM follows SAM; the order is enforced
fresh order
M "${TOP[@]}" --write; M "${BOTTOM[@]}" --write; M "${SOM[@]}" --write
swap 400K 200K "${BOTTOM[@]}"; M "${SW[@]}" --write; eq "$CODE" 0 "smaller SAM"
has "$OUT" "SOM recomputed from the new SAM: \$12M" "SOM recomputed"
eq "$(jq -c '[.sam,.som]' "$MJ")" "[480000000,12000000]" "saved recomputed"
swap 400K 8M "${BOTTOM[@]}"; M "${SW[@]}" --write; eq "$CODE" 1 "SAM over TAM refused"
has "$ERR" "SAM \$19.2B is bigger than TAM \$17.04B" "refusal message"; has "$ERR" "Not saved. Fix the inputs" "not saved"
eq "$(jq .sam "$MJ")" "480000000" "file unchanged"
M "${SW[@]}" --json; eq "$CODE" 1 "json refused"; eq "$(jq .ok <<<"$OUT")" "false" "json ok false"

# ---- refusals and exit codes
fresh refuse
M "${SOM[@]}"; eq "$CODE" 1 "som before sam"; has "$ERR" "size SAM first" "som before sam message"
M "${BOTTOM[@]}" --write
M som --share 2% --years 3 --reason "we are great"; eq "$CODE" 1 "thin reason"
M som --share 2% --years 0 --reason "$REASON"; eq "$CODE" 2 "years 0"; has "$ERR" "market.py: --years is 1 to 10" "years message"
M som --share 2% --years 11 --reason "$REASON"; eq "$CODE" 2 "years 11"
M som --share 2% --years x --reason "$REASON"; eq "$CODE" 2 "years not int"
M som --share 20% --years 3 --reason "$REASON"; eq "$CODE" 0 "big share ok"; has "$OUT" "is a lot: be ready to defend it" "big share note"
has "$OUT" "Note: 20% of the SAM in 3 years is a lot" "note line"
swap "$APPA" google "${TOP[@]}"; M "${SW[@]}" --write; eq "$CODE" 1 "vague source"; has "$ERR" "needs a real source" "vague source message"
swap 10% 150% "${TOP[@]}"; M "${SW[@]}"; eq "$CODE" 2 "share 150%"
M top-down --population 71M; eq "$CODE" 2 "missing flags"
run seed-market/market; eq "$CODE" 2 "no command"; has "$OUT" "usage: market.py" "no command prints help"
run seed-market/market show --dir "$TMP/none"; eq "$CODE" 1 "show no file"; has "$OUT" "No $TMP/none/market.json yet. Start with market.py top-down ... --write" "show no file message"
run seed-market/market check --dir "$TMP/none"; eq "$CODE" 1 "check no file"

# ---- whole dollars, half up
fresh round
M top-down --population 1 --population-source "$APPA" --share 50% --share-source "$APPA" --spend 3 --spend-source "$APPA" --json
eq "$(jq .market.tam <<<"$OUT")" "2" "1.5 -> 2"
for pair in "12.5=38" "2.5/1=3" "2.49/1=2"; do
  p="${pair%%=*}"; c=3; [[ "$p" == */* ]] && { c="${p#*/}"; p="${p%/*}"; }
  M bottom-up --customers "$c" --customers-source "$APPA" --frequency 1 --frequency-source "$APPA" --price "$p" --price-source "$APPA" --json
  eq "$(jq .market.sam <<<"$OUT")" "${pair#*=}" "half up $pair"
done
M bottom-up --customers 3 --customers-source "$APPA" --frequency 1 --frequency-source "$APPA" --price 12.5 --price-source "$APPA"
has "$OUT" "3 customers you can reach x 1 purchases a year x \$12.5 = \$38" "decimal price formula"

# ---- check reads a bad file
fresh badf; mkdir -p "$SEEDD"; cp "$BADF" "$MJ"
M check; eq "$CODE" 1 "check bad"
has "$OUT" "SAM \$960M is bigger than TAM \$500M" "order problem"; has "$OUT" "SOM is not sized yet" "not sized"
has "$OUT" "'google'" "google flagged"; has "$OUT" "'TBD'" "TBD flagged"
has "$OUT" "Problem: input 'dog homes in the city' needs a real source" "input label"
M check --json; eq "$(jq '.problems|length' <<<"$OUT")" "4" "four problems"
M show; eq "$CODE" 0 "show bad"; has "$OUT" "Check: SAM \$960M is bigger than TAM \$500M" "show check line"
has "$OUT" "  SOM  not sized  " "show not sized"

# ---- the port's own edges: --today, invalid JSON, currency
fresh edges
run seed-market/market "${TOP[@]}" --dir "$SEEDD" --today 2026-1-5 --json; eq "$(jq -r .market.generated <<<"$OUT")" "2026-01-05" "strptime takes one-digit month and day"
run seed-market/market "${TOP[@]}" --dir "$SEEDD" --today 2026-02-30; eq "$CODE" 2 "not a real day"; has "$ERR" "--today wants YYYY-MM-DD, not '2026-02-30'" "today message"
run seed-market/market "${TOP[@]}" --dir "$SEEDD" --today 26-10-2026; eq "$CODE" 2 "wrong order"
mkdir -p "$SEEDD"; printf 'nope' > "$MJ"; M "${TOP[@]}"; eq "$CODE" 2 "bad JSON"
has "$ERR" "market.py: $MJ is not valid JSON: Expecting value: line 1 column 1 (char 0)" "bad JSON message"
rm -f "$MJ"; swap 71M 71M "${TOP[@]}"; M "${SW[@]}" --currency EUR --write; eq "$CODE" 0 "EUR"
has "$OUT" "TAM (top-down): EUR 17.04B a year" "EUR big"; has "$OUT" "x EUR 2,400 = EUR 17,040,000,000" "EUR money"
has "$OUT" "spend on walks a year per paying home = EUR 2,400 a year" "EUR show_value"
M show; has "$OUT" "Market size (EUR a year)" "EUR report"
fresh dec
M bottom-up --customers 3 --customers-source "$APPA" --frequency 1.5 --frequency-source "$APPA" --price 0.75 --price-source "$APPA"
has "$OUT" "3 customers you can reach x 1.5 purchases a year x \$0.75 = \$3" "fmt_num of decimals"

done_ "seed-market/market"

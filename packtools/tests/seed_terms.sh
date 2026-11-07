#!/usr/bin/env bash
# seed-terms/terms: the assertions of seed-skill/tests/test_terms.py, run against the C++ tool through its command
# line, plus hand-worked edge cases (each expected number below is derived from terms.py's exact Fraction math).
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
TF="$FIX/seed/seed-terms"
X() { run seed-terms/terms "$@"; }
J() { jq -c "$@"; }
mkdir -p "$TMP/w"; cd "$TMP/w" || exit 1
NOTE_L="Not legal advice and not investment advice: a startup lawyer should read the documents before you sign. The numbers are only as good as the inputs."

# ---- inputs: amounts, percentages, SAFE specs
X safe --investment 1.5m --cap 10m; eq "$CODE" 0 "1.5m"; has "$OUT" "Post-money SAFE: \$1,500,000 at a \$10,000,000 post-money cap" "1.5m = 1,500,000"
X safe --investment '$1,500,000' --cap 10m; has "$OUT" "= 15.00% of the company" "\$1,500,000"
X safe --investment 500K --cap 10m; has "$OUT" "Post-money SAFE: \$500,000 at" "500K"
X safe --investment '1 500 000' --cap 10m; has "$OUT" "SAFE: \$1,500,000 at" "spaces are dropped"
X safe --investment 1234.5 --cap 10m; has "$OUT" "Post-money SAFE: \$1,234.50 at" "cents when not whole"
X safe --investment 0.5m --cap 2b; has "$OUT" "\$500,000 / \$2,000,000,000 = 0.03%" "b = billion"
for b in abc -5; do
  X safe --investment "$b" --cap 10m; eq "$CODE" 1 "bad amount $b"; eq "$ERR" "terms.py: SAFE investment wants an amount like 500k, 1.5m or 1500000, got '$b'" "bad amount message $b"
done
X safe --investment 0 --cap 10m; eq "$CODE" 1 "zero"; eq "$ERR" "terms.py: SAFE investment must be more than zero" "zero message"
X safe --investment 100k --cap 1m --discount 0.2; eq "$CODE" 1 "0.2"; eq "$ERR" "terms.py: SAFE discount: write 20 for 20% (got 0.2)" "say 20 for 20%"
X safe --investment 100k --cap 1m --discount 0.2%; eq "$CODE" 0 "0.2% is fine"; has "$OUT" ", 0.20% discount" "0.2%"
X safe --investment 100k --cap 1m --discount 12.5%; has "$OUT" "Post-money SAFE: \$100,000 at a \$1,000,000 post-money cap, 12.50% discount" "12.5%"
X safe --investment 100k --cap 1m --discount 60; eq "$CODE" 1 "60"; eq "$ERR" "terms.py: SAFE discount must be between 0 and 50%" "discount over 50"
X safe --investment 100k --cap 1m --discount x; eq "$ERR" "terms.py: SAFE discount wants a percentage like 20 or 20%, got 'x'" "bad percent"
X safe --investment 100k --cap 1m --discount 0; eq "$CODE" 0 "discount 0"; hasnt "$OUT" "discount" "a 0 discount is not shown"
X safe --investment 100k --cap ''; eq "$CODE" 1 "empty cap = no cap"; eq "$ERR" "terms.py: a SAFE with no cap owns whatever the next round's price gives it: add --round-pre and --round-raise to see it" "uncapped needs a round"
X safe --investment 100k --cap 0; eq "$ERR" "terms.py: SAFE cap must be more than zero" "cap 0 is parsed"
X safe --investment 100k --cap 10m --other Northlight=500k@10m:20; eq "$CODE" 0 "named spec"; has "$OUT" "  Northlight     \$500,000 at \$10M = 5.00%" "name=amount@cap:discount"
X safe --investment 100k --cap 10m --other 100k@:20; eq "$CODE" 1 "uncapped other"; has "$ERR" "a SAFE with no cap owns" "100k@:20 has no cap"
X safe --investment 100k --cap 10m --other 1m@500k; eq "$CODE" 1 "cap below investment"
eq "$ERR" "terms.py: a \$500,000 cap with a \$1,000,000 investment would hand over the whole company: the post-money cap must be bigger than the investment" "cap below the investment"
X safe --investment 1m --cap 1m; eq "$CODE" 1 "cap = investment"; has "$ERR" "a \$1,000,000 cap with a \$1,000,000 investment" "cap equal to the investment"
for s in "" ":20" "@10m"; do
  X safe --investment 100k --cap 10m --other "$s"; eq "$CODE" 1 "spec [$s]"; eq "$ERR" "terms.py: a SAFE looks like [name=]investment[@cap][:discount], e.g. 500k@10m or 250k@8m:20, got '$s'" "bad spec [$s]"
done
X safe --investment 100k --cap 10m --other "1m@b@c"; eq "$ERR" "terms.py: SAFE cap wants an amount like 500k, 1.5m or 1500000, got 'b@c'" "the cap takes the rest up to ':'"
X safe --investment 100k --cap 10m --other "  =1m@5m  "; eq "$ERR" "terms.py: SAFE investment wants an amount like 500k, 1.5m or 1500000, got '=1m'" "an empty name is part of the amount"
X safe --investment 100k --cap 10m --other " Ann =1m@5m"; has "$OUT" "  Ann            \$1,000,000 at \$5M = 20.00%" "names are stripped"

# ---- post-money SAFE math
X safe --investment 500k --cap 10m --name Northlight --other 1m@10m --write --dir d; eq "$CODE" 0 "safe --write"
has "$OUT" "\$500,000 / \$10,000,000 = 5.00%" "5% at the cap"; has "$OUT" "Existing holders keep 85.00%" "keep 85%"; has "$OUT" "Not legal advice" "note"
has "$OUT" "All SAFEs together: \$1,500,000 for 15.00%:" "all together"; has "$OUT" "  SAFE           \$1,000,000 at \$10M = 10.00%" "an unnamed other"
has "$OUT" "once they convert." "they convert"; eq "$(tail -1 <<<"$OUT")" "Wrote d/terms/safe.json." "wrote"
eq "$(J '[.ownership_at_cap,.all_safes,.existing_after_safes]' d/terms/safe.json)" "[0.05,0.15,0.85]" "safe.json numbers"
eq "$(J 'keys_unsorted' d/terms/safe.json)" '["instrument","safes","note","ownership_at_cap","all_safes","existing_after_safes"]' "update keeps the safes key in place"
eq "$(J '.safes' d/terms/safe.json)" '[{"name":"Northlight","investment":500000.0,"cap":10000000.0,"discount":0.0,"ownership_at_cap":0.05},{"name":null,"investment":1000000.0,"cap":10000000.0,"discount":0.0,"ownership_at_cap":0.1}]' "safe.json safes"
eq "$(grep -c '"investment": 500000.0,' d/terms/safe.json)" 1 "floats keep .0"
[ -e d/terms/safe.json.tmp ] && bad "no .tmp left" || ok
X safe --investment 500k --cap 10m; eq "$OUT" "Post-money SAFE: \$500,000 at a \$10,000,000 post-money cap
  At the cap: \$500,000 / \$10,000,000 = 5.00% of the company when it converts, before the next round's new money and any new option pool dilute everyone.
Existing holders keep 95.00% of the company (from 100.00%) once it converts.
With post-money caps each SAFE's percentage is fixed, so every SAFE you add dilutes you, not the other SAFE holders.
$NOTE_L" "one SAFE, whole output"
X safe --investment 6m --cap 10m --other 5m@10m; eq "$CODE" 1 "over 100%"; eq "$ERR" "terms.py: these SAFEs would own 110.00% of the company: that leaves nothing for you" "SAFEs over 100%"
X safe --investment 100k --cap 10m --other x=1@999 --other y=1@1.5b --other z=1@2.25m; has "$OUT" "  x              \$1 at \$999 = 0.10%" "short: under 1K is money"
has "$OUT" "  y              \$1 at \$1.5B = 0.00%" "short: B"; has "$OUT" "  z              \$1 at \$2.25M = 0.00%" "short: M"
X safe --investment 100k --cap 10m --other w=1@25000; has "$OUT" "  w              \$1 at \$25K = 0.00%" "short: K, zeros stripped"
X safe --investment 500k --cap 10m --round-pre 24m; eq "$CODE" 1 "half a round"; eq "$ERR" "terms.py: a priced round needs both --round-pre and --round-raise" "both round flags"
X safe --investment 500k --round-raise 6m; eq "$CODE" 1 "uncapped, raise only"; has "$ERR" "a SAFE with no cap owns" "uncapped needs --round-pre"

# ---- a priced round with converting SAFEs (test_converts_*)
X round --pre 20m --raise 5m --shares 9000000 --safe 1m@10m; eq "$CODE" 0 "at the cap"
has "$OUT" "  Price per share  \$2.0000  (\$20,000,000 / 10,000,000 fully diluted pre-money shares)" "fd_pre 10M, pps 2"
has "$OUT" "  New shares       2,500,000 for the new investors (20.00% of the company after)" "new shares 2.5M"
has "$OUT" "  SAFE 1, \$1,000,000: cap \$1.0000 a share vs round price \$2.0000, converts at the cap: 1,000,000 shares (8.00% after)" "converts at the cap: 8%"
hasnt "$OUT" "Option pool" "no pool line or row without a pool"
has "$OUT" "The SAFEs convert into 1,000,000 shares worth \$2,000,000 at the round price, also inside the pre-money." "SAFE worth"
has "$OUT" "Existing holders go from 100.00% to 72.00% (diluted by 28.00% of what they had)." "dilution"
X round --pre 20m --raise 5m --shares 9000000 --safe 1m@30m:20; eq "$CODE" 0 "at the discount"
has "$OUT" "  SAFE 1, \$1,000,000: cap \$3.1250 a share vs discount \$1.6667 a share, converts at the discount: 600,000 shares (5.00% after)" "converts at the discount"
has "$OUT" "(\$20,000,000 / 9,600,000 fully diluted pre-money shares)" "fd_pre 9.6M"; has "$OUT" "  New shares       2,400,000 for" "new 2.4M"
has "$OUT" "Existing holders go from 100.00% to 75.00%" "existing 75%"
X round --pre 20m --raise 5m --shares 9000000 --safe 1m; eq "$CODE" 0 "uncapped"
has "$OUT" "  SAFE 1, \$1,000,000: no cap vs round price \$2.1111, converts at the round price: 473,684 shares (4.00% after)" "uncapped converts at the round price"

# ---- the option-pool shuffle (test_the_option_pool_shuffle, test_round_output_and_write)
X round --pre 6m --raise 1.5m --pool 10 --shares 7000000 --write --dir d; eq "$CODE" 0 "round --write"
for part in "Price per share  \$0.7500" "Effective pre-money: \$6,000,000 - \$750,000 = \$5,250,000" "Existing holders go from 100.00% to 70.00%"; do has "$OUT" "$part" "round: $part"; done
eq "$(sed -n 6,10p <<<"$OUT")" "                                         before                    after         
  Founders and existing holders       7,000,000  100.00%       7,000,000   70.00%
  Option pool (unissued)                      0    0.00%       1,000,000   10.00%
  New investors                               -        -       2,000,000   20.00%
  Total                               7,000,000  100.00%      10,000,000  100.00%" "the table"
has "$OUT" "  Option pool      1,000,000 new shares, so the unissued pool is 10.00% after the round" "pool line"
has "$OUT" "Ask for the pool to be sized to a real 18-month hiring plan, not a round number." "pool advice"
eq "$(tail -1 <<<"$OUT")" "Wrote d/terms/round.json." "round wrote"
eq "$(J '[.price_per_share,.pool_topup_shares,.new_investor_shares,.effective_pre_money]' d/terms/round.json)" "[0.75,1000000,2000000,5250000.0]" "round.json numbers"
eq "$(J 'keys_unsorted' d/terms/round.json)" '["pre_money","new_money","post_money","pool_target","price_per_share","fd_pre_money_shares","pool_topup_shares","new_investor_shares","fd_post_money_shares","effective_pre_money","safes","table","note"]' "round.json keys"
eq "$(J '[.pre_money,.new_money,.post_money,.pool_target,.fd_pre_money_shares,.fd_post_money_shares,.safes]' d/terms/round.json)" "[6000000.0,1500000.0,7500000.0,0.1,8000000,10000000,[]]" "round.json rest"
eq "$(J '.table' d/terms/round.json)" '[{"holder":"Founders and existing holders","before":7000000,"after":7000000},{"holder":"Option pool (unissued)","before":0,"after":1000000},{"holder":"New investors","before":null,"after":2000000}]' "round.json table"
X round --pre 6m --raise 1.5m --pool 10 --shares 6000000 --pool-now 2000000; eq "$CODE" 0 "existing pool"
has "$OUT" "  Option pool      0 new shares, so the unissued pool is 10.00% after the round" "an existing pool counts toward the target"
has "$OUT" "  Option pool (unissued)              2,000,000   25.00%       2,000,000   20.00%" "pool row"
hasnt "$OUT" "option-pool shuffle" "no shuffle without a top-up"; has "$OUT" "Existing holders go from 75.00% to 60.00% (diluted by 20.00% of what they had)." "existing pool dilution"
X round --pre 6m --raise 1.5m --pool 0 --shares 7000000; eq "$CODE" 0 "pool 0"; hasnt "$OUT" "Option pool" "pool 0 = no pool"
X round --pre 6m --raise 1.5m --pool 60; eq "$CODE" 1 "pool 60"; eq "$ERR" "terms.py: --pool must be between 0 and 50%" "pool over 50"
X round --pre 6m --raise 1.5m --shares 7_000_000; eq "$CODE" 0 "underscores"; has "$OUT" "7,000,000  100.00%" "7_000_000"
X round --pre 6m --raise 1.5m --shares abc; eq "$CODE" 1 "bad shares"; eq "$ERR" "terms.py: --shares wants a whole number of shares, got 'abc'" "bad shares message"
X round --pre 6m --raise 1.5m --shares 0; eq "$ERR" "terms.py: --shares must be more than zero" "zero shares"
X round --pre 6m --raise 1.5m --pool-now 1.5; eq "$ERR" "terms.py: --pool-now wants a whole number of shares, got '1.5'" "bad pool-now"
X round --pre 10m --raise 1m --shares 1000 --safe 6m@10m --safe 5m@10m; eq "$CODE" 1 "impossible"; eq "$ERR" "terms.py: the SAFEs' caps add up to 110.00% of the company" "caps over 100%"
X round --pre 6m --raise 1.5m --pool 50 --safe 1m --safe 2m; eq "$CODE" 1 "nothing left"; eq "$ERR" "terms.py: a 50.00% pool and these SAFEs leave nothing for the existing holders" "pool and SAFEs leave nothing"
X round --pre 6m; eq "$CODE" 2 "missing --raise"; has "$ERR" "the following arguments are required: --raise" "argparse required"

# ---- SAFEs and a pool together hold every invariant (24m pre, 6m new, 10% pool, 9M + 1M pool)
X round --pre 24m --raise 6m --pool 10 --shares 8000000 --pool-now 1000000 --safe 500k@10m --safe 250k@8m:20 --write --dir inv; eq "$CODE" 0 "invariants"
eq "$(J '[.safes[].converts_at]' inv/terms/round.json)" '["cap","cap"]' "both at the cap"
eq "$(J '[.safes[0].shares,.safes[1].shares,.pool_topup_shares,.new_investor_shares,.fd_pre_money_shares,.fd_post_money_shares]' inv/terms/round.json)" "[489795,306122,256559,2513119,10052478,12565597]" "the round in shares"
has "$OUT" "  New shares       2,513,119 for the new investors (20.00% of the company after)" "new money = N / (V + N)"
has "$OUT" "  Option pool (unissued)              1,000,000   11.11%       1,256,559   10.00%" "pool after = P"

# ---- a cap table file (test_round_from_a_cap_table)
X round --pre 24m --raise 6m --pool 10 --cap-table "$TF/cap-table.json"; eq "$CODE" 0 "cap table"
has "$OUT" "Casey Morgan (founder)" "holder name"; has "$OUT" "Northlight, \$500,000" "table SAFE"
has "$OUT" "  Harbor Seed, \$250,000: cap \$0.8167 a share vs discount \$1.9100 a share, converts at the cap: 306,122 shares (2.44% after)" "Harbor Seed"
eq "$(sed -n 8,15p <<<"$OUT")" "                                   before                    after         
  Casey Morgan (founder)        6,000,000   66.67%       6,000,000   47.75%
  Riley Park (co-founder)       2,000,000   22.22%       2,000,000   15.92%
  Option pool (unissued)        1,000,000   11.11%       1,256,559   10.00%
  SAFE: Northlight                      -        -         489,795    3.90%
  SAFE: Harbor Seed                     -        -         306,122    2.44%
  New investors                         -        -       2,513,119   20.00%
  Total                         9,000,000  100.00%      12,565,595  100.00%" "cap table rows"
X round --pre 24m --raise 6m --pool 10 --cap-table "$TF/cap-table.json" --safe Late=100k@20m; has "$OUT" "  Late, \$100,000: cap" "--safe adds to the table SAFEs"
has "$OUT" "  SAFE: Late" "a third SAFE row"; eq "$(grep -o 'SAFE: [A-Za-z]*' <<<"$OUT" | tr '\n' ',')" "SAFE: Northlight,SAFE: Harbor,SAFE: Late," "table SAFEs come first"
X safe --investment 500k --cap 10m --round-pre 24m --round-raise 6m --pool 10 --cap-table "$TF/cap-table.json" --write --dir sw; eq "$CODE" 0 "safe with a cap table"
has "$OUT" "Casey Morgan (founder)" "safe uses the holders"; hasnt "$OUT" "Northlight" "safe ignores the table SAFEs"; has "$OUT" "  This SAFE, \$500,000: cap" "This SAFE"
has "$OUT" "Priced round: \$6,000,000 of new money at \$24,000,000 pre-money (\$30,000,000 post-money)" "the round in safe"
eq "$(J 'keys_unsorted' sw/terms/safe.json)" '["instrument","safes","note","ownership_at_cap","all_safes","existing_after_safes","round"]' "round last"
eq "$(J '.safes[0].name' sw/terms/safe.json)" '"This SAFE"' "default name"
echo '{"holders": [{"name": "A", "shares": 900}, {"name": "B", "shares": true}], "pool_unissued": "100"}' > ct-bool.json
X round --pre 1m --raise 250k --cap-table ct-bool.json --write --dir b; eq "$CODE" 0 "bool shares and a string pool"
has "$OUT" "  B                                    1    0.10%               1    0.08%" "True counts as one share"; has "$OUT" "  Total                            1,001  100.00%           1,251  100.00%" "total with the string pool"
eq "$(J '.table[1]' b/terms/round.json)" '{"holder":"B","before":true,"after":true}' "json.dump writes the bool"
echo '{"holders": []}' > ct-empty.json; X round --pre 1m --raise 1m --cap-table ct-empty.json; eq "$CODE" 1 "no holders"; eq "$ERR" "terms.py: ct-empty.json has no holders" "no holders message"
for h in '{"name": "A", "shares": 1.5}' '{"name": "A", "shares": -1}' '{"name": "", "shares": 5}' '{"shares": 5}' '{"name": "A", "shares": "5"}'; do
  echo "{\"holders\": [$h]}" > ct-bad.json; X round --pre 1m --raise 1m --cap-table ct-bad.json; eq "$CODE" 1 "bad holder $h"
  eq "$ERR" "terms.py: ct-bad.json: every holder needs a name and whole shares" "bad holder message $h"
done
echo 'nope' > ct-nojson.json; X round --pre 1m --raise 1m --cap-table ct-nojson.json; eq "$CODE" 1 "not json"
eq "$ERR" "terms.py: ct-nojson.json is not valid JSON: Expecting value: line 1 column 1 (char 0)" "not valid JSON"
X round --pre 1m --raise 1m --cap-table missing.json; eq "$ERR" "terms.py: no such file: missing.json" "missing cap table"
echo '{"holders": [{"name": "A", "shares": 10}], "safes": [{"name": "S", "investment": 1500000.0, "cap": 1000000}]}' > ct-safe.json
X round --pre 1m --raise 1m --cap-table ct-safe.json; eq "$CODE" 1 "json SAFE"; eq "$ERR" "terms.py: a \$1,000,000 cap with a \$1,500,000 investment would hand over the whole company: the post-money cap must be bigger than the investment" "JSON numbers go through str()"
echo '{"holders": [{"name": "A", "shares": 10}], "safes": [{"investment": "1m", "cap": 0}]}' > ct-cap0.json
X round --pre 1m --raise 1m --cap-table ct-cap0.json; eq "$ERR" "terms.py: SAFE cap must be more than zero" "a JSON cap of 0 is not 'no cap'"

# ---- refusals exit 1, never a traceback (test_refusals_exit_1)
for argv in "safe --investment 100k --discount 20" "safe --investment 1m --cap 500k" "round --pre 6m --raise 1.5m --pool 60" "scan $TMP/missing.txt"; do
  X $argv; eq "$CODE" 1 "refused: $argv"; hasnt "$ERR" "Traceback" "no traceback: $argv"
done
X; eq "$CODE" 1 "no command"; has "$OUT" "usage: terms.py [-h] {safe,round,scan} ..." "help on stdout"; has "$OUT" "scan             flag the clauses to ask a lawyer about" "help lists scan"

# ---- the scanner (test_the_tough_sheet_flags_every_trap, test_the_clean_sheet_is_standard)
X scan "$TF/term-sheet-tough.txt" --json; eq "$CODE" 3 "tough exit 3"
eq "$(J '[.flags[]|{(.key):.severity}]|add' <<<"$OUT" | jq -S -c .)" '{"anti_dilution":"high","board":"high","dividends":"high","drag_along":"high","exploding":"high","liquidation_preference":"high","mfn":"low","no_shop":"high","participating":"high","pay_to_play":"medium","pool_shuffle":"medium","protective_provisions":"high","redemption":"high","super_pro_rata":"medium","vesting_reset":"high"}' "every trap"
eq "$(J '[.flags[]|select((.note|length)>0 and (.ask|length)>0 and (.excerpt|length)>0)]|length' <<<"$OUT")" 15 "each flag has a note, an ask and an excerpt"
eq "$(J '[.flags[].severity]|unique' <<<"$OUT")" '["high","low","medium"]' "severities"
eq "$(J '[.flags[].severity]' <<<"$OUT")" "$(J '[.flags[].severity]|sort_by({"high":0,"medium":1,"low":2}[.])' <<<"$OUT")" "sorted by severity"
eq "$(J '.counts' <<<"$OUT")" '{"high":11,"medium":3,"low":1}' "counts"; eq "$(J 'keys_unsorted' <<<"$OUT")" '["file","flags","counts","note"]' "json keys"
eq "$(J '.flags[0]|keys_unsorted' <<<"$OUT")" '["key","clause","severity","note","ask","excerpt"]' "flag keys"
eq "$(J '.file' <<<"$OUT")" '"term-sheet-tough.txt"' "file is the basename"
eq "$(J '[.flags[]|select(.severity=="high")|.clause]' <<<"$OUT")" "$(J '[.flags[]|select(.severity=="high")|.clause]|sort' <<<"$OUT")" "then by clause"
X scan "$TF/term-sheet-clean.txt" --json; eq "$CODE" 0 "clean exit 0"
eq "$(J '[.flags[]|{(.key):.severity}]|add' <<<"$OUT" | jq -S -c .)" '{"anti_dilution":"standard","board":"standard","dividends":"standard","exploding":"low","liquidation_preference":"standard","no_shop":"standard","participating":"standard","protective_provisions":"low","redemption":"standard"}' "the clean sheet"
X scan "$TF/term-sheet-clean.txt"; eq "$CODE" 0 "clean text"; has "$OUT" "2 flags: 0 high, 0 medium, 2 low. 7 standard terms recognised." "clean counts"
has "$OUT" "Standard: " "standard line"; eq "$(tail -1 <<<"$OUT")" "The scanner reads words, not intent: a clause it does not know is not flagged. Read the whole document with a startup lawyer before you sign." "last line"
X scan "$TF/safe-side-letter.txt" --json; eq "$(J '[.flags[].key]' <<<"$OUT")" '["mfn"]' "the side letter: MFN only"
X scan "$TF/safe-side-letter.txt"; has "$OUT" "1 flag: 0 high, 0 medium, 1 low. 0 standard terms recognised." "singular flag"
has "$OUT" "LOW    Most favored nation (MFN)" "%-6s severity"; has "$OUT" '       Ask your lawyer: "Which later terms does the MFN reach, and does it end at the next priced round?"' "ask line"

# ---- scan --write (test_scan_cli_exit_codes_and_write)
X scan "$TF/term-sheet-tough.txt" --write --dir sd; eq "$CODE" 3 "write exit 3"
has "$OUT" "15 flags: 11 high, 3 medium, 1 low" "15 flags"; eq "$(tail -1 <<<"$OUT")" "Wrote sd/terms/scan-term-sheet-tough.md." "wrote md"
has "$(cat sd/terms/scan-term-sheet-tough.md)" "Ask your lawyer" "md has the asks"
eq "$(head -4 sd/terms/scan-term-sheet-tough.md)" "# Term sheet scan: term-sheet-tough.txt

$NOTE_L" "md head"
eq "$(grep -c '^## ' sd/terms/scan-term-sheet-tough.md)" 15 "one section per flag"
eq "$(tail -c1 sd/terms/scan-term-sheet-tough.md | od -An -c | tr -d ' ')" '\n' "the md ends with a newline (the last item is an empty line)"
X scan "$TF/term-sheet-clean.txt" --write --dir sd; has "$(cat sd/terms/scan-term-sheet-clean.md)" "## Standard terms

- " "standard terms section"
cp "$TF/safe-side-letter.txt" "My Term Sheet (v2).TXT"; X scan "My Term Sheet (v2).TXT" --write --dir sd; eq "$(tail -1 <<<"$OUT")" "Wrote sd/terms/scan-my-term-sheet-v2.md." "stem"
cp "$TF/safe-side-letter.txt" "___.txt"; X scan ___.txt --write --dir sd; eq "$(tail -1 <<<"$OUT")" "Wrote sd/terms/scan-document.md." "stem fallback"

# ---- scanner edges (test_edges, plus hand-worked cases)
S1() { printf '%s\n' "$1" > e.txt; X scan e.txt --json; R="$(J -r "[.flags[]|select(.key==\"$2\")|.severity+\"|\"+.clause]|join(\",\")" <<<"$OUT")"; }
S1 "No-Shop: forty-five (45) days." no_shop; eq "$R" "medium|No-shop of 45 days" "45 days"
printf 'Pro Rata: investors may participate in future rounds.\n' > e.txt; X scan e.txt --json; eq "$(J '.flags' <<<"$OUT")" "[]" "plain pro rata"
S1 "Liquidation Preference: 1.5x, participating, capped at 3x." participating; eq "$R" "medium|Participating preferred (capped)" "capped participation"
S1 "Liquidation Preference: 1.5x, participating, capped at 3x." liquidation_preference; eq "$R" "high|Liquidation preference 1.5x" "%g multiple"
S1 "Board: three directors, two designated by the holders of Common Stock and one designated by the Investors." board; eq "$R" "standard|Board: founders 2 of 3 seats" "founder board"
S1 "Board: five directors, three designated by the Investors, two designated by the Common." board; eq "$R" "high|Board control: investors 3 of 5 seats" "investor board"
S1 "Board: the board shall consist of three directors: one designated by the Investors, one designated by the Common, and one independent director." board
eq "$R" "medium|Board: investors 1, founders 1, independent 1 of 3" "balanced board"
S1 "Board: the Company will have a board of directors." board; eq "$R" "medium|Board composition" "seats not counted"
S1 "No-shop: sixty (60) days." no_shop; eq "$R" "high|No-shop of 60 days" "60 days"
S1 "No-shop: thirty days." no_shop; eq "$R" "standard|No-shop of 30 days" "30 days"
S1 "Exclusivity: the Company will not solicit other offers." no_shop; eq "$R" "medium|No-shop with no end date" "no end date"
S1 "This term sheet will expire in 48 hours." exploding; eq "$R" "high|Exploding deadline (48 hours)" "48 hours"
S1 "This term sheet will expire in 72 hours." exploding; eq "$R" "medium|Exploding deadline (72 hours)" "72 hours"
S1 "This offer will expire in two days." exploding; eq "$R" "high|Exploding deadline (48 hours)" "2 days = 48 hours"
S1 "This offer will expire in five days." exploding; eq "$R" "medium|Exploding deadline (5 days)" "5 days"
S1 "This offer will expire in ten (10) business days." exploding; eq "$R" "low|Exploding deadline (10 days)" "10 business days"
eq "$(J -r '.flags[0].note' <<<"$OUT")" "A deadline of 10 days: workable, but the clock runs while you compare offers and your lawyer reads it." "low note"
S1 "This offer must be signed by March 1." exploding; eq "$R" "medium|Exploding deadline (a fixed date)" "a fixed date"
S1 "Liquidation Preference: 2x the Original Purchase Price." liquidation_preference; eq "$R" "high|Liquidation preference 2x" "2x"
S1 "Liquidation Preference: two times (2x) the Original Purchase Price." liquidation_preference; eq "$R" "high|Liquidation preference 2x" "two times (2x)"
S1 "Liquidation Preference: one times the Original Purchase Price, non-participating." liquidation_preference; eq "$R" "standard|Liquidation preference 1x" "1x"
S1 "Liquidation Preference: the Original Purchase Price plus declared dividends." liquidation_preference; eq "$R" "standard|Liquidation preference 1x" "original purchase price = 1x"
S1 "Dividends: 8% per annum, cumulative." dividends; eq "$R" "high|Cumulative dividends" "cumulative"
S1 "Dividends: 8% non-cumulative, when, as and if declared." dividends; eq "$R" "standard|Non-cumulative dividends" "non-cumulative"
S1 "Anti-dilution: narrow-based weighted average." anti_dilution; eq "$R" "medium|Narrow-based weighted average anti-dilution" "narrow"
S1 "Redemption: none." redemption; eq "$R" "standard|No redemption rights" "no redemption"
S1 "Drag-along: holders of a majority of the Preferred may force a sale." drag_along; eq "$R" "high|Drag-along" "preferred-only drag"
S1 "Drag-along: approved by the Board and a majority of the Common." drag_along; eq "$R" "medium|Drag-along" "board drag"
S1 "Founder Vesting: founder shares will be subject to a new four-year vesting schedule." vesting_reset; eq "$R" "" "one optional word only: 'a new four-year vesting' does not match"
S1 "Founder Vesting: founder shares will be subject to a new vesting schedule." vesting_reset; eq "$R" "high|Founder vesting reset" "vesting reset"
S1 "Founder Vesting: founder shares will be subject to a new vesting schedule, with credit for time served." vesting_reset; eq "$R" "medium|Founder vesting reset" "with credit"
S1 "Pro Rata: each Investor may purchase two times its pro rata share." super_pro_rata; eq "$R" "medium|Super pro rata" "2x pro rata"
S1 "Option Pool: an option pool of 15% of the post-closing capitalization is included in the pre-money valuation." pool_shuffle; eq "$R" "medium|New option pool in the pre-money (15%)" "pool with size"
S1 "Protective Provisions: the consent of the Preferred is needed to approve the annual budget." protective_provisions; eq "$R" "high|Protective provisions with operating vetoes" "operating veto"
eq "$(J -r '.flags[0].note' <<<"$OUT")" "The investors' consent is needed for day-to-day decisions (budget), not just for selling the company or issuing new senior shares." "veto note"
S1 "Pay-to-play: applies to all Investors." pay_to_play; eq "$R" "medium|Pay-to-play" "pay to play"
printf 'Liquidation Preference: 2x.\r\nRedemption: none.\r\n' > crlf.txt; X scan crlf.txt --json; eq "$(J -c '[.flags[].key]' <<<"$OUT")" '["liquidation_preference","redemption"]' "CRLF clauses"
printf 'hello world\n' > none.txt; X scan none.txt; eq "$CODE" 0 "nothing found"; has "$OUT" "0 flags: 0 high, 0 medium, 0 low. 0 standard terms recognised." "zero counts"
has "$OUT" "No clause this scanner knows was found. That is not a clean bill of health: read it with a lawyer." "nothing-found line"
printf '  \n\n' > blank.txt; X scan blank.txt; eq "$CODE" 1 "blank"; eq "$ERR" "terms.py: blank.txt is empty" "empty file"
printf 'Liquidation Preference: 2x \xff\xfe.\n' > bad.txt; X scan bad.txt --json; eq "$CODE" 3 "invalid UTF-8 is replaced"; has "$OUT" '\ufffd\ufffd' "U+FFFD, ensure_ascii"
L=$(printf 'Terms: '; for i in $(seq 1 40); do printf 'word%02d ' "$i"; done; printf 'the shares are redeemable at the option of the holders after five years'; for i in $(seq 41 80); do printf ' word%02d' "$i"; done; printf '.')
printf '%s\n' "$L" > long.txt; X scan long.txt --json; EXC=$(J -r '.flags[0].excerpt' <<<"$OUT")
eq "${EXC:0:3}" "..." "a long sentence is cut: leading dots"; eq "${EXC: -3}" "..." "and trailing dots"; has "$EXC" "redeemable" "the window holds the trigger"

done_ "seed-terms/terms"

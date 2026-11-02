#!/usr/bin/env bash
# seed-updates/update: the assertions of seed-skill/tests/test_updates.py, run against the C++ tool. metrics.json is the
# Leashly pilot example (fictional) with seed-plan's example config; edge.json covers a raise landing, an explicit net
# burn, a gap month, a metric that starts late and a month where the cash grows.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
UF="$FIX/seed/seed-updates"; SKD="$UF/skill"
U() { run --skill-dir "$SKD" seed-updates/update "$@"; }
lines_has() { grep -qxF -- "$2" <<<"$1" && ok || bad "$3: line [$2] not in output"; }

# ---- growth, flat, pts, new: a crafted pair of months (the Math tests' change() and mom() cases)
mkdir -p "$TMP/m/seed"; cd "$TMP/m" || exit 1
cat > seed/metrics.json <<'EOF'
{"company": "Math Co", "headline": ["e", "a"],
 "metrics": [{"key": "a", "label": "A", "unit": "count"}, {"key": "b", "label": "B", "unit": "count"},
             {"key": "c", "label": "C", "unit": "usd"}, {"key": "d", "label": "D", "unit": "usd"},
             {"key": "e", "label": "E", "unit": "pct"}, {"key": "f", "label": "F", "unit": "usd"},
             {"key": "g", "label": "G", "unit": "pct"}, {"key": "h", "label": "H", "unit": "usd"}],
 "months": [{"month": "2026-12", "values": {"a": 210, "b": 100, "c": 0, "d": -100, "e": 2.8, "g": 4.5, "h": 0}},
            {"month": "2027-01", "values": {"a": 171, "b": 100, "c": 0, "d": -50, "e": 2.4, "f": 5, "g": 4.5, "h": 1500}}]}
EOF
U write --month 2027-01 --stdout; eq "$CODE" 0 "math write"
lines_has "$OUT" "- A: 171, down 18.6% from 210" "count down"
lines_has "$OUT" "- B: 100, flat on Dec 2026" "count flat, last month across the year"
lines_has "$OUT" "- C: \$0, flat on Dec 2026" "0 to 0 is flat"
lines_has "$OUT" "- D: -\$50, up 50% from -\$100" "a smaller loss is up"
lines_has "$OUT" "- E: 2.4%, down 0.4 pts from 2.8%" "pct pts"
lines_has "$OUT" "- F: \$5, new this month" "no last value"
lines_has "$OUT" "- G: 4.5%, flat on Dec 2026" "pct flat"
lines_has "$OUT" "- H: \$1,500, new this month from \$0" "up from 0 is new"
lines_has "$OUT" "**TL;DR:** E 2.4% (-0.4 pts MoM), a 171 (-18.6%)." "headline order, mom, lower-case label"
for b in 2026-13 2026-1 "Nov 2026" 2026/12; do U write --month "$b"; eq "$CODE" 2 "month refused [$b]"; has "$ERR" "--month wants YYYY-MM, like 2026-11 (got '$b')" "month message [$b]"; done
U write --month "٢٠٢٧-٠١" --stdout; eq "$CODE" 2 "Unicode-digit month parses, then is not in the file"; has "$ERR" "no ٢٠٢٧-٠١ in metrics.json" "Unicode month message"

# ---- the reel: runway and the two updates
mkdir -p "$TMP/r/seed"; cd "$TMP/r" || exit 1; cp "$UF/metrics.json" "$UF/config.json" seed/
U write --month 2026-11 --stdout; eq "$CODE" 0 "reel investor"; eq "$ERR" "" "a full month: nothing to flag"
eq "$(head -1 <<<"$OUT")" "Subject: Leashly investor update: November 2026" "subject"
for l in "**TL;DR:** Revenue \$6,600 (+23.9% MoM), walks completed 1,760 (+23.9%), active owners 210 (+22.8%). Runway 9.8 months." \
         "- Revenue: \$6,600, up 23.9% from \$5,325" "- GMV (what owners paid): \$44,000, up 23.9% from \$35,500" \
         "- Walks saved by a backup walker: 31, up 19.2% from 26" "- Walker cancellations: 2.4%, down 0.4 pts from 2.8%" \
         "- Walker cancellations are down to 2.4%, still above our 2% goal." \
         "1. Intros to property managers of dog-friendly apartment buildings in Austin." \
         "Cash at the end of November: \$177,800. Net burn this month: \$18,700. Average net burn over the last 2 months: \$18,100 a month, so the cash lasts about 9.8 months, to around Sep 2027." \
         "Your Name" "Founder, Leashly"; do lines_has "$OUT" "$l" "reel line"; done
prev=-1; okorder=1; for h in "**TL;DR:**" "**The numbers" "**Highlights**" "**Lowlights**" "**Asks**" "**Runway**"; do
  n=$(grep -nF -- "$h" <<<"$OUT" | head -1 | cut -d: -f1); [ -n "$n" ] && [ "$n" -gt "$prev" ] || okorder=0; prev=${n:-0}; done
eq "$okorder" 1 "sections in reading order"
U write --month 2026-10 --stdout; has "$OUT" "Net burn this month: \$17,500." "October burn (214,000 - 196,500)"
has "$OUT" "over the last month: \$17,500 a month" "one month of burn"
U write --month 2026-09 --stdout; has "$OUT" "there is no runway figure yet" "no August to compare with"
U write --month 2026-11 --to prospects --stdout
eq "$(sed -n 1p <<<"$OUT")" "Subject: Leashly: November 2026 update" "prospect subject"; eq "$(sed -n 3p <<<"$OUT")" "Hi {first_name}," "prospect greeting"
lines_has "$OUT" "**November 2026:** Revenue \$6,600 (+23.9% MoM), walks completed 1,760 (+23.9%), active owners 210 (+22.8%)." "prospect tldr"
lines_has "$OUT" "We are raising a \$1.5M seed (post-money SAFE). If the timing is better now, would a 20-minute call be useful?" "prospect raise"
lines_has "$OUT" "Not a fit any more? Reply 'no' and I won't send these again." "opt-out"
eq "$(grep -c '^- ' <<<"$OUT")" "2" "the top two highlights"
for never in "\$177,800" Runway runway burn Lowlights "Card fees" Asks; do hasnt "$OUT" "$never" "prospect leaves out $never"; done
eq "$ERR" "Note: Replace {first_name} for each investor before it goes out." "prospect note on stderr"

# ---- edge.json
mkdir -p "$TMP/e/seed"; cd "$TMP/e" || exit 1; cp "$UF/edge.json" seed/metrics.json
U write --month 2027-02 --stdout
has "$OUT" "- Revenue: \$1,500, new this month" "up from \$0"; has "$OUT" "- NPS: 41, new this month" "late metric"
has "$OUT" "- Churn: 4.5%, down 0.5 pts from 5%" "churn pts"; has "$OUT" "{your_name}" "no founder"; has "$OUT" "Edge Co" "company from metrics"
has "$ERR" "Note: No lowlights for 2027-02" "lowlights flagged"; has "$ERR" "No founder name" "founder flagged"
has "$OUT" "Net burn this month: \$6,000." "44,000 + 500,000 - 538,000"
U write --month 2027-01 --stdout; has "$OUT" "Net burn this month: \$6,000." "January burn"
U write --month 2027-03 --stdout; has "$OUT" "- Churn: 4.5%, flat on Feb 2027" "flat churn"
has "$OUT" "Net burn this month: \$7,500." "the explicit net_burn wins"
has "$OUT" "Average net burn over the last 3 months: \$6,500 a month, so the cash lasts about 81.7 months, to around Jan 2034." "edge runway"
U write --month 2027-05 --stdout; hasnt "$OUT" "NPS" "no NPS in May"; has "$OUT" "there is no runway figure yet" "April is missing"
U write --month 2027-06 --stdout; has "$OUT" "**TL;DR:** Cash went up in June." "own tldr"
has "$OUT" "Cash grew \$3,000 this month." "cash grew"; has "$OUT" "the cash is not running down" "no burn on average"
eq "$(grep -v founder <<<"$ERR" | grep -c 'Note: No ')" "0" "June has nothing else to flag"
U write --month 2027-06 --to prospects --stdout
has "$OUT" "We are raising our seed round." "no raise"; has "$OUT" "**June 2027:** Revenue \$2,600 (+30% MoM), users 230 (+15%)." "edge prospect tldr"

# ---- refusals
cd "$TMP/r" || exit 1
U write --month 2026-12; eq "$CODE" 2 "missing month"; has "$ERR" "no 2026-12 in metrics.json (it has 2026-09, 2026-10, 2026-11)" "missing month message"
mkdir -p "$TMP/b/seed"; jq '.months += [.months[0]] | .metrics[0].unit = "dollars" | .months[1].values.mood = 7 | .months[2].cash = "lots"' "$UF/metrics.json" > "$TMP/b/seed/metrics.json"
run seed-updates/update --dir "$TMP/b/seed" write --month 2026-11; eq "$CODE" 2 "invalid file"
for why in "months has 2026-09 twice" "unit is usd, count or pct" "'mood' is not one of the metrics" "2026-11 cash is not a number"; do has "$ERR" "$why" "validate: $why"; done
has "$ERR" "update.py: metrics.json needs fixing:" "validate header"
echo '{"months": []}' > "$TMP/b/seed/metrics.json"; run seed-updates/update --dir "$TMP/b/seed" write; eq "$CODE" 2 "no metrics"; has "$ERR" "metrics is missing" "no metrics message"
printf '{"x":' > "$TMP/b/seed/metrics.json"; run seed-updates/update --dir "$TMP/b/seed" write; eq "$CODE" 2 "bad JSON"; has "$ERR" "is not valid JSON: Expecting value: line 1 column 6 (char 5)" "bad JSON message"

# ---- the CLI: init, write, add, write --stdout (in a fresh folder, like the Python test's chdir)
mkdir -p "$TMP/c"; cd "$TMP/c" || exit 1
U init; eq "$CODE" 0 "init"; has "$OUT" "Wrote seed/metrics.json (the Leashly example" "init message"
eq "$(jq -S -c . seed/metrics.json)" "$(jq -S -c . "$SKD/metrics.example.json")" "init copies the example"
U init; eq "$CODE" 1 "init again"; has "$ERR" "already exists" "init refuses"
U init --force; eq "$CODE" 0 "init --force"
cp "$UF/config.json" seed/config.json
U write --month 2026-11; eq "$CODE" 0 "write"
has "$OUT" "Wrote seed/updates/2026-11.md (investor update for November 2026)." "write message"; has "$OUT" "Nothing was sent." "nothing sent"
cp seed/metrics.json "$TMP/c-metrics.json"
run --skill-dir "$SKD" seed-updates/update --dir seed write --month 2026-11 --stdout > /dev/null
$T --skill-dir "$SKD" seed-updates/update write --month 2026-11 --stdout > "$TMP/expected.md" 2>/dev/null
cmp -s "$TMP/expected.md" seed/updates/2026-11.md && ok || bad "the file is the --stdout text"
eq "$(tail -c 1 seed/updates/2026-11.md | od -An -c | tr -d ' ')" '\n' "file ends with the empty last line"
U write --month 2026-11 --to prospects; [ -f seed/updates/2026-11-prospects.md ] && ok || bad "prospects file"; has "$OUT" "Note: Replace {first_name}" "prospect note on stdout"
U add --month 2026-12 --set revenue=7,875 --set walks=2100 --cash 1650000 --money-in 1500000 --highlight "Closed the seed." --lowlight "Hiring is slow." --ask "Engineers."
eq "$CODE" 0 "add"; has "$OUT" "Saved 2026-12 in seed/metrics.json: 2 values, cash \$1,650,000." "add message"
has "$OUT" "Not set yet for 2026-12: active_owners, gmv, backup_saves, cancel_rate." "missing list"
eq "$(jq -c '.months[-1].values' seed/metrics.json)" '{"revenue":7875,"walks":2100}' "values saved"
eq "$(jq -c '.months[-1]|[.cash,.money_in]' seed/metrics.json)" "[1650000,1500000]" "cash saved"
U add --month 2026-12 --set gmv=52500 --highlight "Second pilot city signed."
eq "$(jq '.months[-1].values.gmv' seed/metrics.json)" "52500" "gmv merged"
eq "$(jq -c '.months[-1].highlights' seed/metrics.json)" '["Closed the seed.","Second pilot city signed."]' "highlights appended"
U add --month 2026-08 --set revenue=3000 --cash 230000
eq "$(jq -c '[.months[].month]' seed/metrics.json)" '["2026-08","2026-09","2026-10","2026-11","2026-12"]' "months sorted"
U write --stdout; eq "$CODE" 0 "write latest"
eq "$(head -1 <<<"$OUT")" "Subject: Leashly investor update: December 2026" "latest month"
has "$OUT" "Net burn this month: \$27,800." "177,800 + 1,500,000 - 1,650,000"
has "$OUT" "Average net burn over the last 3 months: \$21,333 a month" "three-month average"
[ -e seed/updates/2026-12.md ] && bad "--stdout wrote a file" || ok
eq "$ERR" "" "a full month: nothing on stderr"
U write --stdout --to prospects; eq "$(head -1 <<<"$OUT")" "Subject: Leashly: December 2026 update" "latest prospect"
eq "$ERR" "Note: Replace {first_name} for each investor before it goes out." "only the replace note"
U add --month 2026-12 --tldr "  Café opened.  " --set revenue=1.5; eq "$(jq -r '.months[-1].tldr' seed/metrics.json)" "Café opened." "tldr stripped"
grep -qF 'Caf\u00e9' seed/metrics.json && ok || bad "metrics.json keeps ensure_ascii on"
eq "$(jq '.months[-1].values.revenue' seed/metrics.json)" "1.5" "a decimal stays a float"
mkdir -p other; run --skill-dir "$SKD" seed-updates/update init --dir other; eq "$CODE" 0 "--dir after the command"; [ -f other/metrics.json ] && ok || bad "--dir after the command used"
run --skill-dir "$SKD" seed-updates/update --dir other2 init; [ -f other2/metrics.json ] && ok || bad "--dir before the command used"

# ---- exit codes
mkdir -p "$TMP/x"; cd "$TMP/x" || exit 1
U write; eq "$CODE" 2 "write before init"; has "$ERR" "run update.py init first" "init first message"
U init
while IFS='|' read -r why args; do
  eval "set -- $args"; U "$@"; eq "$CODE" 2 "exit 2: $args"; has "$ERR" "$why" "message: $args"; hasnt "$ERR" "Traceback" "no traceback: $args"
done <<'EOF'
no 2027-02|write --month 2027-02
--month wants YYYY-MM|write --month Nov
'mood' is not one of your metrics|add --month 2026-12 --set mood=7
--set wants key=value|add --month 2026-12 --set revenue
not a number|add --month 2026-12 --set revenue=lots
--month wants YYYY-MM|add --month 2026/12
EOF
eq "$(jq '.months|length' seed/metrics.json)" "3" "nothing half-written"
U; eq "$CODE" 2 "no command"; has "$OUT" "usage: update.py" "help"
U add --month 2026-12 --set; eq "$CODE" 2 "argparse error"

done_ "seed-updates/update"

#!/usr/bin/env bash
# seed-dataroom/dataroom: the assertions of seed-skill/tests/test_dataroom.py, run against the C++ tool.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
ROOMF="$FIX/seed/seed-dataroom/room.json"; TODAY=$(jq -r .today "$ROOMF")
R() { run seed-dataroom/dataroom "$@"; }
build() {   # init, then every fixture file with its size and a modified time of noon UTC that day
  local d="$1"; R init --dir "$d"; eq "$CODE" 0 "init for build"
  jq -r '.files[] | "\(.path)\t\(.bytes)\t\(.modified)"' "$ROOMF" | while IFS=$'\t' read -r p n m; do
    mkdir -p "$d/dataroom/$(dirname "$p")"; head -c "$n" /dev/zero | tr '\0' x > "$d/dataroom/$p"; touch -d "${m}T12:00:00Z" "$d/dataroom/$p"
  done
}

# ---- init makes the nine folders and is safe to re-run
D1="$TMP/a"; mkdir -p "$D1"; R init --dir "$D1"; eq "$CODE" 0 "init exit"
eq "$(ls "$D1/dataroom" | sort | tr '\n' '|')" "01 Company|02 Pitch deck|03 Financials|04 Market|05 Product|06 Team|07 Legal|08 Cap table|09 Customers|" "nine folders"
has "$OUT" "created 9 folders" "created count"; has "$OUT" "  02 Pitch deck  Pitch deck (PDF)" "folder line"
echo mine > "$D1/dataroom/02 Pitch deck/deck.pdf"; R init --dir "$D1"; has "$OUT" "nothing changed" "re-run"; [ -f "$D1/dataroom/02 Pitch deck/deck.pdf" ] && ok || bad "kept the file"

# ---- check: present, missing, stale
D2="$TMP/b"; mkdir -p "$D2"; build "$D2"
R check --dir "$D2" --today "$TODAY" --json; eq "$CODE" 1 "check exit 1"; J="$OUT"
st() { echo "$J" | jq -r --arg k "$1" '.items[] | select(.key==$k) | .state'; }
eq "$(echo "$J" | jq '.items | length')" 25 "25 items"
eq "$(st deck)" present "deck"; eq "$(st metrics)" stale "metrics stale by name date"; eq "$(st cap_table)" stale "cap table stale by mtime"
eq "$(st incorporation)" present "legal never stale"; eq "$(st roadmap)" missing "roadmap"
eq "$(echo "$J" | jq -r '.items[] | select(.key=="deck") | .files[0] | "\(.date) \(.dated_by)"')" "2026-10-24 name" "deck dated by name"
eq "$(echo "$J" | jq -r '.items[] | select(.key=="cap_table") | .files[0] | "\(.date) \(.dated_by) \(.age_days)"')" "2026-08-01 modified 86" "cap table by mtime"
eq "$(echo "$J" | jq -c .loose)" '["notes from the Northlight call.txt"]' "loose"
eq "$(echo "$J" | jq -c '[.flagged[].file]')" '["09 Customers/customer list export.csv"]' "flagged"
eq "$(echo "$J" | jq -c '[.other[].file]')" '["05 Product/Brand colours.pdf"]' "other"
eq "$(echo "$J" | jq -c 'keys_unsorted')" '["root","today","items","other","loose","flagged","empty_folders","files","ok"]' "key order"
eq "$(echo "$J" | jq -c '[.files, .ok, .empty_folders]')" '[17,false,[]]' "files, ok, empty"
eq "$(echo "$J" | jq -c '.items[] | select(.key=="safes") | [.max_age_days, .state]')" '[null,"present"]' "no limit is null"

# ---- the before stage, as text
R check --dir "$D2" --today "$TODAY" --stage before; eq "$CODE" 1 "before exit"
has "$OUT" "Your startup data room, 2026-10-26: 9 of 12 items present (before a term sheet)." "headline"
has "$OUT" "DO NOT SHARE: 09 Customers/customer list export.csv looks like raw customer personal data. Take it out of the data room." "do not share"
has "$OUT" "3 to fix: Traction and KPIs stale; Product roadmap missing; Testimonials and reviews missing." "to fix"
has "$OUT" "  STALE   Traction and KPIs [before]  Traction metrics 2026-06.pdf (2026-06-01, 147 days old, limit 45)" "stale line"
has "$OUT" "  ok      Pitch deck (PDF) [before]  Leashly pitch deck 2026-10-24.pdf (2026-10-24, 2 days old)" "ok line"
has "$OUT" "  LOOSE: notes from the Northlight call.txt is outside the nine folders: move it into one" "loose line"
has "$OUT" "  other: 05 Product/Brand colours.pdf (not on the checklist; index describes it as 'other')" "other line"
hasnt "$OUT" "Cap table (fully diluted)" "after items hidden"

# ---- a complete, fresh stage passes
D3="$TMP/c"; mkdir -p "$D3"; R init --dir "$D3"
for pair in "01 Company|one-pager" "01 Company|business plan" "02 Pitch deck|deck" "03 Financials|model" "03 Financials|use of funds" "04 Market|market" "04 Market|compet" "05 Product|demo" "05 Product|metric" "05 Product|roadmap" "06 Team|bio" "09 Customers|testimonial"; do
  : > "$D3/dataroom/${pair%%|*}/${pair#*|} $TODAY.pdf"
done
R check --dir "$D3" --today "$TODAY" --stage before --json; eq "$CODE" 0 "before complete passes"
R check --dir "$D3" --today "$TODAY" --stage after --json; eq "$CODE" 1 "after still missing"
R check --dir "$D3" --today "$TODAY" --stage before; has "$OUT" "Everything on the list for this stage is there and fresh." "fresh text"

# ---- dates in names (a real date wins, an impossible one falls back to year-month)
D4="$TMP/d"; mkdir -p "$D4"; R init --dir "$D4"
for n in "Cap table 2026-10-01.xlsx" "metrics_20261015.pdf" "Metrics 2026-06.pdf" "Plan 2026-02-30.pdf"; do : > "$D4/dataroom/01 Company/$n"; done
touch -d "2026-10-20T12:00:00Z" "$D4/dataroom/01 Company/"*
R check --dir "$D4" --today "$TODAY" --json
eq "$(echo "$OUT" | jq -r '[.items[].files[], .other[]] | map("\(.file)=\(.date)") | unique | join(",")')" "01 Company/Cap table 2026-10-01.xlsx=2026-10-01,01 Company/Metrics 2026-06.pdf=2026-06-01,01 Company/Plan 2026-02-30.pdf=2026-02-01,01 Company/metrics_20261015.pdf=2026-10-15" "name dates"

# ---- never share
D5="$TMP/e"; mkdir -p "$D5"; R init --dir "$D5"
for n in "passwords.txt" "prod.env" "Casey passport scan.pdf" "api key.txt" "customer emails.csv" "Pitch deck.pdf" "Waitlist counts by city.csv" "Cap table.xlsx"; do : > "$D5/dataroom/07 Legal/$n"; done
R check --dir "$D5" --today "$TODAY" --json
eq "$(echo "$OUT" | jq -r '[.flagged[] | "\(.file | split("/")[1])=\(.why)"] | join(",")')" "Casey passport scan.pdf=a personal ID document,api key.txt=a key or secret,customer emails.csv=raw customer personal data,passwords.txt=a password or login,prod.env=a key or secret" "flagged names"

# ---- index
echo '{"startup": {"name": "Leashly"}}' > "$D2/config.json"
R index --dir "$D2" --today "$TODAY"; eq "$CODE" 0 "index exit"
has "$OUT" "16 files in 9 folders, 11 checklist items missing, 2 out of date" "index summary"
has "$OUT" "  DO NOT SHARE: 09 Customers/customer list export.csv looks like raw customer personal data; it is not in the index." "index flag"
MD=$(cat "$D2/dataroom/INDEX.md")
[[ "$MD" == "# Leashly data room"$'\n'* ]] && ok || bad "index title"
has "$MD" "| Leashly pitch deck 2026-10-24.pdf | Pitch deck (PDF) | 2026-10-24 | 5.0 KB | before |" "deck row"
has "$MD" "| Cap table.xlsx | Cap table (fully diluted) | 2026-08-01 | 900 B | after |" "cap row"
has "$MD" "| Brand colours.pdf | Other | 2026-10-01 | 300 B | ask first |" "other row"
hasnt "$MD" "customer list export" "flagged left out"; has "$MD" "Missing: Product roadmap." "missing"; has "$MD" "Out of date: Traction and KPIs." "out of date"
has "$MD" "## Not in a folder"$'\n\n'"- notes from the Northlight call.txt" "loose section"
has "$MD" "Left out of this index on purpose: 1 file that should not be in a data room." "left out"
has "$MD" "| Leashly business plan.md | Business plan | 2026-10-25 | 8.9 KB | before |" "size rounding"
R index --dir "$D2" --today "$TODAY"; eq "$(cat "$D2/dataroom/INDEX.md")" "$MD" "index is stable (does not index itself)"

# ---- share list
R share-list --dir "$D2" --today "$TODAY" --json; J="$OUT"
eq "$(echo "$J" | jq -r '.before.share[] | select(.label=="Pitch deck (PDF)") | .files[0]')" "02 Pitch deck/Leashly pitch deck 2026-10-24.pdf" "deck shared before"
eq "$(echo "$J" | jq -r '.before.share[] | select(.label=="Traction and KPIs") | .state')" stale "stale in share"
eq "$(echo "$J" | jq '[.before.share[].label] | index("Cap table (fully diluted)")')" null "cap table not before"
eq "$(echo "$J" | jq '[.after.share[].label] | index("Cap table (fully diluted)") != null')" true "cap table after"
eq "$(echo "$J" | jq -c .before.missing)" '["Product roadmap","Testimonials and reviews"]' "before missing"
eq "$(echo "$J" | jq -c 'keys_unsorted')" '["before","after","never"]' "share keys"
R share-list --dir "$D2" --today "$TODAY" --stage before
has "$OUT" "This tool shares nothing" "shares nothing"; hasnt "$OUT" "AFTER a signed term sheet" "no after"
has "$OUT" "  Traction and KPIs (STALE: refresh it first)"$'\n'"      05 Product/Traction metrics 2026-06.pdf" "stale share line"
has "$OUT" "  missing: Product roadmap" "missing line"

# ---- errors: exit 1, a plain message, never a traceback
R check --dir "$TMP/none"; eq "$CODE" 1 "no room"; eq "$ERR" "dataroom.py: no data room at $TMP/none/dataroom: run dataroom.py init first" "no room message"
R check --dir "$D2" --today 10/26/2026; eq "$CODE" 1 "bad date"; eq "$ERR" "dataroom.py: --today wants a date like 2026-10-26, got '10/26/2026'" "bad date message"
R check --dir "$D2" --today 2026-02-30; eq "$CODE" 1 "unreal date"; eq "$ERR" "dataroom.py: --today: 2026-02-30 is not a real date" "unreal date message"
R; eq "$CODE" 1 "no command"; has "$OUT" "usage: dataroom.py" "help printed"; has "$OUT" "share-list" "help lists commands"
R check --dir "$D2" --stage soon; eq "$CODE" 2 "bad choice is argparse exit 2"
done_ "seed-dataroom/dataroom"

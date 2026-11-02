#!/usr/bin/env bash
# forecaster/forecast: the assertions of closer-skill/forecaster/test_forecast.py, run against the C++ tool.
# Fixture: the skill's pipeline.example.json, unmodified; the 12 extra closed deals are synthetic.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
F() { run forecaster/forecast "$@"; }
F --file "$EX" month --month 2026-11 --today 2026-10-29; eq "$CODE" 0 "month"
has "$OUT" "  Commit:      \$4,788  (contracts out)" "commit"; has "$OUT" "  Likely:      \$6,474  (weighted)" "likely"
has "$OUT" "  Best case:   \$16,864  (everything closing this month)" "best"
eq "$(echo "$OUT" | sed -n 6p)" '    Website + Google Ads, monthly  contract-sent    $4,788 x  80% =    $3,830  close 2026-11-06' "harbor first, row layout"
has "$OUT" "  Based on: standard stage chances (fewer than 10 closed deals of your own)." "basis"
F --file "$EX" month --month 2026-10 --today 2026-10-29; has "$OUT" "  Won so far:  \$2,500" "October counts the win"; eq "$(echo "$OUT" | wc -l)" 6 "no rows"
F --file "$EX" month --today 2026-11-02; has "$OUT" "2026-11 forecast" "the month of --today"
F --file "$EX" rates; eq "$OUT" $'Won 1, lost 1: 50% win rate. Too few to trust yet.\nA won deal takes 24 days on average.' "unsubscribes are not lost sales"
jq '.contacts += [range(12) | {id: "c\(.)", name: "C \(.)"}] | .deals += [range(12) | {id: "d\(.)", contact_id: "c\(.)", title: "x", value: 100, stage: (if . < 6 then "won" else "lost" end), lost_reason: "budget"}]' "$EX" > h.json
F --file h.json rates; has "$OUT" "Won 7, lost 7: 50% win rate." "enough history"; hasnt "$OUT" "Too few" "trusted"
F --file h.json month --month 2026-11 --today 2026-10-29; has "$OUT" "scaled to your win rate of 50% over 14 closed deals" "scaled"
has "$OUT" "contract-sent    \$4,788 x  95% =" "capped at 95%"; has "$OUT" "call-booked      \$4,788 x  60% =" "doubled"
F --file "$EX" health --today 2026-10-29; eq "$OUT" "Every open deal has a close date in the future and recent activity." "healthy on the 29th"
F --file "$EX" health --today 2026-11-10
has "$OUT" "harbor-lane-website      close date 2026-11-06 has passed: move it or close the deal" "passed"; has "$OUT" "pine-street-website      quiet for 20 days" "quiet"
jq '(.deals[] | select(.id=="juniper-booking") | .close_date) = null' "$EX" > n.json; F --file n.json health --today 2026-10-29
eq "$OUT" "juniper-booking          no close date, so it is in no month's forecast" "no close date"
F --file "$EX" month --month 2026-13; eq "$CODE" 1 "bad month"; eq "$ERR" "month must be in 1..12" "the ValueError, caught"
F --file "$EX" month --month nov; eq "$ERR" "not enough values to unpack (expected 2, got 1)" "unpack"
F --file "$EX" month --today 2026/10/29; eq "$ERR" "Invalid isoformat string: '2026/10/29'" "bad --today"
F --file "$EX" month --month 2026-12 --today 2026-10-29; has "$OUT" "2026-12 forecast" "December bounds"
done_ "closer_forecast"

#!/usr/bin/env bash
# follow-up/followup: the assertions of closer-skill/follow-up/test_followup.py, run against the C++ tool.
# Fixtures: the skill's pipeline.example.json and templates.json, unmodified. dana_before() is the test's helper:
# the board with Dana's thread as it stood before a cut, and her deal back at "contacted".
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"; SK="$FIX/closer"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
FU() { run --skill-dir "$SK" follow-up/followup "$@"; }
dana_before() { jq --arg cut "$1" '.activities |= map(select(.contact_id != "dana-harbor-lane" or .date < $cut)) | (.deals[] | select(.id=="harbor-lane-website") | .stage) = "contacted"' "$EX" > d.json; }
dana() { echo "$OUT" | grep "Dana Ortiz"; }
section() { echo "$OUT" | awk -v t="$1" '$0==t{f=1;next} /^$/{f=0} f'; }

# ---- the cadence: Mon Oct 19 opening, then Wed 21, Fri 23, Tue 27
dana_before 2026-10-20; FU --file d.json due --today 2026-10-21; has "$(section 'DUE NOW')" "Dana Ortiz (Harbor Lane Dental): follow-up 1 of 3" "follow-up 1 on the 21st"
FU --file d.json due --today 2026-10-20; has "$(section WAITING)" "Dana Ortiz (Harbor Lane Dental): follow-up 1 of 3 is due 2026-10-21" "waiting before"
dana_before 2026-10-22; FU --file d.json due --today 2026-10-23; has "$(section 'DUE NOW')" "Dana Ortiz (Harbor Lane Dental): follow-up 2 of 3" "follow-up 2 on the 23rd"
dana_before 2026-10-24; FU --file d.json due --today 2026-10-27; has "$(section 'DUE NOW')" "Dana Ortiz (Harbor Lane Dental): follow-up 3 of 3" "follow-up 3 on Tue 27 (4 days)"
FU --file d.json due --today 2026-10-26; has "$(section WAITING)" "follow-up 3 of 3 is due 2026-10-27" "waiting on the 26th"
jq '.activities |= map(if .id == "a14" then .date = "2026-10-20T09:30" elif .id == "a17" then .date = "2026-10-20T10:00" else . end)' d.json > w.json; FU --file w.json due --today 2026-10-21; has "$OUT" "follow-up 3 of 3 is due 2026-10-26" "a Saturday moves to Monday"

# ---- stop rules
dana_before 2026-10-27T10; FU --file d.json due --today 2026-11-03; has "$(section STOPPED)" "Dana Ortiz (Harbor Lane Dental): 3 follow-ups sent, no reply: that is the limit" "three then stop"
dana_before 2026-10-27T12; FU --file d.json due --today 2026-10-27; has "$(section 'YOUR TURN (they replied)')" "Dana Ortiz (Harbor Lane Dental): they replied 2026-10-27: Sorry, crazy week! Thursday at 2 works." "a reply is your turn"
dana_before 2026-10-27T13; FU --file d.json due --today 2026-10-28; has "$(section BOOKED)" "Dana Ortiz (Harbor Lane Dental): call booked: no more follow-ups" "a booking stops it"
FU --file d.json draft --contact dana-harbor-lane --today 2026-10-28; eq "$CODE" 1 "no draft once booked"; eq "$ERR" "No draft: Dana Ortiz already booked a call." "message"
dana_before 2026-10-20; FU --file d.json due --today 2026-11-19; has "$(section STOPPED)" "Dana Ortiz (Harbor Lane Dental): 30 days since the opening with no reply" "thirty days of silence"

dana_before 2026-10-22; FU --file d.json reply --contact dana-harbor-lane --text "Thanks, but we're all set." --at 2026-10-22T10:00
eq "$OUT" "Dana Ortiz said no. Every follow-up to them is stopped." "a no"; eq "$(jq -r '.deals[0].stage, .deals[0].lost_reason' d.json | paste -sd'|')" "lost|said no: Thanks, but we're all set." "deal lost"
FU --file d.json due --today 2026-10-23; has "$(section STOPPED)" "Dana Ortiz (Harbor Lane Dental): they said no on 2026-10-22" "stopped"
echo "Bumping this." > t.txt; FU --file d.json record --contact dana-harbor-lane --text-file t.txt --approval yes --at 2026-10-23T09:00
eq "$CODE" 1 "nothing goes out after a no"; has "$OUT" "Not recorded: a stop rule applies (stopped: they said no on 2026-10-22)." "why"
run pipeline/deals --file d.json validate; eq "$CODE" 0 "still valid"

dana_before 2026-10-22; FU --file d.json reply --contact dana-harbor-lane --text "Please remove me from your list." --at 2026-10-22T10:00
eq "$OUT" "Dana Ortiz asked to be removed. Stopped forever and added to suppression." "unsubscribe"
eq "$(jq -c '.suppression' d.json)" '["elena@mesaverdemedspa.com","dana@harborlanedental.com"]' "added to the suppression list"
eq "$(jq -c '.contacts[0] | [.do_not_contact, .notes]' d.json)" '[true,"DO NOT CONTACT since 2026-10-22: Please remove me from your list."]' "do not contact, with the note"
eq "$(jq -r '.deals[0].lost_reason' d.json)" "unsubscribed" "lost as unsubscribed"
FU --file d.json due --today 2030-01-01; has "$(section STOPPED)" "Dana Ortiz (Harbor Lane Dental): do not contact" "forever"

FU --file "$EX" due --today 2026-10-29
has "$(section 'DUE NOW')" "Marcus Lee (Pine Street Fitness)" "marcus due"; has "$(section 'YOUR TURN (they replied)')" "Priya Shah (Bluebird Pet Spa)" "priya your turn"
has "$(section BOOKED)" "Dana Ortiz" "dana booked"; has "$(section STOPPED)" "Elena" "elena stopped"; has "$(section STOPPED)" "Tom Becker" "tom stopped"
eq "$(echo "$OUT" | grep -E '^[A-Z]' | paste -sd'|')" "DUE NOW|YOUR TURN (they replied)|BOOKED|NOT STARTED|STOPPED" "group order"

# ---- drafts
dana_before 2026-10-20; FU --file d.json draft --contact dana-harbor-lane --today 2026-10-21
eq "$OUT" $'To: dana@harborlanedental.com\nSubject: Re: Quick idea for Harbor Lane Dental\n\nHi Dana, just bumping this up in case it got buried.\n\nYou' "follow-up 1 is the bump, same thread"
dana_before 2026-10-22; FU --file d.json draft --contact dana-harbor-lane --today 2026-10-23
has "$OUT" "Hi Dana, [fill: hook]" "placeholder"; has "$OUT" $'\n\nFill before it can go: hook' "needs a hook"
dana_before 2026-10-24; FU --file d.json draft --contact dana-harbor-lane --today 2026-10-27 --hook Thursday; has "$OUT" "is Thursday good for a quick call? Either answer is fine, and this is my last note." "3 says last"
FU --file d.json draft --contact dana-harbor-lane --today 2026-10-27 --template nope; eq "$ERR" "No template nope. Try: followup-1, followup-2, followup-3, confirm-time" "unknown template"
jq '.activities |= map(select(.contact_id != "dana-harbor-lane")) | .deals[0].stage = "contacted"' "$EX" > n.json; FU --file n.json draft --contact dana-harbor-lane --today 2026-10-27
eq "$ERR" "Nothing to follow up with Dana Ortiz yet (no opening email yet (/prospector sends it))." "not started"

# ---- record needs a yes, and no placeholders
dana_before 2026-10-20
FU --file d.json record --contact dana-harbor-lane --text Bump --approval ok --at 2026-10-21T09:00; eq "$CODE" 1 "ok is not a yes"; eq "$OUT" "Not recorded: only an explicit 'yes' sends a message (got 'ok')." "why"
FU --file d.json record --contact dana-harbor-lane --text "Hi [fill: hook]" --approval yes --at 2026-10-21T09:00; eq "$OUT" "Not recorded: the message still has a [fill: ...] in it." "placeholder"
FU --file d.json record --contact dana-harbor-lane --text Bump --approval "yes to all" --at 2026-10-21T09:00; eq "$OUT" "Not recorded: one yes per message: say yes to each one on its own." "batch"
FU --file d.json record --contact dana-harbor-lane --text Bump --approval "yes but tomorrow" --at 2026-10-21T09:00; eq "$OUT" "Not recorded: that yes has a condition in it; ask again." "condition"
FU --file d.json record --contact dana-harbor-lane --text Bump --approval "yes?" --at 2026-10-21T09:00; eq "$OUT" "Not recorded: that is a question, not a yes." "question"
FU --file d.json record --contact dana-harbor-lane --text Bump --approval "Yes, send it" --today 2026-10-21; eq "$CODE" 0 "recorded"; eq "$OUT" "Follow-up 1 of 3 logged. Next is due 2026-10-23." "next due"
eq "$(jq -c '.activities[-1] | [.date, .deal_id, .type, .direction, .subject, .text, .approval]' d.json)" '["2026-10-21T09:00","harbor-lane-website","followup","out","Re: Quick idea for Harbor Lane Dental","Bump","Yes, send it"]' "the activity (--today means 09:00)"
dana_before 2026-10-24; FU --file d.json record --contact dana-harbor-lane --text Last --approval y --at 2026-10-27T09:00; eq "$OUT" "Follow-up 3 of 3 logged. That was the last one." "the last one"
FU --file d.json record --contact dana-harbor-lane --approval yes; eq "$ERR" "Give --text or --text-file: exactly what went out." "no text"

# ---- replies (test_cli_due_and_reply)
dana_before 2026-10-24; FU --file d.json reply --contact dana-harbor-lane --text "Sorry, crazy week! Thursday at 2 works." --at 2026-10-27T11:48
eq "$CODE" 0 "reply"; eq "$OUT" "Dana Ortiz replied with a time. Your turn: confirm it, then /scheduler books it." "a time"
eq "$(jq -c '.deals[0] | [.stage, .next_step]' d.json)" '["replied","Answer Dana"]' "deal moves to replied"
FU --file d.json draft --contact dana-harbor-lane --today 2026-10-27 --hook "Thursday at 2"; has "$OUT" "Thursday at 2 works, Dana. Sending the invite now." "confirm-time on your turn"
FU --file d.json record --contact dana-harbor-lane --text "Thursday at 2 works" --approval yes --at 2026-10-27T12:00; eq "$OUT" "Logged your reply to Dana Ortiz." "your reply"
dana_before 2026-10-24; FU --file d.json reply --contact dana-harbor-lane --text "Can you send prices?" --at 2026-10-27T11:48; eq "$OUT" "Dana Ortiz replied. Your turn: answer them (template confirm-time, or write it)." "no time"
FU --file d.json reply --contact nobody --text hi --at 2026-10-27T11:48; eq "$ERR" "No contact nobody" "unknown contact"

# ---- stop and templates
dana_before 2026-10-24; FU --file d.json stop --contact dana-harbor-lane --today 2026-10-25; eq "$OUT" "Dana Ortiz is do not contact, forever." "stop"
eq "$(jq -r '.contacts[0].notes' d.json)" "DO NOT CONTACT since 2026-10-25" "note without words"
FU templates; eq "$(echo "$OUT" | head -1)" "followup-1     2 days after the opening: a short bump, nothing new needed" "templates (%-14s)"
eq "$(echo "$OUT" | wc -l)" 4 "four templates"
done_ "closer_followup"

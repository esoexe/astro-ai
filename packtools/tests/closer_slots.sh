#!/usr/bin/env bash
# scheduler/slots: the assertions of closer-skill/scheduler/test_slots.py, run against the C++ tool, plus the CLI.
# Fixture: the skill's pipeline.example.json. TUE = 2026-10-27, the test's today.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
cp "$EX" p.json
SL() { run scheduler/slots --file p.json "$@"; }
pw() { SL parse --text "$1" --today 2026-10-27; eq "$(cut -c1-16 <<<"$OUT")" "$2" "$1"; }
# ---- parse
SL parse --text "Sorry, crazy week! Thursday at 2 works." --today 2026-10-27; eq "$OUT" "2026-10-29T14:00 (Thursday October 29, 02:00 PM)" "the reel reply"; eq "$CODE" 0 "found"
pw "tomorrow at 10am" 2026-10-28T10:00
pw "Oct 30 at 2:30pm" 2026-10-30T14:30
pw "How about noon Friday?" 2026-10-30T12:00
pw "Monday at 9 works" 2026-11-02T09:00
pw "next Monday at 9" 2026-11-02T09:00
pw "Tuesday 3pm?" 2026-11-03T15:00
pw "jan 5 at 11" 2027-01-05T11:00
pw "October 27 at 12" 2026-10-27T12:00
pw "Oct. 26 at 4" 2027-10-26T16:00
pw "today at 12am" 2026-10-27T00:00
pw "this afternoon, 4:15" 2026-10-27T16:15
pw "Wednesday at 8" 2026-10-28T08:00
pw "Thurs at 13 or 3 p.m." 2026-10-29T15:00
pw "sat 10:00" 2026-10-31T10:00
pw "wed midday" 2026-10-28T12:00
SL parse --text "Tuesday at 12 pm" --today 2026-10-27; eq "$OUT" "2026-11-03T12:00 (Tuesday November 03, 12:00 PM)" "the same weekday is next week; 12 pm is noon"
for t in "Let's talk soon" "Tuesday works" "Sounds good!" "I have 2 kids" "Friday, 25 people"; do
  SL parse --text "$t" --today 2026-10-27; eq "$OUT" "No clear time in that. Ask them." "no clear time: $t"; eq "$CODE" 1 "exit 1: $t"; done
SL parse --text "feb 30 at 2" --today 2026-10-27; eq "$CODE" 1 "a date that does not exist"; eq "$ERR" "day is out of range for month" "ValueError"
# ---- free
echo '[{"start": "2026-10-29T09:00", "end": "2026-10-29T12:00"}]' > b.json
SL free --busy b.json --from 2026-10-29 --days 1 --length 30; eq "$(head -1 <<<"$OUT")" "Thu Oct 29  12:00 PM" "skips busy"; eq "$(wc -l <<<"$OUT" | tr -d ' ')" 8 "up to 8"
eq "$(tail -1 <<<"$OUT")" "Thu Oct 29  03:30 PM" "every 30 minutes"
echo '[]' > e.json; SL free --busy e.json --from 2026-10-31 --days 2; eq "$OUT" "" "a weekend has no slots"
SL free --busy e.json --from 2026-10-30 --days 4 --length 450; eq "$OUT" $'Fri Oct 30  09:00 AM\nFri Oct 30  09:30 AM\nMon Nov 02  09:00 AM\nMon Nov 02  09:30 AM' "a long call fits only early"
echo '[{"start": "2026-11-02T09:00:00+00:00", "end": "2026-11-02T17:00"}]' > z.json; SL free --busy z.json --from 2026-11-02 --days 1; eq "$OUT" "" "an offset keeps its wall time"
echo '[{"begin": "x"}]' > k.json; SL free --busy k.json --from 2026-11-02; eq "$CODE" 1 "a busy entry without start"
SL free --busy e.json --from 2026-11-02T09:00; eq "$ERR" "Invalid isoformat string: '2026-11-02T09:00'" "--from is a date"
# ---- invite
SL invite --contact dana-harbor-lane --start 2026-10-29T14:00; eq "$OUT" "Wrote closer/invites/2026-10-29-dana-harbor-lane.ics" "invite"
ICS=$(cat closer/invites/2026-10-29-dana-harbor-lane.ics)
has "$ICS" $'DTSTART;TZID=America/Chicago:20261029T140000\r\nDTEND;TZID=America/Chicago:20261029T143000\r\n' "start and end, CRLF"
has "$ICS" "SUMMARY:Harbor Lane Dental x Your Agency" "summary"; has "$ICS" "ATTENDEE;CN=Dana Ortiz;RSVP=TRUE:mailto:dana@harborlanedental.com" "both people"
has "$ICS" "ORGANIZER;CN=You:mailto:hello@youragency.studio" "organizer"
eq "$(grep -cE $'^UID:[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}@closer-skill\r$' closer/invites/2026-10-29-dana-harbor-lane.ics)" 1 "a uuid4"
eq "$(grep -cE $'^DTSTAMP:20[0-9]{6}T[0-9]{6}Z\r$' closer/invites/2026-10-29-dana-harbor-lane.ics)" 1 "a UTC stamp"
eq "$(tail -c 15 closer/invites/2026-10-29-dana-harbor-lane.ics | od -An -c | tr -s ' ' | tr -d '\n')" " E N D : V C A L E N D A R \r \n" "ends with CRLF"
jq '.me.timezone = null | (.contacts[] | select(.id=="luis-redbud") | .email) = null' p.json > n.json
run scheduler/slots --file n.json invite --contact luis-redbud --start "2026-11-05 10:00:30" --minutes 45
I2=$(cat closer/invites/2026-11-05-luis-redbud.ics); has "$I2" "DTSTART;TZID=America/Chicago:20261105T100030" "default zone, seconds kept"; has "$I2" "DTEND;TZID=America/Chicago:20261105T104530" "45 minutes"; hasnt "$I2" "ATTENDEE" "no email, no attendee"
SL invite --contact nobody --start 2026-10-29T14:00; eq "$ERR" "No contact nobody" "unknown contact"
SL invite --contact dana-harbor-lane --start tomorrow; eq "$ERR" "Invalid isoformat string: 'tomorrow'" "a bad start"
# ---- book
SL book --contact priya-bluebird --start 2026-11-04T11:00 --approval ok; eq "$CODE" 1 "needs a yes"; has "$OUT" "Not booked:" "why"
echo '[{"start": "2026-11-04T10:30", "end": "2026-11-04T11:15"}]' > bb.json
SL book --contact priya-bluebird --start 2026-11-04T11:00 --approval yes --busy bb.json; eq "$OUT" "Not booked: you are busy then. Run free for open slots." "and a free slot"
SL book --contact priya-bluebird --start 2026-11-04T11:00 --approval yes --busy e.json --at 2026-11-01T09:00; eq "$CODE" 0 "booked"
eq "$OUT" "Booked Priya Shah for Wed Nov 04 11:00. /researcher can prep the brief now." "message"
eq "$(jq -c '.deals[] | select(.id=="bluebird-ads") | [.stage, .next_step]' p.json)" '["call-booked","Call Wed Nov 04, 11:00"]' "call-booked"
eq "$(jq -c '.activities[-1] | [keys_unsorted, .when, .text, .subject, .date]' p.json)" '[["id","date","contact_id","deal_id","channel","type","direction","subject","text","approval","when"],"2026-11-04T11:00","Booked Wed Nov 04, 11:00-11:30","Bluebird Pet Spa x Your Agency","2026-11-01T09:00"]' "the activity"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "valid"
SL book --contact dana-harbor-lane --start 2026-11-09T09:00 --minutes 60 --approval yes; eq "$(jq -c '.deals[0] | [.stage, .next_step]' p.json)" '["contract-sent","Call Mon Nov 09, 09:00"]' "a later stage is kept"
eq "$(jq -r '.activities[-1].date' p.json | grep -cE '^20[0-9]{2}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}$')" 1 "booked now by default"
SL book --contact elena-mesa-verde --start 2026-11-04T11:00 --approval yes; eq "$OUT" "Not booked: Elena Ruiz is do not contact." "never books do not contact"
done_ "closer_slots"

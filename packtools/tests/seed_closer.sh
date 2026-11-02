#!/usr/bin/env bash
# seed-closer/closer: the assertions of seed-skill/tests/test_closer.py, run against the C++ tool through its command line
# (the upstream unit tests of business days, the personal line, the yes, time zones and reply sorting go through draft,
# due, approve, book and inbox here), plus hand-worked edge cases. fixtures/seed/seed-closer holds the upstream fixtures:
# the reel's made-up founder (Casey Morgan, Leashly) and six investors, all on reserved .example domains, and the
# upstream template.txt as the skill's default template (found through --skill-dir). One deliberate difference: the
# printed hints say Astro and name a Gmail or calendar MCP server and A.S.T.R.O AI's Ask-mode card, not Claude's connectors.
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
CF="$FIX/seed/seed-closer"
PLANFIX="$FIX/seed/seed-plan"
C() { run --skill-dir "$CF" seed-closer/closer "$@"; }
c() { local day=$1; shift; if [ "$day" = - ]; then C "$@" --dir s; else C "$@" --dir s --today "$day"; fi; }
MAYA_LINE="Saw you led two pet-care seed rounds, so you know the 20% fee problem better than anyone."
OPTOUT="Not a fit? Reply 'no' and I won't follow up."
fresh() { cd "$TMP" && rm -rf w && mkdir -p w/s && cd w && cp "$CF/config.json" "$CF/investors.json" s/ || exit 1; }
P() { jq -c "$@" s/pipeline.json; }
bid() { c "$1" batch --json "${@:2}" >/dev/null; jq -r .batch <<<"$OUT"; }
rows_of() { sed -n '/^\[$/,/^\]$/p' <<<"$1"; }
send_all() {   # batch, approve with a yes, and log every row as sent in thread t-<id>
    local day=$1 ids=${2:-} reply=${3:-yes} extra=() b
    [ -n "$ids" ] && extra=(--ids "$ids")
    c "$day" batch --json "${extra[@]}"; b=$(jq -r .batch <<<"$OUT")
    c "$day" approve --batch "$b" --reply "$reply" "${extra[@]}"; eq "$CODE" 0 "send_all: approve on $day ($ERR)"
    ROWS=$(rows_of "$OUT")
    local id
    for id in $(jq -r '.[].id' <<<"$ROWS"); do c "$day" log --id "$id" --event sent --thread-id "t-$id"; eq "$CODE" 0 "send_all: log $id sent"; done
}
mkcfg() { jq "$1" "$CF/config.json" > s/config.json; }
mkpipe() { printf '{"investors": %s}\n' "$1" > s/pipeline.json; }
dues() { c "$1" due --json; jq -r '[(.due[],.later[])|.due]|join(",")' <<<"$OUT"; }
crlf_only() { [ "$(tr -cd '\n' < "$1" | wc -c)" = "$(grep -c $'\r$' "$1")" ] && ! tr -d '\r\n' < "$1" | grep -q $'\r'; }
long_lines() { LC_ALL=C awk '{sub(/\r$/, ""); if (length($0) > 75) n++} END {print n + 0}' "$1"; }

# ================================================================ business days (BusinessDays)
REC() { printf '{"id":"a","name":"A","firm":"F","email":"a@x.example","status":"%s","touches":%s%s}' "$1" "$2" "${3:-}"; }
INTRO() { printf '{"from":"us","kind":"intro","date":"%s"}' "$1"; }
FU() { printf '{"from":"us","kind":"followup","date":"%s"}' "$1"; }
fresh
mkpipe "[$(REC sent "[$(INTRO 2026-10-26)]")]"
eq "$(dues 2026-10-20)" "2026-10-29" "+3 business days from a Monday"
mkpipe "[$(REC sent "[$(INTRO 2026-10-26),$(FU 2026-10-29)]")]"
eq "$(dues 2026-10-20)" "2026-11-04" "+7 business days from a Monday"
mkpipe "[$(REC sent "[$(INTRO 2026-10-26),$(FU 2026-11-02)]")]"
eq "$(dues 2026-10-20)" "2026-11-06" "a late follow-up keeps the 4-day gap, not Nov 4"
mkpipe "[$(REC sent "[$(INTRO 2026-10-26),$(FU 2026-11-02),$(FU 2026-11-06)]")]"
eq "$(dues 2026-10-20)" "" "three touches: nothing more is due"
mkpipe "[$(REC sent "[$(INTRO 2026-10-26)]" ',"hold_until":"2026-11-09"')]"
eq "$(dues 2026-10-20)" "2026-11-10" "hold_until: the day after the return"
mkpipe "[$(REC replied "[$(INTRO 2026-10-26),{\"from\":\"them\",\"kind\":\"reply\"}]")]"
eq "$(dues 2026-10-20)" "" "a reply stops the follow-ups"
mkcfg '.outreach.followup_days = [1, 7]'
mkpipe "[$(REC sent "[$(INTRO 2026-10-30)]")]"
eq "$(dues 2026-10-20)" "2026-11-02" "Friday + 1 business day is Monday"
mkpipe "[$(REC sent "[$(INTRO 2026-10-31)]")]"
eq "$(dues 2026-10-20)" "2026-11-02" "Saturday + 1 business day is Monday"
mkcfg '.outreach.followup_days = [1, 7] | .outreach.holidays = ["2026-11-26"]'
mkpipe "[$(REC sent "[$(INTRO 2026-11-25)]")]"
eq "$(dues 2026-10-20)" "2026-11-27" "a config holiday is skipped"
cp "$CF/config.json" s/
c 2026-11-01 due; eq "$CODE" 1 "due on a Sunday"
has "$OUT" "Sun 2026-11-01 is not a business day: nothing goes out today. Next business day: Mon 2026-11-02." "next workday after a Sunday"
mkcfg '.outreach.business_days = false | .outreach.followup_days = [7, 10]'
mkpipe "[$(REC sent "[$(INTRO 2026-10-26)]")]"
eq "$(dues 2026-10-20)" "2026-11-02" "calendar days when business_days is off"
c 2026-10-31 due; hasnt "$OUT" "not a business day" "every day is a workday with calendar days"
c 2026-11-02 due; has "$OUT" "(calendar days, at most 3 touches" "calendar days in the due header"
# config limits (test_config_limits)
for bad in '{"daily_cap": 51}' '{"daily_cap": 0}' '{"daily_cap": true}' '{"max_touches": 4}' '{"followup_days": [7, 3]}' '{"followup_days": [0, 3]}' '{"holidays": ["Nov 26"]}' \
           '{"followup_days": [3, 3]}' '{"daily_cap": 2.5}' '{"holidays": "2026-11-26"}' '{"max_touches": null}'; do
    mkcfg ".outreach += $bad"; c 2026-10-26 due; eq "$CODE" 1 "outreach $bad is refused"; has "$ERR" "closer.py: outreach." "outreach $bad: the message names the setting"
done
mkcfg '.outreach.daily_cap = 51'; c 2026-10-26 due
eq "$ERR" "closer.py: outreach.daily_cap is 51; the Closer allows at most 50 a day. More than that from one inbox looks like spam and hurts every email you send." "daily_cap over 50"
mkcfg '.outreach.max_touches = 4'; c 2026-10-26 due
eq "$ERR" "closer.py: outreach.max_touches is 4; it is 1 to 3 (the intro and up to two follow-ups)" "max_touches 4"
mkcfg '.outreach.max_touches = null'; c 2026-10-26 due
eq "$ERR" "closer.py: outreach.max_touches is None; it is 1 to 3 (the intro and up to two follow-ups)" "max_touches null is %r None"
mkcfg '.outreach.holidays = ["Nov 26"]'; c 2026-10-26 due
eq "$ERR" "closer.py: outreach.holidays wants a date like 2026-10-26, got 'Nov 26'" "a holiday that is not a date"
mkcfg '.outreach.holidays = ["2026-02-30"]'; c 2026-10-26 due
eq "$ERR" "closer.py: outreach.holidays: 2026-02-30 is not a real date" "a holiday that does not exist"
mkcfg '.outreach.max_touches = 2'
mkpipe "[$(REC sent "[$(INTRO 2026-10-26)]")]"
c 2026-10-20 due --json; eq "$(jq -c '[.later[0].due,.later[0].of]' <<<"$OUT")" '["2026-10-29",1]' "max_touches 2: one follow-up at +3"
mkpipe "[$(REC sent "[$(INTRO 2026-10-26),$(FU 2026-10-29)]")]"
eq "$(dues 2026-10-20)" "" "max_touches 2: done after one follow-up"
mkcfg '.outreach = [["daily_cap", 2]]'; c 2026-10-26 due; eq "$CODE" 1 "outreach as pairs: daily_cap 2 applies (nothing due)"
mkcfg '.outreach = "x"'; c 2026-10-26 due; eq "$CODE" 1 "outreach as a string"
has "$ERR" "ValueError: dictionary update sequence element #0 has length 1; 2 is required" "outreach as a string: dict.update raises"

# ================================================================ the personal line (PersonalLine)
fresh
c 2026-10-26 draft; eq "$CODE" 0 "the reel's six lines all pass"
eq "$(P '.investors|length')" 6 "six drafted"
GENERIC_LINES=("" "I hope this email finds you well and wanted to reach out about Leashly."
    "I love what you're doing at Copperline VC and wanted to connect."
    "I'm a big fan of your work and your incredible portfolio of companies."
    "I came across your profile and thought Leashly would be a fit."
    "You are a great investor and I think you'd love what we are building."
    "Saw you led two pet-care seed rounds and..."
    "Saw you led two pet-care seed rounds…"
    "{personal_line} goes here for this investor today"
    "TODO write something specific about this person"
    "Great fund."
    "We think our company would make sense for your firm right now.")
fresh
rows='[]'; i=0
for line in "${GENERIC_LINES[@]}"; do
    i=$((i + 1)); id=$(printf 'g%02d' "$i")
    rows=$(jq -c --arg id "$id" --arg line "$line" '. + [{id: $id, name: "Marcus Lee", firm: "Copperline VC", sectors: ["seed consumer"], email: ($id + "@copperline.example"), sources: ["https://copperline.example/team"], fit: 50, personal_line: $line}]' <<<"$rows")
done
rows=$(jq -c '. + [{id: "g13", name: "Marcus Lee", firm: "Copperline VC", email: "g13@copperline.example", sources: ["https://copperline.example/team"], fit: 50, personal_line: "We would really be glad to have Copperline join this round soon."}]' <<<"$rows")
jq -n --argjson r "$rows" '{investors: $r}' > s/investors.json
c 2026-10-26 draft; eq "$CODE" 1 "generic lines: nothing drafted"
eq "$(grep -c '^REFUSED g' <<<"$OUT")" 13 "all thirteen generic, broken or firm-only lines are refused"
has "$OUT" "Drafted 0 intros from Casey Morgan <casey@leashly.example>, Mon 2026-10-26. Nothing is sent." "zero drafted"
has "$OUT" "REFUSED g01: no personal_line: write one real sentence" "empty line"
has "$OUT" 'REFUSED g02: the personal line is generic ("hope this email finds you"): it could go to any investor.' "generic: the match is quoted"
has "$OUT" "REFUSED g07: the personal line is cut off (it ends in an ellipsis)" "three dots"
has "$OUT" "REFUSED g08: the personal line is cut off" "the ellipsis character"
has "$OUT" "REFUSED g09: the personal line still has a placeholder in it: '{personal_line} goes here for this investor today'" "a placeholder"
has "$OUT" "REFUSED g11: the personal line is 2 words: a real one says what they did" "too short"
has "$OUT" "REFUSED g13: the personal line mentions nothing from this investor's record" "the firm name is not personal"
hasnt "$OUT" "pipeline.json" "nothing written"
[ -e s/pipeline.json ] && bad "generic lines wrote a pipeline" || ok
# what makes a line specific: an anchor from the record, a number, or a name
fresh
jq -nc '{investors: [
  {id: "s1", name: "Ann Bo", firm: "Fern VC", email: "s1@fern.example", sources: ["https://fern.example"], fit: 9, sectors: ["veterinary clinics"], personal_line: "Your writing about veterinary clinics changed how we priced our walks."},
  {id: "s2", name: "Ann Bo", firm: "Fern VC", email: "s2@fern.example", sources: ["https://fern.example"], fit: 8, personal_line: "You backed three marketplaces before they had their first real users."},
  {id: "s3", name: "Ann Bo", firm: "Fern VC", email: "s3@fern.example", sources: ["https://fern.example"], fit: 7, personal_line: "Your talk at Pawcon about trust between owners and walkers stuck with me."},
  {id: "s4", name: "Ann Bo", firm: "Fern VC", email: "s4@fern.example", sources: ["https://fern.example"], fit: 6, personal_line: "This one is a sentence with forty six words in it so it is far too long to be a single sentence that a founder would write to an investor they never met before today and it keeps going and going for no good reason at all here."}]}' > s/investors.json
c 2026-10-26 draft --dry-run; eq "$CODE" 1 "specific lines: one too long"
has "$OUT" "Would draft 3 intros" "an anchor, a number word, a name"
has "$OUT" "REFUSED s4: the personal line is 48 words: keep it to one sentence (45 words at most)" "too long"

# ================================================================ the yes (Yes)
fresh
c 2026-10-26 draft --ids maya-chen,daniel-okafor; eq "$CODE" 0 "draft two"
cp s/pipeline.json "$TMP/two.json"
B=$(bid 2026-10-26)
for reply in "" "sure" "ok" "go ahead" "send it" "yes?" "no" "yes but wait" "yes except Maya" "yes to Daniel" "yes tomorrow" "yes send 3" \
             "yes, after lunch" "yes if it is short" "yes Northlight only" "yes 02 and 3"; do
    c 2026-10-26 approve --batch "$B" --reply "$reply"; eq "$CODE" 1 "not a yes: [$reply]"; has "$OUT" "Not recorded" "not recorded: [$reply]"
done
eq "$(P '[.investors[].status]|unique')" '["drafted"]' "no reply changed anything"
c 2026-10-26 approve --batch "$B" --reply "yes, after lunch"
eq "$OUT" "Not recorded: the reply has a condition or a change in it (after): ask again. Show the batch again and ask." "the doubt word is named"
c 2026-10-26 approve --batch "$B" --reply "yes Northlight only"
eq "$OUT" "Not recorded: the reply has a condition or a change in it (only): ask again. Show the batch again and ask." "doubt comes before names"
c 2026-10-26 approve --batch "$B" --reply "yes to Daniel"
eq "$OUT" "Not recorded: the reply names daniel: a yes covers the whole batch you were shown. To send to fewer people, show a smaller batch (batch --ids ...). Show the batch again and ask." "a named person"
c 2026-10-26 approve --batch "$B" --reply "yes send 3"
eq "$OUT" "Not recorded: the reply says 3, but this batch is 2 emails. Show the batch again and ask." "the wrong count"
c 2026-10-26 approve --batch "$B" --reply "yes 007"
eq "$OUT" "Not recorded: the reply says 7, but this batch is 2 emails. Show the batch again and ask." "a count is an int (007 is 7)"
c 2026-10-26 approve --batch "$B" --reply "sure"
eq "$OUT" 'Not recorded: only a reply that starts with "yes" counts (not ok, sure, go, send it). Show the batch again and ask.' "not a yes word"
for reply in "yes" "Yes." "y" "yes, send them" "Yes please send all 2" "yes send both" "yes, two" "YES ٢"; do
    cp "$TMP/two.json" s/pipeline.json
    c 2026-10-26 approve --batch "$B" --reply "$reply"; eq "$CODE" 0 "a clear yes: [$reply]"
done
eq "$(P '.investors[0].draft.reply')" '"YES ٢"' "the reply is kept word for word"
eq "$(P '.investors[0].history[-1].note')" "\"batch $B, reply 'YES ٢'\"" "history quotes the reply"

# ================================================================ time zones and the invite (Ics)
fresh
c 2026-10-26 draft --ids maya-chen >/dev/null; send_all 2026-10-26
c 2026-10-27 log --id maya-chen --event interested; eq "$CODE" 0 "log interested"
eq "$OUT" "maya-chen: Interested, status interested. Follow-ups stop: this is a conversation now." "log interested output"
TITLE=""; for _ in 1 2 3 4 5 6 7 8; do TITLE+="Leashly × Northlight · "; done
c 2026-10-27 book --id maya-chen --start "2026-11-05 10:00" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z --title "$TITLE"
eq "$CODE" 0 "book with a long title"
ICS=s/calls/maya-chen.ics
eq "$(long_lines "$ICS")" 0 "every line is 75 octets at most"
crlf_only "$ICS" && ok || bad "the invite has CRLF line ends only"
eq "$(sed -z 's/\r\n //g' "$ICS" | grep -a '^SUMMARY:' | tr -d '\r')" "SUMMARY:$TITLE" "unfolding gives the title back"
grep -qa $'^ ' "$ICS" && ok || bad "the long title is folded"
iconv -f utf-8 -t utf-8 "$ICS" >/dev/null 2>&1 && ok || bad "folding never splits a UTF-8 character"
start_utc() { c "${3:-2026-01-01}" book --id maya-chen --start "$1" --tz "$2" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z; tr -d '\r' < "$ICS" | sed -n 's/^DTSTART://p'; }
eq "$(start_utc "2026-11-05 10:00" America/New_York)" 20261105T150000Z "New York after the DST change"
eq "$(start_utc "2026-10-29 10:00" ET)" 20261029T140000Z "ET before the change"
eq "$(start_utc "2026-11-05T10:00-05:00" ET)" 20261105T150000Z "an explicit offset"
eq "$(start_utc "2026-07-01 10:00" Europe/London)" 20260701T090000Z "London in summer"
eq "$(start_utc "2026-07-01 10:00" Australia/Sydney)" 20260701T000000Z "Sydney in its winter"
eq "$(start_utc "2026-01-15 10:00" Australia/Sydney)" 20260114T230000Z "Sydney in its summer"
eq "$(start_utc "2026-11-01 01:30" ET)" 20261101T053000Z "1:30 happens twice: the first (daylight) one wins"
eq "$(start_utc "2026-03-29 01:30" Europe/Berlin)" 20260329T003000Z "Berlin just before the jump"
eq "$(start_utc "2026-11-05 10:00" PT)" 20261105T180000Z "PT alias"
eq "$(start_utc "2026-11-05T15:00Z" ET)" 20261105T150000Z "Z"
eq "$(start_utc "2026-11-05T20:30+0530" ET)" 20261105T150000Z "an offset without a colon"
c 2026-01-01 book --id maya-chen --start "2026-03-08 02:30" --tz ET --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
eq "$CODE" 1 "2:30 on Mar 8 never happens in New York"
eq "$ERR" "closer.py: 2026-03-08 02:30:00 does not exist in America/New_York: the clocks jump forward that night" "the gap message"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --tz Mars/Olympus --link https://meet.google.com/x
eq "$CODE" 1 "an unknown zone"
has "$ERR" "closer.py: unknown time zone 'Mars/Olympus'. Use one of: America/Anchorage, America/Chicago," "the zones are listed, sorted"
has "$ERR" "Pacific/Pago_Pago, UTC, or give the start with an offset, like 2026-11-05T10:00-05:00" "the list ends with the offset hint"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --tz "UTC" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
has "$(tr -d '\r' < "$ICS")" "X-WR-TIMEZONE:UTC" "UTC names its zone"
c 2026-01-01 book --id maya-chen --start "2026-11-05T10:00+01:00" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
hasnt "$(tr -d '\r' < "$ICS")" "X-WR-TIMEZONE" "an offset zone has no X-WR-TIMEZONE"
has "$OUT" "UTC+01:00 (UTC+01:00)" "the offset is the zone"
for bad in "2026-11-05" "2026-11-05 24:00" "2026-11-05 10:61" "05/11/2026 10:00"; do
    c 2026-01-01 book --id maya-chen --start "$bad" --link https://meet.google.com/x; eq "$CODE" 1 "a bad start [$bad]"
done
c 2026-01-01 book --id maya-chen --start "2026-11-05 24:00" --link https://meet.google.com/x
eq "$ERR" "closer.py: --start: 2026-11-05 24:00 is not a real time" "hour 24"
c 2026-01-01 book --id maya-chen --start "2026-02-30 10:00" --link https://meet.google.com/x
eq "$ERR" "closer.py: --start: 2026-02-30 is not a real date" "Feb 30"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --end 10:45 --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
eq "$(tr -d '\r' < "$ICS" | sed -n 's/^DTEND://p')" 20261105T154500Z "--end 10:45 makes 45 minutes"
eq "$(P '.investors[0].call.minutes')" 45 "the call keeps its minutes"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --end 9:00 --link https://meet.google.com/x
eq "$ERR" "closer.py: the call is -60 minutes; book 10 to 240" "an end before the start"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --location "Phone:  +1 555 0100" --stamp 2026-11-02T15:04:00Z
eq "$(tr -d '\r' < "$ICS" | grep -c '^URL:')" 0 "no URL without a link"
has "$(sed -z 's/\r\n //g' "$ICS" | tr -d '\r')" 'LOCATION:Phone: +1 555 0100' "the location, blanks squeezed"
c 2026-01-01 book --id maya-chen --start "2026-11-05 10:00" --link https://meet.google.com/x --stamp 2026-13-01T00:00:00Z
eq "$CODE" 1 "a stamp in month 13"; has "$ERR" "ValueError: month must be in 1..12" "datetime raises for month 13"
c 2026-01-01 book --id maya-chen --start "2026-11-07 10:00" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
has "$OUT" "  note: that is a Saturday." "a weekend call is noted"

# ================================================================ sorting replies (Classify)
fresh
klass() { c 2026-10-30 inbox --text "$1" --json "${@:2}"; jq -r '.[0].class' <<<"$OUT"; }
while IFS='|' read -r want text; do
    eq "$(klass "$text")" "$want" "classify: $text"
done <<'EOF'
question|Interesting. What's your take rate, and how do you guarantee the backup walker?
interested|Send the deck over?
interested|Happy to chat, I love this.
interested|Thursday 10am works. Send an invite.
not_now|Not this quarter, keep me posted.
not_now|We'll pass for now, but keep in touch.
no|Pet services are outside our thesis, so we'll pass.
no|No thanks.
no|no
interested|No worries if not, but happy to chat next week!
optout|Please remove me from your list.
optout|Unsubscribe
ooo|I am out of the office until Monday, November 9 with limited access to email.
unclear|Thanks for the note.
question|How big is the market
no|Nope!
interested|We’re interested in learning more.
EOF
eq "$(klass "Address not found. Your message wasn't delivered to x@y.example." --subject "Delivery Status Notification (Failure)" --from "Mail Delivery Subsystem <mailer-daemon@mail.example>")" bounce "a bounce from the mail system"
klass "Hello" --from "postmaster@mail.example" >/dev/null; eq "$(jq -r '.[0].class' <<<"$OUT")" bounce "postmaster is the mail system"
eq "$(jq -r '.[0].why' <<<"$OUT")" "from the mail system" "the why of a bare postmaster bounce"
eq "$(klass "Thanks" --subject "Automatic reply: away")" ooo "an auto-reply subject"
eq "$(klass $'Sounds good, let\'s find a time.\n\nOn Mon, Oct 26, 2026 at 9:02 AM Casey Morgan <\ncasey@leashly.example> wrote:\n> Not a fit? Reply \'no\' and I won\'t follow up.\n')" interested "the quoted opt-out line is ignored"
klass $'Yes!\n> no\n> no' >/dev/null; eq "$(jq -r '.[0].excerpt' <<<"$OUT")" "Yes!" "strip_quoted drops the quote"
klass $'Sounds great.\n-- \nAnn\nNo thanks' >/dev/null; eq "$(jq -r '.[0].class' <<<"$OUT")" interested "a signature is dropped"
klass $'Not for us.\n\nFrom: Casey\nSent: Monday\nTo: Ann\nSubject: Leashly' >/dev/null; eq "$(jq -r '.[0].class' <<<"$OUT")" no "an Outlook header block ends the new part"
klass "Send the deck over?" >/dev/null; eq "$(jq -r '.[0]|[.deck,.next]|.[1]|contains("They asked for the deck")' <<<"$OUT")" true "the deck ask is in the next step"
eq "$(jq -c '.[0]|[.id,.name,.label,.back]' <<<"$OUT")" '[null,"unknown sender","Interested",null]' "an unmatched reply"
back() { c 2026-10-30 inbox --text "$1" --json; jq -r '.[0].back' <<<"$OUT"; }
eq "$(back "Automatic reply. out until Monday, November 9")" 2026-11-09 "return: a month and a day"
eq "$(back "Automatic reply. back on 2026-11-12")" 2026-11-12 "return: an ISO date"
eq "$(back "Automatic reply. returning on 3 January")" 2027-01-03 "return: a day and a month, next year"
eq "$(back "Automatic reply. away until 11/16")" 2026-11-16 "return: m/d"
eq "$(back "Automatic reply. back on Tuesday")" 2026-11-03 "return: a weekday"
eq "$(back "Automatic reply. back on Friday")" 2026-11-06 "return: today's weekday is a week ahead"
eq "$(back "on vacation, limited email")" null "return: no date"
eq "$(back "Automatic reply. back on 2026-02-30, or until Nov. 2nd")" 2026-11-02 "return: an impossible ISO date falls through"
eq "$(back "Automatic reply. until February 30")" null "return: Feb 30 is no date"
c 2026-10-30 inbox --text "Thanks for the note."; eq "$CODE" 0 "inbox without --write"
eq "$OUT" 'unknown sender [not in the pipeline]: REPLIED (no rule matched)
  "Thanks for the note."
  next: The rules could not sort it. Read it yourself and tell Astro what to answer; nothing goes out without a yes.
Nothing recorded yet. Check the labels with the user, then run it again with --write.' "inbox text output"
c 2026-10-30 inbox; eq "$CODE" 1 "inbox needs replies"
eq "$ERR" 'closer.py: give the replies: --file replies.json (or a .txt with --id), or --text "..."' "inbox without input"
c 2026-10-30 inbox --file "$TMP/nope-$$.json"; eq "$ERR" "closer.py: no such file: $TMP/nope-$$.json" "inbox with a missing file"
printf '{"replies": 5}' > "$TMP/r.json"; c 2026-10-30 inbox --file "$TMP/r.json"
eq "$ERR" "closer.py: $TMP/r.json needs a list of replies: [{\"id\" or \"from\", \"text\", ...}]" "replies that are not a list"
printf '{"replies": [5, "x"]}' > "$TMP/r.json"; c 2026-10-30 inbox --file "$TMP/r.json"
eq "$ERR" "closer.py: no replies to sort" "non-dict replies are dropped"
printf '{"replies": [' > "$TMP/r.json"; c 2026-10-30 inbox --file "$TMP/r.json"
has "$ERR" "closer.py: $TMP/r.json is not valid JSON: Expecting value: line 1 column 14 (char 13)" "bad replies JSON"
printf 'Not this quarter.\r\nkeep me posted\r\n' > "$TMP/r.txt"; c 2026-10-30 inbox --file "$TMP/r.txt" --json
eq "$(jq -c '.[0]|[.class,.excerpt]' <<<"$OUT")" '["not_now","Not this quarter. keep me posted"]' "a .txt reply"
printf '[{"text": 5}]' > "$TMP/r.json"; c 2026-10-30 inbox --file "$TMP/r.json"
eq "$CODE" 1 "a reply text that is a number"; has "$ERR" "AttributeError: 'int' object has no attribute 'replace'" "like Python, (5 or '').replace raises"

# ================================================================ draft (Flow)
fresh
c 2026-10-26 draft; eq "$CODE" 0 "draft"
EML=s/outreach/maya-chen-1.eml
crlf_only "$EML" && ok || bad "the .eml has CRLF line ends only"
eq "$(sed -n 1,8p "$EML" | tr -d '\r')" 'From: Casey Morgan <casey@leashly.example>
To: Maya Chen <maya@northlight.example>
Subject: Leashly: dog walks without the surprise fees
Date: Mon, 26 Oct 2026 13:00:00 +0000
Message-ID: <maya-chen-1.a96c03450d97@leashly.example>
Content-Type: text/plain; charset="utf-8"
Content-Transfer-Encoding: quoted-printable
MIME-Version: 1.0' "the headers, in Python's order"
[[ "$(sed -n 5p "$EML" | tr -d '\r')" =~ ^Message-ID:\ \<maya-chen-1\.[0-9a-f]{12}@leashly\.example\>$ ]] && ok || bad "Message-ID shape"
eq "$(sed -n 12p "$EML" | tr -d '\r')" "Saw you led two pet-care seed rounds, so you know the 20% fee problem better =" "a soft line break at 76"
eq "$(P '.investors|length')" 6 "six in the pipeline"
eq "$(P '[.investors[].status]|unique')" '["drafted"]' "all drafted"
eq "$(P '.investors[0].draft')" '{"n":1,"kind":"intro","file":"outreach/maya-chen-1.eml","subject":"Leashly: dog walks without the surprise fees","message_id":"<maya-chen-1.a96c03450d97@leashly.example>","state":"drafted","drafted_on":"2026-10-26"}' "the draft record"
eq "$(P '.investors[0]|keys_unsorted')" '["id","email","name","firm","fit","status","touches","next_due","labels","notes","personal_line","sources","thread_id","call","draft","history"]' "the record's keys, in order"
eq "$(P '.updated')" '"2026-10-26"' "pipeline updated"
has "$OUT" "Nothing is sent" "nothing sent"
eq "$(head -c 2 s/pipeline.json; tail -c 2 s/pipeline.json | od -An -c | tr -d ' ')" '{
}\n' "pipeline JSON: indent 2 and a newline"
c 2026-10-26 batch --json --ids maya-chen; BODY=$(jq -r .first.body <<<"$OUT")
for part in "Hi Maya," "$MAYA_LINE" "Leashly is a dog-walking app with flat prices and a backup walker on every walk." "We're raising a \$1.5M seed." "$OPTOUT" $'Casey Morgan\nFounder, Leashly\n100 Example Street, Austin, TX 78701'; do
    has "$BODY" "$part" "the intro body has: $part"
done
eq "$(jq -r .first.to <<<"$OUT")" "Maya Chen <maya@northlight.example>" "str(msg[To])"
c 2026-10-26 draft; eq "$CODE" 0 "a second draft re-drafts the drafted"
has "$OUT" "Drafted 6 intros" "drafted again"
c 2026-10-26 draft --ids maya-chen; eq "$CODE" 0 "re-draft one drafted intro"
# dry run (test_dry_run_writes_nothing)
fresh
c 2026-10-26 draft --top 2 --dry-run; eq "$CODE" 0 "dry run"
has "$OUT" "Would draft 2 intros" "would draft 2"
[ -e s/pipeline.json ] || [ -e s/outreach ] && bad "the dry run wrote files" || ok
eq "$(sed -n 2p <<<"$OUT")" "  maya-chen          Maya Chen, Northlight Ventures <maya@northlight.example>" "dry-run rows have no path"
c 2026-10-26 draft --top -4 --dry-run; has "$OUT" "Would draft 2 intros" "--top -4 is picked[:-4]"
c 2026-10-26 draft --min-fit 90 --dry-run; has "$OUT" "Would draft 3 intros" "--min-fit 90"
c 2026-10-26 draft --min-fit 99; eq "$CODE" 1 "nobody fits"; eq "$OUT" "Nobody to draft." "nobody to draft"
c 2026-10-26 draft --ids nobody; eq "$ERR" "closer.py: nobody is not in investors.json" "an unknown id"
# bad rows (test_bad_rows_are_refused_and_the_rest_drafted)
fresh
cp "$CF/investors-bad.json" s/investors.json
c 2026-10-26 draft; eq "$CODE" 1 "bad rows: exit 1"
eq "$(P '[.investors[].id]')" '["maya-chen"]' "only maya-chen drafted"
for pair in "gary-stone:generic" "nina-hart:no public source" "omar-said:no valid email" "lena-fox:no personal_line" "dana-cole:already used for maya-chen" "ray-kim:cut off"; do
    grep -q "^REFUSED ${pair%%:*}: .*${pair#*:}" <<<"$OUT" && ok || bad "bad rows: ${pair%%:*} refused for ${pair#*:}"
done
# the example config (test_the_example_config_name_is_refused)
fresh
cp "$PLANFIX/config.example.json" s/config.json
c 2026-10-26 draft; eq "$CODE" 1 "the example config"
eq "$ERR" "closer.py: seed/config.json founder.name is 'Your Name': put your real name there. Every email is signed by a real person." "the example name"
mkcfg '.founder.email = "casey@leashly"'; c 2026-10-26 draft
eq "$ERR" "closer.py: seed/config.json founder.email must be the address you send from (your own inbox), got 'casey@leashly'" "a bad sender address"
mkcfg '.founder.name = "Casey [Morgan]"'; c 2026-10-26 draft; has "$ERR" "founder.name is 'Casey [Morgan]'" "brackets are a placeholder"
mkcfg 'del(.startup.one_liner)'; c 2026-10-26 draft
eq "$ERR" "closer.py: seed/config.json needs startup.name and startup.one_liner for the email" "no one-liner"
mkcfg 'del(.founder.company_address)'; c 2026-10-26 draft --ids maya-chen
has "$OUT" "note: founder.company_address is empty: anti-spam laws such as CAN-SPAM expect a postal address" "the address note"
eq "$(c 2026-10-26 batch --json; jq -r .first.body <<<"$OUT" | tail -2)" $'Casey Morgan\nFounder, Leashly' "the signature without an address"
# the tagline (test_missing_tagline_falls_back_to_the_one_liner)
fresh
mkcfg 'del(.startup.tagline)'
c 2026-10-26 draft --ids maya-chen; eq "$CODE" 0 "no tagline"
has "$OUT" "Subject: Leashly: a dog-walking app with flat prices and a backup walker on every walk" "the one-liner, sentence case"
has "$OUT" "note: maya-chen: the subject is 77 characters; under 60 shows in full on a phone" "the long-subject note"
# the template (test_template_must_keep_the_opt_out)
fresh
sed 's/{optout}//' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$CODE" 1 "a template without the opt-out"
eq "$ERR" "closer.py: the template's intro lost {optout}: every intro keeps the personal line, the opt-out line and your name" "the opt-out is required"
sed 's/{personal_line}/{personal_line} {favorite_color}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$CODE" 1 "an unknown placeholder"
has "$ERR" "closer.py: template intro uses {favorite_color}, which the Closer does not fill. It knows: ask, company_address, extra, firm, first_name, geography, instrument, intro_day, intro_subject, name, one_liner, one_liner_sentence, optout, personal_line, raise, sector, sender, sender_first, signature, stage, startup, tagline, title" "the known placeholders, sorted"
sed 's/{personal_line}/{personal_line} {0}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; has "$ERR" "closer.py: template intro uses {0}, which the Closer does not fill." "a numbered field matches \\w+"
sed 's/{personal_line}/{personal_line} {}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro: {} is not a placeholder the Closer knows" "an empty field"
sed 's/{personal_line}/{personal_line} {firm.x}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro: {firm.x} is not a placeholder the Closer knows" "an attribute field"
sed 's/{personal_line}/{personal_line} {firm/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro: unexpected '{' in field name (write a literal brace as {{ or }})" "an open brace runs into the next field"
sed '/^=== followup 1 ===/i {firm' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro: expected '}' before end of string (write a literal brace as {{ or }})" "an open brace at the end"
sed 's/{personal_line}/{personal_line} }/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro: Single '}' encountered in format string (write a literal brace as {{ or }})" "a lone close brace"
sed 's/{personal_line}/{personal_line} {{literal}} {raise:>8} {firm!r}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft --ids maya-chen; eq "$CODE" 0 "literal braces, a spec and a conversion"
has "$OUT" "$MAYA_LINE {literal} \$1.5M 'Northlight Ventures'" "format_map: {{ }}, :>8 (squeezed by tidy), !r"
sed 's/{personal_line}/{personal_line} {raise:d}/' "$CF/template.txt" > s/template.txt
c 2026-10-26 draft --ids maya-chen; eq "$CODE" 1 "a bad format spec"; has "$ERR" "ValueError: Unknown format code 'd' for object of type 'str'" "str.__format__ raises"
mkcfg 'del(.startup.raise)'; cp "$CF/template.txt" s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template intro uses {raise} but it is empty: add startup.raise to seed/config.json" "an empty placeholder says how to fill it"
cp "$CF/config.json" s/
printf '=== intro ===\nSubject: Hi {first_name}\n{personal_line}\n{optout}\n{signature}\n=== followup 1 ===\nSubject: Re\n{optout} {signature}\n' > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template s/template.txt has no '=== followup 2 ===' section" "a missing section"
printf '=== intro ===\n\nHi\n' > s/template.txt
c 2026-10-26 draft; eq "$ERR" "closer.py: template s/template.txt: the 'intro' section must start with a Subject: line" "no Subject line"
rm s/template.txt
C draft --dir s --today 2026-10-26 --template "$TMP/none-$$.txt"; eq "$ERR" "closer.py: no template at $TMP/none-$$.txt" "a missing --template"
run seed-closer/closer draft --dir s --today 2026-10-26; eq "$CODE" 1 "no skill dir: the default template is next to the tool"
has "$ERR" "closer.py: no template at /" "the default template path is absolute"

# ================================================================ the yes gate (test_the_yes_gate)
fresh
c 2026-10-26 draft >/dev/null
B=$(bid 2026-10-26)
c 2026-10-26 batch; eq "$CODE" 0 "batch"
has "$OUT" "BATCH $B: 6 emails" "BATCH id: 6 emails"
has "$OUT" "\"$MAYA_LINE\"" "the personal line is shown"
has "$OUT" "The first email, in full:" "the first email"
eq "$(sed -n 2p <<<"$OUT")" "Daily cap: 20 cold emails. 0 sent or approved today, 6 in this batch." "the cap line"
has "$OUT" "Ask: \"Send these 6 emails from your Gmail? Reply yes or no.\"
Then: closer.py approve --batch $B --reply \"<the reply, word for word>\"" "the ask"
c 2026-10-26 batch --json; eq "$(jq -c '[.emails,.cold,.cap,.used_today,.held,(.recipients|length),.recipients[0].touch,.recipients[0].personal_line]' <<<"$OUT")" \
    "[6,6,20,0,[],6,\"intro (touch 1 of 3)\",\"$MAYA_LINE\"]" "batch --json"
eq "$(jq -r .approve <<<"$OUT")" "closer.py approve --batch $B --reply \"<the reply, word for word>\"" "batch --json approve line"
c 2026-10-26 batch --json --ids maya-chen,marcus-lee --limit 1; eq "$(jq -c '[.emails,.held]' <<<"$OUT")" '[1,["marcus-lee"]]' "--ids and --limit"
has "$(jq -r .approve <<<"$OUT")" "--ids maya-chen,marcus-lee --limit 1 --reply" "the approve line repeats the flags"
c 2026-10-26 batch --ids priya-raman,nobody; eq "$ERR" "closer.py: nobody is not in the pipeline (draft them first)" "--ids with an unknown id"
for reply in "sure" "yes except Marcus" "yes send 5"; do
    c 2026-10-26 approve --batch "$B" --reply "$reply"; eq "$CODE" 1 "refused: $reply"; has "$OUT" "Not recorded" "not recorded: $reply"
done
eq "$(P '.investors[0].status')" '"drafted"' "still drafted"
c 2026-10-26 log --id maya-chen --event sent; eq "$CODE" 1 "sent before the yes"
has "$ERR" "the yes comes first" "the yes comes first"
printf 'P.S. edited\r\n' >> s/outreach/marcus-lee-1.eml
c 2026-10-26 approve --batch "$B" --reply yes; eq "$CODE" 1 "a draft edited after the batch"
has "$ERR" "changed since it was shown" "the batch id changed"
B2=$(bid 2026-10-26)
[ "$B2" != "$B" ] && ok || bad "the edit changes the batch id"
c 2026-10-26 approve --batch "$B2" --reply "Yes, send all 6"; eq "$CODE" 0 "approve"
has "$OUT" "one tool call per email" "send instructions"
has "$OUT" "Now send each one through the Gmail MCP server the user added in A.S.T.R.O AI's MCP settings" "A.S.T.R.O AI: the sender is a Gmail MCP server"
has "$OUT" "In Ask mode A.S.T.R.O AI's card asks Allow or Deny every time" "A.S.T.R.O AI: the card is named honestly"
hasnt "$OUT" "Claude" "A.S.T.R.O AI: the assistant is Astro"
has "$OUT" "Recorded your yes for batch $B2: 6 emails (6 of today's 20 cold emails)." "recorded"
eq "$(P '.investors[0].status')" '"approved"' "approved"
eq "$(P '.investors[0].draft.reply')" '"Yes, send all 6"' "the reply is kept"
eq "$(P '.investors[0].draft|[.state,.approved_on,.batch,(.sha|length)]')" "[\"approved\",\"2026-10-26\",\"$B2\",64]" "the approved draft"
C outbox --dir s --json; eq "$CODE" 0 "outbox --json"
eq "$(jq 'length' <<<"$OUT")" 6 "six in the outbox"
has "$(jq -r '.[0].body' <<<"$OUT")" "$OPTOUT" "the opt-out is in the body"
eq "$(jq -c '.[0]|[.id,.kind,.touch,.to,.to_email,.subject,.thread_id,.in_reply_to,.eml]' <<<"$OUT")" '["maya-chen","intro",1,"Maya Chen <maya@northlight.example>","maya@northlight.example","Leashly: dog walks without the surprise fees",null,null,"s/outreach/maya-chen-1.eml"]' "an outbox row"
C outbox --dir s; eq "$(sed -n 1,2p <<<"$OUT")" "6 approved emails, not sent yet:
  maya-chen          Maya Chen <maya@northlight.example>  Leashly: dog walks without the surprise fees" "outbox text"
printf 'x' >> s/outreach/priya-raman-1.eml
C outbox --dir s; eq "$CODE" 1 "an approved draft edited after the yes"
eq "$ERR" "closer.py: outreach/priya-raman-1.eml changed after the yes: show it again (batch) before it goes anywhere" "the sha check"
c 2026-10-26 log --id marcus-lee --event denied; eq "$CODE" 0 "denied"
eq "$OUT" "marcus-lee: back to drafted. It needs a new yes before it goes anywhere." "denied output"
eq "$(P '.investors[]|select(.id=="marcus-lee")|[.status,.draft.state,(.draft|has("sha"))]')" '["drafted","drafted",false]' "back to drafted"
c 2026-10-26 log --id marcus-lee --event denied; eq "$ERR" "closer.py: marcus-lee has no approved email to put back" "denied twice"
c 2026-10-26 log --id maya-chen --event sent --date 2026-10-25; eq "$ERR" "closer.py: maya-chen: the send date 2026-10-25 is before the yes (2026-10-26)" "sent before the yes date"
c 2026-10-26 log --id " , "; eq "$CODE" 2 "--event is required"
c 2026-10-26 log --id " , " --event sent; eq "$ERR" "closer.py: say who: --id maya-chen (or several, comma separated)" "an empty --id"
c 2026-10-26 log --id maya-chen --event note; eq "$ERR" 'closer.py: --event note needs --note "..."' "a note needs text"
c 2026-10-26 log --id maya-chen,daniel-okafor --event note --note "met at a demo day"; eq "$OUT" "maya-chen: note added.
daniel-okafor: note added." "notes for two"
c 2026-10-26 log --id maya-chen --event note --note "second"; eq "$(P '.investors[0].notes')" '"met at a demo day; second"' "notes accumulate"
c 2026-10-26 log --id maya-chen --event bogus; eq "$CODE" 2 "an unknown event"
has "$ERR" "argument --event: invalid choice: 'bogus' (choose from 'sent', 'denied', 'note', 'interested', 'question', 'replied', 'notnow', 'passed', 'no', 'optout', 'bounced', 'ooo')" "the event choices, in order"
c 2026-10-26 log --id maya-chen --event interested; eq "$ERR" "closer.py: nothing was sent to maya-chen yet, so there is no reply to record" "a reply before a send"

# ================================================================ the daily cap (test_the_daily_cap)
fresh
mkcfg '.outreach.daily_cap = 4'
c 2026-10-26 draft >/dev/null
c 2026-10-26 batch
has "$OUT" "4 emails" "4 in the batch"
has "$OUT" "2 more drafts wait for the next day" "2 held"
send_all 2026-10-26
eq "$(jq -c '[.[].id]' <<<"$ROWS")" '["maya-chen","daniel-okafor","priya-raman","sam-whitaker"]' "the best four fits"
c 2026-10-26 batch; eq "$CODE" 1 "the cap is used"
has "$OUT" "cap of 4 cold emails is used" "cap used"
eq "$OUT" "Today's cap of 4 cold emails is used (4 sent or approved). 2 drafts wait for the next day." "the cap message"
c 2026-10-26 approve --batch x --reply yes; eq "$ERR" "closer.py: nothing is waiting for a yes (or today's cap is used): run batch" "approve with the cap used"
send_all 2026-10-27
eq "$(jq 'length' <<<"$ROWS")" 2 "the other two the next day"

# ================================================================ the whole road (test_the_whole_road_to_a_booked_call)
fresh
c 2026-10-26 draft >/dev/null
send_all 2026-10-26
eq "$(P '.investors[0]|[.status,.next_due,.thread_id]')" '["sent","2026-10-29","t-maya-chen"]' "sent, due Thursday, in its thread"
eq "$(P '.investors[0].touches[0]|keys_unsorted')" '["n","date","kind","from","subject","message_id","file"]' "the touch record"
c 2026-10-28 due; eq "$CODE" 1 "nothing due Wednesday"
c 2026-10-31 due; eq "$CODE" 1 "Saturday"
has "$OUT" "not a business day" "not a business day"
c 2026-10-29 due --json; eq "$CODE" 0 "due Thursday"
eq "$(jq '.due|length' <<<"$OUT")" 6 "six due"
eq "$(jq -c '.due[0]' <<<"$OUT")" '{"id":"maya-chen","name":"Maya Chen","firm":"Northlight Ventures","due":"2026-10-29","followup":1,"of":2,"intro_sent":"2026-10-26"}' "a due row"
c 2026-10-29 due; eq "$(sed -n 1,2p <<<"$OUT")" "Follow-ups due Thu 2026-10-29 (business days, at most 3 touches, never after a reply):
  maya-chen          Maya Chen, Northlight Ventures  follow-up 1 of 2, intro sent Mon 2026-10-26" "due text"
has "$OUT" "Next: closer.py followup --due, then batch and the user's yes." "due next step"
c 2026-10-28 followup --id maya-chen; has "$ERR" "not due until Thu 2026-10-29" "not due yet"
c 2026-10-29 followup --due --ask "would Monday work"; eq "$ERR" "closer.py: --ask is a question: end it with a question mark" "--ask needs a ?"
c 2026-10-29 followup --due --extra "see [link]"; eq "$ERR" "closer.py: --extra still has a placeholder in it: 'see [link]'" "--extra placeholder"
c 2026-10-29 followup; eq "$ERR" "closer.py: say who: --id maya-chen, or --due for everyone due today" "followup needs someone"
c 2026-10-29 followup --due --extra "Here's our 60-second demo: https://leashly.example/demo"; eq "$CODE" 0 "follow-ups for everyone due"
has "$OUT" "Drafted follow-up 1 of 2 for Maya Chen (maya-chen) -> s/outreach/maya-chen-2.eml" "the follow-up is drafted"
has "$OUT" "Send it in the intro's thread (t-maya-chen)." "in the thread"
FUE=s/outreach/maya-chen-2.eml
INTRO_ID=$(P -r '.investors[0].touches[0].message_id')
eq "$(grep -a '^In-Reply-To: ' "$FUE" | tr -d '\r')" "In-Reply-To: $INTRO_ID" "In-Reply-To"
eq "$(grep -a '^References: ' "$FUE" | tr -d '\r')" "References: $INTRO_ID" "References"
eq "$(grep -a '^Subject: ' "$FUE" | tr -d '\r')" "Subject: Re: Leashly: dog walks without the surprise fees" "Re: subject"
c 2026-10-29 batch --json --ids maya-chen; BODY=$(jq -r .first.body <<<"$OUT")
has "$BODY" "my note from Monday, Oct 26 about Leashly" "the intro day"
has "$BODY" "60-second demo" "the extra line"
has "$BODY" "$OPTOUT" "the opt-out"
eq "$(jq -r '.recipients[0].touch' <<<"$OUT")" "follow-up 1 of 2" "touch label"
WB=$(awk -v o="$OPTOUT" '{i=index($0,o); if(i){print substr($0,1,i-1); exit} print}' <<<"$BODY" | wc -w)
[ "$WB" -lt 80 ] && ok || bad "the follow-up is under 80 words before the opt-out ($WB)"
c 2026-10-29 followup --id maya-chen; eq "$ERR" "closer.py: maya-chen already has a followup draft waiting (drafted)" "one draft at a time"
c 2026-10-30 inbox --file "$CF/replies.json" --write; eq "$CODE" 0 "inbox --write"
eq "$(P -c '[.investors[]|{(.id): [.status, .labels[-1]]}]|add')" '{"maya-chen":["replied","Asked a question"],"daniel-okafor":["interested","Interested"],"priya-raman":["passed","Not now"],"sam-whitaker":["interested","Interested"],"elena-brooks":["passed","No"],"marcus-lee":["sent","Out of office"]}' "the six replies, recorded"
eq "$(P '.investors[0].draft')" null "maya's waiting follow-up is void"
eq "$(P '.investors[5]|[.draft,.hold_until,.next_due]')" '[null,"2026-11-09","2026-11-10"]' "Marcus is held until he is back"
eq "$(P '.investors[0].touches[-1]')" '{"date":"2026-10-30","kind":"reply","from":"them","class":"question","excerpt":"Interesting. What'"'"'s your take rate, and how do you guarantee the backup walker?"}' "the reply touch"
has "$OUT" "marcus-lee: out of office until 2026-11-09. Status stays sent, next follow-up 2026-11-10. The waiting follow-up (outreach/marcus-lee-2.eml) is void; it is drafted again when it is due." "the OOO record"
c 2026-11-04 followup --id maya-chen; has "$ERR" "follow-ups stop after a reply" "no follow-up after a reply"
c 2026-11-04 draft --ids elena-brooks; eq "$CODE" 1 "a closed thread"
has "$OUT" "REFUSED elena-brooks: the thread is closed for good" "closed for good"
c 2026-11-04 log --id elena-brooks --event interested; eq "$CODE" 1 "a closed thread takes nothing"
has "$ERR" "ended for good" "ended"
c 2026-11-04 reply --id priya-raman --body x; has "$ERR" "closer.py: priya-raman is passed: a reply answers a conversation they started" "no reply on a pass"
c 2026-11-02 reply --id maya-chen; eq "$ERR" "closer.py: the reply is empty: give --body or --body-file" "an empty reply"
c 2026-11-02 reply --id maya-chen --body "Our take rate is TBD"; eq "$ERR" "closer.py: the reply still has a placeholder in it" "a placeholder reply"
c 2026-11-02 reply --id maya-chen --body-file "$TMP/missing-$$.txt"; eq "$CODE" 1 "a missing body file"; has "$ERR" "No such file or directory" "open() raises"
c 2026-11-02 reply --id maya-chen --body-file "$CF/answer.txt"; eq "$CODE" 0 "reply"
has "$OUT" "Drafted a reply to Maya Chen -> s/outreach/maya-chen-2.eml" "the reply is drafted"
send_all 2026-11-02 maya-chen
eq "$(jq -c '.[0]|[.kind,.thread_id,(.subject|startswith("Re: ")),.in_reply_to]' <<<"$ROWS")" "[\"reply\",\"t-maya-chen\",true,\"$INTRO_ID\"]" "the reply goes into the thread"
c 2026-11-02 inbox --id maya-chen --text "Thursday 10am works. Send an invite." --write; eq "$CODE" 0 "interested"
c 2026-11-02 book --id maya-chen --start "2026-11-05 10:00" --tz America/New_York --link https://meet.google.com/abc-defg-hij --stamp 2026-11-02T15:04:00Z
eq "$CODE" 0 "book"
has "$OUT" "Nothing was sent" "nothing sent"
has "$OUT" "(a calendar MCP server the user added in A.S.T.R.O AI's MCP settings sends the invite)" "A.S.T.R.O AI: the invite goes through a calendar MCP server"
hasnt "$OUT" "connector" "A.S.T.R.O AI: no connectors"
eq "$(sed -n 1p <<<"$OUT")" "Booked: Maya Chen (Northlight Ventures), Thursday, November 5, 2026, 10:00 AM to 10:30 AM America/New_York (UTC-05:00)." "the booked line"
ICS=s/calls/maya-chen.ics
crlf_only "$ICS" && ok || bad "the invite is CRLF only"
eq "$(long_lines "$ICS")" 0 "75 octets"
UNF=$(sed -z 's/\r\n //g' "$ICS")
prop() { tr -d '\r' <<<"$UNF" | grep -a "^$1:" | head -n 1 | cut -d: -f2-; }
eq "$(grep -c 'BEGIN:VEVENT' <<<"$UNF")" 1 "one event"
[[ "$UNF" == $'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:'* ]] && ok || bad "the invite starts with the calendar"
[[ "$UNF" == *$'END:VEVENT\r\nEND:VCALENDAR\r' ]] && ok || bad "the invite ends with the calendar"
[ "$(tail -c 2 "$ICS" | od -An -c | tr -d ' ')" = '\r\n' ] && ok || bad "a CRLF at the end"
eq "$(prop DTSTART) $(prop DTEND) $(prop DTSTAMP)" "20261105T150000Z 20261105T153000Z 20261102T150400Z" "start, end, stamp"
eq "$(prop METHOD)" REQUEST "METHOD"
UID_=$(prop UID)
[[ "$UID_" =~ ^seed-[0-9a-f]{16}@leashly\.example$ ]] && ok || bad "the UID shape ($UID_)"
eq "$(prop SUMMARY)" "Leashly × Northlight Ventures · Intro call" "SUMMARY"
has "$UNF" "ORGANIZER;CN=Casey Morgan:mailto:casey@leashly.example" "ORGANIZER"
has "$UNF" "ATTENDEE;CN=Maya Chen;CUTYPE=INDIVIDUAL;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:maya@northlight.example" "ATTENDEE"
has "$UNF" 'Thursday\, November 5\, 2026\, 10:00 AM to 10:30 AM America/New_York (UTC-05:00)' "the time in words"
eq "$(prop LOCATION)" "https://meet.google.com/abc-defg-hij" "LOCATION"
eq "$(prop DESCRIPTION)" 'Intro call: Maya Chen (Northlight Ventures) and Casey Morgan (Leashly).\nThursday\, November 5\, 2026\, 10:00 AM to 10:30 AM America/New_York (UTC-05:00).\nJoin: https://meet.google.com/abc-defg-hij' "DESCRIPTION"
eq "$(P '.investors[0].call')" "{\"title\":\"Leashly × Northlight Ventures · Intro call\",\"start_utc\":\"20261105T150000Z\",\"end_utc\":\"20261105T153000Z\",\"local\":\"2026-11-05 10:00\",\"tz\":\"America/New_York\",\"offset\":\"-05:00\",\"minutes\":30,\"link\":\"https://meet.google.com/abc-defg-hij\",\"location\":\"https://meet.google.com/abc-defg-hij\",\"uid\":\"$UID_\",\"sequence\":0,\"file\":\"calls/maya-chen.ics\"}" "the call record"
eq "$(P '.investors[0]|[.status,.labels[-1]]')" '["booked","Booked"]' "booked"
c 2026-11-03 book --id maya-chen --start "2026-11-06 10:00" --link https://meet.google.com/abc --stamp 2026-11-03T15:00:00Z; eq "$CODE" 0 "reschedule"
has "$(sed -z 's/\r\n //g' "$ICS")" "UID:$UID_"$'\r\n'"SEQUENCE:1"$'\r\n' "a reschedule keeps the UID and bumps the sequence"
c 2026-11-03 book --id maya-chen --start "2026-11-06 10:00" --link https://meet.google.com/abc --stamp 2026-11-03T15:00:00Z
eq "$(P '.investors[0].call.sequence')" 1 "the same time again keeps the sequence"
c 2026-11-03 status --json; eq "$CODE" 0 "status --json"
eq "$(jq -c .funnel <<<"$OUT")" '{"drafted":6,"sent":6,"replied":5,"interested":3,"booked":1}' "the funnel"
eq "$(jq -c .reply_rate_pct <<<"$OUT")" 83.3 "the reply rate"
eq "$(jq -r '.calls[0].start_utc' <<<"$OUT")" 20261106T150000Z "the call"
eq "$(jq -c '.by_status' <<<"$OUT")" '{"booked":1,"interested":2,"passed":2,"sent":1}' "by status, sorted"
eq "$(jq -c '.calls[0]|[.id,.name,.firm]' <<<"$OUT")" '["maya-chen","Maya Chen","Northlight Ventures"]' "the call names who"
eq "$(jq -c '[.waiting_for_yes,.approved_not_sent,.followups_due]' <<<"$OUT")" '[0,0,0]' "nothing waiting"
c 2026-11-03 status; has "$OUT" "stopped: 2 passed (1 not now)" "stopped"
eq "$OUT" "Leashly outreach, Tue 2026-11-03
  drafted        6
  sent           6
  replied        5  (83% of sent)
  interested     3
  booked         1
  stopped: 2 passed (1 not now)
  waiting for a yes: 0   approved, not sent: 0   follow-ups due today: 0
  call: Maya Chen (Northlight Ventures) 2026-11-06 10:00 America/New_York" "status text"
c 2026-11-03 status --id marcus-lee; eq "$(sed -n 1p <<<"$OUT")" "Marcus Lee, Copperline VC <marcus@copperline.example>: sent (Out of office)" "status of one"
has "$OUT" "  2026-10-26  out of office  out of the office" "history rows: the reply's own date"
has "$OUT" "  next follow-up: 2026-11-10" "the next follow-up"
c 2026-11-03 status --id maya-chen --json; eq "$(jq -r .id <<<"$OUT")" maya-chen "status --id --json"
c 2026-11-10 followup --id marcus-lee; eq "$CODE" 0 "Marcus's follow-up after his return"
has "$OUT" "Drafted follow-up 1 of 2 for Marcus Lee (marcus-lee)" "his first follow-up"

# ================================================================ book refusals (test_book_refusals)
fresh
c 2026-10-26 draft >/dev/null; send_all 2026-10-26
ARGS=(book --id maya-chen --start "2026-11-05 10:00" --link https://meet.google.com/x)
c 2026-10-27 "${ARGS[@]}"; has "$ERR" "only after they replied" "book only after a reply"
c 2026-10-27 log --id maya-chen --event interested >/dev/null
c 2026-10-27 book --id maya-chen --start "2026-10-01 10:00" --link https://meet.google.com/x; eq "$CODE" 1 "past"; has "$ERR" "in the past" "in the past"
eq "$ERR" "closer.py: 2026-10-01 10:00 is in the past (today is 2026-10-27)" "the past message"
c 2026-10-27 book --id maya-chen --start "2026-11-05 10:00" --link http://insecure.example; eq "$CODE" 1 "http"; has "$ERR" "https://" "https only"
c 2026-10-27 "${ARGS[@]}" --minutes 5; eq "$CODE" 1 "5 minutes"; has "$ERR" "book 10 to 240" "10 to 240"
c 2026-10-27 "${ARGS[@]}" --tz Mars/Olympus; eq "$CODE" 1 "Mars"; has "$ERR" "unknown time zone" "unknown zone"
hasnt "$ERR" "Traceback" "no traceback"
c 2026-10-27 book --id maya-chen --start "2026-11-05 10:00"; eq "$ERR" 'closer.py: say where: --link https://meet.google.com/... (or --location "Phone: ...")' "say where"
c 2026-10-27 "${ARGS[@]}" --end 1030; eq "$ERR" "closer.py: --end wants a time like 10:30 (same day, same zone)" "a bad --end"
c 2026-10-27 "${ARGS[@]}" --minutes x; eq "$CODE" 2 "--minutes is an int"
c 2026-10-27 book --id sam-whitaker --start "2026-11-05 10:00" --link https://meet.google.com/x; has "$ERR" "sam-whitaker is sent: book a call only after" "status in the refusal"
c 2026-10-29 followup --due >/dev/null
c 2026-10-29 log --id daniel-okafor --event interested >/dev/null
c 2026-10-29 book --id daniel-okafor --start "2026-11-05 10:00" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
eq "$(P '.investors[1].draft')" null "booking voids a waiting follow-up"
eq "$(P '[.investors[1].history[].event]|index("voided") != null')" true "the follow-up was voided by the reply"
c 2026-10-29 log --id sam-whitaker --event interested >/dev/null
c 2026-10-29 book --id sam-whitaker --start "2026-11-05 10:00" --link https://meet.google.com/x --stamp 2026-11-02T15:04:00Z
eq "$(sed -z 's/\r\n //g' s/calls/sam-whitaker.ics | tr -d '\r' | grep -a '^SUMMARY:')" "SUMMARY:Leashly × Sam Whitaker · Intro call" "an angel is named, not the firm"

# ================================================================ bounce and opt-out (test_bounce_and_optout_close_the_thread_and_the_address)
fresh
c 2026-10-26 draft --top 2 >/dev/null; send_all 2026-10-26
c 2026-10-27 inbox --from "Mail Delivery Subsystem <mailer-daemon@mail.example>" --subject "Delivery Status Notification (Failure)" \
    --text "Address not found: daniel@harborseed.example could not be delivered." --write
has "$OUT" "[daniel-okafor]: BOUNCED" "the bounce finds daniel by the address in the text"
has "$OUT" "recorded: daniel-okafor: Bounced, status bounced. The thread is closed for good." "recorded"
c 2026-10-27 log --id maya-chen --event optout; eq "$OUT" "maya-chen: Opt-out, status optout. Never emailed again." "opt-out"
eq "$(P '[.investors[0].status,.investors[1].status]')" '["optout","bounced"]' "both closed"
c 2026-10-29 due --json; eq "$(jq -c '[.due,.later]' <<<"$OUT")" '[[],[]]' "nothing is due on closed threads"
eq "$(P '.investors[1].touches|length')" 1 "a bounce adds no reply touch"
jq '.investors += [(.investors[0] + {id: "maya-chen-2"})]' "$CF/investors.json" > s/investors.json
c 2026-10-28 draft --ids maya-chen-2; eq "$CODE" 1 "the same address under a new id"
has "$OUT" "never emailed again" "never emailed again"
eq "$OUT" "Drafted 0 intros from Casey Morgan <casey@leashly.example>, Wed 2026-10-28. Nothing is sent.
REFUSED maya-chen-2: maya@northlight.example is on a closed thread (optout): never emailed again" "the refusal"
c 2026-10-28 inbox --text "hi" --from "Someone <SOMEONE@else.example>" --json; eq "$(jq -r '.[0].name' <<<"$OUT")" "Someone <SOMEONE@else.example>" "an unknown sender keeps the From"
c 2026-10-28 inbox --text "Count me in" --from "Daniel <DANIEL@harborseed.example>" --json; eq "$(jq -r '.[0].id' <<<"$OUT")" daniel-okafor "From matches the pipeline address, any case"

# ================================================================ errors (test_errors_exit_1_without_a_traceback)
fresh
for argv in "batch" "status --id nobody" "log --id x --event sent" "due --today 26/10/2026"; do
    read -ra A <<<"$argv"; C "${A[@]}" --dir s; eq "$CODE" 1 "exit 1: $argv"; hasnt "$ERR" "Traceback" "no traceback: $argv"
done
C due --dir s --today 26/10/2026; eq "$ERR" "closer.py: --today wants a date like 2026-10-26, got '26/10/2026'" "a bad --today"
C status --dir s --id nobody; eq "$ERR" "closer.py: nobody is not in the pipeline (draft them first)" "an unknown id"
run seed-closer/closer; eq "$CODE" 1 "no command: help, exit 1"
has "$OUT" "usage: closer.py [-h]" "the usage"
has "$OUT" "    book                write calls/<id>.ics, a calendar invite" "the commands are listed"
has "$OUT" "the approved emails, exactly as Astro sends them" "A.S.T.R.O AI: outbox help"
C draft --bogus; eq "$CODE" 2 "an unknown option"
C outbox --dir s --today 2026-10-26; eq "$CODE" 2 "outbox has no --today"
C draft --dir "$TMP/nowhere-$$"; eq "$CODE" 1 "no config"
eq "$ERR" "closer.py: no config (seed-plan writes it: plan.py init, then fill in your details) at $TMP/nowhere-$$/config.json" "the missing config"
printf '{"startup": ' > s/config.json; C draft --dir s --today 2026-10-26
eq "$ERR" "closer.py: s/config.json is not valid JSON: Expecting value: line 1 column 13 (char 12)" "a broken config"
cp "$CF/config.json" s/; printf '{"investors": 5}' > s/pipeline.json; C draft --dir s --today 2026-10-26
eq "$ERR" "closer.py: s/pipeline.json has no investors list" "a pipeline without a list"
rm s/pipeline.json; printf '{"x": 1}' > s/investors.json; C draft --dir s --today 2026-10-26
eq "$ERR" "closer.py: s/investors.json has no investors list" "investors without a list"
rm s/investors.json; C draft --dir s --today 2026-10-26
eq "$ERR" "closer.py: no investors file (seed-investors writes it: investors.py rank) at s/investors.json" "no investors file"
C status --dir s --today 2026-10-26; eq "$CODE" 0 "status with an empty pipeline"
has "$OUT" "  drafted        0" "zero drafted"
rm s/config.json; C status --dir s --today 2026-10-26 --json; eq "$(jq -c '[.funnel.sent,.reply_rate_pct]' <<<"$OUT")" '[0,0.0]' "status without a config"
jq -c '{investors: [{id: "a", status: "sent"}, {id: "b", status: null}, {id: "c"}]}' <<<'{}' > s/pipeline.json
C status --dir s --today 2026-10-26 --json; eq "$(jq -c .by_status <<<"$OUT")" '{"None":2,"sent":1}' "by_status: str(None) sorts first"

# ================================================================ edge cases: headers in any script, quoted-printable bodies
fresh
jq '.investors = [{id: "jose-garcia", name: "José García", firm: "Ñandú Capital", email: "jose@nandu.example", fit: 70, sources: ["https://nandu.example/team"], personal_line: "You led the €2M round for Patitas, a dog-care app in Madrid, last spring."}]' "$CF/investors.json" > s/investors.json
c 2026-10-26 draft; eq "$CODE" 0 "a name with accents"
EML=s/outreach/jose-garcia-1.eml
LC_ALL=C grep -q $'[\x80-\xff]' "$EML" && bad "the .eml is pure ASCII" || ok
eq "$(grep -a '^To: ' "$EML" | tr -d '\r')" "To: =?utf-8?q?Jos=C3=A9_Garc=C3=ADa?= <jose@nandu.example>" "the To display name is an encoded word"
c 2026-10-26 batch --json; eq "$(jq -r .first.to <<<"$OUT")" "José García <jose@nandu.example>" "the encoded word decodes"
has "$(jq -r .first.body <<<"$OUT")" "You led the €2M round for Patitas" "the quoted-printable body decodes"
has "$(jq -r .first.body <<<"$OUT")" "Hi José," "the first name"
mkcfg '.founder.name = "Zoë Ångström" | .startup.tagline = "Paseos sin sorpresas: tarifas planas y un paseador de respaldo en cada paseo, siempre"'
rm -rf s/pipeline.json s/outreach; c 2026-10-26 draft; eq "$CODE" 0 "an accented sender and a long subject"
eq "$(grep -a '^From: ' "$EML" | tr -d '\r')" "From: =?utf-8?b?Wm/DqyDDhW5nc3Ryw7Zt?= <casey@leashly.example>" "From: base64 when it is 5+ shorter"
c 2026-10-26 batch --json; eq "$(jq -r .first.subject <<<"$OUT")" "Leashly: paseos sin sorpresas: tarifas planas y un paseador de respaldo en cada paseo, siempre" "a long subject folds and unfolds"
LONGEST=$(tr -d '\r' < "$EML" | sed '/^$/q' | awk '{ if (length($0) > m) m = length($0) } END { print m }')
[ "$LONGEST" -le 78 ] && ok || bad "headers fold at 78 ($LONGEST)"

done_ seed-closer

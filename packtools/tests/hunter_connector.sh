#!/usr/bin/env bash
# hunter-connector/emails and hunter-connector/outreach: the assertions of hunter-skill/tests/test_connector.py, run
# against the C++ tools through their command lines (the Python tests call the functions; each call here is the CLI
# command that does the same thing, and the saved pipeline is checked with jq).
source "$(dirname "$0")/lib.sh"
DAY5="$FIX/hunter-connector/pipeline-day5.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
E=hunter-connector/emails
EM=$'\u2014'
FIRST="Hi Maya, Your post about how your team runs design reviews hit home. I used the same approach to rebuild our payments API. I'm a fellow Michigan alum and I'm applying for the Software Engineer role on your team. Would you be open to a 20 minute coffee chat next week? Jordan"
NOTE="Hi Maya, fellow Michigan alum and backend engineer. Would love to connect."
j() { echo "$OUT" | jq -c "$1"; }
g() { run $E guess --json "$@"; }

# ---- names
g --name "Dr. Jordan Reyes Jr." --domain example.com; eq "$(j '.candidates[0].email')" '"jordan.reyes@example.com"' "titles and suffixes are dropped"
g --name "Jordan O'Reyes-Díaz" --domain example.com; eq "$(j '.candidates[0].email')" '"jordan.oreyesdiaz@example.com"' "apostrophe, hyphen and accent folded"
g --name "Dr. María García-López Jr." --domain example.com; eq "$(j '.candidates[0].email')" '"maria.garcialopez@example.com"' "the docstring's example"
g --name "Mr Smith" --domain example.com; eq "$(j '.candidates[0].email')" '"mr.smith@example.com"' "a title is kept when there are only two tokens"
g --name "Maya C." --domain example.com; eq "$(j '[.candidates[].email]')" '["maya@example.com","mayac@example.com","maya.c@example.com"]' "initial only"
eq "$(j '[.candidates[].label] | unique')" '["low"]' "initial only: all low"
has "$(j '.notes[0]')" "full last name" "initial only: the note"
run $E guess --name "   " --domain example.com; eq "$CODE" 2 "no name exits 2"; eq "$ERR" "error: no name given" "no name"
run $E guess --name "123" --domain example.com; eq "$ERR" "error: could not read a first name from '123'" "no letters"
run $E pattern --known "Maya C. <Maya.C@Example.com>"; eq "$OUT" "first.l      fits 1 known address" "a known address, lowercased"
run $E pattern --known "maya.c@example.com"; eq "$CODE" 2 "a bare address exits 2"; eq "$ERR" "error: --known wants 'First Last <address>', got 'maya.c@example.com'" "a bare address"
run $E pattern --known "Maya <not-an-address>"; eq "$ERR" "error: not an email address: 'not-an-address'" "not an address"

# ---- guess
g --name "Jordan Reyes" --domain example.com
eq "$(j '.candidates[0] | [.email, .pattern, .confidence, .label]')" '["jordan.reyes@example.com","first.last",0.36,"medium"]' "first.last first"
eq "$(j '.candidates[1].email')" '"jordan@example.com"' "then first"
eq "$(j '[.candidates[].status] | unique')" '["guessed"]' "every one is guessed"
hasnt "$(echo "$OUT" | tr 'A-Z' 'a-z')" "verified" "never verified"
eq "$(j '[.candidates[].confidence] | . == (sort | reverse)')" true "ranked by confidence"
eq "$(j '.candidates | length')" 12 "twelve patterns"
eq "$(j 'keys_unsorted')" '["name","domain","candidates","learned","notes"]' "result keys"
eq "$(j '.candidates[0] | keys_unsorted')" '["email","pattern","confidence","label","status","why"]' "candidate keys"
eq "$(j '.learned')" null "nothing learned"
g --name "Jordan Reyes" --domain example.com --known "Maya C. <maya.c@example.com>"
eq "$(j '.candidates[0] | [.email, .confidence, .label]')" '["jordan.r@example.com",0.8,"high"]' "a learned pattern wins"
has "$(j '.candidates[0].why')" "learned from 1 known address" "learned why"
eq "$(j '.learned')" '["first.l"]' "learned first.l"
g --name "Maya C." --domain example.com --known "Jordan Reyes <jordan.r@example.com>" --known "Jordan Reyes <jordan.r@example.com>"
eq "$(j '.candidates[0] | [.email, .confidence]')" '["maya.c@example.com",0.9]' "two that agree lift it to 0.90"
has "$(j '.candidates[0].why')" "learned from 2 known addresses at example.com" "two known addresses"
g --name "Jordan Reyes" --domain example.com --known "Maya C. <maya.c@example.org>" --known "Maya C. <careers@example.com>"
eq "$(j '[.learned, (.notes | length), .candidates[0].pattern]')" '[null,2,"first.last"]' "addresses that do not count"
eq "$(j '.notes')" '["maya.c@example.org is at example.org, not example.com: not used","careers@example.com is a shared inbox, not a person: not used"]' "the notes say why"
g --name "Jordan Reyes" --domain example.com --known "Alex Kim <zz@example.com>"; eq "$(j '.notes')" '["zz@example.com fits none of the common patterns for Alex Kim"]' "fits no pattern"
# Jordan Reyes as jreyes (flast) and Alex Kim as alex (first): two patterns tie at one each
g --name "Sam Lee" --domain example.com --known "Jordan Reyes <jreyes@example.com>" --known "Alex Kim <alex@example.com>"
eq "$(j '[.learned, .candidates[0].confidence, .candidates[1].confidence]')" '[["first","flast"],0.4,0.4]' "known addresses that disagree share 0.80"
has "$(j '.candidates[0].why')" "(known addresses disagree)" "disagree why"
run $E guess --name "Jordan Reyes" --domain example.com --found jordan.reyes@example.com; eq "$CODE" 2 "published needs a source"
eq "$ERR" "error: --found needs --found-source: the public URL where the address is published" "the source message"
run $E guess --name "Jordan Reyes" --domain example.com --found x@other.com --found-source https://example.com; eq "$ERR" "error: --found must be an address at example.com" "found at another domain"
g --name "Jordan Reyes" --domain example.com --found jreyes@example.com --found-source https://example.com/team
eq "$(j '.candidates[0] | [.email, .status, .confidence, .pattern, .label, .why]')" '["jreyes@example.com","published",1.0,"flast","published","published at https://example.com/team"]' "a published address leads"
eq "$(j '[.candidates[] | select(.email == "jreyes@example.com")] | length')" 1 "only once"
g --name "Jordan Reyes" --domain "@Example.COM "; eq "$(j '.domain')" '"example.com"' "the domain is cleaned"
# the CLI
run $E guess --name "Jordan Reyes" --domain example.com; eq "$CODE" 0 "guess exits 0"; has "$OUT" "none is verified" "none is verified"; hasnt "$OUT" "$EM" "no em dash"
eq "$(echo "$OUT" | sed -n 2p)" "  jordan.reyes@example.com           0.36  medium    common pattern first.last" "the table row"
eq "$(echo "$OUT" | wc -l | tr -d ' ')" 8 "six rows by default"
run $E guess --name "Jordan Reyes" --domain example.com --top 2; eq "$(echo "$OUT" | wc -l | tr -d ' ')" 4 "--top 2"
run $E guess --name "Jordan Reyes" --domain "not a domain"; eq "$CODE" 2 "a bad domain exits 2"; eq "$ERR" "error: --domain wants the company's email domain, like example.com" "a bad domain"
run $E pattern --known "Jordan Reyes <jreyes@example.com>"; has "$OUT" "flast" "pattern finds flast"; eq "$CODE" 0 "pattern exits 0"
run $E pattern --known "Alex Kim <zz@example.com>"; eq "$CODE" 1 "no pattern exits 1"; eq "$OUT" "note: zz@example.com fits none of the common patterns for Alex Kim" "no pattern note"
run $E; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: emails" "usage"
run $E guess --domain example.com; eq "$CODE" 2 "--name is required"; has "$ERR" "the following arguments are required: --name" "required message"

# ---- outreach: a pipeline with Maya C. at Google and a saved role
O() { run hunter-connector/outreach --pipeline "$PIPE" "$@"; }
fresh() {
  PIPE="$TMP/p$RANDOM.json"
  O contact add --name "Maya C." --role "Engineering Manager" --company Google --email maya.c@example.com --email-confidence 0.36 --source "team page" --research "University of Michigan alum (public profile)"
  jq '.roles += [{"id": "google-swe", "company": "Google", "title": "Software Engineer", "url": "https://example.com/job", "stage": "saved", "applied": null, "next_step": null, "next_date": null, "notes": ""}]' "$PIPE" > "$PIPE.x" && mv "$PIPE.x" "$PIPE"
}
p() { jq -c "$1" "$PIPE"; }
send() {   # send TEXT DATE [CHANNEL] [SUBJECT] [--connected]
  local text=$1 when=$2 ch=${3:-email} subj=${4:-}
  if [ -n "$subj" ]; then O draft --contact maya-c-google --channel "$ch" --text "$text" --subject "$subj" --role google-swe --today "$when" "${@:5}"
  else O draft --contact maya-c-google --channel "$ch" --text "$text" --role google-swe --today "$when" "${@:5}"; fi
  O approve --contact maya-c-google --channel "$ch" --reply yes --today "$when"
  O sent --contact maya-c-google --channel "$ch" --date "$when"
}
th() { p "[.threads[] | select(.channel == \"${2:-email}\")][0] | $1"; }

# rules: yes means yes for this message
fresh; O draft --contact maya-c-google --channel email --subject Hello --text "$FIRST" --today 2026-10-15
for r in "yes" "Yes, send it" "send it" "Yes!" "ok send it" "approve"; do O approve --contact maya-c-google --reply "$r" --today 2026-10-15; eq "$CODE" 0 "a yes: $r"; done
for r in "sure" "yes to all" "yes but change the subject" "no" "send both" "maybe" "" "y"; do O approve --contact maya-c-google --reply "$r" --today 2026-10-15; eq "$CODE" 1 "not a yes: [$r]"; has "$ERR" "error: Not recorded: " "not recorded: [$r]"; done
O approve --contact maya-c-google --reply "sure"; eq "$ERR" "error: Not recorded: 'sure' is not an explicit yes to send: ask \"Send this exact message? yes or no\"" "sure is not explicit"
O approve --contact maya-c-google --reply ""; eq "$ERR" "error: Not recorded: no reply recorded" "an empty reply"
O approve --contact maya-c-google --reply "Yes, but wait"; eq "$ERR" "error: Not recorded: the reply has a condition or covers more than this one message: ask again" "a condition"
O approve --contact maya-c-google --reply "lovely"; eq "$ERR" "error: Not recorded: not an explicit yes: ask again" "not a yes at all"
# research must be professional and sourced
O contact add --name "Sam" --company X --research "Ran the Chicago Marathon last October"; eq "$CODE" 1 "personal research exits 1"
eq "$ERR" "error: personal life ('Marathon'): research uses public professional information only: 'Ran the Chicago Marathon last October'" "personal research"
O contact add --name "Sam" --company Y --research "Posted about how her team runs design reviews (public post)"; eq "$OUT" "added contact sam-y" "sourced research, no warning"
O contact add --name "Sam" --company Z --research "Spoke on a mentoring panel"; eq "$OUT" $'added contact sam-z\nwarning: no source: add the public URL it came from: Spoke on a mentoring panel' "no source warns"
# lint
O contact add --name "Lee" --company L --linkedin https://example.com/in/lee
O draft --contact lee-l --channel linkedin --text "$(printf 'x%.0s' $(seq 201))"; eq "$CODE" 1 "a 201-character invite"
has "$ERR" "200 characters" "the invite limit"; eq "$ERR" "error: a LinkedIn invitation note is at most 200 characters (LinkedIn Help 46662); this one is 201" "the whole message"
O draft --contact lee-l --channel linkedin --text "$NOTE"; eq "$CODE" 0 "the reel's note fits"
eq "$OUT" $'Draft saved (linkedin, first). Show it to the user exactly as below and ask: Send this exact message? yes or no\n'"$NOTE"$'\nwarning: without Premium LinkedIn allows a personal note on 3 invitations a month' "the invite draft"
O draft --contact maya-c-google --channel email --text "$FIRST" --subject ""; eq "$ERR" "error: an email needs a subject" "an email needs a subject"
O draft --contact maya-c-google --channel email --text "   "; eq "$ERR" "error: empty message" "an empty message"
O draft --contact lee-l --channel email --text "Hi"; eq "$ERR" "error: no email for Lee: run emails.py and add it with its confidence" "no email"
O draft --contact nobody --channel email --text "Hi"; eq "$ERR" "error: no contact nobody (see \`outreach.py show\`)" "no contact"
O draft --contact maya-c-google --channel email --text "$FIRST $FIRST $FIRST" --subject "A subject that is far too long to read on a phone screen at all"
has "$OUT" "warning: subject is 63 characters: under 60 reads better on a phone" "a long subject"; has "$OUT" "warning: 156 words: a first note to a busy manager reads best under 150" "too many words"
fresh; send "$FIRST" 2026-10-01 email Hello
O draft --contact maya-c-google --channel email --text "As per my last email, any update." --today 2026-10-04
has "$OUT" "warning: no question: end with one small, specific ask (a 20 minute chat next week?)" "a follow-up without a question"
has "$OUT" "warning: sounds like pressure: a follow-up adds something new or offers an easy out" "pressure"

# the reel
fresh
O draft --contact maya-c-google --channel email --text "$FIRST" --subject "Michigan alum + your design review post" --role google-swe --today 2026-10-15
eq "$(th '[.status, .draft.kind]')" '["drafted","first"]' "the reel: drafted"
eq "$(th '.draft | keys_unsorted')" '["kind","subject","text","created","hash"]' "draft keys"
eq "$(th '.draft.hash | length')" 12 "a 12-character hash"
O sent --contact maya-c-google --date 2026-10-15; eq "$CODE" 1 "no yes yet"; eq "$ERR" "error: no yes recorded for this exact message: show it to the user and run approve first" "no yes message"
O approve --contact maya-c-google --reply "yes to all" --today 2026-10-15; eq "$CODE" 1 "yes to all"
O approve --contact maya-c-google --reply "Yes, send it" --today 2026-10-15; eq "$CODE" 0 "Yes, send it"
has "$OUT" "Recorded your yes for this one message." "recorded"
eq "$(th '.approval | [.reply, .date, (.hash | length)]')" '["Yes, send it","2026-10-15",12]' "the approval"
O sent --contact maya-c-google --date 2026-10-15; eq "$OUT" "Logged as sent. Next follow-up: 2026-10-18" "sent"
eq "$(th '[.status, .next_followup, has("draft"), has("approval")]')" '["sent","2026-10-18",false,false]' "Day 1 -> Day 4"
send "$NOTE" 2026-10-15 linkedin; eq "$OUT" "Logged as sent. A LinkedIn invitation gets no follow-up: if they accept, message them with draft --connected." "the invite is sent"
eq "$(th '[.next_followup, .invite]' linkedin)" '[null,true]' "an invitation gets no follow-up"
O draft --contact maya-c-google --channel linkedin --text "Hi again?" --today 2026-10-16; eq "$ERR" "error: the invitation is still pending: once they accept, draft with --connected" "the invite is pending"
O draft --contact maya-c-google --channel email --text "Any thoughts?" --today 2026-10-17; eq "$ERR" "error: follow-up 1 is not due until 2026-10-18" "not due yet"
send "Hi Maya, here is the one-page design doc from that rebuild. Would a 20 minute chat work?" 2026-10-18
eq "$(th '.next_followup')" '"2026-10-25"' "the second follow-up is 7 days later"
O reply --contact maya-c-google --text "Love that you rebuilt payments that way. Happy to grab a coffee, does Thursday at 10 work?" --date 2026-10-19
eq "$OUT" "Reply logged. Thread is replied. Draft an answer for the user's yes." "the reply"
eq "$(th '[.status, .next_followup]')" '["replied",null]' "replied"
O book --contact maya-c-google --when "2026-10-22 10:00" --where "Google Meet" --reply sure; eq "$CODE" 1 "book needs a yes"
has "$ERR" "error: Not booked: the calendar invite goes to them, so it needs the user's yes (" "not booked"
O book --contact maya-c-google --when "2026-10-22 9:00" --where "Google Meet" --reply yes
eq "$OUT" "Booked: 2026-10-22 09:00, 30 minutes, Google Meet. Create the invite in the user's calendar now (they said yes)." "booked"
eq "$(th '[.status, .booked]')" '["booked",{"when":"2026-10-22 09:00","minutes":30,"where":"Google Meet","approved":"yes"}]' "the booking"
eq "$(p '.roles[0] | [.next_step, .next_date, .stage]')" '["Coffee chat with Maya C. (Google Meet)","2026-10-22","saved"]' "the role's next step; the application is the user's to send"
O due --today 2026-10-20 --json; eq "$(j '[.chats, .close_keys]')" '[["Maya C.: coffee chat 2026-10-22 09:00, Google Meet"],[]]' "the coffee chat is due"
O due --today 2026-10-23 --json; eq "$(j '.chats')" '[]' "a past chat is not listed"
O book --contact maya-c-google --when "Thursday" --reply yes; eq "$ERR" "error: --when wants 'YYYY-MM-DD HH:MM' in the user's time zone" "a bad --when"
# an edited draft needs a new yes
fresh
O draft --contact maya-c-google --channel email --text "$FIRST" --subject Hello --today 2026-10-15
O approve --contact maya-c-google --reply yes --today 2026-10-15
O draft --contact maya-c-google --channel email --text "$FIRST Thanks!" --subject Hello --today 2026-10-15
eq "$(th 'has("approval")')" false "a new draft drops the old yes"
O sent --contact maya-c-google --date 2026-10-15; eq "$CODE" 1 "an edited draft needs a new yes"
O due --today 2026-10-15; has "$OUT" $'Waiting for your yes:\n  - Maya C. at Google (email): first draft waiting for your yes' "due lists it"
O approve --contact maya-c-google --reply yes --today 2026-10-15
O due --today 2026-10-15; has "$OUT" 'Approved, not sent yet:'$'\n''  - Maya C. at Google (email): approved, send it from your own account, then run `sent`' "approved, not sent"
# two follow-ups, then stop
fresh
send "$FIRST" 2026-10-01 email Hello
send "Following up with one more detail. Would a short chat work?" 2026-10-04
send "Last note from me. Would a short chat work?" 2026-10-11
eq "$(th '.next_followup')" null "no third follow-up"
eq "$OUT" "Logged as sent. Next follow-up: none (stop rules)" "none (stop rules)"
O draft --contact maya-c-google --channel email --text "One more?" --today 2026-10-18; eq "$ERR" "error: already 2 follow-ups with no reply: close the thread" "no third"
O due --today 2026-10-18 --json; has "$(j '.close[0]')" "two follow-ups and no reply" "close after two"
eq "$(j '.close_keys')" '[["maya-c-google","email"]]' "the close key"
O due --today 2026-10-17 --json; eq "$(j '.close')" '[]' "not 7 days after the last one yet"
O due --today 2026-10-18 --close; has "$OUT" "Close (stop rules):" "due --close"; eq "$(th '.status')" '"closed"' "closed by due --close"
O draft --contact maya-c-google --channel email --text "Hi?" --today 2026-10-19; eq "$ERR" "error: this thread is closed: the stop rules end it here" "a closed thread"
# twenty-one days of silence
fresh
send "$FIRST" 2026-09-01 email Hello
send "Following up. Would a short chat work?" 2026-09-20
eq "$(th '.next_followup')" null "2026-09-27 would be 26 days after the first message"
O draft --contact maya-c-google --channel email --text "Still keen?" --today 2026-09-25 --early; has "$ERR" "days of silence" "silence ends it"
eq "$ERR" "error: 24 days of silence: no more messages on this thread" "24 days"
O due --today 2026-09-25 --json; has "$(j '.close[0]')" "days of silence" "due says close"
# follow-ups due
fresh; send "$FIRST" 2026-10-01 email Hello
O due --today 2026-10-04; eq "$OUT" $'Outreach on 2026-10-04:\nFollow-ups due:\n  - Maya C. at Google (email): follow-up 1 due 2026-10-04 (3 days since your first message)' "a follow-up is due"
O draft --contact maya-c-google --channel email --text "One detail more: the design doc. Would a chat work?" --today 2026-10-02 --early; eq "$CODE" 0 "--early drafts before it is due"
# a no ends the thread
fresh; send "$FIRST" 2026-10-15 email Hello
O reply --contact maya-c-google --text "Not right now, sorry." --date 2026-10-16
has "$OUT" "That reads as a no." "a no"; eq "$(th '.status')" '"declined"' "declined"
O draft --contact maya-c-google --channel email --text "Maybe next month?" --today 2026-11-20; eq "$CODE" 1 "no more messages"
O reopen --contact maya-c-google; eq "$OUT" "Reopened as replied." "reopen"; eq "$(th '.status')" '"replied"' "replied again"
O reopen --contact maya-c-google; eq "$ERR" "error: only a declined thread can be reopened" "only declined"
O reply --contact maya-c-google --text "ok" --declined; eq "$(th '.status')" '"declined"' "--declined"
# stop is forever, on every channel
fresh; send "$FIRST" 2026-10-15 email Hello; send "$NOTE" 2026-10-15 linkedin
O reply --contact maya-c-google --text "Please unsubscribe me." --date 2026-10-16
eq "$OUT" "They asked to stop. Marked do not contact, forever. Thread closed." "unsubscribe"
eq "$(p '.contacts[0].do_not_contact')" true "do not contact"
eq "$(p '[.threads[].status] | unique')" '["closed","declined"]' "every thread ends"
eq "$(p '.contacts[0].notes')" '"University of Michigan alum (public profile)\n2026-10-16: Do not contact (asked to stop)"' "the note"
O draft --contact maya-c-google --channel linkedin --text "Hi?" --connected --today 2027-06-01; has "$ERR" "asked not to be contacted" "never again"
eq "$ERR" "error: Maya C. asked not to be contacted: no messages, on any channel, ever" "the whole message"
O reopen --contact maya-c-google; eq "$ERR" "error: Maya C. asked not to be contacted: that is final" "no reopen after a stop"
O show; has "$OUT" "DO NOT CONTACT" "show flags it"
O due --today 2026-10-17; eq "$OUT" $'Outreach on 2026-10-17:\nNothing due.' "nothing due for a stopped contact"
O reply --contact maya-c-google --channel linkedin --text "hi" --date 2026-10-17; eq "$CODE" 0 "a reply can still be logged"
# contact rules
fresh
O contact add --name "Maya C." --company Netflix --email maya.c@example.org; eq "$ERR" "error: an email needs --email-confidence (from emails.py); 1.0 only if it is published" "no confidence"
O contact add --name "Maya C." --company Netflix --research "Ran the Chicago Marathon"; eq "$CODE" 1 "personal research"
O contact add --name "Maya C." --company Google; eq "$ERR" "error: contact maya-c-google already exists" "exists"
O contact add --name "Maya C." --company Netflix --email m@example.org --email-confidence 1.5; eq "$ERR" "error: --email-confidence is between 0 and 1" "confidence range"
O contact add --name "Maya C." --company NVIDIA --research "Spoke on a mentoring panel"
eq "$(echo "$OUT" | head -1)" "added contact maya-c-nvidia" "the id"; has "$OUT" "warning:" "it warns"
eq "$(p '.contacts[-1] | keys | sort')" '["company","email","email_confidence","id","linkedin","name","notes","role","source"]' "contact keys"
eq "$(p '.contacts[-1] | [.role, .linkedin, .email, .email_confidence, .source, .notes]')" '[null,null,null,null,null,"Spoke on a mentoring panel"]' "contact values"
O contact add --name "Ana" --company "Acme, Inc." --notes "met at a meetup" --research "Wrote a post on caching (https://example.com/p)"
eq "$(p '.contacts[-1] | [.id, .notes]')" '["ana-acme-inc","met at a meetup\nWrote a post on caching (https://example.com/p)"]' "notes and research joined"
eq "$(head -3 "$PIPE")" $'{\n  "roles": [\n    {' "saved with indent 2"
O contact add --name "Zoë" --company Café; eq "$OUT" "added contact zo-caf" "slug drops non-ASCII"; eq "$(grep -c 'Zoë' "$PIPE")" 1 "saved as UTF-8 (ensure_ascii off)"
O contact stop --contact ana-acme-inc --reason "moved on" --today 2026-10-19; eq "$OUT" "Ana is marked do not contact, forever. Open threads closed." "contact stop"
eq "$(p '.contacts[] | select(.id == "ana-acme-inc") | .notes')" '"met at a meetup\nWrote a post on caching (https://example.com/p)\n2026-10-19: Do not contact (moved on)"' "the stop note"
O sent --contact x --date 2026-13-01; eq "$CODE" 1 "a bad date exits 1"; has "$ERR" "ValueError: month must be in 1..12" "like datetime.date"
O sent --contact x --date "Oct 1"; eq "$ERR" "error: dates are YYYY-MM-DD, got 'Oct 1'" "not a date"
O close --contact nobody; eq "$ERR" "error: no email thread with nobody yet" "close without a thread"
run hunter-connector/outreach --pipeline "$TMP/none.json" show; eq "$OUT" "No contacts yet." "show on an empty pipeline"; [ ! -e "$TMP/none.json" ] && ok || bad "show does not write"
run hunter-connector/outreach --pipeline "$TMP/none.json"; eq "$CODE" 2 "no command exits 2"; has "$OUT" "usage: outreach" "usage"
run hunter-connector/outreach --pipeline "$TMP/none.json" contact; eq "$CODE" 2 "contact without an action exits 2"
# a text file for the draft
fresh; printf '%s\n' "$FIRST" > note.txt
O draft --contact maya-c-google --channel email --file note.txt --subject Hello --today 2026-10-15; eq "$(th '.draft.text')" "$(jq -n --arg t "$FIRST" '$t')" "--file, stripped"

# ---- Day 5 (the reel's pipeline file)
PIPE="$TMP/day5.json"; cp "$DAY5" "$PIPE"
O due --today 2026-10-19 --json
eq "$(j '.reply')" '["Maya C. at Google (email) replied on 2026-10-19: answer them"]' "day 5: reply"
eq "$(j '.close')" '["Maya C. at Netflix (email): 29 days of silence, close it"]' "day 5: close"
eq "$(j '.close_keys')" '[["maya-c-netflix","email"]]' "day 5: close keys"
eq "$(j '.followup')" '[]' "day 5: no follow-ups"
eq "$(j 'keys_unsorted')" '["approve","send","followup","reply","close","chats","close_keys"]' "due keys"
O due --today 2026-10-19 --close; eq "$CODE" 0 "due --close exits 0"; has "$OUT" "They replied:" "They replied"
eq "$(p '[.threads[].status]')" '["replied","sent","closed"]' "netflix closed"
eq "$(p '.roles')" "$(jq -c '.roles' "$DAY5")" "roles untouched"
eq "$(p '._note')" "$(jq -c '._note' "$DAY5")" "other keys kept"
O draft --contact maya-c-google --channel email --text "Thursday at 10 works great. I'll send an invite for Google Meet. Thank you!" --today 2026-10-19
eq "$CODE" 0 "day 5: draft the answer"; has "$OUT" "Send this exact message? yes or no" "the ask"; has "$OUT" "Draft saved (email, reply)" "a reply draft"
O approve --contact maya-c-google --reply sure; eq "$CODE" 1 "sure"; has "$ERR" "Not recorded" "sure is not recorded"
O approve --contact maya-c-google --reply yes; eq "$CODE" 0 "yes"
O sent --contact maya-c-google --date 2026-10-19; eq "$CODE" 0 "sent"
O book --contact maya-c-google --when "2026-10-22 10:00" --where "Google Meet" --reply yes; eq "$CODE" 0 "book"
eq "$(p '.roles[0] | [.next_step, .next_date]')" '["Coffee chat with Maya C. (Google Meet)","2026-10-22"]' "day 5: the role's next step"
O show; has "$OUT" "email    booked" "show: booked"; hasnt "$OUT" "$EM" "show: no em dash"
eq "$(echo "$OUT" | head -1)" "maya-c-google  Maya C., Engineering Manager at Google  maya.c@example.com (guessed, 0.36)" "show: the contact line"
O show --contact maya-c-netflix; eq "$(echo "$OUT" | wc -l | tr -d ' ')" 2 "show one contact"
O contact stop --contact maya-c-netflix --today 2026-10-19; has "$OUT" "forever" "contact stop"
done_ "hunter-connector"

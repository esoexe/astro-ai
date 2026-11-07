#!/usr/bin/env bash
# hunter-followup/followup: the assertions of hunter-skill/tests/test_followup.py, run against the C++ tool, plus
# cases worked out by hand from the rules in followup.py. Fixtures: the skill's templates.json (skill/), the upstream
# pipeline-day1.json, the tracker's pipeline.example.json and the connector's pipeline-day5.json, unmodified.
# The pipelines edited below with jq are synthetic.
source "$(dirname "$0")/lib.sh"
D="$FIX/hunter-followup"; SK="$D/skill"; DAY1="$D/pipeline-day1.json"; EX="$D/pipeline.example.json"; DAY5="$FIX/hunter-connector/pipeline-day5.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
A() { run --skill-dir "$SK" hunter-followup/followup "$@"; }
F() { A --file p.json "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
fresh() { cp "${1:-$DAY1}" p.json; }
edit() { jq "$1" p.json > p.tmp && mv p.tmp p.json; }
pj() { jq -c "$1" p.json; }
state() { F due --today "$1" --json; j '.people[0].state'; }

# ---- only an explicit yes for this one message counts
for r in "yes" "Yes." "y" "yes, send it" "YES send the follow-up"; do
  fresh; F record --contact maya-c-google --channel email --text "Floating this back up." --approval "$r" --today 2026-10-15
  eq "$CODE" 0 "yes: [$r]"; done
for pair in "|no reply recorded" "sure|the reply must start with \"yes\"" "ok|the reply must start with \"yes\"" \
  "go ahead|the reply must start with \"yes\"" "yeah|the reply must start with \"yes\"" "yes to all|one yes covers one message; ask again for each" \
  "yes, send them all|one yes covers one message; ask again for each" "yes but tomorrow|the reply has a condition or a doubt in it" \
  "yes?|a question is not a yes" "no|the reply must start with \"yes\"" "not yet|the reply must start with \"yes\"" \
  "yes if she has not replied|the reply has a condition or a doubt in it" "yes, both|one yes covers one message; ask again for each" \
  "   |no reply recorded" "yes, don't|the reply has a condition or a doubt in it" "yes'|the reply must start with \"yes\"" \
  "ye-s|the reply must start with \"yes\"" "YES. Send it to EVERYONE|one yes covers one message; ask again for each"; do
  r="${pair%%|*}"; why="${pair#*|}"
  fresh; F record --contact maya-c-google --channel email --text "Floating this back up." --approval "$r" --today 2026-10-15
  eq "$CODE" 1 "not a yes: [$r]"; eq "$OUT" "Not recorded: $why. Ask again, for this one message." "why not: [$r]"
  eq "$(pj '.threads[0].messages|length')" 1 "nothing written for [$r]"; done
fresh; F record --contact maya-c-google --channel email --text "x" --approval "yes all" --today 2026-10-15
eq "$OUT" "Not recorded: one yes covers one message; ask again for each. Ask again, for this one message." "batch beats doubt"
fresh; F record --contact maya-c-google --channel email --text "x" --approval "$(printf 'y\xc3\xa9s')" --today 2026-10-15
eq "$CODE" 0 "a non-ASCII letter splits the word: y\u00e9s is y + s, and y is a yes"
fresh; F record --contact maya-c-google --channel email --text "x" --approval "$(printf 'n\xc3\xa9 yes')" --today 2026-10-15
eq "$OUT" "Not recorded: the reply must start with \"yes\". Ask again, for this one message." "only [a-z'] make words"
fresh; F record --contact maya-c-google --channel email --text "x" --approval "$(printf 'yes bul\xe2\x84\xaa')" --today 2026-10-15
eq "$OUT" "Not recorded: one yes covers one message; ask again for each. Ask again, for this one message." "a Kelvin sign lowercases to k (bulk)"
fresh; F record --contact maya-c-google --channel email --text "x" --approval "$(printf 'yes \xc5\xbftop')" --today 2026-10-15
eq "$CODE" 0 "a long s stays itself under lower(), so ſtop is not stop"

# ---- the reel: Day 1 email + LinkedIn request, the Day 4 follow-up, the last nudge, then stop
fresh
F due --today 2026-10-14 --json
eq "$(j '.people[0]|[.state,.due,.followup]')" '["waiting","2026-10-15",1]' "day 3: waiting for 10-15"
eq "$(j '.people[0].reason')" '"follow-up 1 of 2 due 2026-10-15"' "waiting reason"
F due --today 2026-10-15 --json
eq "$(j '.people[0]|[.state,.followup,.channel,.followups_sent,.opening]')" '["due",1,"email",0,"2026-10-12"]' "day 4: due by email, the same-day pair is one opening"
eq "$(j '.people[0]|keys_unsorted')" '["contact_id","state","reason","due","followup","channel","role_id","opening","followups_sent","name","company"]' "person keys"
eq "$(j 'keys_unsorted')" '["today","people","applications"]' "report keys"; eq "$(j .applications)" '[]' "no applications"
F due --today 2026-10-15
eq "$CODE" 0 "due rc"
eq "$OUT" "Follow-ups for 2026-10-15 (drafts only: nothing goes out without your yes)

DUE NOW
  Maya C. (Google) by email: follow-up 1 of 2: opened 2026-10-12, no reply
    draft it: followup.py draft --contact maya-c-google --template nudge-1" "due text"
F record --contact maya-c-google --channel email --text "Floating this back up." --approval sure --today 2026-10-15
eq "$CODE" 1 "sure is not a yes"; has "$OUT" "Not recorded" "not recorded"
F record --contact maya-c-google --channel email --text "  Floating this back up.  " --approval yes --today 2026-10-15
eq "$CODE" 0 "recorded"; eq "$OUT" "Recorded the follow-up 1 of 2 to Maya C. by email on 2026-10-15 (next follow-up 2026-10-22)." "record message"
eq "$(pj '[.threads[]|[.channel,.next_followup,(.messages|length),.status]]')" '[["email","2026-10-22",2,"sent"],["linkedin",null,1,"sent"]]' "next nudge on the email thread"
eq "$(pj '.threads[0].messages[1]')" '{"date":"2026-10-15","direction":"out","text":"Floating this back up."}' "the sent text, stripped"
eq "$(tail -c 2 p.json | od -An -c | tr -d ' ')" '}\n' "saved with a trailing newline"
has "$(cat p.json)" '  "roles": [' "saved with indent 2"
eq "$(state 2026-10-21)" '"waiting"' "10-21 waiting"
F due --today 2026-10-22 --json; eq "$(j '.people[0]|[.state,.followup,.followups_sent]')" '["due",2,1]' "10-22: the second nudge is due"
F draft --contact maya-c-google --template nudge-2 --me "Jordan Reyes" --hook "Here is the one-page design doc." --today 2026-10-22
eq "$CODE" 0 "draft rc"
eq "$OUT" "DRAFT (nudge-2). Show it to the user; it goes out only after their yes.

Subject: Re: [fill: subject]

Hi Maya,

One last note from me so I am not cluttering your inbox. Here is the one-page design doc.

If the timing is wrong, I completely understand, and thank you for reading.

Jordan

Fill in before showing: subject" "draft text"
F record --contact maya-c-google --channel email --text "One last note." --approval yes --today 2026-10-22
eq "$OUT" "Recorded the follow-up 2 of 2 to Maya C. by email on 2026-10-22 (no more follow-ups scheduled)." "the last nudge"
eq "$(pj '[.threads[].next_followup]')" '[null,null]' "nothing scheduled"
F due --today 2026-10-23 --json; eq "$(j '.people[0]|[.state,.reason,.due,.followup,.followups_sent]')" '["stopped","2 follow-ups sent with no reply: stop here",null,null,2]' "stopped after two"
F record --contact maya-c-google --channel email --text "A third nudge." --approval yes --today 2026-10-30
eq "$CODE" 1 "no third nudge"; eq "$OUT" "Not recorded: stop rule, 2 follow-ups sent with no reply: stop here." "stop rule message"
F draft --contact maya-c-google --template nudge-1 --today 2026-10-30
eq "$CODE" 2 "no nudge draft after the stop"; eq "$ERR" "followup: stop rule for Maya C.: 2 follow-ups sent with no reply: stop here" "draft stop rule"
F draft --contact maya-c-google --template thank-you --today 2026-10-30
eq "$CODE" 0 "other templates are not nudges"

# ---- nothing after 21 days of silence (day 21 is the last day)
fresh; F record --contact maya-c-google --channel email --text "Floating this back up." --approval yes --today 2026-10-15
edit '.threads[].next_followup=null | .threads[0].next_followup="2026-11-03"'
F due --today 2026-11-03 --json; eq "$(j '.people[0]|[.state,.reason]')" '["stopped","22 days of silence since 2026-10-12: let it go"]' "day 22 stopped"
edit '.threads[0].next_followup="2026-11-02"'
F due --today 2026-11-02 --json; eq "$(j '.people[0]|[.state,.due]')" '["due","2026-11-02"]' "day 21 still due"
F due --today 2026-11-01 --json; eq "$(j '.people[0]|[.state,.reason]')" '["waiting","follow-up 2 of 2 due 2026-11-02"]' "a picked date replaces the cadence"
edit '.threads[0].next_followup="2026-11-03"'
F due --today 2026-10-20 --json; eq "$(j '.people[0]|[.state,.reason]')" '["stopped","the next follow-up would land 22 days into the silence (limit 21): no more"]' "a date past the silence is refused early"
eq "$(state 2026-11-03)" '"stopped"' "day 22"
fresh; F record --contact maya-c-google --channel email --text "Late." --approval yes --today 2026-10-28
eq "$OUT" "Recorded the follow-up 1 of 2 to Maya C. by email on 2026-10-28 (no more follow-ups scheduled)." "a late nudge leaves no room for another"
F due --today 2026-10-29 --json; eq "$(j '.people[0].reason')" '"the next follow-up would land 23 days into the silence (limit 21): no more"' "23 days"
edit '.threads[0].next_followup="2026-10-30" | .threads[1].next_followup="2026-10-29" | .threads[1].invite=true'
F due --today 2026-10-29 --json; eq "$(j '.people[0]|[.state,.due]')" '["waiting","2026-10-30"]' "a pending invite's date is ignored"
fresh; F record --contact maya-c-google --channel email --text "x" --approval yes --today 2026-11-03
eq "$CODE" 1 "22 days without a reply"; eq "$OUT" "Not recorded: stop rule, 22 days of silence since 2026-10-12: let it go." "silence message"

# ---- a reply is your turn; a no is forever
fresh; F reply --contact maya-c-google --channel email --text "  Happy to grab a coffee, does Thursday at 10 work?  " --today 2026-10-16
eq "$CODE" 0 "reply rc"; eq "$OUT" "Logged the reply from Maya C.. Your turn: answer them (no follow-up rules apply to a reply)." "reply message"
eq "$(pj '.threads[0]|[.status,.messages[-1]]')" '["replied",{"date":"2026-10-16","direction":"in","text":"Happy to grab a coffee, does Thursday at 10 work?"}]' "reply logged, stripped"
F due --today 2026-10-16 --json; eq "$(j '.people[0]|[.state,.reason,.channel,.role_id,.opening]')" '["your-turn","they replied on 2026-10-16: answer them (no follow-up rules apply)","email","google-software-engineer",null]' "your turn"
F due --today 2026-10-16
has "$OUT" "YOUR TURN (they replied)
  Maya C. (Google) by email: they replied on 2026-10-16: answer them (no follow-up rules apply)
    draft it: followup.py draft --contact maya-c-google --template confirm-time (if they offered a time)" "your-turn text"
F record --contact maya-c-google --channel email --text "Thursday at 10 works." --approval yes --today 2026-10-16
eq "$OUT" "Recorded the reply to Maya C. by email on 2026-10-16 (next follow-up 2026-10-19)." "answering a reply restarts the cadence"
F due --today 2026-10-19 --json; eq "$(j '.people[0]|[.state,.opening,.followups_sent]')" '["due","2026-10-16",0]' "a new stretch after their reply"
F draft --contact maya-c-google --template confirm-time --day "Thursday at 10" --me Jordan --subject "Michigan alum" --json
eq "$(j .subject)" '"Re: Michigan alum"' "confirm-time subject"; has "$(j .body)" 'Thursday at 10 works great' "confirm-time day"; eq "$(j .missing)" '[]' "nothing missing"
fresh; F reply --contact maya-c-google --channel email --text "Thanks, but I'm not interested." --today 2026-10-16
eq "$OUT" "Logged. Maya C. said no: every thread with them is closed for good. Thank them once only if you want to, then stop." "said no"
eq "$(pj '[.threads[]|[.status,.next_followup]]')" '[["declined",null],["declined",null]]' "every thread declined"
F due --today 2027-01-01 --json; eq "$(j '.people[0]|[.state,.reason]')" '["stopped","said no (email thread declined): never message again"]' "stopped forever"
F record --contact maya-c-google --channel linkedin --text "Hi again" --approval yes --today 2026-12-01
eq "$CODE" 1 "no record after a no"; eq "$OUT" "Not recorded: Maya C. said no (email thread declined). Nothing more goes to them." "no record message"
F draft --contact maya-c-google --template nudge-1 --today 2026-10-20
eq "$CODE" 2 "no draft after a no"; eq "$ERR" "followup: Maya C. said no (email thread declined): nothing is drafted for them, ever" "no draft message"
F draft --contact maya-c-google --template thank-you --today 2026-10-20
eq "$CODE" 2 "no template at all after a no"
F draft --role google-software-engineer --template thank-you --me Jordan --json
eq "$CODE" 0 "a role-only draft has no person to check"; has "$(j .body)" "Hi [fill: first]," "no first name"

# ---- the words that mean no
for t in "No thanks." "Please stop emailing me" "unsubscribe" "We are not hiring right now" "The role has been filled" \
  "Not interested at the moment" "not the right person, sorry" "no, thank you" "Don't contact me" "dont email me" "do nt message" \
  "NOT A GOOD FIT" "We decided to move forward with another candidate" "position is closed" "can't help" "cant help" \
  "no headcount" "no open roles" "not right now" "too busy" "please don't" "take me off your list" "not open to it"; do
  fresh; F reply --contact maya-c-google --channel email --text "$t" --today 2026-10-16; has "$OUT" "said no" "no: [$t]"; done
for t in "Notable work!" "I know the team" "Happy to chat, does Thursday work?" "Nothing to add" "not hiring2" \
  "$(printf 'not\xc2\xa0interested')" "$(printf '\xc3\xa9no thanks')" "notinterested" "unsubscribed" "no thanksgiving plans" "nowhere"; do
  fresh; F reply --contact maya-c-google --channel email --text "$t" --today 2026-10-16; has "$OUT" "Your turn" "not a no: [$t]"; done
for t in "$(printf 'not \xc4\xb1nterested')" "$(printf 'ta\xe2\x84\xaae me off')" "$(printf 'UN\xc5\xbfUB\xc5\xbfCRIBE')" "$(printf 'not \xc4\xb0NTERESTED')"; do
  fresh; F reply --contact maya-c-google --channel email --text "$t" --today 2026-10-16; has "$OUT" "said no" "re.I folds [$t]"; done
fresh; edit '.threads[0].messages += [{"date":"2026-10-16T09:30:00Z","direction":"in","text":"Sorry, I am NOT INTERESTED right now."}]'
F due --today 2026-10-17 --json; eq "$(j '.people[0].reason')" '"said \"NOT INTERESTED\" on 2026-10-16: never message again"' "the words they used, first match"
fresh; edit '.threads[0].messages += [{"date":"2026-10-16","direction":"in","text":"x"},{"date":null,"direction":"in","text":"not \u0131nterested, thanks"}]'
F due --today 2026-10-17 --json; eq "$(j '.people[0].reason')" "$(printf '"said \\"not \xc4\xb1nterested\\" on None: never message again"')" "original text and str(None)"
fresh; edit '.threads[0].messages += [{"date":"2026-10-16","direction":"in","text":5}]'
F due --today 2026-10-17; eq "$CODE" 1 "a non-text reply is a crash, like re.search"

# ---- stop is forever
fresh; F stop --contact maya-c-google --said "Please don't contact me again" --today 2026-10-16
eq "$CODE" 0 "stop rc"; eq "$OUT" "Maya C. is marked do not contact. 2 thread(s) closed. Nothing will be drafted for them again." "stop message"
eq "$(pj '.contacts[0].notes')" '"2026-10-16: DO NOT CONTACT (asked to stop). Honour forever."' "notes"
eq "$(pj '.threads[1].messages[-1]')" '{"date":"2026-10-16","direction":"in","text":"Please don'"'"'t contact me again"}' "their words on the last thread"
F due --today 2030-01-01 --json; eq "$(j '.people[0]|[.state,.reason]')" '["stopped","marked do not contact: never message again"]' "stopped in 2030 (the note is read first)"
edit '.threads=[]'; F due --today 2030-01-01 --json; eq "$(j '.people')" '[]' "no threads: nobody listed"
F draft --contact maya-c-google --template thank-you; eq "$ERR" "followup: Maya C. marked do not contact: nothing is drafted for them, ever" "the note alone stops drafts"
fresh; edit '.contacts[0].notes="met at a conference  \n"'; F stop --contact maya-c-google --today 2026-10-16
eq "$(pj '.contacts[0].notes')" '"met at a conference\n2026-10-16: DO NOT CONTACT (asked to stop). Honour forever."' "notes appended"
eq "$(pj '.threads[1].messages|length')" 1 "no --said, no message"
fresh; edit '.threads=[]'; F stop --contact maya-c-google --said "stop" --today 2026-10-16
eq "$OUT" "Maya C. is marked do not contact. 0 thread(s) closed. Nothing will be drafted for them again." "no threads to close"
fresh; edit '.contacts[0].notes=7'; F stop --contact maya-c-google --today 2026-10-16; eq "$CODE" 1 "notes must be text"

# ---- the connector's flags
fresh; edit '.contacts[0].do_not_contact=true'
F due --today 2026-10-15 --json; eq "$(j '.people[0]|[.state,.reason]')" '["stopped","marked do not contact: never message again"]' "do_not_contact"
edit '.threads[0].draft={"kind":"followup","text":"Hi"}'
F due --today 2026-10-15 --json; eq "$(j '.people[0].reason')" '"marked do not contact: never message again; do NOT send the pending draft"' "a pending draft is named"
fresh; edit '.threads=[.threads[]|select(.channel=="linkedin")] | .threads[0].invite=true'
F due --today 2026-10-20 --json; eq "$(j '.people[0]|[.state,.reason,.channel,.due]')" '["waiting","LinkedIn invitation pending: no follow-up until they accept","linkedin",null]' "invite pending"
fresh; edit '.threads[0].draft={"kind":"followup","subject":null,"text":"Hi Maya, ...","created":"2026-10-15","hash":"abc"}'
F due --today 2026-10-15 --json; eq "$(j '.people[0]|[.state,.reason,.due]')" '["draft","follow-up 1 of 2 drafted, waiting for your yes","2026-10-15"]' "draft waiting"
F due --today 2026-10-13 --json; eq "$(j '.people[0].reason')" '"follow-up 1 of 2 drafted, waiting for your yes (not due until 2026-10-15)"' "drafted early"
F record --contact maya-c-google --channel email --text "Hi Maya, ..." --approval yes --today 2026-10-15
eq "$CODE" 0 "the draft goes out"; eq "$(pj '.threads[0]|has("draft")')" false "the draft object is gone"
fresh; edit '.threads[0].draft={"text":""}'; F due --today 2026-10-15 --json; eq "$(j '.people[0].state')" '"due"' "an empty draft is not pending"
fresh; edit '.threads[0].messages += [{"date":"2026-10-15","direction":"out","text":"pending","draft":true}]'
F due --today 2026-10-15 --json; eq "$(j '.people[0]|[.state,.followups_sent]')" '["draft",0]' "a flagged message is a draft, not a follow-up"
F record --contact maya-c-google --channel email --text "sent version" --approval yes --today 2026-10-15
eq "$(pj '[.threads[0].messages[].text]')" '["Subject: Michigan alum + your design review post\n\nHi Maya, ... Would you be open to a 20 minute coffee chat next week?\n\nJordan","sent version"]' "the flagged draft is replaced"

# ---- a status "drafted" thread is not sent
fresh; edit '.threads=[.threads[0]] | .threads[0].messages=[{"date":"2026-10-12","direction":"out","text":"draft text"}] | .threads[0].status="drafted"'
F due --today 2026-10-12 --json; eq "$(j '.people[0]|[.state,.reason,.channel,.role_id]')" '["draft","first message drafted, waiting for your yes","email","google-software-engineer"]' "drafted, not sent"
F record --contact maya-c-google --channel email --text "the real first message" --approval yes --today 2026-10-13
eq "$OUT" "Recorded the first message to Maya C. by email on 2026-10-13 (next follow-up 2026-10-16)." "first message replaces the drafted one"
eq "$(pj '.threads[0]|[.status,(.messages|map(.text))]')" '["sent",["the real first message"]]' "drafted message dropped"
fresh; edit '.threads=[.threads[0]] | .threads[0].messages=[] | .threads[0].status="sent"'
F due --today 2026-10-12 --json; eq "$(j '.people|length')" 0 "no messages yet: not listed"

# ---- the same answer as the connector on its fixture
cp "$DAY5" p.json; F due --today 2026-10-19 --json
eq "$(j '[.people[]|[.contact_id,.state]]')" '[["maya-c-google","your-turn"],["maya-c-netflix","stopped"]]' "connector day 5"
eq "$(j '.people[1].reason')" '"2 follow-ups sent with no reply: stop here"' "netflix reason"
F due --today 2026-10-19
eq "$OUT" "Follow-ups for 2026-10-19 (drafts only: nothing goes out without your yes)

YOUR TURN (they replied)
  Maya C. (Google) by email: they replied on 2026-10-19: answer them (no follow-up rules apply)
    draft it: followup.py draft --contact maya-c-google --template confirm-time (if they offered a time)

STOPPED (stop rules)
  Maya C. (Netflix) by email: 2 follow-ups sent with no reply: stop here" "day 5 text"

# ---- applications and next steps (the reel's tracker pipeline)
fresh "$EX"; F due --today 2026-10-16 --json
eq "$(j '[.people[]|[.contact_id,.state,.reason]]')" '[["maya-c-google","done","chat booked: no follow-ups, prepare for it"]]' "booked is done"
eq "$(j '[.applications[].role_id]')" '["nvidia-deep-learning-software-engineer"]' "one application"
eq "$(j '.applications[0]')" '{"kind":"application","role_id":"nvidia-deep-learning-software-engineer","company":"NVIDIA","title":"Deep Learning Software Engineer","date":"2026-09-30","what":"applied 2026-09-30 (16 days, no news) and no contact at NVIDIA yet: /hunter-connector finds the hiring manager"}' "the NVIDIA item"
F due --today 2026-10-22 --json
eq "$(j '[.applications[]|[.role_id,.kind,.date]]')" '[["nvidia-deep-learning-software-engineer","application","2026-09-30"],["amazon-software-development-engineer-ii","next-step","2026-10-19"],["meta-software-engineer-product","next-step","2026-10-19"],["google-software-engineer","next-step","2026-10-22"]]' "sorted by date, then role id"
eq "$(j '.applications[3].what')" '"today: Coffee chat with Maya C. (Google Meet, 10:00). Afterwards, a thank-you within 24 hours (template after-coffee)"' "coffee today"
eq "$(j '.applications[1].what')" '"Online assessment was due 2026-10-19 (3 days ago): do it or update the step in the tracker"' "overdue step"
eq "$(j '.applications[3]|keys_unsorted')" '["kind","role_id","company","title","date","what"]' "next-step keys"
F due --today 2026-10-23 --json
eq "$(j '.applications[]|select(.role_id=="google-software-engineer").what')" '"Coffee chat with Maya C. (Google Meet, 10:00) was yesterday: send the thank-you today (template after-coffee)"' "yesterday"
F due --today 2026-10-24 --json
has "$(j '.applications[]|select(.role_id=="google-software-engineer").what')" "was due 2026-10-22 (2 days ago)" "two days on, a plain overdue step"
edit '.roles[0].next_step="Phone SCREEN with the team" | .roles[0].next_date="2026-10-24"'
F due --today 2026-10-24 --json; eq "$(j '.applications[]|select(.role_id=="google-software-engineer").what')" '"today: Phone SCREEN with the team. Afterwards, a thank-you within 24 hours (template thank-you)"' "a screen gets thank-you"
edit '.roles[0].next_step="Screenshots for the portfolio"'
F due --today 2026-10-24 --json; eq "$(j '.applications[]|select(.role_id=="google-software-engineer").what')" '"today: Screenshots for the portfolio"' "\\b keeps screen out of screenshots"
edit '.roles[0].next_step=null'
F due --today 2026-10-24 --json; eq "$(j '.applications[]|select(.role_id=="google-software-engineer").what')" '"today: next step"' "no step text"
edit '.roles[0].stage="rejected"'
F due --today 2026-10-24 --json; eq "$(j '[.applications[].role_id]|index("google-software-engineer")')" null "closed roles are skipped"
F due --today 2026-10-16
eq "$OUT" "Follow-ups for 2026-10-16 (drafts only: nothing goes out without your yes)

DONE
  Maya C. (Google): chat booked: no follow-ups, prepare for it

APPLICATIONS AND NEXT STEPS
  NVIDIA, Deep Learning Software Engineer: applied 2026-09-30 (16 days, no news) and no contact at NVIDIA yet: /hunter-connector finds the hiring manager" "applications text"
F record --contact maya-c-google --channel email --text "See you Thursday." --approval yes --today 2026-10-17
eq "$OUT" "Recorded the message to Maya C. by email on 2026-10-17 (no more follow-ups scheduled)." "a message on a booked thread"
eq "$(pj '.threads[0].status')" '"booked"' "booked stays booked"

# ---- the check-in goes to a known contact
fresh "$EX"; edit '.contacts += [{"id":"maya-c-nvidia","name":"Maya C.","role":"Engineering Manager","company":"nvidia","linkedin":null,"email":null,"email_confidence":null,"source":"test","notes":""}]'
F due --today 2026-10-16 --json
eq "$(j '.applications[0]|[.contact_id,.what]')" '["maya-c-nvidia","applied 2026-09-30 (16 days, no news): one check-in to Maya C. (template application-check-in)"]' "check-in to the known contact (company case ignored)"
eq "$(j '.applications[0]|keys_unsorted')" '["kind","role_id","company","title","date","contact_id","what"]' "application keys"
F due --today 2026-10-09 --json; eq "$(j .applications)" '[]' "9 days: too early"
F due --today 2026-10-10 --json; eq "$(j '.applications|length')" 1 "day 10: due"
F draft --contact maya-c-nvidia --role nvidia-deep-learning-software-engineer --template application-check-in --me "Jordan Reyes" --json
eq "$(j .subject)" '"Deep Learning Software Engineer application, Jordan Reyes"' "check-in subject"
has "$(j .body)" "I applied for the Deep Learning Software Engineer role at NVIDIA on September 30 and" "applied date in words"
eq "$(j .missing)" '["hook"]' "only the hook is missing"; eq "$(j 'keys_unsorted')" '["template","use","subject","body","missing"]' "draft keys"
edit '.contacts[1].do_not_contact=true'
F due --today 2026-10-16 --json; has "$(j '.applications[0].what')" "no contact at NVIDIA yet" "a stopped contact is not offered"
fresh "$EX"; F record --contact maya-c-google --channel email --role nvidia-deep-learning-software-engineer --text "x" --approval yes --today 2026-10-16
eq "$(pj '.threads[-1]|[.role_id,.channel,.status,(.messages|length)]')" '["nvidia-deep-learning-software-engineer","email","sent",1]' "a role that matches no thread gets a new one"
eq "$(pj '.threads[-1]|keys_unsorted')" '["contact_id","role_id","channel","messages","status","next_followup"]' "new thread keys"
F due --today 2026-10-16 --json; eq "$(j .applications)" '[]' "a message after applying covers the check-in"

# ---- drafts
F templates; eq "$CODE" 0 "templates rc"
eq "$(echo "$OUT" | cut -c1-23)" "$(for n in nudge-1 nudge-2 application-check-in thank-you after-coffee confirm-time; do printf '%-22s \n' "$n"; done)" "template names, padded to 22"
has "$OUT" "$(printf '%-22s Second and LAST follow-up' nudge-2)" "use text"
cp "$DAY1" p.json; F draft --contact maya-c-google --template nudge-1 --me "Jordan Reyes" --today 2026-10-15 --json
eq "$CODE" 0 "draft json rc"
eq "$(j .body)" '"Hi Maya,\n\nFloating this back up in case it got buried. [fill: hook]\n\nIf a 20 minute chat about the Software Engineer role is not possible right now, no worries at all.\n\nJordan"' "nudge-1 body"
eq "$(j .missing)" '["hook","subject"]' "missing"; eq "$(j .template)" '"nudge-1"' "template"; has "$(j .use)" "First follow-up" "use"
F draft --contact maya-c-google --template thank-you --me "Jordan Reyes" --json
eq "$(j .missing)" '["hook","one specific thing you discussed"]' "thank-you missing"
has "$(j .body)" "the Software Engineer role at Google after" "company from the role"
F draft --contact maya-c-google --template after-coffee --json
eq "$(j .missing)" '["my_first","one thing they said","the project you talked about","what you will do with it"]' "after-coffee missing"
F draft --contact maya-c-google --template nudge-1 --hook "{first} {role}" --today 2026-10-15 --json
has "$(j .body)" "buried. {first} Software Engineer" "a value is substituted again only by later keys, like str.replace"
echo '{"name": "Jordan Reyes"}' > profile.json
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --profile profile.json --json; has "$(j .body)" '\n\nJordan"' "name from the profile"
mkdir -p hunter; echo '{"name": "Sam Lee"}' > hunter/profile.json
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --json; has "$(j .body)" '\n\nSam"' "default profile path"
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --me Ana --json; has "$(j .body)" '\n\nAna"' "--me wins"
echo 'not json' > bad-profile.json
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --profile bad-profile.json --json; eq "$CODE" 0 "a broken profile is ignored"; has "$(j .missing)" '"my_first"' "no name"
echo '[1]' > list-profile.json
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --profile list-profile.json --json; eq "$CODE" 1 "a list profile crashes (.get on a list)"
echo '[]' > empty-profile.json
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --profile empty-profile.json --json; eq "$CODE" 0 "an empty profile is {}"
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --profile nope.json --json; has "$(j .missing)" '"my_first"' "no profile file"
F draft --template nudge-1 --today 2026-10-15; eq "$CODE" 2 "no target"; eq "$ERR" "followup: give --contact or --role" "give contact or role"
F draft --contact maya-c-google --template nope
eq "$ERR" "followup: no template 'nope' (have: after-coffee, application-check-in, confirm-time, nudge-1, nudge-2, thank-you)" "unknown template"
F draft --contact zed --template nudge-1 --today 2026-10-15; eq "$ERR" "followup: no contact with id 'zed' in the pipeline" "unknown contact"
F draft --role zed --template nudge-1 --today 2026-10-15; eq "$ERR" "followup: no role with id 'zed' in the pipeline" "unknown role"
edit '.contacts[0].name="Zo\u00eb Smith"'
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --json; has "$OUT" "$(printf 'Hi Zo\xc3\xab,')" "draft JSON keeps UTF-8"
F due --today 2026-10-15 --json; has "$OUT" '"name": "Zo\u00eb Smith"' "due JSON escapes like json.dumps"
F draft --contact maya-c-google --template nudge-1 --today 2026-10-15; has "$OUT" "$(printf 'Hi Zo\xc3\xab,')" "text draft"
F record --contact maya-c-google --channel email --text "$(printf 'caf\xc3\xa9')" --approval yes --today 2026-10-15
has "$(cat p.json)" "$(printf '"text": "caf\xc3\xa9"')" "saved without escapes"; has "$(cat p.json)" "$(printf 'Zo\xc3\xab Smith')" "names kept"
edit '.contacts[0].name=null'; F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --json; has "$(j .body)" "Hi there," "no name: there"
edit '.contacts[0].name=5'; F draft --contact maya-c-google --template nudge-1 --today 2026-10-15 --json; eq "$CODE" 1 "a number name crashes (.strip)"

# ---- dates
cp "$DAY1" p.json
for d in "2026-10-15" "2026-10-15T09:30" "2026-10-15 09:30:00" "2026-10-15T09:30:00.123Z" "2026-10-15T09:30:00+02:00" "2026-10-15T09:30-0500" "  2026-10-15  " \
  "$(printf '\xef\xbc\x92\xef\xbc\x90\xef\xbc\x92\xef\xbc\x96-10-15')" "$(printf '\xd9\xa2\xd9\xa0\xd9\xa2\xd9\xa6-\xd9\xa1\xd9\xa0-\xd9\xa1\xd9\xa5')"; do
  F due --today "$d" --json; eq "$(j .today)" '"2026-10-15"' "a stamp: [$d]"; done
for d in "2026-10-15T9:30" "2026/10/15" "26-10-15" "2026-10-15T09:30:00Zx" "2026-10-15T" "tomorrow" "2026-10-15T09:30:00." "2026-10-15T09:30+5"; do
  F due --today "$d"; eq "$CODE" 2 "not a stamp: [$d]"; eq "$ERR" "followup: --today must be YYYY-MM-DD, got '$d'" "message for [$d]"; done
F due --today 2026-13-01; eq "$ERR" "followup: --today is not a real date: '2026-13-01'" "month 13"
F due --today 2025-02-29; eq "$ERR" "followup: --today is not a real date: '2025-02-29'" "no leap day"
F due --today 0000-01-01; eq "$ERR" "followup: --today is not a real date: '0000-01-01'" "year 0"
F due --today 2028-02-29 --json; eq "$(j .today)" '"2028-02-29"' "a leap day"
edit '.threads[0].messages[0].date="2026-10-12T23:59:00-07:00" | .threads[1].messages[0].date="not a date"'
F due --today 2026-10-15 --json; eq "$(j '.people[0]|[.state,.channel,.opening]')" '["due","email","2026-10-12"]' "a timestamp counts by its date; a bad date is skipped"
edit '.threads[0].messages[0].date=20261012 | .threads[1].messages[0].date=null'
F due --today 2026-10-15 --json; eq "$(j .people)" '[]' "undated messages do not count"

# ---- files and arguments
F due --today 2026-10-15; eq "$CODE" 0 "--file before the command"
A due --file p.json --today 2026-10-15; eq "$CODE" 0 "--file after the command (SUPPRESS)"; has "$OUT" "Follow-ups for 2026-10-15" "reads the given file"
A --file nope.json due --file p.json --today 2026-10-15; eq "$CODE" 0 "the command's --file wins"
A due --today 2026-10-15; eq "$CODE" 2 "default file missing"; eq "$ERR" "followup: no pipeline at hunter/pipeline.json (hunter-tracker: pipeline.py init)" "default path"
echo '{bad' > bad.json; A --file bad.json due; eq "$CODE" 2 "bad JSON"
eq "$ERR" "followup: bad.json is not valid JSON: Expecting property name enclosed in double quotes: line 1 column 2 (char 1)" "bad JSON message"
echo '{}' > empty.json; A --file empty.json due --today 2026-10-15
eq "$OUT" "Follow-ups for 2026-10-15 (drafts only: nothing goes out without your yes)

Nothing due. Every thread is waiting on its date or closed." "nothing due"
echo '{"roles":null,"contacts":null,"threads":null}' > nulls.json; A --file nulls.json due --today 2026-10-15 --json
eq "$(j '[.people,.applications]')" '[[],[]]' "nulls are empty lists"
echo '[]' > list.json; A --file list.json due; eq "$CODE" 1 "a list pipeline crashes"
A --file /tmp due; eq "$CODE" 1 "a folder crashes on open"
A --file p.json due --today 2026-10-15 --bogus; eq "$CODE" 2 "unknown option"; has "$ERR" "unrecognized arguments: --bogus" "argparse error"
A --file p.json record --contact maya-c-google --channel email; eq "$CODE" 2 "approval required"; has "$ERR" "the following arguments are required: --approval" "required"
A; eq "$CODE" 2 "no command"; has "$OUT" "usage: followup [-h] [--file FILE]" "usage"; has "$OUT" "pipeline file (default hunter/pipeline.json)" "help"
A due -h; eq "$CODE" 0 "sub help"; hasnt "$OUT" "--file" "the suppressed --file is not listed"; has "$OUT" "--today TODAY" "--today listed"
A templates --file nope.json; eq "$CODE" 0 "templates needs no pipeline"

# ---- reply and record from files and odd input
cp "$DAY1" p.json; printf 'Not interested,\r\nthanks\r\n' > r.txt
F reply --contact maya-c-google --channel email --text-file r.txt --today 2026-10-16; has "$OUT" "said no" "reply from a file"
eq "$(pj '.threads[0].messages[-1].text')" '"Not interested,\nthanks"' "universal newlines, stripped"
cp "$DAY1" p.json; F reply --contact maya-c-google --channel email --today 2026-10-16; eq "$CODE" 2 "no text"; eq "$ERR" "followup: give --text or --text-file" "give text"
F reply --contact maya-c-google --channel email --text-file nope.txt --today 2026-10-16; eq "$CODE" 1 "a missing text file crashes"
F reply --contact maya-c-google --channel phone --text "Call me" --today 2026-10-16
eq "$(pj '.threads[-1]|[.channel,.role_id,.status,.messages[0].direction]')" '["phone",null,"replied","in"]' "a reply on a new channel opens a thread"
eq "$(pj '[.threads[].next_followup]')" '[null,null,null]' "a reply clears every date"
F reply --contact zed --channel email --text "hi" --today 2026-10-16; eq "$ERR" "followup: no contact with id 'zed' in the pipeline" "reply unknown contact"
cp "$DAY1" p.json; F record --contact maya-c-google --channel email --text "   " --approval yes --today 2026-10-15
eq "$CODE" 1 "blank text"; eq "$OUT" "Not recorded: the message text is empty." "blank message"
F record --contact maya-c-google --channel email --approval yes --today 2026-10-15; eq "$OUT" "Not recorded: the message text is empty." "no text"
printf 'From a file.\n' > m.txt
F record --contact maya-c-google --channel email --text-file m.txt --text ignored --approval yes --today 2026-10-15
eq "$(pj '.threads[0].messages[-1].text')" '"From a file."' "--text-file wins over --text"
cp "$DAY1" p.json; F record --contact zed --channel email --text x --approval sure --today 2026-10-15
eq "$CODE" 1 "the yes is checked before the contact"; has "$OUT" "Not recorded" "yes first"
F record --contact zed --channel email --text x --approval yes --today 2026-10-15; eq "$CODE" 2 "then the contact"
edit '.threads[0].messages=null | .threads[0].status="sent"'
F record --contact maya-c-google --channel email --text x --approval yes --today 2026-10-15; eq "$CODE" 1 "messages: null cannot be appended to"
cp "$DAY1" p.json; edit 'del(.threads[0].messages)'
F record --contact maya-c-google --channel email --text x --approval yes --today 2026-10-15; eq "$CODE" 0 "a missing messages list is created"
eq "$(pj '.threads[0]|keys_unsorted')" '["contact_id","role_id","channel","status","next_followup","messages"]' "messages appended at the end"
cp "$DAY1" p.json; edit '.contacts += [{"id":"sam","name":"Sam","company":"Acme"}]'
F record --contact sam --channel linkedin --text "Hi Sam" --approval yes --today 2026-10-15
eq "$OUT" "Recorded the first message to Sam by linkedin on 2026-10-15 (next follow-up 2026-10-18)." "first message to someone new"
F due --today 2026-10-18 --json; eq "$(j '[.people[]|[.contact_id,.state]]')" '[["maya-c-google","due"],["sam","due"]]' "both due, by due date"
F due --today 2026-10-16 --json; eq "$(j '[.people[]|[.contact_id,.state,.due]]')" '[["maya-c-google","due","2026-10-15"],["sam","waiting","2026-10-18"]]' "due before waiting"
edit '.contacts += [{"id":7,"name":"Num"}] | .threads += [{"contact_id":7,"channel":"email","messages":[{"date":"2026-10-14","direction":"out","text":"x"}],"status":"sent"}]'
F due --today 2026-10-18 --json; eq "$CODE" 0 "a number id"; eq "$(j '[.people[]|.contact_id]')" '["maya-c-google",7,"sam"]' "different due dates: no id comparison needed"
F due --today 2026-10-17 --json; eq "$(j '[.people[]|[.contact_id,.state,.due]]')" '[["maya-c-google","due","2026-10-15"],[7,"due","2026-10-17"],["sam","waiting","2026-10-18"]]' "sorted by state, then due date"
cp p.json keep.json
edit '.contacts += [{"id":8,"name":"Eight"}] | .threads += [{"contact_id":8,"channel":"email","messages":[{"date":"2026-10-15","direction":"out","text":"x"}],"status":"sent"}]'
F due --today 2026-10-18 --json; eq "$CODE" 1 "a tie on state and date compares 8 with 'sam': TypeError"
cp keep.json p.json
F record --contact 7 --channel email --text x --approval yes --today 2026-10-17; eq "$ERR" "followup: no contact with id '7' in the pipeline" "--contact is a string; 7 != '7'"
edit '.contacts += [{"id":[1],"name":"L"}]'; F due --today 2026-10-18; eq "$CODE" 1 "a list id is unhashable"

done_ "hunter-followup/followup"

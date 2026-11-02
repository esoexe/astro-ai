#!/usr/bin/env bash
# hunter-tracker/pipeline: the assertions of hunter-skill/tests/test_tracker.py, run against the C++ tool, plus cases
# worked out by hand from the rules in pipeline.py. Fixtures: the upstream pipeline-broken.json, the tracker's
# pipeline.example.json (kept with the hunter-followup fixtures) and the connector's pipeline-day5.json, unmodified.
# The pipelines written below are synthetic.
source "$(dirname "$0")/lib.sh"
EX="$FIX/hunter-followup/pipeline.example.json"; BROKEN="$FIX/hunter-tracker/pipeline-broken.json"; DAY5="$FIX/hunter-connector/pipeline-day5.json"
DAY1="$FIX/hunter-followup/pipeline-day1.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
A() { run hunter-tracker/pipeline "$@"; }
F() { A --file p.json "$@"; }
j() { echo "$OUT" | jq -c "$1"; }
pj() { jq -c "$1" p.json; }
# V '<pipeline json>': validate it; $OUT is the JSON report
V() { printf '%s' "$1" > v.json; A --file v.json validate --json; }
E() { j .errors; }; W() { j .warnings; }
R='{"id":"r","company":"Acme","title":"SWE","url":null,"stage":"saved","applied":null,"next_step":null,"next_date":null,"notes":""}'
C='{"id":"c","name":"Ann","role":null,"company":"Acme","linkedin":null,"email":null,"email_confidence":null,"source":null,"notes":""}'
TH='{"contact_id":"c","role_id":"r","channel":"email","messages":[{"date":"2026-10-12","direction":"out","text":"Hi"}],"status":"sent","next_followup":null}'
role() { jq -cn --argjson r "$R" "\$r + ($1)"; }
contact() { jq -cn --argjson c "$C" "\$c + ($1)"; }
thread() { jq -cn --argjson t "$TH" "\$t + ($1)"; }
P() { printf '{"roles":[%s],"contacts":[%s],"threads":[%s]}' "$1" "$2" "$3"; }

# ---- the fixtures
A --file "$EX" validate; eq "$CODE" 0 "the example is clean"
eq "$OUT" "OK: $EX matches the pipeline schema (5 roles, 1 contact, 2 threads)" "OK line"
A --file "$EX" validate --json; eq "$OUT" '{
  "ok": true,
  "errors": [],
  "warnings": []
}' "no errors, no warnings"
eq "$(jq -c '[.contacts[].name]|unique' "$EX")" '["Maya C."]' "only the reel's fictional people"
A --file "$DAY5" validate --json; eq "$(j '[.ok,.errors,.warnings]')" '[true,[],[]]' "the connector's file: invite and _note are known"
A --file "$BROKEN" validate --json; eq "$CODE" 1 "broken exits 1"
eq "$(E)" '["role google-software-engineer: stage must be one of saved, applied, screen, interview, onsite, offer, rejected, withdrawn, got '"'"'dreaming'"'"'","role google-software-engineer: next_date must be YYYY-MM-DD, got '"'"'2026-13-40'"'"'","role google-software-engineer: duplicate id","contact maya-c-google: email_confidence '"'"'published'"'"' needs the public page where the address is published in '"'"'source'"'"' (a URL); otherwise it is a guess","contact jordan-reyes-brightline: email '"'"'not-an-email'"'"' is not an email address","threads[0]: contact_id '"'"'nobody-at-all'"'"' is not in contacts","threads[0].messages[0]: direction must be out or in, got '"'"'sideways'"'"'","threads[1]: role_id '"'"'no-such-role'"'"' is not in roles","threads[1]: status must be one of drafted, sent, replied, booked, declined, closed, got '"'"'lost'"'"'","threads[1]: next_followup must be YYYY-MM-DD, got '"'"'soon'"'"'"]' "every problem in the broken file, in order"
eq "$(W)" '["role google-software-engineer: next_date without a next_step","role meta-software-engineer-product: url should start with https://","role meta-software-engineer-product: stage applied but no applied date","threads[0]: status sent but no outgoing message","threads[1]: channel '"'"'pigeon'"'"' is not one of email, linkedin, phone, in-person, other"]' "every warning"
A --file "$BROKEN" validate; eq "$CODE" 1 "text exits 1"; has "$OUT" "ERROR  role google-software-engineer: duplicate id" "ERROR lines"
has "$OUT" "WARN   threads[1]: channel 'pigeon'" "WARN lines"; hasnt "$OUT" "OK:" "no OK line"

# ---- the file itself
V '{"roles":{},"contacts":[],"threads":[],"extra":1,"_private":2}'
eq "$(E)" '["roles must be a list"]' "roles must be a list"; eq "$(W)" '["extra top-level field '"'"'extra'"'"' (kept)"]' "extra top-level field, _ is private"
V '{"roles":null}'; eq "$(j '[.ok,.errors,.warnings]')" '[true,[],[]]' "missing and null lists are empty"
V '{"roles":"x","contacts":5}'; eq "$(E)" '["roles must be a list","contacts must be a list"]' "only lists"
V "$(P '5,"x"' '' '')"; eq "$(E)" '["roles[0] must be an object","roles[1] must be an object"]' "roles must be objects"

# ---- roles
V "$(P "$(role '{"id":""}')" '' '')"; eq "$(E)" '["roles[0]: id is required"]' "an empty id keeps the index"
V "$(P "$(role '{"id":7,"title":5,"company":""}')" '' '')"
eq "$(E)" '["role 7: id must be text, got 7","role 7: company is required","role 7: title must be text, got 5"]' "text fields"
V "$(P "$(role '{"stage":null}')" '' '')"; eq "$(E)" '["role r: stage must be one of saved, applied, screen, interview, onsite, offer, rejected, withdrawn, got None"]' "stage None"
V "$(P "$(role '{"stage":"Saved"}')" '' '')"; has "$(E)" "got 'Saved'" "stages are exact"
for d in '"2026-10-12T09:30"' '20261012' '"2026-02-30"' '"2026-10-12 "' '"2026-10-12\n\n"' 'true' '"10/12/2026"'; do
  V "$(P "$(role "{\"applied\":$d}")" '' '')"; eq "$(j '.errors|length')" 1 "applied $d is refused"; has "$(E)" "applied must be YYYY-MM-DD, got " "message for $d"; done
V "$(P "$(role '{"applied":"2026-10-12T09:30"}')" '' '')"; eq "$(E)" '["role r: applied must be YYYY-MM-DD, got '"'"'2026-10-12T09:30'"'"'"]' "a time is only for messages"
V "$(P "$(role '{"applied":true}')" '' '')"; eq "$(E)" '["role r: applied must be YYYY-MM-DD, got True"]' "repr True"
for d in '"2026-10-12"' '"2026-10-12\n"' '"\u0662\u0660\u0662\u0666-\u0661\u0660-\u0661\u0662"' '""' 'null' '"2028-02-29"'; do
  V "$(P "$(role "{\"applied\":$d}")" '' '')"; eq "$(E)" '[]' "applied $d is fine"; done
V "$(P "$(role '{"url":"http://acme.example/jobs"}')" '' '')"; eq "$(W)" '[]' "http is fine"
V "$(P "$(role '{"url":"ftp://acme.example"}')" '' '')"; eq "$(W)" '["role r: url should start with https://"]' "url warning"
V "$(P "$(role '{"url":5}')" '' '')"; eq "$(E)" '["role r: url must be text, got 5"]' "url must be text"; eq "$(W)" '[]' "no url warning for a number"
V "$(P "$(role '{"next_date":"2026-10-20","next_step":""}')" '' '')"; eq "$(W)" '["role r: next_date without a next_step"]' "next_date alone"
V "$(P "$(role '{"stage":"offer"}')" '' '')"; eq "$(W)" '["role r: stage offer but no applied date"]' "offer without applied"
V "$(P "$(role '{"stage":"rejected"}')" '' '')"; eq "$(W)" '[]' "rejected needs no applied date"
V "$(P "$(role '{"salary":1,"it'"'"'s":2}')" '' '')"; eq "$(W)" '["role r: extra field '"'"'salary'"'"' (kept)","role r: extra field \"it'"'"'s\" (kept)"]' "extra fields, repr quotes"
V "$(P "$(role '{"id":1}'),$(role '{"id":1.0}'),$(role '{"id":true}')" '' '')"
eq "$(j '[.errors[]|select(endswith("duplicate id"))]')" '["role 1.0: duplicate id","role True: duplicate id"]' "1 == 1.0 == True, like a Python set"
V "$(P "$(role '{"id":["a"]}')" '' '')"; eq "$CODE" 1 "a list id is unhashable"

# ---- contacts
for e in '"a@b.co"' '"a@b.co\n"' '"a.b+c@d.co.uk"' '"\u00fc@b.de"' '"a@b.c.de"'; do
  V "$(P '' "$(contact "{\"email\":$e,\"email_confidence\":\"low\"}")" '')"; eq "$(E)" '[]' "email $e is fine"; done
for e in '"a b@c.de"' '"a@b@c.de"' '"a@b.c"' '"a@.co"' '"a@b.c0m"' '"a@b.d\u00e9"' '"a@b.de\u00a0"' '"a@b.de\n\n"' '"@b.de"' '"a@b."' '"ab.de"'; do
  V "$(P '' "$(contact "{\"email\":$e,\"email_confidence\":\"low\"}")" '')"; eq "$(j '.errors|length')" 1 "email $e is refused"; has "$(E)" "is not an email address" "message for $e"; done
V "$(P '' "$(contact '{"email":"a b@c.de","email_confidence":"low"}')" '')"; eq "$(E)" '["contact c: email '"'"'a b@c.de'"'"' is not an email address"]' "email message"
V "$(P '' "$(contact '{"email":5}')" '')"; eq "$(E)" '["contact c: email must be text, got 5"]' "email must be text"
eq "$(W)" '["contact c: email without email_confidence (a guess unless it was found published)"]' "a number email still warns"
V "$(P '' "$(contact '{"email":"a@b.co"}')" '')"; eq "$(W)" '["contact c: email without email_confidence (a guess unless it was found published)"]' "a guess unless published"
conf() { V "$(P '' "$(contact "{\"email_confidence\":$1,\"source\":$2}")" '')"; }
conf 0.36 null; eq "$(j '[.errors,.warnings]')" '[[],[]]' "0.36"
conf '"medium"' null; eq "$(E)" '[]' "medium"
conf '" HIGH "' null; eq "$(E)" '[]' "words are stripped and lowercased"
conf 0 null; eq "$(E)" '[]' "0"; conf 1 null; eq "$(E)" '[]' "1"
conf '"published"' '"guessed from the pattern"'; eq "$(E)" '["contact c: email_confidence '"'"'published'"'"' needs the public page where the address is published in '"'"'source'"'"' (a URL); otherwise it is a guess"]' "published needs a URL"
conf '"verified"' null; eq "$(E)" '["contact c: email_confidence '"'"'verified'"'"' needs the public page where the address is published in '"'"'source'"'"' (a URL); otherwise it is a guess"]' "verified means published"
conf '"verified"' '"https://example.com/team"'; eq "$(j '[.errors,.warnings]')" '[[],[]]' "verified with its page"
conf '"VERIFIED"' '"see http://x"'; eq "$(E)" '[]' "http anywhere in the source"
conf 85 null; eq "$(j '[.errors,.warnings]')" '[[],["contact c: email_confidence 85 looks like a percent; use 0 to 1"]]' "85 looks like a percent"
conf 100 null; eq "$(W)" '["contact c: email_confidence 100 looks like a percent; use 0 to 1"]' "100 too"
conf 150 null; eq "$(E)" '["contact c: email_confidence must be from 0 to 1, got 150"]' "150"
conf 100.5 null; eq "$(E)" '["contact c: email_confidence must be from 0 to 1, got 100.5"]' "100.5"
conf -0.1 null; eq "$(E)" '["contact c: email_confidence must be from 0 to 1, got -0.1"]' "negative"
conf true null; eq "$(E)" '["contact c: email_confidence must be a word or a number from 0 to 1, got True"]' "True"
conf '"certain"' null; eq "$(E)" '["contact c: email_confidence must be one of published, high, medium, low, unknown or a number from 0 to 1, got '"'"'certain'"'"'"]' "certain"
conf '[1]' null; eq "$(E)" '["contact c: email_confidence must be one of published, high, medium, low, unknown or a number from 0 to 1, got [1]"]' "a list"
V "$(P '' "$(contact '{"linkedin":"x.com/in/ann"}')" '')"; eq "$(W)" '["contact c: linkedin should be a linkedin.com URL"]' "linkedin warning"
V "$(P '' "$(contact '{"linkedin":"https://www.linkedin.com/in/ann"}')" '')"; eq "$(W)" '[]' "a linkedin URL"
V "$(P '' "$(contact '{"do_not_contact":"yes"}')" '')"; eq "$(E)" '["contact c: do_not_contact must be true or false"]' "do_not_contact bool"
V "$(P '' "$(contact '{"do_not_contact":false,"twitter":"@ann"}')" '')"; eq "$(j '[.errors,.warnings]')" '[[],["contact c: extra field '"'"'twitter'"'"' (kept)"]]' "do_not_contact is known, twitter is extra"
V "$(P '' "$(contact '{"name":null}'),$(contact '{}')" '')"; eq "$(E)" '["contact c: name is required","contact c: duplicate id"]' "contacts: required name, duplicate id"

# ---- threads
V "$(P "$R" "$C" '5')"; eq "$(E)" '["threads[0] must be an object"]' "threads must be objects"
V "$(P "$R" "$C" "$TH")"; eq "$(j '[.errors,.warnings]')" '[[],[]]' "a clean thread"
V "$(P "$R" '{"name":"A","company":"B"}' "$(thread '{"contact_id":null}')")"; eq "$(E)" '["contacts[0]: id is required"]' "a contact without an id lets a thread without one through (None is in the set)"
V "$(P "$R" "$C" "$(thread '{"role_id":""}'),$(thread '{"role_id":null}')")"; eq "$(E)" '[]' "role_id is optional"
eq "$(W)" '[]' "\"\" and None are different keys"
V "$(P "$R" "$C" "$(thread '{"channel":null}'),$(thread '{"channel":5}')")"; eq "$(E)" '["threads[0]: channel is required (email, linkedin, ...)","threads[1]: channel is required (email, linkedin, ...)"]' "channel is required"
V "$(P "$R" "$C" "$TH,$TH")"; eq "$(W)" '["threads[1]: a second thread for the same contact, role and channel"]' "a second thread"
V "$(P "$R" "$C" "$(thread '{"messages":"none"}')")"; eq "$(E)" '["threads[0]: messages must be a list"]' "messages must be a list"
eq "$(W)" '["threads[0]: status sent but no outgoing message"]' "and then counts as empty"
V "$(P "$R" "$C" "$(thread '{"messages":[5,{"direction":"out","text":"x"},{"date":"2026-10-12T9:30","direction":"in","text":5}]}')")"
eq "$(E)" '["threads[0].messages[0] must be an object","threads[0].messages[1]: date is required","threads[0].messages[2]: date must be YYYY-MM-DD, got '"'"'2026-10-12T9:30'"'"'","threads[0].messages[2]: text is required"]' "message checks"
V "$(P "$R" "$C" "$(thread '{"messages":[{"date":"2026-10-12T09:30:00Z","direction":"out","text":"a"},{"date":"2026-10-12 23:59","direction":"in","text":"b","note":1}]}')")"
eq "$(E)" '[]' "message dates may carry a time"; eq "$(W)" '["threads[0].messages[1]: extra field '"'"'note'"'"' (kept)"]' "message extra field"
V "$(P "$R" "$C" "$(thread '{"messages":[{"date":"2026-10-12","direction":"out","text":"a"},{"date":"x","direction":"out","text":"b"},{"date":"2026-10-11","direction":"out","text":"c"}]}')")"
eq "$(W)" '["threads[0].messages[2]: messages are out of date order"]' "order is checked across a bad date"
V "$(P "$R" "$C" "$(thread '{"status":"replied"}'),$(thread '{"channel":"phone","status":"booked","messages":[],"next_followup":"2026-10-20"}')")"
eq "$(W)" '["threads[0]: status replied but no reply logged","threads[1]: status booked but no outgoing message","threads[1]: status booked but no reply logged","threads[1]: next_followup on a booked thread (stop rules: nothing more is sent)"]' "status warnings"
V "$(P "$R" "$C" "$(thread '{"invite":"yes","draft":"x","approval":5,"booked":[]}')")"
eq "$(E)" '["threads[0]: invite must be true or false","threads[0]: draft must be an object","threads[0]: approval must be an object","threads[0]: booked must be an object"]' "connector fields"
eq "$(W)" '[]' "a truthy non-object draft still counts as a draft (no approval warning)"
V "$(P "$R" "$C" "$(thread '{"draft":{}}'),$(thread '{"channel":"linkedin","approval":{"reply":"yes"},"invite":true,"draft":null}')")"
eq "$(E)" '["threads[0]: draft.text is required"]' "draft.text"; eq "$(W)" '["threads[1]: an approval with no draft (it went out or was dropped)"]' "approval without a draft"
V "$(P "$R" "$C" "$(thread '{"approval":{}}')")"; eq "$(W)" '[]' "an empty approval is falsy"
V "$(P "$R" "$C" "$(thread '{"tags":[]}')")"; eq "$(W)" '["threads[0]: extra field '"'"'tags'"'"' (kept)"]' "thread extra field"
V "$(P "$R" "$C" "$(thread '{"contact_id":["c"]}')")"; eq "$CODE" 1 "a list contact_id is unhashable"
V "$(P "$R" "$C" "$(thread '{"channel":["email"]}')")"; eq "$CODE" 1 "a list channel is unhashable in the key"
V "$(P "$(role '{"id":1}')" "$(contact '{"id":2.0}')" "$(thread '{"contact_id":2,"role_id":1.0}')")"; eq "$(j '[.errors[]|select(startswith("threads"))]')" '[]' "ids compare like Python numbers"

# ================================================================ commands
# ---- init
rm -f p.json; F init; eq "$CODE" 0 "init"; eq "$OUT" "Created p.json (roles, contacts, threads: all empty)" "init line"
eq "$(cat p.json; echo .)" '{
  "roles": [],
  "contacts": [],
  "threads": []
}
.' "the empty file, byte for byte"
F init; eq "$CODE" 2 "a second init"; eq "$ERR" "pipeline: p.json already exists; nothing changed" "init refuses"
A --file a/b/c.json init; eq "$CODE" 0 "nested init"; ok_=$( [ -f a/b/c.json ] && echo y ); eq "$ok_" y "the folders are made"
mkdir -p d1 && (cd d1 && $T hunter-tracker/pipeline init >/dev/null); eq "$( [ -f d1/hunter/pipeline.json ] && echo y )" y "default file hunter/pipeline.json"
A init --file sub.json; eq "$OUT" "Created sub.json (roles, contacts, threads: all empty)" "--file after the command"
A --file one.json init --file two.json; eq "$( [ -f two.json ] && [ ! -f one.json ] && echo y )" y "the command's --file wins"

# ---- add-role
rm -f p.json; F add-role --company Acme --title SWE --today 2026-10-11; eq "$CODE" 0 "add-role on a missing file"
eq "$OUT" "Added role acme-swe: Acme, SWE (saved)" "added line"
eq "$(pj '.roles[0]')" '{"id":"acme-swe","company":"Acme","title":"SWE","url":null,"stage":"saved","applied":null,"next_step":null,"next_date":null,"notes":"2026-10-11: added as saved"}' "the role record"
eq "$(jq -c 'keys_unsorted' p.json)" '["roles","contacts","threads"]' "a new file has all three lists"
F add-role --company acme --title swe --today 2026-10-11; eq "$CODE" 1 "same company and title, any case"
eq "$OUT" "Already in the pipeline as acme-swe (stage saved). Use --force to add a second one." "duplicate line"
F add-role --company acme --title swe --today 2026-10-11 --force; eq "$OUT" "Added role acme-swe-2: acme, swe (saved)" "--force: -2"
F add-role --company Acme --title SWE --today 2026-10-11 --force; eq "$OUT" "Added role acme-swe-3: Acme, SWE (saved)" "then -3"
F add-role --company 'AT&T' --title 'C++ / C# Dev' --today 2026-10-11; has "$OUT" "role at-and-t-cpp-csharp-dev:" "slug & + #"
F add-role --company 'Zürich AG' --title 'Ingénieur' --today 2026-10-11; has "$OUT" "role z-rich-ag-ing-nieur:" "slug drops non-ASCII"
F add-role --company '!!!' --title '???' --today 2026-10-11; has "$OUT" "role item:" "an empty slug is item"
F add-role --company 'ÉCOLE' --title 'X' --today 2026-10-11; F add-role --company 'école' --title 'x' --today 2026-10-11; eq "$CODE" 1 "Unicode lowercase match"
F add-role --company New --title Thing --id item --today 2026-10-11; eq "$CODE" 2 "a taken --id"; eq "$ERR" "pipeline: id 'item' is taken" "taken message"
F add-role --company U --title V --url https://x.io/1 --today 2026-10-11; F add-role --company Other --title Role --url https://x.io/1 --today 2026-10-11
eq "$CODE" 1 "same url is a duplicate"; has "$OUT" "Already in the pipeline as u-v (stage saved)" "url duplicate names the first"
F add-role --company Other2 --title Role2 --url '' --today 2026-10-11; eq "$CODE" 0 "an empty --url matches nothing"
eq "$(pj '.roles[-1].url')" '""' "and is kept as given"
F add-role --company S --title T --stage applied --today 2026-10-23; eq "$(pj '.roles[-1]|[.stage,.applied,.notes]')" '["applied","2026-10-23","2026-10-23: added as applied"]' "applied stage stamps today"
F add-role --company S2 --title T --stage offer --applied 2026-09-01 --today 2026-10-23; eq "$(pj '.roles[-1].applied')" '"2026-09-01"' "a given --applied is kept"
F add-role --company S3 --title T --stage applied --applied '' --today 2026-10-23; eq "$(pj '.roles[-1].applied')" '"2026-10-23"' "an empty --applied is replaced"
F add-role --company S4 --title T --stage rejected --today 2026-10-23; eq "$(pj '.roles[-1].applied')" 'null' "a closed stage gets no date"
F add-role --company N --title T --notes '  hi  ' --today 2026-10-11; eq "$(pj '.roles[-1].notes')" '"hi\n2026-10-11: added as saved"' "notes are stripped and stamped"
F add-role --company N2 --title T --stage nope --today 2026-10-11; eq "$CODE" 2 "a bad --stage"; has "$ERR" "invalid choice: 'nope'" "argparse choices"
F add-role --company N3 --title T --next-date 2026-13-01 --today 2026-10-11; eq "$CODE" 2 "a bad --next-date"
eq "$ERR" "pipeline: the change would leave the pipeline invalid:
  role n3-t: next_date must be YYYY-MM-DD, got '2026-13-01'" "the invalid message"
eq "$(pj '[.roles[].id]|index("n3-t")')" 'null' "and nothing was saved"
F add-role --company N4 --title T --today 2026-02-30; eq "$CODE" 2 "a bad --today"; eq "$ERR" "pipeline: --today is not a real date: '2026-02-30'" "--today message"
F add-role --title T --today 2026-10-11; eq "$CODE" 2 "--company is required"; has "$ERR" "the following arguments are required: --company" "required message"
printf '{"roles":[{"id":"a","company":null,"title":"T","stage":"saved"}],"contacts":[],"threads":[]}' > n.json
A --file n.json add-role --company None --title t --today 2026-10-11; eq "$CODE" 1 "a null company reads as None"
printf '{"roles":[{"id":"a","company":"A","title":"T","stage":"dreaming"}]}' > bad.json; cp bad.json bad0.json
A --file bad.json add-role --company B --title T --today 2026-10-11; eq "$CODE" 2 "a broken file blocks a change"
has "$ERR" "role a: stage must be one of" "the old error is reported"; eq "$(cmp -s bad.json bad0.json && echo same)" same "and the file is untouched"
printf '{"threads":null,"x":1}' > ord.json; A --file ord.json add-role --company O --title R --today 2026-10-11
eq "$(jq -c 'keys_unsorted' ord.json)" '["threads","x","roles","contacts"]' "null lists keep their place, missing ones go last"
printf '{"roles":[],"contacts":[],"threads":[]}' > u.json; A --file u.json add-role --company 'Café' --title 'Ünï' --today 2026-10-11
has "$(cat u.json)" '"company": "Café"' "non-ASCII is written raw"

# ---- update-role
rm -f p.json; F add-role --company Acme --title SWE --url https://a.io --next-step Call --next-date 2026-10-20 --today 2026-10-11 >/dev/null
F update-role acme-swe --url '' --title 'Staff SWE' --company 'Acme Inc' --today 2026-10-12; eq "$OUT" "Updated acme-swe: company, title, url" "changed list in field order"
eq "$(pj '.roles[0]|[.company,.title,.url]')" '["Acme Inc","Staff SWE",null]' "\"\" clears to null"
F update-role acme-swe --next-step X --clear-next; eq "$OUT" "Updated acme-swe: next_step, next step cleared" "--clear-next wins"
eq "$(pj '.roles[0]|[.next_step,.next_date]')" '[null,null]' "both cleared"
F update-role acme-swe --notes 'spoke to HR' --today 2026-10-13; eq "$OUT" "Updated acme-swe: notes" "notes"
eq "$(pj '.roles[0].notes')" '"2026-10-11: added as saved\n2026-10-13: spoke to HR"' "a stamped note line"
F update-role acme-swe --notes ''; eq "$CODE" 1 "an empty note is nothing"; eq "$OUT" "Nothing to change." "nothing line"
F update-role acme-swe; eq "$CODE" 1 "no flags"
F update-role acme-swe --company ''; eq "$CODE" 2 "company cannot be cleared"; has "$ERR" "role acme-swe: company is required" "required"
F update-role zz --title x; eq "$CODE" 2 "unknown role"; eq "$ERR" "pipeline: no role with id 'zz'" "unknown message"
F update-role acme-swe --applied 2026-10-01 --next-date 2026-10-30; eq "$OUT" "Updated acme-swe: applied, next_date" "applied and next_date"
A update-role acme-swe --file p.json --next-step Y; eq "$OUT" "Updated acme-swe: next_step" "--file after the command"

# ---- move
rm -f p.json; F add-role --company Acme --title SWE --next-step Call --next-date 2026-10-20 --today 2026-10-11 >/dev/null
F move acme-swe applied --today 2026-10-23; eq "$OUT" "Moved acme-swe: saved -> applied" "moved line"
eq "$(pj '.roles[0]|[.stage,.applied,.next_step,.next_date]')" '["applied","2026-10-23","Call","2026-10-20"]' "applied stamped, next kept"
F move acme-swe screen --today 2026-10-30 --next-date ''; eq "$(pj '.roles[0]|[.applied,.next_date,.next_step]')" '["2026-10-23",null,"Call"]' "applied kept, --next-date '' clears"
F move acme-swe withdrawn --today 2026-11-02; eq "$(pj '.roles[0]|[.next_step,.next_date]')" '[null,null]' "a closed stage clears the next step"
eq "$(pj '.roles[0].notes')" '"2026-10-11: added as saved\n2026-10-23: saved -> applied\n2026-10-30: applied -> screen\n2026-11-02: screen -> withdrawn"' "every move is a note"
F move acme-swe rejected --next-step 'Ask for feedback' --next-date 2026-11-09 --today 2026-11-03; eq "$(pj '.roles[0]|[.next_step,.next_date]')" '["Ask for feedback","2026-11-09"]' "explicit next on a closed stage"
F move acme-swe dreaming; eq "$CODE" 2 "a bad stage"; has "$ERR" "invalid choice: 'dreaming'" "choices"
F move zz saved --today 2026-10-11; eq "$ERR" "pipeline: no role with id 'zz'" "unknown id"
F move acme-swe saved --today x; eq "$CODE" 2 "bad --today"; eq "$ERR" "pipeline: --today must be YYYY-MM-DD, got 'x'" "--today message"
F move acme-swe saved --today ' 2026-11-04T10:00 '; eq "$(pj '.roles[0].notes|split("\n")[-1]')" '"2026-11-04: rejected -> saved"' "--today may carry a time and spaces"
F move acme-swe offer --today '٢٠٢٦-١١-٠٥'; eq "$(pj '.roles[0].notes|split("\n")[-1]')" '"2026-11-05: saved -> offer"' "--today in Arabic-Indic digits"
printf '{"roles":[{"id":"r","company":"A","title":"T","stage":null}]}' > ns.json
A --file ns.json move r saved --today 2026-10-11; eq "$OUT" "Moved r: None -> saved" "an old None stage"
eq "$(jq -c '.roles[0].notes' ns.json)" '"2026-10-11: None -> saved"' "None in the note"

# ---- add-contact / update-contact
rm -f p.json; F add-contact --name 'Ann Lee' --company Acme --role Recruiter --email ann@acme.com --email-confidence 1e-1 --source https://acme.com/team
eq "$CODE" 0 "add-contact"; eq "$OUT" "Added contact ann-lee-acme: Ann Lee, Acme" "added line"
eq "$(jq -c '.contacts[0]|keys_unsorted' p.json)" '["id","name","role","company","linkedin","email","email_confidence","source","notes"]' "contact key order"
eq "$(pj '.contacts[0]|[.email_confidence,.linkedin,.notes]')" '[0.1,null,""]' "1e-1 is 0.1"
F add-contact --name 'ann lee' --company ACME; eq "$CODE" 1 "a duplicate contact"; eq "$OUT" "Already in the pipeline as ann-lee-acme. Use --force to add a second one." "duplicate line"
F add-contact --name 'ann lee' --company ACME --force; eq "$OUT" "Added contact ann-lee-acme-2: ann lee, ACME" "--force"
F add-contact --name B --company C --id ann-lee-acme; eq "$ERR" "pipeline: id 'ann-lee-acme' is taken" "a taken --id"
conf() { rm -f c.json; A --file c.json add-contact --name X --company Y "$@"; }
conf --email-confidence ' 0.5 '; eq "$(jq -c '.contacts[0].email_confidence' c.json)" '0.5' "spaces around a number"
conf --email-confidence '٠.٥'; eq "$(jq -c '.contacts[0].email_confidence' c.json)" '0.5' "Arabic-Indic digits are a number"
conf --email-confidence '1_0'; eq "$CODE" 0 "1_0 is saved"; has "$(cat c.json)" '"email_confidence": 10.0' "as 10.0"
conf --email-confidence '-0'; has "$(cat c.json)" '"email_confidence": -0.0' "-0 is -0.0"
conf --email-confidence 'high'; eq "$(jq -c '.contacts[0].email_confidence' c.json)" '"high"' "a word stays a word"
conf --email-confidence ''; eq "$(jq -c '.contacts[0].email_confidence' c.json)" '""' "an empty value stays empty"
conf --email-confidence nan; eq "$CODE" 2 "nan is refused"; has "$ERR" "contact x-y: email_confidence must be from 0 to 1, got nan" "nan message"
conf --email-confidence inFINity; has "$ERR" "got inf" "infinity"
conf --email-confidence 0x1; has "$ERR" "or a number from 0 to 1, got '0x1'" "hex is a word"
conf --email-confidence Verified; has "$ERR" "email_confidence 'Verified' needs the public page" "verified means published"
conf --email-confidence published --source 'https://y.com/team'; eq "$CODE" 0 "published with a URL"
conf --email nope; eq "$CODE" 2 "a bad email"; has "$ERR" "contact x-y: email 'nope' is not an email address" "email message"
eq "$( [ -f c.json ] && echo y || echo n )" n "nothing written on a refused add"
F update-contact ann-lee-acme --email '' --email-confidence '' --role '' --today 2026-10-12; eq "$OUT" "Updated ann-lee-acme: role, email, email_confidence" "update-contact order"
eq "$(pj '.contacts[0]|[.role,.email,.email_confidence]')" '[null,null,null]' "\"\" clears to null"
F update-contact ann-lee-acme --email-confidence '0.75' --source x --notes 'met at a meetup' --today 2026-10-12; eq "$OUT" "Updated ann-lee-acme: source, email_confidence, notes" "source before confidence"
eq "$(pj '.contacts[0]|[.email_confidence,.notes]')" '[0.75,"2026-10-12: met at a meetup"]' "float and note"
F update-contact ann-lee-acme --name ''; eq "$CODE" 2 "a name is required"
F update-contact ann-lee-acme; eq "$OUT" "Nothing to change." "nothing"; eq "$CODE" 1 "rc 1"
F update-contact zz --name a; eq "$ERR" "pipeline: no contact with id 'zz'" "unknown contact"

# ---- summary
rm -f p.json; F init >/dev/null; F summary --today 2026-10-16
eq "$OUT" 'Pipeline on 2026-10-16: 0 roles (0 active, 0 applied), 0 contacts

By stage:
  (no roles yet)

Next 7 days:
  nothing scheduled' "an empty summary"
A --file "$EX" summary --today 2026-10-16
eq "$OUT" 'Pipeline on 2026-10-16: 5 roles (5 active, 2 applied), 1 contact

By stage:
  saved      3
  applied    1
  screen     1

Conversations: 1 sent, 1 booked

Next 7 days:
  2026-10-19  Amazon, Software Development Engineer II: Online assessment
  2026-10-19  Meta, Software Engineer, Product: Tailor the resume (hunter-resume)
  2026-10-22  Google, Software Engineer: Coffee chat with Maya C. (Google Meet, 10:00)

Stale (no activity and nothing scheduled):
  netflix-senior-software-engineer (Netflix, saved): 22 days idle
  nvidia-deep-learning-software-engineer (NVIDIA, applied): 16 days idle' "the example summary"
A --file "$EX" summary --today 2026-10-16 --stale-days 20; hasnt "$OUT" "nvidia-deep" "--stale-days 20"; has "$OUT" "netflix-senior-software-engineer (Netflix, saved): 22 days idle" "netflix stays"
A --file "$EX" summary --today 2026-10-20; has "$OUT" "  2026-10-19 OVERDUE  Amazon" "overdue in the summary"
A --file "$EX" summary --today 2026-10-16 --json
eq "$(j '[.as_of,.roles,.active,.applied,.contacts,.by_stage.saved,.by_stage.withdrawn,.threads.booked,.threads.drafted]')" '["2026-10-16",5,5,2,1,3,0,1,0]' "summary json counts"
eq "$(j '.by_stage|keys_unsorted')" '["saved","applied","screen","interview","onsite","offer","rejected","withdrawn"]' "stages in order"
eq "$(j '.next[0]')" '{"date":"2026-10-19","kind":"role","id":"amazon-software-development-engineer-ii","what":"Amazon, Software Development Engineer II: Online assessment","overdue":false}' "a next item"
eq "$(j '.stale[1]')" '{"id":"nvidia-deep-learning-software-engineer","company":"NVIDIA","title":"Deep Learning Software Engineer","stage":"applied","last_activity":"2026-09-30","idle_days":16}' "a stale item"
A --file "$EX" summary --stale-days x --today 2026-10-16; eq "$CODE" 2 "--stale-days is an int"; has "$ERR" "invalid int value: 'x'" "int message"

# ---- next
A --file "$EX" next --today 2026-10-22 --days 0
eq "$OUT" '2026-10-19 OVERDUE  Amazon, Software Development Engineer II: Online assessment
2026-10-19 OVERDUE  Meta, Software Engineer, Product: Tailor the resume (hunter-resume)
2026-10-22 TODAY    Google, Software Engineer: Coffee chat with Maya C. (Google Meet, 10:00)' "OVERDUE and TODAY"
A --file "$EX" next --today 2026-10-16; has "$OUT" "2026-10-19          Amazon" "a plain date has eight spaces"
A --file "$EX" next --today 2026-10-01; eq "$OUT" "Nothing scheduled in the next 7 days." "nothing scheduled"
A --file "$EX" next --today 2026-10-01 --days 30; eq "$(echo "$OUT" | wc -l)" 3 "--days 30"
cat > nx.json <<'J'
{"roles":[{"id":"z","company":"Zed","title":"T","stage":"saved","next_date":"2026-10-20"},
 {"id":"gone","company":"G","title":"T","stage":"rejected","next_step":"x","next_date":"2026-10-20"}],
 "contacts":[{"id":"c1","name":"Ann"},{"id":"c2","name":null}],
 "threads":[{"contact_id":"c1","channel":"email","status":"sent","next_followup":"2026-10-20"},
  {"contact_id":"ghost","channel":"linkedin","status":"replied","next_followup":"2026-10-18"},
  {"contact_id":"c2","channel":"phone","status":"drafted","next_followup":"2026-10-20"},
  {"contact_id":"c1","channel":"email","status":"booked","next_followup":"2026-10-20"},
  {"contact_id":"c1","channel":"email","status":"closed","next_followup":"2026-10-20"},
  {"contact_id":"c1","channel":"other","status":"sent","next_followup":"not a date"}]}
J
A --file nx.json next --today 2026-10-19
eq "$OUT" '2026-10-18 OVERDUE  follow up with ghost (linkedin)
2026-10-20          Zed, T: next step
2026-10-20          follow up with Ann (email)
2026-10-20          follow up with None (phone)' "threads: names, ids, None, stop statuses, role before thread"

# ---- stale
cat > st.json <<'J'
{"roles":[{"id":"b-old","company":"B","title":"T","stage":"applied","applied":"2026-09-01"},
 {"id":"a-never","company":"A","title":"T","stage":"saved","notes":"no dates here 2026-10-01: mid-line"},
 {"id":"c-arabic","company":"C","title":"T","stage":"saved","notes":"x\n٢٠٢٦-٠٩-٢٠: note"},
 {"id":"d-thread","company":"D","title":"T","stage":"screen","applied":"2026-08-01"},
 {"id":"e-sched","company":"E","title":"T","stage":"saved","next_date":"2026-10-30"},
 {"id":"f-overdue","company":"F","title":"T","stage":"interview","applied":"2026-09-01","next_date":"2026-10-02"},
 {"id":"g-closed","company":"G","title":"T","stage":"rejected","applied":"2026-01-01"},
 {"id":"h-fresh","company":"H","title":"T","stage":"saved","notes":"2026-10-10: added"}],
 "contacts":[],"threads":[{"role_id":"d-thread","messages":[{"date":"2026-10-12T09:30"},{"date":"bad"}]}]}
J
A --file st.json stale --today 2026-10-16
eq "$OUT" 'a-never  A, T  [saved]  never touched
b-old  B, T  [applied]  last activity 2026-09-01 (45 days)
f-overdue  F, T  [interview]  last activity 2026-09-01 (45 days)
c-arabic  C, T  [saved]  last activity 2026-09-20 (26 days)' "never first, then by idle days and id"
A --file st.json stale --today 2026-10-16 --days 4; has "$OUT" "d-thread  D, T  [screen]  last activity 2026-10-12 (4 days)" "thread messages count"
has "$OUT" "h-fresh  H, T  [saved]  last activity 2026-10-10 (6 days)" "note dates count"
A --file "$EX" stale --today 2026-10-16 --days 20; eq "$OUT" "netflix-senior-software-engineer  Netflix, Senior Software Engineer  [saved]  last activity 2026-09-24 (22 days)" "the example at 20 days"
A --file "$EX" stale --today 2026-09-25; eq "$OUT" "Nothing stale: every active role moved in the last 14 days or has a next step scheduled." "nothing stale"

# ---- export
A --file "$EX" export --what threads; eq "$OUT" $'contact_id,role_id,channel,status,next_followup,messages,first_date,last_date,last_direction\r\nmaya-c-google,google-software-engineer,email,booked,,4,2026-10-12,2026-10-16,out\r\nmaya-c-google,google-software-engineer,linkedin,sent,,1,2026-10-12,2026-10-12,out\r' "threads CSV, CRLF"
A --file "$EX" export; eq "$(echo "$OUT" | head -1)" $'id,company,title,url,stage,applied,next_step,next_date,notes\r' "roles header"
has "$OUT" $'meta-software-engineer-product,Meta,"Software Engineer, Product",' "a comma is quoted"
has "$OUT" $'"Menlo Park, CA. Fit 95.\n2026-10-11: added as saved"\r\n' "a newline is quoted"
A --file "$EX" export --what contacts; has "$OUT" $'maya-c-google,Maya C.,Engineering Manager,Google,,,,' "null contact fields are empty"
cat > ex.json <<'J'
{"roles":[{"id":"q","company":"Say \"hi\"","title":true,"url":1.0,"stage":"saved","applied":"","next_step":0,"notes":"é"}],
 "contacts":[],"threads":[{"contact_id":0,"role_id":null,"channel":"email","status":"sent","messages":[{"direction":"out"},{"date":null,"direction":"in"}]},{"contact_id":"c","messages":null}]}
J
A --file ex.json export; eq "$OUT" $'id,company,title,url,stage,applied,next_step,next_date,notes\r\nq,"Say ""hi""",True,1.0,saved,,0,,é\r' "quotes doubled, True, 1.0, 0 kept"
A --file ex.json export --what threads; eq "$OUT" $'contact_id,role_id,channel,status,next_followup,messages,first_date,last_date,last_direction\r\n,,email,sent,,2,,,in\r\nc,,,,,0,,,\r' "falsy ids are empty, missing dates are empty"
A --file "$EX" export --out out/roles.csv; eq "$OUT" "Wrote out/roles.csv (5 roles)" "--out line"
A --file "$EX" export; eq "$(cat out/roles.csv; echo .)" "$OUT
." "the file matches stdout"
A --file "$EX" export --what threads --out t.csv; eq "$OUT" "Wrote t.csv (2 threads)" "--out with no folder"
A --file "$EX" export --what nope; eq "$CODE" 2 "--what choices"

# ---- show
A --file "$EX" show maya-c-google; eq "$CODE" 0 "show a contact"
eq "$(j '[.contact.id,(.threads|length),(keys_unsorted)]')" '["maya-c-google",2,["contact","threads"]]' "a contact and its threads"
A --file "$EX" show meta-software-engineer-product; eq "$(j '[.role.company,.threads]')" '["Meta",[]]' "a role with no threads"
has "$OUT" '  "threads": []' "indent 2"
eq "$(jq -r '.roles[0].id' u.json)" "caf-n" "Café, Ünï slugs to caf-n"
A --file u.json show caf-n; eq "$CODE" 0 "show by slug id"; has "$OUT" '"company": "Café"' "show prints raw non-ASCII"
A --file "$EX" show zz; eq "$CODE" 2 "unknown id"; eq "$ERR" "pipeline: no role or contact with id 'zz'" "show message"
printf '{"roles":[{"id":"x","company":"R","title":"T","stage":"saved"}],"contacts":[{"id":"x","name":"N","company":"C"}],"threads":[{"contact_id":"x","role_id":"y","channel":"email","messages":[],"status":"drafted"}]}' > sh.json
A --file sh.json show x; eq "$(j '[keys_unsorted,(.threads|length)]')" '[["role","threads"],0]' "a role wins over a contact, threads by role_id"

# ---- load errors and the CLI
A --file nope.json validate; eq "$CODE" 2 "a missing file"; eq "$ERR" "pipeline: no pipeline at nope.json (run: pipeline.py init)" "missing message"
for c in summary stale next "show x" export "update-role x --title y" "move x saved" "update-contact x --name y"; do
  A --file nope.json $c; eq "$CODE" 2 "$c needs the file"; done
printf '[1]' > l.json; A --file l.json validate; eq "$ERR" "pipeline: l.json must hold a JSON object with roles, contacts and threads" "a list file"
printf '{' > j.json; A --file j.json summary; eq "$CODE" 2 "bad JSON"; eq "$ERR" "pipeline: j.json is not valid JSON: Expecting property name enclosed in double quotes: line 1 column 2 (char 1)" "JSON message"
printf '{"roles":null,"contacts":null,"threads":null}' > nul.json; A --file nul.json summary --today 2026-10-16; has "$OUT" "0 roles (0 active, 0 applied), 0 contacts" "null lists read as empty"
A; eq "$CODE" 2 "no command"; has "$OUT" "usage:" "prints help"; has "$OUT" "add-role" "lists the commands"
A nope; eq "$CODE" 2 "an unknown command"; has "$ERR" "invalid choice: 'nope'" "choices"

# ---- upstream test_contacts_and_bad_updates / test_next_marks_overdue
rm -f p.json; F init >/dev/null; F add-contact --name 'Maya C.' --company Google --email-confidence 0.36; eq "$(pj '.contacts[0]|[.id,.email_confidence]')" '["maya-c-google",0.36]' "0.36 is kept"
F update-contact maya-c-google --email-confidence published; eq "$CODE" 2 "published without a source"; has "$ERR" "needs the public page" "refused"
eq "$(pj '.contacts[0].email_confidence')" '0.36' "the file keeps 0.36"
A --file "$EX" next --today 2026-10-21 --days 1; has "$OUT" "OVERDUE" "next marks overdue"

done_ hunter-tracker/pipeline

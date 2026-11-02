#!/usr/bin/env bash
# record-keeper/log: the assertions of closer-skill/record-keeper/test_log.py, run against the C++ tool, plus the CLI.
# Fixture: the skill's pipeline.example.json.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
LG() { run record-keeper/log "$@"; }
cp "$EX" p.json
cat > in.json <<'J'
[{"date": "2026-10-30T10:00", "from": "someone@newbiz.example", "to": ["hello@youragency.studio"], "subject": "Do you build sites?", "text": "Hi"},
 {"date": "2026-10-27T11:48", "from": "Dana Ortiz <dana@harborlanedental.com>", "to": ["hello@youragency.studio"], "subject": null, "text": "Sorry, crazy week! Thursday at 2 works."},
 {"date": "2026-10-30T08:00", "from": "Dana Ortiz <DANA@harborlanedental.com>", "to": ["hello@youragency.studio"], "subject": "Signed!", "text": "  Signed and sent back.\n"},
 {"date": "2026-10-30T09:15:42", "from": "You <hello@youragency.studio>", "to": ["Priya Shah <priya@bluebirdpetspa.com>"], "subject": "Prices", "text": "Here you go."}]
J
# ---- sync: matches, dedupes, classifies
LG --file p.json unknown --emails in.json; eq "$OUT" "someone@newbiz.example" "unknown lists new addresses"; eq "$(jq '.activities | length' p.json)" "$(jq '.activities | length' "$EX")" "unknown saves nothing"
LG --file p.json sync --emails in.json; eq "$CODE" 0 "sync"
eq "$OUT" $'Logged 2 emails, 1 already logged.\n  2026-10-30T08:00 in Signed!\n  2026-10-30T09:15 out Prices\nNot contacts yet (ignored): someone@newbiz.example' "sorted by date; the reel's reply was already logged"
eq "$(jq -c '.activities[-2] | [.direction, .type, .deal_id, .contact_id, .text, .channel, has("approval")]' p.json)" '["in","reply","harbor-lane-website","dana-harbor-lane","Signed and sent back.","email",false]' "signed: an inbound reply on her deal"
eq "$(jq -c '.activities[-1] | [.direction, .type, .approval, .date]' p.json)" '["out","reply","sent by you from your inbox","2026-10-30T09:15"]' "prices: an outbound reply, sent by you"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "still valid"
jq '.[2:4]' in.json > again.json; LG --file p.json sync --emails again.json; eq "$OUT" "Logged 0 emails, 2 already logged." "a second sync logs nothing"
cp "$EX" q.json; echo '[{"date": "2026-10-30T09:00", "from": "hello@youragency.studio", "to": ["luis@redbudfamilychiropractic.com"], "subject": "Quick idea", "text": "Hi Luis", "approval": "yes"}]' > o.json
LG --file q.json sync --emails o.json; eq "$(jq -c '.activities[-1] | [.type, .approval, .deal_id]' q.json)" '["opening","yes","redbud-speed"]' "the first email to a new contact is the opening; its own approval kept"
echo '[{"date": "2026-10-30T09:00", "from": "x@y.example", "to": ["luis@redbudfamilychiropractic.com"], "subject": "Fwd", "text": "x"}]' > f.json
cp "$EX" q.json; LG --file q.json sync --emails f.json; eq "$(jq -c '.activities[-1] | [.direction, .type]' q.json)" '["out","opening"]' "a stranger sending to a contact counts as out"
echo '[{"date": "2026-10-27T11:48", "from": "dana@harborlanedental.com", "to": [], "subject": "Different", "text": "x"}]' > d.json
cp "$EX" q.json; LG --file q.json sync --emails d.json; eq "$OUT" $'Logged 1 emails, 0 already logged.\n  2026-10-27T11:48 in Different' "same minute, both with subjects that differ: a different email"
echo '[]' > e.json; LG --file q.json unknown --emails e.json; eq "$OUT" "Every address is a contact already." "nothing unknown"
echo '{' > bad.json; LG --file q.json sync --emails bad.json; eq "$CODE" 1 "bad json"; has "$ERR" "line 2 column 1" "the json error"

# ---- log by hand
cp "$EX" p.json
LG --file p.json log --contact dana-harbor-lane --channel email --direction in --subject "Re: Quick idea" --text "Thanks" --at 2026-10-30T11:48; eq "$OUT" "Logged email reply for Dana Ortiz" "an inbound email is a reply"
LG --file p.json log --contact luis-redbud --channel email --direction out --text Hi --approval yes --at 2026-10-30T09:00; eq "$OUT" "Logged email opening for Luis Romero" "the first out is the opening"
eq "$(jq -c '.activities[-1] | [.approval, .deal_id, .subject]' p.json)" '["yes","redbud-speed",null]' "approval kept"
LG --file p.json log --contact luis-redbud --channel call --at 2026-10-30T10:00; eq "$OUT" "Logged call call for Luis Romero" "a call is a call"
eq "$(jq -c '.activities[-1] | [.text, .direction, has("approval")]' p.json)" '["",null,false]' "empty text, no approval"
LG --file p.json log --contact luis-redbud --channel note --type winback --deal nope --at 2026-10-30T10:00; eq "$(jq -c '.activities[-1] | [.type, .deal_id]' p.json)" '["winback",null]' "--type wins; an unknown deal is none"
printf '  from a file \n' > t.txt; LG --file p.json log --contact luis-redbud --channel sms --text-file t.txt --at 2026-10-30T10:00; eq "$(jq -r '.activities[-1] | .type + "|" + .text' p.json)" "followup|from a file" "out by default: a followup after the opening"
LG --file p.json log --contact luis-redbud --channel fax --at x; eq "$CODE" 2 "channel choices"
LG --file p.json log --contact nobody --channel email --at x; eq "$ERR" "No contact nobody" "unknown contact"

# ---- calls
printf -- '# Notes\n- Wants class booking\n- Budget ok\n' > n.md
LG --file p.json call --contact hannah-juniper --minutes 22 --notes-file n.md --at 2026-11-03T10:00
eq "$OUT" "Logged the call and saved notes to closer/calls/2026-11-03-juniper-yoga-loft.md" "notes saved"
eq "$(cat closer/calls/2026-11-03-juniper-yoga-loft.md)" $'# Call: Hannah Cole, Juniper Yoga Loft\n\n- When: 2026-11-03 10:00\n- Length: 22 min\n\n# Notes\n- Wants class booking\n- Budget ok' "the notes file"
eq "$(jq -c '.activities[-1] | [.channel, .type, .direction, .subject, .text, .minutes, .notes, .deal_id]' p.json)" '["call","call",null,null,"Call, 22 min. Wants class booking Budget ok",22,"closer/calls/2026-11-03-juniper-yoga-loft.md","juniper-booking"]' "summarised"
W=""; for k in $(seq 50); do W="$W abcd"; done; printf -- '-%s\n' "$W" > long.md; X=""; for k in $(seq 39); do X="$X abcd"; done
LG --file p.json call --contact hannah-juniper --minutes 5 --notes-file long.md --at 2026-11-04T10:00; eq "$(jq -r '.activities[-1].text' p.json)" "Call, 5 min. ${X# }..." "a long summary is cut at a word"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "valid after calls"

# ---- timeline
cp "$EX" p.json; LG --file p.json timeline --contact dana-harbor-lane
has "$OUT" "Dana Ortiz, Harbor Lane Dental" "header"; has "$OUT" "Thursday at 2 works" "her reply"; eq "$(wc -l <<<"$OUT" | tr -d ' ')" 9 "9 lines"
eq "$(sed -n 6p <<<"$OUT")" "  2026-10-27T11:48 <- email    reply     Re: Quick idea for Harbor Lane Dental: Sorry, crazy week! Thursday at 2 works." "the columns"
has "$(sed -n 8p <<<"$OUT")" "  2026-10-29T14:00    call     call      " "no direction: blank arrow"
LG --file p.json timeline --contact nobody; eq "$CODE" 1 "unknown"; eq "$ERR" "No contact nobody" "message"
done_ "closer_log"

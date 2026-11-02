#!/usr/bin/env bash
# pipeline/deals: the assertions of closer-skill/pipeline/test_deals.py, run against the C++ tool, plus the file
# format. Fixture: the skill's pipeline.example.json (the reel's board), unmodified.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
P() { run pipeline/deals "$@"; }
board() { jq "$1" "$EX" > b.json; }

# ---- the schema
P --file "$EX" validate; eq "$CODE" 0 "the example is valid"; eq "$OUT" "OK" "no errors and no warnings"
board 'del(.activities[0].approval)'; P --file b.json validate; eq "$CODE" 1 "invalid exits 1"
has "$OUT" "ERROR activity a1 went out with no recorded approval" "an email out needs an approval"; has "$OUT" "1 errors" "the count"
board '(.deals[] | select(.id=="lakeline-website") | .lost_reason) = null | .activities[0].contact_id = "nobody"'; P --file b.json validate
has "$OUT" "deal lakeline-website is lost with no lost_reason" "lost needs a reason"; has "$OUT" "unknown contact 'nobody'" "ids must resolve"
board '.deals[0].stage = "maybe" | .deals[0].value = -1 | .deals[0].close_date = "Nov 6" | .contacts[0].email = "dana@x" | .contacts[1].id = "Marcus"'; P --file b.json validate
has "$OUT" "stage 'maybe' is not one of lead, contacted, replied, call-booked, proposal, contract-sent, won, lost" "bad stage"
has "$OUT" "deal harbor-lane-website value must be a number >= 0" "negative value"; has "$OUT" "close_date 'Nov 6' must be YYYY-MM-DD" "bad date"
has "$OUT" "contact dana-harbor-lane email 'dana@x' does not look like an email" "bad email"; has "$OUT" "contact id 'Marcus' must be lowercase-with-hyphens" "bad id"
board '.suppression = ["DANA@harborlanedental.com"] | .deals[1].next_step = null'; P --file b.json validate; eq "$CODE" 0 "warnings do not fail"
has "$OUT" "warn  contact dana-harbor-lane is on the suppression list; set do_not_contact" "suppressed contact"; has "$OUT" "warn  deal pine-street-website has no next step" "no next step"
board 'del(.me)'; P --file b.json validate; eq "$OUT" $'ERROR missing top-level key: me\n1 errors' "missing key"
board '.deals[0].stage = "maybe"'; P --file b.json move --deal pine-street-website --stage proposal --today 2026-10-30
eq "$CODE" 1 "save refuses an invalid file"; has "$ERR" "Not saved, the pipeline would be invalid:" "why"; eq "$(jq -r '.deals[1].stage' b.json)" contacted "file unchanged"

# ---- the board
P --file "$EX" board --today 2026-10-29
has "$OUT" "CONTRACT-SENT (1, \$4,788)" "stage header"; has "$OUT" "Harbor Lane Dental" "company"
has "$OUT" "Open: \$19,364 across 5 deals. Won to date: \$2,500." "totals"
eq "$(echo "$OUT" | sed -n 2p)" '  Website speed rebuild        Redbud Family Chiropra    $2,500  next: Research, then first email' "row layout (28/22/9)"
P --file "$EX" board --today 2026-11-20; has "$OUT" "next: Follow-up 2 on Oct 23  [quiet 30 days]" "quiet flag"
P --file "$EX" show --deal harbor-lane-website
eq "$(echo "$OUT" | head -3)" $'Website + Google Ads, monthly  [contract-sent]  $4,788\nDana Ortiz, Harbor Lane Dental  dana@harborlanedental.com\nnext: Dana signs the agreement   close: 2026-11-06' "show header"
has "$OUT" "  2026-10-27T11:48 <- email    Re: Quick idea for Harbor Lane Dental" "an inbound line"; has "$OUT" "  2026-10-29T14:00    call     " "no direction"
P --file "$EX" show --deal nope; eq "$CODE" 1 "unknown deal"; eq "$ERR" "No deal nope" "message"

# ---- move
cp "$EX" m.json
P --file m.json move --deal pine-street-website --stage lost --today 2026-10-30; eq "$CODE" 1 "lost needs a reason"
eq "$ERR" "Say why it was lost (--reason): it is what /win-back reads later." "the message"
P --file m.json move --deal pine-street-website --stage lost --reason budget --today 2026-10-30; eq "$OUT" "Website rebuild is now lost" "moved"
eq "$(jq -c '.deals[1] | [.lost_reason, .next_step]' m.json)" '["budget",null]' "reason kept, next step cleared"
eq "$(jq -c '.activities[-1]' m.json)" '{"id":"a23","date":"2026-10-30","contact_id":"marcus-pine-street","deal_id":"pine-street-website","channel":"note","type":"stage","direction":null,"subject":null,"text":"contacted -> lost: budget"}' "the stage activity"
P --file m.json validate; eq "$CODE" 0 "still valid"
P --file m.json move --deal bluebird-ads --stage maybe; eq "$ERR" "Stage must be one of: lead, contacted, replied, call-booked, proposal, contract-sent, won, lost" "bad stage"

# ---- the CLI end to end (test_cli_init_contact_deal_move)
F="$TMP/closer/pipeline.json"
P --file "$F" init --name You --company "Your Agency" --email hello@youragency.studio --address "1 Main St"; eq "$CODE" 0 "init"; eq "$OUT" "Created $F" "created"
P --file "$F" init; eq "$CODE" 1 "init twice"; eq "$ERR" "$F already exists; nothing changed." "nothing changed"
eq "$(jq -c '.me | keys_unsorted' "$F")" '["name","company","email","address","timezone","phone","offer","price","plan_b","value_line","proof"]' "me keys"
has "$(cat "$F")" $'{\n  "version": 1,\n  "me": {\n    "name": "You",' "indent 2"
P --file "$F" contact --name "Dana Ortiz" --company "Harbor Lane Dental" --email dana@harborlanedental.com; eq "$OUT" "Saved contact dana-ortiz-harbor-lane-dental" "slug id"
P --file "$F" deal --id harbor --contact dana-ortiz-harbor-lane-dental --title Website --value 2500 --next Call --today 2026-10-19; eq "$CODE" 0 "deal"
P --file "$F" move --deal harbor --stage won --today 2026-10-29; eq "$OUT" "Website is now won" "won"
P --file "$F" validate; eq "$CODE" 0 "valid"
eq "$(jq -c '.deals[0] | [.stage, .value, .close_date, .created, .next_step]' "$F")" '["won",2500,"2026-10-29","2026-10-19","Call"]' "value stays an int, won keeps the next step"
eq "$(jq -c '.deals[0] | keys_unsorted' "$F")" '["id","contact_id","title","stage","value","currency","close_date","created","next_step","lost_reason"]' "deal keys"
P --file "$F" deal --id half --contact dana-ortiz-harbor-lane-dental --title Half --value 99.5 --next x --revisit 2027-01-04; eq "$(jq -c '.deals[1] | [.value, .revisit]' "$F")" '[99.5,"2027-01-04"]' "float value, revisit added"
P --file "$F" contact --id "Bad Id" --name x; eq "$CODE" 1 "a bad id is not saved"; has "$ERR" "contact id 'Bad Id' must be lowercase-with-hyphens" "why"
P --file "$F" contact --name "Zoë Ångström" --company "Café #1"; eq "$OUT" "Saved contact zo-ngstr-m-caf-1" "slug drops non-ASCII"
P --file "$F" contact --id zo-ngstr-m-caf-1 --notes "café"; has "$(cat "$F")" '"notes": "café"' "UTF-8 kept"
P --file "$TMP/none.json" board; eq "$CODE" 1 "no file"; eq "$ERR" "No $TMP/none.json yet. Run: deals.py init" "run init"
P --file "$F" deal --id x --today 2026-13-01; eq "$CODE" 1 "a bad date"; has "$ERR" "ValueError" "ValueError"
P; eq "$CODE" 2 "a subcommand is required"
done_ "closer_pipeline"

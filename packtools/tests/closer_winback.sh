#!/usr/bin/env bash
# win-back/winback: the assertions of closer-skill/win-back/test_winback.py, run against the C++ tool, plus the CLI.
# Fixture: the skill's pipeline.example.json (lakeline was lost on Aug 10 for timing; mesa-verde unsubscribed).
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
cp "$EX" p.json
WB() { run win-back/winback "$@"; }
# ---- the example board
WB --file p.json list --today 2026-10-29
eq "$OUT" $'READY  Website + reviews widget       lost 80 days ago: timing: busy season, said ask again in the fall\nNEVER  Google Ads, monthly            do not contact' "lakeline ready, mesa-verde never, the rest skipped"
# ---- sixty days and the revisit date
WB --file p.json list --today 2026-10-08; has "$OUT" "LATER  Website + reviews widget       lost 2026-08-10 (timing: busy season, said ask again in the fall); try from 2026-10-09" "59 days after Aug 10"
WB --file p.json list --today 2026-10-09; has "$OUT" "READY  Website + reviews widget       lost 60 days ago" "60 days"
jq '(.deals[] | select(.id=="lakeline-website") | .revisit) = "2026-11-15"' p.json > r.json
WB --file r.json list --today 2026-10-29; has "$OUT" "LATER  Website + reviews widget       they asked to revisit on 2026-11-15" "revisit later"
WB --file r.json list --today 2026-11-15; has "$OUT" "READY  Website + reviews widget       revisit date 2026-11-15 has come (timing: busy season, said ask again in the fall)" "revisit ready"
# ---- the competitor waits longer
jq '(.deals[] | select(.id=="lakeline-website") | .lost_reason) = "went with another agency"' p.json > c.json
WB --file c.json list --today 2026-10-29; has "$OUT" "LATER  Website + reviews widget       lost 2026-08-10 (went with another agency); try from 2027-02-06" "180 days"
WB --file c.json list --today 2027-02-06; has "$OUT" "READY" "ready after 180"
WB --file c.json draft --deal lakeline-website --today 2027-02-06; eq "$OUT" $'To: tom@lakelineroofingco.com\nSubject: How\'s it going, Tom?\n\nHi Tom, hope the project with the other team went well. If anything is still on your list, I\'m around. If not, no reply needed.\n\nYou' "the competitor draft"
jq '(.deals[] | select(.id=="lakeline-website") | .lost_reason) = "said no"' p.json > n.json; WB --file n.json list --today 2027-02-06; has "$OUT" "NEVER  Website + reviews widget       lost: said no" "a lost reason that means never"
# ---- drafts
WB --file p.json draft --deal lakeline-website --today 2026-10-29
eq "$OUT" $'To: tom@lakelineroofingco.com\nSubject: Checking back in, Tom\n\nHi Tom, when we spoke you said the timing wasn\'t right until the fall. Is now any better for website + reviews widget? If not, no problem at all, just say so and I\'ll leave it.\n\nYou' "the timing draft keeps the easy out"
jq '(.deals[] | select(.id=="lakeline-website") | .lost_reason) = "budget"' p.json > b.json; WB --file b.json draft --deal lakeline-website --today 2026-10-29
has "$OUT" "Subject: A smaller way to start, Tom" "budget subject"; has "$OUT" "We now have \$399 monthly plan instead, with no setup fee, if that makes it easier." "budget uses plan_b"
jq '.me.plan_b = null | (.deals[] | select(.id=="lakeline-website") | .lost_reason) = "price"' p.json > b2.json; WB --file b2.json draft --deal lakeline-website --today 2026-10-29; has "$OUT" "We now have [fill: plan_b]," "plan_b missing"
jq '(.deals[] | select(.id=="lakeline-website") | .lost_reason) = "stalled"' p.json > u.json; WB --file u.json draft --deal lakeline-website --today 2026-10-29; has "$OUT" "Subject: Still on your list, Tom?" "undecided"
WB --file p.json draft --deal mesa-verde-ads --today 2027-01-01; eq "$CODE" 1 "no draft for never"; eq "$ERR" "Not today: do not contact." "why"
WB --file p.json draft --deal nope; eq "$ERR" "No deal nope" "unknown deal"
WB --file p.json draft --deal harbor-lane-website --today 2026-10-29; eq "$ERR" "Not today: still active." "an active deal"
WB --file p.json draft --deal cedar-oak-ordering --today 2026-10-29; eq "$ERR" "Not today: customer." "a customer"
# ---- record: one win-back only
WB --file p.json record --deal lakeline-website --text Hi --approval ok --at 2026-10-29T10:00; eq "$CODE" 1 "needs a yes"
WB --file p.json record --deal mesa-verde-ads --text Hi --approval yes --at 2026-10-29T10:00; eq "$OUT" "Not recorded: do not contact." "never mesa-verde"
WB --file p.json record --deal lakeline-website --text "Hi Tom, [fill: x]" --approval yes --at 2026-10-29T10:00; eq "$OUT" "Not recorded: the message still has a [fill: ...] in it." "placeholders"
WB --file p.json record --deal lakeline-website --text "Hi Tom, checking back in." --approval yes --at 2026-10-29T10:00; eq "$CODE" 0 "recorded"
eq "$OUT" "Logged the win-back. The deal is open again; if they don't reply, that's the end of it." "message"
eq "$(jq -c '.deals[] | select(.id=="lakeline-website") | [.stage, .lost_reason, .next_step]' p.json)" '["contacted",null,"Wait for a reply to the win-back (no follow-up)"]' "reopened"
eq "$(jq -c '.activities[-1] | [.type, .channel, .direction, .subject, .text, .approval, .deal_id]' p.json)" '["winback","email","out",null,"Hi Tom, checking back in.","yes","lakeline-website"]' "the activity"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "valid"
WB --file p.json list --today 2027-06-01; has "$OUT" "NEVER  Website + reviews widget       a win-back already went on 2026-10-29 with no reply: one is the limit" "one is the limit"
jq '.activities += [{id: "z1", date: "2026-11-02T09:00", contact_id: "tom-lakeline", deal_id: "lakeline-website", channel: "email", type: "reply", direction: "in", subject: null, text: "Maybe next spring"}]' p.json > rep.json
eq "$(jq -r '.deals[] | select(.id=="lakeline-website") | .contact_id' p.json)" tom-lakeline "the contact id"
WB --file rep.json list --today 2026-12-01; has "$OUT" "LATER  Website + reviews widget       win-back on 2026-10-29; wait until 2027-01-27" "a reply: 90 days of quiet first"
WB --file rep.json list --today 2027-01-27; hasnt "$OUT" "Website + reviews" "then it is an open deal again: still active"
# ---- quiet open deals after three follow-ups
jq '.activities += [range(3) as $i | {id: ("q\($i)"), date: "2026-09-0\($i + 1)T09:00", contact_id: "luis-redbud", deal_id: "redbud-speed", channel: "email", type: "followup", direction: "out", subject: null, text: "nudge"}]' "$EX" > q.json
WB --file q.json list --today 2026-10-02; hasnt "$OUT" "Website speed" "29 quiet days: not yet"
WB --file q.json list --today 2026-10-03; has "$OUT" "READY  Website speed rebuild          quiet 30 days after the last follow-up" "quiet after the limit"
WB --file q.json draft --deal redbud-speed --today 2026-10-03; has "$OUT" "we talked about website speed rebuild a while back" "undecided draft"
done_ "closer_winback"

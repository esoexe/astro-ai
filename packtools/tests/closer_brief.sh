#!/usr/bin/env bash
# researcher/brief: the assertions of closer-skill/researcher/test_brief.py, run against the C++ tool, plus the CLI.
# Pages: fixtures/closer/pages/harbor.example*.html are test_brief's HOME and ABOUT, served through the test-only
# ASTRO_TOOL_PAGES hook (no /services page, so that fetch fails and is skipped like a dead link).
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"
export ASTRO_TOOL_OFFLINE=1 ASTRO_TOOL_PAGES="$FIX/closer/pages"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
B() { run researcher/brief "$@"; }
B facts --url harbor.example; eq "$CODE" 0 "facts"
F="$OUT"
eq "$(jq -r .title <<<"$F")" "Harbor Lane Dental | Family Dentist in Austin" "title"
eq "$(jq -r .description <<<"$F")" "Gentle family dentistry on Harbor Lane since 2009." "description"
eq "$(jq -c '[.since, .rating.count, .rating.value, .rating.source]' <<<"$F")" '[2009,212,4.8,"https://harbor.example"]' "since and rating"
eq "$(jq -c .phones <<<"$F")" '["(512) 555-0142"]' "phones"; eq "$(jq -c .emails <<<"$F")" '["frontdesk@harborlanedental.com"]' "emails"
eq "$(jq -c .headings <<<"$F")" '["Family dentistry in Austin","Cleanings & checkups","Meet Dr. Ortiz"]' "headings from both pages, unescaped"
eq "$(jq -c '[.tools_seen, .tools_missing]' <<<"$F")" '[[],["booking","ordering","google ads tag","chat widget"]]' "tools"
eq "$(jq -c .socials <<<"$F")" '["https://www.instagram.com/harborlanedental"]' "socials"
eq "$(jq -c .sources <<<"$F")" '["https://harbor.example","https://harbor.example/about"]' "pick_pages stays on site; a dead page is skipped"
eq "$(jq -r 'keys_unsorted | join(",")' <<<"$F")" "title,description,headings,phones,emails,since,rating,socials,tools_seen,tools_missing,sources" "key order"
eq "$(sed -n 2p <<<"$F")" '  "title": "Harbor Lane Dental | Family Dentist in Austin",' "indent 2"
B facts --url https://good.example; eq "$(jq -c '[.tools_seen, .title, .since, .rating, .sources]' <<<"$OUT")" '[["booking","google ads tag"],"Good Dental",null,null,["https://good.example"]]' "tools seen; nothing to pick"
B facts --url nowhere.example; eq "$CODE" 1 "no site"; eq "$ERR" "Could not read the site: <urlopen error [Errno -2] Name or service not known>" "message"

cp "$EX" p.json
B --file p.json build --contact dana-harbor-lane --url https://harbor.example; eq "$OUT" "Wrote closer/research/dana-harbor-lane.md from 2 pages." "build"
MD=$(cat closer/research/dana-harbor-lane.md)
eq "$(head -3 <<<"$MD")" $'# Brief: Harbor Lane Dental\n\nPrepared for the call with Dana Ortiz (Owner).' "header"
has "$MD" "- In business since 2009" "since"; has "$MD" "- Rating on their site: 4.8 from 212 reviews" "rating"
has "$MD" $'## Sources\n\n- https://harbor.example\n- https://harbor.example/about' "cites sources"
has "$MD" "- [Astro: 3 specific, true points from the facts above]" "leaves slots for the assistant"; hasnt "$MD" "[Claude:" "rebranded"
has "$MD" "- Not seen: booking, ordering, google ads tag, chat widget (a hint, not proof: ask)" "a hint, not proof"
eq "$(tail -c 1 closer/research/dana-harbor-lane.md | od -An -c | tr -d ' ')" '\n' "ends with a newline"
jq '(.contacts[] | select(.id=="luis-redbud") | .website) = "good.example" | (.deals[] | select(.contact_id=="luis-redbud") | .gaps) = ["slow", "no-ads"]' p.json > q.json
B --file q.json build --contact luis-redbud; eq "$OUT" "Wrote closer/research/luis-redbud.md from 1 pages." "the contact's website by default"
has "$(cat closer/research/luis-redbud.md)" "- Prospector found: slow, no-ads" "the prospector's gaps"
has "$(cat closer/research/luis-redbud.md)" "- Seen on the site: booking, google ads tag" "seen"
jq '(.contacts[] | select(.id=="luis-redbud") | .website) = null' p.json > n.json
B --file n.json build --contact luis-redbud; eq "$CODE" 1 "no website"; eq "$ERR" "Luis Romero has no website. Pass --url." "message"
B --file p.json build --contact nobody --url x; eq "$ERR" "No contact nobody" "unknown contact"
done_ "closer_brief"

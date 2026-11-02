#!/usr/bin/env bash
# prospector/leads: the assertions of closer-skill/prospector/test_leads.py, run against the C++ tool, plus the CLI.
# Fixtures: the skill's pipeline.example.json, leads.example.csv and openers.json. The audit's pages come from
# fixtures/closer/pages through the test-only ASTRO_TOOL_PAGES hook (good = test_leads' GOOD, bare = its BARE).
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"; SK="$FIX/closer"
export ASTRO_TOOL_OFFLINE=1 ASTRO_TOOL_PAGES="$FIX/closer/pages"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
L() { run --skill-dir "$SK" prospector/leads "$@"; }

# ---- audit
L audit https://good.example --type Dentist; eq "$OUT" $'https://good.example  (0.2 s)\n  no gaps found' "a site with everything has no gaps (round(0.25, 1) is 0.2)"
L audit good.example --type Bakery; eq "$OUT" $'good.example  (0.2 s)\n  gap: No online ordering' "a bakery needs ordering, the url gets https://"
L audit bare.example --type Dentist
eq "$OUT" $'bare.example  (5.0 s)\n  gap: No online booking\n  gap: Site not mobile-friendly\n  gap: Site loads in 5 s\n  gap: No Google Ads tag on the site\n  gap: No reviews on site' "a bare dental site: booking first, no ordering"
L audit bare.example --type "Pet groomer"; has "$OUT" "No online booking" "groomer needs booking"
L audit bare.example --type Bakery; hasnt "$OUT" "booking" "bakery: no booking"; has "$OUT" "No online ordering" "bakery: ordering"
L audit bare.example --type Roofer; eq "$(grep -c gap: <<<"$OUT")" 4 "a roofer needs neither"
L audit cedar.example --type Bakery; eq "$OUT" $'cedar.example  (4.0 s)\n  gap: Site loads in 4 s\n  gap: No Google Ads tag on the site' "4 s is slow; viewport attribute order; ordering and reviews found"
L audit slow.example --type Yoga; eq "$OUT" $'slow.example  (4.0 s)\n  gap: Site not mobile-friendly\n  gap: No Google Ads tag on the site\n  gap: No reviews on site' "3.99 s is not slow, shown rounded"
L audit nowhere.example; eq "$OUT" $'nowhere.example  could not load: <urlopen error [Errno -2] Name or service not known>\n  gap: No website found' "no site"; eq "$CODE" 0 "audit exits 0"

# ---- rank
printf 'name,who,type,email,website\nCedar Co,Grace Kim,Bakery,g@cedar.example,cedar.example\nBare Dental,Dana Ortiz,Dentist,d@bare.example,bare.example\nAlpha,Al,Roofer,a@x.example,good.example\nBeta,Bo,Roofer,,good.example\n' > l.csv
L rank --csv l.csv --offer "booking-ready websites" --wait 0 --out o/leads.json; eq "$CODE" 0 "rank"
eq "$OUT" $' 11  Bare Dental                    Dana Ortiz           No online booking\n  4  Cedar Co                       Grace Kim            Site loads in 4 s\n  0  Alpha                          Al                   -\n  0  Beta                           Bo                   -\nSaved 4 leads to o/leads.json' "ranked by score, then name"
eq "$ERR" $'  checked Cedar Co\n  checked Bare Dental\n  checked Alpha\n  checked Beta' "progress on stderr"
eq "$(jq -c '.[0] | [keys_unsorted, .gaps, .audit.seconds, .audit.title, .offer]' o/leads.json)" '[["name","who","type","email","website","score","why","gaps","audit","offer"],["no-booking","not-mobile","slow","no-ads","no-reviews"],5.0,"Harbor Lane Dental | Austin","booking-ready websites"]' "the saved row"
eq "$(jq -c '.[1].audit.title' o/leads.json)" '"Cedar & Oak Bakery"' "title unescaped, whitespace folded"
eq "$(sed -n 2p o/leads.json)" '  {' "indent 2"
L rank --csv "$SK/leads.example.csv" --wait 0; eq "$(grep -c "No website found" <<<"$OUT")" 8 "the example csv reads: 8 rows, none reachable offline"; has "$OUT" "Dana Ortiz" "who"
eq "$(jq -c '.[0].offer' closer/leads.json)" 'null' "no offer: null"
printf 'name,website\nA,\n' > bad.csv; L rank --csv bad.csv; eq "$CODE" 1 "a row without a website"; eq "$ERR" "Every row needs website (row: {'name': 'A', 'website': ''})" "says which row"
printf '\xef\xbb\xbfname,website\nA,good.example\n' > bom.csv; L rank --csv bom.csv --wait 0; has "$OUT" "Saved 1 leads" "utf-8-sig"

# ---- add
cp "$EX" p.json
cat > r.json <<'J'
[{"name": "Mesa Verde Med Spa", "who": "Elena Ruiz", "email": "elena@mesaverdemedspa.com", "website": "x", "why": "No Google Ads tag on the site", "gaps": ["no-ads"]},
 {"name": "Harbor Lane Dental", "who": "Dana Ortiz", "email": "dana@harborlanedental.com", "website": "x", "why": "No online booking", "gaps": ["no-booking"]},
 {"name": "Oak Hill Vet", "who": "Sam Park", "email": "Sam@OakHillVet.example ", "website": "https://oakhillvet.example", "why": "No online booking", "gaps": ["no-booking"], "offer": "Booking site"}]
J
L --file p.json add --leads r.json --top 10 --value 2500 --today 2026-10-30
eq "$OUT" $'Added 1 leads: sam-oak-hill-vet\n  skipped Mesa Verde Med Spa: on the suppression list\n  skipped Harbor Lane Dental: already on the board' "skips suppressed and existing"
eq "$(jq -c '.contacts[-1] | [.id, .name, .company, .email, .website, .source, .notes, .title]' p.json)" '["sam-oak-hill-vet","Sam Park","Oak Hill Vet","sam@oakhillvet.example","https://oakhillvet.example","prospector","Why: No online booking",null]' "the contact"
eq "$(jq -c '.deals[-1] | [.id, .contact_id, .title, .stage, .value, .next_step, .gaps, .created]' p.json)" '["oak-hill-vet","sam-oak-hill-vet","Booking site","lead",2500,"Opening email: No online booking",["no-booking"],"2026-10-30"]' "the deal"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "still valid"
cp "$EX" p2.json; L --file p2.json add --leads r.json --top -1 --value 99.5 --today 2026-10-30; eq "$(head -1 <<<"$OUT")" "Added 0 leads: " "top -1 is ranked[:-1]"
cp "$EX" p3.json; L --file p3.json add --leads r.json --top 3 --value 99.5 --today 2026-10-30; eq "$(jq '.deals[-1].value' p3.json)" 99.5 "a fractional value stays"

# ---- draft
cp "$EX" p.json
L --file p.json draft --contact luis-redbud; eq "$CODE" 0 "draft"
eq "$OUT" $'To: luis@redbudfamilychiropractic.com\nSubject: Quick idea for Redbud Family Chiropractic\n\nHi Luis,\n\nThe Redbud Family Chiropractic site took a while to load for me, and people tend to leave a slow page before it finishes.\n\nWe do booking-ready websites and Google Ads for local businesses in 2 weeks. Worth a 15 minute call?\n\nThanks,\nYou\n\n--\nYou, Your Agency\n100 Congress Ave, Suite 400, Austin, TX 78701\nNot interested? Reply "no thanks" and I won\'t email again.' "the slow opener with the legal footer"
L --file p.json draft --contact luis-redbud --gap no-booking --offer "a booking page"; has "$OUT" "I noticed Redbud Family Chiropractic doesn't have online booking yet." "the short name keeps a full name"; has "$OUT" "We do a booking page for" "--offer"
L --file p.json draft --contact luis-redbud --gap nonsense; has "$OUT" "I had a quick idea for Redbud" "an unknown gap: the generic opener"
jq '.me.offer = null' p.json > no.json; L --file no.json draft --contact luis-redbud; has "$OUT" $'\n\nFill before it can go: offer' "a missing offer is flagged"; has "$OUT" "We do [fill: offer] for" "placeholder"
jq '.me.address = null' p.json > na.json; L --file na.json draft --contact luis-redbud; eq "$CODE" 1 "no address, no email"; has "$ERR" "CAN-SPAM" "why"
L --file p.json draft --contact elena-mesa-verde; eq "$CODE" 1 "never to do not contact"; eq "$ERR" "Elena Ruiz is do not contact. No email." "message"
L --file p.json draft --contact dana-harbor-lane; eq "$CODE" 1 "never twice"; eq "$ERR" "Dana Ortiz already got an opening email. /follow-up takes it from here." "message"
L --file p.json draft --contact nobody; eq "$ERR" "No contact nobody" "unknown"
jq '(.contacts[] | select(.id=="luis-redbud") | .company) = "Pine Street Fitness" | (.deals[] | select(.contact_id=="luis-redbud") | .gaps) = ["no-booking"]' p.json > sn.json
L --file sn.json draft --contact luis-redbud; has "$OUT" "I noticed Pine Street doesn't have" "short name trims Fitness"
jq '(.contacts[] | select(.id=="luis-redbud") | .company) = "Juniper Loft"' sn.json > sn2.json; L --file sn2.json draft --contact luis-redbud; has "$OUT" "I noticed Juniper Loft doesn't" "one word left: keep it all"

# ---- record
L --file p.json record --contact luis-redbud --text "Hi Luis" --approval sure --at 2026-10-30T09:00; eq "$CODE" 1 "needs a yes"; has "$OUT" "Not recorded:" "why"
L --file p.json record --contact luis-redbud --text "Hi [fill: offer]" --approval yes --at 2026-10-30T09:00; eq "$OUT" "Not recorded: the message still has a [fill: ...] in it." "no placeholders"
L --file p.json record --contact elena-mesa-verde --text "Hi" --approval yes --at 2026-10-30T09:00; eq "$OUT" "Not recorded: Elena Ruiz is do not contact." "dnc"
L --file p.json record --contact luis-redbud --text "Hi Luis" --approval yes --at 2026-10-30T09:00 --subject "Quick idea"; eq "$CODE" 0 "recorded"
eq "$OUT" "Opening to Luis Romero logged (1 of 50 today). /follow-up has it now." "logged"
eq "$(jq -c '.deals[] | select(.id=="redbud-speed") | [.stage, .next_step]' p.json)" '["contacted","Follow-up 1 (/follow-up)"]' "deal is contacted"
eq "$(jq -c '.activities[-1] | [.date, .contact_id, .deal_id, .channel, .type, .direction, .subject, .text, .approval]' p.json)" '["2026-10-30T09:00","luis-redbud","redbud-speed","email","opening","out","Quick idea","Hi Luis","yes"]' "the activity"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "valid"
printf 'Hi Luis,\n\nfrom a file\n\n' > t.txt; cp "$EX" q.json; L --file q.json record --contact luis-redbud --text-file t.txt --approval yes --at 2026-10-30T09:00; eq "$(jq -r '.activities[-1].text' q.json)" $'Hi Luis,\n\nfrom a file' "text file, stripped"
L --file q.json record --contact luis-redbud --approval yes --at 2026-10-30T09:00; eq "$ERR" "Give --text or --text-file: exactly what went out." "no text"
jq '.activities += [range(50) as $i | {id: ("z\($i)"), date: "2026-10-30T08:\($i % 60 | tostring | if length == 1 then "0" + . else . end)", contact_id: "luis-redbud", deal_id: null, channel: "email", type: "opening", direction: "out", subject: null, text: null, approval: "yes"}]' "$EX" > busy.json
L --file busy.json record --contact luis-redbud --text Hi --approval yes --at 2026-10-30T09:00; eq "$CODE" 1 "the daily cap"; eq "$OUT" "Not recorded: 50 opening emails today is the cap. Send the rest tomorrow." "cap message"
L --file busy.json record --contact luis-redbud --text Hi --approval yes --at 2026-10-31T09:00; eq "$CODE" 0 "tomorrow is fine"
done_ "closer_prospect"

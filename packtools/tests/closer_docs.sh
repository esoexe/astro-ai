#!/usr/bin/env bash
# contract-writer/contract and proposal-writer/proposal: the assertions of closer-skill's test_contract.py and
# test_proposal.py, run against the C++ tools. Fixtures: the skill's pipeline.example.json and options.example.json.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"; OPT="$FIX/closer/options.example.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
C() { run contract-writer/contract "$@"; }
PR() { run proposal-writer/proposal "$@"; }
cp "$EX" p.json
REEL=(--services "A booking-ready website and a managed Google Ads campaign for Harbor Lane Dental." --fee "\$399 per month, no setup fee, billed monthly."
      --term "Month to month. Cancel anytime with 30 days notice." --launch "Website live within 14 days of signing.")

# ---- the reel agreement
C --file p.json draft --deal harbor-lane-website "${REEL[@]}" --date "October 29, 2026"; eq "$CODE" 0 "draft"
eq "$OUT" $'Wrote closer/contracts/harbor-lane-dental-agreement.md and closer/contracts/harbor-lane-dental-agreement.html\nA template, not legal advice. Read it, then: contract.py pdf, and send only on a yes.' "no CHECK: the fee was said"
MD=$(cat closer/contracts/harbor-lane-dental-agreement.md); HT=$(cat closer/contracts/harbor-lane-dental-agreement.html)
eq "$(head -1 <<<"$MD")" "# Services Agreement" "title"; has "$MD" "**Your Agency** and **Harbor Lane Dental**" "parties"
has "$MD" $'## 2. Fee\n\n$399 per month, no setup fee, billed monthly.' "fee section"; has "$MD" "## 9. Changes" "standard terms numbered after"
has "$MD" "Dana Ortiz, Harbor Lane Dental: ______" "her signature line"; eq "$(tail -c 1 closer/contracts/harbor-lane-dental-agreement.md | od -An -c | tr -d ' ')" '\n' "ends with one newline"
has "$HT" "<h1>Services Agreement</h1>" "h1"; has "$HT" "<p><strong>Your Agency</strong> and <strong>Harbor Lane Dental</strong></p>" "strong"
has "$HT" "<h2>5. Payment</h2>" "h2"; has "$HT" "client&#x27;s name" "html.escape quotes"; eq "$(tail -c 15 closer/contracts/harbor-lane-dental-agreement.html)" $'</body></html>' "closes"
C --file p.json draft --deal harbor-lane-website --services x --fee "[fill: fee]" --term t --launch l; eq "$CODE" 1 "empty sections are refused"; eq "$ERR" "The Fee section is empty. Say what was agreed." "message"
C --file p.json draft --deal harbor-lane-website --services x --fee '$349 per month' --term t --launch l
has "$OUT" 'CHECK: The fee ($349) is not an amount said on the call or in the proposal ($2500, $399). Check it.' "fee checked against the call"
C --file p.json draft --deal harbor-lane-website --services x --fee 'Monthly, as agreed' --term t --launch l; has "$OUT" "CHECK: The fee has no dollar amount in it." "no amount"
C --file p.json draft --deal harbor-lane-website --services x --fee '$ 399.00 a month, $1,000 setup' --term t --launch l; has "$OUT" 'CHECK: The fee ($1000, $399.00) is not an amount' "amounts are normalised, not rounded"
C --file p.json draft --deal harbor-lane-website "${REEL[@]}"; has "$(sed -n 5p closer/contracts/harbor-lane-dental-agreement.md)" ", 20" "default date is today, written out"
jq '.me.company = null' p.json > nc.json; C --file nc.json draft --deal harbor-lane-website "${REEL[@]}"; eq "$ERR" "Both parties need a company name (me.company and the contact's company)." "company needed"
C --file p.json draft --deal nope "${REEL[@]}"; eq "$ERR" "No deal nope" "unknown deal"
OUT=$(PATH=/nonexistent $T contract-writer/contract --file p.json pdf --deal harbor-lane-website 2>/dev/null); CODE=$?; eq "$CODE" 1 "no chrome"; eq "$OUT" "No Chrome found. Open closer/contracts/harbor-lane-dental-agreement.html and Print > Save as PDF." "print it yourself"

# ---- record needs a draft and a yes; signed
C --file p.json record --deal juniper-booking --approval yes --at 2026-11-03T11:00; eq "$CODE" 1 "no draft"; eq "$OUT" "Not recorded: no agreement drafted for juniper-booking yet." "why"
mkdir -p closer/contracts && echo x > closer/contracts/juniper-yoga-loft-agreement.md && echo x > closer/contracts/juniper-yoga-loft-agreement.html
C --file p.json record --deal juniper-booking --approval "yes to all" --at 2026-11-03T11:00; eq "$OUT" "Not recorded: one yes per message: say yes to each one on its own." "batch yes"
C --file p.json record --deal juniper-booking --approval yes --at 2026-11-03T11:00; eq "$CODE" 0 "recorded"; eq "$OUT" "Logged: agreement sent to Hannah Cole. Deal is contract-sent." "logged"
eq "$(jq -c '.deals[] | select(.id=="juniper-booking") | [.stage, .close_date, .next_step]' p.json)" '["contract-sent","2026-11-13","Hannah signs the agreement"]' "deal moved, close date kept"
eq "$(jq -c '.activities[-1] | [.type, .direction, .subject, .text, .approval, .attachment]' p.json)" '["contract","out","Your agreement with Your Agency","Agreement sent","yes","closer/contracts/juniper-yoga-loft-agreement.html"]' "the activity"
jq '(.deals[] | select(.id=="redbud-speed") | .close_date) = null' p.json > q.json; echo x > closer/contracts/redbud-family-chiropractic-agreement.md; echo x > closer/contracts/redbud-family-chiropractic-agreement.pdf
C --file q.json record --deal redbud-speed --approval y --at 2026-11-03T11:00; eq "$(jq -c '.deals[] | select(.id=="redbud-speed") | .close_date' q.json)" '"2026-11-10"' "no close date: a week out"
eq "$(jq -r '.activities[-1].attachment' q.json)" "closer/contracts/redbud-family-chiropractic-agreement.pdf" "the pdf when there is one"
C --file p.json signed --deal juniper-booking --today 2026-11-05; eq "$OUT" "Booking site, monthly is won." "signed"
eq "$(jq -c '.deals[] | select(.id=="juniper-booking") | [.stage, .close_date, .next_step]' p.json)" '["won","2026-11-05","Kickoff"]' "won, kickoff next"

# ---- the bluebird proposal
cp "$EX" p.json
PR --file p.json draft --deal bluebird-ads --options "$OPT" --today 2026-10-29; eq "$CODE" 0 "draft"
eq "$OUT" "Wrote closer/proposals/bluebird-pet-spa.md. Fill every [Astro: ...] from the call before it goes." "wrote"
MD=$(cat closer/proposals/bluebird-pet-spa.md)
has "$MD" "# Proposal for Bluebird Pet Spa" "title"; has "$MD" "Prepared for Priya Shah by You, Your Agency. Valid until November 12, 2026." "valid 14 days"
has "$MD" "- Maybe! Can you send me prices?" "their words"; has "$MD" "| Full build | \$2,500 setup | \$2,500 | Booking-ready website; Live in 14 days; Google Ads set up |" "option 1"
has "$MD" "| Monthly plan (recommended) | \$399/month | \$4,788 |" "option 2"; has "$MD" $'## What it pays back\n\nMost practices win the cost back with 2 new patients a month.' "value line"
has "$MD" "- [Astro: 3 to 5 lines, what they get and by when]" "placeholder"
bad_opts() { echo "$1" > o.json; PR --file p.json draft --deal bluebird-ads --options o.json --today 2026-10-29; eq "$CODE" 1 "$2"; eq "$ERR" "$3" "$2 message"; }
bad_opts '[]' "no options" "Give 1 to 3 options. More than 3 makes it harder to say yes."
bad_opts "$(jq -c '. + . ' "$OPT")" "four options" "Give 1 to 3 options. More than 3 makes it harder to say yes."
bad_opts '[{"name": "Free", "includes": ["x"]}]' "no price" "Option Free needs a price (setup and/or monthly)."
bad_opts "$(jq -c 'map(.recommended = true)' "$OPT")" "two recommended" "Recommend one option at most."
bad_opts '[{"name": "x", "monthly": 5}]' "no includes" "Every option needs includes."
echo '{' > o.json; PR --file p.json draft --deal bluebird-ads --options o.json; eq "$CODE" 1 "bad json"; has "$ERR" "line 2 column 1 (char 2)" "JSONDecodeError is a ValueError"
PR --file p.json record --deal bluebird-ads --approval yes --at 2026-10-29T16:00; eq "$CODE" 1 "placeholder left"; eq "$OUT" "Not recorded: the proposal still has a [Astro: ...] placeholder in it." "why"
sed -i 's/- \[Astro: 3 to 5 lines, what they get and by when\]/- Ads live in 7 days/' closer/proposals/bluebird-pet-spa.md
PR --file p.json record --deal bluebird-ads --approval sure --at 2026-10-29T16:00; eq "$CODE" 1 "sure is not yes"
PR --file p.json record --deal bluebird-ads --approval yes --at 2026-10-29T16:00; eq "$CODE" 0 "recorded"; eq "$OUT" "Logged: proposal sent to Priya Shah." "logged"
eq "$(jq -c '.deals[] | select(.id=="bluebird-ads") | [.stage, .next_step]' p.json)" '["proposal","Priya picks an option"]' "stage and next step"
eq "$(jq -r '.activities[-1].text' p.json)" 'Proposal sent: $2,500 setup / $399/month' "the prices in the activity"
run pipeline/deals --file p.json validate; eq "$CODE" 0 "valid"
cp "$EX" r.json; PR --file r.json draft --deal harbor-lane-website --options "$OPT" --today 2026-10-29; sed -i 's/- \[Astro:[^]]*\]/- done/' closer/proposals/harbor-lane-dental.md
PR --file r.json record --deal harbor-lane-website --approval yes --at 2026-10-29T16:00; eq "$(jq -r '.deals[0].stage' r.json)" contract-sent "a proposal never moves a deal back"
done_ "closer_docs"

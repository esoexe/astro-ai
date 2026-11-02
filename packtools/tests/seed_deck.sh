#!/usr/bin/env bash
# seed-deck/deck: the assertions of seed-skill/tests/test_deck.py, run against the C++ tool through its command line,
# plus hand-worked edge cases. fixtures/seed/seed-deck/leashly is the upstream fixture: a seed/ folder after seed-plan's
# plan.py build on the Leashly example (plan.json with its numbers block, and the files the lint traces numbers to).
cd "$(dirname "$0")/../.." || exit 1
. packtools/tests/lib.sh
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
LF="$FIX/seed/seed-deck/leashly"
K() { run seed-deck/deck "$@"; }
J() { jq -c "$@"; }
fresh() { cd "$TMP" && rm -rf w && mkdir w && cd w && cp -r "$LF" seed; }
mod() { jq "$1" seed/plan.json > seed/p.tmp && mv seed/p.tmp seed/plan.json; }
ORDER_J='["title","problem","solution","market","competition","business_model","go_to_market","team","financials","ask"]'
fresh

# ---- ten slides, what each says (test_ten_slides_in_the_standard_order, test_what_each_slide_says)
K outline --json; eq "$CODE" 0 "outline --json"; O="$OUT"
eq "$(J '[.[].key]' <<<"$O")" "$ORDER_J" "standard order"; eq "$(J '[.[].n]' <<<"$O")" "[1,2,3,4,5,6,7,8,9,10]" "numbered"
eq "$(J '[.[].title]' <<<"$O")" '["Leashly","The problem","The solution","The market","Competition","Business model","Go to market","Team","Financials","The ask"]' "titles"
eq "$(J '.[0].body' <<<"$O")" '["Dog walks without the surprise fees.","Raising a $1.5M seed · post-money SAFE"]' "title slide"
eq "$(J -r '.[0].foot' <<<"$O")" "Your Name · you@leashly.example" "title foot"
eq "$(J '.[1].body' <<<"$O")" '["Owners pay fees on top of fees. Walkers cancel last minute.","106 of 341","recent 1-2★ Rover reviews mention fees","81 of 324","recent 1-2★ Wag! reviews mention cancellations"]' "problem slide"
has "$(J -r '.[1].foot' <<<"$O")" "the 500 most recent per app, read 2026-10-25" "problem foot"
eq "$(J '.[3].body[1:]' <<<"$O")" '["TAM","$8.5B","the whole market","SAM","$228M","what we can serve","SOM","$22.8M","what we can win"]' "market slide"
has "$(J -r '.[3].foot' <<<"$O")" "APPA 2026 State of the Industry report" "market sources"; hasnt "$(J -r '.[3].foot' <<<"$O")" "assumption" "no assumption sources"
eq "$(J '.[4].body[1:9]' <<<"$O")" '["Rover","4.9★ · 515,771 ratings","Weak point","Fees and pricing","Wag!","4.8★ · 67,447 ratings","Weak point","Cancellations and no-shows"]' "competition"
eq "$(J '.[5].body[1:]' <<<"$O")" '["$25","a walk","$3.75","we keep (15%)","$2.23","contribution a walk","4.24x","LTV:CAC","3.37 mo","CAC payback"]' "business model"
eq "$(J '.[8].body[1:]' <<<"$O")" '["Revenue","Year 1","$467K","Year 2","$1.59M","Year 3","$2.99M","Month 29","breakeven (May 2029)","$82.8K","peak monthly burn"]' "financials"
eq "$(J '.[9].body' <<<"$O")" '["$1.5M seed to launch in 3 cities.","Post-money SAFE","Product and engineering","$600K · 40%","City launches (3 cities)","$450K · 30%","Marketing","$300K · 20%","Operations and insurance","$150K · 10%"]' "the ask"
eq "$(J '[.[].words]' <<<"$O")" "[12,29,22,28,33,23,31,23,27,28]" "word counts"
eq "$(J '.[0]|keys_unsorted' <<<"$O")" '["n","key","title","body","words","foot"]' "outline json keys"
eq "$(grep -c '\\u2605' <<<"$OUT")" 4 "ensure_ascii: the stars are escaped"
K outline; eq "$CODE" 0 "outline"; eq "$(head -1 <<<"$OUT")" "Leashly: pitch deck outline (10 slides)" "outline head"
has "$OUT" " 2. The problem      29 words  Owners pay fees on top of fees. Walkers cancel last minute." "outline line 2"
has "$OUT" "10. The ask          28 words  \$1.5M seed to launch in 3 cities. | Post-money SAFE | Product and engineering | " "outline line 10"
# the outline lines quoted in SKILL.md's example are the tool's real output (each quoted line, cut at "| ...")
for q in " 1. Leashly          12 words  Dog walks without the surprise fees. | Raising a \$1.5M seed · post-money SAFE" \
         " 4. The market       28 words  \$158B US pet spend (2025). 71M US homes have a dog. | TAM | \$8.5B" \
         " 5. Competition      33 words  Rover: fees. Wag!: cancellations. | Rover | 4.9★ · 515,771 ratings" \
         " 6. Business model   23 words  15% take rate. No owner fee. | \$25 | a walk | \$3.75 | we keep (15%)" \
         " 9. Financials       27 words  Breakeven in month 29 on the seed alone. | Revenue | Year 1 | \$467K"; do
  grep -qF -- "$q" <<<"$OUT" && ok || bad "SKILL.md line: $q"
done

# ---- lint: the reel is clean (test_the_reel_lints_clean)
K lint; eq "$CODE" 0 "lint clean"; eq "$OUT" "Lint: clean. 10 slides, the most words on one is 33." "lint clean line"
K lint --json; eq "$CODE" 0 "lint --json"; eq "$(J '.ok,.issues' <<<"$OUT" | tr '\n' ' ')" "true [] " "ok, no issues"
eq "$(J '.words' <<<"$OUT")" '{"title":12,"problem":29,"solution":22,"market":28,"competition":33,"business_model":23,"go_to_market":31,"team":23,"financials":27,"ask":28}' "lint words"
eq "$(J 'keys_unsorted' <<<"$OUT")" '["ok","issues","words"]' "lint json keys"

# ---- build: one self-contained file (test_one_self_contained_file, test_16_by_9_print_and_keys, test_charts_use_the_numbers)
K build; eq "$CODE" 0 "build"; H=seed/deck.html
eq "$(head -1 <<<"$OUT")" "Wrote seed/deck.html: 10 slides, $(( ($(wc -c < $H) + 1023) / 1024 )) KB, one self-contained file." "wrote, KB rounded up"
has "$OUT" "Lint: clean" "build lint clean"; has "$OUT" "/seed-coach" "next step"
eq "$(sed -n 2p <<<"$OUT")" "Open it in a browser: the arrow keys move, P prints. Print > Save as PDF gives one slide a page." "open line"
HT=$(cat $H)
eq "${HT:0:15}" "<!doctype html>" "doctype"; eq "$(grep -o '<section class="slide' $H | wc -l)" 10 "10 sections"
for never in "<link" "src=" "@import" "url(" "http://" "https://" "—"; do hasnt "$HT" "$never" "never $never"; done
eq "$(grep -o '<style>' $H | wc -l)" 1 "one style"; eq "$(grep -o '<script>' $H | wc -l)" 1 "one script"
has "$HT" "<title>Leashly: pitch deck</title>" "title tag"
has "$HT" "width:1280px;height:720px" "16:9"; has "$HT" "@page{size:13.333in 7.5in;margin:0}" "page size"; has "$HT" "@media print{" "print css"; has "$HT" "break-after:page" "page breaks"
for key in ArrowRight ArrowLeft PageDown PageUp Home End; do has "$HT" "'$key'" "key $key"; done
has "$HT" "history.replaceState" "hash"; hasnt "$HT" "%%" "every CSS percent was formatted"
has "$HT" ".replace(/ on\\b/g,'')" "the JS regex keeps its backslash"; has "$HT" "'  ·  arrows to move, P to print'" "the JS \\u00b7 is a real middle dot"
has "$HT" "--brand:#2F6FEB;--ink:#0E1B33;--accent:#FFB020" "brand colours"
has "$HT" "--soft:#EEF3FD;--soft2:#D1DFFB;--line:#E2E4E7;" "mix: 47 x .08 + 255 x .92 = 238.36 -> EE"
has "$HT" "--muted:#6A7281;--paper:#FFFFFF;--on-brand:#FFFFFF;" "muted and on-brand"
has "$HT" ">\$2.99M</text>" "bar label"; has "$HT" '<div class="fill" style="width:75%"></div>' "450K of the 600K top bar"
has "$HT" 'aria-label="TAM, SAM and SOM, not to scale"' "market svg"
has "$HT" '<section class="slide s-business-model" id="s6" aria-label="Slide 6: Business model">' "section attributes"
has "$HT" '<div class="num">6 / 10</div></section>' "page numbers"
has "$HT" '<h1><span class="bigmark">L</span>Leashly</h1><p class="tagline">Dog walks without the surprise fees.</p>' "title slide html"
has "$HT" '<div class="brandbar"><span class="mark">L</span>Leashly</div><h2>The problem</h2>' "brand bar"
has "$HT" '<div class="tile"><b>3.37 <small>mo</small></b><span>CAC payback</span></div>' "tile with a unit"
has "$HT" '<div class="tiles five" style="grid-template-columns:repeat(5,1fr)">' "five tiles"
has "$HT" '<div class="face">YN</div><div><h3>Your Name</h3><div class="role">Founder and CEO</div><p>Ran the Austin pilot with 210 owners</p>' "team person"
has "$HT" '<div class="card us"><h3>Leashly</h3><div class="val">Flat $25 walks and a backup walker on every booking.</div></div>' "our card"
has "$HT" '<p class="lead">Breakeven in month 29 on the seed alone.</p><div class="fin"><svg width="560" height="380"' "financials layout"
has "$HT" '<rect x="400" y="54" width="120" height="282" rx="12" fill="var(--brand)"/>' "the top bar is full height"
has "$HT" '<rect x="40" y="292" width="120" height="44" rx="12" fill="var(--soft2)"/>' "year 1 bar: int(467400/2993520 x 282) = 44"
has "$HT" '<circle cx="190" cy="306" r="64" fill="var(--brand)"/><text x="190" y="306" text-anchor="middle" font-size="19" font-weight="800" fill="#fff" letter-spacing="1.5" dominant-baseline="middle">SOM</text>' "SOM circle"
eq "$(tail -c 16 $H | od -An -c | tr -d ' \n')" '</body>\n</html>\n' "ends with a newline"
K build --out out/pitch.html --brand "#0F9D58"; eq "$CODE" 0 "build --out"; has "$(cat out/pitch.html)" "--brand:#0F9D58" "--brand flag"
eq "$(head -1 <<<"$OUT" | cut -d: -f1)" "Wrote out/pitch.html" "out path"
K build --out deck2.html; eq "$CODE" 0 "--out with no folder"; [ -f deck2.html ] && ok || bad "deck2.html"
K build --out x.html --ink "#000000"; has "$(cat x.html)" "--ink:#000000;--accent:#FFB020;--soft:#EEF3FD;--soft2:#D1DFFB;--line:#E0E0E0;" "ink black: 255 x .88 = 224.4 -> E0"
has "$(cat x.html)" "--muted:#616161;" "255 x .38 = 96.9 -> 97"
mod '.brand.brand = "#123456"'; K build --out b.html; has "$(cat b.html)" "--brand:#123456;--ink:#0E1B33;--accent:#FFB020" "plan.json brand"
K build --out b2.html --brand "#ABCDEF"; has "$(cat b2.html)" "--brand:#ABCDEF;" "the flag beats plan.json"
mod '.brand.accent = ""'; K build --out b3.html; has "$(cat b3.html)" "--accent:#FFB020" "an empty plan colour keeps the default"
mod '.brand.accent = "orange"'; K build --out b4.html; eq "$CODE" 2 "bad plan colour"; eq "$ERR" "deck.py: the accent colour wants #RRGGBB, like #2F6FEB (got 'orange')" "bad plan colour message"
[ -e b4.html ] && bad "nothing written on a bad colour" || ok
fresh
K build --accent gold; eq "$CODE" 2 "bad accent"; has "$ERR" "#RRGGBB" "bad accent message"; eq "$ERR" "deck.py: the accent colour wants #RRGGBB, like #2F6FEB (got 'gold')" "exact message"
K build --ink "#12345"; eq "$ERR" "deck.py: the ink colour wants #RRGGBB, like #2F6FEB (got '#12345')" "short hex"

# ---- text is escaped (test_text_is_escaped)
fresh; mod '.tagline = "<script>alert(1)</script> & more" | .team[0].name = "A \"quoted\" <b>name</b>"'
K build; HT=$(cat seed/deck.html)
has "$HT" "&lt;script&gt;alert(1)&lt;/script&gt; &amp; more" "tagline escaped"; has "$HT" "A &quot;quoted&quot; &lt;b&gt;name&lt;/b&gt;" "name escaped"
eq "$(grep -o '<script>' seed/deck.html | wc -l)" 1 "still one script"; has "$HT" '<div class="face">A&quot;</div>' "initials of a quoted name"
mod ".tagline = \"It's ours\""; K build; has "$(cat seed/deck.html)" "It&#x27;s ours" "quote=True escapes '"

# ---- section fallbacks (test_ask_mentions_the_raise_when_the_line_does_not, test_pitch_falls_back_to_the_first_sentence)
fresh; mod '.sections[8].pitch = "Launch in three cities."'; K outline --json; eq "$(J -r '.[9].body[1]' <<<"$OUT")" '$1.5M seed · post-money SAFE' "the ask says the raise"
fresh; mod '.sections[2].pitch = ""'; K outline --json
eq "$(J -r '.[2].body[0]' <<<"$OUT")" "One flat price per walk with no owner fee, and a second walker on standby near every booking." "first sentence"
fresh; mod '.tagline = ""'; K outline --json; eq "$(J -r '.[0].body[0]' <<<"$OUT")" "$(jq -r .startup.one_liner seed/plan.json)" "tagline falls back to the one-liner"
fresh; mod '.numbers.low_stars = 1'; K outline --json; eq "$(J -r '.[1].body[2]' <<<"$OUT")" "recent 1★ Rover reviews mention fees" "1★"
fresh; mod '.numbers.competitors |= map(del(.top_count))'; K outline --json; eq "$(J -r '.[1].body|length' <<<"$OUT")" "$(( 1 + $(jq '[.sections[1].points[]?]|.[:4]|length' seed/plan.json) ))" "no stats: the points"
eq "$(J -r '.[1].foot' <<<"$OUT")" "" "no stats, no source"
fresh; mod '.numbers.financials.breakeven_date = "2029-00"'; K outline --json; eq "$(J -r '.[8].body[9]' <<<"$OUT")" "breakeven (Dec 2029)" "month 00 is index -1"
mod '.numbers.financials.breakeven_date = "2029-13"'; K outline; eq "$CODE" 1 "month 13 is an IndexError"
mod '.numbers.financials.breakeven_date = "May"'; K outline --json; eq "$(J -r '.[8].body[9]' <<<"$OUT")" "breakeven (May)" "not YYYY-MM: as is"

# ---- the lint rules (class Lint)
fresh; mod '.sections[2].pitch = ([range(23)|"word"]|join(" ")) + "."'
K lint; eq "$CODE" 1 "too many words exit 1"; has "$OUT" "  - Slide 3 (The solution) has 36 words; keep it to 35 or fewer." "36 words"
fresh; mod '.sections[2].pitch = "Flat price. Backup walker. Live tracking."'; K lint; has "$OUT" "Slide 3 (The solution) says 3 things in its lead: one idea a slide, 2 sentences at most." "3 sentences"
mod '.sections[2].pitch = "Flat price." | .sections[2].points = ["One","Two","Three","Four"]'; K lint; has "$OUT" "Slide 3 (The solution) has 4 points: one idea a slide, 3 points at most." "4 points"
fresh; mod '.sections[3].pitch = "$9B US dog walking. 43% of owners use apps, 71M homes have a dog."'; K lint --json
eq "$(J '.issues' <<<"$OUT")" '["Slide 4 (The market) shows $9B, 43%, in none of plan.json, model.json, market.json, competitors.json or config.json."]' "every number traces"
mod '.sections[3].pitch = "40% of the raise builds the product."'; K lint --json; eq "$(J '.issues' <<<"$OUT")" "[]" "the use of funds share traces"
mod '.sections[3].pitch = "$8.5B US dog walking."'; K lint --json; eq "$(J '.issues' <<<"$OUT")" "[]" "rounded is fine"
mod '.sections[3].pitch = "$8.52B and 2030 and 7 cities and 1,999 owners."'; K lint --json; eq "$(J -r '.issues[0]' <<<"$OUT")" "Slide 4 (The market) shows 1,999, in none of plan.json, model.json, market.json, competitors.json or config.json." "years and small counts are not checked; a comma year is"
mod '.sections[3].pitch = "Growth of 3.5x and $1.5 million raised."'; K lint --json; eq "$(J -r '.issues[0]' <<<"$OUT")" "Slide 4 (The market) shows 3.5x, in none of plan.json, model.json, market.json, competitors.json or config.json." "multiples are checked, million scales"
fresh; mod '.numbers.use_of_funds.Marketing = 250000'; K lint; has "$OUT" "The ask: the use of funds adds up to \$1,450,000, not the \$1,500,000 raise." "the ask adds up"
mod '.numbers.raise = null'; K lint; has "$OUT" "The ask has no raise: set startup.raise in seed/config.json, then plan.py build." "no raise"
fresh; mod 'del(.numbers.market) | .team = [] | .sections[6] += {text: "", pitch: "", points: []} | .sections[1] += {text: "", pitch: ""}'
K lint; eq "$CODE" 1 "missing data"
for x in "The market slide has no data: run /seed-market, then plan.py build." "The team slide has no people: add the team to seed/plan.json, then plan.py build." \
         "Slide 8 (Team) is empty: write its section in seed/plan.json, then plan.py build." "Slide 7 (Go to market) is empty: write its section in seed/plan.json, then plan.py build." \
         "Slide 2 (The problem) has no line: write the pitch for it in seed/plan.json."; do has "$OUT" "  - $x" "$x"; done
K outline; has "$OUT" " 8. Team              0 words  (empty)" "an empty slide in the outline"
K build; eq "$CODE" 0 "build with problems"; has "$(cat seed/deck.html)" '<p class="lead">(not written yet)</p>' "an unwritten lead"
fresh; jq '.summary.revenue_y1 = 500000' seed/model.json > seed/m.tmp && mv seed/m.tmp seed/model.json
K lint; has "$OUT" "plan.json is older than model.json: run plan.py build again, then build the deck." "older plan"
fresh; rm seed/model.json seed/market.json seed/competitors.json seed/config.json; K lint --json; eq "$CODE" 1 "no other files"
# known numbers are plan.numbers, plan.startup and the other files; plan.team is not a source, so its 210 no longer traces
eq "$(J '.issues' <<<"$OUT")" '["Slide 8 (Team) shows 210, in none of plan.json, model.json, market.json, competitors.json or config.json."]' "the team's 210 came from model.json"

# ---- build writes even when the lint finds something (test_lint_exit_code_and_build_still_writes)
fresh; mod '.sections[1].pitch = "Fees cost owners $43 a walk."'
K lint --json; eq "$CODE" 1 "lint exit 1"; eq "$(J .ok <<<"$OUT")" false "not ok"; has "$(J -r '.issues[0]' <<<"$OUT")" "Slide 2 (The problem) shows \$43" "\$43"
K build; eq "$CODE" 0 "build still writes"; has "$OUT" "The lint found 1 thing to fix:" "1 thing"; has "$OUT" "  - Slide 2 (The problem) shows \$43, in none of" "the issue"
[ -f seed/deck.html ] && ok || bad "deck.html written"

# ---- refusals (test_refusals) and bad input
fresh; mod '.numbers = {} | .built = null'; K build; eq "$CODE" 2 "never built"; eq "$ERR" "deck.py: seed/plan.json has no numbers yet: run plan.py build first" "never built message"
[ -e seed/deck.html ] && bad "no deck written" || ok
fresh; rm -rf seed; K lint; eq "$CODE" 2 "no plan"; eq "$ERR" "deck.py: no plan (run /seed-plan first: plan.py init, then plan.py build) at seed/plan.json" "no plan message"
K; eq "$CODE" 2 "no command"; has "$OUT" "usage: deck.py [-h] [--dir DIR] [--plan PATH] {build,outline,lint} ..." "help"
fresh; echo nope > seed/model.json; K lint; eq "$CODE" 2 "bad model.json"; eq "$ERR" "deck.py: seed/model.json is not valid JSON: Expecting value: line 1 column 1 (char 0)" "bad JSON message"
fresh; mod '.numbers.raise = "abc"'; K outline; eq "$CODE" 2 "raise abc"; eq "$ERR" "deck.py: not a number: 'abc'" "not a number"
mod '.numbers.raise = true'; K outline; eq "$ERR" "deck.py: not a number: True" "a bool is not a number"
mod '.numbers.raise = "$1,500,000"'; K outline --json; eq "$(J -r '.[0].body[1]' <<<"$OUT")" "Raising a \$1.5M seed · post-money SAFE" "\$ and commas are stripped"
fresh; mod '.numbers.unit_economics |= del(.price)'; K outline; eq "$CODE" 1 "a missing price is a KeyError"
fresh; mkdir other && mv seed other/s; K outline --dir other/s; eq "$CODE" 0 "--dir after the command"; K --dir other/s outline; eq "$CODE" 0 "--dir before"
K outline --plan other/s/plan.json --dir other/s; eq "$CODE" 0 "--plan"; K outline --plan nope.json --dir other/s; eq "$ERR" "deck.py: no plan (run /seed-plan first: plan.py init, then plan.py build) at nope.json" "--plan missing"

done_ "seed-deck/deck"

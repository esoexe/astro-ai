#!/usr/bin/env bash
# sales-coach/coach: the assertions of closer-skill/sales-coach/test_coach.py, run against the C++ tool, plus the CLI
# and --follow. Fixtures: the skill's pipeline.example.json, objections.json and call.example.txt.
source "$(dirname "$0")/lib.sh"
EX="$FIX/closer/pipeline.example.json"; SK="$FIX/closer"; CALL="$FIX/closer/call.example.txt"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
cp "$EX" p.json
CO() { run --skill-dir "$SK" sales-coach/coach --file p.json "$@"; }
BAR=------------------------------------------------------------
# ---- detect: one line per case through listen
det() { printf 'Dana: %s\n' "$1" > c.txt; CO listen --captions c.txt; eq "$(sed -n 2p <<<"$OUT" | cut -d' ' -f2 | tr -d ' ')" "$2" "$1"; }
det "It looks great, but honestly, it's too expensive for us right now." price
det "That's out of our budget." price
det "It's our busy season, maybe next quarter." timing
det "Let me think about it." needs
det "I need to run it by my business partner." someone
det "We already have someone who does our site." already
det "How do I know this actually works?" does
det "Can you just send me some info?" send
printf 'Dana: Mostly word of mouth, and people calling the front desk.\nDana: Sounds like a plan.\n' > c.txt; CO listen --captions c.txt
eq "$OUT" "Nothing to coach yet." "small talk and a near-miss get nothing"
# ---- the reel answer, and a missing offer is never invented
printf 'Dana: It looks great, but honestly, it'"'"'s too expensive for us right now.\n' > c.txt; CO listen --captions c.txt
eq "$OUT" "$BAR"$'\nObjection: price  (Dana: "It looks great, but honestly, it\'s too expensive for us right now.")\nSay this next: Totally fair. Most practices win the cost back with 2 new patients a month. Want to start on the $399 monthly plan instead, with no setup fee?\n  Agree first, then make the price small next to what it brings back, then give a smaller first step. Never drop the price without changing what they get.\n'"$BAR" "the reel answer"
printf 'Dana: too expensive\n' > c.txt; run --skill-dir "$SK" sales-coach/coach --file none.json listen --captions c.txt
has "$OUT" "Say this next: Totally fair. [your value line] Want to start on the [your plan b]?" "never invents a missing offer"
jq '.me.value_line = ""' p.json > e.json; run --skill-dir "$SK" sales-coach/coach --file e.json listen --captions c.txt; has "$OUT" "[your value line]" "an empty value is missing too"
# ---- own lines, timestamps, questions
printf 'You: Is it too expensive?\nyou: too expensive\nSam: too expensive\n' > c.txt; CO listen --captions c.txt --me Sam; eq "$OUT" "Nothing to coach yet." "your own lines (and You) get no card"
printf '[2:14] Dana: It'"'"'s too expensive.\n[01:02:03] Dana Ortiz: What does it include\n12:00 Who: how long does it take\n' > c.txt; CO listen --captions c.txt
has "$OUT" '(Dana: "It'"'"'s too expensive.")' "a caption line with a timestamp"; has "$OUT" $'They asked a question  (Dana Ortiz: "What does it include")\nSay this next: Answer it directly before you move on.\n  What does it include' "a question without a question mark"
has "$OUT" '(Who: "how long does it take")' "bare timestamp"
printf 'what is this exactly, a very long line that goes on and on and on and on and on and on and on and on and on?\n' > c.txt; CO listen --captions c.txt
has "$OUT" '(them: "what is this exactly, a very long line that goes on and on and on and on and on and on and")' "no speaker: them, cut at 90"
# ---- the example call
CO listen --captions "$CALL"; eq "$(grep -c "^Say this next" <<<"$OUT")" 2 "two cards"; has "$(sed -n 7p <<<"$OUT")" "Buying signal  (Dana: \"Okay, that's easier. How soon could we start?\")" "objection, then buying"
has "$OUT" "Great. The next step is a short agreement. Want me to send that over today?" "next step from the offer"
CO review --captions "$CALL"
eq "$OUT" $'You talked 68% of the time (aim for under 50%).\nYou asked 2 questions.\nObjection (price): It looks great, but honestly, it\'s too expensive for us right now.\nBuying signal: Okay, that\'s easier. How soon could we start?\nNext step agreed: yes' "review"
printf 'Dana: I think we need a lower price, I want a discount, it is too much, honestly I mean it really\nYou: ok\nYou: bye\n' > c.txt; CO review --captions c.txt
eq "$OUT" $'You talked 9% of the time.\nYou asked 0 questions.\nObjection (price): I think we need a lower price, I want a discount, it is too much, honestly I mean it really\nNext step agreed: NO. Send a recap with one clear next step.' "a quiet call with no next step"
printf 'Dana: hi there\nYou: ok\n' > c.txt; CO review --captions c.txt; has "$OUT" "You talked 33% of the time." "round"
printf 'Dana: a b c\nYou: a\n' > c.txt; CO review --captions c.txt; has "$OUT" "You talked 25% of the time." "25"
printf 'Dana: a\nYou: a\n' > c.txt; CO review --captions c.txt; has "$OUT" "You talked 50% of the time." "50 is not over"
printf 'Dana: a\nYou: a b c d e f g\n' > c.txt; CO review --captions c.txt; has "$OUT" "You talked 88% of the time (aim for under 50%)." "88"
printf '' > c.txt; CO review --captions c.txt; has "$OUT" "You talked 0% of the time." "an empty call"
# ---- card
CO card --objection proof; eq "$OUT" $'Objection: does it work?\nSay this next: Fair question. Here\'s what happened for a business like yours: Cedar & Oak Bakery took 31 online orders in their first two weeks. Want me to send it over?\n  Answer with a real result you can show. If you have none yet, say so and offer a short first step instead.' "card"
CO card --objection nope; eq "$CODE" 1 "unknown objection"; eq "$ERR" "No objection nope. Try: price, timing, think, authority, incumbent, proof, send_info" "lists them"
CO listen --captions missing.txt; eq "$CODE" 1 "no captions file"
# ---- --follow: new lines as they come, Ctrl-C stops
: > live.txt
$T --skill-dir "$SK" sales-coach/coach --file p.json listen --captions live.txt --follow > follow.out 2>&1 &
PID=$!
sleep 1.5; printf 'Dana: Let me think about it.\nYou: sure\n' >> live.txt; sleep 2; printf 'Dana: Can we start next week?\n' >> live.txt; sleep 2
kill -INT $PID; wait $PID; FCODE=$?
eq "$FCODE" 0 "Ctrl-C exits cleanly"
FO=$(cat follow.out)
eq "$(head -1 <<<"$FO")" "Listening to live.txt. Ctrl-C to stop." "says what it listens to"
eq "$(grep -c "^Say this next" <<<"$FO")" 2 "a card per new line from them"; has "$FO" "Objection: needs to think" "think"; has "$FO" "Buying signal  (Dana: \"Can we start next week?\")" "buying"
eq "$(tail -2 <<<"$FO")" $'\nStopped listening.' "stops"
done_ "closer_coach"

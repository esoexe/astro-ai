#!/usr/bin/env bash
# arena/bracket: the contract of arena-skill/tests/test_bracket.py (whole tournaments driven through the CLI, the
# dealer, the pairing, the plan, verdict decisions, the full pipeline with a baseline, the guards), plus the
# A.S.T.R.O AI additions: show (a brief with its files pasted in) and save (a reply stored as the job's files).
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-arena/skills/arena"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
b() { OUT=$($T --skill-dir "$SK" arena/bracket "$@" 2>"$TMP/err"); CODE=$?; ERR=$(cat "$TMP/err"); }
j() { jq -c "$1" <<<"$OUT"; }

# ---------------------------------------------------------------- whole tournaments (record .a as every winner)
play() {   # play <dir>: run to the champion, one line per round: "<alive> <matches> <bye?>"
    local d=$1 rows=""
    while :; do
        b pairings --dir "$d" --json
        [ "$CODE" -eq 0 ] && [[ "$OUT" == "{"* ]] || break
        rows+="$(jq -r '"\(.alive | length) \(.matches | length) \(if .bye then "T" else "F" end)"' <<<"$OUT")"$'\n'
        for m in $(jq -r '.matches[] | "\(.id),\(.a)"' <<<"$OUT"); do
            b record --dir "$d" "${m%,*}" "${m#*,}"
        done
        b advance --dir "$d"
    done
    printf '%s' "$rows"
}
b init --dir t100 --seed 7 --agents 100 --task "solve it"
eq "$CODE" 0 "init 100"
has "$OUT" "seed 7. 100 agents, 100 distinct cards dealt from 2160, no repeats." "init line"
has "$OUT" "7 rounds (100 -> 50 -> 25 -> 13 -> 7 -> 4 -> 2 -> 1). 595 sub-agent calls in 70 waves of 10." "init plan line"
eq "$(jq -c '[.agents | keys[0], keys[-1], length]' t100/arena.json)" '["a001","a100",100]' "agent ids"
rows=$(play t100)
eq "$(awk '{print $1}' <<<"$rows" | tr '\n' ' ')" "100 50 25 13 7 4 2 " "alive per round"
eq "$(awk '{print $3}' <<<"$rows" | tr '\n' ' ')" "F F T T T F F " "byes per round"
eq "$(awk '{s+=$2} END {print s}' <<<"$rows")" 99 "99 matches"
eq "$(jq '[.rounds[] | select(.closed)] | length' t100/arena.json)" 7 "7 rounds"
eq "$(jq '[.agents[] | .byes] | max' t100/arena.json)" 1 "nobody gets two byes"
b status --dir t100
champ=$(jq -r .champion t100/arena.json)
has "$OUT" "now: 1 alive, 99 eliminated, champion $champ" "status champion"
b winner --dir t100
has "$OUT" "CHAMPION  $champ" "winner"
has "$OUT" "7 played, 100 agents in, 1 left" "winner rounds"
b pairings --dir t100
has "$OUT" "last one standing" "pairings after the end"
b advance --dir t100
eq "$CODE" 2 "advance after the end"; has "$ERR" "error: no round is open (the arena has a champion)" "advance after the end msg"

b init --dir t16 --seed 3 --agents 16 --task t
rows=$(play t16)
eq "$(awk '{print $3}' <<<"$rows" | tr '\n' ' ')" "F F F F " "16: no byes"
b init --dir t7 --seed 11 --agents 7 --task t
rows=$(play t7)
eq "$(head -1 <<<"$rows")" "7 3 T" "7: round 1 has a bye"
b init --dir t1 --agents 1 --task t
eq "$(jq -c '[.champion, .rounds]' t1/arena.json)" '["a001",[]]' "1 agent: champion at once"
b winner --dir t1
has "$OUT" "0 played, 1 agents in, 1 left" "1 agent winner"

# ---------------------------------------------------------------- the dealer
cards() { jq -c '[.agents[] | .card | [.reasoning.id, .workflow.id, .strategy.id]]' "$1/arena.json"; }
eq "$(jq '[.reasoning, .workflows, .strategies] | map(length)' -c "$SK/strategies.json")" "[15,12,12]" "deck 15/12/12"
eq "$(jq .cards_available t100/arena.json)" 2160 "2160 combos"
balanced() { cards "$1" | jq '[0,1,2] | map(. as $k | [($c | map(.[$k])) | group_by(.)[] | length] | (max - min)) | max' --argjson c "$(cards "$1")"; }
nodup() { cards "$1" | jq '(length) == (unique | length)'; }
pairfree() { cards "$1" | jq '[[0,1],[0,2],[1,2]] | map(. as [$x,$y] | ($c | map([.[$x], .[$y]])) | length == (unique | length)) | all' --argjson c "$(cards "$1")"; }
for n in 1 2 12 15 16 50 100 144 145 300 1000 2160; do
    b init --dir "d$n" --seed 5 --agents "$n" --task t
    eq "$CODE" 0 "deal $n"
    [ "$(balanced "d$n")" -le 1 ] && ok || bad "deal $n balanced"
    eq "$(nodup "d$n")" true "deal $n no duplicate card"
    [ "$n" -le 144 ] && eq "$(pairfree "d$n")" true "deal $n no shared pair"
done
b init --dir d0 --agents 0 --task t
eq "$CODE" 2 "0 agents"; has "$ERR" "error: --agents must be between 1 and 2160 (the number of distinct cards)" "0 agents msg"
b init --dir d2161 --agents 2161 --task t
eq "$CODE" 2 "2161 agents"
eq "$([ -e d2161/arena.json ] && echo y || echo n)" n "no arena written on a guard"

# ---------------------------------------------------------------- pairing: at most one same-reasoning match
worst=0
for s in $(seq 0 49); do
    b init --dir "p$s" --seed "$s" --agents 100 --task t
    same=$(jq '. as $s | [.rounds[0].matches[] | select($s.agents[.a].card.reasoning.id == $s.agents[.b].card.reasoning.id)] | length' "p$s/arena.json")
    [ "$same" -gt "$worst" ] && worst=$same
done
[ "$worst" -le 1 ] && ok || bad "same-reasoning matches across seeds 0-49: $worst"
# pinned against the reference implementation
# pinned against the Node.js port (CPython's random.Random(str) seeding, shuffle and random())
eq "$(jq -c '[.rounds[0].matches[0] | .id, .a, .b]' t100/arena.json)" '["r1-m01","a087","a047"]' "pairing pinned"
eq "$(jq -c '[.agents.a001.card | .reasoning.id, .workflow.id, .strategy.id]' t100/arena.json)" '["adversarial","three-drafts","defensive"]' "deal pinned"
eq "$(jq -c '.rounds[0].matches | length' p7/arena.json)" 50 "100 agents: 50 matches"
eq "$(jq -r '.rounds[0].matches[0].id' p7/arena.json)" "r1-m01" "match id width 2"
b init --dir w300 --seed 1 --agents 300 --task t
eq "$(jq -r '.rounds[0].matches[0].id' w300/arena.json)" "r1-m001" "match id width 3"
eq "$(jq -r '.agents | keys[0]' w300/arena.json)" "a001" "agent id width 3"
b init --dir w1000 --seed 1 --agents 1000 --task t
eq "$(jq -r '.agents | keys[0]' w1000/arena.json)" "a0001" "agent id width 4"

# ---------------------------------------------------------------- plan
b plan --json
eq "$(j '[.rounds, .calls, .waves]')" "[7,595,70]" "plan 100"
b plan --quick --json
eq "$(j '[.agents, .rounds, .calls, .waves]')" "[16,4,91,16]" "plan quick"
b plan --agents 7 --wave 3 --json
eq "$(j '[.rows[] | [.alive, .matches, .bye, .calls, .waves]]')" "[[7,3,1,15,5],[4,2,0,10,5],[2,1,0,5,3]]" "plan rows"
b plan
has "$OUT" "100 agents, 7 rounds, waves of 10" "plan text head"
has "$OUT" "  spawn    100        -    -              100     10" "plan spawn row"
has "$OUT" "      1    100       50    -              250     25" "plan row 1"
has "$OUT" "      3     25       12  yes               60      8" "plan bye row"
has "$OUT" "  total                                 595     70" "plan total"
has "$OUT" "  alive per round: 100 -> 50 -> 25 -> 13 -> 7 -> 4 -> 2 -> 1" "plan sizes"
b plan --wave 0
eq "$CODE" 2 "plan wave 0"; has "$ERR" "error: --wave must not be 0" "plan wave 0 msg"
b plan --agents 0
eq "$CODE" 2 "plan 0 agents"; has "$ERR" "error: --agents must be at least 1" "plan 0 msg"
b plan --quick --agents 4
eq "$CODE" 2 "plan quick+agents"; has "$ERR" "error: pass --quick or --agents, not both" "quick+agents msg"

# ---------------------------------------------------------------- verdicts (through collect on a 2-agent arena)
verdict() {   # verdict <json>: a fresh 2-agent arena, the verdict on disk, collect; sets A B and the outputs
    rm -rf v; b init --dir v --seed 1 --agents 2 --task t
    A=$(jq -r '.rounds[0].matches[0].a' v/arena.json); B=$(jq -r '.rounds[0].matches[0].b' v/arena.json)
    local body=${1//@A/$A}; body=${body//@B/$B}
    mkdir -p v/r1; printf '%s' "$body" > v/r1/r1-m01.verdict.json
    b collect --dir v
}
S10='{"correctness": 10, "completeness": 10, "specificity": 10, "robustness": 10, "clarity": 10}'
S68='{"correctness": 8, "completeness": 6, "specificity": 7, "robustness": 5, "clarity": 9}'
verdict "{\"winner\": \"@B\", \"scores\": {\"@A\": $S10, \"@B\": $S68}}"
eq "$CODE" 0 "collect rc"
eq "$(jq -c ".rounds[0].matches[0].scores" v/arena.json)" "{\"$A\":100.0,\"$B\":68.5}" "weighted totals 100 and 68.5"
eq "$(jq -r ".rounds[0].matches[0].winner" v/arena.json)" "$A" "scores beat the pick"
has "$OUT" "$A beat $B, 100 to 68.5. judge picked $B but its own scores favour $A, so the scores win" "scores win note"
has "$OUT" "round 1: 1 of 1 match(es) recorded" "collect count"
verdict "{\"winner\": \"\", \"scores\": {\"@A\": {\"correctness\": 10, \"completeness\": 10, \"specificity\": 10, \"robustness\": 10, \"clarity\": 10, \"fatal\": \"yes\"}, \"@B\": $S68}}"
eq "$(jq -r ".rounds[0].matches[0].winner" v/arena.json)" "$B" "the fatal rule"
verdict "{\"scores\": {\"@A\": $S68, \"@B\": $S68}, \"standing\": {\"@A\": [1, 2], \"@B\": [1]}}"
eq "$(jq -r ".rounds[0].matches[0].winner" v/arena.json)" "$B" "tie: fewer standing attacks"
verdict "{\"winner\": \"@B wins\", \"scores\": {\"@A\": {\"correctness\": 9}, \"@B\": $S68}}"
eq "$(jq -r ".rounds[0].matches[0].winner" v/arena.json)" "$B" "incomplete scores: the pick"
has "$OUT" "scores incomplete, used the judge's pick" "incomplete note"
has "$OUT" "$B beat $A, 68.5 to ?" "incomplete totals"
verdict '{"winner": "", "scores": {}}'
eq "$CODE" 1 "empty scores: collect fails"
has "$OUT" "unreadable verdict has neither complete scores nor a clear winner" "empty scores msg"
eq "$([ -e v/r1/r1-m01.verdict.json.unreadable ] && echo y || echo n)" y "renamed .unreadable"
has "$OUT" "1 verdict(s) missing or unreadable. Re-run those judges:" "re-run line"
verdict "Sure. Here it is:
\`\`\`json
{\"winner\": \"@A\", \"scores\": {\"@A\": $S68, \"@B\": {\"correctness\": \"7\", \"completeness\": 6, \"specificity\": 7, \"robustness\": 5, \"clarity\": 9}}, \"reason\": \"steadier\", \"survived\": [\"edge case\", 3]}
\`\`\`"
eq "$(jq -c ".rounds[0].matches[0] | [.winner, .reason, .survived, .source]" v/arena.json)" "[\"$A\",\"steadier\",[\"edge case\",\"3\"],\"judge\"]" "fenced JSON parses"
verdict 'no json here'
eq "$CODE" 1 "no json: unreadable"; has "$OUT" "unreadable verdict is not a JSON object" "no json msg"
verdict "{\"winner\": \"@A and @B\", \"scores\": {\"@A\": $S68, \"@B\": $S68}}"
eq "$CODE" 1 "exact tie, no pick"; has "$OUT" "exact tie and the judge did not pick a winner" "tie msg"
verdict "{\"winner\": \"@B\", \"scores\": {\"@A\": {\"correctness\": 99, \"completeness\": -4, \"specificity\": 7, \"robustness\": 5, \"clarity\": true}, \"@B\": $S68}}"
eq "$(jq -c ".rounds[0].matches[0].scores" v/arena.json)" "{\"$A\":null,\"$B\":68.5}" "a bool score is missing"
verdict "{\"scores\": {\"@A\": {\"correctness\": 99, \"completeness\": -4, \"specificity\": 7, \"robustness\": 5, \"clarity\": \"8\"}, \"@B\": $S68}}"
eq "$(jq -c ".rounds[0].matches[0].scores[\"$A\"]" v/arena.json)" "58.5" "scores clamp to 0..10" # (300 + 0 + 105 + 100 + 80) / 10

# ---------------------------------------------------------------- the full pipeline, with a baseline
printf 'Write a poem for {{name}} about {{rubric}}\nsecond line\n' > task.txt
printf 'the rejected answer\n' > base.txt
b init --dir full --seed 9 --agents 5 --task-file task.txt --baseline-file base.txt
eq "$CODE" 0 "full init"
has "$OUT" "5 agents, 5 distinct cards dealt from 2160, no repeats." "full init line"
has "$OUT" "3 rounds (5 -> 3 -> 2 -> 1). 26 sub-agent calls in 11 waves of 10, including the final check." "full plan line"
eq "$(cat full/task.md)" "$(cat task.txt)" "task.md word for word"
eq "$(cat full/baseline.md)" "the rejected answer" "baseline.md"
cmp -s full/rubric.md "$SK/rubric.md" && ok || bad "rubric copied"
b next --dir full
has "$OUT" "spawn: 5 competitors, then 3 round(s)" "next spawn head"
has "$OUT" "NEXT: spawn. 5 of 5 job(s) to run." "next spawn"
has "$OUT" "arena/bracket prompts spawn" "next names the tool"
b prompts --dir full spawn
has "$OUT" "round 0, spawn: 5 job(s), 5 still to run, 1 wave(s) of up to 10" "prompts head"
has "$OUT" "  a001.spawn                 $TMP/full/prompts/r0/a001.spawn.md" "prompts row"
has "$OUT" "Each job is one task tool call, description \"arena <job id>\"." "prompts how"
has "$OUT" "The built-in sub-agent cannot open files: run " "prompts built-in line"
has "$OUT" "arena/bracket show <job id>, give its whole output as the prompt" "prompts show cmd"
p1=$(cat full/prompts/r0/a001.spawn.md)
task_block=$(awk '/^=== THE TASK \(identical for every competitor\) ===$/{f=1; next} /^=== END OF THE TASK ===$/{f=0} f' full/prompts/r0/a001.spawn.md)
eq "$task_block" "$(cat task.txt)" "the task block, braces untouched"
has "$p1" "Reasoning mode: $(jq -r '.agents.a001.card.reasoning.name' full/arena.json)." "spawn brief names the mode"
has "$p1" "NOT satisfied" "spawn brief: baseline note"
has "$p1" "$TMP/full/baseline.md" "spawn brief: baseline path"
eq "$([ -d full/scratch/a003 ] && echo y || echo n)" y "scratch folders"
b check --dir full spawn
eq "$CODE" 1 "check: missing"; has "$OUT" "spawn: 0 of 5 job(s) done" "check head"
has "$OUT" "  missing a001.spawn                 $TMP/full/r0/a001.md" "check row"
# show/save for a spawn job
b show --dir full a001.spawn
eq "$CODE" 0 "show spawn"
has "$OUT" "$p1"$'\n\n'"---"$'\n'"HOW TO ANSWER IN THIS SETTING" "show: brief, then the setting block"
has "$OUT" "Reply with exactly what the brief says to write to $TMP/full/r0/a001.md, and nothing else." "show: one-part reply"
hasnt "$OUT" "===== FILE $TMP/full/task.md =====" "the task is inline, so task.md is not pasted"
has "$OUT" "===== FILE $TMP/full/baseline.md =====" "show pastes baseline.md"
hasnt "$OUT" "===== FILE $TMP/full/r0/a001.md" "show never pastes the output"
hasnt "$OUT" "FILE $TMP/full/prompts" "show never pastes prompts"
b show --dir full A001.SPAWN
eq "$CODE" 0 "show is case-insensitive"
b show --dir full nope
eq "$CODE" 2 "show unknown"; has "$ERR" "error: no open job called 'nope'. " "show unknown msg"; has "$ERR" "arena/bracket next shows what is open" "show unknown hint"
for a in a001 a002 a003 a004; do printf 'solution %s\n' "$a" > full/r0/$a.md; done
printf '\n\nsolution a005 {{x}}\n\n\n' > rep.txt
b save --dir full a005.spawn rep.txt
eq "$CODE" 0 "save"; has "$OUT" "saved a005.spawn: $TMP/full/r0/a005.md" "save line"; has "$OUT" "then: " "save then"
eq "$(cat -A full/r0/a005.md)" 'solution a005 {{x}}$' "save trims and ends with one newline"
printf ' \n\t\n' > empty.txt
b save --dir full a005.spawn empty.txt
eq "$CODE" 2 "save empty"; has "$ERR" "error: the reply is empty. Re-run that job" "save empty msg"
printf 'from stdin\r\n' | { OUT=$($T --skill-dir "$SK" arena/bracket save --dir full a004.spawn - 2>&1); echo "$?" > rc.txt; }
eq "$(cat rc.txt)" 0 "save from stdin"; eq "$(cat full/r0/a004.md)" "from stdin" "stdin text"
printf 'solution a004\n' > full/r0/a004.md
b next --dir full
has "$OUT" "round 1 of 3, 5 alive" "next round 1"; has "$OUT" "NEXT: attack. 4 of 4 job(s) to run." "next attack"
b prompts --dir full attack
for id in $(grep -o 'r1-m0[0-9]\.a00[0-9]\.attack' <<<"$OUT" | sort -u); do printf 'attacks by %s\n' "$id" > rep.txt; b save --dir full "$id" rep.txt; done
b prompts --dir full defend
ids=$(grep -o 'r1-m0[0-9]\.a00[0-9]\.defend' <<<"$OUT" | sort -u)
first=$(head -1 <<<"$ids")
b show --dir full "$first"
has "$OUT" $'Reply in two parts, in this order, each starting with its marker on a line of its own:\n=== DEFENSE ===' "show: defender two-part reply"
has "$OUT" "===== FILE " "show defender pastes files"
has "$OUT" ".attack.md =====" "show defender pastes the attacks"
printf 'just one part\n' > rep.txt
b save --dir full "$first" rep.txt
eq "$CODE" 2 "defender reply without markers"; has "$ERR" 'error: a defender reply needs both parts: a line "=== DEFENSE ===", the defense, a line "=== SOLUTION ===", the revised solution. Re-run that job with the brief from show' "defender msg"
for id in $ids; do printf '\n=== defense ===\nI stand by %s.\n\n  == REVISED SOLUTION ==\n\nrevised %s.solution.md body\n\n' "$id" "$id" > rep.txt; b save --dir full "$id" rep.txt; done
has "$OUT" "saved $id: $TMP/full/r1/" "save defender line"
has "$OUT" ".defense.md, $TMP/full/r1/" "save defender: both files"
d=${first%.defend}; aid=${d#*.}; mid=${d%%.*}
eq "$(cat "full/r1/$mid.$aid.defense.md")" "I stand by $first." "defense body"
eq "$(cat -A "full/r1/$mid.$aid.solution.md")" "revised $first.solution.md body\$" "solution body"
b prompts --dir full judge
jp=$(cat "full/prompts/r1/r1-m01.judge.md")
hasnt "$jp" "Reasoning mode" "judge brief: no reasoning mode"
judge_all() {   # write a verdict for every open match: the first agent wins on scores
    local r=$1
    for m in $(jq -r --argjson r "$r" '.rounds[$r - 1].matches[] | select(.winner == null) | "\(.id),\(.a),\(.b)"' full/arena.json); do
        IFS=, read -r mid a bb <<<"$m"
        printf '{"winner": "%s", "scores": {"%s": %s, "%s": %s}, "reason": "r%s"}' "$a" "$a" "$S10" "$bb" "$S68" "$r" > "full/r$r/$mid.verdict.json"
    done
}
judge_all 1
b collect --dir full; eq "$CODE" 0 "collect round 1"
b next --dir full; has "$OUT" "NEXT: advance. Every match in round 1 has a winner." "next advance"
b advance --dir full; eq "$CODE" 0 "advance 1"; has "$OUT" "round 1 closed: 3 through, 2 eliminated" "advance line"
rev=$(jq -r '.rounds[0].matches[0].winner' full/arena.json)
has "$(jq -r ".agents.$rev.solution" full/arena.json)" ".solution.md" "a survivor carries its revised solution"
advances=1
for r in 2 3; do
    b pairings --dir full --json; [[ "$OUT" == "{"* ]] || break
    for ph in attack defend; do
        b prompts --dir full $ph
        for id in $(grep -oE "r$r-m0[0-9]\.a00[0-9]\.$ph" <<<"$OUT" | sort -u); do
            if [ $ph = attack ]; then printf 'atk\n' > rep.txt; else printf '=== DEFENSE ===\nd\n=== SOLUTION ===\nrevised %s\n' "$id" > rep.txt; fi
            b save --dir full "$id" rep.txt
        done
    done
    b prompts --dir full judge; judge_all $r; b collect --dir full; b advance --dir full; advances=$((advances+1))
done
eq "$advances" 3 "3 advances"
champ=$(jq -r .champion full/arena.json)
[ "$champ" != null ] && ok || bad "a champion"
has "$(jq -r ".agents.$champ.solution" full/arena.json)" ".solution.md" "champion solution is revised"
b next --dir full; has "$OUT" "NEXT: final. 1 of 1 job(s) to run." "next final"
b prompts --dir full final; has "$OUT" "final, final: 1 job(s), 1 still to run" "prompts final"
X=$(jq -r .final.X full/arena.json)
if [ "$X" = champion ]; then cmp -s full/final/X.md "$(jq -r ".agents.$champ.solution" full/arena.json)" && ok || bad "X is the champion"; else cmp -s full/final/X.md full/baseline.md && ok || bad "X is the baseline"; fi
printf '{"winner": "X", "scores": {"X": %s, "Y": %s}, "reason": "X fixed it", "fixed": ["one"]}' "$S10" "$S68" > full/final.verdict.json
b collect --dir full; eq "$CODE" 0 "collect final"; has "$OUT" "final" "collect final row"; has "$OUT" "$X is better." "final note"
b next --dir full; has "$OUT" "DONE. " "done"
b winner --dir full --json
eq "$(j '[.rounds, .agents, .final.better, .final.fixed]')" "[3,5,\"$X\",[\"one\"]]" "winner --json"
b winner --dir full
has "$OUT" "vs the answer you rejected: winner" "winner vs baseline"
b card --dir full 1
has "$OUT" "a001  " "card"; has "$OUT" "  reasoning " "card reasoning row"; has "$OUT" "  solution  $TMP/full/" "card solution"
b card --dir full a999
eq "$CODE" 2 "card unknown"; has "$ERR" "error: no agent called 'a999'" "card unknown msg"

# ---------------------------------------------------------------- quick spawn: 16 cards, one task
b init --dir q --quick --seed 4 --task "the same task"
eq "$(jq .agents_n q/arena.json)" 16 "--quick: 16"
b prompts --dir q spawn
eq "$(ls q/prompts/r0 | wc -l | tr -d ' ')" 16 "16 prompts"
eq "$(for f in q/prompts/r0/*.md; do awk '/^=== THE TASK/{f=1; next} /^=== END OF THE TASK/{f=0} f' "$f"; done | sort -u)" "the same task" "one task for all"
eq "$(for f in q/prompts/r0/*.md; do awk '/^=== YOUR STRATEGY CARD ===$/{f=1; next} /^=== END OF THE CARD ===$/{f=0} f' "$f" | md5sum; done | sort -u | wc -l | tr -d ' ')" 16 "16 different cards"
hasnt "$(cat q/prompts/r0/a001.spawn.md)" "NOT satisfied" "no baseline note without one"

# ---------------------------------------------------------------- guards
b init --dir q --task again
eq "$CODE" 2 "re-init"; has "$ERR" "already has an arena in it. Pick another --dir" "re-init msg"
b init --dir g1 --quick --agents 8 --task t
eq "$CODE" 2 "quick + agents"
b init --dir g2 --task "   "
eq "$CODE" 2 "blank task"; has "$ERR" "error: the task is empty. Pass --task-file or --task" "blank task msg"
printf '\n \n' > eb.txt
b init --dir g3 --task t --baseline-file eb.txt
eq "$CODE" 2 "empty baseline"; has "$ERR" "error: the baseline file is empty" "empty baseline msg"
b init --dir g4 --task t --wave 0
eq "$CODE" 2 "wave 0"; has "$ERR" "error: --wave must be at least 1" "wave msg"
b record --dir t7 r1-m01 a001
eq "$CODE" 2 "record after the end"
b init --dir g5 --seed 2 --agents 4 --task t
out=$(jq -r '.agents | keys[]' g5/arena.json | grep -vxF "$(jq -r '.rounds[0].matches[0] | .a, .b' g5/arena.json)" | head -1)
b record --dir g5 r1-m01 "$out"
eq "$CODE" 2 "record an outsider"; has "$ERR" "is not in r1-m01" "outsider msg"
b record --dir g5 m9 a001
eq "$CODE" 2 "record unknown match"; has "$ERR" "error: no match 'm9' in round 1 (run pairings)" "unknown match msg"
b advance --dir g5
eq "$CODE" 2 "advance with open matches"; has "$ERR" "error: round 1 has 2 unrecorded match(es): r1-m01, r1-m02" "open matches msg"
w=$(jq -r '.rounds[0].matches[0].b' g5/arena.json)
b record --dir g5 1 "${w#a}" --reason "by hand" --survived s1 --survived s2
eq "$CODE" 0 "record by number"; has "$OUT" "r1-m01: $w beat " "record line"
eq "$(jq -c '.rounds[0].matches[0] | [.winner, .reason, .survived, .source, .scores]' g5/arena.json)" "[\"$w\",\"by hand\",[\"s1\",\"s2\"],\"manual\",null]" "record fields"
a=$(jq -r '.rounds[0].matches[0].a' g5/arena.json)
b record --dir g5 r1-m01 "$a"
has "$OUT" "r1-m01: $a beat $w (was $w)" "re-record shows the old winner"
b next --dir nowhere
eq "$CODE" 2 "no arena"; has "$ERR" "error: no arena.json in $TMP/nowhere" "no arena msg"
b status
eq "$CODE" 2 "no LATEST"; has "$ERR" "error: no arena here. Run init first, or pass --dir" "no LATEST msg"
b init --seed 6 --agents 3 --task t
eq "$CODE" 0 "default dir"; has "$(cat .arena/LATEST)" "$TMP/.arena/run-" "LATEST written"; has "$(cat .arena/LATEST)" "-s6" "LATEST names the seed"
b status
eq "$CODE" 0 "status via LATEST"; has "$OUT" "seed 6, 3 agents, waves of 10, 2160 distinct cards available" "status head"
b prompts --dir q nope
eq "$CODE" 2 "bad phase"; has "$ERR" "argument phase: invalid choice: 'nope'" "bad phase msg"

done_ arena

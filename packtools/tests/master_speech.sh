#!/usr/bin/env bash
# master-virality/speech: the cases of master-skills/skills/virality/tools/test_virality.py (TestSpeech) plus hand-counted
# extras, run against the C++ tool. Every expected word count below is counted by hand from the rules in speech.py
# ("$4,200" = four thousand two hundred dollars = 5).
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
S=(--skill-dir "$SK" master-virality/speech)
j() { echo "$OUT" | jq -c "$1"; }
w() { run "${S[@]}" --line "$1" --json; eq "$(j '.lines[0].spoken_words')" "$2" "spoken words of [$1]"; }

# ---- numbers said out loud (upstream cases)
w '$4,200' 5; w '97%' 3; w 10x 2; w 1.2M 4; w '$9M' 3; w 2026 3; w 1,000 2; w 412,000 4; w 24/7 3; w 5-10 3
w 'I saved $4,200 last month.' 9
# ---- hand-counted extras
w 2005 3          # two thousand five
w 2000 2          # two thousand
w 1900 2          # nineteen hundred
w 1999 3          # nineteen ninety nine
w 1990s 2         # nineteen ninety
w 2,026 4         # a comma means a quantity: two thousand twenty six
w '#1' 2          # number one
w highest-paying 2
w opusjake.ai 3   # opusjake dot ai
w "don't" 1
w '&' 1
w '🔥' 0
w 3:30 2          # three thirty
w '$20/mo' 3      # twenty dollars mo
w 1,000,000 2     # one million
w 4.25 4          # four point two five
w '100%' 3        # one hundred percent
w 1st 1
w '(97%)' 3       # edge punctuation is stripped first
w 0 1             # zero
w 'Hook.' 1

# ---- a timed sequence
L65=$(printf 'w%.0s ' $(seq 65))
run "${S[@]}" --line "$L65" --line "end." --wpm 60 --json
eq "$CODE" 0 "lines exit 0"
eq "$(j '.wpm')" 60.0 "wpm echoed as a float"
eq "$(j '[.lines[] | .start]')" "[0.0,65.0]" "starts"
eq "$(j '[.lines[] | .seconds]')" "[65.0,1.0]" "seconds"
eq "$(j '.total')" 66.0 "total"
eq "$(j '.lines[0] | keys_unsorted')" '["line","spoken_words","start","seconds"]' "row keys"
run "${S[@]}" --line "$L65" --line "end." --wpm 60
eq "$OUT" "$(printf '\n  0:00.0  65.0s  65 words  %s\n  1:05.0   1.0s   1 words  end.\n\n  total 66.0s at 60 wpm' "$L65")" "text rows and total"
run "${S[@]}" --line 'I saved $4,200 last month.' --line 'Hook.' --wpm 180
eq "$OUT" "$(printf '\n  0:00.0   3.0s   9 words  I saved $4,200 last month.\n  0:03.0   0.3s   1 words  Hook.\n\n  total 3.3s at 180 wpm')" "upstream-style sequence"
run "${S[@]}" --line 'one two three'
has "$OUT" "at 165 wpm" "default wpm prints with %g"

# ---- transcripts
printf '1\n00:00:00,500 --> 00:00:02,000\nI saved $400\n\n2\n00:00:02,100 --> 00:00:03,500\nwith one prompt.\n' > "$TMP/a.srt"
run "${S[@]}" "$TMP/a.srt" --json
eq "$(j '[.cues,.words,.start,.end,.span,.wpm]')" "[2,8,0.5,3.5,3.0,160]" "srt: 2 cues, 8 words, 3.0s, 160 wpm"
eq "$(j 'keys_unsorted')" '["cues","words","start","end","span","wpm"]' "measure keys (text left out)"
run "${S[@]}" "$TMP/a.srt"
eq "$CODE" 0 "srt exit 0"
eq "$OUT" "$(printf '\n  %s   2 cues   8 spoken words   0:00.5 -> 0:03.5 (3.0s)   pace 160 wpm\n  Pass --wpm 160 when you time scripts for this voice.\n' "$TMP/a.srt")" "srt text report"
printf 'WEBVTT\n\n00:00:00.000 --> 00:00:02.000\n \nhey<00:00:00.300><c> guys</c>\n\n00:00:02.000 --> 00:00:04.000\nhey guys\ntoday we test\n' > "$TMP/b.vtt"
run "${S[@]}" "$TMP/b.vtt" --json
eq "$(j '[.cues,.words,.span,.wpm]')" "[2,5,4.0,75]" "vtt rolling captions: only the new lines count (hey guys today we test)"
printf '{"events": [{"tStartMs": 0, "dDurationMs": 2000, "segs": [{"utf8": "I made $9M"}]}]}' > "$TMP/c.json3"
run "${S[@]}" "$TMP/c.json3" --json
eq "$(j '[.cues,.words,.start,.end,.wpm]')" "[1,5,0.0,2.0,150]" "json3 cue"
printf '{"events": [{"tStartMs": 0, "segs": [{"utf8": "a b"}]}, {"tStartMs": 1500, "segs": [{"utf8": "c"}]}, {"tStartMs": 9000}]}' > "$TMP/d.json3"
run "${S[@]}" "$TMP/d.json3" --json
eq "$(j '[.cues,.words,.start,.end,.span,.wpm]')" "[2,3,0.0,1.5,1.5,120]" "json3 without durations ends at the next event"
printf '{"segments":[{"start":1.0,"end":3.0,"text":" hello world "},{"start":3,"end":5.5,"text":"again"},{"text":""},7]}' > "$TMP/w.json"
run "${S[@]}" "$TMP/w.json" --json
eq "$(j '[.cues,.words,.start,.end,.span,.wpm]')" "[2,3,1.0,5.5,4.5,40]" "whisper segments"
printf '{"text":"one two three"}' > "$TMP/t.json"
run "${S[@]}" "$TMP/t.json" --json
eq "$(j '[.cues,.words,.start,.end,.span,.wpm]')" "[1,3,null,null,null,null]" "whisper text only: no timings"
run "${S[@]}" "$TMP/t.json"
eq "$OUT" "$(printf '\n  %s   3 spoken words, no timings in this file.\n  At 165 wpm that is about 1.1s. Use a .srt/.vtt/.json3 to measure real pace.\n' "$TMP/t.json")" "untimed report"
printf 'null' > "$TMP/n.json"
run "${S[@]}" "$TMP/n.json" --json
eq "$(j '[.cues,.words]')" "[1,1]" "JSON null is read as prose"
printf '{oops' > "$TMP/bad.json"
run "${S[@]}" "$TMP/bad.json" --json
eq "$(j '[.cues,.words]')" "[1,1]" "invalid JSON falls back to prose ({ is stripped)"
printf '[]' > "$TMP/e.json"
run "${S[@]}" "$TMP/e.json"
eq "$CODE" 1 "valid JSON with no transcript is empty"
has "$ERR" "no speech found in $TMP/e.json" "empty message"
printf 'I saved $400 today.\n' > "$TMP/p.txt"
run "${S[@]}" "$TMP/p.txt"
eq "$OUT" "$(printf '\n  %s   6 spoken words, no timings in this file.\n  At 165 wpm that is about 2.2s. Use a .srt/.vtt/.json3 to measure real pace.\n' "$TMP/p.txt")" "plain text"
printf '1\n00:00:01,000 --> 00:00:61,000\nbad ts\n\n2\n00:00:02,000 --> 00:00:03,000\nfine \xff line\n' > "$TMP/f.srt"
run "${S[@]}" "$TMP/f.srt" --json
eq "$(j '[.cues,.words,.start,.end]')" "[2,4,1.0,61.0]" "bad bytes become U+FFFD (silent), 61 seconds still parses"
printf '\xef\xbb\xbf{"events": [{"tStartMs": 0, "dDurationMs": 2000, "segs": [{"utf8": "I made $9M"}]}]}' > "$TMP/bom.json3"
run "${S[@]}" "$TMP/bom.json3" --json
eq "$(j '[.cues,.words,.start,.end]')" "[1,5,0.0,2.0]" "utf-8-sig: the BOM is stripped before json.loads"
printf '1\naa:bb --> 00:00:01,000\nskipped\n\n00:00:02,000 --> 00:00:03,000\nkept\n' > "$TMP/g.srt"
run "${S[@]}" "$TMP/g.srt" --json
eq "$(j '[.cues,.words,.start]')" "[1,1,2.0]" "a cue with a bad timestamp is dropped"
printf '{"segments":[{"start":-5,"end":55,"text":"x y"}]}' > "$TMP/neg.json"
run "${S[@]}" "$TMP/neg.json"
has "$OUT" "0:00.0 -> 0:55.0 (60.0s)   pace 2 wpm" "a negative start prints as 0:00.0"
printf '{"segments":[{"start":59.96,"end":61,"text":"a"}]}' > "$TMP/q.json"
run "${S[@]}" "$TMP/q.json"
has "$OUT" "0:60.0 -> 1:01.0" "%04.1f rounds 59.96 up to 60.0, as the original does"
OUT=$(printf 'a b c' | $T "${S[@]}" - --json); eq "$(j '[.cues,.words]')" "[1,3]" "stdin"

# ---- arguments
run "${S[@]}"
eq "$CODE" 2 "no input: exit 2"
has "$ERR" "usage: speech [-h] [--line LINE] [--wpm WPM] [--json] [input]" "help on stderr"
has "$ERR" "speaking rate for timing lines (default 165.0)" "default in help"
run "${S[@]}" --line x --wpm 0
eq "$CODE" 2 "--wpm 0"
has "$ERR" "speech: error: --wpm must be positive" "wpm error"
run "${S[@]}" --line x --wpm abc
eq "$CODE" 2 "--wpm abc"
has "$ERR" "argument --wpm: invalid float value: 'abc'" "float error"
run "${S[@]}" "$TMP/missing.srt"
eq "$CODE" 2 "missing file"
eq "$ERR" "no such file: $TMP/missing.srt" "missing message"
run "${S[@]}" -h
eq "$CODE" 0 "-h"
has "$OUT" "Numbers are expanded the way they are said" "epilog"
done_ "master-virality/speech"

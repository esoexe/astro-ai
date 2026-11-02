#!/usr/bin/env bash
# The YouTube Pack's built-in helpers (youtube-agent-skill has no test suite of its own): every expectation below is
# worked by hand from the Python originals, with the arithmetic in the comments.
source "$(dirname "$0")/lib.sh"
PK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-youtube/skills"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
tool() { local sk="$1"; shift; run --skill-dir "$PK/$sk" "$sk/$@"; }
j()  { echo "$OUT" | jq -c "$1"; }
jr() { echo "$OUT" | jq -r "$1"; }
line() { echo "$OUT" | sed -n "$1p"; }

# ================================================================ yt-package/title
# 32 characters; "7" and "30" are figures; the first three words (i, tried, 7) are not all filler;
# title words minus stop words {tried 7 ai editors 30 days} meet the thumbnail's {ai ran} on "ai": 100 - 14 + 2 * 4 = 94
tool yt-package title --title "I Tried 7 AI Editors for 30 Days" --thumb "AI RAN IT"
eq "$CODE" 0 "title rc"
eq "$OUT" '
  "I Tried 7 AI Editors for 30 Days"
  32 chars   score 94/100
    x  duplicate    thumbnail repeats the title on ai - the thumbnail should say what the title does not
    ok              32 characters, inside the 60-character desktop cut
    ok              carries a concrete figure (7, 30)' "title text"
tool yt-package title --title "I Tried 7 AI Editors for 30 Days" --thumb "AI RAN IT" --json
eq "$(j '.[0] | [.chars, .score, .issues]')" '[32,94,[["duplicate","thumbnail repeats the title on ai - the thumbnail should say what the title does not"]]]' "title json"
has "$OUT" '[
 {
  "title": "I Tried 7 AI Editors for 30 Days",' "indent 1"
# the YouTube original has no --cover (the virality copy maps it to --thumb) and no -h
tool yt-package title --title "I Tried 7 AI Editors for 30 Days" --cover "AI RAN IT" --json
eq "$(j '.[0] | [.score, .issues]')" '[100,[]]' "--cover is not the thumbnail here: no duplicate, 100 + 2 * 4 clamped to 100"
tool yt-package title --title "I Tried 7 AI Editors for 30 Days" --cover "AI RAN IT"
has "$OUT" "score 100/100" "clamped to 100"
tool yt-package title -h
eq "$CODE" 1 "-h is not special: usage and exit 1"
eq "$(line 1)" "title - lint a YouTube title and thumbnail pairing before you publish it." "usage first line"
has "$OUT" "    astro-server tool yt-package/title titles.txt            # one per line, ranked" "usage names the tool"
run --skill-dir "$PK/../../astro-master/skills/master-virality" master-virality/title -h
eq "$CODE" 0 "the virality copy still answers -h"
# 69 chars: past the desktop cut and the mobile cut; INSANE + HUGE + SECRET shout; no figure -> 100 - 5 * 14 = 30.
# The 32-character question has two figures and no issue: 100 + 3 * 4, clamped to 100.
printf 'The INSANE HUGE SECRET nobody tells you about editing your own videos\nWhy 90%% of editors quit in 2024?\n\n' > titles.txt
tool yt-package title titles.txt --json
eq "$(j '[.[].score]')" '[100,30]' "ranked best first"
eq "$(j '.[1].issues | map(.[0])')" '["length","mobile","shouting","vague","no-number"]' "every issue kind"
eq "$(jr '.[1].issues[1][1]')" 'a mobile feed shows about "The INSANE HUGE SECRET nobody tells you..." - check the subject survives' "mobile cut at a space"
eq "$(jr '.[1].issues[3][1]')" 'huge, insane, secret - swap for a number, a name or a date' "vague words sorted"
eq "$(j '.[0].good')" '["32 characters, inside the 60-character desktop cut","carries a concrete figure (90%, 2024)","open question in the title"]' "good notes"
printf 'ok title\n\xff\n' > bad.txt
tool yt-package title bad.txt
eq "$CODE" 1 "undecodable titles file"
has "$ERR" "'utf-8' codec can't decode byte 0xff in position 9: invalid start byte" "strict utf-8"
tool yt-package title --title
eq "$CODE" 1 "--title without a value (IndexError)"
tool yt-package title
eq "$CODE" 1 "no arguments: usage"

# ================================================================ yt-viral/swipe
# Chan A: 100, 200, 300, 1000 and one without views (counted as 0 here: the YouTube original keeps it) ->
# median of [0 100 200 300 1000] = 200; multiples 0.5 1 1.5 5 0. Chan B has 2 videos: thin.
cat > col.json <<'EOF'
[{"channel": "Chan A", "title": "vlog", "views": 100, "url": "u1"},
 {"channel": "Chan A", "title": "My setup", "views": 200, "url": "u2"},
 {"channel": "Chan A", "title": "Editing tips", "views": 300, "url": "u3"},
 {"channel": "Chan A", "title": "I tried 3 editors for 30 days", "hook": "why nobody tells you this", "views": 1000, "url": "u4"},
 {"channel": "Chan A", "title": "old", "views": null, "url": "u5"},
 {"channel": "Chan B", "title": "one", "views": 50},
 {"channel": "Chan B", "title": "two", "views": 70}]
EOF
tool yt-viral swipe col.json --json
eq "$CODE" 0 "swipe rc"
eq "$(j '[.outliers[] | [.title, .views, .median, .formula, .url]]')" '[["I tried 3 editors for 30 days",1000,200,"I Tried It","u4"],["Editing tips",300,200,"Unclassified","u3"]]' "outliers (title, not hook)"
has "$OUT" '"multiple": 5.0,' "multiple is a float"
has "$OUT" '"multiple": 1.5,' "1.5 kept at --min 1.5"
eq "$(j '.skipped_thin_channels')" '[["Chan B",2]]' "thin channels"
tool yt-viral swipe col.json
eq "$(line 2)" "  7 videos across 2 channels, outliers at 1.5x or better" "header counts every video"
eq "$(line 4)" "$(printf '    %5sx  %9s  vs %9s median   %-22s %s' 5.00 1,000 200 'Chan A' 'I tried 3 editors for 30 days')" "outlier row"
eq "$(line 5)" "            I Tried It" "formula row"
eq "$(echo "$OUT" | sed -n '8,14p')" '
  skipped 1 channel(s) with under 4 videos collected - a median off one or
  two videos is not a median: Chan B (2)

  formulas among the outliers
     1x  I Tried It
     1x  Unclassified' "summary"
tool yt-viral swipe col.json --min 6
has "$OUT" "    nothing cleared the threshold - collect more per channel or lower --min" "nothing cleared"
has "$OUT" "outliers at 6.0x or better" "--min shown as a float"
tool yt-viral swipe -h
eq "$CODE" 1 "-h is a missing file here"
has "$OUT" "The formula comes from skills/yt-script/hook-formulas.json, matched against the TITLE." "swipe usage"
mkdir -p empty/yt-viral
run --skill-dir "$TMP/empty/yt-viral" yt-viral/swipe col.json
eq "$CODE" 1 "formulas are read before anything else"
has "$ERR" "No such file or directory" "missing formulas file"

# ================================================================ yt-script/hookscore
# A: words(t) has 13 entries; CONCRETE finds "3 " once.
#   SPECIFICITY 34 + 22 = 56; ADDRESS 26 + 20 + 30 (You in the first six words) = 76; STAKES 22 + 26 (wasting) + 14 = 62
#   CURIOSITY 24 + 17 (why) = 41; BREVITY 100 -> mean 67 -> 0.6 * 67 + 0.4 * 41 = 56.6 -> 57 WORKABLE, no formula
# B: SPECIFICITY 34; ADDRESS 26 + 2 * 20 + 30 = 96; STAKES 22; CURIOSITY 24 + 3 * 17 (why nobody reason) + 18 (?) = 93;
#   BREVITY 100 -> mean 69 -> 41.4 + 8.8 = 50.2 -> 50 WEAK; The Question matches twice (^why, ? at the end)
HA="You are wasting 3 hours a week on edits, and here is why."
HB="Why nobody tells you the real reason your videos stall?"
printf '%s\n\n%s\n' "$HB" "$HA" > hooks.txt
tool yt-script hookscore hooks.txt --json
eq "$CODE" 0 "hookscore rc"
eq "$(j '[.[] | [.properties.SPECIFICITY, .properties.ADDRESS, .properties.STAKES, .properties.CURIOSITY, .properties.BREVITY, .verdict, .band, .formula]]')" \
   '[[56,76,62,41,100,57,"WORKABLE","Unclassified"],[34,96,22,93,100,50,"WEAK","The Question"]]' "scores, ranked"
eq "$(j '.[0] | keys_unsorted')" '["hook","properties","verdict","band","formula"]' "no matched count in json"
tool yt-script hookscore hooks.txt
hashes() { local n=$1 s=""; while [ "$n" -gt 0 ]; do s+="#"; n=$((n-1)); done; printf '%s' "$s"; }
prop() { printf '    %-12s %3d  %s' "$1" "$2" "$(hashes $(($2 / 5)))"; }
WANT_A="
  $HA
  $(printf '%*s' ${#HA} '' | tr ' ' '-')
$(prop SPECIFICITY 56)
$(prop ADDRESS 76)
$(prop STAKES 62)
$(prop CURIOSITY 41)
$(prop BREVITY 100)
    VERDICT       57  WORKABLE
    formula      Unclassified  (no formula matched - that is usually a summary, not a hook)
    weakest      CURIOSITY - cut the half of the sentence that answers itself"
eq "$(echo "$OUT" | sed -n '1,11p')" "$WANT_A" "report A"
has "$OUT" "    formula      The Question  (2 patterns matched)
    weakest      STAKES - name what it costs them to keep doing it the current way" "report B"
eq "$(echo "$OUT" | tail -n1)" "  winner: $HA  (57, WORKABLE)" "winner"
tool yt-script hookscore --hook "stop"
has "$OUT" "  --------------------" "rule is at least 20 wide"
has "$OUT" "(1 pattern matched)" "singular pattern"
hasnt "$OUT" "winner" "one hook, no winner"
tool yt-script hookscore
eq "$CODE" 1 "no arguments: usage, exit 1"
eq "$(line 1)" "hookscore - score a YouTube hook before you waste a take on it." "usage"
tool yt-script hookscore missing.txt
eq "$CODE" 0 "a missing file prints the usage and exits 0 (as the original does)"

# ================================================================ yt-edit/deadair
cat > t.srt <<'EOF'
1
00:00:00,000 --> 00:00:02,000
So today I want to show you my editing setup.

2
00:00:02,100 --> 00:00:03,000
um

3
00:00:04,000 --> 00:00:06,000
So today I want to show you the real setup.

4
00:00:06,200 --> 00:00:09,500
It starts with the microphone.
EOF
# FILLER: cue 2 is only "um". DEAD: 3.0 -> 4.0 is a 1.00 s gap; keep 0.225 each side -> 3.225 .. 3.775.
# REPEAT: cue 3 restarts cue 1 ("so today i want to"), skipping the "um" between them.
tool yt-edit deadair t.srt --json
eq "$CODE" 0 "deadair rc"
eq "$(j '[.cuts[] | [.kind, .why]]')" '[["REPEAT","restart of \"so today i want to\""],["FILLER","um"],["DEAD","1.00s gap"]]' "cuts sorted by start"
has "$OUT" '"start": 3.225,' "dead start rounded to 3 places"
has "$OUT" '"end": 3.775,' "dead end"
has "$OUT" '"duration": 9.5,' "duration"
has "$OUT" '"removed": 3.45,' "removed: 2.0 + 0.9 + 0.55"
has "$OUT" '"out": 6.05' "runtime after the cuts"
eq "$(j '.source')" '"t.srt"' "source"
tool yt-edit deadair t.srt
eq "$OUT" "
  t.srt   9.50s in, 4 cues, dead-air floor 0.45s

$(printf '    %-7s %8s -> %8s   %5ss   %s' REPEAT 0.00 2.00 2.00 'restart of "so today i want to"')
$(printf '    %-7s %8s -> %8s   %5ss   %s' FILLER 2.10 3.00 0.90 um)
$(printf '    %-7s %8s -> %8s   %5ss   %s' DEAD 3.23 3.77 0.55 '1.00s gap')

  3 cuts, 3.45s removed, 6.05s out (36.3% shorter)" "deadair text"
tool yt-edit deadair t.srt --floor 2
has "$OUT" "dead-air floor 2.0s" "--floor as a float"
has "$OUT" "  2 cuts," "no gap is over 2 s"
cat > w.json <<'EOF'
{"segments": [{"start": 0, "end": 1.5, "text": " hello there everyone "}, {"start": 3, "end": 4, "text": "uh, okay"}, {"start": 4, "end": 5, "text": null}]}
EOF
tool yt-edit deadair w.json --json
eq "$(j '[.cuts[] | [.kind, .start, .end, .why]]')" '[["DEAD",1.725,2.775,"1.50s gap"],["FILLER",3.0,4.0,"uh, okay"]]' "whisper json (0.225 kept each side)"
echo '[{"start": 0, "end": 1, "text": "x"}]' > list.json
tool yt-edit deadair list.json
eq "$CODE" 1 "a bare list has no .get (AttributeError, as the original)"
has "$ERR" "'list' object has no attribute 'get'" "list message"
printf 'WEBVTT\n\n00:01.000 --> 00:02.500\nhi \xff there\n' > v.vtt
tool yt-edit deadair v.vtt --json
eq "$(j '.cuts')" '[]' "vtt mm:ss timing"
has "$OUT" '"duration": 2.5,' "vtt end"
has "$OUT" '"removed": 0,' "no cuts: sum() is the int 0"
printf 'nothing here\n' > none.srt
tool yt-edit deadair none.srt
eq "$CODE:$OUT" "1:no cues found - is this an srt, vtt or whisper json?" "no cues"
printf '00:00:01.2.3 --> 00:00:02,000\nx\n' > badts.srt
tool yt-edit deadair badts.srt
eq "$CODE" 1 "bad timestamp: ValueError"
tool yt-edit deadair
eq "$CODE" 1 "usage"
has "$OUT" "  REPEAT  a sentence restarted - the second take of the same opening" "usage text"

# ================================================================ yt-chapters/chapters
cat > ch.srt <<'EOF'
00:00:00,000 --> 00:00:04,000
welcome to the channel today we talk about cameras

00:00:04,200 --> 00:00:08,000
cameras need light and lenses for sharp images

00:00:08,200 --> 00:00:12,000
lenses matter more than camera bodies honestly

00:00:14,000 --> 00:00:18,000
audio is the other half microphones matter

00:00:18,200 --> 00:00:22,000
microphones placed close sound warmer audio

00:00:22,200 --> 00:00:26,000
audio cables and interfaces round it out

00:00:29,000 --> 00:00:33,000
editing software choices come next timeline

00:00:33,200 --> 00:00:37,000
timeline editing with shortcuts saves hours

00:00:37,200 --> 00:00:41,000
editing done export settings finish everything
EOF
# The 3 s pause at 29 (no shared words either side: 3 * 1.6 + 3.2 = 8.0) and the 2 s pause at 14 are the only
# boundaries 10 s from 0, from each other and from the end (41). Titles: the three most frequent keywords
# (str.count is a substring count: "camera" also counts inside "cameras", 3).
tool yt-chapters chapters ch.srt --json
eq "$CODE" 0 "chapters rc"
eq "$(j '.valid')" true "valid"
eq "$(j '[.chapters[] | [.label, .draft_title]]')" '[["0:00","Camera Cameras Lenses"],["0:14","Audio Microphones Other"],["0:29","Editing Timeline Software"]]' "chapters"
has "$OUT" '"start": 0.0,' "start is a float"
has "$OUT" '"seconds": 14.0' "first chapter length"
tool yt-chapters chapters ch.srt
eq "$OUT" "
  0:00 Camera Cameras Lenses
  0:14 Audio Microphones Other
  0:29 Editing Timeline Software

  3 chapters
  Retitle every line before pasting. These are the topic words, not your words." "chapters text"
tool yt-chapters chapters ch.srt --target 2
eq "$(line 2):$(line 3)" "  0:00 Camera Audio Cameras:  0:29 Editing Timeline Software" "--target 2 keeps the strongest boundary"
has "$OUT" "  2 chapters  -- INVALID: YouTube needs 3+, a 00:00 first entry and 10s minimum each" "invalid"
head -n 11 ch.srt > few.srt
tool yt-chapters chapters few.srt
eq "$CODE:$OUT" "1:too few cues to chapter" "too few cues"
tool yt-chapters chapters ch.srt --target x
eq "$CODE" 1 "--target must be an int"
tool yt-chapters chapters
eq "$CODE" 1 "usage"
eq "$(line 1)" "chapters - YouTube chapters from a timestamped transcript." "usage line"

# ================================================================ yt-retention/retention
printf 'Video position (%%),Absolute audience retention (%%)\n0,100\n5,80\n10,72\n20,70\n30,69\n40,60\n50,59\n60,58\n70,57\n80,56\n' > r.csv
# percent axis (max 80), no duration -> the hook is the first 10%: 100 -> 72 = 28 lost (leaking).
# drops per unit, steepest first: 0->5 (4.0/unit, 20 lost), 5->10 (1.6, 8), 30->40 (0.9, 9), 10->20 (0.2, 2), 70->80 (0.1, 1)
# slide: the per-unit drops starting past 10: 0.9 and five of 0.1 -> 1.4 / 6 = 0.233
tool yt-retention retention r.csv --json
eq "$CODE" 0 "retention rc"
eq "$(j '[.points, .hook_leak, .slide_per_unit, .said]')" '[10,28.0,0.233,{}]' "summary values"
eq "$(j '[.cliffs[] | [.from, .to, .lost, .at_seconds]]')" '[[0.0,5.0,20.0,null],[5.0,10.0,8.0,null],[30.0,40.0,9.0,null],[10.0,20.0,2.0,null],[70.0,80.0,1.0,null]]' "cliffs"
has "$OUT" '"start": 100.0,' "start"
has "$OUT" '"end": 56.0,' "end"
tool yt-retention retention r.csv
eq "$OUT" "
  r.csv   10 points   100.0% -> 56.0%

  HOOK LEAK   28.0% lost in the opening   [leaking]
              under 25 is healthy for this length. Fix the first line before anything else.

  CLIFFS      the moments people actually left
    - 20.0%  at      0.0
    -  8.0%  at      5.0
    -  9.0%  at     30.0
    -  2.0%  at     10.0
    -  1.0%  at     70.0

  SLIDE       0.233% per unit across the middle
              a flat slide is pacing, not content. Cut the middle, do not rewrite it." "retention text"
# --duration 200: positions become seconds (x / 100 * 200); the hook is the first 15%
tool yt-retention retention r.csv --duration 200 --transcript t.srt --json
eq "$(j '[.cliffs[].at_seconds]')" '[0.0,10.0,60.0,20.0,140.0]' "cliff seconds"
eq "$(j '.said')" '{"0.0":"So today I want to show you my editing setup. um So today I want to show you the real setup.","5.0":"So today I want to show you the real setup. It starts with the microphone.","30.0":"","10.0":"","70.0":""}' "what was said near each cliff (+-4 s)"
tool yt-retention retention r.csv --duration 200 --transcript t.srt
eq "$(echo "$OUT" | sed -n '8,9p')" '    - 20.0%  at       0s   "So today I want to show you my editing setup. um So today I want to show you the real setup."
    -  8.0%  at      10s   "So today I want to show you the real setup. It starts with the microphone."' "cliff lines with the transcript"
# seconds axis (max 240): the hook is x <= 30. A 0 start reads as 100 ("ys[0] or 100.0"), but the min over the hook still
# sees the 0, so all 100 points count as lost. Without the BOM skipped the first row would not parse (7 rows: too few).
printf '\xef\xbb\xbf0,0\n10,90\n20,80\n30,75\n60,74\n120,73\n180,72\n240,71\n' > s.csv
tool yt-retention retention s.csv --json
eq "$CODE:$(j '[.points, .start, .hook_leak]')" '0:[8,100.0,100.0]' "a BOM is skipped; a 0 start reads as 100"
printf '0,100\n1,90\n' > short.csv
tool yt-retention retention short.csv
eq "$CODE:$OUT" "1:could not read at least 8 data points from that csv" "too few points"
tool yt-retention retention
eq "$CODE" 1 "usage"
has "$OUT" "  HOOK LEAK    what you lost in the first 30 seconds" "usage text"

# ================================================================ li-human, ig-human: the master detector and humanizer, skill-local slop.json
LI="$PK/../../astro-linkedin/skills/li-human"; IG="$PK/../../astro-instagram/skills/ig-human"
printf 'In todays fast-paced world, it is important to note that we must delve into the landscape. Moreover, leverage synergy.\n' > d.txt
run --skill-dir "$LI" li-human/detect d.txt --json
A="$OUT"; AC=$CODE
run --skill-dir "$PK/../../astro-master/skills/master-virality" master-virality/detect d.txt --json --lexicon "$LI/slop.json"
eq "$A" "$OUT" "li-human/detect = the virality detector with li-human's slop.json"
eq "$AC" 0 "--json exits 0"
run --skill-dir "$IG" ig-human/detect d.txt
B="$OUT"; BC=$CODE
run --skill-dir "$PK/../../astro-master/skills/master-virality" master-virality/detect d.txt --lexicon "$IG/slop.json"
eq "$B:$BC" "$OUT:$CODE" "ig-human/detect = the virality detector with ig-human's slop.json"
run --skill-dir "$LI" li-human/humanize d.txt --json
A="$OUT"
run --skill-dir "$PK/../../astro-master/skills/master-virality" master-virality/humanize d.txt --json --lexicon "$LI/slop.json"
eq "$A" "$OUT" "li-human/humanize = the virality humanizer with li-human's slop.json"
run --skill-dir "$TMP/empty/yt-viral" li-human/detect d.txt
eq "$CODE" 1 "no slop.json next to the skill"

done_ "youtube pack + li/ig-human"

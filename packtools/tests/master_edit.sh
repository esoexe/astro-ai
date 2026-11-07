#!/usr/bin/env bash
# master-edit/{probe,deadspace,captions,stopmotion}: every case of master-skills/skills/edit/tools/test_edit.py, plus exit codes,
# messages, the CPython-seeded wobble, a libass-less ffmpeg and a stand-in whisper.cpp. Fixtures are made with ffmpeg lavfi
# (tone bursts stand in for speech: 0.5-2.0, 3.2-4.5, 4.8-6.0 s; a faint 7.9 kHz floor stands in for room noise).
# Like the original, the suite is skipped when ffmpeg/ffprobe are not installed.
source "$(dirname "$0")/lib.sh"
if ! command -v ffmpeg >/dev/null || ! command -v ffprobe >/dev/null; then echo "master-edit: skipped (ffmpeg/ffprobe not on PATH)"; exit 0; fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
FF=$(command -v ffmpeg)
ff() { "$FF" -nostdin -hide_banner -v error -y "$@"; }
SPEECH="between(t,0.5,2.0)+between(t,3.2,4.5)+between(t,4.8,6.0)"
TONE="aevalsrc='0.3*sin(2*PI*220*t)*($SPEECH)+0.0005*sin(2*PI*7919*t)':s=48000:d=7"
ff -f lavfi -i testsrc2=s=180x320:r=30:d=7 -f lavfi -i "$TONE" -c:v libx264 -pix_fmt yuv420p -c:a aac -b:a 192k -shortest talk.mp4
ff -f lavfi -i "$TONE" -c:a pcm_s16le talk.wav
ff -f lavfi -i testsrc2=s=180x320:r=30:d=1 -c:v libx264 -pix_fmt yuv420p noaudio.mp4
# words aligned to the bursts: burst 2 is "um", burst 1 is an abandoned attempt that burst 3 repeats
cat > talk.words.json <<'EOF'
{"segments": [{"words": [{"word": " So", "start": 0.5, "end": 0.8}, {"word": " the", "start": 0.85, "end": 1.1},
 {"word": " first", "start": 1.15, "end": 1.5}, {"word": " thing", "start": 1.55, "end": 1.95}, {"word": " um", "start": 3.25, "end": 4.45},
 {"word": " So", "start": 4.8, "end": 5.0}, {"word": " the", "start": 5.05, "end": 5.2}, {"word": " first", "start": 5.25, "end": 5.5},
 {"word": " thing", "start": 5.55, "end": 5.75}, {"word": " works.", "start": 5.8, "end": 5.98}]}]}
EOF
frame_md5s() { "$FF" -v error -i "$1" -map 0:v -f framemd5 - | grep -v '^#' | awk -F, '{gsub(/ /,"",$NF); print $NF}'; }
# seconds of "speech" in a file: 10 ms frames above -25 dBFS after the same 70 Hz high-pass and 16 kHz resample as the tool
speech_secs() {
  "$FF" -nostdin -v error -i "$1" -map 0:a:0 -vn -af highpass=f=70,aresample=16000 -ac 1 -f s16le -acodec pcm_s16le - |
  node -e 'const c=[];process.stdin.on("data",d=>c.push(d)).on("end",()=>{const b=Buffer.concat(c);let n=0;
    for(let o=0;o+320<=b.length;o+=320){let p=0;for(let k=0;k<160;k++){const v=b.readInt16LE(o+2*k);p+=v*v;}
      const l=p?10*Math.log10(p/160/1073741824):-120;if(l>-25)n++;}console.log((n*0.01).toFixed(2));});'
}
PR=(master-edit/probe); DS=(master-edit/deadspace); CP=(master-edit/captions); SM=(master-edit/stopmotion)

# ---- probe (upstream test_probe_reports_streams)
run "${PR[@]}" talk.mp4 --json
eq "$CODE" 0 "probe exit 0"
eq "$(echo "$OUT" | jq -c '[.video.display_width, .video.fps, .video.vfr, .audio.channels, .video.r_frame_rate, .video.codec, .audio.codec]')" '[180,30.0,false,1,"30","h264","aac"]' "probe talk.mp4: 180 wide, 30 fps CFR, mono"
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["path","format","duration","end","size","bit_rate","video","audio","audio_streams","notes"]' "probe JSON keys, in order"
eq "$(echo "$OUT" | jq -c '.video | keys_unsorted')" '["index","codec","profile","width","height","rotation","display_width","display_height","pix_fmt","bit_depth","fps","r_frame_rate","avg_frame_rate","vfr","color_primaries","color_transfer","color_space","color_range","hdr","start","offset","duration","nb_frames"]' "video keys, in order"
eq "$(echo "$OUT" | jq -r '.path')" "$TMP/talk.mp4" "path is absolute"
eq "$(echo "$OUT" | jq -r '.notes[0]')" "mono audio: fine for voice; finish keeps it mono unless a music bed is added" "mono note"
eq "$(echo "$OUT" | jq '.notes | length')" 1 "180/320 = 0.5625 is exactly 9:16: no aspect note"
run "${PR[@]}" talk.wav --json
eq "$(echo "$OUT" | jq -c '[.video, .audio.codec, .audio.sample_rate, .notes]')" '[null,"pcm_s16le",48000,["mono audio: fine for voice; finish keeps it mono unless a music bed is added"]]' "probe talk.wav: no video, so no aspect note"
run "${PR[@]}" talk.mp4
eq "$(echo "$OUT" | sed -n 3,4p)" $'  video  h264 180x320 @ 30 fps, yuv420p, 8-bit\n  audio  aac 48000 Hz, 1 ch (mono)' "text summary lines"
has "$(echo "$OUT" | sed -n 2p)" "  container mov,mp4,m4a,3gp,3g2,mj2, 7.0" "container line"
run "${PR[@]}" talk.mp4 noaudio.mp4 --json
eq "$(echo "$OUT" | jq -c '[(.files | length), .files[1].audio, .files[1].notes[0]]')" '[2,null,"no audio stream: dead-space cutting and transcription need audio"]' "two files: {files: [...]}"
run "${PR[@]}" talk.mp4 --loudness --json
eq "$(echo "$OUT" | jq -c '.loudness | [keys_unsorted, (.integrated_lufs < -5 and .integrated_lufs > -30), (.true_peak_dbtp <= 0)]')" '[["integrated_lufs","lra_lu","true_peak_dbtp"],true,true]' "--loudness: R128 numbers"
run "${PR[@]}"
eq "$CODE" 2 "no file and no --doctor: exit 2"
has "$ERR" "probe: error: give a media file or --doctor" "usage error"
run "${PR[@]}" nope.mp4
eq "$CODE" 2 "missing file: exit 2"; eq "$ERR" "error: file not found: nope.mp4" "missing file message"
echo "not media" > fake.mp4
run "${PR[@]}" fake.mp4
eq "$CODE" 2 "unreadable media: exit 2"; has "$ERR" "error: ffprobe cannot read fake.mp4: " "ffprobe error text"
OUT=$(EDIT_FFPROBE=/no/such/ffprobe $T "${PR[@]}" talk.mp4 2>&1); CODE=$?
eq "$CODE" 3 "EDIT_FFPROBE pointing nowhere: exit 3"; eq "$OUT" "error: cannot run /no/such/ffprobe: [Errno 2] No such file or directory: '/no/such/ffprobe'" "cannot run message"
OUT=$(PATH=/nonexistent $T "${PR[@]}" talk.mp4 2>&1); CODE=$?
eq "$CODE" 3 "no ffprobe on PATH: exit 3"; has "$OUT" "error: ffprobe not found on PATH. Install ffmpeg first" "install hint"
run "${PR[@]}" --doctor --json
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["engine","platform","ffmpeg","filters","alimiter_latency","encoders","whisper_cpp","node","hyperframes_ready","npx"]' "doctor keys"
eq "$(echo "$OUT" | jq -c '[.filters.ass, .filters.acrossfade, .encoders.libx264, .encoders.aac, (.ffmpeg.version | startswith("ffmpeg version"))]')" '[true,true,true,true,true]' "doctor sees this ffmpeg"
eq "$CODE" 0 "doctor exit 0"
OUT=$(PATH=/nonexistent $T "${PR[@]}" --doctor 2>&1); CODE=$?
eq "$CODE" 3 "doctor without ffmpeg: exit 3"; has "$OUT" "ffmpeg: MISSING (ffmpeg not found on PATH." "doctor MISSING line"
has "$OUT" "whisper.cpp: not found (captions needs --words" "doctor whisper line"
run "${PR[@]}" --help
has "$OUT" $'exit codes:\n  0  success\n  1  ffmpeg or runtime failure' "raw epilog with the exit codes"

# ---- deadspace (upstream test_deadspace_dry_run_cuts_long_pause_only)
run "${DS[@]}" talk.mp4 --dry-run --json -o "$TMP/dry.mp4"
eq "$CODE" 0 "dry run exit 0"
EDL=$(echo "$OUT" | jq -r '.edl')
eq "$EDL" "$TMP/dry.edl.json" "EDL next to the output"
eq "$(jq '.cuts | length' "$EDL")" 3 "three cuts: head, the 1.2 s gap, tail"
eq "$(jq '[.cuts[] | select(.start > 2.0 and .start < 2.3 and .end > 3.0 and .end < 3.2)] | length' "$EDL")" 1 "the long pause is cut"
eq "$(jq '[.cuts[] | select(.start < 4.6 and .end > 4.6)] | length' "$EDL")" 0 "the 0.3 s pause stays"
eq "$(echo "$OUT" | jq '.saved_s > 1.8')" true "saved > 1.8 s"
[ ! -e "$TMP/dry.mp4" ] && ok || bad "dry run must not render"
eq "$(echo "$OUT" | jq -c 'keys_unsorted')" '["input","edl","cuts","by_reason","source_s","output_s","saved_s","saved_pct","threshold_db","rendered"]' "summary keys"
eq "$(jq -c 'keys_unsorted' "$EDL")" '["version","tool","source","duration","fps","settings","analysis","note","cuts","keep","output"]' "EDL keys"
eq "$(jq -c '[.fps, .settings, (.cuts[0] | keys_unsorted), .cuts[0].reason, .cuts[0].text, .cuts[0].apply, .analysis.levels.reliable]' "$EDL")" \
   '["30",{"pace":"normal","min_silence":0.35,"pad_before":0.08,"pad_after":0.14,"min_cut":0.12,"crossfade_ms":30.0,"retakes":"fuzzy"},["start","end","reason","text","apply"],"silence",null,true,true]' "EDL settings and cut shape"
# keeps sit on the 30 fps grid: every boundary * 30 is a whole frame
eq "$(jq '[.keep[] | .start, .end | . * 30 | (. - (. | round)) | fabs < 0.002] | all' "$EDL")" true "keeps snap to the frame grid"
eq "$(sed -n '1p;2p' "$EDL")" $'{\n  "version": 1,' "EDL: indent 2"
eq "$(tail -c 2 "$EDL" | od -An -tx1 | tr -d ' ')" "7d0a" "EDL ends with a newline"

# ---- upstream test_deadspace_render_keeps_speech_and_sync
run "${DS[@]}" talk.mp4 -o "$TMP/cut.mp4" --json
eq "$CODE" 0 "render exit 0"
INFO=$($T "${PR[@]}" cut.mp4 --json)
eq "$(jq -n --argjson i "$INFO" --argjson s "$OUT" '[(($i.video.duration - $i.audio.duration) | fabs) < 0.04, (($i.video.duration - $s.output_s) | fabs) < 0.05, $s.rendered, ($s.av_drift_ms < 40)]')" \
   $'[\n  true,\n  true,\n  true,\n  true\n]' "A/V in sync, duration as reported"
eq "$(echo "$INFO" | jq '.video.nb_frames')" 145 "every kept frame rendered: (2.1667-0.4)*30 + (6.1667-3.1)*30 = 53 + 92"
SP=$(speech_secs cut.mp4)
eq "$(node -e "console.log(Math.abs($SP - 4.0) <= 0.12)")" true "all 4.0 s of speech survived (got $SP)"
run "${DS[@]}" talk.mp4 -o "$TMP/cut2.mp4"
eq "$(echo "$OUT" | head -1)" "deadspace: 3 cuts (silence 3), threshold $(jq -r '.analysis.threshold_db' "$EDL") dBFS" "text summary"
has "$OUT" "  wrote: $TMP/cut2.mp4  (A/V drift " "text: wrote + drift"

# ---- upstream test_deadspace_audio_only_and_no_clicks
run "${DS[@]}" talk.wav -o "$TMP/cut.wav"
eq "$CODE" 0 "audio-only exit 0"
eq "$($T "${PR[@]}" cut.wav --json | jq -r '.audio.codec')" pcm_s24le "wav out is 24-bit PCM"
STEP=$("$FF" -v error -i cut.wav -f f32le -acodec pcm_f32le - | node -e 'const c=[];process.stdin.on("data",d=>c.push(d)).on("end",()=>{const b=Buffer.concat(c);
  let m=0;for(let o=4;o+4<=b.length;o+=4){const d=Math.abs(b.readFloatLE(o)-b.readFloatLE(o-4));if(d>m)m=d;}console.log(m, b.length/4/48000);});')
read -r MAXSTEP SECS <<<"$STEP"
eq "$(node -e "console.log($MAXSTEP < 0.3*2*3.1416*220/48000*1.5, $SECS < 5.2)")" "true true" "no clicks at the joins, shorter than 5.2 s ($STEP)"
run "${DS[@]}" talk.wav -o "$TMP/cut.ogg"
eq "$CODE" 2 "unsupported audio type: exit 2"; eq "$ERR" "error: unsupported audio output .ogg" "unsupported audio message"
run "${DS[@]}" talk.mp4 -o "$TMP/cut.ogg"
eq "$CODE" 2 "video to an odd extension: exit 2"; eq "$ERR" "error: unsupported output extension .ogg; use .mp4/.mov/.mkv or an audio type" "unsupported extension message"

# ---- upstream test_deadspace_no_audio_is_usage_error
run "${DS[@]}" noaudio.mp4 --dry-run
eq "$CODE" 2 "no audio: exit 2"; has "$ERR" "no audio" "no audio message"
run "${DS[@]}" talk.mp4 --dry-run --audio-stream 3
eq "$CODE" 2 "missing stream: exit 2"; eq "$ERR" "error: there is no audio stream #3" "missing stream message"

# ---- upstream test_deadspace_transcript_fillers_retakes_and_edl_veto
run "${DS[@]}" talk.mp4 -o "$TMP/tx.mp4" --transcript talk.words.json --json
eq "$CODE" 0 "transcript render exit 0"
eq "$(echo "$OUT" | jq -c '[(.by_reason | has("filler")), (.by_reason | has("retake")), (.output_s < 1.6)]')" '[true,true,true]' "fillers + retakes cut, only the last take remains"
WORDS=$(echo "$OUT" | jq -r '.words')
eq "$(jq -c '[.words[].text]' "$WORDS")" '["So","the","first","thing","works."]' "words on the cut timeline"
eq "$(jq -c '[.kind, .engine, .language_code, (.words[0] | keys_unsorted), .source.path]' "$WORDS")" "[\"words\",\"remapped by deadspace\",\"en\",[\"text\",\"start\",\"end\",\"type\"],\"$TMP/tx.mp4\"]" "words.json schema"
TXEDL=$(echo "$OUT" | jq -r '.edl'); TXS=$(echo "$OUT" | jq '.output_s')
jq '.cuts |= map(if (.reason | contains("retake")) then .apply = false else . end)' "$TXEDL" > edited.edl.json
run "${DS[@]}" talk.mp4 -o "$TMP/veto.mp4" --from-edl edited.edl.json --json
eq "$CODE" 0 "--from-edl exit 0"
eq "$(echo "$OUT" | jq --argjson s "$TXS" '.output_s > $s + 1.2')" true "vetoing the retake keeps it"
eq "$(jq -c '[.cuts[] | select(.apply == false) | .reason] | length > 0' "$(echo "$OUT" | jq -r '.edl')")" true "the vetoed cut stays in the new EDL"
run "${DS[@]}" talk.mp4 -o "$TMP/tx2.mp4" --transcript talk.words.json --dry-run
# head silence 0-0.42, retake 0.0-(4.8-0.08), filler (1.95+0.14)-(4.8-0.08) merge in start order
eq "$(echo "$OUT" | sed -n '1p;4p')" $'deadspace: 2 cuts (filler 1, retake 1, silence 2), threshold '"$(jq -r '.analysis.threshold_db' "$EDL")"$' dBFS\n  silence+retake+filler 0:00.00-0:04.72  So the first thing um' "text lists the merged cut"

# ---- upstream test_retake_detection_unit, through --threshold 0 (nothing is loud, so only transcript cuts are made)
mk_words() { node -e 'const t=process.argv[1].split(" ");console.log(JSON.stringify(t.map((x,i)=>({text:x,start:i*0.5,end:i*0.5+0.3}))))' "$1"; }
mk_words "so the first so the first thing" > r1.json
OUT=$($T "${DS[@]}" talk.wav --threshold 0 --transcript r1.json --dry-run --json -o r1.wav 2>/tmp/pt_err); ERR=$(cat /tmp/pt_err)
eq "$(jq -c '[.cuts[] | [.reason, .text]]' r1.edl.json)" '[["retake","so the first"]]' "exact retake: 'so the first'"
eq "$ERR" "warning: no speech found above the gate; nothing to cut" "threshold 0 warns"
mk_words "we build tools and we ship them" > r2.json
$T "${DS[@]}" talk.wav --threshold 0 --transcript r2.json --dry-run -o r2.wav >/dev/null 2>&1
eq "$(jq -c '.cuts' r2.edl.json)" '[]' "different lines are not retakes"
# fuzzy: 4 of 5 tokens match (SequenceMatcher ratio 2*4/10 = 0.8) after a 0.4 s pause; with --retakes exact it stays
cat > r3.json <<'EOF'
[{"text":"I","start":0.0,"end":0.2},{"text":"think","start":0.3,"end":0.5},{"text":"we","start":0.6,"end":0.8},{"text":"should","start":0.9,"end":1.1},{"text":"go","start":1.2,"end":1.4},
 {"text":"I","start":1.8,"end":2.0},{"text":"think","start":2.1,"end":2.3},{"text":"we","start":2.4,"end":2.6},{"text":"could","start":2.7,"end":2.9},{"text":"go","start":3.0,"end":3.2}]
EOF
$T "${DS[@]}" talk.wav --threshold 0 --transcript r3.json --dry-run -o r3.wav >/dev/null 2>&1
eq "$(jq -c '[.cuts[] | [.reason, .text, .start, .end]]' r3.edl.json)" '[["retake","I think we should go",0.0,1.72]]' "fuzzy retake (ratio 0.8) cut up to the restart's pad"
$T "${DS[@]}" talk.wav --threshold 0 --transcript r3.json --dry-run --retakes exact -o r3.wav >/dev/null 2>&1
eq "$(jq -c '.cuts' r3.edl.json)" '[]' "--retakes exact ignores the near-repeat"
# merge_cuts: overlapping filler + retake merge their reasons and texts
mk_words "so um so the first so the first thing" > r4.json
$T "${DS[@]}" talk.wav --threshold 0 --transcript r4.json --dry-run -o r4.wav >/dev/null 2>&1
# filler (0.3+0.1)-(1.0-0.08) = 0.4-0.92 touches the retake (0.8+0.1)-(2.5-0.08) = 0.9-2.42: one cut, reasons and texts joined
eq "$(jq -c '[.cuts[] | [.reason, .text, .start, .end]]' r4.edl.json)" '[["filler+retake","um so the first",0.4,2.42]]' "merge_cuts: silence+filler style joining"
mk_words "so um the so um the end" > r5.json
$T "${DS[@]}" talk.wav --threshold 0 --transcript r5.json --dry-run -o r5.wav >/dev/null 2>&1
# retake "so the" 0.0-(1.5-0.08) swallows the first um (0.4-0.92); the second um (1.9-2.42) stays apart
eq "$(jq -c '[.cuts[] | [.reason, .text, .start, .end]]' r5.edl.json)" '[["retake+filler","so the um",0.0,1.42],["filler","um",1.9,2.42]]' "overlapping cuts merge in start order"
eq "$(jq -c '.keep' r5.edl.json)" '[{"start":1.42,"end":1.9},{"start":2.42,"end":7.0}]' "keeps between the cuts"
printf '{"cuts":[{"start":1,"end":2,"reason":"manual"}]}' > manual.edl.json
$T "${DS[@]}" talk.wav --from-edl manual.edl.json --dry-run -o m.wav >/dev/null 2>&1
eq "$(jq -c '[.keep, .cuts, .analysis]' m.edl.json)" '[[{"start":0.0,"end":1},{"start":2,"end":7.0}],[{"start":1,"end":2,"reason":"manual"}],{"levels":null,"threshold_db":null}]' "manual EDL: ints stay ints, like Python"
run "${DS[@]}" talk.wav --from-edl manual.edl.json --dry-run -o m.wav
eq "$(echo "$OUT" | head -1)" "deadspace: 1 cuts (manual 1), threshold None dBFS" "manual cut summary"
has "$OUT" "           manual 0:01.00-0:02.00  " "manual cut listed"
printf '{"cuts":[{"start":0,"end":99,"reason":"manual"}]}' > all.edl.json
run "${DS[@]}" talk.wav --from-edl all.edl.json --dry-run
eq "$CODE" 2 "everything cut: exit 2"; eq "$ERR" "error: every second would be cut; check --threshold" "everything cut message"

# ---- captions (upstream test_chunking_rules): --case keep --keep-punctuation shows the raw words
node -e 'const t="Stop editing videos by hand. I built a tool for the job".split(" ");console.log(JSON.stringify(t.map((x,i)=>({text:x,start:i*0.4,end:i*0.4+0.35}))))' > chunk.json
run "${CP[@]}" chunk.json --case keep --keep-punctuation -o "$TMP/ch" --json
eq "$CODE" 0 "chunking exit 0"
CUES="$TMP/ch/chunk.cues.json"
eq "$(jq '[.cues[] | .words | length <= 3] | all' "$CUES")" true "<= 3 words per cue"
eq "$(jq -r '[.cues[].text] | (map(endswith("hand.")) | index(true)) as $k | .[$k + 1] | startswith("I")' "$CUES")" true "breaks after the sentence end"
eq "$(jq '[.cues[:-1][] | .text | split(" ") | last | IN("a","the","for")] | any' "$CUES")" false "never ends on a weak opener"
eq "$(jq '[.cues as $c | range(1; $c | length) | $c[. - 1].end <= $c[.].start + 1e-9] | all' "$CUES")" true "cues never overlap"
eq "$(jq -c '[.cues[].text]' "$CUES")" '["Stop editing","videos by hand.","I built","a tool","for the job"]' "the phrases (Stop editing videos is 19 chars > 18)"

# ---- upstream test_captions_files_and_safe_zone
run "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/caps" --size 1080x1920 --json
eq "$CODE" 0 "captions exit 0"
C="$TMP/caps/talk.cues.json"
eq "$(jq -c '[.height, ([.cues[] | (.words | length) >= 1 and (.words | length) <= 3] | all), ([.cues[].text] | join(" ") | contains("UM")), .safe_area, .style]' "$C")" \
   '[1920,true,false,{"x0":90,"y0":240,"x1":950,"y1":1470},{"case":"upper","font":"Arial","color":"#FFFFFF","accent":"#FFD400","position":"lower","anchor_y":1440,"align":"bottom"}]' "cues.json: 1920 tall, 1-3 words, no UM, safe area"
ASS=$(cat "$TMP/caps/talk.ass")
has "$ASS" "PlayResY: 1920" "ASS canvas"
has "$ASS" ",2,140,140,480,1" "bottom-centre, text bottom at y 1440, clear of the rail"
has "$ASS" '\c&H0000D4FF&' "active word in the accent colour"
has "$ASS" 'Style: Caption,Arial,84,&H00FFFFFF,&H00FFFFFF,&H00000000,&H64000000,-1,0,0,0,100,100,0,0,1,6,2,2,140,140,480,1' "style line: 84 px, outline 6"
has "$ASS" 'Dialogue: 0,0:00:00.50,0:00:00.85,Caption,,0,0,0,,{\c&H0000D4FF&}SO{\r} THE FIRST' "first dialogue: SO highlighted"
SRT=$(cat "$TMP/caps/talk.srt")
eq "${SRT:0:19}" $'1\n00:00:00,500 --> ' "SRT starts at 0.5 s"
eq "$(echo "$OUT" | jq -c '[.words, .cues, (.files | keys_unsorted)]')" '[10,4,["srt","ass","cues"]]' "summary"
eq "$(jq -c '[.cues[].text]' "$C")" '["SO THE FIRST","THING","SO THE FIRST","THING WORKS"]' "upper case, no trailing period, no UM"
run "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/seam" --position seam --no-highlight
has "$(cat "$TMP/seam/talk.ass")" 'Dialogue: 0,0:00:00.50,0:00:01.55,Caption,,0,0,0,,{\an5\pos(90,160)}SO THE FIRST' "seam: centred \\pos, no highlight"
eq "$(echo "$OUT" | head -1)" "captions: 4 phrases from 10 words" "text summary"
run "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/x" --color red
eq "$CODE" 2 "bad colour: exit 2"; eq "$ERR" "error: bad colour 'red', use #RRGGBB" "bad colour message"
run "${CP[@]}" talk.mp4 --words talk.words.json --max-words 9
eq "$CODE" 2 "--max-words 9: exit 2"; has "$ERR" "captions: error: --max-words must be 1-8" "max-words message"
run "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/x" --size 1081x1920
eq "$CODE" 2 "odd size: exit 2"; eq "$ERR" "error: size 1081x1920 must be even and at least 16 px" "odd size message"

# ---- upstream test_captions_burn_needs_libass: this ffmpeg has libass; a wrapper that hides it gives exit 3
run "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/b" --burn "$TMP/burned.mp4" --json
eq "$CODE" 0 "burn exit 0"; [ -s "$TMP/burned.mp4" ] && ok || bad "burned file written"
eq "$(echo "$OUT" | jq -c '[.burned, .files.burned]')" "[true,\"$TMP/burned.mp4\"]" "burned summary"
cat > noass.sh <<EOF
#!/usr/bin/env bash
if [[ " \$* " == *" -filters "* ]]; then "$FF" "\$@" | grep -vE ' (ass|subtitles) '; exit \${PIPESTATUS[0]}; fi
exec "$FF" "\$@"
EOF
chmod +x noass.sh
OUT=$(EDIT_FFMPEG="$TMP/noass.sh" $T "${CP[@]}" talk.mp4 --words talk.words.json -o "$TMP/nb" --burn "$TMP/nb.mp4" 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err)
eq "$CODE" 3 "no libass: exit 3"; has "$ERR" "HyperFrames" "no libass: HyperFrames route"
[ -f "$TMP/nb/talk.srt" ] && ok || bad "no libass: SRT still written"

# ---- upstream test_srt_input_gives_words
printf '1\n00:00:01,000 --> 00:00:02,000\nhello there world\n\n' > in.srt
run "${CP[@]}" in.srt --case keep -o "$TMP/srt" --json
eq "$(jq -c '[.cues[].words[] | .text]' "$TMP/srt/in.cues.json")" '["hello","there","world"]' "SRT words"
eq "$(jq -c '[.cues[0].words[0].start, (.cues[-1].words[-1].end <= 2.0), .source]' "$TMP/srt/in.cues.json")" '[1.0,true,null]' "SRT cue boundaries stay exact"
# character-weighted spread: weights 6,6,6 of 1.0 s, each word 0.92 of its span
eq "$(jq -c '[.cues[].words[] | [.start, .end]]' "$TMP/srt/in.cues.json")" '[[1.0,1.307],[1.333,1.64],[1.667,1.973]]' "word times inside the cue"
printf 'WEBVTT\n\n00:01.500 --> 00:03.000\n<v Bob>Hi <b>there</b>\n' > in.vtt
run "${CP[@]}" in.vtt --case keep -o "$TMP/vtt" --json
eq "$(jq -c '[.cues[].words[] | [.text, .start]]' "$TMP/vtt/in.cues.json")" '[["Hi",1.5],["there",2.0]]' "VTT: tags stripped, mm:ss.mmm"
# Deepgram, whisper.cpp offsets (ms) and millisecond lists are all found
echo '{"results":{"channels":[{"alternatives":[{"words":[{"word":"hey","punctuated_word":"Hey,","start":0.2,"end":0.4,"confidence":0.9}]}]}]}}' > dg.json
run "${CP[@]}" dg.json --case keep --keep-punctuation -o "$TMP/dg" --json
eq "$(jq -c '.cues[0].words' "$TMP/dg/dg.cues.json")" '[{"text":"Hey,","start":0.2,"end":0.4}]' "Deepgram words (punctuated_word first)"
echo '{"transcription":[{"offsets":{"from":1200,"to":1500},"text":" yes"},{"offsets":{"from":1500,"to":1900},"text":" no"}]}' > wc.json
run "${CP[@]}" wc.json --case keep -o "$TMP/wc" --json
eq "$(jq -c '[.cues[].words[] | [.text, .start, .end]]' "$TMP/wc/wc.cues.json")" '[["yes",1.2,1.5],["no",1.5,1.9]]' "whisper.cpp offsets in ms"
echo '[{"text":"a","start":1000,"end":1400},{"text":"b","start":2000,"end":2600}]' > ms.json
run "${CP[@]}" ms.json --case keep -o "$TMP/ms" --json
eq "$(jq -c '[.cues[].words[] | [.start, .end]]' "$TMP/ms/ms.cues.json")" '[[1.0,1.4],[2.0,2.6]]' "integer times over 1000 are milliseconds"
echo '{"nothing": [1, 2]}' > none.json
run "${CP[@]}" none.json
eq "$CODE" 2 "no word list: exit 2"; eq "$ERR" "error: no word timings found in that JSON (expected a list of {text, start, end})" "no word list message"
echo '{bad' > bad.json
run "${CP[@]}" talk.mp4 --words bad.json -o "$TMP/x"
eq "$CODE" 2 "bad JSON: exit 2"; eq "$ERR" "error: bad.json is not valid JSON: Expecting property name enclosed in double quotes: line 1 column 2 (char 1)" "bad JSON message"

# ---- transcription through whisper.cpp (a stand-in whisper-cli that writes the JSON the real one writes for -oj -ml 1 -sow)
cat > whisper-cli <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$(dirname "$0")/whisper.args"
while [ $# -gt 0 ]; do case "$1" in -of) OF="$2"; shift;; -f) WAV="$2"; shift;; esac; shift; done
[ -s "$WAV" ] || { echo "no wav" >&2; exit 1; }
cat > "$OF.json" <<'J'
{"result":{"language":"en"},"transcription":[{"offsets":{"from":500,"to":800},"text":" So"},{"offsets":{"from":800,"to":810},"text":"[BLANK_AUDIO]"},
 {"offsets":{"from":850,"to":1100},"text":" the"},{"offsets":{"from":1150,"to":1500},"text":" first."},{"offsets":{"from":3250,"to":4450},"text":" Um"},
 {"offsets":{"from":4800,"to":5000},"text":" it"},{"offsets":{"from":5050,"to":5980},"text":" works."}]}
J
EOF
chmod +x whisper-cli; : > model.bin
run "${CP[@]}" talk.mp4 --words-only -o "$TMP/tx"
eq "$CODE" 3 "no whisper.cpp: exit 3"; has "$ERR" "error: whisper.cpp is not installed, so I cannot transcribe." "install hint"
OUT=$(EDIT_WHISPER="$TMP/whisper-cli" $T "${CP[@]}" talk.mp4 --words-only -o "$TMP/tx" 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err)
eq "$CODE" 3 "no model: exit 3"; has "$ERR" "error: whisper.cpp model 'small' not found. Download https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin" "model hint"
OUT=$(EDIT_WHISPER="$TMP/whisper-cli" $T "${CP[@]}" talk.mp4 --words-only --model "$TMP/model.bin" --verbatim --language en -o "$TMP/tx" 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err)
eq "$CODE" 0 "transcribe exit 0"
eq "$OUT" "transcript: 6 words -> $TMP/tx/talk.words.json" "words-only line"
eq "$ERR" "transcribing with whisper.cpp $TMP/model.bin..." "progress line on stderr"
eq "$(jq -c '[[.words[].text], .engine, .language_code, .source.path, (.source.size > 0)]' "$TMP/tx/talk.words.json")" "[[\"So\",\"the\",\"first.\",\"Um\",\"it\",\"works.\"],\"whisper.cpp $TMP/model.bin\",\"en\",\"$TMP/talk.mp4\",true]" "words.json from whisper.cpp ([BLANK_AUDIO] dropped)"
eq "$(cat "$TMP/tx/talk.phrases.txt")" $'[0000.50-0001.50] So the first.\n[0003.25-0005.98] Um it works.' "phrases.txt: breaks at the 1.75 s pause, not the 0.35 s one"
has "$(cat whisper.args)" "-oj -of " "whisper-cli asked for JSON"
has "$(cat whisper.args)" "-ml 1 -sow -bs 5 -np -l en --prompt Um, so, uh, I mean" "word-level, beam 5, language and the verbatim prompt"
OUT=$(EDIT_WHISPER="$TMP/whisper-cli" $T "${CP[@]}" talk.mp4 --model "$TMP/model.bin" -o "$TMP/tx" --json 2>/tmp/pt_err); CODE=$?; ERR=$(cat /tmp/pt_err)
eq "$ERR" "using cached transcript $TMP/tx/talk.words.json" "second run reuses the transcript"
eq "$(echo "$OUT" | jq -c '[.words, (.files | keys_unsorted)]')" '[6,["words","phrases","srt","ass","cues"]]' "captions from the cache"
eq "$(jq -c '[.cues[].text]' "$TMP/tx/talk.cues.json")" '["SO THE FIRST","IT WORKS"]' "the Um is not shown"
: > whisper.args
EDIT_WHISPER="$TMP/whisper-cli" $T "${CP[@]}" talk.mp4 --model "$TMP/model.bin" -o "$TMP/tx" --retranscribe --device cpu >/dev/null 2>&1
has "$(cat whisper.args)" " -ng" "--retranscribe runs whisper again; --device cpu turns the GPU off"
run "${CP[@]}" noaudio.mp4 -o "$TMP/x"
eq "$CODE" 2 "nothing to transcribe: exit 2"; eq "$ERR" "error: no audio stream to transcribe; pass --words" "no audio message"

# ---- upstream test_stopmotion_frames_holds_and_determinism (mixed sizes and formats on purpose)
mkdir frames
for i in 0 1 2 3; do
  if [ $((i % 2)) -eq 1 ]; then e=png; s=320x240; else e=jpg; s=240x320; fi
  ff -f lavfi -i "testsrc2=s=$s:d=1" -ss "0.$((i * 2))" -frames:v 1 "frames/frame$((i + 1)).$e"
done
for x in "a 3" "b 3" "c 4"; do
  read -r n s <<<"$x"
  run "${SM[@]}" frames -o "$TMP/sm_$n.mp4" --size 180x320 --crf 0 --seed "$s" --loop 2
  eq "$CODE" 0 "stopmotion $n exit 0"
done
mapfile -t A < <(frame_md5s sm_a.mp4)
eq "${#A[@]}" 16 "4 images x 2 loops x held on twos"
eq "${A[0]}" "${A[1]}" "held on twos"
[ "${A[1]}" != "${A[2]}" ] && ok || bad "next drawing differs"
[ "${A[0]}" != "${A[8]}" ] && ok || bad "second loop wobbles differently"
eq "$(frame_md5s sm_b.mp4 | md5sum)" "$(printf '%s\n' "${A[@]}" | md5sum)" "same seed, same frames"
[ "$(frame_md5s sm_c.mp4 | md5sum)" != "$(printf '%s\n' "${A[@]}" | md5sum)" ] && ok || bad "another seed, other frames"
eq "$($T "${PR[@]}" sm_a.mp4 --json | jq '.video.fps')" 24.0 "24 fps out"
run "${SM[@]}" frames -o "$TMP/sm_j.mp4" --size 180x320 --pingpong --end-hold 0.5 --json
eq "$(echo "$OUT" | jq -c '.')" "{\"output\":\"$TMP/sm_j.mp4\",\"drawing_fps\":12.0,\"hold\":2,\"output_fps\":24.0,\"seed\":1,\"mode\":\"frames\",\"images\":4,\"drawings\":6,\"frames\":12,\"size\":\"180x320\",\"duration_s\":1.0}" "ping-pong summary: 0 1 2 3 2 1"
run "${SM[@]}" frames -o "$TMP/sm_t.mp4" --size 180x320
eq "$OUT" $'stopmotion: frames, 12 drawings/s held 2 -> 24 fps, 180x320\n  wrote: '"$TMP/sm_t.mp4" "text summary"

# ---- the wobble comes from CPython's Mersenne Twister: random.Random(1) gives 0.13436424411240122, 0.8474337369372327,
# 0.763774618976614, 0.2550690257394217; random.Random(42) starts 0.6394267984578837, 0.025010755222666936
cat > logff.sh <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$TMP/ff.args"
exec "$FF" "\$@"
EOF
chmod +x logff.sh
EDIT_FFMPEG="$TMP/logff.sh" $T "${SM[@]}" frames -o "$TMP/sm_s.mp4" --size 180x320 --flicker 0.01 >/dev/null 2>&1
L=$(grep -- '-framerate' ff.args)
has "$L" "rotate=a='0.008727*(2*(sin(floor(n/2)*12.9898+13.4364)*43758.5453-floor(sin(floor(n/2)*12.9898+13.4364)*43758.5453))-1)':c=black:ow=iw:oh=ih" "seed 1: rotation hash offset 13.4364"
has "$L" "*78.2330+84.7434)" "seed 1: x offset 84.7434"
has "$L" "*39.3468+76.3775)" "seed 1: y offset 76.3775"
has "$L" "eq=brightness='0.0100*(2*(sin(floor(n/2)*93.9898+25.5069)" "seed 1: flicker offset 25.5069"
has "$L" "crop=w=iw-12:h=ih-12:x='6+0.900*(2*" "margin: ceil(0.9 + 160*sin(0.5 deg) + 2) = ceil(4.296) = 5, made even = 6"
: > ff.args
EDIT_FFMPEG="$TMP/logff.sh" $T "${SM[@]}" frames -o "$TMP/sm_s.mp4" --size 180x320 --seed 42 >/dev/null 2>&1
has "$(grep -- '-framerate' ff.args)" "*12.9898+63.9427)" "seed 42: rotation offset 63.9427"
has "$(grep -- '-framerate' ff.args)" "*78.2330+2.5011)" "seed 42: x offset 2.5011"

# ---- upstream test_stopmotion_from_video_and_bad_folder
run "${SM[@]}" --from-video talk.mp4 -o "$TMP/live.mp4" --crf 0 --fps 8 --hold 3
eq "$CODE" 0 "from-video exit 0"
eq "$($T "${PR[@]}" live.mp4 --json | jq -c '[.video.fps, (.audio != null)]')" '[24.0,true]' "24 fps with audio"
mapfile -t L < <(frame_md5s live.mp4)
eq "${L[0]}" "${L[2]}" "held three frames"
[ "${L[2]}" != "${L[3]}" ] && ok || bad "then the next drawing"
mkdir empty
run "${SM[@]}" empty -o "$TMP/live.mp4"
eq "$CODE" 2 "empty folder: exit 2"; eq "$ERR" "error: no images in empty (png, jpg, webp, bmp, tif)" "empty folder message"
run "${SM[@]}" nope -o x.mp4
eq "$CODE" 2 "not a folder: exit 2"; eq "$ERR" "error: not a folder: nope" "not a folder message"
run "${SM[@]}" frames --from-video talk.mp4 -o x.mp4
eq "$CODE" 2 "both inputs: exit 2"; has "$ERR" "stopmotion: error: give a frames folder or --from-video CLIP (one of them)" "both inputs message"
run "${SM[@]}" frames -o x.mp4 --fps 31
eq "$CODE" 2 "--fps 31: exit 2"; has "$ERR" "stopmotion: error: --fps must be 1-30 and --hold 1-8" "range message"
run "${SM[@]}" frames -o x.mp4 --fps 30 --hold 3
eq "$CODE" 2 "90 fps: exit 2"; has "$ERR" "stopmotion: error: fps x hold must be <= 60" "fps x hold message"
run "${SM[@]}" frames
eq "$CODE" 2 "no -o: exit 2"; has "$ERR" "stopmotion: error: the following arguments are required: -o/--output" "required -o"
run "${SM[@]}" --from-video talk.wav -o x.mp4
eq "$CODE" 2 "audio clip: exit 2"; eq "$ERR" "error: that file has no video" "no video message"

done_ "master-edit"

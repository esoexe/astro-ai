#!/usr/bin/env bash
# console-sound/vag: the RoundTrip, Decoder, Header, Inputs, Stereo, Like, Scan, Split, Helpers and Cli cases of
# console-skill/tests/test_sound.py through the CLI. Audio is synthesized by fixtures/mkvag.js, which also reads the
# WAVs back with its own RIFF code, computes SNR independently and holds a straight-line reference decoder.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkvag.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
V=(console-sound/vag)
j()  { echo "$OUT" | jq -c "$1"; }
mk() { node "$MK" "$@"; }
gt() { node -e 'const [a,b]=process.argv.slice(1);process.exit((a==="inf"?Infinity:+a)>+b?0:1)' "$1" "$2" && ok || bad "$3: $1 not > $2"; }
byte() { node -e 'const b=require("fs").readFileSync(process.argv[1]);console.log(b[+process.argv[2]])' "$1" "$2"; }
hexr() { node -e 'const b=require("fs").readFileSync(process.argv[1]);console.log(b.subarray(+process.argv[2],+process.argv[3]).toString("hex"))' "$1" "$2" "$3"; }
be32() { node -e 'const b=require("fs").readFileSync(process.argv[1]);console.log(b.readUInt32BE(+process.argv[2]))' "$1" "$2"; }
size() { node -e 'console.log(require("fs").statSync(process.argv[1]).size)' "$1"; }
flags() { node -e 'const b=require("fs").readFileSync(process.argv[1]).subarray(48);const f=[];for(let i=0;i<b.length;i+=16)f.push(b[i+1]);console.log(f.join(","))' "$1"; }
SINE='{"sine":[11025]}'

# ---- RoundTrip.test_sine_round_trip_snr_above_25_db
mk wav s.wav "{\"chans\":[$SINE]}"
run "${V[@]}" fromwav s.wav --out v.vag; eq "$CODE" 0 "fromwav rc 0"
run "${V[@]}" towav v.vag --out back.wav; eq "$CODE" 0 "towav rc 0"
S=$(mk samples back.wav 0 0 28)
eq "$(echo "$S" | jq -c .rate)" 22050 "rate kept"
eq "$(echo "$S" | jq -c .head)" "[$(printf '0,%.0s' {1..27})0]" "silent lead frame"
gt "$(mk snr "$SINE" back.wav 28)" 25 "sine SNR"

# ---- test_chord_round_trip
CH='{"sum":[{"sine":[8000,220,9000]},{"sine":[8000,1375,6000]}]}'
mk wav c.wav "{\"chans\":[$CH]}"
run "${V[@]}" fromwav c.wav --out c.vag --no-lead-frame; eq "$CODE" 0 "chord rc"
run "${V[@]}" towav c.vag --out cb.wav
gt "$(mk snr "$CH" cb.wav 0)" 25 "chord SNR"

# ---- test_fromwav_reports_quality (and the figure agrees with an independent decode)
mk wav q.wav '{"chans":[{"sine":[4000]}]}'
run "${V[@]}" fromwav q.wav --out q.vag; eq "$CODE" 0 "quality rc"
DB=$(echo "$OUT" | sed -n 's/^quality: \(.*\) dB.*/\1/p'); gt "$DB" 25 "reported quality"
run "${V[@]}" towav q.vag --out qb.wav
eq "$DB" "$(node -e 'console.log((+process.argv[1]).toFixed(1))' "$(mk snr '{"sine":[4000]}' qb.wav 28)")" "quality = independent SNR"

# ---- test_exhaustive_is_at_least_as_good
EX='{"sum":[{"sine":[4200,300,15000]},{"sine":[4200,4100,9000]}]}'
mk wav ex.wav "{\"chans\":[$EX]}"
run "${V[@]}" fromwav ex.wav --out fast.vag --no-lead-frame; run "${V[@]}" towav fast.vag --out fast.wav
run "${V[@]}" fromwav ex.wav --out full.vag --no-lead-frame --exhaustive; eq "$CODE" 0 "exhaustive rc"
run "${V[@]}" towav full.vag --out full.wav
F1=$(mk snr "$EX" fast.wav 0); F2=$(mk snr "$EX" full.wav 0)
gt "$F2" "$(node -e 'console.log(+process.argv[1]-0.5)' "$F1")" "exhaustive >= fast - 0.5"

# ---- test_full_scale_square_clamps_cleanly
mk wav sq.wav '{"chans":[{"square":2800}]}'
run "${V[@]}" fromwav sq.wav --out sq.vag --no-lead-frame
run "${V[@]}" info sq.vag --json; eq "$(j .bad_frames)" 0 "square bad frames"; eq "$(j .samples)" 2800 "square samples"
run "${V[@]}" towav sq.vag --out sqb.wav; eq "$(mk samples sqb.wav | jq .n)" 2800 "2800 decoded"
gt "$(mk snr '{"square":2800}' sqb.wav 0)" 10 "square SNR"

# ---- test_silence_encodes_to_zero_frames
mk wav z.wav '{"chans":[{"zeros":56}]}'
run "${V[@]}" fromwav z.wav --out z.vag --no-lead-frame
eq "$(byte z.vag 48)" 0 "silent header byte"; eq "$(hexr z.vag 50 64)" "0000000000000000000000000000" "silent nibbles"
run "${V[@]}" fromwav z.wav --out z2.vag; has "$OUT" "quality: inf dB" "silence is exact"

# ---- test_encode_block_stays_in_range (four hard frames: every header byte has shift <= 12, filter <= 4)
for spec in '{"sine":[28,9000,30000]}' "{\"list\":[$(printf '32767,%.0s' {1..27})32767]}" "{\"list\":[$(printf -- '-32768,32767,%.0s' {1..13})-32768,32767]}" "{\"list\":[$(seq -s, -14 13)]}"; do
  mk wav eb.wav "{\"chans\":[$spec]}"; run "${V[@]}" fromwav eb.wav --out eb.vag --no-lead-frame
  H=$(byte eb.vag 48); [ $((H & 15)) -le 12 ] && [ $((H >> 4)) -le 4 ] && ok || bad "header byte $H out of range for $spec"
  run "${V[@]}" info eb.vag --json; eq "$(j .bad_frames)" 0 "no odd frames for $spec"
done

# ---- Decoder.test_matches_the_runtime_reference
FR=(1c00$(printf '11%.0s' {1..14}) 2a00$(printf '9f71%.0s' {1..7}) 4c00$(printf 'f808%.0s' {1..7}) 3000$(printf '77%.0s' {1..14}))
mk bytes ref.bin "${FR[@]}"
run "${V[@]}" towav ref.bin --rate 8000 --out ref.wav; eq "$CODE" 0 "raw decode rc"
WANT=$(mk refdecode "${FR[@]}")
eq "$(mk samples ref.wav 0 0 112 | jq -c .head)" "$WANT" "decoder = runtime reference (112 samples)"

# ---- test_c_division_truncates_toward_zero
mk bytes cdiv.bin 1c00$(printf 'ff%.0s' {1..14})
run "${V[@]}" towav cdiv.bin --rate 8000 --out cdiv.wav
eq "$(mk samples cdiv.wav 0 0 4 | jq -c .head)" "[-1,-1,-1,-1]" "C division"

# ---- test_out_of_range_shift_and_filter_follow_the_runtime
mk bytes odd.bin 5d00$(printf '11%.0s' {1..14})
run "${V[@]}" towav odd.bin --rate 8000 --out odd.wav; eq "$CODE" 1 "odd frame rc 1"
eq "$(mk samples odd.wav 0 0 2 | jq -c .head)" "[8,8]" "shift 13 -> 9, filter 5 -> 0"
has "$OUT" "WARNING: 1 frames had an out-of-range filter or shift" "one bad frame"

# ---- test_end_flag_and_marker
ONE=0c00$(printf '11%.0s' {1..14}); END=0c01$(printf '11%.0s' {1..14}); MRK=0007$(printf '77%.0s' {1..14})
mk bytes e1.bin $ONE $END $ONE; run "${V[@]}" towav e1.bin --rate 8000 --out e1.wav; eq "$(mk samples e1.wav | jq .n)" 56 "end flag stops (56)"
run "${V[@]}" info e1.bin --rate 8000 --json; eq "$(j '[.frames,.end_flag_frame,.end_marker_frame]')" "[2,1,null]" "info end flag"
mk bytes e2.bin $ONE $MRK $ONE; run "${V[@]}" towav e2.bin --rate 8000 --out e2.wav; eq "$(mk samples e2.wav | jq .n)" 28 "marker stops (28)"
run "${V[@]}" info e2.bin --rate 8000 --json; eq "$(j '[.frames,.end_flag_frame,.end_marker_frame]')" "[1,null,1]" "info marker"
run "${V[@]}" towav e1.bin --rate 8000 --out e3.wav --all; eq "$(mk samples e3.wav | jq .n)" 84 "--all decodes everything"

# ---- Header.test_vagp_header_fields
mk wav JUMP_SFX.vag.wav '{"chans":[{"sine":[1000]}]}'
run "${V[@]}" fromwav JUMP_SFX.vag.wav --out JUMP_SFX.vag; eq "$CODE" 0 "JUMP_SFX rc"
eq "$(hexr JUMP_SFX.vag 0 4)" "56414770" "VAGp magic"
eq "$(be32 JUMP_SFX.vag 4)" 32 "version 0x20"
eq "$(be32 JUMP_SFX.vag 12)" "$(( $(size JUMP_SFX.vag) - 48 ))" "data size field"
eq "$(be32 JUMP_SFX.vag 16)" 22050 "rate field"
eq "$(hexr JUMP_SFX.vag 32 48)" "4a554d505f5346580000000000000000" "name JUMP_SFX"
eq "$(( $(size JUMP_SFX.vag) % 16 ))" 0 "16-byte multiple"
eq "$(be32 JUMP_SFX.vag 8)" 0 "reserved 0"

# ---- test_one_shot_flags
mk wav v1.wav '{"chans":[{"sine":[1000]}]}'
run "${V[@]}" fromwav v1.wav --out v1.vag
FL=$(flags v1.vag); NF=$(echo "$FL" | tr ',' '\n' | wc -l)
eq "$(hexr v1.vag 48 64)" "00000000000000000000000000000000" "silent lead frame"
eq "$(echo "$FL" | tr ',' '\n' | sed -n "2,$((NF - 2))p" | sort -u)" 0 "middle flags 0"
eq "$(echo "$FL" | tr ',' '\n' | sed -n "$((NF - 1))p")" 1 "end flag"
eq "$(hexr v1.vag $(( $(size v1.vag) - 16 )) $(size v1.vag))" "00070000000000000000000000000000" "silent marker frame"
eq "$NF" 38 "1000 samples -> lead + 36 + marker"

# ---- test_loop_flags_and_info
run "${V[@]}" fromwav v1.wav --out lp.vag --loop
FL=$(flags lp.vag); NF=$(echo "$FL" | tr ',' '\n' | wc -l)
eq "$(echo "$FL" | cut -d, -f1-2)" "0,6" "lead 0, loop start 6"
eq "$(echo "$FL" | tr ',' '\n' | sed -n "3,$((NF - 1))p" | sort -u)" 2 "repeat flags"
eq "$(echo "$FL" | tr ',' '\n' | tail -n1)" 3 "last 3"
run "${V[@]}" info lp.vag --json; eq "$CODE" 0 "info rc"
eq "$(j .loops)" true "loops"; eq "$(j .loop_start_sample)" 28 "loop start 28"; eq "$(j .rate)" 22050 "rate"
eq "$(j keys_unsorted)" '["file","offset","header","rate","data_bytes","frames","samples","seconds","lead_zero_frame","loop_start_sample","loops","end_flag_frame","end_marker_frame","bad_frames","note"]' "info keys"
eq "$(j '.header | keys_unsorted')" '["magic","version","reserved","data_size","rate","name"]' "header keys"
eq "$(j '[.header.name,.header.magic,.lead_zero_frame,.note,.end_marker_frame]')" '["lp","VAGp",true,null,null]' "header values"

# ---- test_loop_points_survive_wav_and_back
mk wav x.wav '{"chans":[{"sine":[2800]}]}'
run "${V[@]}" fromwav x.wav --out x.vag --loop-start 1000
has "$OUT" "added 8 samples of silence" "pad note"; has "$OUT" "loops from sample 1036" "loops from 1036"
run "${V[@]}" info x.vag --json; eq "$(j .loop_start_sample)" 1036 "1000 -> 1008 + lead"
run "${V[@]}" towav x.vag --out l.wav; eq "$CODE" 0 "loop towav rc"; has "$OUT" "smpl" "smpl chunk"
eq "$(mk samples l.wav | jq -c .smpl)" "[1036,$(( $(mk samples l.wav | jq .n) - 1 ))]" "smpl loop points (independent reader)"
run "${V[@]}" fromwav l.wav --out l2.vag --no-lead-frame; eq "$CODE" 0 "back rc"; has "$OUT" "smpl" "smpl picked up"
run "${V[@]}" info l2.vag --json; eq "$(j .loop_start_sample)" 1036 "loop survives"
run "${V[@]}" towav x.vag --out nl.wav --no-loop-chunk; hasnt "$OUT" "smpl" "--no-loop-chunk"; eq "$(mk samples nl.wav | jq -c .smpl)" null "no smpl chunk"

# ---- test_no_loop_overrides_smpl
run "${V[@]}" towav lp.vag --out lw.wav
run "${V[@]}" fromwav lw.wav --out n.vag --no-loop
run "${V[@]}" info n.vag --json; eq "$(j .loops)" false "--no-loop"

# ---- test_info_text
mk wav t5.wav '{"chans":[{"sine":[500]}]}'
run "${V[@]}" fromwav t5.wav --out t5.vag
run "${V[@]}" info t5.vag; eq "$CODE" 0 "info text rc"
has "$OUT" "one-shot" "one-shot"; has "$OUT" "22050 Hz" "22050 Hz"
eq "$OUT" "t5.vag: VAGp header, version 0x20, name 't5'
  data size   320 bytes (header field 320)
  sample rate 22050 Hz
  frames      19 (532 samples, 0.024 s, up to the end flag)
  loop        none (one-shot)
  lead frame  silent first frame (Sony convention)" "info text exact"

# ---- test_header_size_quirk_is_accepted
cp t5.vag hq.vag; node -e 'const fs=require("fs");const b=fs.readFileSync("hq.vag");b.writeUInt32BE(b.length,12);fs.writeFileSync("hq.vag",b)'
run "${V[@]}" towav hq.vag --out hq.wav; eq "$CODE" 0 "quirk rc"; has "$OUT" "counts the header" "quirk note"
run "${V[@]}" info hq.vag --json; eq "$(j .note)" '"the header'"'"'s data size counts the header too; using the 320 bytes present"' "quirk note text"

# ---- test_truncated_vag_is_refused
mk wav t50.wav '{"chans":[{"sine":[5000]}]}'; run "${V[@]}" fromwav t50.wav --out tr.vag
node -e 'const fs=require("fs");const b=fs.readFileSync("tr.vag");fs.writeFileSync("tr.vag",b.subarray(0,b.length>>1))'
run "${V[@]}" towav tr.vag; eq "$CODE" 2 "truncated rc 2"; has "$ERR" "truncated" "truncated"; hasnt "$ERR" "Traceback" "no traceback"
has "$ERR" "error: truncated VAG: the header says" "error prefix"

# ---- test_zero_rate_defaults_like_the_runtime
cp t5.vag zr.vag; node -e 'const fs=require("fs");const b=fs.readFileSync("zr.vag");b.writeUInt32BE(0,16);fs.writeFileSync("zr.vag",b)'
run "${V[@]}" towav zr.vag --out zr.wav; eq "$CODE" 0 "zero rate rc"; eq "$(mk samples zr.wav | jq .rate)" 44100 "44100 default"
has "$OUT" "header rate is 0, assuming 44100 Hz" "zero-rate note"

# ---- Inputs.test_raw_adpcm_needs_a_rate
run "${V[@]}" fromwav v1.wav --out raw.vag; mk strip raw.vag bank.bd
run "${V[@]}" towav bank.bd; eq "$CODE" 2 "raw needs rate"; has "$ERR" "--rate" "--rate hint"
run "${V[@]}" towav bank.bd --rate 11025 --out r.wav; eq "$CODE" 0 "raw rc 0"; eq "$(mk samples r.wav | jq .rate)" 11025 "raw rate"
run "${V[@]}" info bank.bd; eq "$CODE" 2 "info raw rc 2"
run "${V[@]}" info bank.bd --rate 11025; eq "$CODE" 0 "info raw with rate"
has "$OUT" "bank.bd: raw ADPCM from offset 0x0 (no header)" "raw text"
run "${V[@]}" info bank.bd --rate 0x2b11 --json; eq "$(j .rate)" 11025 "hex rate"
run "${V[@]}" info bank.bd --rate banana; eq "$CODE" 2 "bad int rc 2"; has "$ERR" "argument --rate: not a number: 'banana'" "bad int message"

# ---- test_offset_outside_file
run "${V[@]}" towav t5.vag --offset 0x100000; eq "$CODE" 2 "offset outside"; has "$ERR" "offset 0x100000 is outside the file (368 bytes)" "offset message"
run "${V[@]}" info t5.vag --offset=-5; eq "$CODE" 2 "negative offset"; has "$ERR" "offset 0x-5 is outside" "negative offset hex"

# ---- test_default_output_goes_under_console_audio
mk wav theme.vag.wav '{"chans":[{"sine":[300]}]}'; run "${V[@]}" fromwav theme.vag.wav --out theme.vag
run "${V[@]}" towav theme.vag; eq "$CODE" 0 "default out rc"; [ -f console/audio/theme.wav ] && ok || bad "console/audio/theme.wav"
run "${V[@]}" fromwav theme.vag.wav; [ -f console/audio/theme.vag.vag ] && ok || bad "fromwav default console/audio/theme.vag.vag"

# ---- test_not_a_wav / 8-bit / empty / 3 channels / missing
printf 'hello there, not audio' > nw.wav
run "${V[@]}" fromwav nw.wav; eq "$CODE" 2 "not a wav"; has "$ERR" "RIFF" "RIFF"
mk wav 8.wav '{"width":1,"chans":[{"sine":[100]}]}'
run "${V[@]}" fromwav 8.wav; eq "$CODE" 2 "8-bit"; has "$ERR" "16-bit" "16-bit"; has "$ERR" "(format 1, 8-bit)" "format text"
mk wav empty.wav '{"chans":[{"list":[]}]}'
run "${V[@]}" fromwav empty.wav; eq "$CODE" 2 "empty"; has "$ERR" "no audio" "no audio"
mk wav 3.wav '{"chans":[{"sine":[100]},{"sine":[100]},{"sine":[100]}]}'
run "${V[@]}" fromwav 3.wav; eq "$CODE" 2 "3 channels"; has "$ERR" "has 3 channels: only mono or stereo" "3ch message"
run "${V[@]}" info "$TMP/nope.vag"; eq "$CODE" 2 "missing vag"; has "$ERR" "cannot read $TMP/nope.vag: No such file or directory" "missing message"
run "${V[@]}" fromwav "$TMP/nope.wav"; eq "$CODE" 2 "missing wav"
run "${V[@]}" info "$TMP"; eq "$CODE" 2 "folder"; has "$ERR" "is a folder, not a file" "folder message"

# ---- test_bad_frames_warn
mk bytes noise.bin $(printf 'f700%s' "$(printf '55%.0s' {1..14})")$(printf 'f700%s' "$(printf '55%.0s' {1..14})")$(printf 'f700%s' "$(printf '55%.0s' {1..14})")$(printf 'f700%s' "$(printf '55%.0s' {1..14})")
run "${V[@]}" towav noise.bin --rate 8000 --out n.wav; eq "$CODE" 1 "bad frames rc 1"; has "$OUT" "WARNING" "WARNING"
run "${V[@]}" info noise.bin --rate 8000; eq "$CODE" 1 "info bad rc 1"; has "$OUT" "WARNING: 4 frames have an out-of-range" "info warning"

# ---- Stereo
L='{"sine":[3000,440]}'; R='{"sine":[3000,660,9000]}'
mk wav st.wav "{\"chans\":[$L,$R]}"
run "${V[@]}" fromwav st.wav --out st.vag; eq "$CODE" 0 "split rc"; has "$OUT" "stereo written as two mono files" "split note"
for side in L R; do
  run "${V[@]}" towav "st_$side.vag" --out "$side.wav"
  [ "$side" = L ] && ref=$L || ref=$R
  gt "$(mk snr "$ref" "$side.wav" 28)" 25 "$side SNR"
done
run "${V[@]}" fromwav st.wav --out i.vag --stereo interleave --interleave 256; eq "$CODE" 0 "interleave rc"
eq "$(( ($(size i.vag) - 48) % 512 ))" 0 "interleaved blocks"
run "${V[@]}" towav i.vag --channels 2 --interleave 256 --out i.wav; eq "$CODE" 0 "stereo towav rc"
eq "$(mk samples i.wav | jq .channels)" 2 "two channels"
gt "$(mk snr "$L" i.wav 28 0)" 25 "interleaved L SNR"; gt "$(mk snr "$R" i.wav 28 1)" 25 "interleaved R SNR"
has "$OUT" "stereo" "stereo text"
run "${V[@]}" fromwav st.wav --out i2.vag --interleave 256; has "$OUT" "stereo interleaved every 256 bytes" "--interleave implies interleave"
run "${V[@]}" towav i2.vag --channels 2; eq "$CODE" 2 "needs interleave"; has "$ERR" "stereo needs --interleave N" "needs message"
run "${V[@]}" towav i2.vag --channels 2 --interleave 100; eq "$CODE" 2 "interleave multiple of 16"
run "${V[@]}" towav i2.vag --channels 3; eq "$CODE" 2 "channels 3"; has "$ERR" "--channels must be 1 or 2" "channels message"
run "${V[@]}" fromwav st.wav --out bad.vag --stereo interleave; eq "$CODE" 2 "interleave without size"
run "${V[@]}" fromwav st.wav --out m.vag --mono; eq "$CODE" 0 "mono rc"; [ -f m.vag ] && ok || bad "m.vag"; has "$OUT" "mixed stereo down" "mono note"
run "${V[@]}" fromwav st.wav --stereo both; eq "$CODE" 2 "bad choice"

# ---- Like
mk wav orig.vag.wav '{"rate":11025,"chans":[{"sine":[2000,440,12000,11025]}]}'
run "${V[@]}" fromwav orig.vag.wav --out orig.vag; eq "$CODE" 0 "orig rc"
mk wav mine.wav '{"rate":44100,"chans":[{"sine":[44100,440,12000,44100]}]}'
run "${V[@]}" fromwav mine.wav --like orig.vag --out new.vag; eq "$CODE" 1 "bigger refused rc 1"
has "$ERR" "SPU2 RAM" "SPU2 RAM"; [ ! -e new.vag ] && ok || bad "new.vag must not exist"
run "${V[@]}" fromwav mine.wav --like orig.vag --out new.vag --allow-bigger; eq "$CODE" 0 "--allow-bigger rc"
run "${V[@]}" info new.vag --json; eq "$(j .rate)" 11025 "like rate"
node -e 'process.exit(Math.abs(+process.argv[1]-1)<=0.02?0:1)' "$(j .seconds)" && ok || bad "about 1 s: $(j .seconds)"
run "${V[@]}" fromwav mine.wav --like orig.vag --out new2.vag --allow-bigger
has "$OUT" "resampled 44100 Hz to 11025 Hz" "resample note"; has "$OUT" "matched the original: rate 11025, magic VAGp, version 0x20, name 'orig'" "matched line"
mk wav lo.vag.wav '{"chans":[{"sine":[4000]}]}'; run "${V[@]}" fromwav lo.vag.wav --out loop.vag --loop
mk wav sh.wav '{"chans":[{"sine":[1000]}]}'
run "${V[@]}" fromwav sh.wav --like loop.vag --out ln.vag; eq "$CODE" 0 "fits rc"; has "$OUT" "loops because the original (--like) loops" "like loop note"
run "${V[@]}" info ln.vag --json; eq "$(j .loops)" true "copies looping"; eq "$(j .header.name)" '"loop"' "copies name"
run "${V[@]}" fromwav sh.wav --like sh.wav; eq "$CODE" 2 "like must be a VAG"; has "$ERR" "has no VAGp header" "like message"

# ---- Scan
mk wav a.vag.wav '{"chans":[{"sine":[700]}]}'; run "${V[@]}" fromwav a.vag.wav --out a.vag
mk wav b.vag.wav '{"rate":32000,"chans":[{"sine":[900,300,12000,32000]}]}'; run "${V[@]}" fromwav b.vag.wav --out b.vag
node -e 'const b=Buffer.alloc(48);b.write("VAGp",0);b.writeUInt32BE(7,16);require("fs").writeFileSync("fake.bin",b)'   # rate 7, size 0
mk cat pack.dat junk:256 a.vag zeros:4933 b.vag fake.bin junk:256
A=$(size a.vag); OFF2=$((256 + A + 4933))
run "${V[@]}" scan pack.dat --json; eq "$CODE" 0 "scan rc"
eq "$(j '[.[].offset]')" "[256,$OFF2]" "embedded offsets"
eq "$(j '[.[].rate]')" "[22050,32000]" "embedded rates"
eq "$(j '.[0] | keys_unsorted')" '["magic","version","reserved","data_size","rate","name","offset","valid","bytes","seconds"]' "hit keys"
eq "$(j '.[0] | [.name,.valid,.bytes]')" "[\"a\",true,$((A - 48))]" "hit values"
run "${V[@]}" scan pack.dat --all --json; eq "$(j length)" 3 "--all lists the fake"; eq "$(j '.[2] | [.valid,.seconds,.rate]')" "[false,0.0,7]" "fake hit"
run "${V[@]}" scan pack.dat --extract out; eq "$CODE" 0 "extract rc"
eq "$(ls out | wc -l)" 2 "two files"
cmp -s "out/$(ls out | sed -n 1p)" a.vag && ok || bad "first extract = a.vag"
cmp -s "out/$(ls out | sed -n 2p)" b.vag && ok || bad "second extract = b.vag"
eq "$(ls out | sed -n 1p)" "000_00000100_a.vag" "extract name"
has "$OUT" "2 VAG header(s), extracted to out" "extract summary"
run "${V[@]}" scan pack.dat
eq "$(echo "$OUT" | head -n1)" "offset       magic rate     bytes      seconds   name" "table header"
eq "$(echo "$OUT" | sed -n 2p)" "$(printf '0x%-10x %-5s %-8d %-10d %-9s %s' 256 VAGp 22050 $((A - 48)) "$(node -e 'console.log((Math.floor('"$((A - 48))"'/16)*28/22050).toFixed(3))')" a)" "table row"
run "${V[@]}" scan pack.dat --all; has "$OUT" "(implausible header, skipped)" "implausible text"
# across the 4 MiB read pieces: a header split over the boundary is still found
mk cat big.dat zeros:4194302 a.vag zeros:100
run "${V[@]}" scan big.dat --json; eq "$(j '[.[].offset]')" "[4194302]" "header across the chunk boundary"
mk cat big2.dat zeros:4194303 a.vag; run "${V[@]}" scan big2.dat --json; eq "$(j '[.[].offset]')" "[4194303]" "boundary at VAG|p"
mk cat big3.dat zeros:4194301 a.vag; run "${V[@]}" scan big3.dat --json; eq "$(j '[.[].offset]')" "[4194301]" "boundary at VAGp|"
head -c 100 /dev/zero > empty.dat
run "${V[@]}" scan empty.dat; eq "$CODE" 0 "nothing rc 0"; has "$OUT" "no VAGp" "nothing text"
run "${V[@]}" scan "$TMP"; eq "$CODE" 2 "folder refused"; has "$ERR" "is not a file" "folder message"

# ---- Split
mk wav one.wav '{"chans":[{"sine":[600]}]}'; run "${V[@]}" fromwav one.wav --out one.vag; mk strip one.vag one.raw
mk wav two.wav '{"chans":[{"sine":[900,200]}]}'; run "${V[@]}" fromwav two.wav --out two.vag --loop; mk strip two.vag two.raw
mk cat se.bd one.raw two.raw zeros:64
run "${V[@]}" split se.bd --rate 22050 --out se --vag --json; eq "$CODE" 0 "split rc"
eq "$(j length)" 2 "two sounds"; eq "$(j '[.[].loops]')" "[false,true]" "loops flags"
eq "$(j '.[1].offset')" "$(size one.raw)" "second offset"
eq "$(jq '.sounds | length' se/split.json)" 2 "split.json"
eq "$(jq -c '[.source,.rate]' se/split.json)" '["se.bd",22050]' "split.json header"
eq "$(tail -c1 se/split.json | od -An -c | tr -d ' ')" "}" "split.json has no trailing newline"
gt "$(mk snr '{"sine":[600]}' "$(echo "$OUT" | jq -r '.[0].wav')" 28)" 25 "split sound SNR"
eq "$(j '.[0] | keys_unsorted')" '["index","offset","bytes","samples","seconds","loops","wav","bad_frames","vag"]' "row keys"
run "${V[@]}" info se/se_001.vag; eq "$CODE" 0 "split vag info rc"; has "$OUT" "name 'se_001'" "split vag name"
run "${V[@]}" split se.bd --rate 22050 --out se2
has "$OUT" "2 sound(s) written to se2 (heuristic split at end flags" "split text"
has "$OUT" "  1  0x$(printf '%08x' "$(size one.raw)")" "split row"
run "${V[@]}" split t5.vag --rate 8000 --out o; eq "$CODE" 2 "headed file refused"; has "$ERR" "has a VAGp header" "headed message"
run "${V[@]}" split se.bd --out o; eq "$CODE" 2 "--rate required"

# ---- Helpers (through the CLI)
mk wav r100.wav '{"rate":100,"chans":[{"list":['"$(seq -s, 0 99)"']}]}'
run "${V[@]}" fromwav r100.wav --rate 50 --out r50.vag --no-lead-frame; has "$OUT" "48 bytes of ADPCM (3 frames, 1.000 s)" "resample 100 -> 50 samples (2 frames + marker)"
run "${V[@]}" info r50.vag --json; eq "$(j .rate)" 50 "resampled rate"
mk wav a50.wav "{\"chans\":[{\"list\":[$(printf '1000,%.0s' {1..49})1000]}]}"
run "${V[@]}" fromwav a50.wav --out a50.vag --loop-start 30; has "$OUT" "added 26 samples of silence" "align pad 26"
has "$OUT" "loops from sample 84" "56 + lead"
run "${V[@]}" fromwav a50.wav --out a28.vag --loop-start 28; hasnt "$OUT" "added" "aligned loop needs no pad"
run "${V[@]}" fromwav a50.wav --out a99.vag --loop-start 50; eq "$CODE" 2 "loop start outside"; has "$ERR" "loop start 50 is outside the sound (50 samples)" "outside message"
node -e 'const b=Buffer.alloc(64);b.write("RIFF",0);require("fs").writeFileSync("riff.bin",b)'
run "${V[@]}" info riff.bin; eq "$CODE" 2 "RIFF is not a VAG"; has "$ERR" "no VAGp header at offset 0x0" "no header message"
printf 'VAGp' > short.bin; run "${V[@]}" info short.bin; eq "$CODE" 2 "4-byte VAGp is not a header"
printf 'pGAV%060d' 0 > swapped.bin; run "${V[@]}" info swapped.bin --rate 8000; eq "$CODE" 2 "byte-swapped"; has "$ERR" "byte-swapped VAG header" "pGAV message"

# ---- Cli.test_help_everywhere
run "${V[@]}" --help; eq "$CODE" 0 "help rc"; has "$OUT" "usage" "usage"; has "$OUT" "Rip only from discs you own" "epilog"
for c in info towav fromwav scan split; do run "${V[@]}" "$c" --help; eq "$CODE" 0 "$c help"; has "$OUT" "usage: vag $c" "$c usage"; done
run "${V[@]}"; eq "$CODE" 2 "no command rc 2"; has "$ERR" "required: command" "command required"

done_ console-sound/vag

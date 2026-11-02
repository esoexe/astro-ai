#!/usr/bin/env bash
# console-graphics/gspacket: the Tables, Gif, Vif, Dma and Cli cases of console-skill/tests/test_gspacket.py through
# the CLI (gif/vif/dma --json stand in for decode_gif / decode_vif / walk_dma), plus exact text, register details,
# 64-bit values, indent=1 JSON, Python slicing of --offset/--length/--max-lines and argparse errors.
source "$(dirname "$0")/lib.sh"
MK="$FIX/mkgsp.js"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
for n in $(node "$MK" list); do node "$MK" "$n" "$n.bin"; done
GS=(console-graphics/gspacket)
j()  { echo "$OUT" | jq -c "$1"; }
gj() { run "${GS[@]}" gif "$@" --json; }
vj() { run "${GS[@]}" vif "$@" --json; }
dj() { run "${GS[@]}" dma "$@" --json; }
vneed() { node "$MK" vifop op.bin "$@"; vj op.bin; j '[.issues[] | select(.message | test("needs")) | .message | capture("needs (?<n>[0-9]+) bytes").n | tonumber][0]'; }
iss() { j "[.issues[] | [.severity, .message]]"; }

# ---- Tables
gj vifstream.bin >/dev/null
node "$MK" vifop m.bin 21 0; vj m.bin; eq "$(j '.vif[0].cmd')" '"MSCALF"' "0x15 is MSCALF"
node "$MK" vifop m.bin 23 0; vj m.bin; eq "$(j '.vif[0].cmd')" '"MSCNT"' "0x17 is MSCNT"
node "$MK" vifop m.bin 74 1; vj m.bin; eq "$(j '.vif[0].cmd')" '"MPG"' "0x4A is MPG"
node "$MK" vifop m.bin 80 1; vj m.bin; eq "$(j '.vif[0].cmd')" '"DIRECT"' "0x50 is DIRECT"
gj regs.bin; eq "$(j '.gif_tags[0].regs')" '["A+D"]' "0xE is A+D"; eq "$(j '.gif_tags[0].writes[7].reg')" '"TEXFLUSH"' "0x3F is TEXFLUSH"
gj nreg16.bin; eq "$(j '.gif_tags[0].regs | unique')" '["NOP"]' "0xF is NOP"
gj upload_sprite.bin; eq "$(j '.gif_tags[0].writes[0].reg')" '"BITBLTBUF"' "0x50 is BITBLTBUF"
# test_unpack_sizes (through the "needs N bytes" message of a lone code)
eq "$(vneed 108 2 257)" 32 "V4-32 x2"
eq "$(vneed 111 4 257)" 8 "V4-5 x4: 16 bits each"
eq "$(vneed 106 3 257)" 12 "V3-8 x3: 9 bytes, word padded"
eq "$(vneed 97 1 257)" 4 "S-16 x1: 2 bytes, word padded"
eq "$(vneed 108 8 1025)" 32 "CL=1 WL=4: 2 source vectors"
eq "$(vneed 108 0 0)" 4096 "NUM=0 is 256, CL=WL=0 count as 1"
eq "$(vneed 108 0)" 4096 "no STCYCL: 256 x 16"
eq "$(vneed 101 5 257)" 20 "V2-16 x5"
eq "$(vneed 74 0)" 2048 "MPG NUM=0 is 256 instructions"
eq "$(vneed 80 0)" 1048576 "DIRECT IMM=0 is 65536 qwords"
eq "$(vneed 32 0)" 4 "STMASK takes a word"; eq "$(vneed 49 0)" 16 "STCOL takes a qword"
# test_physical_addresses (--base in KSEG0/KSEG1 and the uncached mirrors all map to the same dump)
for b in 0x00400000 0x80400000 0xA0400000 0x20400000 0x30400000; do
    vj vifpad.bin --base "$b" --addr 0x00400020; eq "$(j '[.vif[0].cmd, .vif[0].addr]')" '["STCYCL",4194336]' "physical base $b"
done
vj vifpad.bin --base 0x00400000 --addr 0x80400020; eq "$(j '.vif[0].addr')" 4194336 "KSEG0 --addr"
# test_register_details
gj regs.bin
eq "$(j '[.gif_tags[0].writes[] | .detail]')" '["TBP0=0x1000 TBW=4 PSMT8 256x128 CBP=0x3000 CPSM=PSMCT32 CSM=0","sprite","host -> GS (upload)","FBP=0x005 FBW=10 PSMCT16 FBMSK=0xFF00FF00","ZBP=0x007 PSMZ24 ZMSK=1","src 1,2 dst 3,4","x 0..639 y 0..447",""]' "register details"
has "$OUT" '"value": 18446744073709551615' "64-bit value as a JSON int"
has "$OUT" $'{\n "gif_tags": [\n  {\n   "off": 0,' "indent=1 JSON"

# ---- Gif
gj upload_sprite.bin; eq "$CODE" 0 "clean upload exit 0"; eq "$(j .issues)" '[]' "clean upload and draw"
eq "$(j '.gif_tags | length')" 3 "3 tags"
eq "$(j .uploads)" '[{"dbp":8192,"psm":"PSMCT32","width":8,"height":8,"bytes":256}]' "uploads"
eq "$(j .prims)" '{"sprite":1}' "prims"; eq "$(j '[.gif_tags[1].flg, .gif_tags[1].bytes]')" '["IMAGE",256]' "IMAGE tag"
eq "$(j 'keys_unsorted')" '["gif_tags","prims","uploads","issues"]' "gif JSON keys"
eq "$(j '.gif_tags[0] | keys_unsorted')" '["off","addr","packet","nloop","eop","flg","nreg","regs","writes","bytes"]' "tag keys"
eq "$(j '.gif_tags[2].writes[0]')" '{"reg":"PRIM","value":6,"detail":"sprite"}' "write keys"
eq "$(j '[.gif_tags[] | [.off, .addr, .packet]]')" '[[0,0,null],[80,80,null],[352,352,null]]' "offsets and addresses"
gj short.bin; eq "$(iss)" '[["warning","image upload started at 0x00000000 is 128 bytes short of its TRXREG size: the texture will be incomplete"]]' "short upload flagged"
eq "$(j '.issues[0].addr')" 0 "short upload addr: the setup tag's address"; eq "$CODE" 0 "warnings exit 0"
gj overrun.bin; eq "$(iss)" '[["warning","IMAGE data overruns the upload set up at 0x00000000 by 64 bytes: the wrong TRXREG or pixel format"]]' "overrun flagged"
gj noset.bin; eq "$(iss)" '[["warning","IMAGE data with no upload set up before it (BITBLTBUF, TRXREG, then TRXDIR=0)"]]' "image without setup"
gj prim7.bin; eq "$(iss)" '[["error","PRIM type 7 is prohibited on the GS"]]' "prohibited prim"; eq "$CODE" 1 "error exit 1"
eq "$(j '.gif_tags[0].writes[0].detail')" '"PROHIBITED"' "PROHIBITED detail"
gj pre.bin; eq "$(j .prims)" '{"triangle":1}' "PRE bit sets PRIM"; eq "$(j '.gif_tags[0].writes')" '[{"reg":"PRIM","value":3,"detail":"triangle"}]' "PRE write"
gj reglist.bin; eq "$(j '[.gif_tags[0].bytes, .issues]')" '[32,[]]' "REGLIST payload padded"
gj nreg16.bin; eq "$(j '[.gif_tags[0].nreg, .gif_tags[0].bytes]')" '[16,256]' "NREG=0 means 16"
gj truncgif.bin; eq "$(j '.gif_tags[0].truncated')" true "truncated packet"; eq "$CODE" 1 "truncated is an error"
eq "$(iss)" '[["error","PACKED GIF packet needs 64 bytes but the buffer has 32 left"],["warning","32 bytes after the last whole GIF packet"]]' "truncated issues (EOP=1: no note)"
eq "$(j '.gif_tags[0] | keys_unsorted')" '["off","addr","packet","nloop","eop","flg","nreg","regs","writes","truncated"]' "truncated tag keys"
gj noeop.bin; eq "$(j '[.issues[].severity]')" '["note"]' "missing EOP is a note"
gj unkreg.bin; eq "$(iss)" '[["warning","write to an unknown GS register 0x7E"]]' "unknown register"; eq "$(j '.gif_tags[0].writes[0].reg')" '"reg 0x7E?"' "unknown name"

# ---- Vif
vj vifstream.bin; eq "$(j .issues)" '[]' "clean stream"
eq "$(j '[.vif[].cmd]')" '["STCYCL","UNPACK","NOP","MPG","MSCAL","NOP","NOP","NOP","NOP","DIRECT"]' "commands"
eq "$(j '.vif_counts.NOP')" 5 "NOP x5"; eq "$(j '.gif_tags | length')" 1 "one GIF tag inside DIRECT"
has "$(j '.vif[1].detail')" "V4-32 x2" "UNPACK detail"; has "$(j '.vif[4].detail')" "0x0080" "MSCAL detail"
eq "$(j 'keys_unsorted')" '["vif","vif_counts","gif_tags","issues"]' "vif JSON keys"
eq "$(j '.vif[0]')" '{"off":0,"addr":0,"packet":null,"code":16777473,"irq":0,"cmd":"STCYCL","detail":"CL=1 WL=1"}' "VIF entry keys"
eq "$(j '.vif[2] | keys_unsorted')" '["off","addr","packet","code","irq","cmd"]' "no detail key for NOP"
eq "$(j .vif_counts)" '{"STCYCL":1,"UNPACK":1,"NOP":5,"MPG":1,"MSCAL":1,"DIRECT":1}' "counts in first-seen order"
eq "$(j '.gif_tags[0] | [.off, .addr]')" '[0,80]' "DIRECT tag: off in its block, addr in the stream"
vj vifunk.bin; eq "$(j '.vif[-1].cmd')" '"UNKNOWN 0x08"' "unknown code stops"; eq "$CODE" 1 "unknown is an error"
eq "$(iss)" '[["error","VIF code 0x08000000 at stream offset 4 is not a command the VIF knows: the stream is corrupt, or decoding started in the wrong place"]]' "unknown message"
eq "$(j .vif_counts)" '{"NOP":1}' "unknown not counted"
vj viftrunc.bin; eq "$(j '.vif[-1].truncated')" true "truncated UNPACK"
eq "$(iss)" '[["error","UNPACK at stream offset 0 needs 64 bytes but only 16 are left: the stream ends in the middle of a command"]]' "ends in the middle"
vj vifmis.bin; eq "$(j '[.issues[] | select(.severity == "warning") | .message]')" '["DIRECT data at stream offset 4 is not 128-bit aligned: real hardware expects NOP padding before it, so the stream may be decoded from the wrong place"]' "misaligned DIRECT"
vj vifstcycl.bin; eq "$(j '[.issues, .vif[-1].cmd]')" '[[],"NOP"]' "UNPACK respects STCYCL"
vj vifstmask.bin; eq "$(j '[[.vif[].cmd], .issues]')" '[["STMASK","STROW","MARK"],[]]' "STMASK/STROW take their words"
vj vifbadfmt.bin; eq "$(iss)" '[["error","UNPACK with an invalid format (vn=0 vl=3) at stream offset 4"]]' "invalid UNPACK format"
eq "$(j '[.vif[-1].cmd, .vif_counts]')" '["UNPACK ?",{"NOP":1}]' "UNPACK ? not counted"
vj vifmpgmis.bin; eq "$(iss)" '[["warning","MPG data at stream offset 4 is not 64-bit aligned"]]' "MPG alignment"
vj vifstray.bin; eq "$(iss)" '[["warning","2 stray bytes at the end of the stream"]]' "stray bytes"; eq "$(j '.issues[0].addr')" null "stray addr null"
vj vifirq.bin; eq "$(j '[.vif[] | [.cmd, .irq, .detail]]')" '[["NOP",1,null],["OFFSET",0,"OFFSET=1023"],["UNPACK",0,"V4-32 x1 to VU1 0x123 +TOPS unsigned masked"]]' "irq, OFFSET, UNPACK flags"
run "${GS[@]}" vif vifirq.bin; has "$OUT" "  0x00000000 NOP        (interrupt)" "interrupt text"

# ---- Dma
dj chain.bin --base 0x00100000 --tag 0x00100000
eq "$(j '[.packets[].id]')" '["cnt","next","ref","call","ret","end"]' "walks every tag kind"
eq "$(j '.packets[2].data')" 1050624 "ref data address"
eq "$(j '[.issues[] | select(.severity != "note")]')" '[]' "no errors or warnings"
eq "$(j '[.vif[] | select(.cmd == "MARK") | .code % 65536]')" '[1,2,2,2,2,3,4,4,4,4,5,5,5,5,6]' "marks in order"
eq "$(j '[.vif[] | select(.cmd == "MARK")][6].addr')" 1050624 "first ref mark addr"
eq "$(j '[.vif[] | select(.cmd == "MARK")][6].packet')" 2 "first ref mark packet"
eq "$(j 'keys_unsorted')" '["packets","stream_bytes","vif","vif_counts","gif_tags","issues"]' "dma JSON keys"
eq "$(j '.packets[0]')" '{"index":0,"tag":1048576,"id":"cnt","qwc":1,"addr":0,"spr":0,"irq":0,"pce":0,"data":1048592,"vif":[0,117440513]}' "packet keys"
eq "$(j .stream_bytes)" 96 "stream bytes with TTE"
dj chain.bin --base 0x00100000 --tag 0x00100000 --no-tte; eq "$(j .stream_bytes)" 48 "without TTE the tag words are skipped"
dj loop.bin --tag 0; eq "$(iss)" '[["note","packet #0 (next) has QWC=0: it moves no data"],["error","the chain loops: tag 0x00000000 was already visited as packet #0"]]' "loop is an error"; eq "$CODE" 1 "loop exit 1"
dj offend.bin --tag 0; eq "$(j '[.issues[] | select(.severity == "error") | .message]')" '["tag at 0x00200000 is outside the dump (the chain points off the end, or --base is wrong)"]' "off the end"
dj call3.bin --tag 0; eq "$(j '[.issues[] | .message]')" '["packet #2 is a third nested call: the DMAC only keeps two return addresses"]' "third nested call"
dj tie.bin --tag 0 --tie; eq "$(j '[(.packets | length), [.issues[].message]]')" '[1,["packet #0 (next) has QWC=0: it moves no data","packet #0 has IRQ set and TIE is on: the transfer stops here"]]' "TIE stops at IRQ"
dj tie.bin --tag 0; eq "$(j '.packets | length')" 2 "no TIE: follows"
dj zeros64.bin --tag 4; eq "$(j '[.issues[] | .message]')" '["tag at 0x00000004 is not 16-byte aligned"]' "unaligned tag"
dj maxtags.bin --tag 0 --max-tags 10; eq "$(j '[.issues[] | select(.severity == "warning") | .message]')" '["stopped after 10 tags (--max-tags): the chain may not end"]' "--max-tags"
eq "$(j '[(.issues | length), .issues[-1].addr, .issues[0].message]')" '[11,160,"packet #0 (cnt) has QWC=0: it moves no data"]' "max-tags addr is the next tag, after 10 QWC=0 notes"
dj maxtags.bin --tag 0 --max-tags 0; eq "$(j '[(.packets | length), .issues[0].addr]')" '[0,0]' "--max-tags 0 (for/else)"
dj spr.bin --tag 0; eq "$(j '[.issues[] | [.severity, .message]][0]')" '["warning","packet #0 reads from scratchpad 0x00000040 (not in a RAM dump)"]' "scratchpad"
dj qwcout.bin --tag 0; eq "$(j '[.issues[] | .message]')" '["packet #0 (cnt) moves 8 qwords from 0x00000010, outside the dump"]' "payload outside"
dj dmagif.bin --tag 0 --channel gif; eq "$(j '[.stream_bytes, (.vif | length), .vif_counts, (.gif_tags | length)]')" '[240,0,{},3]' "gif channel JSON"

# ---- Cli
run "${GS[@]}" gif upload44.bin; eq "$CODE" 0 "gif command"
has "$OUT" "image uploads: 4x4 PSMCT32 to DBP 0x2000 (64 bytes)" "uploads line"; has "$OUT" "issues: none" "issues none"
eq "$OUT" "GIF packets from 0x00000000: 3 tags
  GIF PACKED nloop=4 nreg=1 eop=0 regs=A+D  @0x00000000
      BITBLTBUF  0x0001200000000000  SBP=0x0000 SBW=0 PSMCT32 -> DBP=0x2000 DBW=1 PSMCT32
      TRXPOS     0x0000000000000000  src 0,0 dst 0,0
      TRXREG     0x0000000400000004  4x4
      TRXDIR     0x0000000000000000  host -> GS (upload)
  GIF IMAGE nloop=4 nreg=16 eop=1 regs=PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM,PRIM  @0x00000050
  GIF PACKED nloop=1 nreg=4 eop=1 regs=PRIM,RGBAQ,XYZ2,XYZ2  @0x000000A0
      PRIM       0x0000000000000006  sprite

primitives set: sprite x1
image uploads: 4x4 PSMCT32 to DBP 0x2000 (64 bytes)

issues: none" "gif text, exactly"
run "${GS[@]}" gif upload44.bin --max-lines 3; has "$OUT" $'      TRXPOS     0x0000000000000000  src 0,0 dst 0,0\n  ... 0 more GIF tags (raise --max-lines)' "--max-lines counts lines, so 3 tags - 3 lines = 0 more (as upstream)"
run "${GS[@]}" gif upload44.bin --max-lines=-1; has "$OUT" $'GIF packets from 0x00000000: 3 tags\n  ... 3 more GIF tags (raise --max-lines)' "negative --max-lines"
gj prim7.bin; eq "$CODE" 1 "gif json error exit"; eq "$(j '.issues[0].severity')" '"error"' "severity error"
run "${GS[@]}" gif truncgif.bin
has "$OUT" $'issues (2):\n  error   PACKED GIF packet needs 64 bytes but the buffer has 32 left  [0x00000000]\n  warning 32 bytes after the last whole GIF packet' "issues text"
run "${GS[@]}" vif vifpad.bin --base 0x00400000 --addr 0x00400020 --verbose; eq "$CODE" 0 "vif command"
has "$OUT" "UNPACK" "UNPACK listed"; has "$OUT" "sprite" "sprite with --verbose"; has "$OUT" "0x00400020" "guest address"
has "$OUT" "VIF1 stream: 10 commands (NOP x5, STCYCL x1, UNPACK x1, MPG x1, MSCAL x1, DIRECT x1)" "counts sorted by count, then first seen"
has "$OUT" "  0x00400024 UNPACK     V4-32 x2 to VU1 0x010" "UNPACK line"
has "$OUT" $'GIF tags inside DIRECT blocks: 1\n  GIF PACKED nloop=1 nreg=4 eop=1 regs=PRIM,RGBAQ,XYZ2,XYZ2  @0x00400070' "verbose GIF tags"
run "${GS[@]}" vif vifpad.bin --offset 32; has "$OUT" $'GIF tags inside DIRECT blocks: 1\nprimitives set: sprite x1' "not verbose: primitives only"
run "${GS[@]}" vif vifpad.bin --offset 32 --no-gif; hasnt "$OUT" "GIF tags inside" "--no-gif"
run "${GS[@]}" vif vifpad.bin --offset 32 --max-lines 2; has "$OUT" "  ... 8 more VIF commands" "VIF --max-lines"
vj vifpad.bin --offset=-160; eq "$(j '[.vif[0].cmd, .vif[0].off, .vif[0].addr]')" '["STCYCL",0,-160]' "negative --offset slices from the end (Python)"
vj vifpad.bin --offset 32 --length 8; eq "$(j '[[.vif[].cmd], (.issues | length)]')" '[["STCYCL","UNPACK"],1]' "--length"
vj vifpad.bin --offset 32 --length=-4; eq "$(j '.vif | length')" 0 "negative --length: data[32:28] is empty"
run "${GS[@]}" dma chain.bin --base 0x00100000 --tag 0x00100000; eq "$CODE" 0 "dma command"
has "$OUT" "6 packets" "6 packets"; has "$OUT" "call" "call listed"
has "$OUT" "DMA chain from 0x00100000 (VIF1 channel, TTE on): 6 packets, 96 bytes of data" "dma title"
has "$OUT" "  #3    0x00100410 call qwc=0     data@0x00100420  tag VIF: NOP, NOP" "packet line"
run "${GS[@]}" dma chain.bin --base 0x00100000 --tag 0x00100000 --max-lines 2; has "$OUT" "  ... 4 more packets" "packets --max-lines"
run "${GS[@]}" dma tie.bin --tag 0; has "$OUT" "  #0    0x00000000 next qwc=0     data@0x00000010  irq  tag VIF: NOP, NOP" "irq in packet line"
dj chain.bin --base 0x00100000 --tag 0x00100000; eq "$(j '.packets | length')" 6 "dma json"
run "${GS[@]}" dma dmagif.bin --tag 0 --channel gif; eq "$CODE" 0 "gif channel"; has "$OUT" "TTE off" "TTE off"; has "$OUT" "sprite" "gif channel sprite"
has "$OUT" $'\nGIF packets: 3\n' "gif channel count"
has "$OUT" "  #0    0x00000000 end  qwc=15    data@0x00000010" "no tag VIF with TTE off"
run "${GS[@]}" gif no-such-dump.bin; eq "$CODE" 2 "missing dump"; eq "$ERR" "gspacket: no-such-dump.bin does not exist" "does not exist"
: > empty.bin; run "${GS[@]}" gif empty.bin; eq "$ERR" "gspacket: empty.bin is empty" "empty dump"
run "${GS[@]}" vif zeros64.bin --addr 0x00900000; eq "$CODE" 2 "addr outside"; hasnt "$ERR" "Traceback" "clean"
eq "$ERR" "gspacket: 0x00900000 is not inside the dump (base 0x00000000, 64 bytes)" "addr outside message"
run "${GS[@]}" vif zeros64.bin --offset 64; eq "$ERR" "gspacket: offset 64 is past the end of the dump (64 bytes)" "offset past the end"
run "${GS[@]}" gif zeros64.bin --base zz; eq "$CODE" 2 "bad --base"; has "$ERR" "argument --base: not a number: zz (use 0x... for hex)" "parse_int message"
run "${GS[@]}" gif zeros64.bin --base 010; has "$ERR" "not a number: 010" "int(x, 0) refuses 010"
gj zeros64.bin --base 0o20 --addr 0b10000; eq "$CODE" 0 "zero tags: NLOOP 0, EOP=0 is only a note"; eq "$(j '[(.gif_tags | length), .gif_tags[0].addr, [.issues[].severity]]')" '[4,16,["note"]]' "0o/0b prefixes"
run "${GS[@]}" dma chain.bin --tag 0 --channel x; has "$ERR" "argument --channel: invalid choice: 'x' (choose from 'vif1', 'gif')" "channel choices"
run "${GS[@]}" dma chain.bin; has "$ERR" "the following arguments are required: --tag" "--tag required"
run "${GS[@]}"; eq "$CODE" 2 "no command"; has "$OUT" "usage:" "no command prints help"
run "${GS[@]}" --help; eq "$CODE" 0 "--help"; has "$OUT" $'examples:\n  gspacket.py dma ram.bin --tag 0x01F2A000                  # VIF1 chain, TTE on' "raw epilog"
has "$OUT" "exit codes: 0 no errors, 1 errors found, 2 bad input" "epilog exit codes"
for c in dma vif gif; do run "${GS[@]}" "$c" --help; eq "$CODE" 0 "$c --help"; done
done_ "console-graphics/gspacket"

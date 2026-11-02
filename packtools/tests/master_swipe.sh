#!/usr/bin/env bash
# master-virality/swipe: the case of master-skills/skills/virality/tools/test_virality.py (test_cli_pipeline) plus hand-built
# collections, run against the C++ tool. Medians and multiples are computed by hand in the comments; formula names are
# cross-checked with jq's own regex engine (Oniguruma) running the library patterns case-insensitively.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
LIB="$SK/tools/hook-formulas.json"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SW=(--skill-dir "$SK" master-virality/swipe)
j() { echo "$OUT" | jq -c "$1"; }
# swipe.classify, independently: most matching patterns wins, ties go to the larger name
cls() { jq -r --arg t "$1" '[.hooks[] | {n: ([.match[] as $p | select($t | test($p; "i"))] | length), name}] | map(select(.n > 0)) | sort_by([.n, .name]) | (last.name // "Unclassified")' "$LIB"; }

cat > "$TMP/c.json" <<'EOF'
{"videos":[
{"channel":"@a","views":1200000,"hook":"I feed four people on $62 a week.","title":"x","url":"u1"},
{"channel":"@a","views":48000,"title":"Three pantry meals","url":"u2"},
{"channel":"@a","views":51300,"title":"My Sunday prep","url":"u3"},
{"channel":"@a","views":39000,"title":"Stop buying pre-cut vegetables.","url":"u4"},
{"channel":"@a","views":null,"title":"no count"},
{"channel":"@a","views":"","title":"empty count"},
{"channel":"@b","views":3100000,"hook":"I cut our bill by $300.","url":""}]}
EOF
# @a: 39000 48000 51300 1200000 -> median (48000 + 51300) / 2 = 49650; 1200000 / 49650 = 24.169... -> 24.17
run "${SW[@]}" "$TMP/c.json" --min 2.0
eq "$CODE" 0 "exit 0"
eq "$OUT" "$(cat <<'EOF'

  5 videos across 2 channels, outliers at 2.0x or better

    24.17x  1,200,000  vs    49,650 median   @a                     x
            The Payoff

  skipped 1 channel(s) with under 4 videos collected - a median off one or
  two videos is not a median: @b (1)

  formulas among the outliers
     1x  The Payoff
EOF
)" "text report (upstream: 1,200,000, The Payoff, @b (1))"
run "${SW[@]}" "$TMP/c.json" --json --min 0.5
eq "$(j '[.outliers[] | .multiple]')" "[24.17,1.03,0.97,0.79]" "multiples: 51300/49650, 48000/49650, 39000/49650"
eq "$(j '[.outliers[] | .median] | unique')" "[49650]" "int(median)"
eq "$(j '.outliers[0] | keys_unsorted')" '["channel","title","views","median","multiple","formula","url"]' "row keys"
eq "$(j '.skipped_thin_channels')" '[["@b",1]]' "thin channels as [channel, n]"
eq "$(j '.outliers[0].formula')" "\"$(cls 'I feed four people on $62 a week.')\"" "hook formula agrees with jq"
eq "$(j '.outliers[3].formula')" "\"$(cls 'Stop buying pre-cut vegetables.')\"" "title formula agrees with jq"
eq "$(j '.outliers[1].formula')" "\"$(cls 'My Sunday prep')\"" "unclassified agrees with jq"
run "${SW[@]}" "$TMP/c.json" --json
eq "$(j '.outliers | length')" 1 "default --min 1.5"

# ---- every row's formula agrees with jq, over the library's own examples as titles
jq '{videos: [.hooks[] | {channel: "@lib", views: 100, title: .example}, {channel: "@lib", views: 100, title: .written}]}' "$LIB" > "$TMP/lib.json"
run "${SW[@]}" "$TMP/lib.json" --json --min 0
dis=0
while IFS=$'\t' read -r t f; do [ "$(cls "$t")" = "$f" ] || { dis=$((dis+1)); echo "    <$t>: $f vs $(cls "$t")"; }; done < <(echo "$OUT" | jq -r '.outliers[] | [.title, .formula] | @tsv')
eq "$(j '.outliers | length')" 52 "52 rows"
eq "$dis" 0 "classify agrees with jq on all 52"

# ---- medians, zero medians, keys
cat > "$TMP/m.json" <<'EOF'
[{"channel":"odd","views":10,"title":"a"},{"channel":"odd","views":20,"title":"b"},{"channel":"odd","views":30,"title":"c"},
 {"channel":"odd","views":40,"title":"d"},{"channel":"odd","views":"100","title":"e"},
 {"channel":"zero","views":0,"title":"z1"},{"channel":"zero","views":0,"title":"z2"},{"channel":"zero","views":0,"title":"z3"},{"channel":"zero","views":0,"title":"z4"},
 {"views":5,"title":"n1"},{"views":5,"title":"n2"},{"views":5,"title":"n3"},{"views":15.9,"title":"n4"},
 {"channel":1,"views":1,"title":"k1"},{"channel":1.0,"views":1,"title":"k2"},{"channel":true,"views":1,"title":"k3"},{"channel":"1","views":1,"title":"k4"},{"channel":1,"views":1,"title":"k5"}]
EOF
run "${SW[@]}" "$TMP/m.json" --json --min 0
# odd: 10 20 30 40 100 -> median 30; "100" is float("100") and int("100")
eq "$(j '[.outliers[] | select(.channel == "odd") | [.title, .multiple]]')" '[["e",3.33],["d",1.33],["c",1.0],["b",0.67],["a",0.33]]' "odd count: the middle value"
eq "$(j '[.outliers[] | select(.channel == "odd") | .views]')" "[100,40,30,20,10]" "string views become ints"
has "$OUT" '"multiple": 0,' "a zero median gives the int 0, not 0.0"
eq "$(j '[.outliers[] | select(.channel == "zero")] | length')" 4 "zero-median rows kept at --min 0"
# no channel -> "?": 5 5 5 15.9 -> median 5; int(15.9) = 15
eq "$(j '[.outliers[] | select(.channel == "?") | [.title, .views, .multiple]]')" '[["n4",15,3.18],["n1",5,1.0],["n2",5,1.0],["n3",5,1.0]]' "missing channel is ?"
# 1, 1.0 and true are one dict key (the first spelling wins); "1" is a different key
eq "$(j '.skipped_thin_channels')" '[["1",1]]' "only the string \"1\" is thin"
eq "$(j '[.outliers[] | select(.channel == 1) | .title]')" '["k1","k2","k3","k5"]' "1, 1.0 and true group together"
run "${SW[@]}" "$TMP/m.json" --min 1.2
has "$OUT" "outliers at 1.2x or better" "--min echoed as a float"
has "$OUT" "     3.33x        100  vs        30 median   odd                    e" "row columns"
run "${SW[@]}" "$TMP/m.json" --min 0
eq "$CODE" 1 "text mode: slicing the int channel 1 is a TypeError, exit 1"

# ---- caps: 25 rows, 6 thin channels named, 22 / 52 character columns
jq -n '[range(1; 31) | {channel: "A channel name longer than twenty-two", views: ., title: ("t" * 60)}] + [range(1; 8) | {channel: "thin\(.)", views: 1, title: "x"}]' > "$TMP/big.json"
run "${SW[@]}" "$TMP/big.json" --min 0
eq "$(echo "$OUT" | grep -cE '^ +[0-9.]+x .* vs ')" 25 "at most 25 rows"
has "$OUT" "A channel name longer  $(printf 't%.0s' $(seq 52))"$'\n' "channel cut to 22, title to 52"
has "$OUT" "skipped 7 channel(s)" "7 thin"
has "$OUT" "two videos is not a median: thin1 (1), thin2 (1), thin3 (1), thin4 (1), thin5 (1), thin6 (1)"$'\n' "first 6 named"
has "$OUT" "    30x  Unclassified" "formula counts"

# ---- errors, as the original
echo '[{"channel":"a","views":"1,200"},{"channel":"a","views":1},{"channel":"a","views":1},{"channel":"a","views":1}]' > "$TMP/e1.json"
run "${SW[@]}" "$TMP/e1.json"
eq "$CODE" 1 "float('1,200') raises"
echo '[{"channel":"a","views":1,"title":5},{"channel":"a","views":1},{"channel":"a","views":1},{"channel":"a","views":1}]' > "$TMP/e2.json"
run "${SW[@]}" "$TMP/e2.json"
eq "$CODE" 1 "a non-string title with no hook raises in re.search"
echo '["not a dict"]' > "$TMP/e3.json"
run "${SW[@]}" "$TMP/e3.json"
eq "$CODE" 1 "a non-dict row raises"
echo '{"items":[]}' > "$TMP/e4.json"
run "${SW[@]}" "$TMP/e4.json"
eq "$OUT" "$(printf '\n  0 videos across 0 channels, outliers at 1.5x or better\n\n    nothing cleared the threshold - collect more per channel or lower --min')" "a dict with no videos is empty"
run "${SW[@]}" 3 "$TMP/c.json" --json
eq "$CODE" 0 "a bare number is not taken as the file"
run "${SW[@]}" "$TMP/c.json" --min
eq "$CODE" 1 "--min with no value: IndexError"
run "${SW[@]}" "$TMP/c.json" --min abc
eq "$CODE" 1 "--min abc: ValueError"
run "${SW[@]}" "$TMP/nope.json"
eq "$CODE" 1 "missing file: doc, exit 1"
has "$OUT" "swipe - rank collected videos" "doc"
run "${SW[@]}" --help
eq "$CODE" 0 "--help exits 0"
has "$OUT" "Input can be collect output. The formula comes from hook-formulas.json in this folder" "help renames as documented"
hasnt "$OUT" ".py" "no .py in the help"
done_ "master-virality/swipe"

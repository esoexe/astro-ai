#!/usr/bin/env bash
# master-virality/humanize: the case of master-skills/skills/virality/tools/test_virality.py (test_humanize_strips_tells) plus
# hand-traced drafts, run against the C++ tool with the converted pack's tools/slop.json. Each expected text is traced by
# hand through the three passes (invisible, typographic, lexical longest-first) and the cleanup substitutions.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-virality"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
HU=(--skill-dir "$SK" master-virality/humanize)
j() { echo "$OUT" | jq -c "$1"; }
hz() { OUT=$(printf '%s' "$1" | $T "${HU[@]}" - --json); }

# ---- upstream draft: every tell is gone
printf 'In this video we delve into a robust \xe2\x80\x94 seamless workflow.\xe2\x80\x8b Let that sink in.\n' > "$TMP/d.txt"
run "${HU[@]}" "$TMP/d.txt" --report
eq "$CODE" 0 "exit 0"
for gone in $'\xe2\x80\x94' $'\xe2\x80\x8b' delve robust "In this video" "sink in"; do hasnt "$OUT" "$gone" "removed: $gone"; done
# U+200B deleted; em dash -> ", "; "let that sink in" and "in this video" deleted, "delve into" -> "look at", seamless -> clean,
# robust -> solid; "workflow. ." collapses to "workflow.." (as the original does)
eq "$OUT" "we look at a solid, clean workflow.." "the cleaned text"
eq "$ERR" "$(cat <<'EOF'

HUMANIZE REPORT
---------------
7 machine artefacts removed, 0 structural tells flagged for rewrite

1. INVISIBLE CHARACTERS
-----------------------
    1x  U+200B ZERO WIDTH SPACE  -> delete

2. TYPOGRAPHY
-------------
    1x  — EM DASH  -> ,

3. SLOP LEXICON
---------------
    1x  let that sink in  -> (deleted)   [closers]
    1x  in this video  -> (deleted)   [spoken]
    1x  delve into  -> look at   [verbs]
    1x  seamless  -> clean   [adjectives]
    1x  robust  -> solid   [adjectives]
EOF
)" "the report, longest lexicon entries first"

# ---- passes
hz 'Read https://ex.com/robust—x and stop—now.'
eq "$(j '.text')" '"Read https://ex.com/robust—x and stop, now.\n"' "URLs are protected from every pass"
eq "$(j '.report.typographic')" '[{"name":"— EM DASH","count":1,"to":","}]' "only the em dash outside the URL counts"
eq "$(j '.report.lexical')" '[]' "robust inside the URL is left alone"
hz 'Pages 5–10 are fine – mostly.'
eq "$(j '.text')" '"Pages 5-10 are fine, mostly.\n"' "en dash: a range becomes a hyphen, a spaced dash a comma"
eq "$(j '.report.typographic')" '[{"name":"– EN DASH","count":2,"to":"-"}]' "en dash count"
hz '“Hi” it’s fine… ok → go · done • end′'
eq "$(j '.text')" "\"\\\"Hi\\\" it's fine... ok -> go - done - end'\\n\"" "quotes, ellipsis, arrow, dots, prime"
eq "$(j '[.report.typographic[].name]')" '["’ RIGHT SINGLE QUOTE","“ LEFT DOUBLE QUOTE","” RIGHT DOUBLE QUOTE","… HORIZONTAL ELLIPSIS","′ PRIME","· MIDDLE DOT","• BULLET","→ RIGHTWARDS ARROW"]' "typography in lexicon order"
hz 'DELVE INTO this. Delve into that. delve into it.'
eq "$(j '.text')" '"LOOK AT this. Look at that. look at it.\n"' "replacements keep the case"
eq "$(j '.report.lexical')" '[{"find":"delve into","replace":"look at","count":3,"family":"verbs"}]' "one lexical hit, three times"
hz 'Let that sink in. We grew 40%.'
eq "$(j '.text')" '"We grew 40%.\n"' "a deleted phrase leaves no stray full stop"
OUT=$(printf 'We\xc2\xa0grew\xe2\x80\x89fast\xe2\x81\xa6 ok\xf3\xa0\x81\x81.\n' | $T "${HU[@]}" - --json)
eq "$(j '.text')" '"We grew fast ok.\n"' "hard spaces become spaces; tags and other format characters go"
eq "$(j '.report.invisible')" '[{"name":"U+E0000-U+E007F TAG CHARACTERS (invisible ASCII smuggling)","count":1,"action":"delete"},{"name":"U+00A0 NO-BREAK SPACE","count":1,"action":"space"},{"name":"U+2009 THIN SPACE","count":1,"action":"space"},{"name":"other invisible format chars","count":1,"action":"delete"}]' "invisible report in lexicon order, stray Cf last"
hz 'We ship code daily. We test code weekly. We fix bugs quickly. We read docs often.'
eq "$(j '.report.structures')" '[{"name":"Uniform sentence length (variation 0.00)","count":4,"fix":"Break one sentence in half. Let another run long. Machines write even."}]' "four equal sentences are flagged"
hz "It's not just a tool, it's a habit."
eq "$(j '.report.structures[0].name')" "$(jq -c '.structures[] | select(.id == "not-just") | .name' "$SK/tools/slop.json")" "a structural tell is reported"
eq "$(j '.text')" "\"It's not just a tool, it's a habit.\\n\"" "and never rewritten"
eq "$(j 'keys_unsorted')" '["text","report"]' "JSON keys"
eq "$(j '.report | keys_unsorted')" '["invisible","typographic","lexical","structures"]' "report keys"
OUT=$(printf 'café — ok\n' | $T "${HU[@]}" - --json)
has "$OUT" '"text": "café, ok\n"' "ensure_ascii=False, indent 2"

# ---- outputs
run "${HU[@]}" "$TMP/d.txt" -o "$TMP/clean.txt"
eq "$OUT" "" "-o: nothing on stdout"
eq "$ERR" "wrote $TMP/clean.txt" "-o: says where"
eq "$(cat "$TMP/clean.txt")" "we look at a solid, clean workflow.." "-o: the file holds the text"
eq "$(tail -c1 "$TMP/clean.txt" | od -An -c | tr -d ' ')" '\n' "one trailing newline"
printf 'We grew 40%% in May.\n' > "$TMP/clean_in.txt"
run "${HU[@]}" "$TMP/clean_in.txt" --report
eq "$ERR" "$(printf '\nHUMANIZE REPORT\n---------------\n0 machine artefacts removed, 0 structural tells flagged for rewrite\n\nCLEAN\n-----\n  Nothing to strip.')" "a clean draft"
echo '{"invisible":[],"typographic":[],"words":[],"phrases":[],"structures":[{"id":"b","name":"Broken","regex":"(","fix":"x"},{"id":"o","name":"Ok","regex":"(?i)synergy","fix":"y"}]}' > "$TMP/lex.json"
OUT=$(printf 'Synergy matters. synergy wins.\n' | $T "${HU[@]}" - --json --lexicon "$TMP/lex.json")
eq "$(j '.report.structures')" '[{"name":"Ok","count":2,"fix":"y"}]' "a structure regex that does not compile is skipped (re.error)"
run "${HU[@]}" "$TMP/missing.txt"
eq "$CODE" 1 "missing input: exit 1"
run "${HU[@]}" "$TMP/d.txt" --lexicon "$TMP/missing.json"
eq "$CODE" 1 "missing lexicon: exit 1"
run "${HU[@]}" -h
eq "$CODE" 0 "-h"
has "$OUT" "usage: humanize [-h] [-o OUT] [--report] [--json] [--lexicon LEXICON] [input]" "usage"
has "$OUT" "  -o OUT, --out OUT  write cleaned text here instead of stdout" "help column = longest invocation + 4, as argparse"
has "$OUT" "  --lexicon LEXICON  path to slop.json" "lexicon help"
done_ "master-virality/humanize"

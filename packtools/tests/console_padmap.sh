#!/usr/bin/env bash
# console-controller/padmap: the cases of console-skill/tests/test_controller.py through the CLI, plus exact-output
# checks. The upstream TestCompile / TestBehaviour classes build the generated ps2_pad.cpp against a mock raylib.h and
# the same stdin-driven harness with the host C++ compiler (skipped when there is none).
source "$(dirname "$0")/lib.sh"
LIBSKILL="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-console/skills/console-controller"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
TMP=$(pwd -P)
PM=(console-controller/padmap)
j()  { echo "$OUT" | jq -c "$1"; }
jr() { echo "$OUT" | jq -r "$1"; }
# check a config given as JSON text: ck '<json>' [args...]  (OUT = the --json report)
ck() { printf '%s' "$1" > c.json; shift; run "${PM[@]}" check --config c.json --json "$@"; }
# generate from a config given as JSON text to stdout: gn '<json>' [args...]
gn() { printf '%s' "$1" > g.json; shift; run "${PM[@]}" gen --config g.json --out - "$@"; }
line() { echo "$OUT" | sed -n "$1p"; }

# ================================================================ Tables and presets
run "${PM[@]}" table --json
eq "$CODE" 0 "table rc"
eq "$(j '[.buttons[] | select(.button=="CROSS")][0]')" '{"button":"CROSS","mask":"0x4000","byte":"data[3]","bit":6,"raylib":"GAMEPAD_BUTTON_RIGHT_FACE_DOWN","keyboard":"X/SPACE"}' "cross row"
eq "$(j '.buttons[0]')" '{"button":"UP","mask":"0x0010","byte":"data[2]","bit":4,"raylib":"GAMEPAD_BUTTON_LEFT_FACE_UP","keyboard":"UP"}' "first row"
eq "$(j '[.buttons[].button]')" '["UP","DOWN","LEFT","RIGHT","CROSS","CIRCLE","SQUARE","TRIANGLE","L1","R1","L2","R2","L3","R3","START","SELECT"]' "DS2 order"
eq "$(j '[.buttons[] | .mask]')" '["0x0010","0x0040","0x0080","0x0020","0x4000","0x2000","0x8000","0x1000","0x0400","0x0800","0x0100","0x0200","0x0002","0x0004","0x0008","0x0001"]' "bit table matches runtime"
eq "$(j '[.buttons[] | select(.button=="SELECT" or .button=="L2" or .button=="SQUARE") | [.byte, .bit]]')" '[["data[3]",7],["data[3]",0],["data[2]",0]]' "byte and bit"
eq "$(j '.sticks')" '{"data[4]":"RX","data[5]":"RY","data[6]":"LX","data[7]":"LY","centre":"0x80","range":"0x00 (left/up) to 0xFF (right/down)"}' "sticks"
eq "$(j 'keys_unsorted')" '["buttons","sticks","active_low"]' "table key order"
eq "$(j '.active_low')" true "active low"
run "${PM[@]}" table
eq "$CODE" 0 "table text rc"
has "$OUT" "ACTIVE-LOW" "table text"
eq "$(line 1)" "DualShock 2 report (32 bytes) as PS2Recomp's PSPadBackend::readState fills it" "table title"
eq "$(line 4)" "$(printf '  %-9s %-7s %-8s %-4s %-32s %s' button mask byte bit 'raylib (default)' keyboard)" "table header"
eq "$(line 9)" "$(printf '  %-9s %-7s %-8s %-4d %-32s %s' CROSS 0x4000 'data[3]' 6 GAMEPAD_BUTTON_RIGHT_FACE_DOWN X/SPACE)" "table cross line"
eq "$(echo "$OUT" | wc -l)" 23 "table line count"
eq "$(line 23)" "          0x00 = full left/up, 0xFF = full right/down" "table last line"

run "${PM[@]}" presets
eq "$CODE" 0 "presets rc"
for n in playstation xbox switch switch-labels keyboard; do has "$OUT" "$n:" "preset $n listed"; done
has "$OUT" "playstation: PlayStation (DualSense, DualShock 4, DualShock 3)
  Same buttons in the same places as the DualShock 2.
  UP        <- D-pad up (LEFT_FACE_UP)" "playstation text"
has "$OUT" "  CROSS     <- Cross (RIGHT_FACE_DOWN)" "cross label"
has "$OUT" "keyboard: Keyboard only (gamepads ignored)
  Arrows = D-pad, WASD = left stick, IJKL = right stick, X/Space = Cross, C = Circle, Z = Square, V = Triangle, Q/E = L1/R1, 1/3 = L2/R2, Left Shift/F = L3/R3, Enter = Start, Tab = Select.
  gamepads ignored; keyboard below
" "keyboard preset text"
has "$OUT" "keyboard (every preset, works next to a pad unless keyboard_always is false):
  UP            <- UP" "keyboard section"
has "$OUT" "  CROSS         <- X, SPACE" "keyboard cross"
eq "$(echo "$OUT" | tail -n1)" "  RSTICK_RIGHT  <- L" "keyboard last"
run "${PM[@]}" presets xbox
has "$OUT" "xbox: Xbox (Series, One, 360) and most PC pads" "xbox title"
has "$OUT" "  CROSS     <- A (RIGHT_FACE_DOWN)" "xbox label"
has "$OUT" "  START     <- Menu / Start (MIDDLE_RIGHT)" "xbox start"
hasnt "$OUT" "playstation:" "only the named preset"
run "${PM[@]}" presets switch-labels --json
eq "$(j '.["switch-labels"].buttons.CROSS[0].raylib')" '"RIGHT_FACE_RIGHT"' "switch-labels cross"
has "$(jr '.["switch-labels"].buttons.CROSS[0].label')" "A" "switch-labels label"
eq "$(j '.["switch-labels"].buttons.CROSS')" '[{"raylib":"RIGHT_FACE_RIGHT","label":"A (right)"}]' "switch-labels cross entry"
eq "$(j '[.["switch-labels"].buttons | .CIRCLE[0].raylib, .SQUARE[0].raylib, .TRIANGLE[0].raylib]')" '["RIGHT_FACE_DOWN","RIGHT_FACE_UP","RIGHT_FACE_LEFT"]' "switch by label"
eq "$(j 'keys_unsorted')" '["keyboard","switch-labels"]' "presets json keys"
eq "$(j '.["switch-labels"] | keys_unsorted')" '["title","notes","gamepad","buttons"]' "preset key order"
eq "$(j '.keyboard.CROSS')" '["X","SPACE"]' "keyboard map in json"
eq "$(j '.keyboard | length')" 24 "keyboard map size"
run "${PM[@]}" presets switch --json
eq "$(j '.switch.buttons.CROSS[0]')" '{"raylib":"RIGHT_FACE_DOWN","label":"B (bottom)"}' "switch by position"
eq "$(j '.switch.buttons.L2[0].label')" '"ZL"' "switch label"
run "${PM[@]}" presets --json
eq "$(j 'keys_unsorted')" '["keyboard","playstation","xbox","switch","switch-labels"]' "all presets keys (keyboard preset replaces the map)"
eq "$(j '.keyboard.title')" '"Keyboard only (gamepads ignored)"' "keyboard preset wins the key"
eq "$(j '.keyboard.gamepad')" '"none"' "keyboard preset gamepad"
run "${PM[@]}" presets playstation --json
eq "$(j '[.keyboard[][]] | length')" 25 "default keys"
eq "$(j '[.keyboard[][]] | unique | length')" 25 "default keys are unique"
eq "$(j '[.keyboard[][] | select(. == "ESCAPE" or . == "F1")]')" '[]' "default keyboard avoids escape and f1"
run "${PM[@]}" presets gamecube
eq "$CODE" 2 "bad preset name"
has "$ERR" "invalid choice: 'gamecube'" "bad preset message"

for n in playstation xbox switch switch-labels keyboard; do
    run "${PM[@]}" check --layout "$n" --json
    eq "$CODE:$(j '.errors'):$(j '.source'):$(j '.layout')" "0:[]:\"the $n preset\":\"$n\"" "preset $n is valid"
done

# ================================================================ Resolve and validate
ck '{"buttons": {"cross": "GAMEPAD_BUTTON_RIGHT_FACE_RIGHT", "CIRCLE": "RIGHT_FACE_DOWN"}}'
eq "$CODE:$(j .errors)" "0:[]" "override by name and prefix"
gn '{"buttons": {"cross": "GAMEPAD_BUTTON_RIGHT_FACE_RIGHT", "CIRCLE": "RIGHT_FACE_DOWN"}}'
has "$OUT" "        {PAD_CROSS, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
        {PAD_CIRCLE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}," "override rows"
ck '{"buttons": {"CROSS": "RIGHT_FACE_RIGHT"}}'
eq "$CODE" 1 "duplicate gamepad rc"
eq "$(j .errors)" '["gamepad RIGHT_FACE_RIGHT is bound to CROSS and CIRCLE (add it to allow_duplicates if you mean it)"]' "duplicate gamepad binding"
ck '{"buttons": {"CROSS": "RIGHT_FACE_RIGHT"}, "allow_duplicates": ["RIGHT_FACE_RIGHT"]}'
eq "$CODE:$(j .errors)" "0:[]" "allow_duplicates"
ck '{"buttons": {"CROSS": "RIGHT_FACE_RIGHT"}, "allow_duplicates": "right_face_right"}'
eq "$(j .errors)" '[]' "allow_duplicates as one string, any case"
ck '{"buttons": {"L3": []}}'
eq "$(j .errors)" '["L3 is not bound to any gamepad button"]' "unbound button"
ck '{"buttons": {"L3": null}}'
eq "$(j .errors)" '["L3 is not bound to any gamepad button"]' "null unbinds"
ck '{"keyboard": {"CROSS": ["Z"]}}'
eq "$(j .errors)" '["key Z is bound to CROSS and SQUARE (add it to allow_duplicates if you mean it)"]' "duplicate key"
ck '{"keyboard": {"CROSS": ["Z"]}, "allow_duplicates": ["KEY_Z"]}'
eq "$(j .errors)" '[]' "allow KEY_ prefix"
ck '{"keyboard": {"lstick_up": "key_up"}}'
eq "$(j .errors)" '["key UP is bound to UP and LSTICK_UP (add it to allow_duplicates if you mean it)"]' "stick key names and owner order"
ck '{"buttons": {"HOME": "MIDDLE", "CROSS": "BIG_RED"}, "keyboard": {"START": "NOPE", "LSTICK_SIDEWAYS": "A"}, "layout": "gamecube", "colour": "red"}'
eq "$CODE" 1 "unknown names rc"
eq "$(jr '.errors[]')" "layout 'gamecube' is not one of: playstation, xbox, switch, switch-labels, keyboard
buttons: 'HOME' is not a DS2 button (UP, DOWN, LEFT, RIGHT, CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2, L3, R3, START, SELECT)
buttons.CROSS: 'BIG_RED' is not a raylib gamepad button (see padmap.py table)
keyboard.START: 'NOPE' is not a raylib key name
keyboard: 'LSTICK_SIDEWAYS' is not a DS2 button or LSTICK_/RSTICK_ direction
CROSS is not bound to any gamepad button" "unknown names"
eq "$(j .warnings)" "[\"unknown setting 'colour' is ignored\",\"START has no keyboard key\"]" "unknown setting warning"
eq "$(j .layout)" '"playstation"' "bad layout falls back"
ck '{"$comment": "x", "comment": 1, "notes": [], "it'"'"'s": 1}'
eq "$(j .warnings)" "[\"unknown setting \\\"it's\\\" is ignored\"]" "comment keys known, repr quoting"
ck '{"buttons": {"CROSS": 5, "CIRCLE": [true, null, 1.5]}}'
eq "$(jr '.errors[0:4][]')" "buttons.CROSS: 5 is not a raylib gamepad button (see padmap.py table)
buttons.CIRCLE: True is not a raylib gamepad button (see padmap.py table)
buttons.CIRCLE: None is not a raylib gamepad button (see padmap.py table)
buttons.CIRCLE: 1.5 is not a raylib gamepad button (see padmap.py table)" "non-string sources use repr"
ck '{"buttons": {"\u00df": "x"}}'
eq "$(jr '.errors[0]')" "buttons: 'ß' is not a DS2 button (UP, DOWN, LEFT, RIGHT, CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2, L3, R3, START, SELECT)" "unicode key"
ck '{"layout": 5}'
eq "$(jr '.errors[0]')" "layout 5 is not one of: playstation, xbox, switch, switch-labels, keyboard" "numeric layout"
printf '{"layout": [1]}' > c.json; run "${PM[@]}" check --config c.json
eq "$CODE" 1 "unhashable layout is an uncaught error"
has "$ERR" "unhashable type: 'list'" "unhashable message"

DZ="deadzone (left) must be a number from 0 to 0.9"
DZR="deadzone (right) must be a number from 0 to 0.9"
INV='invert_y must be false, true (the right stick), "left", "right" or "both"'
GP='gamepad must be "first", "none" or a raylib pad index 0 to 3'
TT="trigger_threshold must be null or a number above 0 and at most 1"
while IFS='|' read -r cfg want; do
    ck "$cfg"
    eq "$(jr '.errors | join(" / ")')" "$want" "range $cfg"
done <<EOF
{"deadzone": 1.5}|$DZ / $DZR
{"deadzone": -0.1}|$DZ / $DZR
{"deadzone": true}|$DZ / $DZR
{"deadzone": NaN}|$DZ / $DZR
{"deadzone": {"left": 0.2, "right": "x"}}|$DZR
{"deadzone": {"left": null}}|$DZ
{"invert_y": "up"}|$INV
{"invert_y": 1}|$INV
{"invert_y": [true]}|$INV
{"invert_y": "true"}|$INV
{"players": 3}|players must be 1 or 2
{"players": true}|players must be 1 or 2
{"players": 1.5}|players must be 1 or 2
{"players": "2"}|players must be 1 or 2
{"gamepad": 7}|$GP
{"gamepad": "second"}|$GP
{"gamepad": true}|$GP
{"gamepad": 2.0}|$GP
{"gamepad": -1}|$GP
{"trigger_threshold": 0}|$TT
{"trigger_threshold": 2}|$TT
{"trigger_threshold": true}|$TT
{"trigger_threshold": "0.5"}|$TT
{"stick_swap": "yes"}|stick_swap must be true or false
{"keyboard_always": 1}|keyboard_always must be true or false
{"touch_on_desktop": "x"}|touch_on_desktop must be true or false
{"buttons": []}|buttons must be an object of DS2 button -> raylib button
{"keyboard": "WASD"}|keyboard must be an object of DS2 button or stick direction -> key names
{"touch": {}}|touch must be a list of zones
{"buttons": null, "keyboard": null, "touch": null, "allow_duplicates": null}|
{"players": 2.0, "gamepad": 3, "deadzone": 0, "trigger_threshold": 1}|
EOF

gn '{"deadzone": {"left": 0.1, "right": 0.25}, "invert_y": "both"}'
has "$OUT" "    constexpr float kDeadzoneLeft = 0.1f; // radial
    constexpr float kDeadzoneRight = 0.25f;
    constexpr bool kInvertLeftY = true;
    constexpr bool kInvertRightY = true;" "deadzone per stick and invert both"
gn '{"invert_y": true}'; has "$OUT" "kInvertLeftY = false;
    constexpr bool kInvertRightY = true;" "invert true is the right stick"
gn '{"invert_y": "LEFT"}'; has "$OUT" "kInvertLeftY = true;
    constexpr bool kInvertRightY = false;" "invert left (any case)"
gn '{"invert_y": "right"}'; has "$OUT" "kInvertLeftY = false;
    constexpr bool kInvertRightY = true;" "invert right"
gn '{"invert_y": "none"}'; has "$OUT" "kInvertLeftY = false;
    constexpr bool kInvertRightY = false;" "invert none"
gn '{"players": 2.0, "gamepad": 3, "deadzone": 0, "trigger_threshold": 1, "stick_swap": true, "keyboard_always": false}'
has "$OUT" "    constexpr int kGamepadIndex = 3; // -1 = first connected pads in order, -2 = gamepads ignored
    constexpr int kPlayers = 2; // port 1 gets the second pad when 2
    constexpr bool kKeyboardAlways = false; // keyboard also works while a pad is connected
    constexpr float kDeadzoneLeft = 0.0f; // radial
    constexpr float kDeadzoneRight = 0.0f;
    constexpr bool kInvertLeftY = false;
    constexpr bool kInvertRightY = false;
    constexpr bool kStickSwap = true;
    constexpr float kTriggerThreshold = 1.0f; // analog L2/R2 bindings only" "settings block"
gn '{"deadzone": 1e-05}'; has "$OUT" "kDeadzoneLeft = 0.000010000f;" "exponent floats use %.9f"
gn '{"layout": "keyboard"}'; has "$OUT" "kGamepadIndex = -2;" "keyboard layout ignores pads"
has "$OUT" "// Layout preset: keyboard. Edit controls.json" "keyboard layout line"
gn '{"gamepad": "none"}'; has "$OUT" "kGamepadIndex = -2;" "gamepad none"

ck '{"trigger_threshold": 0.3}'
eq "$(j .errors)" '[]' "trigger threshold ok"
gn '{"trigger_threshold": 0.3}'
has "$OUT" "    constexpr AxisBinding kTriggerAxes[] = {
        {PAD_L2, GAMEPAD_AXIS_LEFT_TRIGGER},
        {PAD_R2, GAMEPAD_AXIS_RIGHT_TRIGGER},
    };" "trigger threshold moves l2 r2 to axes"
hasnt "$OUT" "GAMEPAD_BUTTON_LEFT_TRIGGER_2" "no trigger button row"
has "$OUT" "kTriggerThreshold = 0.3f;" "threshold value"
gn '{}'
has "$OUT" "kTriggerThreshold = 0.5f; // analog L2/R2 bindings only" "default threshold"
has "$OUT" "    constexpr AxisBinding kTriggerAxes[] = {
        {0u, 0}, // none
    };" "empty axis table"

ck '{"keyboard": {"CIRCLE": ["C", "ESCAPE"], "SELECT": ["F1"]}}'
eq "$(j .errors)" '[]' "escape and f1 are allowed"
eq "$(jr '.warnings[]')" "ESCAPE is bound, but raylib closes the window on Escape (the runtime never calls SetExitKey), so that press also quits the game
F1 is bound, but desktop builds with the debug UI toggle the debug panel on F1 (ps2_debug_panel.cpp)" "escape and f1 warn"
ck '{"layout": "keyboard", "keyboard": {"START": []}}'
eq "$(j .errors)" '["START has no keyboard key"]' "keyboard layout needs every key"
ck '{"layout": "xbox", "keyboard": {"START": []}}'
eq "$(j .errors):$(j .warnings)" '[]:["START has no keyboard key"]' "a pad layout only warns"
ck '{"layout": "keyboard", "players": 2}'
eq "$(j .errors)" '["players 2 needs gamepads (gamepad is \"none\")"]' "players two needs pads"
ck '{"layout": "keyboard", "keyboard_always": false, "touch_on_desktop": true}'
eq "$(jr '.warnings[]')" "keyboard_always is false but gamepads are ignored: the keyboard is used anyway
touch_on_desktop is true but there are no touch zones" "keyboard_always and touch_on_desktop warnings"
ck '{"buttons": {"START": "MIDDLE", "SELECT": ["MIDDLE_LEFT", "middle"]}, "allow_duplicates": ["MIDDLE"]}'
eq "$(j .errors)" '[]' "guide button allowed"
eq "$(jr '.warnings[]')" "START uses the guide button (MIDDLE): Steam, Windows and macOS often take that press for themselves
SELECT uses the guide button (MIDDLE): Steam, Windows and macOS often take that press for themselves" "guide button warnings"
ck '{"buttons": {"CROSS": ["RIGHT_FACE_DOWN", "RIGHT_FACE_DOWN"]}}'
eq "$(j .errors)" '["gamepad RIGHT_FACE_DOWN is bound to CROSS and CROSS (add it to allow_duplicates if you mean it)"]' "same source twice"
ck '{"buttons": {"CROSS": "RIGHT_FACE_RIGHT", "SQUARE": "RIGHT_FACE_RIGHT", "R1": "LEFT_TRIGGER_1"}}'
eq "$(jr '.errors[]')" "gamepad LEFT_TRIGGER_1 is bound to L1 and R1 (add it to allow_duplicates if you mean it)
gamepad RIGHT_FACE_RIGHT is bound to CROSS and CIRCLE and SQUARE (add it to allow_duplicates if you mean it)" "clashes sorted by source"
ck '{"layout": "keyboard", "buttons": {"CROSS": []}}'
eq "$(j .errors)" '[]' "gamepad bindings unchecked when pads are ignored"

ck '{"touch": [{"button": "cross", "x": 0.9, "y": 0.8, "r": 0.06}, {"stick": "LEFT", "x": 0.15, "y": 0.75, "r": 0.12}]}'
eq "$(j .errors)" '[]' "touch zones ok"
gn '{"touch": [{"button": "cross", "x": 0.9, "y": 0.8, "r": 0.06}, {"stick": "LEFT", "x": 0.15, "y": 0.75, "r": 0.12}]}'
has "$OUT" "    constexpr TouchButton kTouchButtons[] = {
        {PAD_CROSS, 0.9f, 0.8f, 0.06f},
    };
    constexpr TouchStick kTouchSticks[] = {
        {0, 0.15f, 0.75f, 0.12f},
    };" "touch tables"
has "$OUT" "#if defined(__ANDROID__) || defined(PS2X_PAD_TOUCH)" "touch guard"
has "$OUT" "GetTouchPosition" "touch reader"
hasnt "$OUT" "#define PS2X_PAD_TOUCH 1" "touch stays mobile-only by default"
gn '{"touch_on_desktop": true, "touch": [{"stick": "right", "x": 1, "y": 0, "r": 0.5}]}'
has "$OUT" "#ifndef PS2X_PAD_TOUCH
#define PS2X_PAD_TOUCH 1
#endif" "touch_on_desktop defines the switch"
has "$OUT" "    constexpr TouchButton kTouchButtons[] = {
        {0u, 0.0f, 0.0f, 0.0f}, // none
    };
    constexpr TouchStick kTouchSticks[] = {
        {1, 1.0f, 0.0f, 0.5f},
    };" "touch sentinel and int coordinates"
while IFS='|' read -r zone want; do
    ck "{\"touch\": [$zone]}"
    eq "$(jr '.errors | join(" / ")')" "$want" "touch zone $zone"
done <<'EOF'
{"button": "CROSS", "x": 1.2, "y": 0.5, "r": 0.1}|touch[0]: x and y are fractions of the screen, 0 to 1
{"button": "CROSS", "x": 0.5, "y": -0.5, "r": 0.1}|touch[0]: x and y are fractions of the screen, 0 to 1
{"button": "CROSS", "x": 0.5, "y": 0.5, "r": 0.0}|touch[0]: r is a fraction of the shorter screen side, above 0 and at most 0.5
{"button": "CROSS", "x": 0.5, "y": 0.5, "r": 0.9}|touch[0]: r is a fraction of the shorter screen side, above 0 and at most 0.5
{"button": "HOME", "x": 0.5, "y": 0.5, "r": 0.1}|touch[0].button 'HOME' is not a DS2 button
{"button": null, "x": 0.5, "y": 0.5, "r": 0.1}|touch[0].button None is not a DS2 button
{"stick": "middle", "x": 0.5, "y": 0.5, "r": 0.1}|touch[0].stick must be "left" or "right"
{"button": "CROSS", "stick": "left", "x": 0.5, "y": 0.5, "r": 0.1}|touch[0] needs exactly one of "button" or "stick"
{"x": 0.5, "y": 0.5, "r": 0.1}|touch[0] needs exactly one of "button" or "stick"
{"button": "CROSS", "x": "a", "y": 0, "r": 0.1}|touch[0].x must be a number
{"button": "CROSS", "x": 0.5, "r": 0.1}|touch[0].y must be a number
{"button": "CROSS", "x": 0.5, "y": 0.5, "r": true}|touch[0].r must be a number
"zone"|touch[0] must be an object
{"button": "CROSS", "x": 0.5, "y": 0.5, "r": 0.1}, 7, {"stick": "up", "x": 0, "y": 0, "r": 0.1}|touch[1] must be an object / touch[2].stick must be "left" or "right"
EOF

# ================================================================ Scripts
printf '# walk to the menu\n0 -\n120 START\n125 none 128 128 128 128   # release\n300 cross+UP 128 0 128 128\n300 0x4000 255 128 128 128\n\n' > s.txt
run "${PM[@]}" check --script s.txt --json
eq "$CODE:$(j .script)" '0:{"path":"s.txt","lines":5,"last_frame":300}' "parse forms"
run "${PM[@]}" check --script s.txt
eq "$(line 2)" "  script s.txt: 5 lines, last change at frame 300" "script text line"
: > e.txt; run "${PM[@]}" check --script e.txt
eq "$(line 2)" "  script e.txt: 0 lines, last change at frame None" "empty script"
run "${PM[@]}" check --script e.txt --json
eq "$(j .script)" '{"path":"e.txt","lines":0,"last_frame":null}' "empty script json"
sc() { printf -- "$1" > b.txt; run "${PM[@]}" check --script b.txt --json; }
while IFS='|' read -r text want; do
    sc "$text"
    eq "$CODE:$(jr '.errors | join(" / ")')" "1:script b.txt: $want" "script error $text"
done <<'EOF'
5 START\n4 -\n|line 2: frame 4 comes before the previous line's 5
x START\n|line 1: frame 'x' is not a whole number
1 JUMP\n|line 1: 'JUMP' is not a DS2 button
1 START 1 2\n|line 1: expected 'frame buttons' or 'frame buttons lx ly rx ry'
1\n|line 1: expected 'frame buttons' or 'frame buttons lx ly rx ry'
1 START 0 0 0 300\n|line 1: stick value '300' is outside 0 to 255
1 0x1FFFF\n|line 1: mask '0x1FFFF' is above 0xFFFF
1 0xZZ\n|line 1: bad hex mask '0xZZ'
1 0x\n|line 1: bad hex mask '0x'
1 0x_1f\n|line 1: bad hex mask '0x_1f'
1 0x-1\n|line 1: mask '0x-1' is above 0xFFFF
-1 START\n|line 1: frame -1 is out of range
4294967296 -\n|line 1: frame 4294967296 is out of range
000099999999999999999999 -\n|line 1: frame 99999999999999999999 is out of range
1 START 0 0 0 q\n|line 1: stick value 'q' is not a number
1 START 0 0 0 -1\n|line 1: stick value '-1' is outside 0 to 255
1 START 0 0 0 0x100\n|line 1: stick value '0x100' is outside 0 to 255
1 START+\n|line 1: '' is not a DS2 button
1 START+jump\n|line 1: 'jump' is not a DS2 button
1__0 -\n|line 1: frame '1__0' is not a whole number
1_ -\n|line 1: frame '1_' is not a whole number
1.5 -\n|line 1: frame '1.5' is not a whole number
# fine\n  \n7 -\n3 L1 # comment\n|line 4: frame 3 comes before the previous line's 7
EOF
while IFS='|' read -r text want; do
    sc "$text"
    eq "$CODE:$(j .script.lines):$(j .script.last_frame)" "0:$want" "script ok $text"
done <<'EOF'
1_0 -\n|1:10
+3 -\n|1:3
-0 -\n|1:0
4294967295 -\n|1:4294967295
1 0x0x10\n|1:1
1 0X1f\n|1:1
1 0x0x_1f\n|1:1
1 cross+up+l1\n|1:1
1 START 0x80 0X7f 255 0\n|1:1
1 START 1_2 00 0 0\n|1:1
5 -\n5 START\n|2:5
1 -\r\n2 START\r\n|2:2
1 -\r2 START\r|2:2
1 -\f2 START\v3 L1\n|3:3
1 -\t# tab before the comment\n|1:1
EOF
printf '\xd9\xa1 START \xd9\xa2\xd9\xa5\xd9\xa5 0 0 0\n' > u.txt
run "${PM[@]}" check --script u.txt --json
eq "$CODE:$(j .script)" '0:{"path":"u.txt","lines":1,"last_frame":1}' "Unicode digits, as int() reads them"
printf '\xef\xbb\xbf0 -\n' > bom.txt
run "${PM[@]}" check --script bom.txt --json
eq "$(jr '.errors[0]')" "script bom.txt: line 1: frame '\\ufeff0' is not a whole number" "a BOM stays in the first line"
printf '1 -\n\xff\n' > bad8.txt
run "${PM[@]}" check --script bad8.txt
eq "$CODE" 2 "undecodable script rc"
eq "$ERR" "error: cannot read bad8.txt: 'utf-8' codec can't decode byte 0xff in position 4: invalid start byte" "undecodable script"
mkdir -p sdir; run "${PM[@]}" check --script sdir
eq "$CODE:$ERR" "2:error: cannot read sdir: [Errno 21] Is a directory: 'sdir'" "script folder"
run "${PM[@]}" check --script nope.txt
eq "$CODE:$ERR" "2:error: no such file: nope.txt" "missing script"
run "${PM[@]}" check --script "$LIBSKILL/templates/menu-walk.txt" --json
eq "$CODE" 0 "template script is valid"
[ "$(j .script.lines)" -gt 3 ] && ok || bad "template script has more than 3 lines"

# ================================================================ CLI
run "${PM[@]}" init --layout xbox --out console/controls.json
eq "$CODE" 0 "init rc"
eq "$OUT" "wrote console/controls.json (layout xbox). Next: padmap.py check --config console/controls.json" "init text"
cat > want.json <<'EOF'
{
  "$comment": "console-controller mapping. Edit, then: padmap.py check && padmap.py gen",
  "layout": "xbox",
  "buttons": {},
  "keyboard": {},
  "deadzone": 0.15,
  "invert_y": false,
  "stick_swap": false,
  "trigger_threshold": null,
  "players": 1,
  "keyboard_always": true,
  "touch": [],
  "allow_duplicates": []
}
EOF
cmp -s console/controls.json want.json && ok || bad "starter config bytes"
run "${PM[@]}" init --out console/controls.json
eq "$CODE:$ERR" "2:error: console/controls.json already exists (pass --force to replace it)" "init refuses to overwrite"
run "${PM[@]}" init --out console/controls.json --force
eq "$CODE:$(jq -r .layout console/controls.json)" "0:playstation" "init --force, default layout"
run "${PM[@]}" init --layout xbox --out console/controls.json --force
run "${PM[@]}" check --config console/controls.json
eq "$CODE" 0 "check rc"
eq "$OUT" "checked controls.json (layout xbox)
  OK: every DS2 button is bound" "check text"
run "${PM[@]}" check
eq "$OUT" "checked controls.json (layout xbox)
  OK: every DS2 button is bound" "default config is console/controls.json"
run "${PM[@]}" check --config console/controls.json --json
eq "$(j 'keys_unsorted')" '["ok","source","layout","errors","warnings","script"]' "check json keys"
eq "$(j '[.ok, .source, .script]')" '[true,"controls.json",null]' "check json values"
run "${PM[@]}" gen --config console/controls.json --out console/ps2_pad.cpp --replay
eq "$CODE" 0 "gen rc"
N=$(wc -l < console/ps2_pad.cpp)
eq "$OUT" "wrote console/ps2_pad.cpp ($N lines, layout xbox, input replay + record)
install: padmap.py install --repo <your PS2Recomp checkout> --from console/ps2_pad.cpp
then rebuild incrementally: cmake --build <your build dir>" "gen text"
G=$(cat console/ps2_pad.cpp)
has "$G" "generated by console-controller (padmap.py gen)" "marker"
has "$G" "PS2X_INPUT_SCRIPT" "replay"
has "$G" "PS2X_INPUT_RECORD" "record"
eq "$(head -n1 console/ps2_pad.cpp)" "// ps2_pad.cpp: generated by console-controller (padmap.py gen) from controls.json." "first line"
has "$G" "            if (c == ' ' || c == '\\t' || c == '\\r' || c == '\\n')" "replay code kept verbatim (raw backslashes)"
has "$G" '            std::fprintf(stderr, "[pad-replay] %s line %u is not '"'"'frame buttons [lx ly rx ry]'"'"' in frame order; replay disabled\n",' "replay message verbatim"
has "$G" "    }

    // ---- input replay (PS2X_INPUT_SCRIPT) and recording (PS2X_INPUT_RECORD) ----" "replay block follows toReport after a blank line"
has "$G" "        std::fflush(state.record);
    }

}
" "replay block ends with a blank line before the namespace closes"
has "$G" "#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include \"runtime/ps2_pad.h\"" "MSVC guard before the includes"
has "$G" "        if (!replayReport(report))
        {
            PadFrame frame;
            readLive(0, frame);
            report = toReport(frame);
            recordReport(report);
        }" "replay readState"
[ "$(tail -c1 console/ps2_pad.cpp | od -An -tx1 | tr -d ' ')" = "0a" ] && ok || bad "ends with a newline"

mkdir -p fresh && cd fresh || exit 1
run "${PM[@]}" gen --out -
eq "$CODE" 0 "gen without config"
D="$OUT"
for want in '#include "runtime/ps2_pad.h"' '#include "ps2_host_backend.h"' "#include <cstring>" \
    "bool PSPadBackend::readState(int port, int slot, uint8_t *data, size_t size)" "if (!data || size < 32)" \
    "data[1] = kPadAnalogMarker;" "constexpr uint8_t kPadAnalogMarker = 0x73;" "data[6] = report.lx;" "data[4] = report.rx;"; do
    has "$D" "$want" "drop-in: $want"
done
hasnt "$D" "PS2X_INPUT_SCRIPT" "no replay by default"
hasnt "$D" "GetTouchPosition" "no touch by default"
hasnt "$D" "$(printf '\xe2\x80\x94')" "no em dash"
eq "$(echo "$D" | sed -n 1,2p)" "// ps2_pad.cpp: generated by console-controller (padmap.py gen) from the playstation preset.
// Layout preset: playstation. Edit controls.json and run padmap.py gen again; do not edit this file." "preset header"
has "$D" "    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;

    // ---- settings from the playstation preset ----
    constexpr int kGamepadIndex = -1;" "mask constants"
has "$D" "    constexpr ButtonBinding kGamepadButtons[] = {
        {PAD_UP, GAMEPAD_BUTTON_LEFT_FACE_UP},
        {PAD_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN},
        {PAD_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT},
        {PAD_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
        {PAD_CROSS, GAMEPAD_BUTTON_RIGHT_FACE_DOWN},
        {PAD_CIRCLE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
        {PAD_SQUARE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT},
        {PAD_TRIANGLE, GAMEPAD_BUTTON_RIGHT_FACE_UP},
        {PAD_L1, GAMEPAD_BUTTON_LEFT_TRIGGER_1},
        {PAD_R1, GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
        {PAD_L2, GAMEPAD_BUTTON_LEFT_TRIGGER_2},
        {PAD_R2, GAMEPAD_BUTTON_RIGHT_TRIGGER_2},
        {PAD_L3, GAMEPAD_BUTTON_LEFT_THUMB},
        {PAD_R3, GAMEPAD_BUTTON_RIGHT_THUMB},
        {PAD_START, GAMEPAD_BUTTON_MIDDLE_RIGHT},
        {PAD_SELECT, GAMEPAD_BUTTON_MIDDLE_LEFT},
    };" "gamepad table"
has "$D" "    constexpr KeyBinding kKeyButtons[] = {
        {PAD_UP, KEY_UP},
        {PAD_DOWN, KEY_DOWN},
        {PAD_LEFT, KEY_LEFT},
        {PAD_RIGHT, KEY_RIGHT},
        {PAD_CROSS, KEY_X},
        {PAD_CROSS, KEY_SPACE},
        {PAD_CIRCLE, KEY_C}," "key table"
has "$D" "    constexpr StickKey kStickKeys[] = {
        {0, 1, 1.0f, KEY_S} /* LSTICK_DOWN */,
        {0, 0, -1.0f, KEY_A} /* LSTICK_LEFT */,
        {0, 0, 1.0f, KEY_D} /* LSTICK_RIGHT */,
        {0, 1, -1.0f, KEY_W} /* LSTICK_UP */,
        {1, 1, 1.0f, KEY_K} /* RSTICK_DOWN */,
        {1, 0, -1.0f, KEY_J} /* RSTICK_LEFT */,
        {1, 0, 1.0f, KEY_L} /* RSTICK_RIGHT */,
        {1, 1, -1.0f, KEY_I} /* RSTICK_UP */,
    };" "stick keys sorted by direction name"
has "$D" "    }
    else if (port > 0 && port < kPlayers)" "unscripted readState"
eq "$(echo "$D" | tail -n1)" "}" "ends with the function"
run "${PM[@]}" gen --layout switch --out -
eq "$CODE" 0 "gen with layout and no config"
has "$OUT" "Layout preset: switch" "gen without config uses preset"
has "$OUT" "from the switch preset." "preset source"
run "${PM[@]}" gen --out - --replay
eq "$(printf '%s\n' "$OUT" | wc -l)" "$(( $(printf '%s\n' "$D" | wc -l) + 291 ))" "replay adds the includes, the comments and the replay code"
run "${PM[@]}" gen --config "" --layout xbox --out -
eq "$CODE" 0 "empty --config means the preset"
has "$OUT" "from the xbox preset." "empty config source"
cd .. || exit 1

printf '{"layout": "xbox"}' > lay.json
run "${PM[@]}" gen --config lay.json --layout keyboard --out -
has "$OUT" "Layout preset: keyboard" "--layout overrides the file"
has "$OUT" "from lay.json." "source is the file name"
printf '{"buttons": {"START": "MIDDLE"}}' > w.json
run "${PM[@]}" gen --config w.json --out wout/pad.cpp
eq "$(line 1)" "warning: START uses the guide button (MIDDLE): Steam, Windows and macOS often take that press for themselves" "gen prints warnings"
has "$(line 2)" "wrote wout/pad.cpp (" "gen creates parent folders"
run "${PM[@]}" gen --config w.json --out -
hasnt "$OUT" "warning:" "stdout output has no warnings"
mkdir -p odir; run "${PM[@]}" gen --out odir
eq "$CODE:$ERR" "2:error: cannot write odir: [Errno 21] Is a directory: 'odir'" "gen to a folder"
: > afile; run "${PM[@]}" gen --out afile/pad.cpp
eq "$CODE:$ERR" "2:error: cannot write afile/pad.cpp: [Errno 17] File exists: '$TMP/afile'" "parent is a file"
run "${PM[@]}" gen --out afile/sub/pad.cpp
eq "$CODE:$ERR" "2:error: cannot write afile/sub/pad.cpp: [Errno 20] Not a directory: '$TMP/afile/sub'" "grandparent is a file"
run "${PM[@]}" init --out afile/c.json
eq "$CODE:$ERR" "2:error: cannot write afile/c.json: [Errno 17] File exists: '$TMP/afile'" "init write error"

printf '{"keyboard": {"CROSS": ["Z"]}}' > bad.json
run "${PM[@]}" check --config bad.json
eq "$CODE" 1 "check failure rc"
eq "$OUT" "checked bad.json (layout playstation)
  ERROR   key Z is bound to CROSS and SQUARE (add it to allow_duplicates if you mean it)
  NOT OK" "check failure text"
run "${PM[@]}" check --config bad.json --json
eq "$(j .ok)" false "check json not ok"
run "${PM[@]}" gen --config bad.json --out x.cpp
eq "$CODE" 2 "gen refuses"
eq "$ERR" "error: key Z is bound to CROSS and SQUARE (add it to allow_duplicates if you mean it)
error: bad.json has 1 problem(s); fix them (padmap.py check) and run gen again" "gen errors"
[ -e x.cpp ] && bad "gen wrote despite errors" || ok
printf '{not json' > bad.json; run "${PM[@]}" check --config bad.json
eq "$CODE:$ERR" "2:error: bad.json is not valid JSON (line 1: Expecting property name enclosed in double quotes)" "not json"
printf '{\n"a": 1\n"b": 2}' > bad.json; run "${PM[@]}" check --config bad.json
eq "$ERR" "error: bad.json is not valid JSON (line 3: Expecting ',' delimiter)" "json line number"
printf '{\r\n"a": 1\r\n"b": 2}' > bad.json; run "${PM[@]}" check --config bad.json
eq "$ERR" "error: bad.json is not valid JSON (line 3: Expecting ',' delimiter)" "CRLF counts once"
printf '\xef\xbb\xbf{}' > bad.json; run "${PM[@]}" check --config bad.json
eq "$ERR" "error: bad.json is not valid JSON (line 1: Unexpected UTF-8 BOM (decode using utf-8-sig))" "BOM"
printf '{"a": "\xff"}' > bad.json; run "${PM[@]}" check --config bad.json
eq "$CODE:$ERR" "2:error: cannot read bad.json: 'utf-8' codec can't decode byte 0xff in position 7: invalid start byte" "undecodable config"
printf '[1, 2]' > bad.json; run "${PM[@]}" check --config bad.json
eq "$CODE:$ERR" "2:error: bad.json must hold a JSON object" "not an object"
printf '{"layout": "xbox"}\r\n' > crlf.json; run "${PM[@]}" check --config crlf.json
eq "$CODE" 0 "CRLF config"
run "${PM[@]}" check --config missing.json
eq "$CODE:$ERR" "2:error: no such file: missing.json" "missing config"
mkdir -p cdir; run "${PM[@]}" check --config cdir
eq "$CODE:$ERR" "2:error: cannot read cdir: [Errno 21] Is a directory: 'cdir'" "config folder"
run "${PM[@]}"
eq "$CODE" 2 "no command"
has "$OUT" "usage: padmap" "no command prints help"
has "$OUT" "Typical: padmap.py init --layout xbox;" "epilog"

printf '{"keyboard": {"CIRCLE": ["C", "ESCAPE"]}}' > warn.json
run "${PM[@]}" check --config warn.json
eq "$CODE" 0 "warnings pass"
has "$OUT" "  warning ESCAPE is bound" "warning line"
run "${PM[@]}" check --config warn.json --strict
eq "$CODE" 1 "strict"
eq "$(echo "$OUT" | tail -n1)" "  NOT OK" "strict text"
printf '10 START\n5 -\n' > s2.txt
run "${PM[@]}" check --config warn.json --script s2.txt
eq "$CODE" 1 "script error fails the check"
has "$OUT" "  ERROR   script s2.txt: line 2: frame 5 comes before the previous line's 10" "script error line"
eq "$(echo "$OUT" | grep -c "^  script ")" 0 "no script summary on error"
run "${PM[@]}" check --script nope.txt
eq "$CODE" 2 "missing script"

# install and restore
mkdir -p ins && cd ins || exit 1
mkdir -p PS2Recomp/ps2xRuntime/src/lib
TGT=PS2Recomp/ps2xRuntime/src/lib/ps2_pad.cpp
printf '// stock\nbool PSPadBackend::readState(int, int, uint8_t *, size_t) { return false; }\n' > $TGT
cp $TGT stock.cpp
touch -d '2001-02-03 04:05:06' $TGT
run "${PM[@]}" gen --out console/ps2_pad.cpp --config none.json --layout xbox
eq "$CODE:$ERR" "2:error: no such file: none.json" "explicit missing config"
run "${PM[@]}" gen --out console/ps2_pad.cpp --layout xbox
eq "$CODE" 0 "gen for install"
run "${PM[@]}" install --repo PS2Recomp --from console/ps2_pad.cpp --backups console/backups
eq "$CODE" 0 "install rc"
eq "$OUT" "backed up the stock file to console/backups/ps2_pad.cpp.orig
installed console/ps2_pad.cpp -> $TGT
rebuild incrementally: cmake --build <your build dir> (one .cpp, no headers changed)" "install text"
cmp -s $TGT console/ps2_pad.cpp && ok || bad "installed file"
cmp -s console/backups/ps2_pad.cpp.orig stock.cpp && ok || bad "stock backup"
eq "$(stat -c %Y console/backups/ps2_pad.cpp.orig)" "$(date -d '2001-02-03 04:05:06' +%s)" "backup keeps the time (copy2)"
run "${PM[@]}" install --repo PS2Recomp --from console/ps2_pad.cpp --backups console/backups
eq "$CODE" 0 "second install"
[[ "$(line 1)" =~ ^backed\ up\ the\ current\ file\ to\ console/backups/ps2_pad\.cpp\.[0-9]{8}T[0-9]{6}Z\.bak$ ]] && ok || bad "timestamped backup: $(line 1)"
eq "$(line 2)" "$TGT is already this mapping; nothing to do" "same mapping"
cmp -s console/backups/ps2_pad.cpp.orig stock.cpp && ok || bad "orig kept"
BAK=$(ls console/backups/*.bak | head -n1)
cmp -s "$BAK" console/ps2_pad.cpp && ok || bad "bak holds the current file"
sed 's/$/\r/' console/ps2_pad.cpp > crlf.cpp
run "${PM[@]}" install --repo PS2Recomp --from crlf.cpp --backups console/backups
eq "$(line 2)" "$TGT is already this mapping; nothing to do" "CRLF copy reads the same (text mode)"
run "${PM[@]}" install --repo PS2Recomp --from console/ps2_pad.cpp --backups b2
[[ "$(line 1)" == "backed up the current file to b2/ps2_pad.cpp."*".bak" ]] && ok || bad "installed file is never the stock backup"
[ -e b2/ps2_pad.cpp.orig ] && bad "no orig from a generated file" || ok
run "${PM[@]}" gen --out sw.cpp --layout switch
run "${PM[@]}" install --repo PS2Recomp --from sw.cpp --backups console/backups
eq "$(line 2)" "installed sw.cpp -> $TGT" "install a new mapping"
cmp -s $TGT sw.cpp && ok || bad "new mapping installed"
run "${PM[@]}" restore --repo PS2Recomp --backups console/backups
eq "$CODE:$OUT" "0:restored the stock ps2_pad.cpp from console/backups/ps2_pad.cpp.orig" "restore"
cmp -s $TGT stock.cpp && ok || bad "stock restored"
printf 'bool PSPadBackend::readState(int, int, uint8_t *, size_t) { return true; }\n' > foreign.cpp
run "${PM[@]}" install --repo PS2Recomp --from foreign.cpp --backups b
eq "$CODE:$ERR" "2:error: foreign.cpp was not written by padmap.py gen; refusing to install it" "foreign file"
[ -e b ] && bad "nothing created on refusal" || ok
run "${PM[@]}" install --repo nowhere --from foreign.cpp
eq "$CODE:$ERR" "2:error: nowhere is not a PS2Recomp checkout (no ps2xRuntime/src/lib/ps2_pad.cpp)" "not a checkout"
run "${PM[@]}" install --repo PS2Recomp --from missing.cpp
eq "$CODE:$ERR" "2:error: no such file: missing.cpp (run padmap.py gen first)" "missing generated file"
run "${PM[@]}" restore --repo PS2Recomp --backups empty
eq "$CODE:$ERR" "2:error: no backup at empty/ps2_pad.cpp.orig" "no backup"
mkdir -p R2/ps2xRuntime/src/lib; echo "// changed" > R2/ps2xRuntime/src/lib/ps2_pad.cpp
run "${PM[@]}" install --repo R2 --from console/ps2_pad.cpp
eq "$CODE:$ERR" "2:error: R2/ps2xRuntime/src/lib/ps2_pad.cpp does not define PSPadBackend::readState; this checkout's pad layer changed, read it before replacing it" "changed pad layer"
run "${PM[@]}" install --from console/ps2_pad.cpp
eq "$CODE" 2 "--repo is required"
cd .. || exit 1

# docs and help
for sub in init check gen install restore presets table; do
    run "${PM[@]}" $sub --help
    eq "$CODE" 0 "$sub --help"
done
run "${PM[@]}" gen --help
has "$OUT" "output file (default console/ps2_pad.cpp, - for
                        stdout)" "gen help (wrapped at 78)"
run "${PM[@]}" install --help
has "$OUT" "--from" "install help"
head -c 30 "$LIBSKILL/SKILL.md" | grep -q '^---' && ok || bad "skill doc front matter"
grep -q "^name: console-controller" "$LIBSKILL/SKILL.md" && ok || bad "skill doc name"

# ================================================================ Compile and behaviour (generated C++ against a mock raylib)
CXX=$(command -v c++ || command -v g++ || command -v clang++)
if [ -z "$CXX" ]; then
    echo "  (no C++ compiler: compile and behaviour checks skipped)"
else
RB=(LEFT_FACE_UP LEFT_FACE_RIGHT LEFT_FACE_DOWN LEFT_FACE_LEFT RIGHT_FACE_UP RIGHT_FACE_RIGHT RIGHT_FACE_DOWN RIGHT_FACE_LEFT
    LEFT_TRIGGER_1 LEFT_TRIGGER_2 RIGHT_TRIGGER_1 RIGHT_TRIGGER_2 MIDDLE_LEFT MIDDLE MIDDLE_RIGHT LEFT_THUMB RIGHT_THUMB)
RK=(APOSTROPHE COMMA MINUS PERIOD SLASH ZERO ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NINE SEMICOLON EQUAL
    A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
    LEFT_BRACKET BACKSLASH RIGHT_BRACKET GRAVE SPACE ESCAPE ENTER TAB BACKSPACE INSERT DELETE RIGHT LEFT DOWN UP PAGE_UP
    PAGE_DOWN HOME END CAPS_LOCK SCROLL_LOCK NUM_LOCK PRINT_SCREEN PAUSE F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
    LEFT_SHIFT LEFT_CONTROL LEFT_ALT LEFT_SUPER RIGHT_SHIFT RIGHT_CONTROL RIGHT_ALT RIGHT_SUPER KB_MENU
    KP_0 KP_1 KP_2 KP_3 KP_4 KP_5 KP_6 KP_7 KP_8 KP_9 KP_DECIMAL KP_DIVIDE KP_MULTIPLY KP_SUBTRACT KP_ADD KP_ENTER
    KP_EQUAL BACK MENU VOLUME_UP VOLUME_DOWN)
btn() { local i; for i in "${!RB[@]}"; do [ "${RB[$i]}" = "$1" ] && echo $((i + 1)); done; }
key() { local i; for i in "${!RK[@]}"; do [ "${RK[$i]}" = "$1" ] && echo $((i + 1)); done; }
INC=$TMP/inc; mkdir -p $INC/runtime
{
    echo "#pragma once"
    echo "typedef struct Vector2 { float x; float y; } Vector2;"
    b="GAMEPAD_BUTTON_UNKNOWN = 0"; for i in "${!RB[@]}"; do b+=", GAMEPAD_BUTTON_${RB[$i]} = $((i + 1))"; done
    echo "typedef enum { $b } GamepadButton;"
    echo "typedef enum { GAMEPAD_AXIS_LEFT_X = 0, GAMEPAD_AXIS_LEFT_Y = 1, GAMEPAD_AXIS_RIGHT_X = 2, GAMEPAD_AXIS_RIGHT_Y = 3, GAMEPAD_AXIS_LEFT_TRIGGER = 4, GAMEPAD_AXIS_RIGHT_TRIGGER = 5 } GamepadAxis;"
    k="KEY_NULL = 0"; for i in "${!RK[@]}"; do k+=", KEY_${RK[$i]} = $((i + 1))"; done
    echo "typedef enum { $k } KeyboardKey;"
    printf '%s\n' "bool IsGamepadAvailable(int gamepad);" "bool IsGamepadButtonDown(int gamepad, int button);" \
        "float GetGamepadAxisMovement(int gamepad, int axis);" "bool IsKeyDown(int key);" "int GetTouchPointCount(void);" \
        "Vector2 GetTouchPosition(int index);" "int GetScreenWidth(void);" "int GetScreenHeight(void);"
} > $INC/raylib.h
printf '#pragma once\n\n#include "raylib.h"\n' > $INC/ps2_host_backend.h
cat > $INC/runtime/ps2_pad.h <<'EOF'
#ifndef PS2_PAD_H
#define PS2_PAD_H

#include <cstddef>
#include <cstdint>

class PSPadBackend
{
public:
    PSPadBackend() = default;
    ~PSPadBackend() = default;

    bool readState(int port, int slot, uint8_t *data, size_t size);
};

#endif
EOF
cat > harness.cpp <<'EOF'
#include "runtime/ps2_pad.h"
#include "raylib.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

static bool g_avail[4];
static bool g_btn[4][32];
static float g_axis[4][8];
static bool g_key[1024];
static int g_touchCount = 0;
static Vector2 g_touch[8];

bool IsGamepadAvailable(int p) { return p >= 0 && p < 4 && g_avail[p]; }
bool IsGamepadButtonDown(int p, int b) { return p >= 0 && p < 4 && b >= 0 && b < 32 && g_btn[p][b]; }
float GetGamepadAxisMovement(int p, int a) { return (p >= 0 && p < 4 && a >= 0 && a < 8) ? g_axis[p][a] : 0.0f; }
bool IsKeyDown(int k) { return k >= 0 && k < 1024 && g_key[k]; }
int GetTouchPointCount(void) { return g_touchCount; }
Vector2 GetTouchPosition(int i) { return (i >= 0 && i < 8) ? g_touch[i] : Vector2{0.0f, 0.0f}; }
int GetScreenWidth(void) { return 1000; }
int GetScreenHeight(void) { return 500; }

int main()
{
    PSPadBackend pad;
    std::string cmd;
    while (std::cin >> cmd)
    {
        if (cmd == "pad") { int p, v; std::cin >> p >> v; g_avail[p] = v != 0; }
        else if (cmd == "btn") { int p, b, v; std::cin >> p >> b >> v; g_btn[p][b] = v != 0; }
        else if (cmd == "axis") { int p, a; float v; std::cin >> p >> a >> v; g_axis[p][a] = v; }
        else if (cmd == "key") { int k, v; std::cin >> k >> v; g_key[k] = v != 0; }
        else if (cmd == "touch") { float x, y; std::cin >> x >> y; g_touch[g_touchCount++] = Vector2{x, y}; }
        else if (cmd == "notouch") { g_touchCount = 0; }
        else if (cmd == "read")
        {
            int port; std::cin >> port;
            uint8_t data[32];
            std::memset(data, 0xEE, sizeof(data));
            bool ok = pad.readState(port, 0, data, sizeof(data));
            std::printf("%d", ok ? 1 : 0);
            for (int i = 0; i < 8; ++i) std::printf(" %02X", data[i]);
            std::printf("\n");
        }
        else if (cmd == "short")
        {
            uint8_t data[16];
            std::printf("%d\n", pad.readState(0, 0, data, sizeof(data)) ? 1 : 0);
        }
    }
    return 0;
}
EOF
# syn '<config json>' [gen flags] -- [defines]: generate and compile with -fsyntax-only
syn() {
    local cfg="$1" name="$2"; shift 2
    printf '%s' "$cfg" > cc.json
    run "${PM[@]}" gen --config cc.json --out "$name.cpp" "$@"
    eq "$CODE" 0 "gen $name"
}
syn '{}' plain
"$CXX" -std=c++20 -fsyntax-only -I $INC plain.cpp 2>cc.log && ok || bad "plain compiles: $(head -c 600 cc.log)"
syn '{"layout": "switch-labels", "trigger_threshold": 0.4, "players": 2, "stick_swap": true, "invert_y": "both", "gamepad": 1, "touch": [{"button": "CROSS", "x": 0.9, "y": 0.8, "r": 0.06}, {"stick": "right", "x": 0.8, "y": 0.4, "r": 0.1}]}' every --replay
"$CXX" -std=c++20 -fsyntax-only -I $INC -DPS2X_PAD_TOUCH every.cpp 2>cc.log && ok || bad "everything compiles: $(head -c 600 cc.log)"
syn '{"touch_on_desktop": true, "touch": [{"button": "CROSS", "x": 0.9, "y": 0.8, "r": 0.06}], "layout": "keyboard"}' desk
"$CXX" -std=c++20 -fsyntax-only -I $INC desk.cpp 2>cc.log && ok || bad "touch_on_desktop compiles without -D: $(head -c 600 cc.log)"
syn '{"keyboard": {"CROSS": [], "LSTICK_UP": [], "LSTICK_DOWN": [], "LSTICK_LEFT": [], "LSTICK_RIGHT": [], "RSTICK_UP": [], "RSTICK_DOWN": [], "RSTICK_LEFT": [], "RSTICK_RIGHT": []}}' sparse
"$CXX" -std=c++20 -fsyntax-only -I $INC sparse.cpp 2>cc.log && ok || bad "sentinel tables compile: $(head -c 600 cc.log)"
printf '%s\n' '#include "runtime/ps2_pad.h"' '#include "ps2_host_backend.h"' '#include <cstring>' \
    'bool PSPadBackend::readState(int, int, uint8_t *data, size_t size)' '{' '    if (!data || size < 32) return false;' \
    '    if (IsGamepadAvailable(0) && IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) data[2] = 0;' \
    '    data[6] = static_cast<uint8_t>(128 + GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) * 127);' \
    '    if (IsKeyDown(KEY_X) || IsKeyDown(KEY_KP_0)) data[3] = 0;' '    return true;' '}' > stock.cpp
"$CXX" -std=c++20 -fsyntax-only -I $INC stock.cpp 2>cc.log && ok || bad "stock file shape compiles with the mocks"

build() {   # build <name> '<config>' [gen flags] [-- defines]
    local name="$1" cfg="$2"; shift 2
    local gflags=() defs=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do gflags+=("$1"); shift; done
    [ "$1" = "--" ] && shift
    for d in "$@"; do defs+=("-D$d"); done
    printf '%s' "$cfg" > b.json
    run "${PM[@]}" gen --config b.json --out "$name.cpp" "${gflags[@]}"
    "$CXX" -std=c++20 -O0 -I $INC "${defs[@]}" "$name.cpp" harness.cpp -o "$name" 2>cc.log || { bad "build $name: $(head -c 600 cc.log)"; return 1; }
}
# pad <exe> [ENV=value...] -- commands...   (ROWS = stdout, PERR = stderr)
pad() {
    local exe="$1"; shift
    local envs=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    ROWS=$(printf '%s\n' "$@" | env -u PS2X_INPUT_SCRIPT -u PS2X_INPUT_RECORD "${envs[@]}" "./$exe" 2>pad_err); PERR=$(cat pad_err)
}
row() { echo "$ROWS" | sed -n "$1p" | cut -d' ' -f"$2"; }
if build pad_default '{}' --replay && build pad_custom '{"players": 2, "invert_y": true, "stick_swap": true, "trigger_threshold": 0.4, "deadzone": 0.0, "keyboard_always": false, "touch": [{"button": "CROSS", "x": 0.9, "y": 0.8, "r": 0.06}, {"stick": "left", "x": 0.2, "y": 0.5, "r": 0.1}]}' -- PS2X_PAD_TOUCH; then
    pad pad_default -- "read 0" "read 1" "short"
    eq "$(row 1 1-9)" "1 01 73 FF FF 80 80 80 80" "idle report"
    eq "$(row 2 1-9)" "1 01 73 FF FF 80 80 80 80" "idle port 1"
    eq "$(row 3 1)" "0" "buffers under 32 bytes are refused"
    pad pad_default -- "pad 0 1" "btn 0 $(btn RIGHT_FACE_DOWN) 1" "read 0" "btn 0 $(btn RIGHT_FACE_DOWN) 0" "btn 0 $(btn LEFT_FACE_UP) 1" "btn 0 $(btn MIDDLE_RIGHT) 1" "read 0"
    eq "$(row 1 4-5)" "FF BF" "CROSS 0x4000 cleared"
    eq "$(row 2 4-5)" "E7 FF" "UP 0x10 and START 0x08 cleared"
    pad pad_default -- "pad 0 1" "axis 0 0 1.0" "read 0" "axis 0 0 0.0" "axis 0 1 -1.0" "read 0" "axis 0 0 0.1" "axis 0 1 0.0" "read 0" \
        "axis 0 0 0.5" "axis 0 1 0.5" "read 0" "axis 0 0 0" "axis 0 2 -1.0" "read 0"
    eq "$(row 1 8-9)" "FF 80" "LX full right in data[6]"
    eq "$(row 2 8-9)" "80 00" "LY full up in data[7]"
    eq "$(row 3 8-9)" "80 80" "inside the radial deadzone"
    eq "$(row 4 8-9)" "BB BB" "0.707 radius rescaled: 187"
    eq "$(row 5 6)" "00" "RX full left in data[4]"
    pad pad_default -- "key $(key X) 1" "key $(key W) 1" "read 0" "read 1" "pad 0 1" "read 0"
    eq "$(row 1 4-5)" "FF BF" "keyboard X is CROSS"
    eq "$(row 1 9)" "00" "W pushes LY up"
    eq "$(row 2 4-9)" "FF FF 80 80 80 80" "port 1 untouched"
    eq "$(row 3 4-5)" "FF BF" "keyboard_always: still works with a pad"
    pad pad_default -- "pad 2 1" "btn 2 $(btn RIGHT_FACE_UP) 1" "read 0"
    eq "$(row 1 5)" "EF" "TRIANGLE from the first available pad (index 2)"
    pad pad_custom -- "pad 0 1" "pad 1 1" "btn 1 $(btn RIGHT_FACE_RIGHT) 1" "read 1" "axis 0 4 -1.0" "read 0" "axis 0 4 0.0" "read 0" \
        "axis 0 4 -1.0" "axis 0 3 -1.0" "read 0" "key $(key X) 1" "read 0" "touch 900 400" "read 0" "notouch" "touch 230 250" "read 0"
    eq "$(row 1 4-5)" "FF DF" "player 2's CIRCLE"
    eq "$(row 2 4-5)" "FF FF" "trigger at rest"
    eq "$(row 3 4-5)" "FF FE" "L2 0x0100 via the axis"
    eq "$(row 4 8-9)" "80 00" "swapped onto the left stick, not inverted"
    eq "$(row 5 4-5)" "FF FF" "keyboard off with a pad"
    eq "$(row 6 4-5)" "FF BF" "touch zone at (0.9, 0.8) is CROSS"
    eq "$(row 7 8)" "CC" "touch stick 0.6 right: round(1.6*127.5) = 204"
    printf '# test\n0 -\n2 START\n3 CROSS 255 128 128 0\n5 -\n' > walk.txt
    pad pad_default PS2X_INPUT_SCRIPT=walk.txt -- "pad 0 1" "btn 0 $(btn RIGHT_FACE_UP) 1" "read 0" "read 0" "read 0" "read 0" "read 0" "read 0" "read 1"
    has "$PERR" "[pad-replay] loaded 4 lines from walk.txt" "replay loaded"
    has "$PERR" "[pad-replay] script finished at frame 5" "replay finished"
    eq "$(row 1 4-5)" "FF FF" "live TRIANGLE ignored while replaying"
    eq "$(row 3 4-5)" "F7 FF" "frame 2: START"
    eq "$(row 4 4-9)" "FF BF 80 00 FF 80" "frame 3: CROSS and sticks"
    eq "$(row 6 4-5)" "FF FF" "frame 5: released"
    eq "$(row 7 4-5)" "FF FF" "port 1 is not scripted"
    printf '5 START\n1 -\n' > badw.txt
    pad pad_default PS2X_INPUT_SCRIPT=badw.txt -- "pad 0 1" "btn 0 $(btn RIGHT_FACE_UP) 1" "read 0"
    has "$PERR" "replay disabled" "bad script disables replay"
    eq "$(row 1 5)" "EF" "falls back to live input"
    pad pad_default PS2X_INPUT_RECORD=rec.txt -- "pad 0 1" "read 0" "btn 0 $(btn RIGHT_FACE_DOWN) 1" "read 0" "read 0" "btn 0 $(btn RIGHT_FACE_DOWN) 0" "axis 0 0 1.0" "read 0"
    REC_ROWS="$ROWS"
    has "$PERR" "[pad-record] writing rec.txt" "record announced"
    eq "$(cat rec.txt)" "# recorded by ps2_pad.cpp: frame buttons lx ly rx ry (one line per change)
0 - 128 128 128 128
1 CROSS 128 128 128 128
3 - 255 128 128 128" "recording, one line per change"
    run "${PM[@]}" check --script rec.txt --json
    eq "$(j .script)" '{"path":"rec.txt","lines":3,"last_frame":3}' "recording is a valid script"
    pad pad_default PS2X_INPUT_SCRIPT=rec.txt -- "read 0" "read 0" "read 0" "read 0"
    eq "$(echo "$ROWS" | cut -d' ' -f2-)" "$(echo "$REC_ROWS" | cut -d' ' -f2-)" "record then replay round trip"
fi
fi

done_ "console-controller/padmap"

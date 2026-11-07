#!/usr/bin/env bash
# modder/win: `um win` off Windows, with stand-in powershell.exe / taskkill.exe / cmd.exe / reg.exe on PATH and
# UM_FAKE_WSL=1 (a test switch that makes the port take its WSL paths). Mirrors upstream tests/test_win_launch.py and
# test_win_capture_escape.py plus the process list, the exact-PID kill and the registry read.
source "$(dirname "$0")/lib.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export UM_HOME="$TMP/umh" HOME="$TMP/home"; mkdir -p "$HOME" bin
cat > bin/powershell.exe <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >> "$CALLS"
case "$*" in
  *"Get-Process -Name 'Game'"*) echo 4242;;
  *"Get-Process -Name 'Gam*'"*) echo '{"Id":4242,"ProcessName":"Game","MainWindowTitle":"Ünïcode Game","Hwnd":1234}';;
  *Get-Process*) echo '[{"Id":4242,"ProcessName":"Game","MainWindowTitle":"Ünïcode Game","Hwnd":1234},{"Id":7,"ProcessName":"explorer","MainWindowTitle":"","Hwnd":5}]';;
esac
EOF
cat > bin/taskkill.exe <<'EOF'
#!/bin/bash
printf 'taskkill %s\n' "$*" >> "$CALLS"; echo "SUCCESS: The process with PID $2 has been terminated."
EOF
cat > bin/cmd.exe <<'EOF'
#!/bin/bash
for a in "$@"; do printf '[%s]' "$a"; done >> "$CALLS"; echo >> "$CALLS"
EOF
cat > bin/reg.exe <<'EOF'
#!/bin/bash
printf 'reg %s\n' "$*" >> "$CALLS"
[ "$1" = query ] && printf '\r\nHKEY_CURRENT_USER\\Software\\Microsoft\\DirectX\\UserGpuPreferences\r\n    DirectXUserGlobalSettings    REG_SZ    AutoHDREnable=2097;\r\n'
exit 0
EOF
chmod +x bin/*
export CALLS="$TMP/calls.log"; : > "$CALLS"
w() { OUT=$(PATH="$TMP/bin:$PATH" UM_FAKE_WSL=1 $T um win "$@" 2>"$TMP/err"); CODE=$?; ERR=$(cat "$TMP/err"); }

w ps
eq "$CODE" 0 "ps"
has "$OUT" "   4242  Game                         Ünïcode Game" "ps row (UTF-8 title)"
has "$OUT" "      7  explorer" "ps second row"
has "$(cat "$CALLS")" "-NoProfile -NonInteractive -Command try{[Console]::OutputEncoding=[Text.Encoding]::UTF8}catch{};Get-Process  -ErrorAction SilentlyContinue" "PowerShell asked for UTF-8"
w ps Gam
has "$OUT" "4242  Game" "ps filter (single object, not a list)"
has "$(cat "$CALLS")" "Get-Process -Name 'Gam*'" "ps name filter"
w kill 4242
has "$OUT" "SUCCESS: The process with PID 4242 has been terminated." "kill by PID"
has "$(cat "$CALLS")" "taskkill /PID 4242 /F" "taskkill arguments"
w kill Game
eq "$CODE" 2 "kill takes a PID only"
: > "$CALLS"
w launch --steam 105600
eq "$(cat "$CALLS")" "[/c][start][][steam://rungameid/105600]" "steam launch"
: > "$CALLS"
w launch 'C:\Games\Foo\foo.exe' -- -windowed
eq "$(cat "$CALLS")" '[/c][start][][/D][C:\Games\Foo][C:\Games\Foo\foo.exe][-windowed]' "exe launch: start in its folder"
w reg get 'HKCU\Software\Microsoft\DirectX\UserGpuPreferences'
has "$OUT" "AutoHDREnable=2097;" "reg get"
w drive --proc Game
eq "$CODE" 2 "drive needs commands"
w shot out.png
has "$ERR" "run \`um win setup\`" "shot without ffmpeg asks for setup"
done_ "modder/win"

#!/usr/bin/env bash
# Records one REAL astro CLI session with timing: recterm.sh <name> <astro args...>
# Output: rec/<name>.log + rec/<name>.tim (util-linux script format). Demo repo is reset first.
set -u
NAME="$1"; shift
D=/home/user/.cache/demo/todo-api; R=/home/user/tools/term-demo/rec; mkdir -p "$R"
cd "$D" && git reset -q --hard && git clean -qfd && git worktree prune
for wt in $(git worktree list --porcelain | awk '/^worktree/{print $2}' | tail -n +2); do git worktree remove --force "$wt"; done
rm -rf ../todo-api.agent-* 2>/dev/null
ARGS=$(printf '%q ' "$@")
ASTRO_URL=http://127.0.0.1:4187 script -q -T "$R/$NAME.tim" -c "stty cols 96 rows 30; /home/user/astro $ARGS" "$R/$NAME.log" >/dev/null 2>&1
sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$R/$NAME.log" | tr '\r' '\n' | grep -v '^\s*$' | uniq | sed -n '2,200p' | grep -v '^Script done'

#!/usr/bin/env bash
# API checks for the Workspace intelligence layer: repo index, git pipeline, profiler stats, assembly, TypeScript, vendor CORS.
# Usage: tests/intel_api_test.sh [port]   (server must run with ASTRO_AUTH=off)
set -u
P="${1:-4189}"; H=(-H 'Content-Type: application/json' -H 'x-astro-client: 1')
fail=0; ok(){ echo "PASS $1"; }; bad(){ echo "FAIL $1: $2"; fail=1; }
api(){ curl -s -m 90 -X POST "localhost:$P/api/$1" "${H[@]}" -d "$2"; }
PR="intel-t$$"
api fs/project/create "{\"project\":\"$PR\"}" >/dev/null
w(){ node -e 'process.stdout.write(JSON.stringify({project:process.argv[1],path:process.argv[2],content:require("fs").readFileSync(0,"utf8")}))' "$PR" "$1" | curl -s -X POST "localhost:$P/api/fs/write" "${H[@]}" --data-binary @- >/dev/null; }
printf '#include "util.h"\n#include <stdio.h>\nint square(int x) { return x * x; }\nint main(void) {\n  long s = 0;\n  for (int i = 0; i < 3000000; i++) s += square(i %% 100);\n  printf("sum=%%ld\\n", s);\n  return 0;\n}\n' | w main.c
printf '#pragma once\nstruct Vec3 { float x, y, z; };\n#define CLAMP(v) (v)\n' | w util.h
printf 'import * as THREE from "three";\nimport { helper } from "./lib/helper.js";\nexport function boot() { return helper(); }\nexport const tick = (dt) => dt * 2;\nclass Scene {}\n' | w app.js
printf 'export function helper() { return 42; }\n' | w lib/helper.js
printf '<!doctype html><title>HUD</title><script type="importmap">{"imports":{"three":"/vendor/three.module.min.js"}}</script><script type="module" src="app.js"></script><link rel="stylesheet" href="style.css"><canvas id="gl"></canvas>\n' | w index.html
printf ':root { --neon: #0ff; }\n@keyframes pulse { to { opacity: .5 } }\n' | w style.css
printf 'interface Row { id: number; name: string }\nconst rows: Row[] = [{ id: 1, name: "a" }, { id: 2, name: "b" }];\nfunction total(r: Row[]): number { return r.reduce((s, x) => s + x.id, 0); }\nconsole.log("total=" + total(rows));\n' | w cli.ts
printf 'import os\nfrom lib import thing\nclass Model:\n    def run(self):\n        return 1\ndef main():\n    print("ok")\n@app.route("/api")\ndef api(): pass\n' | w tool.py

IDX=$(api ide/index "{\"project\":\"$PR\"}")
echo "$IDX" | node -e '
const j=JSON.parse(require("fs").readFileSync(0,"utf8")); const f=(p)=>j.files.find(x=>x.path===p)||{};
const names=(p)=>(f(p).symbols||[]).map(s=>s.k+":"+s.n).join(",");
const checks=[["main.c fns", /function:square/.test(names("main.c")) && /function:main/.test(names("main.c"))],["main.c include util.h",(f("main.c").deps||[]).includes("util.h")],
 ["util.h struct+macro",/class:Vec3/.test(names("util.h"))&&/macro:CLAMP/.test(names("util.h"))],["app.js symbols",/function:boot/.test(names("app.js"))&&/function:tick/.test(names("app.js"))&&/class:Scene/.test(names("app.js"))],
 ["app.js deps",JSON.stringify(f("app.js").deps)===JSON.stringify(["three","./lib/helper.js"])],["html deps",(f("index.html").deps||[]).includes("app.js")&&(f("index.html").deps||[]).includes("style.css")],
 ["css vars",/var:--neon/.test(names("style.css"))&&/keyframes:pulse/.test(names("style.css"))],["ts types",/type:Row/.test(names("cli.ts"))&&/function:total/.test(names("cli.ts"))],
 ["py",/class:Model/.test(names("tool.py"))&&/method:run/.test(names("tool.py"))&&/function:main/.test(names("tool.py"))&&/route:ROUTE \/api/.test(names("tool.py"))&&(f("tool.py").deps||[]).includes("lib")],
 ["totals",j.lines>20&&j.symbols>15&&j.languages.javascript>0]];
for(const [n,c] of checks) console.log((c?"PASS ":"FAIL ")+"index "+n);' | tee /tmp/intel_idx.txt
grep -q FAIL /tmp/intel_idx.txt && fail=1

# git
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"status\"}"); [[ "$S" == *'"repo":false'* ]] && ok "git status (no repo)" || bad "git status no repo" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"commit\",\"message\":\"x\"}"); [[ "$S" == *"not a git repository"* ]] && ok "commit refused before init" || bad "commit before init" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"init\"}"); [[ "$S" == *'"ok":true'* ]] && ok "git init" || bad "git init" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"status\"}"); [[ "$S" == *'"repo":true'* && "$S" == *'main.c'* && "$S" == *'.gitignore'* ]] && ok "status lists untracked" || bad "status" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"diff\"}"); [[ "$S" == *'+int square'* ]] && ok "diff (no HEAD, intent-to-add)" || bad "diff" "${S:0:200}"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"show\",\"path\":\"main.c\"}"); [[ "$S" == *'"missing":true'* ]] && ok "show before first commit" || bad "show" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"commit\",\"message\":\"feat(core): initial HUD scaffold\\n\\n- add C kernel\"}"); [[ "$S" == *'"hash"'* ]] && ok "commit" || bad "commit" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"commit\",\"message\":\"again\"}"); [[ "$S" == *"Nothing to commit"* ]] && ok "clean tree refuses commit" || bad "clean commit" "$S"
printf 'export function helper() { return 43; }\n' | w lib/helper.js
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"status\"}"); [[ "$S" == *'"path":"lib/helper.js","x":" ","y":"M"'* || "$S" == *'lib/helper.js'* ]] && ok "status shows modified" || bad "status modified" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"show\",\"path\":\"lib/helper.js\"}"); [[ "$S" == *'return 42'* ]] && ok "show HEAD:path" || bad "show HEAD" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"diff\",\"stat\":true}"); [[ "$S" == *'helper.js'* && "$S" == *'1 file changed'* ]] && ok "diff --stat" || bad "diff stat" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"checkout\",\"branch\":\"feat/hud\",\"create\":true}"); [[ "$S" == *'"ok":true'* ]] && ok "create branch" || bad "branch" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"commit\",\"message\":\"fix(lib): bump helper\"}"); [[ "$S" == *'"hash"'* ]] && ok "commit on branch" || bad "commit branch" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"branches\"}"); [[ "$S" == *'"current":"feat/hud"'* && "$S" == *'"main"'* ]] && ok "branches" || bad "branches" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"log\"}"); [[ "$S" == *'fix(lib): bump helper'* && "$S" == *'feat(core): initial HUD scaffold'* && "$S" == *'"stat"'* ]] && ok "log with stats" || bad "log" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"checkout\",\"branch\":\"main\"}"); [[ "$S" == *'"ok":true'* ]] && ok "switch branch" || bad "switch" "$S"
grep -q "return 42" "$(ls -d .cache/wst2/projects/$PR 2>/dev/null || find / -maxdepth 6 -type d -name "$PR" 2>/dev/null | head -1)/lib/helper.js" && ok "checkout restored files" || bad "checkout files" "helper not reverted"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"checkout\",\"branch\":\"--evil\"}"); [[ "$S" == *'Invalid branch'* ]] && ok "rejects option-like branch" || bad "branch inject" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"show\",\"path\":\"../../etc/passwd\"}"); [[ "$S" == *'Invalid'* ]] && ok "rejects path escape" || bad "path escape" "$S"
S=$(api ide/git "{\"project\":\"$PR\",\"op\":\"show\",\"path\":\"main.c\",\"ref\":\"--output=/tmp/x\"}"); [[ "$S" == *'Invalid'* ]] && ok "rejects option-like ref" || bad "ref inject" "$S"

# runs: profiler stats, assembly, TypeScript, C# message
run(){ curl -s -m 120 -N -X POST "localhost:$P/api/run" "${H[@]}" -d "$1"; }
R=$(run "{\"project\":\"$PR\",\"file\":\"main.c\"}"); [[ "$R" == *'sum=9850500000'* && "$R" == *'"max_rss_kb"'* && "$R" == *'"user_ms"'* ]] && ok "C run + rusage stats" || bad "C stats" "${R: -400}"
echo "$R" | grep -o '"stats":{[^}]*}' | head -1
[[ "$R" != *'astro_measure'* ]] && ok "profiler wrapper hidden from command" || bad "wrapper shown" "$(echo "$R" | head -c 300)"
R=$(run "{\"project\":\"$PR\",\"file\":\"main.c\",\"mode\":\"asm\"}"); [[ "$R" == *'square'* && "$R" == *'main:'* && "$R" == *'imul'* || "$R" == *'main:'* ]] && ok "assembly listing" || bad "asm" "${R:0:400}"
[[ "$R" != *'.cfi_'* ]] && ok "asm noise filtered" || bad "asm noise" "cfi present"
R=$(run "{\"project\":\"$PR\",\"file\":\"tool.py\",\"mode\":\"asm\"}"); [[ "$R" == *'works for .c and .cpp'* ]] && ok "asm refuses non-C" || bad "asm py" "$R"
R=$(run "{\"project\":\"$PR\",\"file\":\"cli.ts\",\"timeout\":110}"); [[ "$R" == *'total=3'* ]] && ok "TypeScript run (tsx)" || bad "ts" "${R: -600}"
printf 'class P { static void Main() {} }\n' | w Prog.cs
R=$(run "{\"project\":\"$PR\",\"file\":\"Prog.cs\"}"); [[ "$R" == *'.NET SDK'* ]] && ok "C# explains missing SDK" || bad "cs" "$R"

# vendor three.js with CORS for sandboxed previews
Hh=$(curl -sI "localhost:$P/vendor/three.module.min.js"); [[ "$Hh" == *'200'* && "$Hh" == *'Access-Control-Allow-Origin: *'* && "$Hh" == *'text/javascript'* ]] && ok "three.js served with CORS" || bad "three" "$Hh"
Hh=$(curl -sI "localhost:$P/vendor/OrbitControls.js"); [[ "$Hh" == *'200'* ]] && ok "OrbitControls served" || bad "orbit" "$Hh"
api fs/project/delete "{\"project\":\"$PR\"}" >/dev/null
[ $fail = 0 ] && echo "ALL INTEL API CHECKS PASSED" || { echo "SOME CHECKS FAILED"; exit 1; }

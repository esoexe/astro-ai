#!/usr/bin/env bash
# API checks for the Workspace IDE features. Usage: tests/ide_api_test.sh [port]
P=${1:-4189}
H=(-H Content-Type:application/json -H x-astro-client:1)
api(){ curl -s -m 40 -X POST "localhost:$P/api/$1" "${H[@]}" -d "$2"; echo; }
curl -s "localhost:$P/api/fs/projects" -H x-astro-client:1 | head -c 100; echo
echo "== search"; api ide/search '{"project":"my-project","query":"hello"}' | head -c 300; echo
echo "== zip"; curl -s -m 30 -X POST "localhost:$P/api/ide/zip" "${H[@]}" -d '{"project":"my-project"}' -o /tmp/p.zip -w "%{http_code} %{size_download}B\n"; unzip -tq /tmp/p.zip
echo "== history"; api ide/history/create '{"project":"my-project","label":"First"}'
ID=$(api ide/history/list '{"project":"my-project"}' | node -pe 'JSON.parse(require("fs").readFileSync(0)).checkpoints[0].id')
api fs/write '{"project":"my-project","path":"main.cpp","content":"changed"}' >/dev/null; api fs/write '{"project":"my-project","path":"new.txt","content":"x"}' >/dev/null
echo "changes:"; api ide/history/changes "{\"project\":\"my-project\",\"id\":\"$ID\"}" | head -c 250; echo
echo "read old main.cpp:"; api ide/history/read "{\"project\":\"my-project\",\"id\":\"$ID\",\"path\":\"main.cpp\"}" | head -c 60; echo
echo "restore:"; api ide/history/restore "{\"project\":\"my-project\",\"id\":\"$ID\"}"
echo "tree after restore:"; api fs/tree '{"project":"my-project"}' | node -pe 'JSON.parse(require("fs").readFileSync(0)).files.map(f=>f.path).join(", ")'
echo "checkpoints:"; api ide/history/list '{"project":"my-project"}' | node -pe 'JSON.parse(require("fs").readFileSync(0)).checkpoints.map(c=>c.kind+":"+c.label+"("+c.files+")").join(" | ")'

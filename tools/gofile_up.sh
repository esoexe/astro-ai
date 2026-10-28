#!/usr/bin/env bash
# Uploads every file in a folder to one Gofile folder and prints the share link.
#   tools/gofile_up.sh <dir>
# The first upload creates a guest account + folder; later files reuse its token and folder id.
set -euo pipefail
DIR="${1:?usage: gofile_up.sh <dir>}"
TOKEN=""; FOLDER=""; LINK=""
for f in "$DIR"/*; do
  [ -f "$f" ] || continue
  for try in 1 2 3; do
    if [ -z "$TOKEN" ]; then
      R=$(curl -sS --max-time 900 -F "file=@$f" https://upload.gofile.io/uploadfile || true)
    else
      R=$(curl -sS --max-time 900 -H "Authorization: Bearer $TOKEN" -F "folderId=$FOLDER" -F "file=@$f" https://upload.gofile.io/uploadfile || true)
    fi
    OK=$(printf '%s' "$R" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{try{const j=JSON.parse(s);console.log(j.status==="ok"?"1":"0")}catch{console.log("0")}})')
    [ "$OK" = 1 ] && break
    echo "  retry $try for $(basename "$f")" >&2; sleep 5
  done
  [ "$OK" = 1 ] || { echo "upload failed: $(basename "$f")" >&2; exit 1; }
  if [ -z "$TOKEN" ]; then
    eval "$(printf '%s' "$R" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const d=JSON.parse(s).data;console.log(`TOKEN=${JSON.stringify(d.guestToken||"")} FOLDER=${JSON.stringify(d.parentFolder)} LINK=${JSON.stringify(d.downloadPage)}`)})')"
  fi
  echo "  uploaded $(basename "$f")" >&2
done
echo "$LINK"

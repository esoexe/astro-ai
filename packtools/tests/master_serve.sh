#!/usr/bin/env bash
# master-build/serve: the ServeTest cases of master-skills/skills/build/tools/test_build.py, plus the parts of Python's
# http.server that serve.py inherits (status lines, header order, error pages, request parsing, Range rules), against
# real servers on free local ports. Needs curl.
source "$(dirname "$0")/lib.sh"
SK="$(cd "$(dirname "$0")/../.." && pwd)/library/plugins/astro-master/skills/master-build"
TMP=$(mktemp -d)
SV=(--skill-dir "$SK" master-build/serve)
PIDS=()
trap 'kill ${PIDS[@]} 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
HERE=$(pwd -P)

# the ServeTest site, plus a few files for types, ranges and names
mkdir -p site/docs plain empty
printf '<h1>home</h1>' > site/index.html; printf '<h1>about</h1>' > site/about.html; printf '<h1>custom 404</h1>' > site/404.html
printf 'export {}' > site/app.mjs; printf 'wOF2' > site/font.woff2; printf 'docs' > site/docs/index.html; printf '0123456789' > site/video.mp4
printf 'x' > "site/a b.txt"; printf 't' > site/pack.tgz; printf 'b' > site/app.js.br; printf 'q' > site/blob.qqq; printf '{}' > site/DATA.JSON
printf 'g' > site/x.gz; printf '# hi' > site/notes.md; : > site/empty.txt
touch -d '2024-03-05 06:07:08 UTC' site/index.html
printf 'plain' > plain/index.html

# start NAME ARGS...: a server in the background; sets URL and PORT once it prints its banner
start() {
    local name=$1; shift
    $T "${SV[@]}" "$@" > "$name.out" 2> "$name.err" &
    PIDS+=($!); LAST_PID=$!
    for _ in $(seq 1 50); do grep -q "Ctrl+C" "$name.out" 2>/dev/null && break; sleep 0.1; done
    URL=$(grep -o 'http://127.0.0.1:[0-9]*/' "$name.out" | head -1); PORT=${URL##*:}; PORT=${PORT%/}
}
heads() { curl -s -o /dev/null -D - "$@" | tr -d '\r'; }
status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
raw() { exec 3<>"/dev/tcp/127.0.0.1/$1" || return; printf '%s' "$2" >&3; timeout 3 cat <&3 | tr -d '\r'; exec 3<&-; }
CRLF=$'\r\n'
mask() { sed -E 's/^Date: .*/Date: X/; s/^Last-Modified: .*/Last-Modified: X/'; }

start p site --port 0; P_URL=$URL; P_PORT=$PORT; P_PID=$LAST_PID
start f site --port 0 --spa --clean-urls; F_URL=$URL
start q plain --port 0; Q_URL=$URL; Q_PORT=$PORT
start v plain --port 0 --verbose; V_URL=$URL

# ---- upstream ServeTest
eq "$(status "$P_URL") $(curl -s "$P_URL")" "200 <h1>home</h1>" "basics: index.html at /"
has "$(heads "$P_URL")" "Cache-Control: no-store" "no-cache header"
has "$(heads "${P_URL}app.mjs")" "Content-Type: text/javascript" ".mjs is JavaScript"
has "$(heads "${P_URL}font.woff2")" "Content-Type: font/woff2" ".woff2"
eq "$(curl -sL "${P_URL}docs")" "docs" "the redirect to docs/ is followed"
eq "$(status "${P_URL}nope") $(curl -s "${P_URL}nope")" "404 <h1>custom 404</h1>" "host-style 404"
eq "$(curl -s "${F_URL}about")" "<h1>about</h1>" "clean URL"
eq "$(curl -s "${F_URL}app/route")" "<h1>home</h1>" "SPA fallback"
eq "$(status "${F_URL}missing.png")" "404" "assets still 404 with --spa"
eq "$(status -H 'Range: bytes=2-5' "${P_URL}video.mp4") $(curl -s -H 'Range: bytes=2-5' "${P_URL}video.mp4")" "206 2345" "a byte range"
has "$(heads -H 'Range: bytes=2-5' "${P_URL}video.mp4")" "Content-Range: bytes 2-5/10" "Content-Range"

# ---- banners
eq "$(cat p.out)" "$(printf 'Serving %s/site\n  Local:  %s\nLogging 404s and errors. Ctrl+C to stop.' "$HERE" "$P_URL")" "the banner"
eq "$(sed -n 3p f.out)" "  Mode:   spa fallback, clean urls" "modes"

# ---- responses: status line and header order as http.server sends them
eq "$(heads "$P_URL" | mask)" "$(printf 'HTTP/1.0 200 OK\nServer: build-serve/1.0.0\nDate: X\nContent-Type: text/html\nContent-Length: 13\nAccept-Ranges: bytes\nLast-Modified: X\nCache-Control: no-store')" "200 headers in order"
has "$(heads "$P_URL")" "Last-Modified: Tue, 05 Mar 2024 06:07:08 GMT" "Last-Modified is the file's mtime in RFC 1123 form"
[[ "$(heads "$P_URL" | grep '^Date:')" =~ ^Date:\ (Mon|Tue|Wed|Thu|Fri|Sat|Sun),\ [0-9]{2}\ [A-Z][a-z]{2}\ [0-9]{4}\ [0-9]{2}:[0-9]{2}:[0-9]{2}\ GMT$ ]] && ok || bad "Date header format"
eq "$(heads "${P_URL}docs?x=1" | mask)" "$(printf 'HTTP/1.0 301 Moved Permanently\nServer: build-serve/1.0.0\nDate: X\nLocation: /docs/?x=1\nContent-Length: 0\nCache-Control: no-store')" "a folder without / redirects, keeping the query"
has "$(heads "${P_URL}nope")" "HTTP/1.0 404 Not Found" "the custom 404 page has the plain status line"
BODY404=$(cat <<'EOF'
<!DOCTYPE HTML>
<html lang="en">
    <head>
        <meta charset="utf-8">
        <title>Error response</title>
    </head>
    <body>
        <h1>Error response</h1>
        <p>Error code: 404</p>
        <p>Message: File not found.</p>
        <p>Error code explanation: 404 - Nothing matches the given URI.</p>
    </body>
</html>
EOF
)
eq "$(curl -s "${Q_URL}nope")" "$BODY404" "no 404.html: http.server's error page"
eq "$(heads "${Q_URL}nope" | mask)" "$(printf 'HTTP/1.0 404 File not found\nServer: build-serve/1.0.0\nDate: X\nConnection: close\nContent-Type: text/html;charset=utf-8\nContent-Length: %d\nCache-Control: no-store' $(( ${#BODY404} + 1 )))" "error page headers"
H=$(raw "$P_PORT" "HEAD / HTTP/1.0$CRLF$CRLF")
has "$H" "Content-Length: 13" "HEAD: the length of the body"
hasnt "$H" "<h1>" "HEAD: no body"
H=$(raw "$Q_PORT" "HEAD /nope HTTP/1.0$CRLF$CRLF")
has "$H" "HTTP/1.0 404 File not found" "HEAD of a missing file"
hasnt "$H" "DOCTYPE" "HEAD: no error page body"

# ---- ranges (serve.parse_range)
rng() { echo "$(status -H "Range: $1" "${P_URL}${2:-video.mp4}") $(curl -s -H "Range: $1" "${P_URL}${2:-video.mp4}")"; }
eq "$(rng 'bytes=-3')" "206 789" "a suffix range"
has "$(heads -H 'Range: bytes=-3' "${P_URL}video.mp4")" "Content-Range: bytes 7-9/10" "suffix Content-Range"
eq "$(rng 'bytes=5-')" "206 56789" "an open range"
eq "$(rng 'bytes=3-100')" "206 3456789" "the end is clamped to the file"
has "$(heads -H 'Range: bytes=3-100' "${P_URL}video.mp4")" "Content-Range: bytes 3-9/10" "clamped Content-Range"
eq "$(rng 'BYTES= 2 - 4')" "206 234" "case and spaces: int() strips"
eq "$(rng 'bytes=1_0-')" "416 " "int() reads 1_0 as 10, past the end"
eq "$(rng 'bytes=20-')" "416 " "unsatisfiable"
eq "$(heads -H 'Range: bytes=20-' "${P_URL}video.mp4" | mask)" "$(printf 'HTTP/1.0 416 Range Not Satisfiable\nServer: build-serve/1.0.0\nDate: X\nContent-Range: bytes */10\nContent-Length: 0\nCache-Control: no-store')" "416 headers"
eq "$(rng 'bytes=-0')" "416 " "a zero suffix"
eq "$(rng 'bytes=0-1,4-5')" "200 0123456789" "multiple ranges: the whole file"
eq "$(rng 'bytes=x-2')" "200 0123456789" "a malformed range: the whole file"
eq "$(rng 'bytes=0-3' nope)" "404 <h1>custom 404</h1>" "no ranges on a 404 page"
eq "$(rng 'bytes=0-3' empty.txt)" "200 " "no ranges on an empty file"

# ---- paths
eq "$(curl -s "${P_URL}a%20b.txt")" "x" "percent-decoding"
eq "$(raw "$P_PORT" "GET /about.html?q=1 HTTP/1.0$CRLF$CRLF" | tail -1)" "<h1>about</h1>" "the query is ignored"
eq "$(raw "$P_PORT" "GET //about.html HTTP/1.0$CRLF$CRLF" | tail -1)" "<h1>about</h1>" "a leading // is reduced to /"
eq "$(raw "$P_PORT" "GET //docs HTTP/1.0$CRLF$CRLF" | grep Location)" "Location: /docs/" "so a redirect never points at another host"
eq "$(raw "$P_PORT" "GET /docs/../about.html HTTP/1.0$CRLF$CRLF" | tail -1)" "<h1>about</h1>" "normpath"
eq "$(raw "$P_PORT" "GET /../../../etc/passwd HTTP/1.0$CRLF$CRLF" | tail -1)" "<h1>custom 404</h1>" "no way out of the folder"
eq "$(raw "$P_PORT" "GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.0$CRLF$CRLF" | tail -1)" "<h1>custom 404</h1>" "not even percent-encoded"
eq "$(curl -s "${F_URL}about/") $(curl -s "${F_URL}x.html")" "<h1>about</h1> <h1>home</h1>" "clean URL with a slash; SPA for .html routes"

# ---- types: serve.py's table, then http.server's, then mimetypes (suffix and encoding rules)
ctype() { heads "${P_URL}$1" | sed -n 's/^Content-Type: //p'; }
eq "$(ctype pack.tgz)|$(ctype app.js.br)|$(ctype blob.qqq)|$(ctype DATA.JSON)|$(ctype x.gz)|$(ctype notes.md)|$(ctype video.mp4)" \
   "application/x-tar|text/javascript|application/octet-stream|application/json|application/gzip|text/markdown|video/mp4" "content types"

# ---- request parsing (BaseHTTPRequestHandler)
eq "$(raw "$P_PORT" "GET /$CRLF$CRLF")" "<h1>home</h1>" "HTTP/0.9: the body alone"
eq "$(raw "$P_PORT" "FOO / HTTP/1.0$CRLF$CRLF" | head -1)" "HTTP/1.0 501 Unsupported method ('FOO')" "unknown method"
eq "$(raw "$P_PORT" "get / HTTP/1.0$CRLF$CRLF" | head -1)" "HTTP/1.0 501 Unsupported method ('get')" "methods are case-sensitive"
has "$(raw "$Q_PORT" "POST / HTTP/1.0$CRLF$CRLF")" "<p>Error code explanation: 501 - Server does not support this operation.</p>" "the 501 page"
eq "$(raw "$P_PORT" "GET / HTTP/2.0$CRLF$CRLF")" "" "HTTP/2.0: serve.py's logger fails before replying"
eq "$(raw "$P_PORT" "GET / HTTP/1.x$CRLF$CRLF")" "" "a bad version: no reply"
eq "$(raw "$P_PORT" "GET$CRLF")" "" "one word: no reply"
MANY=""; for i in $(seq 1 101); do MANY+="X-$i: y$CRLF"; done
eq "$(raw "$P_PORT" "GET / HTTP/1.0$CRLF$MANY$CRLF" | head -1)" "HTTP/1.0 431 Too many headers" "more than 100 headers"
LONG=$(head -c 70000 /dev/zero | tr '\0' a)
has "$(raw "$P_PORT" "GET / HTTP/1.0${CRLF}X: $LONG$CRLF$CRLF")" "431 - got more than 65536 bytes when reading header line." "a header line over 64 KiB"

# ---- logging
sleep 0.2
E=$(cat p.err)
has "$E" "  404 GET /nope" "404s are logged"
has "$E" "  416 GET /video.mp4" "so are other errors"
has "$E" "  501 FOO /" "with the method"
hasnt "$E" "  200 GET" "successes are not"
has "$E" $'----------------------------------------\nException occurred during processing of request from (\'127.0.0.1\', ' "a failed request is reported"
has "$E" "AttributeError: 'BoundPreviewHandler' object has no attribute 'path'" "with the original's error"
curl -s -o /dev/null "${V_URL}"; curl -s -o /dev/null "${V_URL}nope"; sleep 0.2
E=$(cat v.err)
has "$E" "  200 GET /" "--verbose logs every request"
grep -qE '^127\.0\.0\.1 - - \[[0-9]{2}/[A-Z][a-z]{2}/[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}\] code 404, message File not found$' v.err && ok || bad "--verbose: http.server's log_message line"

# ---- stopping
kill -INT "$P_PID"; wait "$P_PID"; RC=$?
eq "$RC" 0 "Ctrl+C: exit 0"
eq "$(tail -2 p.out)" $'\nStopped.' "Ctrl+C: Stopped."

# ---- starting
start l empty --port 0 --lan
grep -qE '^  Phone:  (http://[0-9.]+:[0-9]+/   \(same Wi-Fi\)|no Wi-Fi address found)$' l.out && ok || bad "--lan prints a phone URL"
has "$(cat l.out)" "  Note:   no index.html in this folder, so / will 404" "a folder without index.html"
start b site --port "$Q_PORT"
[ -n "$PORT" ] && [ "$PORT" -gt "$Q_PORT" ] && [ "$PORT" -lt $((Q_PORT + 20)) ] && ok || bad "a busy port moves to the next free one ($Q_PORT -> $PORT)"
run "${SV[@]}" missing
eq "$CODE:$ERR" "2:serve: not a folder: missing" "not a folder"
run "${SV[@]}" site --port 70000
eq "$CODE:$ERR" "1:master-build/serve: OverflowError: getsockaddrarg: port must be 0-65535." "a port out of range"
run "${SV[@]}" site --host nonexistent.invalid
eq "$CODE" 2 "a host that does not resolve"
has "$ERR" "serve: could not bind nonexistent.invalid:8000 ([Errno " "could not bind"
run "${SV[@]}" --version
eq "$CODE:$OUT" "0:serve 1.0.0" "--version"
run "${SV[@]}" -h
has "$OUT" $'usage: serve [-h] [--port PORT] [--host HOST] [--lan] [--spa] [--clean-urls]\n             [--open] [--verbose] [--version]\n             [dir]' "usage"
has "$OUT" "Example: serve site/ --lan (then open the printed URL)" "the epilog, filled"
done_ "master-build/serve"

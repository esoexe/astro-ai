// Terminal + app server + proxy checks. Usage: node tests/ide_proc_test.js [port]
const P = process.argv[2] || 4189, B = `http://localhost:${P}`;
const H = { 'Content-Type': 'application/json', 'x-astro-client': '1' };
const api = async (p, b) => { const r = await fetch(`${B}/api/${p}`, { method: 'POST', headers: H, body: JSON.stringify(b) }); return r.json(); };
async function sse(p, b, ms, stopWhen) {
  const ctl = new AbortController(); setTimeout(() => ctl.abort(), ms);
  let text = ''; let exit = null;
  try {
    const r = await fetch(`${B}/api/${p}`, { method: 'POST', headers: H, body: JSON.stringify(b), signal: ctl.signal });
    const rd = r.body.getReader(); let buf = '';
    for (;;) { const { value, done } = await rd.read(); if (done) break; buf += Buffer.from(value).toString();
      let i; while ((i = buf.indexOf('\n\n')) >= 0) { const ev = buf.slice(0, i); buf = buf.slice(i + 2); const line = ev.split('\n').find((l) => l.startsWith('data:')); if (!line) continue;
        const j = JSON.parse(line.slice(5)); if (j.d) text += Buffer.from(j.d, 'base64').toString(); if ('exit' in j) exit = j.exit; }
      if (stopWhen && stopWhen(text)) { ctl.abort(); break; } }
  } catch (_e) { /* aborted */ }
  return { text, exit };
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
(async () => {
  const proj = 'my-project';
  // terminal
  const t = await api('ide/term/open', { project: proj, cols: 100, rows: 30 });
  console.log('term open:', t.id ? 'ok' : JSON.stringify(t));
  await sleep(500);
  await api('ide/term/input', { id: t.id, data: 'echo TERM-$((6*7)); python3 -c "print(input(\'name? \')[::-1])"\r' });
  await sleep(800);
  await api('ide/term/input', { id: t.id, data: 'abc\r' });
  const out = await sse('ide/term/stream', { id: t.id, from: 0 }, 5000, (s) => s.includes('cba'));
  console.log('term output has TERM-42:', out.text.includes('TERM-42'), '| interactive input reversed:', out.text.includes('cba'), '| prompt:', /astro.*\$/.test(out.text.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '')));
  await api('ide/term/input', { id: t.id, data: 'pwd\r' }); await sleep(400);
  const out2 = await sse('ide/term/stream', { id: t.id, from: 0 }, 1500);
  console.log('term cwd is project:', /projects\/my-project/.test(out2.text));
  await api('ide/term/close', { id: t.id });
  // node server honoring PORT
  await api('fs/write', { project: proj, path: 'server.js', content: `const http=require('http');http.createServer((q,s)=>{let b='';q.on('data',c=>b+=c);q.on('end',()=>{if(q.url.startsWith('/go')){s.writeHead(302,{Location:'/landed'});return s.end()}if(q.url.startsWith('/api')){s.writeHead(200,{'Content-Type':'application/json'});return s.end(JSON.stringify({method:q.method,url:q.url,body:b}))}s.writeHead(200,{'Content-Type':'text/html'});s.end('<!doctype html><html><head><title>N</title><link rel="stylesheet" href="/style.css"></head><body><img src="/logo.png"><h1>node ok</h1></body></html>')})}).listen(process.env.PORT,()=>console.log('listening on',process.env.PORT));` });
  let st = await api('ide/proc/start', { project: proj, file: 'server.js' });
  console.log('proc start:', st.running, st.url ? 'url ok' : JSON.stringify(st));
  await sleep(1500);
  st = await api('ide/proc/status', { project: proj });
  console.log('status running:', st.running, 'port:', st.port, 'listening:', st.listening);
  let r = await fetch(`${B}${st.url}`); let html = await r.text();
  console.log('proxy GET:', r.status, '| app html:', html.includes('node ok'), '| links rewritten:', html.includes(`href="${st.url}style.css"`) && html.includes(`src="${st.url}logo.png"`), '| shims:', html.includes('astroConsole') && html.includes('XMLHttpRequest.prototype.open'), '| csp:', (r.headers.get('content-security-policy') || '').startsWith('sandbox'));
  r = await fetch(`${B}${st.url}api/items?x=1&y=two`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"a":1}' });
  console.log('proxy POST with query:', r.status, await r.text());
  r = await fetch(`${B}${st.url}go`, { redirect: 'manual' });
  console.log('redirect rewritten:', r.status, r.headers.get('location') === `${st.url.slice(0, -1)}/landed`);
  const logs = await sse('ide/proc/logs', { project: proj, from: 0 }, 1500);
  console.log('server log:', JSON.stringify(logs.text.trim().slice(0, 120)));
  // python server with a hard-coded port (ignores PORT) -> detected
  await api('fs/write', { project: proj, path: 'app.py', content: 'import http.server, socketserver\nclass H(http.server.BaseHTTPRequestHandler):\n    def do_GET(self):\n        self.send_response(200); self.send_header("Content-Type","text/plain"); self.end_headers(); self.wfile.write(b"python ok " + self.path.encode())\nsocketserver.TCPServer.allow_reuse_address=True\nprint("serving on 8765", flush=True)\nsocketserver.TCPServer(("127.0.0.1", 8765), H).serve_forever()\n' });
  st = await api('ide/proc/start', { project: proj, file: 'app.py' });
  await sleep(2500);
  st = await api('ide/proc/status', { project: proj });
  r = await fetch(`${B}${st.url}hello?q=1`);
  console.log('python hard-coded port detected:', st.listening === 8765, '| proxy:', r.status, await r.text());
  await api('ide/proc/stop', { project: proj }); await sleep(500);
  st = await api('ide/proc/status', { project: proj });
  r = await fetch(`${B}/app/${st.url ? '' : 'x'}`);
  console.log('after stop running:', st.running, '| bad key ->', r.status);
  const k = (await (await fetch(`${B}/api/fs/projects`, { headers: H })).json()).preview_key;
  r = await fetch(`${B}/app/${k}/${proj}/`); console.log('not running page:', r.status);
  r = await fetch(`${B}/api/ide/search`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"project":"my-project","query":"x"}' });
  console.log('no client header ->', r.status);
})();

#!/usr/bin/env node
// Tiny static server for previewing ./dist (no dependencies).
//   node serve.js [port]      default port 3000, binds 0.0.0.0
'use strict';
const http = require('http'), fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, 'dist');
const PORT = Number(process.argv[2] || process.env.PORT || 3000);
const TYPES = {
  '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json', '.webmanifest': 'application/manifest+json', '.xml': 'application/xml', '.txt': 'text/plain; charset=utf-8',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.ico': 'image/x-icon',
  '.woff2': 'font/woff2', '.zip': 'application/zip', '.gz': 'application/gzip', '.exe': 'application/vnd.microsoft.portable-executable',
  '.deb': 'application/vnd.debian.binary-package', '.mp4': 'video/mp4', '.webm': 'video/webm',
};
http.createServer((req, res) => {
  let p;
  try { p = decodeURIComponent(new URL(req.url, 'http://x').pathname); } catch { res.writeHead(400).end(); return; }
  let file = path.normalize(path.join(ROOT, p));
  if (!file.startsWith(ROOT)) { res.writeHead(403).end(); return; }
  try { if (fs.statSync(file).isDirectory()) file = path.join(file, 'index.html'); } catch {
    if (!path.extname(file) && fs.existsSync(file + '.html')) file += '.html';   // clean URLs like most hosts
  }
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) {
      const nf = path.join(ROOT, '404.html');
      res.writeHead(404, { 'Content-Type': TYPES['.html'] });
      fs.createReadStream(nf).on('error', () => res.end('Not found')).pipe(res);
      return;
    }
    const ext = path.extname(file).toLowerCase();
    const headers = { 'Content-Type': TYPES[ext] || 'application/octet-stream', 'Content-Length': st.size, 'Cache-Control': 'no-cache',
      'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'strict-origin-when-cross-origin',
      // same policy as _headers / vercel.json, so problems show up in the preview too
      'Content-Security-Policy': "default-src 'self'; img-src 'self' data: https://avatars.githubusercontent.com; style-src 'self' 'unsafe-inline'; script-src 'self'; font-src 'self'; connect-src 'self' https://vgtucdfretcvojcfvbzk.supabase.co; frame-ancestors *; base-uri 'self'; form-action 'self'" };
    if (file.includes(path.sep + 'downloads' + path.sep)) headers['Content-Disposition'] = 'attachment';
    headers['Accept-Ranges'] = 'bytes';
    const m = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
    if (m && (m[1] || m[2])) {                                    // byte ranges, so video can seek (Safari requires it)
      let a = m[1] ? +m[1] : st.size - +m[2], b = m[1] && m[2] ? Math.min(+m[2], st.size - 1) : st.size - 1;
      if (a < 0) a = 0;
      if (a > b || a >= st.size) { res.writeHead(416, { 'Content-Range': `bytes */${st.size}` }).end(); return; }
      res.writeHead(206, { ...headers, 'Content-Length': b - a + 1, 'Content-Range': `bytes ${a}-${b}/${st.size}` });
      if (req.method === 'HEAD') return res.end();
      fs.createReadStream(file, { start: a, end: b }).pipe(res); return;
    }
    res.writeHead(200, headers);
    if (req.method === 'HEAD') return res.end();
    fs.createReadStream(file).pipe(res);
  });
}).listen(PORT, '0.0.0.0', () => console.log(`Astro AI website: http://0.0.0.0:${PORT}`));

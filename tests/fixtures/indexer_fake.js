// Fake npm / Hugging Face / Open VSX / GitHub search APIs for tests/indexer_sources_e2e.sh.
// Every request is appended to <log> so the test can check what was (and wasn't) fetched again.
'use strict';
const http = require('http'), fs = require('fs');
const [port, logFile] = [Number(process.argv[2]), process.argv[3]];
const send = (res, code, body, headers = {}) => { res.writeHead(code, { 'Content-Type': 'application/json', ...headers }); res.end(typeof body === 'string' ? body : JSON.stringify(body)); };
const pkg = (name, version, description, extra = {}) => ({ package: { name, version, description, keywords: ['mcp', 'weather'], links: { repository: `https://github.com/acme/${name.replace(/^@[^/]+\//, '')}` }, publisher: { username: 'acme' }, ...extra } });
const NPM = [
  pkg('weather-mcp', '1.2.3', 'Weather forecasts as an MCP server'),
  pkg('@acme/db-mcp', '0.4.0', 'Query a database over MCP'),
  pkg('mcp-sdk-helpers', '2.0.0', 'Helpers for writing MCP servers (a library)'),
  pkg('registry-dup-mcp', '1.0.0', 'Already in the official registry'),
  pkg('nsfw-image-mcp', '1.0.0', 'Generates nsfw images'),
];
const BIN = { 'weather-mcp': { 'weather-mcp': 'bin.js' }, '@acme/db-mcp': 'cli.js', 'registry-dup-mcp': { x: 'x.js' }, 'nsfw-image-mcp': { y: 'y.js' } };
const repo = (full, extra = {}) => ({ full_name: full, stargazers_count: 7, pushed_at: '2026-10-01T00:00:00Z', description: `Project ${full}`, topics: ['mcp-server', 'weather'], license: { spdx_id: 'MIT' }, fork: false, archived: false, ...extra });
http.createServer((req, res) => {
  fs.appendFileSync(logFile, req.url + '\n');
  const u = new URL(req.url, 'http://x');
  if (u.pathname === '/npm/-/v1/search') {
    const from = Number(u.searchParams.get('from') || 0);
    return send(res, 200, { total: NPM.length, objects: from === 0 ? NPM : [] });
  }
  if (u.pathname.startsWith('/npm/')) {
    const name = decodeURIComponent(u.pathname.slice(5));
    const p = NPM.find((x) => x.package.name === name);
    if (!p) return send(res, 404, { error: 'not found' });
    if (req.headers.accept !== 'application/vnd.npm.install-v1+json') return send(res, 400, { error: 'wrong accept' });
    return send(res, 200, { name, versions: { [p.package.version]: { name, version: p.package.version, bin: BIN[name] } } });
  }
  if (u.pathname === '/hf/api/spaces') {
    if (u.searchParams.get('cursor') === '2') return send(res, 200, [{ id: 'bob/Echo_Tool', sdk: 'gradio', likes: 3, cardData: { title: 'Echo Tool', short_description: 'Echoes text' } }]);
    return send(res, 200, [
      { id: 'alice/weather-space', sdk: 'gradio', likes: 12, cardData: { title: 'Weather Space', short_description: 'Weather tools over MCP' } },
      { id: 'alice/static-page', sdk: 'static', likes: 1, cardData: {} },
      { id: 'carol/a-very-long-space-name-that-gets-a-hashed-subdomain-on-hf', sdk: 'gradio', likes: 0, cardData: {} },
      { id: 'dan/undress-anyone', sdk: 'gradio', likes: 99, cardData: { title: 'Undress anyone' } },
    ], { Link: `<http://127.0.0.1:${port}/hf/api/spaces?filter=mcp-server&limit=1000&cursor=2>; rel="next"` });
  }
  if (u.pathname === '/ovsx/api/-/search') {
    const cat = u.searchParams.get('category');
    const ext = cat === 'Themes' ? [{ namespace: 'peach', name: 'peach-theme', version: '1.0.0', displayName: 'Peach Theme', description: 'A warm theme' }]
      : [{ namespace: 'acme', name: 'lint-all', version: '3.1.0', displayName: 'Lint All', description: 'Lints everything', verified: true }, { namespace: 'old', name: 'gone', version: '0.1.0', deprecated: true }];
    return send(res, 200, { totalSize: ext.length, extensions: ext });
  }
  if (u.pathname === '/ghapi/search/repositories') {
    const q = u.searchParams.get('q') || '';
    if (req.headers.authorization !== 'Bearer test-token') return send(res, 401, { message: 'Bad credentials' });
    let items = [];
    if (/topic:mcp-server /.test(q)) items = [repo('acme/weather-mcp'), repo('zed/tiny-mcp'), repo('zed/empty', { description: null, stargazers_count: 0 }), repo('zed/forked', { fork: true }), repo('official/registry-server')];
    if (/topic:claude-skills /.test(q)) items = [repo('sk/skills-repo', { topics: ['claude-skills'] })];
    return send(res, 200, { total_count: items.length, items });
  }
  send(res, 404, { error: 'unknown ' + u.pathname });
}).listen(port, '127.0.0.1', () => console.log('ready'));

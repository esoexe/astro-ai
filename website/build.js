#!/usr/bin/env node
// Builds the Astro AI website into ./dist (plain static files, deploy anywhere).
//   node build.js            build
//   node build.js --no-dl    build without copying the installers (faster while editing)
//
// src/pages/**.html   page bodies. First line: <!--meta {"title": "...", "description": "...", "nav": "features"} -->
// src/static/**       copied as-is (CSS, JS, fonts, images, press kit)
// site.json           site name, public URL (used for canonical links, social cards and the sitemap), version
// Downloads           the CLEAN edition only, copied from ../desktop-builds/clean. Never the WITH-KEYS build.
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto');

const ROOT = __dirname, SRC = path.join(ROOT, 'src'), OUT = path.join(ROOT, 'dist');
const icon = require('./icons.js');
const SITE = JSON.parse(fs.readFileSync(path.join(ROOT, 'site.json'), 'utf8'));
// CI (GitHub Pages) passes the real address, e.g. https://you.github.io/astro-ai
if (process.env.ASTRO_SITE_URL) SITE.url = process.env.ASTRO_SITE_URL;
const BASE_PATH = (new URL(SITE.url).pathname.replace(/\/+$/, '') || '') + '/';   // '/' or '/astro-ai/'
const NO_DL = process.argv.includes('--no-dl');
// Installers: $ASTRO_DL_DIR, else ../desktop-builds/clean (local dev), else ./downloads (the GitHub repo)
const DL_SRC = process.env.ASTRO_DL_DIR
  || [path.join(ROOT, '..', 'desktop-builds', 'clean'), path.join(ROOT, 'downloads')].find((p) => fs.existsSync(p))
  || path.join(ROOT, 'downloads');

const rm = (p) => fs.rmSync(p, { recursive: true, force: true });
function copyDir(a, b) {
  fs.mkdirSync(b, { recursive: true });
  for (const e of fs.readdirSync(a, { withFileTypes: true })) {
    const s = path.join(a, e.name), d = path.join(b, e.name);
    if (e.isDirectory()) copyDir(s, d); else fs.copyFileSync(s, d);
  }
}
function walk(dir, base = dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = path.join(dir, e.name);
    return e.isDirectory() ? walk(p, base) : [path.relative(base, p)];
  });
}
const human = (n) => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.round(n / 1024) + ' KB';
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

// ------------------------------------------------------------------ prepare
const keepDl = NO_DL && fs.existsSync(path.join(OUT, 'downloads'));
if (keepDl) fs.renameSync(path.join(OUT, 'downloads'), path.join(ROOT, '.dl-keep'));
rm(OUT);
copyDir(path.join(SRC, 'static'), OUT);
if (keepDl) fs.renameSync(path.join(ROOT, '.dl-keep'), path.join(OUT, 'downloads'));

// ------------------------------------------------------------------ downloads (CLEAN only)
const downloads = {};
const dlDir = path.join(OUT, 'downloads');
if (!NO_DL) {
  fs.mkdirSync(dlDir, { recursive: true });
  if (!fs.existsSync(DL_SRC)) throw new Error('Missing ' + DL_SRC + ' (run desktop/package.sh first, or put the CLEAN files in website/downloads)');
  for (const f of fs.readdirSync(DL_SRC)) {
    if (!(/CLEAN/.test(f) || /^astro-ai-vscode-[\d.]+\.vsix$/.test(f)) || /WITH-KEYS/i.test(f)) continue;           // belt and braces
    fs.copyFileSync(path.join(DL_SRC, f), path.join(dlDir, f));
  }
}
if (fs.existsSync(dlDir)) {
  const sums = [];
  for (const f of fs.readdirSync(dlDir).filter((f) => f !== 'SHA256SUMS.txt').sort()) {
    const buf = fs.readFileSync(path.join(dlDir, f));
    const sha = crypto.createHash('sha256').update(buf).digest('hex');
    downloads[f] = { size: human(buf.length), sha };
    sums.push(`${sha}  ${f}`);
  }
  fs.writeFileSync(path.join(dlDir, 'SHA256SUMS.txt'), sums.join('\n') + '\n');
}
const dlKey = (pattern) => Object.keys(downloads).find((f) => f.includes(pattern));

// ------------------------------------------------------------------ Astro AI Library (validated + checksummed)
const LIB = require(path.join(ROOT, '..', 'library', 'build_library.js')).build(path.join(OUT, 'library'));

// ------------------------------------------------------------------ layout
const NAV = [
  ['features', 'Features', 'features.html'],
  ['download', 'Download', 'download.html'],
  ['docs', 'Docs', 'docs.html'],
  ['api', 'API', 'api.html'],
  ['blog', 'Blog', 'blog/index.html'],
  ['changelog', 'Changelog', 'changelog.html'],
];
const LOGO = (r) => `<a class="brand" href="${r}index.html" aria-label="Astro AI home"><img src="${r}assets/img/icon-192.png" alt="" width="28" height="28"><span>Astro AI</span></a>`;

function layout(meta, body, rel, file) {
  const r = rel;
  const url = SITE.url.replace(/\/$/, '') + '/' + file.replace(/index\.html$/, '');
  const title = meta.title ? `${meta.title} · Astro AI` : 'Astro AI: one brain, many minds';
  const nav = NAV.map(([id, label, href]) => `<a href="${r}${href}"${meta.nav === id ? ' aria-current="page"' : ''}>${label}</a>`).join('');
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(title)}</title>
<meta name="description" content="${esc(meta.description || SITE.description)}">
<link rel="canonical" href="${esc(url)}">
<meta name="theme-color" content="#0a0a0c">
<meta name="color-scheme" content="dark">
<meta property="og:type" content="${meta.ogType || 'website'}">
<meta property="og:site_name" content="Astro AI">
<meta property="og:title" content="${esc(meta.title || 'Astro AI: one brain, many minds')}">
<meta property="og:description" content="${esc(meta.description || SITE.description)}">
<meta property="og:url" content="${esc(url)}">
<meta property="og:image" content="${esc(SITE.url.replace(/\/$/, ''))}/assets/img/og.png">
<meta name="twitter:card" content="summary_large_image">
<link rel="icon" type="image/png" sizes="32x32" href="${r}assets/img/favicon-32.png">
<link rel="apple-touch-icon" href="${r}assets/img/apple-touch-icon.png">
<link rel="manifest" href="${r}site.webmanifest">
<link rel="preload" href="${r}assets/fonts/inter-latin.woff2" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="${r}assets/fonts/instrument-serif.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="${r}assets/css/site.css">
<script defer src="${r}assets/js/site.js"></script>${(meta.scripts || []).map((x) => `\n<script defer src="${r}assets/js/${x}"></script>`).join('')}
</head>
<body class="site ${meta.bodyClass || ''}" data-root="${r}">
<a class="skip" href="#main">Skip to content</a>
<header class="nav" id="top">
  <div class="nav-inner">
    ${LOGO(r)}
    <a class="nav-ver" href="${r}changelog.html" title="What's new in ${SITE.version}">v${SITE.version.split('.').slice(0, 2).join('.')}</a>
    <nav class="nav-links" aria-label="Main">${nav}</nav>
    <div class="nav-cta">
      <a class="btn btn-ghost btn-sm nav-signin" href="${r}profile" data-account-link>Sign in</a>
      <a class="btn btn-primary btn-sm" href="${r}download.html" data-os-cta>Download</a>
      <button class="nav-toggle" aria-label="Open menu" aria-expanded="false" aria-controls="mobile-menu"><span></span><span></span></button>
    </div>
  </div>
  <div class="mobile-menu" id="mobile-menu" hidden>
    ${NAV.map(([id, label, href]) => `<a href="${r}${href}"${meta.nav === id ? ' aria-current="page"' : ''}>${label}</a>`).join('')}
    <a href="${r}press.html">Press kit</a>
    <a href="${r}profile" data-account-link>Sign in</a>
    <a class="btn btn-primary" href="${r}download.html">Download Astro AI</a>
  </div>
</header>
<main id="main">
${body}
</main>
<footer class="footer">
  <div class="wrap footer-grid">
    <div class="footer-brand">
      ${LOGO(r)}
      <p>One brain, many minds. A free AI coding platform and desktop app: open a folder, pick Plan, Ask or Auto, and let a team of free models build with you.</p>
      <span class="footer-status"><i></i>v${SITE.version} · ${SITE.dateLabel}</span>
    </div>
    <div><h4>Product</h4><a href="${r}features.html">Features</a><a href="${r}features.html#extensions">Extensions</a><a href="${r}download.html">Download</a><a href="${r}changelog.html">Changelog</a></div>
    <div><h4>Resources</h4><a href="${r}docs.html">Docs</a><a href="${r}api.html">Astro API</a><a href="${r}profile">Account</a><a href="${r}blog/index.html">Blog</a><a href="${r}press.html">Press kit</a>${SITE.repo ? `<a href="${SITE.repo}" rel="noopener">Source on GitHub</a>` : ''}</div>
    <div><h4>Legal</h4><a href="${r}privacy.html">Privacy</a><a href="${r}assets/fonts/FONTS-LICENSE.txt">Font licenses</a></div>
  </div>
  <div class="wrap footer-word" aria-hidden="true">Astro <em>AI</em></div>
  <div class="wrap footer-base"><span>© ${SITE.year} Astro AI</span><span class="mono">Made for curious people · runs on free AI models, which can make mistakes</span></div>
</footer>
</body>
</html>
`;
}

// App layout (meta.layout = "app"): the account console has its own chrome, no marketing header or footer.
function appLayout(meta, body, rel, file) {
  const r = rel;
  const url = SITE.url.replace(/\/$/, '') + '/' + file;
  const title = meta.title ? `${meta.title} · Astro AI` : 'Astro AI';
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>${esc(title)}</title>
<meta name="description" content="${esc(meta.description || SITE.description)}">
<link rel="canonical" href="${esc(url)}">
${meta.noindex ? '<meta name="robots" content="noindex">\n' : ''}<meta name="theme-color" content="#0a0a0c">
<meta name="color-scheme" content="dark">
<link rel="icon" type="image/png" sizes="32x32" href="${r}assets/img/favicon-32.png">
<link rel="apple-touch-icon" href="${r}assets/img/apple-touch-icon.png">
<link rel="preload" href="${r}assets/fonts/inter-latin.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="${r}assets/css/site.css">
${(meta.styles || []).map((x) => `<link rel="stylesheet" href="${r}assets/css/${x}">`).join('\n')}
${(meta.scripts || []).map((x) => `<script defer src="${r}assets/js/${x}"></script>`).join('\n')}
</head>
<body class="${meta.bodyClass || ''}" data-root="${r}">
${body}
</body>
</html>
`;
}

// ------------------------------------------------------------------ pages
const pages = walk(path.join(SRC, 'pages')).filter((f) => f.endsWith('.html'));
const sitemap = [];
for (const f of pages) {
  let src = fs.readFileSync(path.join(SRC, 'pages', f), 'utf8');
  let meta = {};
  const m = src.match(/^<!--meta\s+([\s\S]*?)-->\s*/);
  if (m) { meta = JSON.parse(m[1].replace(/\{\{version\}\}/g, SITE.version)); src = src.slice(m[0].length); }
  const depth = f.split(path.sep).length - 1;
  // 404.html is served at any depth, so it uses absolute links based on the site URL ('/' or '/astro-ai/')
  const rel = f === '404.html' ? BASE_PATH : '../'.repeat(depth);
  src = src
    .replace(/\{\{root\}\}/g, rel)
    .replace(/\{\{icon:([a-z]+)\}\}/g, (_, n) => icon(n))
    .replace(/\{\{version\}\}/g, SITE.version)
    .replace(/\{\{date\}\}/g, SITE.dateLabel)
    .replace(/\{\{dl:([^:}]+):(file|size|sha|href)\}\}/g, (_, pat, what) => {
      const k = dlKey(pat);
      if (!k) { console.warn('  ! no download matching', pat); return what === 'href' ? `${rel}download.html` : '—'; }
      return what === 'file' ? k : what === 'href' ? `${rel}downloads/${k}` : downloads[k][what];
    });
  const out = path.join(OUT, f);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, (meta.layout === 'app' ? appLayout : layout)(meta, src, rel, f.split(path.sep).join('/')));
  if (!meta.noindex) sitemap.push(f.split(path.sep).join('/').replace(/index\.html$/, ''));
}
// The Console moved from account.html to /profile. Old links (and their #section or ?mode=) keep working.
fs.writeFileSync(path.join(OUT, 'account.html'), `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Astro AI Console</title><meta name="robots" content="noindex">
<script src="assets/js/moved.js"></script>
<link rel="canonical" href="profile"><noscript><meta http-equiv="refresh" content="0; url=profile"></noscript></head>
<body><p>The Console moved to <a href="profile">/profile</a>.</p></body></html>
`);
const base = SITE.url.replace(/\/$/, '');
fs.writeFileSync(path.join(OUT, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
${sitemap.sort().map((p) => `  <url><loc>${base}/${p}</loc></url>`).join('\n')}
</urlset>
`);
fs.writeFileSync(path.join(OUT, 'robots.txt'), `User-agent: *\nAllow: /\nSitemap: ${base}/sitemap.xml\n`);

// sanity: nothing from the personal edition may ever be published
const bad = walk(OUT).filter((f) => /WITH-KEYS|seed-keys|gateway\.env/i.test(f));
if (bad.length) { rm(OUT); throw new Error('Refusing to publish personal-edition files: ' + bad.join(', ')); }

console.log(`Built ${pages.length} pages into ${path.relative(process.cwd(), OUT) || '.'}` +
  (Object.keys(downloads).length ? `, ${Object.keys(downloads).length} downloads` : ''));

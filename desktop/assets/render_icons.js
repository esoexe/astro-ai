#!/usr/bin/env node
// Renders the A.S.T.R.O AI icon SVGs (make_icon.js) into every PNG/ICO used by the app, installers, VS Code extension and website.
//   NODE_PATH=/tmp/pw/node_modules node desktop/assets/render_icons.js [--preview out.png]
'use strict';
const fs = require('fs'); const path = require('path'); const { execFileSync } = require('child_process');
const { chromium } = require('playwright');
const A = __dirname, ROOT = path.resolve(A, '../..');
const svg = (f) => fs.readFileSync(path.join(A, f), 'utf8');

// [svg source, size, output paths...]
const JOBS = [
  ['icon-small.svg', 16, 'desktop/assets/icon-16.png'],
  ['icon-small.svg', 24, 'desktop/assets/icon-24.png'],
  ['icon-small.svg', 32, 'desktop/assets/icon-32.png', 'web/favicon-32.png', 'website/src/static/assets/img/favicon-32.png'],
  ['icon-small.svg', 48, 'desktop/assets/icon-48.png'],
  ['icon.svg', 64, 'desktop/assets/icon-64.png'],
  ['icon.svg', 128, 'desktop/assets/icon-128.png', 'ide/vscode/icon.png'],
  ['icon.svg', 192, 'web/icon-192.png', 'website/src/static/assets/img/icon-192.png'],
  ['icon.svg', 256, 'desktop/assets/icon-256.png'],
  ['icon.svg', 512, 'desktop/assets/icon-512.png', 'web/icon-512.png', 'website/src/static/assets/img/icon-512.png'],
  ['icon.svg', 1024, 'desktop/assets/icon-1024.png', 'website/src/static/assets/img/icon-1024.png'],
  ['icon.svg', 2048, 'desktop/assets/icon-source.png'],
  ['icon-mac.svg', 1024, 'desktop/assets/icon-mac-1024.png'],
  ['icon-maskable.svg', 512, 'web/icon-maskable-512.png'],
  ['icon-flat.svg', 180, 'web/apple-touch-icon.png', 'website/src/static/assets/img/apple-touch-icon.png'],
];

(async () => {
  const b = await chromium.launch();
  const pg = await b.newPage({ deviceScaleFactor: 1 });
  async function render(src, size) {
    await pg.setViewportSize({ width: size, height: size });
    const s = svg(src).replace('width="1024" height="1024"', `width="${size}" height="${size}"`);
    await pg.setContent(`<!doctype html><html><body style="margin:0;background:transparent">${s}</body></html>`);
    return pg.screenshot({ omitBackground: true, clip: { x: 0, y: 0, width: size, height: size } });
  }
  for (const [src, size, ...outs] of JOBS) {
    const png = await render(src, size);
    for (const o of outs) { fs.mkdirSync(path.dirname(path.join(ROOT, o)), { recursive: true }); fs.writeFileSync(path.join(ROOT, o), png); }
    console.log(`${String(size).padStart(4)} ${src.padEnd(18)} -> ${outs.join(', ')}`);
  }
  // the SVG itself for the web app (favicon / PWA)
  fs.copyFileSync(path.join(A, 'icon.svg'), path.join(ROOT, 'web/icon.svg'));
  fs.copyFileSync(path.join(A, 'icon.svg'), path.join(ROOT, 'website/src/static/assets/img/icon.svg'));
  // Windows .ico (installer, exe, setup)
  const icoIn = [16, 24, 32, 48, 64, 128, 256].map((s) => path.join(A, `icon-${s}.png`));
  execFileSync('convert', [...icoIn, path.join(A, 'astro.ico')]);
  console.log('     astro.ico (16–256)');
  const pv = process.argv.indexOf('--preview');
  if (pv > 0) {
    const sizes = [16, 24, 32, 48, 64, 128, 256, 512];
    const cells = sizes.map((s) => `<div style="display:flex;flex-direction:column;align-items:center;gap:10px"><img src="data:image/png;base64,${fs.readFileSync(path.join(ROOT, s === 512 ? 'desktop/assets/icon-512.png' : `desktop/assets/icon-${s}.png`)).toString('base64')}" width="${s}" height="${s}" style="image-rendering:auto"><span style="font:12px monospace;color:#888">${s}</span></div>`).join('');
    await pg.setViewportSize({ width: 1500, height: 640 });
    await pg.setContent(`<body style="margin:0;padding:40px;display:flex;gap:28px;align-items:flex-end;background:linear-gradient(90deg,#f4f1ee 0 50%,#16161a 50%);height:560px;box-sizing:border-box">${cells}</body>`);
    await pg.screenshot({ path: process.argv[pv + 1] });
    console.log('preview', process.argv[pv + 1]);
  }
  await b.close();
})().catch((e) => { console.error(e); process.exit(1); });

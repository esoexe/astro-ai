#!/usr/bin/env node
// A.S.T.R.O AI icon: one glowing core ("one brain") inside three crossing orbits, each with a satellite ("many minds").
// Writes the vector sources; render_icons.js turns them into every PNG/ICO the app, installers and website use.
//   node desktop/assets/make_icon.js            -> desktop/assets/icon.svg, icon-small.svg, icon-mac.svg, icon-maskable.svg, icon-flat.svg, mark.svg
'use strict';
const fs = require('fs'); const path = require('path');
const OUT = __dirname;

const PEACH = '#f3a679';
const C = 512;                       // centre of the 1024 canvas
const ORBITS = [0, 60, -60];         // tilt of each orbit, degrees
const R = (d) => (d * Math.PI) / 180;

// point on an orbit (ellipse rx/ry, tilted by deg) at parameter t (degrees)
function onOrbit(rx, ry, deg, t) {
  const x = rx * Math.cos(R(t)), y = ry * Math.sin(R(t));
  return [C + x * Math.cos(R(deg)) - y * Math.sin(R(deg)), C + x * Math.sin(R(deg)) + y * Math.cos(R(deg))];
}
// half of a tilted ellipse as a path: back = upper half (behind the core), front = lower half
function half(rx, ry, deg, front) {
  const [x0, y0] = onOrbit(rx, ry, deg, 180), [x1, y1] = onOrbit(rx, ry, deg, 0);
  return front
    ? `M${x0.toFixed(2)} ${y0.toFixed(2)} A${rx} ${ry} ${deg} 0 0 ${x1.toFixed(2)} ${y1.toFixed(2)}`
    : `M${x1.toFixed(2)} ${y1.toFixed(2)} A${rx} ${ry} ${deg} 0 0 ${x0.toFixed(2)} ${y0.toFixed(2)}`;
}

/* opts: tile = 'round' | 'square' | 'none', scale (artwork size inside the tile), inset (tile margin for macOS),
   small = simplified for 16–48 px, glow */
function svg(o = {}) {
  const tile = o.tile || 'round', s = o.scale || 1, inset = o.inset || 0, small = !!o.small, glow = o.glow !== false && !small;
  const rx = 392, ry = small ? 150 : 128, coreR = small ? 214 : 176;
  const sw = small ? 46 : 11;
  const sats = small ? [] : [[0, 202], [60, -28], [-60, 152], [60, 152]];
  const tSize = 1024 - inset * 2, tRad = tile === 'round' ? tSize * 0.2237 : 0;   // Apple-like corner ratio
  const defs = `
    <radialGradient id="bg" cx="50%" cy="46%" r="70%"><stop offset="0" stop-color="#1d1411"/><stop offset=".55" stop-color="#0e0c0d"/><stop offset="1" stop-color="#070709"/></radialGradient>
    <radialGradient id="halo" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="${PEACH}" stop-opacity=".55"/><stop offset=".45" stop-color="${PEACH}" stop-opacity=".16"/><stop offset="1" stop-color="${PEACH}" stop-opacity="0"/></radialGradient>
    <radialGradient id="core" cx="38%" cy="32%" r="78%"><stop offset="0" stop-color="#ffe6d3"/><stop offset=".38" stop-color="#f7b78f"/><stop offset=".78" stop-color="#ec8d63"/><stop offset="1" stop-color="#d9714b"/></radialGradient>
    <radialGradient id="rim" cx="50%" cy="50%" r="50%"><stop offset=".86" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#ffd9c2" stop-opacity=".35"/></radialGradient>
    <linearGradient id="ring" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffd2b5"/><stop offset=".5" stop-color="${PEACH}"/><stop offset="1" stop-color="#e98f66"/></linearGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".07"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient>
    <filter id="soft" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="9"/></filter>
    <filter id="spark" x="-200%" y="-200%" width="500%" height="500%"><feGaussianBlur stdDeviation="10" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>`;
  let art = '';
  if (glow) art += `<circle cx="${C}" cy="${C}" r="430" fill="url(#halo)"/>`;
  // back halves of the orbits (behind the core), dimmer
  for (const d of ORBITS) art += `<path d="${half(rx, ry, d, false)}" fill="none" stroke="url(#ring)" stroke-width="${sw}" stroke-linecap="round" opacity="${small ? 0.85 : 0.5}"/>`;
  // the core
  if (glow) art += `<circle cx="${C}" cy="${C + 6}" r="${coreR + 22}" fill="${PEACH}" opacity=".35" filter="url(#soft)"/>`;
  art += `<circle cx="${C}" cy="${C}" r="${coreR}" fill="url(#core)"/>`;
  if (!small) art += `<circle cx="${C}" cy="${C}" r="${coreR}" fill="url(#rim)"/>`;
  // front halves (over the core), bright
  for (const d of ORBITS) {
    if (glow) art += `<path d="${half(rx, ry, d, true)}" fill="none" stroke="${PEACH}" stroke-width="${sw * 2.6}" stroke-linecap="round" opacity=".18" filter="url(#soft)"/>`;
    art += `<path d="${half(rx, ry, d, true)}" fill="none" stroke="url(#ring)" stroke-width="${sw}" stroke-linecap="round"/>`;
  }
  // satellites
  for (const [d, t] of sats) {
    const [x, y] = onOrbit(rx, ry, d, t);
    art += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="19" fill="#fff4ec" filter="url(#spark)"/>`;
  }
  const g = `<g transform="translate(${C} ${C}) scale(${(s * tSize) / 1024}) translate(${-C} ${-C})">${art}</g>`;
  let body = '';
  if (tile !== 'none') {
    const shadow = inset ? `<rect x="${inset}" y="${inset + 14}" width="${tSize}" height="${tSize}" rx="${tRad}" fill="#000" opacity=".35" filter="url(#soft)"/>` : '';
    body += `${shadow}<rect x="${inset}" y="${inset}" width="${tSize}" height="${tSize}" rx="${tRad}" fill="url(#bg)"/>`;
    if (!small) body += `<rect x="${inset}" y="${inset}" width="${tSize}" height="${tSize}" rx="${tRad}" fill="url(#sheen)"/>`;
    if (!small && tile === 'round') body += `<rect x="${inset + 1.5}" y="${inset + 1.5}" width="${tSize - 3}" height="${tSize - 3}" rx="${tRad - 1.5}" fill="none" stroke="#fff" stroke-opacity=".07" stroke-width="3"/>`;
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024"><defs>${defs}</defs>${body}${g}</svg>\n`;
}

const files = {
  'icon.svg': svg({ tile: 'round', scale: 0.9 }),                 // app / website / VS Code
  'icon-small.svg': svg({ tile: 'round', scale: 0.94, small: true }), // 16–48 px: thicker lines, no satellites
  'icon-mac.svg': svg({ tile: 'round', scale: 0.9, inset: 100 }),  // macOS grid: 824 px tile inside 1024
  'icon-maskable.svg': svg({ tile: 'square', scale: 0.7 }),        // Android maskable: art inside the safe zone
  'icon-flat.svg': svg({ tile: 'square', scale: 0.9 }),            // apple-touch-icon (iOS rounds it)
  'mark.svg': svg({ tile: 'none', scale: 1 }),                     // the mark alone, transparent
};
for (const [f, s] of Object.entries(files)) fs.writeFileSync(path.join(OUT, f), s);
console.log('wrote', Object.keys(files).join(', '));
module.exports = { svg };

// A.S.T.R.O AI live logo (window.AstroLogo): the "refined orbit" mark, drawn as SVG and animated.
// A glowing core, three rings at 0 / +60 / -60 degrees (front halves over the core, back halves behind it), light
// travelling along the rings and four satellites that really orbit: they pass behind the core and come round in
// front. Colours follow the active theme accent. Every logo in the app is upgraded automatically, including ones
// rendered later (empty states) and the desktop title bar. Respects prefers-reduced-motion and a user switch.
(() => {
  'use strict';
  const NS = 'http://www.w3.org/2000/svg';
  const RX = 19, RY = 6.3, CORE = 7.4, C = 24;
  const RINGS = [0, 60, -60];
  // [ring index, start angle (rad), speed (rad/s)]
  const SATS = [[0, 3.3, 0.55], [1, 0.9, 0.42], [2, 2.2, -0.47], [1, 4.0, 0.42]];
  const KEY = 'astro_logo_still';
  let uid = 0; const live = new Set(); let raf = 0; let last = 0;
  const reduced = () => (window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  const still = () => { try { return localStorage.getItem(KEY) === '1'; } catch (_e) { return false; } };

  function markup(id) {
    const ring = (rot, front) => `<path class="al-ring${front ? ' al-front' : ' al-back'}" transform="rotate(${rot} ${C} ${C})" d="${front ? `M${C - RX} ${C}A${RX} ${RY} 0 0 0 ${C + RX} ${C}` : `M${C + RX} ${C}A${RX} ${RY} 0 0 0 ${C - RX} ${C}`}"/>`;
    const streak = (rot, i) => `<path class="al-streak" style="animation-delay:${-i * 1.9}s" transform="rotate(${rot} ${C} ${C})" d="M${C - RX} ${C}A${RX} ${RY} 0 0 0 ${C + RX} ${C}" pathLength="100"/>`;
    return `<defs>
      <radialGradient id="al-core-${id}" cx="38%" cy="32%" r="78%"><stop offset="0" class="al-s0"/><stop offset=".4" class="al-s1"/><stop offset=".8" class="al-s2"/><stop offset="1" class="al-s3"/></radialGradient>
      <radialGradient id="al-halo-${id}" cx="50%" cy="50%" r="50%"><stop offset="0" class="al-h0"/><stop offset=".5" class="al-h1"/><stop offset="1" class="al-h2"/></radialGradient>
      <filter id="al-glow-${id}" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation=".9" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    </defs>
    <circle class="al-halo" cx="${C}" cy="${C}" r="17" fill="url(#al-halo-${id})"/>
    <g class="al-backs">${RINGS.map((r) => ring(r, false)).join('')}</g>
    <g class="al-sats-back"></g>
    <circle class="al-core" cx="${C}" cy="${C}" r="${CORE}" fill="url(#al-core-${id})"/>
    <circle class="al-rim" cx="${C}" cy="${C}" r="${CORE - 0.35}"/>
    <g class="al-fronts">${RINGS.map((r) => ring(r, true)).join('')}</g>
    <g class="al-streaks">${RINGS.map((r, i) => streak(r, i)).join('')}</g>
    <g class="al-sats-front" filter="url(#al-glow-${id})"></g>`;
  }
  function create(cls) {
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('viewBox', '0 0 48 48'); svg.setAttribute('aria-hidden', 'true');
    svg.setAttribute('class', `orbit-svg astro-live${cls ? ` ${cls}` : ''}`);
    const id = ++uid; svg.innerHTML = markup(id);
    svg._al = { back: svg.querySelector('.al-sats-back'), front: svg.querySelector('.al-sats-front'), dots: [] };
    for (let i = 0; i < SATS.length; i++) { const c = document.createElementNS(NS, 'circle'); c.setAttribute('r', '1.25'); c.setAttribute('class', 'al-sat'); svg._al.dots.push(c); }
    place(svg, still() || reduced() ? 1.4 : performance.now() / 1000);
    live.add(svg); kick();
    return svg;
  }
  function place(svg, t) {
    const st = svg._al; if (!st) return;
    SATS.forEach(([ri, a0, sp], i) => {
      const th = a0 + sp * t; const rot = (RINGS[ri] * Math.PI) / 180;
      const lx = RX * Math.cos(th), ly = RY * Math.sin(th);
      const x = C + lx * Math.cos(rot) - ly * Math.sin(rot), y = C + lx * Math.sin(rot) + ly * Math.cos(rot);
      const front = Math.sin(th) > 0; const dot = st.dots[i];
      dot.setAttribute('cx', x.toFixed(2)); dot.setAttribute('cy', y.toFixed(2));
      dot.setAttribute('r', front ? '1.3' : '1.05'); dot.classList.toggle('al-far', !front);
      const want = front ? st.front : st.back; if (dot.parentNode !== want) want.appendChild(dot);
    });
  }
  function frame(now) {
    raf = 0;
    if (document.hidden || still() || reduced()) return;
    if (now - last >= 33) {   // ~30 fps is plenty for a logo
      last = now; const t = now / 1000;
      for (const svg of live) { if (!svg.isConnected) { live.delete(svg); continue; } place(svg, t); }
    }
    if (live.size) raf = requestAnimationFrame(frame);
  }
  function kick() { if (!raf && live.size && !document.hidden && !still() && !reduced()) raf = requestAnimationFrame(frame); }

  // Upgrade the existing marks: old spinning-atom SVGs and the desktop title-bar PNG.
  function upgrade(root) {
    const scope = root && root.querySelectorAll ? root : document;
    const olds = [...scope.querySelectorAll('svg.orbit-svg:not(.astro-live)')];
    if (scope.matches && scope.matches('svg.orbit-svg:not(.astro-live)')) olds.push(scope);
    for (const old of olds) old.replaceWith(create());
    for (const img of scope.querySelectorAll('img.dt-logo')) { const s = create('dt-logo'); s.setAttribute('width', img.getAttribute('width') || '20'); s.setAttribute('height', img.getAttribute('height') || '20'); img.replaceWith(s); }
  }
  function setStill(on) {
    try { if (on) localStorage.setItem(KEY, '1'); else localStorage.removeItem(KEY); } catch (_e) { /* private mode */ }
    document.documentElement.classList.toggle('logo-still', Boolean(on));
    if (!on) kick(); else for (const svg of live) place(svg, 1.4);
  }
  function start() {
    document.documentElement.classList.toggle('logo-still', still());
    upgrade(document);
    new MutationObserver((muts) => {
      for (const m of muts) for (const n of m.addedNodes) if (n.nodeType === 1 && (n.matches('svg.orbit-svg:not(.astro-live), img.dt-logo') || n.querySelector('svg.orbit-svg:not(.astro-live), img.dt-logo'))) upgrade(n);
    }).observe(document.body, { childList: true, subtree: true });
    document.addEventListener('visibilitychange', kick);
    if (window.matchMedia) window.matchMedia('(prefers-reduced-motion: reduce)').addEventListener?.('change', kick);
  }
  window.AstroLogo = { create, upgrade, setStill, get still() { return still(); } };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();

/* Astro AI website. No trackers, no external requests. */
(() => {
  'use strict';
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const ROOT = document.body.dataset.root || '';

  /* ------------------------------------------------------------ nav */
  const nav = $('.nav');
  const onScroll = () => nav && nav.classList.toggle('scrolled', scrollY > 8);
  addEventListener('scroll', onScroll, { passive: true }); onScroll();
  const toggle = $('.nav-toggle'), menu = $('#mobile-menu');
  if (toggle && menu) {
    toggle.addEventListener('click', () => {
      const open = toggle.getAttribute('aria-expanded') !== 'true';
      toggle.setAttribute('aria-expanded', String(open));
      toggle.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
      menu.hidden = !open;
      document.body.style.overflow = open ? 'hidden' : '';
    });
    addEventListener('keydown', (e) => { if (e.key === 'Escape' && !menu.hidden) toggle.click(); });
  }

  /* ------------------------------------------------------------ reveal */
  const io = 'IntersectionObserver' in window && !reduced ? new IntersectionObserver((es) => {
    for (const e of es) if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); }
  }, { rootMargin: '0px 0px -8% 0px', threshold: 0.08 }) : null;
  $$('.reveal, .steps').forEach((el) => io ? io.observe(el) : el.classList.add('in'));

  /* card spotlight */
  if (!reduced) $$('.card').forEach((c) => c.addEventListener('pointermove', (e) => {
    const r = c.getBoundingClientRect();
    c.style.setProperty('--mx', (e.clientX - r.left) + 'px');
    c.style.setProperty('--my', (e.clientY - r.top) + 'px');
  }));

  /* ------------------------------------------------------------ tabs */
  $$('[data-tabs]').forEach((group) => {
    const tabs = $$('[role="tab"]', group);
    const select = (t, focus) => {
      tabs.forEach((x) => {
        const on = x === t;
        x.setAttribute('aria-selected', String(on));
        x.tabIndex = on ? 0 : -1;
        const p = document.getElementById(x.getAttribute('aria-controls'));
        if (p) p.classList.toggle('active', on);
      });
      if (focus) t.focus();
    };
    tabs.forEach((t, i) => {
      t.addEventListener('click', () => select(t));
      t.addEventListener('keydown', (e) => {
        const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
        if (d) { e.preventDefault(); select(tabs[(i + d + tabs.length) % tabs.length], true); }
      });
    });
  });

  /* ------------------------------------------------------------ OS detection */
  const ua = navigator.userAgent || '';
  const plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
  const os = /Win/i.test(plat) || /Windows/i.test(ua) ? 'windows'
    : /Mac/i.test(plat) || /Mac OS X/i.test(ua) ? (/iPhone|iPad/i.test(ua) ? 'mobile' : 'macos')
    : /Android|iPhone|iPad/i.test(ua) ? 'mobile'
    : /Linux|X11|CrOS/i.test(plat + ua) ? 'linux' : 'unknown';
  document.documentElement.dataset.os = os;
  const LABEL = { windows: 'Download for Windows', linux: 'Download for Linux', macos: 'Get it for macOS' };
  $$('[data-os-primary]').forEach((a) => {
    const href = a.dataset['href' + os.charAt(0).toUpperCase() + os.slice(1)];
    if (href && LABEL[os]) {
      a.href = href;
      const l = $('.label', a); if (l) l.textContent = LABEL[os];
      if (os === 'macos') a.removeAttribute('download');
    }
  });
  $$('[data-os-note]').forEach((el) => {
    const t = el.dataset['note' + os.charAt(0).toUpperCase() + os.slice(1)];
    if (t) el.textContent = t;
  });
  $$('[data-os-card]').forEach((c) => {
    if (c.dataset.osCard === os) {
      c.classList.add('recommended');
      const b = $('.for-you', c); if (b) b.hidden = false;
    }
  });

  /* ------------------------------------------------------------ copy */
  const copy = async (text, btn) => {
    try { await navigator.clipboard.writeText(text); }
    catch { const t = document.createElement('textarea'); t.value = text; t.style.position = 'fixed'; t.style.opacity = '0'; document.body.appendChild(t); t.select(); try { document.execCommand('copy'); } catch {} t.remove(); }
    const old = btn.textContent; btn.textContent = 'Copied'; btn.classList.add('done');
    setTimeout(() => { btn.textContent = old; btn.classList.remove('done'); }, 1400);
  };
  $$('[data-copy]').forEach((b) => b.addEventListener('click', () => copy(b.dataset.copy, b)));
  $$('.prose pre').forEach((pre) => {
    const b = document.createElement('button'); b.className = 'copy'; b.type = 'button'; b.textContent = 'Copy';
    b.addEventListener('click', () => copy(pre.querySelector('code') ? pre.querySelector('code').innerText : pre.innerText, b));
    pre.appendChild(b);
  });

  /* ------------------------------------------------------------ docs scrollspy */
  const toc = $('.toc');
  if (toc && 'IntersectionObserver' in window) {
    const links = $$('a[href^="#"]', toc);
    const map = new Map(links.map((a) => [decodeURIComponent(a.hash.slice(1)), a]));
    const heads = $$('.prose h2[id], .prose h3[id]').filter((h) => map.has(h.id));
    let current = null;
    const spy = new IntersectionObserver(() => {
      let best = heads[0];
      for (const h of heads) if (h.getBoundingClientRect().top < innerHeight * 0.3) best = h;
      if (best && best !== current) { current = best; links.forEach((a) => a.classList.toggle('active', a === map.get(best.id))); }
    }, { rootMargin: '0px 0px -60% 0px', threshold: [0, 1] });
    heads.forEach((h) => spy.observe(h));
    addEventListener('scroll', () => {
      let best = heads[0];
      for (const h of heads) if (h.getBoundingClientRect().top < innerHeight * 0.3) best = h;
      if (best && best !== current) { current = best; links.forEach((a) => a.classList.toggle('active', a === map.get(best.id))); }
    }, { passive: true });
  }

  /* ------------------------------------------------------------ orbit canvas */
  // The same scene as the installer: twinkling stars, a tilted peach ring, a glowing core and a moon
  // that passes in front of and behind it. Pauses off-screen; one still frame for reduced motion.
  function orbit(canvas) {
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const o = {
      cx: parseFloat(canvas.dataset.cx || '0.7'), cy: parseFloat(canvas.dataset.cy || '0.5'),
      size: parseFloat(canvas.dataset.size || '0.36'), stars: parseFloat(canvas.dataset.stars || '1'),
      mcx: parseFloat(canvas.dataset.mcx || '0.5'), mcy: parseFloat(canvas.dataset.mcy || '0.34'), msize: parseFloat(canvas.dataset.msize || '0.62'),
    };
    let W = 0, H = 0, dpr = 1, stars = [], shoot = null, raf = 0, visible = true, t0 = performance.now();
    let px = 0, py = 0, tx = 0, ty = 0;
    const TILT = -0.32;

    function resize() {
      const r = canvas.getBoundingClientRect();
      dpr = Math.min(devicePixelRatio || 1, 2);
      W = Math.max(1, r.width); H = Math.max(1, r.height);
      canvas.width = Math.round(W * dpr); canvas.height = Math.round(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      const n = Math.round(Math.min(420, (W * H) / 3800) * o.stars);
      let seed = 7;
      const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
      stars = Array.from({ length: n }, () => ({
        x: rnd() * W, y: rnd() * H, r: rnd() < 0.08 ? 1.3 + rnd() * 0.8 : 0.4 + rnd() * 0.8,
        a: 0.25 + rnd() * 0.6, tw: 0.4 + rnd() * 1.8, ph: rnd() * 6.28, z: 0.2 + rnd() * 0.8,
        warm: rnd() < 0.18,
      }));
      if (reduced) draw(performance.now());
    }

    function geom() {
      const mobile = W < 760;
      const cx = (mobile ? o.mcx : o.cx) * W, cy = (mobile ? o.mcy : o.cy) * H;
      const R = Math.min(W, H * 1.6) * (mobile ? o.msize : o.size);
      return { cx: cx + px * 10, cy: cy + py * 8, R };
    }
    const ell = (g, a, k = 1) => {
      const x = Math.cos(a) * g.R * k, y = Math.sin(a) * g.R * 0.3 * k;
      return [g.cx + x * Math.cos(TILT) - y * Math.sin(TILT), g.cy + x * Math.sin(TILT) + y * Math.cos(TILT)];
    };

    function ring(g, front, k, alpha, width) {
      const N = 160;
      ctx.lineWidth = width; ctx.lineCap = 'round';
      for (let i = 0; i < N; i++) {
        const a0 = (i / N) * Math.PI * 2, a1 = ((i + 1) / N) * Math.PI * 2;
        const depth = Math.sin((a0 + a1) / 2);           // >0 = in front of the core
        if ((depth > 0) !== front) continue;
        const [x0, y0] = ell(g, a0, k), [x1, y1] = ell(g, a1, k);
        const f = front ? 0.55 + depth * 0.45 : 0.22 + (1 + depth) * 0.2;
        ctx.strokeStyle = `rgba(243,166,121,${(alpha * f).toFixed(3)})`;
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
      }
    }

    function core(g, t) {
      const r = g.R * 0.15, pulse = 1 + Math.sin(t * 1.3) * 0.025;
      let grd = ctx.createRadialGradient(g.cx, g.cy, 0, g.cx, g.cy, r * 5.5);
      grd.addColorStop(0, 'rgba(240,138,75,0.32)'); grd.addColorStop(0.35, 'rgba(240,138,75,0.08)'); grd.addColorStop(1, 'rgba(240,138,75,0)');
      ctx.fillStyle = grd; ctx.beginPath(); ctx.arc(g.cx, g.cy, r * 5.5, 0, 7); ctx.fill();
      grd = ctx.createRadialGradient(g.cx - r * 0.35, g.cy - r * 0.4, r * 0.1, g.cx, g.cy, r * pulse);
      grd.addColorStop(0, '#fff3e8'); grd.addColorStop(0.35, '#ffd2b0'); grd.addColorStop(0.8, '#f3a679'); grd.addColorStop(1, '#f08a4b');
      ctx.fillStyle = grd; ctx.beginPath(); ctx.arc(g.cx, g.cy, r * pulse, 0, 7); ctx.fill();
      ctx.strokeStyle = 'rgba(255,230,210,0.35)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(g.cx, g.cy, r * pulse * 1.18, 0, 7); ctx.stroke();
    }

    function moon(g, a) {
      const depth = Math.sin(a), [x, y] = ell(g, a);
      const r = g.R * (0.032 + 0.016 * (depth + 1) / 2);
      // trail: a smooth tapered arc behind the moon
      ctx.lineCap = 'round';
      const STEPS = 48, SPAN = 1.25;
      for (let i = 0; i < STEPS; i++) {
        const b0 = a - (i / STEPS) * SPAN, b1 = a - ((i + 1) / STEPS) * SPAN;
        const [x0, y0] = ell(g, b0), [x1, y1] = ell(g, b1), k = 1 - i / STEPS;
        const dep = 0.45 + 0.55 * (Math.sin(b0) + 1) / 2;
        ctx.strokeStyle = `rgba(250,178,131,${(0.5 * k * k * dep).toFixed(3)})`;
        ctx.lineWidth = Math.max(0.6, r * 1.5 * k);
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
      }
      const grd = ctx.createRadialGradient(x, y, 0, x, y, r * 4);
      grd.addColorStop(0, 'rgba(250,178,131,0.45)'); grd.addColorStop(1, 'rgba(250,178,131,0)');
      ctx.fillStyle = grd; ctx.beginPath(); ctx.arc(x, y, r * 4, 0, 7); ctx.fill();
      ctx.fillStyle = '#fab283'; ctx.beginPath(); ctx.arc(x, y, r, 0, 7); ctx.fill();
      ctx.fillStyle = 'rgba(255,240,228,0.85)'; ctx.beginPath(); ctx.arc(x - r * 0.3, y - r * 0.3, r * 0.38, 0, 7); ctx.fill();
    }

    function draw(now) {
      const t = (now - t0) / 1000;
      px += (tx - px) * 0.04; py += (ty - py) * 0.04;
      ctx.clearRect(0, 0, W, H);
      for (const s of stars) {
        const a = s.a * (0.55 + 0.45 * Math.sin(t * s.tw + s.ph));
        ctx.fillStyle = s.warm ? `rgba(255,214,186,${a.toFixed(3)})` : `rgba(235,235,245,${a.toFixed(3)})`;
        const x = s.x - px * 14 * s.z, y = s.y - py * 10 * s.z;
        if (s.r > 1.2) { ctx.beginPath(); ctx.arc(x, y, s.r, 0, 7); ctx.fill(); }
        else ctx.fillRect(x, y, s.r * 1.4, s.r * 1.4);
      }
      if (!reduced) {
        if (!shoot && Math.random() < 0.004) shoot = { x: Math.random() * W * 0.8, y: Math.random() * H * 0.4, life: 0 };
        if (shoot) {
          shoot.life += 0.016;
          const L = shoot.life, x = shoot.x + L * 520, y = shoot.y + L * 200, al = Math.max(0, 1 - L / 0.9);
          const grd = ctx.createLinearGradient(x, y, x - 120, y - 46);
          grd.addColorStop(0, `rgba(255,230,210,${(0.8 * al).toFixed(3)})`); grd.addColorStop(1, 'rgba(255,230,210,0)');
          ctx.strokeStyle = grd; ctx.lineWidth = 1.2; ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x - 120, y - 46); ctx.stroke();
          if (L > 0.9) shoot = null;
        }
      }
      const g = geom();
      const a = reduced ? 0.9 : t * 0.42 + 0.9;
      ring(g, false, 1.38, 0.16, 1);
      ring(g, false, 1, 0.9, 1.6);
      if (Math.sin(a) <= 0) moon(g, a);
      core(g, t);
      ring(g, true, 1, 0.9, 1.6);
      ring(g, true, 1.38, 0.16, 1);
      if (Math.sin(a) > 0) moon(g, a);
    }

    function loop(now) { draw(now); raf = visible && !document.hidden ? requestAnimationFrame(loop) : 0; }
    const start = () => { if (!raf && !reduced && visible && !document.hidden) raf = requestAnimationFrame(loop); };

    resize();
    new ResizeObserver(resize).observe(canvas);
    if (reduced) { draw(performance.now()); return; }
    if ('IntersectionObserver' in window) new IntersectionObserver((es) => { visible = es[0].isIntersecting; start(); }).observe(canvas);
    document.addEventListener('visibilitychange', start);
    addEventListener('pointermove', (e) => { tx = e.clientX / innerWidth - 0.5; ty = e.clientY / innerHeight - 0.5; }, { passive: true });
    start();
  }
  $$('canvas[data-orbit]').forEach(orbit);

  /* footer year links etc. are static; nothing else to do */
  void ROOT;
})();

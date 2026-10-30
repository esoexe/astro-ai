/* A.S.T.R.O AI website. No trackers, no external requests. */
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
    if (c.dataset.osCard === os || (c.dataset.osCard === 'android' && /Android/i.test(ua))) {
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
  // Twinkling stars and the A.S.T.R.O AI mark: a glowing core inside three crossed orbits, with
  // satellites that pass in front of and behind it. Pauses off-screen; one still frame for reduced motion.
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
    let TILT = -0.32;
    // The 1.5 icon: a flat orbit and two crossed at about ±60°, with four satellites.
    const ORBITS = [-0.12, 1.02, -1.26];
    const SATS = [[0, 0.42, 0.9], [1, 0.31, 2.6], [2, 0.36, 4.4], [1, 0.31, 5.7]]; // [orbit, speed, phase]

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
      const sat = SATS.map(([o, v, ph]) => [o, reduced ? ph : t * v + ph]);
      for (const front of [false, true]) {
        if (front) core(g, t);
        ORBITS.forEach((tilt, i) => { TILT = tilt; ring(g, front, 1, i ? 0.62 : 0.85, 1.3); });
        for (const [o, a] of sat) if ((Math.sin(a) > 0) === front) { TILT = ORBITS[o]; moon(g, a); }
      }
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

  /* ------------------------------------------------------------ account link (fixed: $$ not $) */
  try { if (localStorage.getItem('astro.session')) $$('[data-account-link]').forEach((a) => { a.textContent = 'Account'; }); } catch {}

  /* ------------------------------------------------------------ tiny JS highlighter (for the demos) */
  const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const KW = /^(import|from|export|function|const|let|return|if|new|await|async|default|true|false|null|test|expect)$/;
  function hl(src) {
    const re = /(\/\/.*$)|('(?:[^'\\]|\\.)*'?|`[^`]*`?)|([A-Za-z_$][\w$]*)|(\d[\d_]*)/g;
    let out = '', last = 0, m;
    while ((m = re.exec(src))) {
      out += esc(src.slice(last, m.index)); last = re.lastIndex;
      if (m[1]) out += `<span class="c">${esc(m[1])}</span>`;
      else if (m[2]) out += `<span class="s">${esc(m[2])}</span>`;
      else if (m[3]) out += KW.test(m[3]) ? `<span class="k">${m[3]}</span>` : src[re.lastIndex] === '(' ? `<span class="fn">${m[3]}</span>` : esc(m[3]);
      else out += `<span class="n">${m[4]}</span>`;
    }
    return out + esc(src.slice(last));
  }
  $$('[data-hl]').forEach((el) => { el.innerHTML = el.textContent.split('\n').map(hl).join('\n'); });

  /* ------------------------------------------------------------ the IDE hero: a scripted agent run */
  function ide(root) {
    const q = (s) => root.querySelector(s);
    const treeEl = q('[data-ide-tree]'), tabsEl = q('[data-ide-tabs]'), codeEl = q('[data-ide-code]'), termEl = q('[data-ide-term]');
    const logEl = q('[data-ide-log]'), statusEl = q('[data-ide-status]'), liveEl = q('[data-ide-live]'), modeEl = q('[data-ide-mode-label]');
    const modeBtns = $$('[data-mode]', root);
    const FILES = {
      server: { name: 'server.js', path: 'src/server.js', lines: [
        "import express from 'express';",
        "import { todos } from './routes/todos.js';",
        '',
        'const app = express();',
        'app.use(express.json());',
        "app.use('/todos', todos);",
        '',
        "app.listen(3000, () => console.log('API on :3000'));",
      ] },
      limit: { name: 'limit.js', path: 'src/limit.js', lines: [
        '// Fixed-window rate limit: `max` requests per client per window.',
        "export function limit({ max = 100, windowMs = 60_000 } = {}) {",
        '  const hits = new Map();',
        '  return (req, res, next) => {',
        '    const now = Date.now();',
        '    const w = hits.get(req.ip) ?? { n: 0, start: now };',
        '    if (now - w.start > windowMs) { w.n = 0; w.start = now; }',
        '    w.n += 1;',
        '    hits.set(req.ip, w);',
        '    if (w.n > max) {',
        "      return res.status(429).json({ error: 'Too many requests' });",
        '    }',
        '    next();',
        '  };',
        '}',
      ] },
    };
    const TREE = [
      ['dir', 'todo-api', 0, '▾'], ['dir', '.claude', 1, '▸'], ['dir', 'src', 1, '▾'],
      ['js', 'routes/todos.js', 2, 'todos'], ['js', 'server.js', 2, 'server'], ['js', 'limit.js', 2, 'limit'],
      ['dir', 'test', 1, '▾'], ['js', 'todos.test.js', 2, 'test'],
      ['md', 'AGENTS.md', 1, 'agents'], ['json', '.mcp.json', 1, 'mcp'], ['json', 'package.json', 1, 'pkg'],
    ];
    let st, runId = 0, mode = 'ask', speed = 1, visible = false, loopTimer = 0;
    const STOP = {};
    const sleep = (ms, id) => new Promise((res, rej) => setTimeout(() => (id === runId ? res() : rej(STOP)), ms * speed));

    const fresh = () => ({
      active: 'server', tabs: ['server'], added: new Set(), modified: new Set(), showLimit: false,
      buf: { server: FILES.server.lines.slice(), limit: [] }, addLines: { server: new Set(), limit: new Set() }, cur: -1,
    });
    function paintTree() {
      treeEl.innerHTML = '<div class="th"><span>Explorer</span><span>todo-api</span></div>' + TREE.filter((t) => t[3] !== 'limit' || st.showLimit).map(([k, n, d, id]) => {
        const dir = k === 'dir';
        const flag = st.added.has(id) ? '<em class="a">A</em>' : st.modified.has(id) ? '<em class="m">M</em>' : '';
        return `<div class="f ${k} d${d}${id === st.active ? ' on' : ''}${id === 'limit' && st.flash ? ' new' : ''}">${dir ? `<i>${id}</i>` : '<i></i>'}<span>${esc(n)}</span>${flag}</div>`;
      }).join('');
    }
    function paintTabs() {
      tabsEl.innerHTML = st.tabs.map((id) => `<span class="ide-tab${id === st.active ? ' on' : ''}">${FILES[id].name}${st.added.has(id) ? ' <span style="color:var(--green)">A</span>' : st.modified.has(id) ? ' <span style="color:var(--amber)">M</span>' : ''}<span class="x">×</span></span>`).join('');
    }
    function paintCode() {
      const lines = st.buf[st.active], add = st.addLines[st.active];
      codeEl.innerHTML = lines.map((l, i) => `<div class="ln${add.has(i) ? ' add' : ''}${i === st.cur ? ' cur' : ''}" data-n="${i + 1}">${hl(l)}</div>`).join('') || '<div class="ln cur" data-n="1"></div>';
      const c = codeEl.querySelector('.cur');
      if (c) { const top = c.offsetTop - codeEl.clientHeight + 40; codeEl.scrollTop = Math.max(0, top); } else codeEl.scrollTop = 0;
    }
    const paint = () => { paintTree(); paintTabs(); paintCode(); };
    function open(id) { if (!st.tabs.includes(id)) st.tabs.push(id); st.active = id; st.cur = -1; paint(); }
    function status(text, kind) { statusEl.textContent = text; liveEl.className = 'live' + (kind ? ' ' + kind : ''); }
    function term(html) {
      const d = document.createElement('div'); d.innerHTML = html || ' '; termEl.appendChild(d);
      while (termEl.children.length > 7) termEl.firstChild.remove();
    }
    function add(cls, html) {
      const d = document.createElement('div'); d.className = 'ag-item ' + cls; d.innerHTML = html; logEl.appendChild(d);
      while (logEl.children.length > 14) logEl.firstChild.remove();
      return d;
    }
    function tool(icon, label, extra = '') {
      const d = add('ag-tool', `<span class="ic">${icon}</span><span>${label}</span>${extra}<span class="st run"></span>`);
      return { el: d, ok: (txt = '✓') => { const s = d.querySelector('.st'); s.className = 'st ok'; s.textContent = txt; }, wait: () => { const s = d.querySelector('.st'); s.className = 'st wait'; s.textContent = '?'; } };
    }
    let todoEl = null;
    function todo(i, state) { const li = todoEl && todoEl.querySelectorAll('li')[i]; if (!li) return; li.className = state; li.querySelector('i').textContent = state === 'done' ? '✓' : ''; }

    async function typeInto(id, lines, rid) {
      const buf = st.buf[id];
      for (let i = 0; i < lines.length; i++) {
        buf.push(''); st.cur = buf.length - 1; st.addLines[id].add(buf.length - 1);
        const full = lines[i];
        const step = Math.max(2, Math.ceil(full.length / 14));
        for (let c = 0; c <= full.length; c += step) { buf[buf.length - 1] = full.slice(0, c); paintCode(); if (speed) await sleep(14, rid); }
        buf[buf.length - 1] = full; paintCode();
        if (speed) await sleep(40, rid);
      }
      st.cur = -1; paintCode();
    }

    async function run(m) {
      const rid = ++runId; mode = m; clearTimeout(loopTimer);
      modeBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.mode === m)));
      if (modeEl) modeEl.textContent = { plan: 'Plan · read-only', ask: 'Ask · approve writes', auto: 'Auto · checkpoint saved' }[m];
      st = fresh(); logEl.innerHTML = ''; termEl.innerHTML = ''; todoEl = null;
      term('<span class="p">~/todo-api $</span> <span class="dim">npm run dev</span>'); term('<span class="dim">API on :3000</span>');
      paint(); status('Ready', '');
      try {
        await sleep(500, rid);
        status(m === 'plan' ? 'Planning' : 'Working', 'busy');
        add('ag-user', (m === 'plan' ? '<span class="mono" style="color:var(--peach)">/plan</span> ' : '') + 'Add rate limiting to the API, then run the tests.');
        await sleep(800, rid);
        add('ag-note', '<span>AGENTS.md</span><span>skill · api-tests</span><span>MCP · project-notes</span>');
        await sleep(700, rid);
        add('ag-say', m === 'plan' ? "Looking around first. Plan mode is read-only, so I won't change anything." : "I'll add a small limiter middleware, wire it into the server and cover it with a test.");
        await sleep(600, rid);
        if (m !== 'plan') {
          todoEl = add('ag-todo', '<h5>Todo</h5><ul><li><i></i>Read the server setup</li><li><i></i>Create src/limit.js</li><li><i></i>Use it in src/server.js</li><li><i></i>Run the test suite</li></ul>');
          todo(0, 'doing'); await sleep(500, rid);
        }
        let t = tool('R', 'Read <b>src/server.js</b>'); open('server'); await sleep(800, rid); t.ok(); todo(0, 'done');
        if (m === 'plan') {
          t = tool('R', 'Read <b>src/routes/todos.js</b>'); await sleep(700, rid); t.ok();
          t = tool('⌕', 'Search <b>app.use</b> in src/'); await sleep(700, rid); t.ok('2');
          await sleep(400, rid);
          const p = add('ag-plan', '<h5>Plan · 4 steps · nothing changed</h5><ol><li>Create <code>src/limit.js</code>: a fixed-window limiter (100 req/min per IP)</li><li>Register it in <code>src/server.js</code> before the routes</li><li>Add a 429 test to <code>test/todos.test.js</code></li><li>Run <code>npm test</code></li></ol><button type="button">Run this plan in Ask mode</button>');
          p.querySelector('button').addEventListener('click', () => { speed = reduced ? 0 : 1; run('ask'); });
          status('Plan ready', ''); schedule(); return;
        }
        todo(1, 'doing');
        t = tool('W', 'Write <b>src/limit.js</b>', ' <span class="plus">+15</span>');
        await sleep(600, rid);
        if (m === 'ask') {
          t.wait(); status('Waiting for approval', 'wait');
          const ask = add('ag-ask', '<p>Allow the agent to create src/limit.js?</p><pre>+ src/limit.js   15 lines   new file</pre><div class="row"><button type="button">Allow once</button><button type="button">Allow all this run</button><button type="button">Deny</button></div>');
          await sleep(1700, rid);
          ask.querySelectorAll('button')[1].classList.add('hl');
          await sleep(650, rid);
          ask.remove(); status('Working', 'busy');
        }
        st.showLimit = true; st.flash = true; st.added.add('limit'); open('limit'); st.flash = false;
        await typeInto('limit', FILES.limit.lines, rid); t.ok(); todo(1, 'done');
        todo(2, 'doing');
        t = tool('E', 'Edit <b>src/server.js</b>', ' <span class="plus">+2</span>');
        open('server'); await sleep(500, rid);
        st.buf.server.splice(2, 0, "import { limit } from './limit.js';"); st.addLines.server = new Set([2]); st.cur = 2; paintCode(); await sleep(450, rid);
        st.buf.server.splice(6, 0, 'app.use(limit({ max: 100 }));'); st.addLines.server.add(6); st.cur = 6; paintCode();
        st.modified.add('server'); paintTree(); paintTabs(); await sleep(500, rid); st.cur = -1; paintCode(); t.ok(); todo(2, 'done');
        todo(3, 'doing');
        t = tool('$', 'Run <b>npm test</b>');
        term(''); term('<span class="p">~/todo-api $</span> <b>npm test</b>'); await sleep(700, rid);
        term('<span class="ok">PASS</span> test/todos.test.js'); await sleep(250, rid);
        term('  <span class="ok">✓</span> lists, creates and deletes todos <span class="dim">(12 ms)</span>'); await sleep(250, rid);
        term('  <span class="ok">✓</span> returns 429 after 100 requests <span class="dim">(41 ms)</span>'); await sleep(300, rid);
        term('<b>Tests:</b> <span class="ok">14 passed</span>, 14 total <span class="dim">· 0.9 s</span>');
        t.ok('14 ✓'); todo(3, 'done');
        await sleep(600, rid);
        add('ag-done', '<b>Done · 2 files changed · 14 tests pass</b>The review team checked the diff before finishing.<div class="row"><span>View diff</span><span>↶ Undo to checkpoint</span><span>Keep</span></div>');
        status('Done', ''); schedule();
      } catch (e) { if (e !== STOP) throw e; }
    }
    function schedule() { if (reduced) return; clearTimeout(loopTimer); loopTimer = setTimeout(() => { if (visible) run(mode); else schedule(); }, 9000); }

    modeBtns.forEach((b) => b.addEventListener('click', () => { speed = reduced ? 0 : 1; run(b.dataset.mode); }));
    const rp = q('[data-ide-replay]'); if (rp) rp.addEventListener('click', () => { speed = reduced ? 0 : 1; run(mode); });
    st = fresh(); paint(); term('<span class="p">~/todo-api $</span> <span class="dim">npm run dev</span>'); term('<span class="dim">API on :3000</span>');
    if (reduced) { speed = 0; run('ask'); return; }
    let started = false;
    if ('IntersectionObserver' in window) new IntersectionObserver((es) => {
      visible = es[0].isIntersecting;
      if (visible && !started) { started = true; run('ask'); }
    }, { threshold: 0.25 }).observe(root);
    else run('ask');
  }
  $$('[data-ide]').forEach(ide);

  /* ------------------------------------------------------------ theme swatches recolour every code sample */
  const THEME_VARS = ['--c-kw', '--c-str', '--c-fn', '--c-num', '--c-com', '--c-txt', '--c-bg'];
  $$('[data-theme-set]').forEach((b) => {
    const cols = b.dataset.themeSet.split(',');
    const sw = b.querySelector('i'); if (sw) sw.style.background = `linear-gradient(135deg, ${cols[6]} 50%, ${cols[0]} 50%)`;
    b.addEventListener('click', () => {
      THEME_VARS.forEach((v, i) => document.documentElement.style.setProperty(v, cols[i]));
      $$('[data-theme-set]').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
    });
  });

  /* ------------------------------------------------------------ slash-command palette filter */
  $$('[data-palette]').forEach((p) => {
    const input = p.querySelector('input'), items = $$('li[data-k]', p), none = p.querySelector('li.none');
    input.addEventListener('input', () => {
      const v = input.value.trim().replace(/^\//, '').toLowerCase();
      let n = 0;
      items.forEach((li) => { const on = !v || li.dataset.k.includes(v); li.hidden = !on; if (on) n++; });
      if (none) none.hidden = n > 0;
    });
  });

  void ROOT;

  /* ------------------------------------------------------------ beta / Pro waitlist forms
     Insert-only table: the public key can add a row but never read rows back. No IP address is stored. */
  const REPORTS = 'https://vgtucdfretcvojcfvbzk.supabase.co/rest/v1/astro_reports';
  const PUBLIC_KEY = 'sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol';
  $$('form[data-report]').forEach((form) => {
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const out = $('.form-out', form), btn = $('button[type=submit]', form);
      const mail = form.elements.contact.value.trim();
      if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(mail)) { out.textContent = 'Please enter a valid e-mail address.'; form.elements.contact.focus(); return; }
      const picks = $$('input[name=device]:checked', form).map((x) => x.value);
      const row = { kind: form.dataset.report, category: picks.join(',').slice(0, 40), message: (form.elements.message.value.trim() || '(no message)').slice(0, 4000), detail: picks.join(', '), contact: mail.slice(0, 200), platform: (navigator.userAgentData && navigator.userAgentData.platform || navigator.platform || '').slice(0, 80), lang: (navigator.language || '').slice(0, 40), source: 'website' };
      btn.disabled = true; out.textContent = 'Sending…';
      try {
        const r = await fetch(REPORTS, { method: 'POST', headers: { apikey: PUBLIC_KEY, 'Content-Type': 'application/json', Prefer: 'return=minimal' }, body: JSON.stringify(row) });
        if (!r.ok) throw new Error(r.status === 400 ? 'too many signups right now, please try again later' : 'HTTP ' + r.status);
        form.reset(); out.textContent = form.dataset.report === 'beta' ? 'Thank you! You are on the beta list. We will e-mail you when the next test build is ready.' : 'Thank you! You are on the waitlist.';
      } catch (err) { out.textContent = 'Could not send it (' + err.message + '). Please try again in a minute.'; }
      btn.disabled = false;
    });
  });
})();

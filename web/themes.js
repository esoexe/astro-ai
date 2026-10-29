// A.S.T.R.O AI themes: built-in colour palettes plus VS Code colour themes from Open VSX, applied to the whole app
// (chat, workspace, agent, editor and terminal). A palette is ~20 core colours; everything else is derived from them.
// The light/dark switch keeps working: you pick one palette for dark mode and one for light mode.
(() => {
  'use strict';
  const KEY = { dark: 'astro_palette_dark', light: 'astro_palette_light', cache: 'astro_palette_vars' };

  // ---------- built-in palettes (official colours of each theme) ----------
  // f frame, b background, s/s2/s3 surfaces, t/t2/t3 text, a accent, oa text on accent, d/o/w danger/ok/warn,
  // kw str num com fn tag attr = syntax colours.
  const P = (id, name, base, c, extra) => ({ id, name, base, c, ...(extra || {}) });
  const BUILTIN = [
    P('astro', 'A.S.T.R.O Teal', 'dark', null, { note: 'Default' }),
    P('peach', 'Astro Peach', 'dark', { f: '#050506', b: '#0a0a0c', s: '#121215', s2: '#19191d', s3: '#24242a', t: '#efeae6', t2: '#bdb5ae', t3: '#7f7873', a: '#f3a679', oa: '#2a1406', d: '#ff8a80', o: '#9fd89a', w: '#f6c177', kw: '#ff9e7a', str: '#c3e88d', num: '#f7c47a', com: '#6e6a67', fn: '#8fc8ff', tag: '#ff8a80', attr: '#f3c58a' }),
    P('tokyo-night', 'Tokyo Night', 'dark', { f: '#16161e', b: '#1a1b26', s: '#1f2335', s2: '#24283b', s3: '#2f3549', t: '#c0caf5', t2: '#a9b1d6', t3: '#565f89', a: '#7aa2f7', oa: '#16161e', d: '#f7768e', o: '#9ece6a', w: '#e0af68', kw: '#bb9af7', str: '#9ece6a', num: '#ff9e64', com: '#565f89', fn: '#7aa2f7', tag: '#f7768e', attr: '#e0af68' }),
    P('catppuccin-mocha', 'Catppuccin Mocha', 'dark', { f: '#11111b', b: '#1e1e2e', s: '#24243a', s2: '#313244', s3: '#45475a', t: '#cdd6f4', t2: '#bac2de', t3: '#7f849c', a: '#cba6f7', oa: '#1e1e2e', d: '#f38ba8', o: '#a6e3a1', w: '#f9e2af', kw: '#cba6f7', str: '#a6e3a1', num: '#fab387', com: '#6c7086', fn: '#89b4fa', tag: '#f38ba8', attr: '#f9e2af' }),
    P('dracula', 'Dracula', 'dark', { f: '#21222c', b: '#282a36', s: '#2e303e', s2: '#343746', s3: '#44475a', t: '#f8f8f2', t2: '#c9cbd6', t3: '#6272a4', a: '#bd93f9', oa: '#21222c', d: '#ff5555', o: '#50fa7b', w: '#f1fa8c', kw: '#ff79c6', str: '#f1fa8c', num: '#bd93f9', com: '#6272a4', fn: '#50fa7b', tag: '#ff79c6', attr: '#ffb86c' }),
    P('nord', 'Nord', 'dark', { f: '#242933', b: '#2e3440', s: '#3b4252', s2: '#434c5e', s3: '#4c566a', t: '#eceff4', t2: '#d8dee9', t3: '#8a93a6', a: '#88c0d0', oa: '#2e3440', d: '#bf616a', o: '#a3be8c', w: '#ebcb8b', kw: '#81a1c1', str: '#a3be8c', num: '#b48ead', com: '#616e88', fn: '#88c0d0', tag: '#81a1c1', attr: '#8fbcbb' }),
    P('one-dark', 'One Dark', 'dark', { f: '#21252b', b: '#282c34', s: '#2c313a', s2: '#333842', s3: '#3e4451', t: '#dcdfe4', t2: '#abb2bf', t3: '#7f848e', a: '#61afef', oa: '#1e2127', d: '#e06c75', o: '#98c379', w: '#e5c07b', kw: '#c678dd', str: '#98c379', num: '#d19a66', com: '#5c6370', fn: '#61afef', tag: '#e06c75', attr: '#d19a66' }),
    P('gruvbox-dark', 'Gruvbox Dark', 'dark', { f: '#1d2021', b: '#282828', s: '#32302f', s2: '#3c3836', s3: '#504945', t: '#ebdbb2', t2: '#d5c4a1', t3: '#928374', a: '#fe8019', oa: '#1d2021', d: '#fb4934', o: '#b8bb26', w: '#fabd2f', kw: '#fb4934', str: '#b8bb26', num: '#d3869b', com: '#928374', fn: '#fabd2f', tag: '#8ec07c', attr: '#fabd2f' }),
    P('rose-pine', 'Rosé Pine', 'dark', { f: '#12101b', b: '#191724', s: '#1f1d2e', s2: '#26233a', s3: '#403d52', t: '#e0def4', t2: '#908caa', t3: '#6e6a86', a: '#ebbcba', oa: '#191724', d: '#eb6f92', o: '#9ccfd8', w: '#f6c177', kw: '#c4a7e7', str: '#f6c177', num: '#ebbcba', com: '#6e6a86', fn: '#ebbcba', tag: '#9ccfd8', attr: '#c4a7e7' }),
    P('github-dark', 'GitHub Dark', 'dark', { f: '#010409', b: '#0d1117', s: '#161b22', s2: '#21262d', s3: '#30363d', t: '#e6edf3', t2: '#b1bac4', t3: '#7d8590', a: '#2f81f7', oa: '#ffffff', d: '#f85149', o: '#3fb950', w: '#d29922', kw: '#ff7b72', str: '#a5d6ff', num: '#79c0ff', com: '#8b949e', fn: '#d2a8ff', tag: '#7ee787', attr: '#79c0ff' }),
    P('solarized-dark', 'Solarized Dark', 'dark', { f: '#00212b', b: '#002b36', s: '#073642', s2: '#0b3f4c', s3: '#194753', t: '#eee8d5', t2: '#93a1a1', t3: '#657b83', a: '#268bd2', oa: '#fdf6e3', d: '#dc322f', o: '#859900', w: '#b58900', kw: '#859900', str: '#2aa198', num: '#d33682', com: '#586e75', fn: '#268bd2', tag: '#268bd2', attr: '#b58900' }),
    P('midnight', 'Midnight (OLED)', 'dark', { f: '#000000', b: '#000000', s: '#0b0b0d', s2: '#141417', s3: '#1f1f23', t: '#f2f2f2', t2: '#b8b8bd', t3: '#76767d', a: '#a78bfa', oa: '#0b0618', d: '#ff6b6b', o: '#4ade80', w: '#fbbf24', kw: '#c4b5fd', str: '#86efac', num: '#fdba74', com: '#62626a', fn: '#7dd3fc', tag: '#fda4af', attr: '#fcd34d' }),
    P('astro-light', 'A.S.T.R.O Light', 'light', null, { note: 'Default' }),
    P('peach-light', 'Astro Peach Light', 'light', { f: '#f1ebe6', b: '#fcf8f5', s: '#ffffff', s2: '#f6efe9', s3: '#ece3dc', t: '#1c1714', t2: '#51473f', t3: '#8c8178', a: '#c9693a', oa: '#ffffff', d: '#b5402e', o: '#3f7d3a', w: '#a1650f', kw: '#b0457c', str: '#3f7d3a', num: '#b85a1d', com: '#9a8f86', fn: '#2f6eb5', tag: '#b5402e', attr: '#a1650f' }),
    P('github-light', 'GitHub Light', 'light', { f: '#f6f8fa', b: '#ffffff', s: '#ffffff', s2: '#f6f8fa', s3: '#eaeef2', t: '#1f2328', t2: '#59636e', t3: '#818b98', a: '#0969da', oa: '#ffffff', d: '#d1242f', o: '#1a7f37', w: '#9a6700', kw: '#cf222e', str: '#0a3069', num: '#0550ae', com: '#6e7781', fn: '#8250df', tag: '#116329', attr: '#0550ae' }),
    P('catppuccin-latte', 'Catppuccin Latte', 'light', { f: '#dce0e8', b: '#eff1f5', s: '#f7f8fb', s2: '#e6e9ef', s3: '#ccd0da', t: '#4c4f69', t2: '#5c5f77', t3: '#8c8fa1', a: '#8839ef', oa: '#ffffff', d: '#d20f39', o: '#40a02b', w: '#df8e1d', kw: '#8839ef', str: '#40a02b', num: '#fe640b', com: '#8c8fa1', fn: '#1e66f5', tag: '#d20f39', attr: '#df8e1d' }),
    P('solarized-light', 'Solarized Light', 'light', { f: '#eee8d5', b: '#fdf6e3', s: '#fffbef', s2: '#f5efdc', s3: '#eee8d5', t: '#073642', t2: '#586e75', t3: '#93a1a1', a: '#268bd2', oa: '#ffffff', d: '#dc322f', o: '#859900', w: '#b58900', kw: '#859900', str: '#2aa198', num: '#d33682', com: '#93a1a1', fn: '#268bd2', tag: '#268bd2', attr: '#b58900' }),
    P('rose-pine-dawn', 'Rosé Pine Dawn', 'light', { f: '#f2e9e1', b: '#faf4ed', s: '#fffaf3', s2: '#f2e9e1', s3: '#dfdad9', t: '#575279', t2: '#797593', t3: '#9893a5', a: '#b4637a', oa: '#ffffff', d: '#b4637a', o: '#286983', w: '#ea9d34', kw: '#286983', str: '#ea9d34', num: '#d7827e', com: '#9893a5', fn: '#d7827e', tag: '#56949f', attr: '#907aa9' }),
    P('gruvbox-light', 'Gruvbox Light', 'light', { f: '#ebdbb2', b: '#fbf1c7', s: '#f9f5d7', s2: '#f2e5bc', s3: '#ebdbb2', t: '#3c3836', t2: '#504945', t3: '#928374', a: '#af3a03', oa: '#ffffff', d: '#9d0006', o: '#79740e', w: '#b57614', kw: '#9d0006', str: '#79740e', num: '#8f3f71', com: '#928374', fn: '#b57614', tag: '#427b58', attr: '#b57614' }),
  ];
  const DEFAULT = { dark: 'astro', light: 'astro-light' };

  // ---------- colour helpers ----------
  function parse(c) {
    if (typeof c !== 'string') return null;
    let h = c.trim().replace(/^#/, '');
    if (/^[0-9a-f]{3,4}$/i.test(h)) h = h.split('').map((x) => x + x).join('');
    if (!/^[0-9a-f]{6}([0-9a-f]{2})?$/i.test(h)) return null;
    return { r: parseInt(h.slice(0, 2), 16), g: parseInt(h.slice(2, 4), 16), b: parseInt(h.slice(4, 6), 16), a: h.length === 8 ? parseInt(h.slice(6, 8), 16) / 255 : 1 };
  }
  const hex = (o) => '#' + [o.r, o.g, o.b].map((v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, '0')).join('');
  function mix(a, b, t) { const x = parse(a); const y = parse(b); if (!x || !y) return a || b; return hex({ r: x.r + (y.r - x.r) * t, g: x.g + (y.g - x.g) * t, b: x.b + (y.b - x.b) * t }); }
  // A colour with alpha (VS Code themes use #rrggbbaa) flattened onto a background.
  function flat(c, bg) { const x = parse(c); if (!x) return null; if (x.a >= 1 || !bg) return hex(x); return mix(bg, hex(x), x.a); }
  function rgba(c, a) { const x = parse(c); return x ? `rgba(${x.r}, ${x.g}, ${x.b}, ${a})` : c; }
  function lum(c) { const x = parse(c); if (!x) return 0; const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }; return 0.2126 * f(x.r) + 0.7152 * f(x.g) + 0.0722 * f(x.b); }
  function contrast(a, b) { const x = lum(a); const y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); }

  // ---------- palette -> CSS variables ----------
  function derive(c, base) {
    const dark = base !== 'light';
    const v = {};
    const oa = c.oa || (lum(c.a) > 0.36 ? '#111111' : '#ffffff');
    Object.assign(v, {
      '--frame': c.f || mix(c.b, '#000000', dark ? 0.35 : 0.06), '--bg': c.b, '--rail': c.b,
      '--surface': c.s, '--surface-2': c.s2, '--surface-3': c.s3,
      '--hover': rgba(c.t, dark ? 0.06 : 0.05), '--active': rgba(c.a, 0.14), '--active-text': mix(c.a, dark ? '#ffffff' : '#000000', 0.3),
      '--text': c.t, '--text-2': c.t2, '--text-3': c.t3,
      '--border': rgba(c.t, dark ? 0.09 : 0.11), '--border-strong': rgba(c.t, dark ? 0.15 : 0.19),
      '--accent': c.a, '--accent-soft': rgba(c.a, 0.14), '--on-accent': oa,
      '--user-bubble': c.s2, '--code-bg': dark ? mix(c.b, '#000000', 0.22) : mix(c.b, c.t, 0.03), '--code-head': c.s, '--code-text': c.t,
      '--menu-bg': c.s, '--glow': `0 0 0 4px ${rgba(c.a, 0.15)}`,
      '--danger': c.d, '--ok': c.o, '--warn': c.w, '--grad-end': c.b,
      '--grad': `linear-gradient(135deg, ${mix(c.a, '#ffffff', 0.12)} 0%, ${c.a} 100%)`,
      '--grad-soft': `linear-gradient(135deg, ${rgba(c.a, 0.10)}, ${rgba(c.a, 0.06)})`,
      '--g1': c.a, '--g2': mix(c.a, '#000000', 0.25), '--g3': c.w, '--g4': mix(c.a, '#ffffff', 0.3), '--gold': c.w,
      '--tk-kw': c.kw, '--tk-str': c.str, '--tk-num': c.num, '--tk-com': c.com, '--tk-fn': c.fn, '--tk-tag': c.tag, '--tk-attr': c.attr,
      '--cm-fg': c.t, '--cm-caret': c.a, '--cm-sel': rgba(c.a, dark ? 0.24 : 0.18), '--cm-gutter': c.t3, '--cm-active': rgba(c.t, 0.035),
      '--cm-kw': c.kw, '--cm-str': c.str, '--cm-num': c.num, '--cm-com': c.com, '--cm-fn': c.fn, '--cm-tag': c.tag, '--cm-meta': c.attr,
      '--cm-type': c.type || c.attr, '--cm-prop': c.prop || mix(c.t, c.fn, 0.55), '--cm-op': c.t2, '--cm-def': c.t, '--cm-err': c.d,
      '--cm-bracket': rgba(c.a, 0.18), '--cm-bracket-line': rgba(c.a, 0.55), '--cm-selmatch': rgba(c.a, 0.12),
      '--cm-pop': c.s, '--cm-pop-sel': rgba(c.a, 0.16), '--cm-line': rgba(c.t, dark ? 0.09 : 0.11), '--cm-gutter-bg': c.s,
      '--term-bg': c.termBg || (dark ? mix(c.b, '#000000', 0.22) : mix('#121417', c.a, 0.05)),
      '--term-fg': c.termFg || (dark ? c.t : '#e3e6e8'), '--term-cursor': c.a, '--term-sel': rgba(c.a, 0.3),
    });
    for (const k of Object.keys(v)) if (!v[k]) delete v[k];
    return v;
  }

  // ---------- VS Code colour theme (Open VSX) -> palette ----------
  const SCOPES = {
    kw: ['keyword.control', 'keyword', 'storage.type', 'storage'], str: ['string.quoted', 'string'], num: ['constant.numeric', 'constant'],
    com: ['comment.line', 'comment'], fn: ['entity.name.function', 'support.function'], tag: ['entity.name.tag'],
    attr: ['entity.other.attribute-name', 'variable.parameter', 'constant.language'], type: ['entity.name.type', 'support.type', 'entity.name.class'],
    prop: ['variable.other.property', 'support.type.property-name', 'meta.object-literal.key'],
  };
  function tokenColor(theme, want) {
    let best = null; let bestLen = -1;
    for (const rule of theme.tokenColors || []) {
      const fg = rule && rule.settings && rule.settings.foreground; if (!fg) continue;
      const scopes = Array.isArray(rule.scope) ? rule.scope : String(rule.scope || '').split(',');
      for (let sc of scopes) { sc = String(sc).trim(); if (!sc || sc.includes(' ')) continue; if ((want === sc || want.startsWith(`${sc}.`)) && sc.length > bestLen) { best = fg; bestLen = sc.length; } }
    }
    return best;
  }
  function fromVsx(theme) {
    const k = theme.colors || {};
    const pick = (...keys) => { for (const x of keys) if (k[x]) return k[x]; return null; };
    const bg0 = flat(pick('editor.background'), null);
    // Light or dark goes by the real background (VS Code marks light themes "vs", which older installs mislabelled).
    const base = bg0 ? (lum(bg0) > 0.36 ? 'light' : 'dark') : (theme.type === 'light' ? 'light' : 'dark'); const dark = base === 'dark';
    const b = bg0 || (dark ? '#1e1e1e' : '#ffffff');
    const F = (c) => flat(c, b);
    const shift = (t) => mix(b, dark ? '#ffffff' : '#000000', t);
    const t = F(pick('editor.foreground', 'foreground')) || (dark ? '#d4d4d4' : '#1f1f1f');
    let a = F(pick('button.background', 'focusBorder', 'activityBarBadge.background', 'textLink.foreground', 'progressBar.background'));
    if (!a || contrast(a, b) < 1.8) a = F(pick('textLink.foreground', 'activityBarBadge.background', 'editorCursor.foreground')) || (dark ? '#4fc1ff' : '#0066b8');
    const tok = (key, fb) => { for (const s of SCOPES[key]) { const c = tokenColor(theme, s); if (c) return F(c); } return fb; };
    const c = {
      f: F(pick('activityBar.background', 'titleBar.activeBackground', 'sideBar.background')) || shift(dark ? -0.0 : 0.04),
      b,
      s: F(pick('sideBar.background', 'editorWidget.background', 'panel.background')) || shift(0.04),
      s2: F(pick('input.background', 'dropdown.background', 'editorWidget.background')) || shift(0.08),
      s3: F(pick('list.hoverBackground', 'list.inactiveSelectionBackground')) || shift(0.14),
      t, t2: mix(t, b, 0.22), t3: F(pick('descriptionForeground', 'editorLineNumber.foreground')) || mix(t, b, 0.5),
      a, oa: F(pick('button.foreground')),
      d: F(pick('errorForeground', 'editorError.foreground', 'terminal.ansiRed')) || (dark ? '#f48771' : '#c72e0f'),
      o: F(pick('gitDecoration.addedResourceForeground', 'terminal.ansiGreen')) || (dark ? '#89d185' : '#388a34'),
      w: F(pick('editorWarning.foreground', 'terminal.ansiYellow')) || (dark ? '#cca700' : '#bf8803'),
      termBg: F(pick('terminal.background', 'panel.background')), termFg: F(pick('terminal.foreground')),
    };
    // Status colours must look like status colours, and muted text must be dimmer than text.
    const sat = (x) => { const o = parse(x); if (!o) return 0; const mx = Math.max(o.r, o.g, o.b); const mn = Math.min(o.r, o.g, o.b); return mx ? (mx - mn) / mx : 0; };
    if (sat(c.d) < 0.3) c.d = F(pick('terminal.ansiRed', 'editorError.foreground')) || (dark ? '#f48771' : '#c72e0f');
    if (sat(c.d) < 0.3) c.d = dark ? '#f48771' : '#c72e0f';
    if (sat(c.o) < 0.25) c.o = dark ? '#89d185' : '#388a34';
    if (sat(c.w) < 0.25) c.w = dark ? '#cca700' : '#bf8803';
    if (contrast(c.t3, b) > contrast(c.t, b) * 0.8) c.t3 = mix(c.t, b, 0.45);
    if (c.oa && contrast(c.oa, a) < 2.5) c.oa = null;
    // Surfaces must differ from the background a little, or cards disappear.
    if (contrast(c.s, b) < 1.04) c.s = shift(0.035);
    if (contrast(c.s2, b) < 1.08) c.s2 = shift(0.07);
    if (contrast(c.s3, b) < 1.12) c.s3 = shift(0.13);
    Object.assign(c, { kw: tok('kw', a), str: tok('str', c.o), num: tok('num', c.w), com: tok('com', c.t3), fn: tok('fn', a), tag: tok('tag', c.d), attr: tok('attr', c.w), type: tok('type', null), prop: tok('prop', null) });
    return { base, c };
  }

  // ---------- state ----------
  let vsx = []; // [{ id: 'vsx:<path>', name, base, c }]
  const ls = { get: (k) => { try { return localStorage.getItem(k); } catch (_e) { return null; } }, set: (k, v) => { try { localStorage.setItem(k, v); } catch (_e) { /* private mode */ } } };
  const all = () => BUILTIN.concat(vsx);
  const find = (id) => all().find((p) => p.id === id);
  const mode = () => (document.documentElement.dataset.theme === 'light' ? 'light' : 'dark');
  const chosen = (m) => ls.get(KEY[m]) || DEFAULT[m];
  let applied = {};

  function apply() {
    const m = mode(); const id = chosen(m); const p = find(id);
    const root = document.documentElement.style;
    for (const k of Object.keys(applied)) root.removeProperty(k);
    applied = {};
    document.documentElement.dataset.palette = p ? p.id.replace(/[^a-z0-9-]/gi, '_') : DEFAULT[m];
    if (p && p.c) { applied = derive(p.c, p.base); for (const [k, v] of Object.entries(applied)) root.setProperty(k, v); }
    else if (!p && id.startsWith('vsx:')) { // not scanned yet: keep the cached colours until the theme list arrives
      try { const cache = JSON.parse(ls.get(`${KEY.cache}_${m}`) || 'null'); if (cache && cache.id === id) { applied = cache.vars; for (const [k, v] of Object.entries(applied)) root.setProperty(k, v); } } catch (_e) { /* ignore */ }
    }
    ls.set(`${KEY.cache}_${m}`, JSON.stringify({ id, vars: applied }));
    window.dispatchEvent(new CustomEvent('astro-theme', { detail: { mode: m, id, palette: p || null } }));
  }
  function set(id) {
    const p = find(id); if (!p) return false;
    ls.set(KEY[p.base], id);
    if (p.base !== mode() && window.AstroApp && typeof window.AstroApp.setTheme === 'function') window.AstroApp.setTheme(p.base); // switches mode, which re-applies
    else apply();
    return true;
  }
  // Open VSX themes come from the extension scan (dev.js): [{ path, theme: { name, type, colors, tokenColors } }]
  function setVsx(list) {
    vsx = (list || []).filter((x) => x && x.theme && (x.theme.colors || x.theme.tokenColors)).map((x) => {
      const { base, c } = fromVsx(x.theme);
      return { id: `vsx:${x.path}`, name: x.theme.name || x.path.split('/').pop().replace(/\.json$/, ''), base, c, vsx: true, source: x.theme.source || '' };
    });
    apply();
  }
  const activeVsxPath = () => { const id = chosen(mode()); return id.startsWith('vsx:') ? id.slice(4) : ''; };
  // Swatch colours for pickers (default palettes read the live stylesheet values).
  function swatch(p) {
    if (p.c) return { bg: p.c.b, s: p.c.s2, t: p.c.t, a: p.c.a, k: [p.c.kw, p.c.str, p.c.fn, p.c.num] };
    return p.base === 'light' ? { bg: '#fbfbfa', s: '#f3f4f3', t: '#141716', a: '#0d9488', k: ['#a626a4', '#2f7d32', '#0369a1', '#b45309'] }
      : { bg: '#0d0e10', s: '#1a1d20', t: '#eceeed', a: '#2dd4bf', k: ['#c4a1ff', '#a5e075', '#7dd3fc', '#f5a97f'] };
  }

  window.AstroThemes = { list: all, builtin: BUILTIN, find, chosen, mode, apply, set, setVsx, fromVsx, derive, swatch, activeVsxPath, DEFAULT, _c: { mix, flat, contrast, lum } };
  apply();
})();

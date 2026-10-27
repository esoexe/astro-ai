/* Astro AI Console (account.html).
   Supabase Auth (email + GitHub) and the Astro API account endpoints, with plain fetch and no SDK.
   Only the public publishable key lives here. Provider keys are encrypted server-side and never come back to the browser. */
(() => {
  'use strict';
  const SB = 'https://vgtucdfretcvojcfvbzk.supabase.co';
  const PK = 'sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol';
  const FN = SB + '/functions/v1/astro-api/account';
  const STORE = 'astro.session';
  const PLAYKEY = 'astro.playkey';           // sessionStorage only: the Astro key the user pastes for Models/Playground
  const HOME = 'astro.console.home';
  const DAILY_LIMIT = 5000, MAX_KEYS = 10;
  const ROOT = document.body.dataset.root || '';

  /* ================================================================ helpers */
  const $ = (s, el = document) => el.querySelector(s);
  const h = (tag, attrs = {}, ...kids) => {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'value' && (tag === 'input' || tag === 'textarea' || tag === 'select')) el.value = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat(Infinity)) if (c != null && c !== false) el.append(c.nodeType ? c : document.createTextNode(String(c)));
    return el;
  };
  const ICONS = {
    overview: '<rect x="3" y="3" width="7" height="9" rx="1.5"/><rect x="14" y="3" width="7" height="5" rx="1.5"/><rect x="14" y="12" width="7" height="9" rx="1.5"/><rect x="3" y="16" width="7" height="5" rx="1.5"/>',
    key: '<circle cx="7.5" cy="15.5" r="5.5"/><path d="m21 2-9.6 9.6M15.5 7.5l3 3L22 7l-3-3"/>',
    plug: '<path d="M12 22v-5M9 8V2M15 8V2M18 8v5a6 6 0 0 1-12 0V8z"/>',
    box: '<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><path d="M3.3 7 12 12l8.7-5M12 22V12"/>',
    chart: '<path d="M3 3v18h18"/><path d="M7 16v-4M12 16V8M17 16v-7"/>',
    terminal: '<rect x="2" y="3" width="20" height="18" rx="2"/><path d="m7 9 3 3-3 3M13 15h4"/>',
    user: '<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/>',
    shield: '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>',
    db: '<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/><path d="M3 12c0 1.66 4 3 9 3s9-1.34 9-3"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    copy: '<rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>',
    check: '<path d="M20 6 9 17l-5-5"/>',
    dots: '<circle cx="12" cy="12" r="1.2"/><circle cx="19" cy="12" r="1.2"/><circle cx="5" cy="12" r="1.2"/>',
    search: '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>',
    info: '<circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/>',
    alert: '<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><path d="M12 9v4M12 17h.01"/>',
    x: '<path d="M18 6 6 18M6 6l12 12"/>',
    ext: '<path d="M15 3h6v6M10 14 21 3M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/>',
    download: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/>',
    zap: '<path d="M13 2 3 14h9l-1 8 10-12h-9l1-8z"/>',
    refresh: '<path d="M21 12a9 9 0 1 1-3-6.7L21 8"/><path d="M21 3v5h-5"/>',
    trash: '<path d="M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/>',
    send: '<path d="m22 2-7 20-4-9-9-4z"/><path d="M22 2 11 13"/>',
    mail: '<rect x="2" y="4" width="20" height="16" rx="2"/><path d="m22 7-10 6L2 7"/>',
    clock: '<circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>',
    lock: '<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
    code: '<path d="m16 18 6-6-6-6M8 6l-6 6 6 6"/>',
    monitor: '<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8M12 17v4"/>',
    logout: '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4M16 17l5-5-5-5M21 12H9"/>',
    spark: '<path d="M12 3l1.9 5.8L20 11l-6.1 2.2L12 19l-1.9-5.8L4 11l6.1-2.2z"/>',
    activity: '<path d="M22 12h-4l-3 9L9 3l-3 9H2"/>',
    globe: '<circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2a15.3 15.3 0 0 1 0 20M12 2a15.3 15.3 0 0 0 0 20"/>',
    eye: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  };
  const GH_PATH = 'M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z';
  const ico = (name) => {
    const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    s.setAttribute('aria-hidden', 'true');
    if (name === 'github') { s.setAttribute('viewBox', '0 0 16 16'); s.setAttribute('fill', 'currentColor'); s.innerHTML = `<path d="${GH_PATH}"/>`; }
    else { s.setAttribute('viewBox', '0 0 24 24'); s.innerHTML = ICONS[name] || ''; }
    return s;
  };
  const nf = (n) => Number(n || 0).toLocaleString('en-US');
  const fmtDate = (s, withTime) => s ? new Date(s).toLocaleString(undefined, withTime ? { year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' } : { year: 'numeric', month: 'short', day: 'numeric' }) : '—';
  const ago = (s) => {
    if (!s) return 'never';
    const d = (Date.now() - new Date(s).getTime()) / 1000;
    if (d < 45) return 'just now';
    if (d < 3600) return Math.round(d / 60) + ' min ago';
    if (d < 86400) return Math.round(d / 3600) + ' h ago';
    if (d < 86400 * 30) return Math.round(d / 86400) + ' d ago';
    return fmtDate(s);
  };
  const utcDay = (offset = 0) => new Date(Date.now() - offset * 864e5).toISOString().slice(0, 10);
  const debounce = (fn, ms = 150) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };

  /* toasts */
  const toasts = $('#toasts');
  const toast = (msg, bad) => {
    const t = h('div', { class: 'toast-i' + (bad ? ' bad' : ''), role: bad ? 'alert' : 'status' }, ico(bad ? 'alert' : 'check'), h('span', { text: msg }));
    toasts.append(t);
    setTimeout(() => { t.classList.add('out'); setTimeout(() => t.remove(), 250); }, bad ? 5200 : 3400);
  };

  /* clipboard */
  const copyText = async (text) => {
    try { await navigator.clipboard.writeText(text); return true; }
    catch {
      const t = h('textarea', { style: 'position:fixed;opacity:0' }); t.value = text; document.body.append(t); t.select();
      let ok = false; try { ok = document.execCommand('copy'); } catch {} t.remove(); return ok;
    }
  };
  const copyBtn = (get, label) => {
    const b = h('button', { type: 'button', class: label ? 'cx-btn cx-btn-outline cx-btn-sm' : 'icon-btn copy-i', 'aria-label': 'Copy to clipboard', title: 'Copy' }, ico('copy'), label || null);
    b.addEventListener('click', async () => {
      await copyText(typeof get === 'function' ? get() : get);
      b.replaceChildren(ico('check'), label ? 'Copied' : null);
      setTimeout(() => b.replaceChildren(ico('copy'), label || null), 1500);
    });
    return b;
  };
  const codeBlock = (text) => h('div', { class: 'code' }, h('pre', {}, h('code', { text })), copyBtn(() => text));
  const copyField = (text, big) => h('div', { class: 'copy-field' + (big ? ' big' : '') }, h('code', { text, title: text }), copyBtn(text));

  /* provider avatar: initials on a stable tint */
  const tint = (id) => { let x = 0; for (const c of id) x = (x * 31 + c.charCodeAt(0)) >>> 0; return x % 360; };
  const provLogo = (c, lg) => {
    const words = String(c.name || c.id).replace(/[^A-Za-z0-9 ]/g, ' ').trim().split(/\s+/);
    const ini = (words.length > 1 ? words[0][0] + words[1][0] : words[0].slice(0, 2)).replace(/^./, (m) => m.toUpperCase());
    const hue = tint(c.id || 'x');
    return h('span', { class: 'prov-logo' + (lg ? ' lg' : ''), 'aria-hidden': 'true', style: { background: `hsl(${hue} 35% 16%)`, color: `hsl(${hue} 80% 82%)`, borderColor: `hsl(${hue} 40% 26%)` } }, ini);
  };

  /* floating menus (avatar menu is static; row menus are built on demand) */
  let openFloat = null;
  const closeFloat = () => { if (openFloat) { openFloat.el.remove(); openFloat.anchor.setAttribute('aria-expanded', 'false'); openFloat = null; } };
  const floatMenu = (anchor, items) => {
    if (openFloat && openFloat.anchor === anchor) return closeFloat();
    closeFloat();
    const el = h('div', { class: 'menu', role: 'menu' }, items.filter(Boolean).map((it) => it === '-' ? h('hr') :
      h('button', { type: 'button', role: 'menuitem', class: it.danger ? 'danger' : null, onclick: () => { closeFloat(); it.run(); } }, it.icon ? ico(it.icon) : null, it.label)));
    document.body.append(el);
    const r = anchor.getBoundingClientRect(), w = el.offsetWidth, hh = el.offsetHeight;
    const top = r.bottom + 6 + hh > innerHeight ? Math.max(8, r.top - hh - 6) : r.bottom + 6;
    Object.assign(el.style, { position: 'fixed', top: top + 'px', left: Math.max(8, Math.min(innerWidth - w - 8, r.right - w)) + 'px', right: 'auto' });
    anchor.setAttribute('aria-expanded', 'true');
    openFloat = { el, anchor };
    el.querySelector('button')?.focus();
  };
  document.addEventListener('click', (e) => { if (openFloat && !openFloat.el.contains(e.target) && !openFloat.anchor.contains(e.target)) closeFloat(); });
  addEventListener('scroll', closeFloat, true);
  addEventListener('resize', closeFloat);

  /* modals */
  const modalRoot = $('#modal-root');
  const modal = ({ title, desc, body = [], foot = [], wide = false, onClose }) => {
    const prev = document.activeElement;
    const back = h('div', { class: 'modal-back' });
    const close = () => { back.remove(); document.removeEventListener('keydown', onKey); if (onClose) onClose(); if (prev && prev.focus) prev.focus(); };
    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); close(); }
      if (e.key === 'Tab') {
        const f = [...back.querySelectorAll('button,a[href],input,select,textarea')].filter((x) => !x.disabled && x.offsetParent);
        if (!f.length) return;
        if (e.shiftKey && document.activeElement === f[0]) { e.preventDefault(); f[f.length - 1].focus(); }
        else if (!e.shiftKey && document.activeElement === f[f.length - 1]) { e.preventDefault(); f[0].focus(); }
      }
    };
    const head = h('div', { class: 'modal-head' }, h('div', {}, h('h2', { text: title, id: 'm-title' }), desc ? h('p', {}, desc) : null),
      h('button', { type: 'button', class: 'icon-btn', 'aria-label': 'Close', onclick: () => close() }, ico('x')));
    const bodyEl = h('div', { class: 'modal-body' }, body);
    const footEl = h('div', { class: 'modal-foot' }, foot);
    const box = h('div', { class: 'modal' + (wide ? ' wide' : ''), role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'm-title' }, head, bodyEl, foot.length ? footEl : null);
    back.append(box);
    back.addEventListener('mousedown', (e) => { if (e.target === back) close(); });
    document.addEventListener('keydown', onKey);
    modalRoot.append(back);
    setTimeout(() => (box.querySelector('input:not([readonly]),textarea,select') || box.querySelector('.modal-foot .cx-btn:last-child') || box).focus(), 30);
    return { close, box, bodyEl, footEl, setBody: (...n) => bodyEl.replaceChildren(...n.flat()), setFoot: (...n) => { footEl.replaceChildren(...n.flat()); if (!footEl.isConnected) box.append(footEl); } };
  };
  const confirmModal = ({ title, desc, confirm = 'Confirm', danger = false, typeWord = null }) => new Promise((resolve) => {
    let done = false;
    const input = typeWord ? h('input', { type: 'text', autocomplete: 'off', spellcheck: 'false', 'aria-label': `Type ${typeWord} to confirm` }) : null;
    const ok = h('button', { type: 'button', class: 'cx-btn ' + (danger ? 'cx-btn-danger' : 'cx-btn-primary'), text: confirm, disabled: !!typeWord });
    const cancel = h('button', { type: 'button', class: 'cx-btn cx-btn-outline', text: 'Cancel' });
    const m = modal({ title, desc, body: input ? [h('label', { class: 'cx-field' }, h('span', {}, 'Type ', h('code', { text: typeWord }), ' to confirm'), input)] : [], foot: [cancel, ok], onClose: () => { if (!done) resolve(false); } });
    if (!input) m.bodyEl.remove();
    if (input) { input.addEventListener('input', () => { ok.disabled = input.value.trim() !== typeWord; }); input.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !ok.disabled) ok.click(); }); }
    cancel.onclick = () => m.close();
    ok.onclick = () => { done = true; m.close(); resolve(true); };
  });
  const busy = (btn, on, text) => {
    if (on) { btn.dataset.label = btn.innerHTML; btn.disabled = true; btn.replaceChildren(h('span', { class: 'cx-spin', style: { width: '14px', height: '14px' } }), text || btn.textContent); }
    else { btn.disabled = false; if (btn.dataset.label != null) btn.innerHTML = btn.dataset.label; }
  };

  /* ================================================================ session + API */
  const load = () => { try { return JSON.parse(localStorage.getItem(STORE) || 'null'); } catch { return null; } };
  const save = (s) => {
    if (!s) { localStorage.removeItem(STORE); return null; }
    const v = { access_token: s.access_token, refresh_token: s.refresh_token, expires_at: s.expires_at || Math.floor(Date.now() / 1000) + Number(s.expires_in || 3600) };
    localStorage.setItem(STORE, JSON.stringify(v)); return v;
  };
  const authErr = (j, status) => {
    const m = (j && (j.msg || j.error_description || j.message || j.error)) || `Request failed (${status}).`;
    if (/invalid login credentials/i.test(m)) return 'That email and password don\u2019t match an account.';
    if (/already registered|already exists/i.test(m)) return 'An account with that email already exists. Try signing in.';
    if (/rate limit|too many/i.test(m)) return 'Too many attempts. Please wait a minute and try again.';
    if (/different from the old/i.test(m)) return 'Choose a password that is different from your current one.';
    if (/reauthenticat/i.test(m)) return 'For security, sign out and sign in again, then change your password.';
    if (/weak|pwned|characters/i.test(m) && /password/i.test(m)) return 'That password is too weak. Use at least 8 characters with a mix of letters and numbers.';
    return String(m);
  };
  const gotrue = async (path, body, token, method) => {
    const r = await fetch(SB + '/auth/v1' + path, {
      method: method || (body ? 'POST' : 'GET'),
      headers: { apikey: PK, 'content-type': 'application/json', ...(token ? { authorization: 'Bearer ' + token } : {}) },
      body: body ? JSON.stringify(body) : undefined,
    });
    let j = null; try { j = await r.json(); } catch {}
    if (!r.ok) throw new Error(authErr(j, r.status));
    return j;
  };
  let session = load();
  const fresh = async () => {
    if (!session) return null;
    if (session.expires_at - 60 > Date.now() / 1000) return session;
    try { session = save(await gotrue('/token?grant_type=refresh_token', { refresh_token: session.refresh_token })); }
    catch { session = save(null); }
    return session;
  };
  const api = async (action, extra = {}, retry = true) => {
    const s = await fresh();
    if (!s) { showAuth(); throw new Error('Please sign in again.'); }
    const r = await fetch(FN, { method: 'POST', headers: { authorization: 'Bearer ' + s.access_token, 'content-type': 'application/json' }, body: JSON.stringify({ action, ...extra }) });
    let j = {}; try { j = await r.json(); } catch {}
    if (r.status === 401 && retry) { session.expires_at = 0; return api(action, extra, false); }
    if (r.status === 401) { session = save(null); showAuth(); throw new Error('Your session expired. Please sign in again.'); }
    if (!r.ok) throw new Error(j.error || `Something went wrong (${r.status}).`);
    return j;
  };
  const authUser = async (body, method) => { const s = await fresh(); if (!s) throw new Error('Please sign in again.'); return gotrue('/user', body, s.access_token, method); };

  /* ================================================================ OAuth return */
  const hash0 = new URLSearchParams(location.hash.replace(/^#\/?/, ''));
  const query = new URLSearchParams(location.search);
  let oauthError = hash0.get('error_description') || query.get('error_description');
  if (hash0.get('access_token')) {
    session = save({ access_token: hash0.get('access_token'), refresh_token: hash0.get('refresh_token'), expires_in: hash0.get('expires_in'), expires_at: Number(hash0.get('expires_at')) || undefined });
  }
  if (hash0.get('access_token') || hash0.get('error') || query.get('error')) history.replaceState(null, '', location.pathname);

  /* ================================================================ signed-out screen */
  const boot = $('#boot'), authEl = $('#auth'), appEl = $('#app');
  const form = $('#auth-form'), msg = $('#auth-msg'), submit = $('#auth-submit');
  let mode = 'signin';
  const setMsg = (t, cls) => { msg.textContent = t; msg.className = 'cx-msg' + (cls ? ' ' + cls : ''); };
  const setMode = (m) => {
    mode = m;
    const up = m === 'signup';
    $('#auth-title').textContent = up ? 'Create your account' : 'Sign in to Astro AI';
    $('#auth-sub').textContent = up ? 'Free forever. Bring your own provider keys and get one endpoint for all of them.' : 'Welcome back. Sign in to manage your API keys and providers.';
    $('#confirm-wrap').hidden = !up; $('#pw-meter').hidden = !up;
    form.password.autocomplete = up ? 'new-password' : 'current-password';
    submit.textContent = up ? 'Create account' : 'Sign in';
    const sw = $('#auth-switch'); sw.textContent = '';
    sw.append(up ? 'Already have an account? ' : 'New to Astro AI? ',
      h('a', { href: '#', onclick: (e) => { e.preventDefault(); setMode(up ? 'signin' : 'signup'); form.email.focus(); }, text: up ? 'Sign in' : 'Create an account' }));
    setMsg('');
  };
  const strength = (p) => {
    let s = 0; if (p.length >= 8) s++; if (p.length >= 12) s++; if (/[a-z]/.test(p) && /[A-Z]/.test(p)) s++; if (/\d/.test(p)) s++; if (/[^A-Za-z0-9]/.test(p)) s++;
    return [[0, 'Too short', 'var(--c-bad)'], [1, 'Weak', 'var(--c-bad)'], [2, 'Fair', 'var(--c-warn)'], [3, 'Good', 'var(--c-warn)'], [4, 'Strong', 'var(--c-ok)'], [5, 'Very strong', 'var(--c-ok)']][p.length < 8 ? 0 : s];
  };
  const paintMeter = (meter, p) => { const [n, label, c] = strength(p); const i = meter.querySelector('i'); i.style.setProperty('--w', (p ? Math.max(8, n * 20) : 0) + '%'); i.style.setProperty('--c', c); meter.querySelector('span').textContent = p ? label : ''; };
  form.password.addEventListener('input', () => paintMeter($('#pw-meter'), form.password.value));
  document.addEventListener('click', (e) => {
    const b = e.target.closest('[data-eye]'); if (!b) return;
    const inp = b.parentElement.querySelector('input'); const show = inp.type === 'password';
    inp.type = show ? 'text' : 'password'; b.textContent = show ? 'Hide' : 'Show'; b.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
  });
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const email = form.email.value.trim(), password = form.password.value;
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) return setMsg('Please enter a valid email address.', 'bad');
    if (password.length < 8) return setMsg('Your password needs at least 8 characters.', 'bad');
    if (mode === 'signup' && password !== form.confirm.value) return setMsg('The two passwords don\u2019t match.', 'bad');
    busy(submit, true, mode === 'signup' ? 'Creating account…' : 'Signing in…'); setMsg('');
    try {
      const j = mode === 'signup' ? await gotrue('/signup', { email, password }) : await gotrue('/token?grant_type=password', { email, password });
      if (!j.access_token) { setMsg('Check your inbox to confirm your email, then sign in.', 'ok'); return; }
      session = save(j); form.reset();
      if (!location.hash || location.hash === '#') history.replaceState(null, '', location.pathname + '#/overview');
      await showApp(true);
    } catch (err) { setMsg(err.message, 'bad'); }
    finally { busy(submit, false); }
  });
  const githubP = fetch(SB + '/auth/v1/settings', { headers: { apikey: PK } }).then((r) => r.json()).then((s) => !!(s.external && s.external.github)).catch(() => null);
  $('#gh').addEventListener('click', async () => {
    const on = await githubP;
    if (on === false) { setMsg('GitHub sign-in is not available right now. Please use email.', 'bad'); return; }
    location.href = `${SB}/auth/v1/authorize?provider=github&redirect_to=${encodeURIComponent(location.origin + location.pathname)}`;
  });
  const showAuth = () => {
    boot.hidden = true; appEl.hidden = true; authEl.hidden = false; closeFloat(); modalRoot.textContent = '';
    document.title = 'Sign in · Astro AI';
    setMode(mode);
    if (oauthError) { setMsg(oauthError, 'bad'); oauthError = null; }
  };

  /* ================================================================ app state */
  let state = null, me = null;
  const tests = {};            // provider id -> last test result (this page load only)
  let modelsCache = null;      // GET /v1/models result for the session key
  let playKey = sessionStorage.getItem(PLAYKEY) || '';
  const chat = { messages: [], system: '', model: 'astro/auto', temperature: 0.7, max_tokens: 1024, allowPaid: false, busy: false, ctrl: null };

  const catalog = () => (state && state.catalog) || [];
  const catById = (id) => catalog().find((c) => c.id === id) || { id, name: id, requires: [] };
  const activeKeys = () => state.keys.filter((k) => !k.revoked_at);
  const connected = () => state.providers.map((p) => ({ ...p, cat: catById(p.provider) }));
  const isConnected = (id) => state.providers.some((p) => p.provider === id);
  const meta = () => (me && me.user_metadata) || {};
  const displayName = () => meta().display_name || meta().full_name || meta().name || meta().user_name || (state.user.email || '').split('@')[0] || 'there';
  const avatarUrl = () => { const u = meta().avatar_url || ''; return /^https:\/\/avatars\.githubusercontent\.com\//.test(u) ? u : ''; };
  const avatarEl = () => {
    const a = h('span', { class: 'avatar' });
    const url = avatarUrl();
    if (url) a.append(h('img', { src: url, alt: '', referrerpolicy: 'no-referrer', onerror: function () { this.remove(); a.textContent = initial(); } }));
    else a.textContent = initial();
    return a;
  };
  const initial = () => (displayName() || '?').trim().charAt(0).toUpperCase();
  const usageMap = () => Object.fromEntries((state.usage || []).map((u) => [u.day, u.requests]));
  const lastDays = (n) => { const m = usageMap(); return [...Array(n)].map((_, i) => { const d = utcDay(n - 1 - i); return { day: d, n: m[d] || 0 }; }); };
  const sum = (a) => a.reduce((s, x) => s + x.n, 0);
  const totalUsage = () => (state.usage || []).reduce((s, u) => s + u.requests, 0);

  /* ================================================================ chrome */
  const NAV = [
    { id: 'overview', label: 'Overview', icon: 'overview' },
    { id: 'keys', label: 'API keys', icon: 'key', count: () => activeKeys().length },
    { id: 'providers', label: 'Providers', icon: 'plug', count: () => state.providers.length },
    { id: 'models', label: 'Models', icon: 'box' },
    { id: 'usage', label: 'Usage', icon: 'chart' },
    { id: 'playground', label: 'Playground', icon: 'terminal' },
    { group: 'Account' },
    { id: 'settings/profile', label: 'Profile', icon: 'user' },
    { id: 'settings/security', label: 'Security', icon: 'shield' },
    { id: 'settings/data', label: 'Data & privacy', icon: 'db' },
  ];
  const sidebar = $('#sidebar'), scrim = $('#scrim'), navBtn = $('#nav-open');
  const setDrawer = (open) => { sidebar.classList.toggle('open', open); scrim.hidden = !open; navBtn.setAttribute('aria-expanded', String(open)); };
  navBtn.addEventListener('click', () => setDrawer(!sidebar.classList.contains('open')));
  scrim.addEventListener('click', () => setDrawer(false));

  const userBtn = $('#user-btn'), userMenu = $('#user-menu');
  const setUserMenu = (open) => { userMenu.hidden = !open; userBtn.setAttribute('aria-expanded', String(open)); if (open) userMenu.querySelector('a,button').focus(); };
  userBtn.addEventListener('click', (e) => { e.stopPropagation(); setUserMenu(userMenu.hidden); });
  document.addEventListener('click', (e) => { if (!userMenu.hidden && !userMenu.contains(e.target)) setUserMenu(false); });
  userMenu.addEventListener('click', (e) => { if (e.target.closest('a,button')) setUserMenu(false); });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    if (!userMenu.hidden) { setUserMenu(false); userBtn.focus(); }
    closeFloat();
    if (sidebar.classList.contains('open')) setDrawer(false);
  });
  const signOut = async (scope) => {
    try { const s = await fresh(); if (s) await fetch(`${SB}/auth/v1/logout${scope ? '?scope=' + scope : ''}`, { method: 'POST', headers: { apikey: PK, authorization: 'Bearer ' + s.access_token } }); } catch {}
    session = save(null); state = null; me = null; modelsCache = null; playKey = ''; sessionStorage.removeItem(PLAYKEY); chat.messages = [];
    history.replaceState(null, '', location.pathname);
    showAuth();
  };
  $('#signout').addEventListener('click', () => signOut());

  const paintChrome = () => {
    const nav = $('#side-nav'); nav.textContent = '';
    for (const it of NAV) {
      if (it.group) { nav.append(h('div', { class: 'side-group', text: it.group })); continue; }
      const c = it.count ? it.count() : null;
      nav.append(h('a', { href: '#/' + it.id, 'data-route': it.id }, ico(it.icon), h('span', { text: it.label }), c ? h('span', { class: 'count', text: c }) : null));
    }
    const today = Number(state.today || 0), pct = Math.min(100, (today / DAILY_LIMIT) * 100);
    $('#plan-count').textContent = `${nf(today)} / ${nf(DAILY_LIMIT)}`;
    $('#plan-meter').style.width = pct + '%';
    $('#plan-meter').parentElement.className = 'meter' + (pct >= 90 ? ' bad' : pct >= 70 ? ' warn' : '');
    $('#tb-avatar').replaceWith(Object.assign(avatarEl(), { id: 'tb-avatar' }));
    $('#ws-av').textContent = initial();
    $('#ws-name').textContent = displayName();
    $('#um-name').textContent = displayName();
    $('#um-email').textContent = state.user.email || '';
    markNav();
  };
  const markNav = () => document.querySelectorAll('#side-nav a').forEach((a) => {
    const on = a.dataset.route === route();
    if (on) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
  });

  /* ================================================================ router */
  const PAGES = {};
  const route = () => { const r = location.hash.replace(/^#\/?/, '').split('?')[0]; return PAGES[r] ? r : (r === 'settings' ? 'settings/profile' : ''); };
  const view = $('#view');
  const render = () => {
    if (!state) return;
    let r = route();
    if (!r) { r = localStorage.getItem(HOME) || 'overview'; if (!PAGES[r]) r = 'overview'; history.replaceState(null, '', '#/' + r); }
    closeFloat(); setDrawer(false);
    const p = PAGES[r];
    document.title = `${p.title} · Astro AI Console`;
    view.textContent = ''; view.style.animation = 'none'; void view.offsetWidth; view.style.animation = '';
    p.render(view);
    markNav();
  };
  addEventListener('hashchange', () => { if (state) { render(); scrollTo(0, 0); $('#main').focus({ preventScroll: true }); } });
  const go = (r) => { if (location.hash === '#/' + r) render(); else location.hash = '#/' + r; };
  const reload = async (quiet) => {
    try { state = await api('list'); paintChrome(); render(); }
    catch (err) { if (!quiet) toast(err.message, true); }
  };

  const pageHead = (title, desc, actions = [], crumbs) => h('div', { class: 'page-head' },
    h('div', {}, crumbs ? h('div', { class: 'crumbs', text: crumbs }) : null, h('h1', { text: title }), desc ? h('p', {}, desc) : null),
    actions.length ? h('div', { class: 'actions' }, actions) : null);
  const panel = (title, desc, body, { actions, foot, cls, flush } = {}) => h('section', { class: 'panel' + (cls ? ' ' + cls : '') },
    title ? h('div', { class: 'panel-head' }, h('div', {}, h('h2', { text: title }), desc ? h('p', {}, desc) : null), actions ? h('div', { class: 'row' }, actions) : null) : null,
    flush ? body : h('div', { class: 'panel-body' }, body),
    foot ? h('div', { class: 'panel-foot' }, foot) : null);
  const btn = (label, cls, onclick, icon, extra = {}) => h('button', { type: 'button', class: 'cx-btn ' + (cls || 'cx-btn-outline'), onclick, ...extra }, icon ? ico(icon) : null, label);
  const linkBtn = (label, href, cls, icon) => h('a', { class: 'cx-btn ' + (cls || 'cx-btn-outline'), href }, icon ? ico(icon) : null, label);
  const empty = (icon, title, text, action) => h('div', { class: 'empty' }, h('div', { class: 'empty-ico' }, ico(icon)), h('h3', { text: title }), h('p', {}, text), action || null);
  const banner = (cls, icon, content, action) => h('div', { class: 'banner ' + (cls || '') }, ico(icon), h('div', { class: 'grow' }, content), action || null);
  const stat = (label, icon, value, sub, extra) => h('section', { class: 'panel stat' },
    h('div', { class: 'stat-label' }, h('span', { text: label }), ico(icon)), h('div', { class: 'stat-value' }, value), sub ? h('div', { class: 'stat-sub' }, sub) : null, extra || null);
  const meterEl = (pct) => { const m = h('div', { class: 'meter' + (pct >= 90 ? ' bad' : pct >= 70 ? ' warn' : '') }, h('i')); requestAnimationFrame(() => { m.firstChild.style.width = Math.min(100, pct) + '%'; }); return m; };

  /* bar chart with hover tooltip */
  const niceMax = (v) => { if (v <= 4) return 4; const p = 10 ** Math.floor(Math.log10(v)); for (const m of [1, 2, 2.5, 5, 10]) if (m * p >= v) return m * p; return 10 * p; };
  const barChart = (days, small) => {
    const max = niceMax(Math.max(...days.map((d) => d.n)));
    const today = utcDay();
    const step = days.length <= 7 ? 1 : days.length <= 14 ? 2 : 5;
    const wrap = h('div', { class: 'chart' + (small ? ' sm' : ''), role: 'img', 'aria-label': `Requests per day, last ${days.length} days. Total ${nf(sum(days))}.` });
    const grid = h('div', { class: 'chart-grid', 'aria-hidden': 'true' });
    for (let i = 4; i >= 0; i--) grid.append(h('div', {}, small ? null : h('span', { text: nf(Math.round((max * i) / 4)) })));
    wrap.append(grid);
    const tip = h('div', { class: 'chart-tip', hidden: true });
    days.forEach((d, i) => {
      const date = new Date(d.day + 'T00:00:00Z');
      const lab = days.length <= 7 ? date.toLocaleDateString(undefined, { weekday: 'short', timeZone: 'UTC' }) : date.toLocaleDateString(undefined, { month: 'short', day: 'numeric', timeZone: 'UTC' });
      const col = h('div', { class: 'col' + (d.n ? '' : ' zero') + (d.day === today ? ' today' : '') },
        h('i', { style: { height: Math.max(d.n ? 3 : 1, (d.n / max) * 100) + '%' } }),
        !small && (i % step === 0 || i === days.length - 1) ? h('span', { text: lab }) : null);
      col.addEventListener('mouseenter', () => {
        tip.replaceChildren(h('b', { text: nf(d.n) + ' requests' }), date.toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric', timeZone: 'UTC' }) + (d.day === today ? ' · today' : ''));
        tip.hidden = false;
        const cr = col.getBoundingClientRect(), wr = wrap.getBoundingClientRect();
        tip.style.left = Math.min(wr.width - 70, Math.max(70, cr.left - wr.left + cr.width / 2)) + 'px';
        tip.style.top = Math.max(0, cr.top - wr.top + (cr.height - col.firstChild.offsetHeight) - 6) + 'px';
      });
      col.addEventListener('mouseleave', () => { tip.hidden = true; });
      wrap.append(col);
    });
    wrap.append(tip);
    return wrap;
  };

  /* code samples */
  const snippets = (endpoint, key = '$ASTRO_API_KEY', model = 'astro/auto') => {
    const k = key.startsWith('$') ? key : key;
    return {
      curl: `curl ${endpoint}/chat/completions \\\n  -H "Authorization: Bearer ${k}" \\\n  -H "Content-Type: application/json" \\\n  -d '{"model": "${model}", "messages": [{"role": "user", "content": "Hello!"}]}'`,
      python: `# pip install openai\nimport os\nfrom openai import OpenAI\n\nclient = OpenAI(\n    base_url="${endpoint}",\n    api_key=os.environ["ASTRO_API_KEY"],\n)\n\nres = client.chat.completions.create(\n    model="${model}",\n    messages=[{"role": "user", "content": "Hello!"}],\n)\nprint(res.choices[0].message.content)`,
      javascript: `// npm install openai\nimport OpenAI from "openai";\n\nconst client = new OpenAI({\n  baseURL: "${endpoint}",\n  apiKey: process.env.ASTRO_API_KEY,\n});\n\nconst res = await client.chat.completions.create({\n  model: "${model}",\n  messages: [{ role: "user", content: "Hello!" }],\n});\nconsole.log(res.choices[0].message.content);`,
      env: `# Most OpenAI-compatible tools read these variables\nexport OPENAI_BASE_URL="${endpoint}"\nexport OPENAI_API_KEY="$ASTRO_API_KEY"\n\n# Then use model ids like astro/auto or groq/llama-3.3-70b-versatile`,
    };
  };
  const codeTabs = (snips) => {
    const labels = { curl: 'cURL', python: 'Python', javascript: 'JavaScript', env: 'Other tools' };
    const tabs = h('div', { class: 'tabs', role: 'tablist' });
    const body = h('div', { class: 'panel-body' });
    const show = (id) => { tabs.querySelectorAll('button').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.id === id))); body.replaceChildren(codeBlock(snips[id])); };
    for (const id of Object.keys(snips)) tabs.append(h('button', { type: 'button', role: 'tab', 'data-id': id, text: labels[id] || id, onclick: () => show(id) }));
    show('curl');
    return [tabs, body];
  };

  /* ================================================================ modals: keys + providers */
  const createKeyModal = () => {
    if (activeKeys().length >= MAX_KEYS) { toast(`You already have ${MAX_KEYS} active keys. Revoke one first.`, true); return; }
    const name = h('input', { type: 'text', maxlength: '60', placeholder: 'e.g. Production server', autocomplete: 'off' });
    const err = h('p', { class: 'cx-msg bad' });
    const create = btn('Create key', 'cx-btn-primary');
    const cancel = btn('Cancel', 'cx-btn-outline');
    const m = modal({ title: 'Create API key', desc: 'Astro keys authenticate requests to the Astro API. A key can use every provider you have connected.',
      body: [h('label', { class: 'cx-field' }, h('span', { text: 'Name' }), name, h('small', { text: 'A label to help you recognise this key later.' })), err], foot: [cancel, create] });
    cancel.onclick = () => m.close();
    name.addEventListener('keydown', (e) => { if (e.key === 'Enter') create.click(); });
    create.onclick = async () => {
      busy(create, true, 'Creating…'); err.textContent = '';
      try {
        const j = await api('create_key', { name: name.value.trim() || 'My key' });
        const use = h('input', { type: 'checkbox' });
        m.box.querySelector('#m-title').textContent = 'Save your API key';
        m.box.querySelector('.modal-head p').textContent = 'This is the only time the full key is shown. Store it somewhere safe, such as a password manager or your server\u2019s secrets.';
        m.setBody(
          copyField(j.key, true),
          banner('warn', 'alert', [h('b', { text: 'You won\u2019t be able to see this key again. ' }), 'If you lose it, revoke it and create a new one.']),
          h('div', { class: 'cx-field' }, h('span', { text: 'Add it to your environment' }), codeBlock(`export ASTRO_API_KEY="${j.key}"`)),
          h('label', { class: 'check-row' }, h('span', {}, 'Use this key in the Playground and Models pages', h('small', { text: 'Kept in this browser tab only and cleared when you close it.' })), h('span', { class: 'switch' }, use, h('i'))),
        );
        const done = btn('Done', 'cx-btn-primary', () => { if (use.checked) setPlayKey(j.key); m.close(); });
        m.setFoot(done); done.focus();
        reload(true);
      } catch (e) { err.textContent = e.message; busy(create, false); }
    };
  };
  const revokeKey = async (k) => {
    const ok = await confirmModal({ title: `Revoke “${k.name}”?`, desc: `Any app using ${k.prefix}… will stop working immediately. This can’t be undone.`, confirm: 'Revoke key', danger: true });
    if (!ok) return;
    try { await api('revoke_key', { id: k.id }); toast(`Revoked “${k.name}”.`); if (playKey.startsWith(k.prefix)) setPlayKey(''); reload(true); }
    catch (e) { toast(e.message, true); }
  };
  const EXTRA = {
    AWS_REGION: { label: 'AWS region', ph: 'us-east-1', help: 'The Bedrock region your key has access to.' },
    AZURE_OPENAI_RESOURCE: { label: 'Azure resource name', ph: 'my-openai-resource', help: 'From https://<resource>.openai.azure.com.' },
    CLOUDFLARE_ACCOUNT_ID: { label: 'Cloudflare account ID', ph: '32-character account ID', help: 'Shown on your Cloudflare dashboard sidebar.' },
  };
  const connectModal = (c) => {
    const existing = state.providers.find((p) => p.provider === c.id);
    const key = h('input', { type: 'password', autocomplete: 'off', spellcheck: 'false', placeholder: existing ? `Current key ends in …${existing.last4}` : 'Paste your API key', maxlength: '600' });
    const extras = (c.requires || []).map((n) => {
      const e = EXTRA[n] || { label: n, ph: '', help: '' };
      const inp = h('input', { type: 'text', 'data-extra': n, placeholder: e.ph, maxlength: '100', autocomplete: 'off', value: (existing && existing.extra && existing.extra[n]) || '' });
      return h('label', { class: 'cx-field' }, h('span', { text: e.label }), inp, e.help ? h('small', { text: e.help }) : null);
    });
    const err = h('p', { class: 'cx-msg bad' });
    const save = btn(existing ? 'Replace key' : 'Connect', 'cx-btn-primary');
    const cancel = btn('Cancel', 'cx-btn-outline');
    const m = modal({
      title: (existing ? 'Replace key · ' : 'Connect ') + c.name,
      desc: h('span', { class: 'row' }, h('span', { class: 'pill ' + (c.paid ? 'warn' : 'ok'), text: c.paid ? 'Paid' : 'Free tier' }), existing ? h('span', { class: 'pill', text: '•••• ' + existing.last4 }) : null),
      body: [
        c.signup ? h('p', { class: 'muted', style: { margin: 0 } }, '1. Create a key on ', h('a', { class: 'link', href: c.signup, target: '_blank', rel: 'noopener noreferrer' }, new URL(c.signup).hostname, ' ↗'), '. 2. Paste it below.') : null,
        h('label', { class: 'cx-field' }, h('span', { text: 'API key' }), h('span', { class: 'pw-wrap' }, key, h('button', { type: 'button', class: 'pw-eye', 'data-eye': '', 'aria-label': 'Show key', text: 'Show' }))),
        extras,
        c.paid ? banner('warn', 'info', 'This is a paid provider. It is never used by astro/auto unless a request sets "allow_paid": true or names one of its models directly. You pay the provider for usage.') : null,
        banner('', 'lock', 'Your key is encrypted (AES-256-GCM) before it is stored and is only ever sent to ' + c.name + '. It is never shown again.'),
        err,
      ], foot: [cancel, save] });
    cancel.onclick = () => m.close();
    key.addEventListener('keydown', (e) => { if (e.key === 'Enter') save.click(); });
    save.onclick = async () => {
      const extra = {}; m.box.querySelectorAll('[data-extra]').forEach((i) => { extra[i.dataset.extra] = i.value.trim(); });
      if (key.value.trim().length < 8) { err.textContent = 'Paste the full API key from ' + c.name + '.'; key.focus(); return; }
      busy(save, true, 'Saving…'); err.textContent = '';
      try {
        await api('save_provider', { provider: c.id, key: key.value.trim(), extra });
        m.setBody(h('div', { class: 'row' }, h('span', { class: 'cx-spin' }), h('span', { class: 'muted', text: `Saved. Checking the key with ${c.name}…` })));
        m.setFoot();
        let t = null; try { t = await testProvider(c.id, true); } catch {}
        const ok = t && t.ok;
        m.setBody(
          banner(ok ? 'ok' : 'warn', ok ? 'check' : 'alert', ok
            ? [h('b', { text: `${c.name} is connected. ` }), `${nf(t.models)} models found in ${nf(t.ms)} ms.`]
            : [h('b', { text: 'Saved, but the check failed. ' }), `${c.name} didn’t return a model list. The key may be wrong, out of credit, or the provider may be busy. You can test it again from Providers.`]),
          ok && t.sample.length ? h('div', { class: 'stack' }, h('span', { class: 'cx-label', text: 'Example model ids' }), h('div', { class: 'row' }, t.sample.map((s) => h('code', { class: 'pill', text: `${c.id}/${s}` })))) : null,
        );
        m.setFoot(btn('Done', 'cx-btn-primary', () => m.close()));
        modelsCache = null;
        reload(true);
      } catch (e) { err.textContent = e.message; busy(save, false); }
    };
  };
  const testProvider = async (id, quiet) => {
    const t = await api('test_provider', { provider: id });
    tests[id] = { ...t, at: Date.now() };
    if (!quiet) toast(t.ok ? `${catById(id).name}: ${nf(t.models)} models, ${nf(t.ms)} ms.` : `${catById(id).name} did not return any models.`, !t.ok);
    return t;
  };
  const removeProvider = async (p) => {
    const ok = await confirmModal({ title: `Remove ${p.cat.name}?`, desc: 'The encrypted key is deleted. Requests that use this provider will fail until you connect it again.', confirm: 'Remove provider', danger: true });
    if (!ok) return;
    try { await api('delete_provider', { provider: p.provider }); delete tests[p.provider]; modelsCache = null; toast(`${p.cat.name} removed.`); reload(true); }
    catch (e) { toast(e.message, true); }
  };
  const setPlayKey = (k) => {
    playKey = k; modelsCache = null;
    if (k) sessionStorage.setItem(PLAYKEY, k); else sessionStorage.removeItem(PLAYKEY);
  };
  const keyGate = (after) => {
    const inp = h('input', { type: 'password', placeholder: 'astro_sk_…', autocomplete: 'off', spellcheck: 'false' });
    const err = h('p', { class: 'cx-msg bad' });
    const go2 = btn('Use key', 'cx-btn-primary', () => {
      const v = inp.value.trim();
      if (!/^astro_sk_[A-Za-z0-9_-]{16,}$/.test(v)) { err.textContent = 'That doesn\u2019t look like an Astro key. It starts with astro_sk_.'; return; }
      setPlayKey(v); after();
    });
    inp.addEventListener('keydown', (e) => { if (e.key === 'Enter') go2.click(); });
    return panel('Use an Astro key in this tab', 'The Models and Playground pages call the API the same way your apps do, so they need one of your Astro keys.', [
      h('div', { class: 'set-ctl' }, h('div', { class: 'inline' }, inp, go2), err,
        h('p', { class: 'cx-help', style: { margin: 0 } }, 'The key stays in this browser tab (sessionStorage) and is cleared when you close it or sign out. Lost your key? ', h('button', { type: 'button', class: 'link', text: 'Create a new one', onclick: createKeyModal }), '.')),
    ]);
  };
  const fetchModels = async () => {
    if (modelsCache) return modelsCache;
    const r = await fetch(state.endpoint + '/models', { headers: { authorization: 'Bearer ' + playKey } });
    let j = {}; try { j = await r.json(); } catch {}
    if (r.status === 401) { setPlayKey(''); throw new Error('That Astro key was rejected. It may have been revoked.'); }
    if (!r.ok) throw new Error((j.error && j.error.message) || `Could not load models (${r.status}).`);
    modelsCache = j.data || [];
    return modelsCache;
  };

  /* ================================================================ pages */
  PAGES.overview = {
    title: 'Overview',
    render(v) {
      const hr = new Date().getHours();
      const greet = hr < 5 ? 'Good evening' : hr < 12 ? 'Good morning' : hr < 18 ? 'Good afternoon' : 'Good evening';
      const keys = activeKeys(), provs = state.providers, enabled = provs.filter((p) => p.enabled);
      const total = totalUsage();
      v.append(pageHead(`${greet}, ${displayName()}`, 'Here\u2019s an overview of your Astro API workspace.', [
        btn('Add provider', 'cx-btn-outline', () => go('providers?browse'), 'plug'),
        btn('Create API key', 'cx-btn-primary', createKeyModal, 'plus'),
      ]));

      // getting started
      const steps = [
        { done: provs.length > 0, t: 'Connect a provider', d: 'Add a key from Groq, Gemini, OpenRouter or any of the 79 providers.', a: btn('Browse providers', 'cx-btn-outline cx-btn-sm', () => go('providers?browse')) },
        { done: keys.length > 0, t: 'Create an API key', d: 'Your Astro key reaches every provider you connect.', a: btn('Create key', 'cx-btn-outline cx-btn-sm', createKeyModal) },
        { done: total > 0, t: 'Make your first request', d: 'Try it in the Playground or with the code below.', a: btn('Open Playground', 'cx-btn-outline cx-btn-sm', () => go('playground')) },
      ];
      const nDone = steps.filter((s) => s.done).length;
      if (nDone < 3) {
        v.append(panel('Get started', null, h('div', { class: 'setup' }, steps.map((s, i) => h('div', { class: 'setup-step' + (s.done ? ' done' : '') },
          h('span', { class: 'setup-n' }, s.done ? ico('check') : String(i + 1)), h('div', {}, h('h3', { text: s.t }), h('p', { text: s.d }), s.done ? null : s.a)))),
          { flush: true, actions: [h('span', { class: 'progress-txt', text: `${nDone} of 3 complete` })] }));
      }

      // stats
      const today = Number(state.today || 0), pct = (today / DAILY_LIMIT) * 100;
      const d14 = lastDays(14), last7 = sum(d14.slice(7)), prev7 = sum(d14.slice(0, 7));
      const delta = prev7 ? Math.round(((last7 - prev7) / prev7) * 100) : null;
      const paidN = provs.filter((p) => catById(p.provider).paid).length;
      v.append(h('div', { class: 'grid grid-4' },
        stat('Requests today', 'activity', [nf(today), h('small', { text: `/ ${nf(DAILY_LIMIT)}` })], `${(100 - Math.min(100, pct)).toFixed(pct > 99 ? 1 : 0)}% of today\u2019s limit left`, meterEl(pct)),
        stat('Last 7 days', 'chart', nf(last7), delta == null ? 'No requests the week before' : h('span', {}, h('span', { class: delta >= 0 ? 'up' : 'down', text: (delta >= 0 ? '↑ ' : '↓ ') + Math.abs(delta) + '%' }), ' vs previous 7 days')),
        stat('Providers', 'plug', [nf(enabled.length), h('small', { text: `of ${catalog().length}` })], provs.length ? `${provs.length - paidN} free tier · ${paidN} paid${provs.length - enabled.length ? ` · ${provs.length - enabled.length} off` : ''}` : 'None connected yet'),
        stat('Active API keys', 'key', [nf(keys.length), h('small', { text: `/ ${MAX_KEYS}` })], keys.length ? `Last used ${ago(keys.map((k) => k.last_used_at).filter(Boolean).sort().pop())}` : 'Create one to start'),
      ));

      // chart + endpoint
      v.append(h('div', { class: 'grid grid-main' },
        panel('Requests', 'Last 14 days, UTC', barChart(d14, true), { actions: [h('a', { class: 'cx-btn cx-btn-ghost cx-btn-sm', href: '#/usage' }, 'View usage →')] }),
        panel('API endpoint', 'OpenAI-compatible. Works with any OpenAI SDK.', h('div', { class: 'stack' },
          h('div', { class: 'cx-field' }, h('span', { class: 'cx-label', text: 'Base URL' }), copyField(state.endpoint)),
          h('div', { class: 'cx-field' }, h('span', { class: 'cx-label', text: 'Default model' }), copyField('astro/auto')),
          h('p', { class: 'cx-help', style: { margin: 0 } }, 'Authenticate with ', h('code', { text: 'Authorization: Bearer astro_sk_…' }), '. ', h('a', { class: 'link', href: ROOT + 'api.html', text: 'API reference' }), '.'))),
      ));

      // quick start + activity
      v.append(h('div', { class: 'grid grid-main' },
        h('section', { class: 'panel' }, h('div', { class: 'panel-head' }, h('div', {}, h('h2', { text: 'Quick start' }), h('p', { text: 'Set ASTRO_API_KEY to one of your keys, then run:' }))), ...codeTabs(snippets(state.endpoint))),
        panel('Recent activity', null, activityFeed(), { flush: true }),
      ));
    },
  };
  const activityFeed = () => {
    const ev = [];
    for (const k of state.keys) {
      ev.push({ at: k.created_at, icon: 'key', t: h('span', {}, 'Created API key ', h('b', { text: k.name })) });
      if (k.last_used_at) ev.push({ at: k.last_used_at, icon: 'zap', t: h('span', {}, 'Key ', h('b', { text: k.name }), ' made a request') });
      if (k.revoked_at) ev.push({ at: k.revoked_at, icon: 'trash', t: h('span', {}, 'Revoked API key ', h('b', { text: k.name })) });
    }
    for (const p of state.providers) if (p.updated_at) ev.push({ at: p.updated_at, icon: 'plug', t: h('span', {}, 'Connected ', h('b', { text: catById(p.provider).name }), ` (…${p.last4})`) });
    if (me && me.last_sign_in_at) ev.push({ at: me.last_sign_in_at, icon: 'user', t: 'Signed in' });
    ev.sort((a, b) => new Date(b.at) - new Date(a.at));
    if (!ev.length) return empty('activity', 'No activity yet', 'Things you do here will show up in this list.');
    return h('ul', { class: 'feed' }, ev.slice(0, 7).map((e) => h('li', {}, h('span', { class: 'feed-ico' }, ico(e.icon)), h('div', { class: 'feed-txt' }, e.t, h('span', { text: ago(e.at), title: fmtDate(e.at, true) })))));
  };

  /* ---------------------------------------------------------------- keys */
  let keyFilter = 'active', keyQuery = '';
  PAGES.keys = {
    title: 'API keys',
    render(v) {
      const act = activeKeys();
      v.append(pageHead('API keys', 'Use these keys to call the Astro API from your apps. Each key works with every provider you\u2019ve connected.', [
        btn('Create API key', 'cx-btn-primary', createKeyModal, 'plus', { disabled: act.length >= MAX_KEYS }),
      ]));
      v.append(banner(act.length >= MAX_KEYS ? 'warn' : '', 'info', [h('b', { text: `${act.length} of ${MAX_KEYS} active keys. ` }), 'Keys are stored as a SHA-256 hash, so we can show only the prefix after you create one. Never share keys or commit them to code.']));
      const body = h('div');
      const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Filter keys' }, [['active', 'Active'], ['revoked', 'Revoked'], ['all', 'All']].map(([id, l]) =>
        h('button', { type: 'button', 'aria-pressed': String(keyFilter === id), text: l, onclick: (e) => { keyFilter = id; seg.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(b === e.currentTarget))); paint(); } })));
      const q = h('input', { type: 'search', placeholder: 'Search keys', value: keyQuery, 'aria-label': 'Search keys', oninput: debounce(() => { keyQuery = q.value; paint(); }) });
      const paint = () => {
        const rows = state.keys.filter((k) => (keyFilter === 'all' || (keyFilter === 'active' ? !k.revoked_at : k.revoked_at)) && (!keyQuery || (k.name + k.prefix).toLowerCase().includes(keyQuery.toLowerCase())));
        if (!state.keys.length) return body.replaceChildren(empty('key', 'No API keys yet', 'Create a key to start calling every model you\u2019ve connected through one endpoint.', btn('Create API key', 'cx-btn-primary', createKeyModal, 'plus')));
        if (!rows.length) return body.replaceChildren(empty('search', 'No matching keys', keyFilter === 'revoked' ? 'You haven\u2019t revoked any keys.' : 'Try a different search.'));
        body.replaceChildren(h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' },
          h('thead', {}, h('tr', {}, h('th', { text: 'Name' }), h('th', { text: 'Key' }), h('th', { class: 'hide-sm', text: 'Created' }), h('th', { text: 'Last used' }), h('th', { text: 'Status' }), h('th', { class: 'actions' }, h('span', { class: 'sr', text: 'Actions' })))),
          h('tbody', {}, rows.map((k) => {
            const more = h('button', { type: 'button', class: 'icon-btn', 'aria-label': `Actions for ${k.name}`, 'aria-haspopup': 'menu', 'aria-expanded': 'false' }, ico('dots'));
            more.onclick = () => floatMenu(more, [
              { label: 'Copy key prefix', icon: 'copy', run: () => copyText(k.prefix).then(() => toast('Prefix copied.')) },
              !k.revoked_at && { label: 'Use in Playground…', icon: 'terminal', run: () => go('playground') },
              !k.revoked_at && '-',
              !k.revoked_at && { label: 'Revoke key', icon: 'trash', danger: true, run: () => revokeKey(k) },
            ]);
            return h('tr', { class: k.revoked_at ? 'off' : '' },
              h('td', {}, h('div', { class: 'cell-main' }, h('b', { text: k.name }))),
              h('td', {}, h('code', { class: 'muted', text: k.prefix + '…' })),
              h('td', { class: 'muted hide-sm', title: fmtDate(k.created_at, true), text: fmtDate(k.created_at) }),
              h('td', { class: 'muted', title: k.last_used_at ? fmtDate(k.last_used_at, true) : '', text: ago(k.last_used_at) }),
              h('td', {}, k.revoked_at ? h('span', { class: 'pill bad', title: 'Revoked ' + fmtDate(k.revoked_at, true) }, h('span', { class: 'dot' }), 'Revoked') : h('span', { class: 'pill ok' }, h('span', { class: 'dot' }), 'Active')),
              h('td', { class: 'actions' }, more));
          })))));
      };
      v.append(h('section', { class: 'panel mt-16' },
        h('div', { class: 'panel-head' }, h('div', { class: 'toolbar', style: { margin: 0, flex: 1 } }, h('div', { class: 'search' }, ico('search'), q), h('div', { class: 'spacer' }), seg)),
        body));
      paint();
      v.append(panel('Using your key', null, h('div', { class: 'stack' },
        h('p', { class: 'muted', style: { margin: 0 } }, 'Send the key as a Bearer token. Requests go to your connected providers using your own provider keys, and each key shares your account\u2019s daily limit of ' + nf(DAILY_LIMIT) + ' requests.'),
        codeBlock(snippets(state.endpoint).curl))));
    },
  };

  /* ---------------------------------------------------------------- providers */
  let provTab = 'connected', catQuery = '', catFilter = 'all';
  PAGES.providers = {
    title: 'Providers',
    render(v) {
      if (/\?browse/.test(location.hash)) provTab = 'browse';
      const conn = connected();
      if (!conn.length && provTab === 'connected' && !/\?/.test(location.hash) && PAGES.providers._first !== false) provTab = 'browse';
      PAGES.providers._first = false;
      v.append(pageHead('Providers', 'Bring your own keys. Astro routes each request to these providers, using your keys and your free tiers.', [
        conn.length ? btn('Test all', 'cx-btn-outline', async (e) => { const b = e.currentTarget; busy(b, true, 'Testing…'); await Promise.allSettled(conn.map((p) => testProvider(p.provider, true))); busy(b, false); toast('Finished testing providers.'); render(); }, 'refresh') : null,
        btn('Add provider', 'cx-btn-primary', () => { provTab = 'browse'; history.replaceState(null, '', '#/providers'); render(); }, 'plus'),
      ].filter(Boolean)));
      const tabs = h('div', { class: 'tabs', role: 'tablist' },
        [['connected', 'Connected', conn.length], ['browse', 'Browse all', catalog().length]].map(([id, l, n]) =>
          h('button', { type: 'button', role: 'tab', 'aria-selected': String(provTab === id), onclick: () => { provTab = id; history.replaceState(null, '', '#/providers'); render(); } }, l, h('span', { class: 'count', text: n }))));
      v.append(tabs);
      if (provTab === 'connected') v.append(connectedList(conn)); else v.append(catalogGrid());
    },
  };
  const connectedList = (conn) => {
    if (!conn.length) return h('section', { class: 'panel' }, empty('plug', 'No providers connected', 'Connect at least one provider to start making requests. Many have generous free tiers, such as Groq, Google Gemini, Cerebras and OpenRouter.',
      btn('Browse providers', 'cx-btn-primary', () => { provTab = 'browse'; render(); })));
    return h('section', { class: 'panel' }, h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' },
      h('thead', {}, h('tr', {}, h('th', { text: 'Provider' }), h('th', { text: 'Key' }), h('th', { class: 'hide-sm', text: 'Updated' }), h('th', { text: 'Status' }), h('th', { text: 'Enabled' }), h('th', { class: 'actions' }, h('span', { class: 'sr', text: 'Actions' })))),
      h('tbody', {}, conn.map((p) => {
        const t = tests[p.provider];
        const sw = h('input', { type: 'checkbox', 'aria-label': `Enable ${p.cat.name}`, checked: !!p.enabled });
        sw.onchange = async () => {
          try { await api('toggle_provider', { provider: p.provider, enabled: sw.checked }); p.enabled = sw.checked; const raw = state.providers.find((x) => x.provider === p.provider); if (raw) raw.enabled = sw.checked; modelsCache = null; toast(`${p.cat.name} ${sw.checked ? 'enabled' : 'disabled'}.`); paintChrome(); render(); }
          catch (e) { sw.checked = !sw.checked; toast(e.message, true); }
        };
        const status = t ? (t.ok ? h('span', { class: 'pill ok', title: `Tested ${ago(t.at)}` }, h('span', { class: 'dot' }), `${nf(t.models)} models · ${nf(t.ms)} ms`) : h('span', { class: 'pill bad', title: `Tested ${ago(t.at)}` }, h('span', { class: 'dot' }), 'Failed'))
          : h('span', { class: 'pill' }, 'Not tested');
        const testB = btn('Test', 'cx-btn-outline cx-btn-xs', async () => { busy(testB, true, 'Testing'); try { await testProvider(p.provider); } catch (e) { toast(e.message, true); } render(); });
        const more = h('button', { type: 'button', class: 'icon-btn', 'aria-label': `Actions for ${p.cat.name}`, 'aria-haspopup': 'menu', 'aria-expanded': 'false' }, ico('dots'));
        more.onclick = () => floatMenu(more, [
          { label: 'Replace key', icon: 'key', run: () => connectModal(p.cat) },
          p.cat.signup && { label: 'Open provider console', icon: 'ext', run: () => window.open(p.cat.signup, '_blank', 'noopener') },
          '-',
          { label: 'Remove provider', icon: 'trash', danger: true, run: () => removeProvider(p) },
        ]);
        return h('tr', { class: p.enabled ? '' : 'off' },
          h('td', {}, h('div', { class: 'cell-main' }, provLogo(p.cat), h('div', {}, h('b', { text: p.cat.name }), h('span', { class: 'cell-sub' }, p.cat.paid ? 'Paid' : 'Free tier', p.extra && Object.values(p.extra).length ? ' · ' + Object.values(p.extra).join(', ') : '')))),
          h('td', {}, h('code', { class: 'muted', text: '•••• ' + p.last4 })),
          h('td', { class: 'muted hide-sm', title: fmtDate(p.updated_at, true), text: ago(p.updated_at) }),
          h('td', {}, status),
          h('td', {}, h('label', { class: 'switch' }, sw, h('i'))),
          h('td', { class: 'actions' }, h('div', { class: 'row', style: { justifyContent: 'flex-end', flexWrap: 'nowrap' } }, testB, more)));
      })))),
      h('div', { class: 'panel-foot' }, h('span', {}, 'Turning a provider off keeps its key but stops Astro from using it.'), h('button', { type: 'button', class: 'link', text: 'Browse more providers', onclick: () => { provTab = 'browse'; render(); } })));
  };
  const catalogGrid = () => {
    const wrap = h('div');
    const grid = h('div', { class: 'cat-grid' });
    const q = h('input', { type: 'search', placeholder: `Search ${catalog().length} providers`, value: catQuery, 'aria-label': 'Search providers', oninput: debounce(() => { catQuery = q.value; paint(); }, 100) });
    const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Filter providers' }, [['all', 'All'], ['free', 'Free tier'], ['paid', 'Paid'], ['connected', 'Connected']].map(([id, l]) =>
      h('button', { type: 'button', 'aria-pressed': String(catFilter === id), text: l, onclick: (e) => { catFilter = id; seg.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(b === e.currentTarget))); paint(); } })));
    const count = h('span', { class: 'faint' });
    const paint = () => {
      const s = catQuery.trim().toLowerCase();
      const list = catalog().filter((c) => (catFilter === 'all' || (catFilter === 'free' && !c.paid) || (catFilter === 'paid' && c.paid) || (catFilter === 'connected' && isConnected(c.id))) && (!s || (c.name + ' ' + c.id).toLowerCase().includes(s)));
      count.textContent = `${list.length} provider${list.length === 1 ? '' : 's'}`;
      if (!list.length) { grid.replaceChildren(); grid.after(); wrap.querySelector('.empty')?.remove(); grid.before(empty('search', 'No providers match', 'Try another name or filter.')); return; }
      wrap.querySelector('.empty')?.remove();
      grid.replaceChildren(...list.map((c) => {
        const on = isConnected(c.id);
        return h('div', { class: 'cat-card' },
          h('div', { class: 'cat-top' }, provLogo(c), h('b', { text: c.name, title: c.name })),
          h('div', { class: 'cat-meta' }, h('span', { class: 'pill ' + (c.paid ? 'warn' : 'ok'), text: c.paid ? 'Paid' : 'Free tier' }), on ? h('span', { class: 'pill accent' }, ico('check'), 'Connected') : null, (c.requires || []).length ? h('span', { class: 'pill', text: 'Extra setup' }) : null),
          h('div', { class: 'cat-actions' }, c.signup ? h('a', { href: c.signup, target: '_blank', rel: 'noopener noreferrer' }, 'Get a key ↗') : h('span'),
            btn(on ? 'Manage' : 'Connect', on ? 'cx-btn-outline cx-btn-sm' : 'cx-btn-primary cx-btn-sm', () => connectModal(c))));
      }));
    };
    wrap.append(h('div', { class: 'toolbar' }, h('div', { class: 'search' }, ico('search'), q), seg, h('div', { class: 'spacer' }), count), grid);
    paint();
    return wrap;
  };

  /* ---------------------------------------------------------------- models */
  let modelQuery = '', modelProv = '';
  PAGES.models = {
    title: 'Models',
    render(v) {
      const conn = connected().filter((p) => p.enabled);
      v.append(pageHead('Models', h('span', {}, 'Every model you can call, named ', h('code', { text: 'provider/model' }), '. The list comes live from your connected providers.'),
        playKey ? [btn('Refresh', 'cx-btn-outline', () => { modelsCache = null; render(); }, 'refresh'), btn('Forget key', 'cx-btn-ghost', () => { setPlayKey(''); render(); })] : []));
      const freeOrder = conn.filter((p) => !p.cat.paid).map((p) => p.cat.name);
      v.append(h('section', { class: 'panel' }, h('div', { class: 'panel-body row', style: { alignItems: 'flex-start', gap: '16px', flexWrap: 'nowrap' } },
        h('span', { class: 'prov-logo lg', style: { background: 'var(--c-accent-bg)', color: 'var(--c-accent)', borderColor: 'var(--c-accent-line)' } }, ico('spark')),
        h('div', { style: { minWidth: 0 } }, h('h2', { style: { fontSize: '15px', fontWeight: 600 } }, h('code', { text: 'astro/auto' }), ' · automatic routing'),
          h('p', { class: 'muted', style: { margin: '4px 0 0' } }, freeOrder.length
            ? `Tries the default model of your free-tier providers in this order, moving on if one is rate-limited or down (up to 4 attempts): ${freeOrder.join(' → ')}.`
            : 'Connect a free-tier provider to use astro/auto. Paid providers are only used when you name their model or set "allow_paid": true.')))));
      if (!conn.length) { v.append(h('section', { class: 'panel mt-16' }, empty('box', 'No models yet', 'Models appear here once you connect and enable a provider.', btn('Connect a provider', 'cx-btn-primary', () => go('providers?browse'))))); return; }
      if (!playKey) {
        v.append(h('div', { class: 'mt-16' }, keyGate(() => render())));
        v.append(panel('Connected providers', 'Run a test to see how many models each provider offers.', providerSummary(conn), { flush: true }));
        return;
      }
      const holder = h('section', { class: 'panel mt-16' }, h('div', { class: 'panel-body stack' }, [1, 2, 3, 4, 5, 6].map(() => h('div', { class: 'skel', style: { height: '18px' } }))));
      v.append(holder);
      fetchModels().then((list) => {
        if (!holder.isConnected) return;
        const models = list.filter((m) => m.id !== 'astro/auto');
        const provs = [...new Set(models.map((m) => m.provider))];
        const q = h('input', { type: 'search', placeholder: `Search ${nf(models.length)} models`, value: modelQuery, 'aria-label': 'Search models' });
        const sel = h('select', { 'aria-label': 'Filter by provider', style: { width: 'auto', minWidth: '180px' } }, h('option', { value: '', text: 'All providers' }), provs.map((p) => h('option', { value: p, text: catById(p).name, selected: p === modelProv })));
        const tbody = h('tbody'); const count = h('span', { class: 'faint' });
        const paint = () => {
          const s = modelQuery.toLowerCase();
          const rows = models.filter((m) => (!modelProv || m.provider === modelProv) && (!s || m.id.toLowerCase().includes(s)));
          count.textContent = `${nf(rows.length)} model${rows.length === 1 ? '' : 's'}`;
          tbody.replaceChildren(...rows.slice(0, 400).map((m) => h('tr', {},
            h('td', {}, h('code', { text: m.id })),
            h('td', { class: 'hide-sm' }, h('div', { class: 'cell-main' }, provLogo(catById(m.provider)), h('span', { text: catById(m.provider).name }))),
            h('td', {}, h('span', { class: 'pill ' + (m.paid ? 'warn' : 'ok'), text: m.paid ? 'Paid' : 'Free tier' })),
            h('td', { class: 'actions' }, h('div', { class: 'row', style: { justifyContent: 'flex-end', flexWrap: 'nowrap' } }, copyBtn(m.id), h('button', { type: 'button', class: 'cx-btn cx-btn-ghost cx-btn-xs', text: 'Try', onclick: () => { chat.model = m.id; if (m.paid) chat.allowPaid = true; go('playground'); } }))))),
            rows.length > 400 ? h('tr', {}, h('td', { colspan: '4', class: 'muted', text: `Showing 400 of ${nf(rows.length)}. Search to narrow the list.` })) : null);
          if (!rows.length) tbody.replaceChildren(h('tr', {}, h('td', { colspan: '4' }, empty('search', 'No models match', 'Try a different search or provider.'))));
        };
        q.oninput = debounce(() => { modelQuery = q.value; paint(); }, 100);
        sel.onchange = () => { modelProv = sel.value; paint(); };
        holder.replaceChildren(
          h('div', { class: 'panel-head' }, h('div', { class: 'toolbar', style: { margin: 0, flex: 1 } }, h('div', { class: 'search' }, ico('search'), q), sel, h('div', { class: 'spacer' }), count)),
          h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' }, h('thead', {}, h('tr', {}, h('th', { text: 'Model id' }), h('th', { class: 'hide-sm', text: 'Provider' }), h('th', { text: 'Type' }), h('th', { class: 'actions' }, h('span', { class: 'sr', text: 'Actions' })))), tbody)));
        paint();
      }).catch((e) => { if (holder.isConnected) holder.replaceChildren(h('div', { class: 'panel-body' }, banner('warn', 'alert', e.message, playKey ? null : btn('Enter key', 'cx-btn-outline cx-btn-sm', () => render())))); });
    },
  };
  const providerSummary = (conn) => h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' },
    h('thead', {}, h('tr', {}, h('th', { text: 'Provider' }), h('th', { text: 'Models' }), h('th', { class: 'hide-sm', text: 'Examples' }), h('th', { class: 'actions' }, h('span', { class: 'sr', text: 'Actions' })))),
    h('tbody', {}, conn.map((p) => {
      const t = tests[p.provider];
      const b = btn(t ? 'Re-test' : 'Test', 'cx-btn-outline cx-btn-xs', async () => { busy(b, true, 'Testing'); try { await testProvider(p.provider, true); } catch (e) { toast(e.message, true); } render(); });
      return h('tr', {}, h('td', {}, h('div', { class: 'cell-main' }, provLogo(p.cat), h('b', { text: p.cat.name }))),
        h('td', { class: 'muted' }, t ? (t.ok ? nf(t.models) : h('span', { class: 'pill bad', text: 'Failed' })) : '—'),
        h('td', { class: 'hide-sm' }, t && t.sample ? h('div', { class: 'row' }, t.sample.slice(0, 3).map((s) => h('code', { class: 'pill', text: s }))) : h('span', { class: 'faint', text: 'Run a test to list models' })),
        h('td', { class: 'actions' }, b));
    }))));

  /* ---------------------------------------------------------------- usage */
  let usageRange = 30;
  PAGES.usage = {
    title: 'Usage',
    render(v) {
      const days = lastDays(usageRange);
      const total = sum(days), peak = days.reduce((a, b) => (b.n > a.n ? b : a), days[0]);
      const activeDays = days.filter((d) => d.n).length;
      const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Date range' }, [7, 14, 30].map((n) => h('button', { type: 'button', 'aria-pressed': String(usageRange === n), text: `${n} days`, onclick: () => { usageRange = n; render(); } })));
      v.append(pageHead('Usage', 'Requests made with your Astro keys. Counts are per UTC day and include failed attempts that reached a provider.', [seg, btn('Export CSV', 'cx-btn-outline', exportCsv, 'download')]));
      const today = Number(state.today || 0), pct = (today / DAILY_LIMIT) * 100;
      const next = new Date(); next.setUTCHours(24, 0, 0, 0);
      const mins = Math.round((next - Date.now()) / 60000);
      v.append(h('div', { class: 'grid grid-4' },
        stat(`Total, ${usageRange} days`, 'activity', nf(total), `${activeDays} active day${activeDays === 1 ? '' : 's'}`),
        stat('Daily average', 'chart', nf(Math.round(total / usageRange)), 'Requests per day'),
        stat('Peak day', 'zap', nf(peak.n), peak.n ? new Date(peak.day + 'T00:00:00Z').toLocaleDateString(undefined, { month: 'short', day: 'numeric', timeZone: 'UTC' }) : 'No requests yet'),
        stat('Today', 'clock', [nf(today), h('small', { text: `/ ${nf(DAILY_LIMIT)}` })], `Resets in ${Math.floor(mins / 60)} h ${mins % 60} min`, meterEl(pct)),
      ));
      v.append(panel('Requests per day', `Last ${usageRange} days (UTC)`, barChart(days)));
      const keys = [...state.keys].sort((a, b) => new Date(b.last_used_at || 0) - new Date(a.last_used_at || 0));
      v.append(h('div', { class: 'grid grid-2' },
        panel('Limits', null, h('dl', { class: 'kv' },
          h('dt', { text: 'Daily requests' }), h('dd', {}, `${nf(DAILY_LIMIT)} per account, shared by all keys`),
          h('dt', { text: 'Active keys' }), h('dd', {}, `${activeKeys().length} of ${MAX_KEYS}`),
          h('dt', { text: 'Request timeout' }), h('dd', {}, '140 seconds'),
          h('dt', { text: 'Failover' }), h('dd', {}, 'Up to 4 providers per astro/auto request'),
          h('dt', { text: 'Provider limits' }), h('dd', {}, 'Each provider\u2019s own rate limits and free-tier quotas still apply.'))),
        panel('By key', 'When each key was last used', keys.length ? h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' },
          h('thead', {}, h('tr', {}, h('th', { text: 'Key' }), h('th', { text: 'Last used' }), h('th', { text: 'Status' }))),
          h('tbody', {}, keys.slice(0, 8).map((k) => h('tr', {}, h('td', {}, h('b', { text: k.name, style: { fontWeight: 500 } }), h('span', { class: 'cell-sub mono', text: k.prefix + '…' })), h('td', { class: 'muted', text: ago(k.last_used_at) }),
            h('td', {}, h('span', { class: 'pill ' + (k.revoked_at ? 'bad' : 'ok'), text: k.revoked_at ? 'Revoked' : 'Active' }))))))) : empty('key', 'No keys', 'Create a key to see its activity.'), { flush: true }),
      ));
      const rows = [...days].reverse().filter((d) => d.n);
      v.append(panel('Daily breakdown', null, rows.length ? h('div', { class: 'tbl-wrap' }, h('table', { class: 'tbl' },
        h('thead', {}, h('tr', {}, h('th', { text: 'Date (UTC)' }), h('th', { class: 'num', text: 'Requests' }), h('th', { class: 'num', text: '% of daily limit' }))),
        h('tbody', {}, rows.map((d) => h('tr', {}, h('td', { text: new Date(d.day + 'T00:00:00Z').toLocaleDateString(undefined, { weekday: 'short', year: 'numeric', month: 'short', day: 'numeric', timeZone: 'UTC' }) }), h('td', { class: 'num', text: nf(d.n) }), h('td', { class: 'num muted', text: ((d.n / DAILY_LIMIT) * 100).toFixed(1) + '%' })))))) :
        empty('chart', 'No requests in this period', 'Make a request with one of your keys and it will appear here.', btn('Open Playground', 'cx-btn-outline', () => go('playground'))), { flush: true }));
    },
  };
  const download = (name, text, type) => { const a = h('a', { href: URL.createObjectURL(new Blob([text], { type })), download: name }); document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 500); };
  const exportCsv = () => { const days = lastDays(usageRange); download(`astro-usage-${utcDay()}.csv`, 'date,requests\n' + days.map((d) => `${d.day},${d.n}`).join('\n') + '\n', 'text/csv'); toast('Usage exported.'); };

  /* ---------------------------------------------------------------- playground */
  PAGES.playground = {
    title: 'Playground',
    render(v) {
      v.append(pageHead('Playground', 'Send test requests through the Astro API with your own keys. Each message counts toward your daily limit and your provider\u2019s limits.', [
        btn('View code', 'cx-btn-outline', viewCode, 'code'), btn('Clear chat', 'cx-btn-ghost', () => { chat.messages = []; render(); }, 'trash'),
      ]));
      if (!state.providers.some((p) => p.enabled)) { v.append(banner('warn', 'alert', [h('b', { text: 'No providers enabled. ' }), 'Connect a provider first so the Playground has a model to talk to.'], btn('Connect', 'cx-btn-outline cx-btn-sm', () => go('providers?browse')))); v.lastChild.style.marginBottom = '16px'; }
      if (!playKey) { v.append(keyGate(() => render())); return; }

      const modelSel = h('select', { 'aria-label': 'Model' }, h('option', { value: 'astro/auto', text: 'astro/auto (automatic)' }));
      if (chat.model !== 'astro/auto') modelSel.append(h('option', { value: chat.model, text: chat.model }));
      modelSel.value = chat.model;
      modelSel.onchange = () => { chat.model = modelSel.value; };
      const loadModels = () => fetchModels().then((list) => {
        const groups = {};
        for (const m of list) if (m.id !== 'astro/auto') (groups[m.provider] = groups[m.provider] || []).push(m);
        modelSel.replaceChildren(h('option', { value: 'astro/auto', text: 'astro/auto (automatic)' }),
          ...Object.entries(groups).map(([p, ms]) => h('optgroup', { label: catById(p).name + (ms[0].paid ? ' · paid' : '') }, ms.map((m) => h('option', { value: m.id, text: m.id.slice(p.length + 1) })))));
        if (![...modelSel.options].some((o) => o.value === chat.model)) modelSel.append(h('option', { value: chat.model, text: chat.model }));
        modelSel.value = chat.model;
        modelHint.textContent = `${nf(list.length - 1)} models available`;
      }).catch((e) => { modelHint.textContent = e.message; if (!playKey) render(); });
      const modelHint = h('small', { text: 'Loading models…' });
      const temp = h('input', { type: 'range', min: '0', max: '2', step: '0.1', value: String(chat.temperature), 'aria-label': 'Temperature' });
      const tempOut = h('output', { text: chat.temperature.toFixed(1) });
      temp.oninput = () => { chat.temperature = Number(temp.value); tempOut.textContent = chat.temperature.toFixed(1); };
      const maxT = h('input', { type: 'number', min: '1', max: '32000', value: String(chat.max_tokens), 'aria-label': 'Max tokens' });
      maxT.onchange = () => { chat.max_tokens = Math.max(1, Math.min(32000, Number(maxT.value) || 1024)); maxT.value = chat.max_tokens; };
      const paid = h('input', { type: 'checkbox', checked: chat.allowPaid, 'aria-label': 'Allow paid providers' });
      paid.onchange = () => { chat.allowPaid = paid.checked; };
      const sys = h('textarea', { rows: '4', placeholder: 'You are a helpful assistant.', 'aria-label': 'System prompt' }); sys.value = chat.system;
      sys.oninput = () => { chat.system = sys.value; };

      const side = h('section', { class: 'panel play-side' },
        h('label', { class: 'cx-field' }, h('span', { text: 'Model' }), modelSel, modelHint),
        h('label', { class: 'cx-field' }, h('span', { text: 'System prompt' }), sys),
        h('div', { class: 'cx-field' }, h('span', { class: 'cx-label', text: 'Temperature' }), h('div', { class: 'range-row' }, temp, tempOut)),
        h('label', { class: 'cx-field' }, h('span', { text: 'Max tokens' }), maxT),
        h('label', { class: 'check-row' }, h('span', {}, 'Allow paid providers', h('small', { text: 'Lets astro/auto use paid providers too.' })), h('span', { class: 'switch' }, paid, h('i'))),
        h('div', { class: 'row', style: { justifyContent: 'space-between' } }, h('span', { class: 'faint', style: { fontSize: '12px' } }, 'Key ', h('code', { text: playKey.slice(0, 14) + '…' })), h('button', { type: 'button', class: 'link', style: { fontSize: '12px' }, text: 'Change', onclick: () => { setPlayKey(''); render(); } })));

      const log = h('div', { class: 'chat-log', 'aria-live': 'polite' });
      const paintLog = () => {
        if (!chat.messages.length) { log.replaceChildren(h('div', { class: 'empty' }, h('div', { class: 'empty-ico' }, ico('terminal')), h('h3', { text: 'Start a conversation' }), h('p', { text: 'Messages go to the model you pick, through your own provider keys. Responses stream in as they arrive.' }))); return; }
        log.replaceChildren(...chat.messages.map(msgEl));
        log.scrollTop = log.scrollHeight;
      };
      const ta = h('textarea', { placeholder: 'Send a message…  (Enter to send, Shift+Enter for a new line)', 'aria-label': 'Message', rows: '3' });
      const sendB = btn('Send', 'cx-btn-primary cx-btn-sm', () => send(), 'send');
      const stopB = btn('Stop', 'cx-btn-outline cx-btn-sm', () => chat.ctrl && chat.ctrl.abort(), 'x');
      stopB.hidden = !chat.busy; sendB.hidden = chat.busy;
      ta.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); send(); } });
      const send = async () => {
        const text = ta.value.trim(); if (!text || chat.busy) return;
        ta.value = '';
        chat.messages.push({ role: 'user', content: text });
        const out = { role: 'assistant', content: '', pending: true };
        chat.messages.push(out); paintLog();
        chat.busy = true; sendB.hidden = true; stopB.hidden = false;
        chat.ctrl = new AbortController();
        const t0 = performance.now();
        try {
          const msgs = [...(chat.system.trim() ? [{ role: 'system', content: chat.system.trim() }] : []), ...chat.messages.filter((m) => !m.pending && !m.error).map((m) => ({ role: m.role, content: m.content }))];
          const r = await fetch(state.endpoint + '/chat/completions', {
            method: 'POST', signal: chat.ctrl.signal,
            headers: { authorization: 'Bearer ' + playKey, 'content-type': 'application/json' },
            body: JSON.stringify({ model: chat.model, messages: msgs, stream: true, temperature: chat.temperature, max_tokens: chat.max_tokens, ...(chat.allowPaid ? { astro: { allow_paid: true } } : {}) }),
          });
          out.provider = r.headers.get('x-astro-provider'); out.model = r.headers.get('x-astro-model');
          if (!r.ok) { let j = {}; try { j = await r.json(); } catch {} if (r.status === 401) setPlayKey(''); throw new Error((j.error && j.error.message) || j.error || `Request failed (${r.status}).`); }
          const ct = r.headers.get('content-type') || '';
          if (!ct.includes('event-stream') || !r.body) { const j = await r.json(); out.content = j.choices?.[0]?.message?.content || ''; out.usage = j.usage; }
          else {
            const rd = r.body.getReader(), dec = new TextDecoder(); let buf = '';
            const bodyEl = () => log.lastChild && log.lastChild.querySelector('.msg-body');
            for (;;) {
              const { value, done } = await rd.read(); if (done) break;
              buf += dec.decode(value, { stream: true });
              let i;
              while ((i = buf.indexOf('\n')) >= 0) {
                const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
                if (!line.startsWith('data:')) continue;
                const data = line.slice(5).trim(); if (data === '[DONE]') continue;
                try { const j = JSON.parse(data); const d = j.choices?.[0]?.delta?.content; if (d) { out.content += d; const b = bodyEl(); if (b) { b.textContent = out.content; log.scrollTop = log.scrollHeight; } } if (j.usage) out.usage = j.usage; } catch {}
              }
            }
          }
          if (!out.content) out.content = '(The model returned an empty response.)';
        } catch (e) {
          if (e.name === 'AbortError') { out.content = (out.content || '') + (out.content ? '\n\n' : '') + '(Stopped.)'; }
          else { out.error = true; out.content = e.message; }
        } finally {
          out.pending = false; out.ms = Math.round(performance.now() - t0);
          chat.busy = false; chat.ctrl = null;
          if (log.isConnected) { sendB.hidden = false; stopB.hidden = true; paintLog(); ta.focus(); }
          state.today = Number(state.today || 0) + 1; paintChrome();
          if (!playKey && log.isConnected) render();
        }
      };
      const chatPanel = h('section', { class: 'panel play-chat' }, log, h('div', { class: 'composer' }, ta, h('div', { class: 'composer-row' }, h('span', { text: 'Uses your connected providers. Don\u2019t paste secrets.' }), h('div', { class: 'row' }, stopB, sendB))));
      v.append(h('div', { class: 'play' }, side, chatPanel));
      paintLog(); loadModels();
      setTimeout(() => ta.focus(), 50);
    },
  };
  const msgEl = (m) => {
    const who = m.role === 'user' ? 'You' : 'Assistant';
    const metaPills = m.role === 'assistant' && !m.pending ? h('div', { class: 'msg-meta' },
      m.provider ? h('span', { class: 'pill', title: 'Provider that answered' }, catById(m.provider).name) : null,
      m.model ? h('code', { class: 'pill', text: m.model }) : null,
      m.ms ? h('span', { class: 'pill', text: (m.ms / 1000).toFixed(2) + ' s' }) : null,
      m.usage && m.usage.total_tokens ? h('span', { class: 'pill', text: nf(m.usage.total_tokens) + ' tokens' }) : null,
      !m.error ? copyBtn(m.content) : null) : null;
    return h('div', { class: 'msg ' + m.role + (m.error ? ' err' : '') },
      h('div', { class: 'msg-who' }, m.role === 'user' ? ico('user') : ico('spark'), who),
      h('div', { class: 'msg-body' + (m.pending && !m.content ? ' typing' : '') }, m.content), metaPills);
  };
  const viewCode = () => {
    const last = [...chat.messages].reverse().find((m) => m.role === 'user');
    const msgs = [...(chat.system.trim() ? [{ role: 'system', content: chat.system.trim() }] : []), { role: 'user', content: last ? last.content : 'Hello!' }];
    const body = { model: chat.model, messages: msgs, temperature: chat.temperature, max_tokens: chat.max_tokens, ...(chat.allowPaid ? { astro: { allow_paid: true } } : {}) };
    const json = JSON.stringify(body, null, 2);
    const curl = `curl ${state.endpoint}/chat/completions \\\n  -H "Authorization: Bearer $ASTRO_API_KEY" \\\n  -H "Content-Type: application/json" \\\n  -d '${json.replace(/'/g, "'\\''")}'`;
    const py = `import os\nfrom openai import OpenAI\n\nclient = OpenAI(base_url="${state.endpoint}", api_key=os.environ["ASTRO_API_KEY"])\nres = client.chat.completions.create(\n    model=${JSON.stringify(chat.model)},\n    messages=${JSON.stringify(msgs)},\n    temperature=${chat.temperature},\n    max_tokens=${chat.max_tokens},${chat.allowPaid ? '\n    extra_body={"astro": {"allow_paid": True}},' : ''}\n)\nprint(res.choices[0].message.content)`;
    const js = `import OpenAI from "openai";\n\nconst client = new OpenAI({ baseURL: "${state.endpoint}", apiKey: process.env.ASTRO_API_KEY });\nconst res = await client.chat.completions.create(${JSON.stringify(body, null, 2)});\nconsole.log(res.choices[0].message.content);`;
    const m = modal({ title: 'Code for this request', desc: 'Uses the Playground settings and your latest message. Set ASTRO_API_KEY first.', wide: true, body: [], foot: [] });
    m.setBody(h('section', { class: 'panel' }, ...codeTabs({ curl, python: py, javascript: js })));
    m.footEl.remove();
  };

  /* ---------------------------------------------------------------- settings */
  const settingsTabs = (cur) => h('div', { class: 'tabs', role: 'navigation', 'aria-label': 'Settings' },
    [['settings/profile', 'Profile'], ['settings/security', 'Security'], ['settings/data', 'Data & privacy']].map(([id, l]) => h('a', { href: '#/' + id, 'aria-current': cur === id ? 'page' : null, text: l })));
  const setRow = (title, desc, ctl) => h('div', { class: 'set-row' }, h('div', {}, h('h3', { text: title }), desc ? h('p', {}, desc) : null), h('div', { class: 'set-ctl' }, ctl));
  const provLabel = (p) => ({ email: 'Email and password', github: 'GitHub' }[p] || (p ? p[0].toUpperCase() + p.slice(1) : 'Email'));

  PAGES['settings/profile'] = {
    title: 'Profile',
    render(v) {
      v.append(pageHead('Settings', 'Manage your profile, sign-in security and data.'), settingsTabs('settings/profile'));
      const name = h('input', { type: 'text', maxlength: '60', value: meta().display_name || meta().full_name || meta().user_name || '', placeholder: 'Your name', autocomplete: 'name' });
      const saveN = btn('Save', 'cx-btn-outline', async () => {
        const val = name.value.trim();
        busy(saveN, true, 'Saving');
        try { me = await authUser({ data: { display_name: val } }, 'PUT'); paintChrome(); toast('Profile updated.'); }
        catch (e) { toast(e.message, true); }
        finally { busy(saveN, false); }
      });
      name.addEventListener('keydown', (e) => { if (e.key === 'Enter') saveN.click(); });
      const providers = (me && me.app_metadata && me.app_metadata.providers) || [(me && me.app_metadata && me.app_metadata.provider) || 'email'];
      v.append(panel('Profile', 'How you appear in the Console.', h('div', {},
        setRow('Avatar', meta().avatar_url ? 'From your GitHub account.' : 'Your initial. Sign in with GitHub to use your GitHub picture.', h('div', { class: 'profile-av' }, avatarEl())),
        setRow('Display name', 'Shown in the top bar and greetings.', h('div', { class: 'inline' }, name, saveN)),
        setRow('Email', 'Used to sign in. Contact us if you need to change it.', h('input', { type: 'email', readonly: true, value: state.user.email || '—', 'aria-label': 'Email' })),
        setRow('Sign-in methods', null, h('div', { class: 'row' }, providers.map((p) => h('span', { class: 'pill' }, p === 'github' ? ico('github') : ico('mail'), provLabel(p))))),
      ), { flush: true }));
      v.append(panel('Account details', null, h('dl', { class: 'kv' },
        h('dt', { text: 'User ID' }), h('dd', {}, copyField(state.user.id)),
        h('dt', { text: 'Member since' }), h('dd', { text: fmtDate(me && me.created_at) }),
        h('dt', { text: 'Last sign-in' }), h('dd', { text: fmtDate(me && me.last_sign_in_at, true) }),
        h('dt', { text: 'Plan' }), h('dd', {}, h('span', { class: 'pill accent', text: 'Free' }), ` ${nf(DAILY_LIMIT)} requests per day`))));
      const home = h('select', { 'aria-label': 'Start page', style: { maxWidth: '260px' } }, [['overview', 'Overview'], ['keys', 'API keys'], ['providers', 'Providers'], ['usage', 'Usage'], ['playground', 'Playground']].map(([id, l]) => h('option', { value: id, text: l, selected: (localStorage.getItem(HOME) || 'overview') === id })));
      home.onchange = () => { localStorage.setItem(HOME, home.value); toast('Start page saved.'); };
      v.append(panel('Preferences', 'Saved in this browser.', setRow('Start page', 'The page you see when you open the Console.', home), { flush: true }));
    },
  };

  PAGES['settings/security'] = {
    title: 'Security',
    render(v) {
      v.append(pageHead('Settings', 'Manage your profile, sign-in security and data.'), settingsTabs('settings/security'));
      const ids = (me && me.identities) || [];
      const hasEmail = ids.some((i) => i.provider === 'email');
      v.append(panel('Sign-in methods', 'Ways you can sign in to this account.', ids.length ? h('div', {}, ids.map((i) => {
        const d = i.identity_data || {};
        return h('div', { class: 'ident' }, h('span', { class: 'ident-ico' }, ico(i.provider === 'github' ? 'github' : 'mail')),
          h('div', { class: 'grow' }, h('b', { text: provLabel(i.provider), style: { fontWeight: 500 } }), h('span', { text: [i.provider === 'github' && d.user_name ? '@' + d.user_name : d.email, 'connected ' + fmtDate(i.created_at)].filter(Boolean).join(' · ') })),
          h('span', { class: 'pill ok' }, h('span', { class: 'dot' }), 'Active'));
      })) : h('p', { class: 'muted', text: 'Email and password' })));

      const pw1 = h('input', { type: 'password', autocomplete: 'new-password', minlength: '8', maxlength: '72', placeholder: 'New password' });
      const pw2 = h('input', { type: 'password', autocomplete: 'new-password', minlength: '8', maxlength: '72', placeholder: 'Confirm new password' });
      const meter = h('div', { class: 'pw-meter' }, h('i'), h('span'));
      pw1.oninput = () => paintMeter(meter, pw1.value);
      const pmsg = h('p', { class: 'cx-msg' });
      const pbtn = btn(hasEmail ? 'Update password' : 'Set password', 'cx-btn-primary', async () => {
        pmsg.className = 'cx-msg bad';
        if (pw1.value.length < 8) { pmsg.textContent = 'Use at least 8 characters.'; return; }
        if (pw1.value !== pw2.value) { pmsg.textContent = 'The two passwords don\u2019t match.'; return; }
        busy(pbtn, true, 'Saving…'); pmsg.textContent = '';
        try { me = await authUser({ password: pw1.value }, 'PUT'); pw1.value = pw2.value = ''; paintMeter(meter, ''); pmsg.className = 'cx-msg ok'; pmsg.textContent = hasEmail ? 'Password updated.' : `Password set. You can now also sign in with ${state.user.email}.`; toast('Password saved.'); }
        catch (e) { pmsg.textContent = e.message; }
        finally { busy(pbtn, false); }
      });
      v.append(panel(hasEmail ? 'Change password' : 'Set a password', hasEmail ? 'Use a strong password you don\u2019t use anywhere else.' : `You sign in with GitHub. Add a password to also sign in with ${state.user.email || 'your email'}.`,
        h('div', { class: 'stack', style: { maxWidth: '420px' } }, pw1, meter, pw2, pmsg, h('div', {}, pbtn))));

      const exp = session ? new Date(session.expires_at * 1000) : null;
      const ua = navigator.userAgent;
      const browser = /Edg\//.test(ua) ? 'Edge' : /Firefox\//.test(ua) ? 'Firefox' : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : 'Browser';
      const os = /Windows/.test(ua) ? 'Windows' : /Android/.test(ua) ? 'Android' : /iPhone|iPad/.test(ua) ? 'iOS' : /Mac OS X/.test(ua) ? 'macOS' : /Linux/.test(ua) ? 'Linux' : '';
      v.append(panel('Sessions', 'Where you\u2019re signed in.', h('div', {},
        h('div', { class: 'ident' }, h('span', { class: 'ident-ico' }, ico('monitor')), h('div', { class: 'grow' }, h('b', { text: `${browser}${os ? ' on ' + os : ''}`, style: { fontWeight: 500 } }), h('span', { text: `This device · token refreshes ${exp ? 'at ' + exp.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' }) : 'automatically'}` })), h('span', { class: 'pill accent', text: 'Current' }))),
        { foot: [h('span', { text: 'Signing out everywhere ends every session, including this one.' }), h('div', { class: 'row' },
          btn('Sign out', 'cx-btn-outline cx-btn-sm', () => signOut(), 'logout'),
          btn('Sign out of all devices', 'cx-btn-danger-outline cx-btn-sm', async () => { if (await confirmModal({ title: 'Sign out of all devices?', desc: 'You\u2019ll be signed out everywhere, including this browser. Your API keys keep working.', confirm: 'Sign out everywhere', danger: true })) signOut('global'); }))] }));

      v.append(panel('Keeping your keys safe', null, h('ul', { class: 'muted', style: { margin: 0, paddingLeft: '18px', display: 'grid', gap: '6px' } },
        h('li', { text: 'Keep Astro keys on servers or in environment variables. Never put them in front-end code or public repositories.' }),
        h('li', { text: 'Use a separate key for each app so you can revoke one without affecting the others.' }),
        h('li', { text: 'Revoke keys you no longer use. Revoking takes effect immediately.' }),
        h('li', { text: 'Provider keys are encrypted with AES-256-GCM and only used to call that provider. They are never shown again after you save them.' }))));
    },
  };

  PAGES['settings/data'] = {
    title: 'Data & privacy',
    render(v) {
      v.append(pageHead('Settings', 'Manage your profile, sign-in security and data.'), settingsTabs('settings/data'));
      v.append(panel('Export your data', 'Download a JSON copy of your account data: profile, key names and prefixes, connected providers and usage. Secrets are never included.',
        h('div', {}, btn('Download JSON', 'cx-btn-outline', () => {
          const data = {
            exported_at: new Date().toISOString(),
            profile: { id: state.user.id, email: state.user.email, display_name: meta().display_name || null, created_at: me && me.created_at, sign_in_methods: ((me && me.identities) || []).map((i) => i.provider) },
            api_keys: state.keys.map(({ id, name, prefix, created_at, last_used_at, revoked_at }) => ({ id, name, prefix, created_at, last_used_at, revoked_at })),
            providers: state.providers.map((p) => ({ provider: p.provider, name: catById(p.provider).name, enabled: p.enabled, key_last4: p.last4, extra: p.extra || {}, updated_at: p.updated_at })),
            usage: state.usage,
          };
          download(`astro-account-${utcDay()}.json`, JSON.stringify(data, null, 2), 'application/json'); toast('Account data downloaded.');
        }, 'download'))));
      v.append(panel('How we handle your data', null, h('dl', { class: 'kv' },
        h('dt', { text: 'Provider keys' }), h('dd', {}, 'Encrypted with AES-256-GCM before storage. Decrypted only on the server, to call that provider for you.'),
        h('dt', { text: 'Astro keys' }), h('dd', {}, 'Stored as a SHA-256 hash. We can\u2019t show a key again after it\u2019s created.'),
        h('dt', { text: 'Requests' }), h('dd', {}, 'Prompts and responses pass through to your provider and are not stored. We keep a daily request count.'),
        h('dt', { text: 'More' }), h('dd', {}, h('a', { class: 'link', href: ROOT + 'privacy.html#accounts', text: 'Read the privacy notes' })))));
      v.append(panel('Delete account', 'Permanently delete your account, every API key, every saved provider key and your usage history. Apps using your keys will stop working right away. This can\u2019t be undone.', null,
        { cls: 'danger', foot: [h('span', { text: 'Tip: export your data first.' }), btn('Delete account', 'cx-btn-danger cx-btn-sm', async () => {
          const ok = await confirmModal({ title: 'Delete your account?', desc: `This permanently deletes ${state.user.email || 'your account'}, ${state.keys.length} API key${state.keys.length === 1 ? '' : 's'} and ${state.providers.length} provider key${state.providers.length === 1 ? '' : 's'}.`, confirm: 'Delete account', danger: true, typeWord: 'DELETE' });
          if (!ok) return;
          try { await api('delete_account', { confirm: 'DELETE' }); session = save(null); state = null; setPlayKey(''); history.replaceState(null, '', location.pathname); showAuth(); setMsg('Your account was deleted.', 'ok'); }
          catch (e) { toast(e.message, true); }
        }, 'trash')] }));
      v.lastChild.querySelector('.panel-body')?.remove();
    },
  };

  /* ================================================================ boot */
  const showApp = async (fromLogin) => {
    boot.hidden = false; authEl.hidden = true;
    try {
      const s = await fresh(); if (!s) return showAuth();
      [me, state] = await Promise.all([gotrue('/user', null, s.access_token), api('list')]);
    } catch (err) {
      if (!session) return showAuth();
      boot.replaceChildren(h('p', { class: 'muted', text: 'We couldn\u2019t load your account. ' + err.message }), h('div', { class: 'row' }, btn('Try again', 'cx-btn-primary', () => location.reload()), btn('Sign out', 'cx-btn-outline', () => signOut())));
      return;
    }
    boot.hidden = true; appEl.hidden = false;
    paintChrome(); render();
    if (fromLogin) toast(`Signed in as ${state.user.email || displayName()}.`);
  };

  if (session) showApp(!!hash0.get('access_token')); else showAuth();
})();

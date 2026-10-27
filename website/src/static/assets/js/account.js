/* Astro AI account page: Supabase Auth (email + GitHub) and the Astro API dashboard.
   Talks to Supabase with plain fetch, no SDK. Only the public publishable key lives here. */
(() => {
  'use strict';
  const SB = 'https://vgtucdfretcvojcfvbzk.supabase.co';
  const PK = 'sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol';
  const FN = SB + '/functions/v1/astro-api/account';
  const STORE = 'astro.session';
  const DAILY_LIMIT = 5000;
  const LEAD = document.getElementById('acct-lead').textContent;

  const $ = (s, el = document) => el.querySelector(s);
  const h = (tag, attrs = {}, ...kids) => {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c != null && c !== false) el.append(c.nodeType ? c : document.createTextNode(String(c)));
    return el;
  };
  const toastEl = $('#toast');
  let toastT;
  const toast = (msg, bad) => {
    toastEl.textContent = msg; toastEl.hidden = false; toastEl.classList.toggle('bad', !!bad);
    clearTimeout(toastT); toastT = setTimeout(() => { toastEl.hidden = true; }, 3800);
  };
  const fmtDate = (s) => s ? new Date(s).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : 'never';
  const copyText = async (text, btn) => {
    try { await navigator.clipboard.writeText(text); }
    catch { const t = h('textarea', { style: 'position:fixed;opacity:0' }); t.value = text; document.body.append(t); t.select(); try { document.execCommand('copy'); } catch {} t.remove(); }
    if (btn) { const o = btn.textContent; btn.textContent = 'Copied'; setTimeout(() => { btn.textContent = o; }, 1500); }
  };
  document.addEventListener('click', (e) => {
    const b = e.target.closest('[data-copy-target]');
    if (b) copyText($('#' + b.dataset.copyTarget).textContent, b);
  });

  /* ------------------------------------------------------------ session */
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
    return String(m);
  };
  const gotrue = async (path, body, token) => {
    const r = await fetch(SB + '/auth/v1' + path, {
      method: body ? 'POST' : 'GET',
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

  /* ------------------------------------------------------------ OAuth return */
  const hash = new URLSearchParams(location.hash.slice(1));
  const query = new URLSearchParams(location.search);
  let oauthError = hash.get('error_description') || query.get('error_description');
  if (hash.get('access_token')) {
    session = save({ access_token: hash.get('access_token'), refresh_token: hash.get('refresh_token'), expires_in: hash.get('expires_in'), expires_at: Number(hash.get('expires_at')) || undefined });
  }
  if (hash.toString() || query.get('error')) history.replaceState(null, '', location.pathname);

  /* ------------------------------------------------------------ signed-out UI */
  const authCard = $('#auth'), dash = $('#dash'), loading = $('#loading');
  const form = $('#auth-form'), msg = $('#auth-msg'), submit = $('#auth-submit');
  let mode = 'signin';
  const setMode = (m) => {
    mode = m;
    $('#tab-in').setAttribute('aria-selected', m === 'signin');
    $('#tab-up').setAttribute('aria-selected', m === 'signup');
    $('#confirm-wrap').hidden = m !== 'signup';
    form.password.autocomplete = m === 'signup' ? 'new-password' : 'current-password';
    submit.textContent = m === 'signup' ? 'Create account' : 'Sign in';
    const foot = $('#auth-foot'); foot.textContent = '';
    foot.append(m === 'signup' ? 'Already have an account? ' : 'New here? ',
      h('a', { href: '#', 'data-switch': m === 'signup' ? 'signin' : 'signup', text: m === 'signup' ? 'Sign in' : 'Create a free account' }), '.');
    setMsg('');
  };
  const setMsg = (t, bad) => { msg.textContent = t; msg.classList.toggle('bad', !!bad); };
  document.addEventListener('click', (e) => {
    const t = e.target.closest('[data-mode],[data-switch]');
    if (t && authCard.contains(t)) { e.preventDefault(); setMode(t.dataset.mode || t.dataset.switch); }
  });
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const email = form.email.value.trim(), password = form.password.value;
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) return setMsg('Please enter a valid email address.', true);
    if (password.length < 8) return setMsg('Your password needs at least 8 characters.', true);
    if (mode === 'signup' && password !== form.confirm.value) return setMsg('The two passwords don\u2019t match.', true);
    submit.disabled = true; setMsg(mode === 'signup' ? 'Creating your account…' : 'Signing in…');
    try {
      const j = mode === 'signup'
        ? await gotrue('/signup', { email, password })
        : await gotrue('/token?grant_type=password', { email, password });
      if (!j.access_token) { setMsg('Check your inbox to confirm your email, then sign in.'); return; }
      session = save(j); form.reset(); await showDash();
    } catch (err) { setMsg(err.message, true); }
    finally { submit.disabled = false; }
  });

  const githubP = fetch(SB + '/auth/v1/settings', { headers: { apikey: PK } }).then((r) => r.json()).then((s) => !!(s.external && s.external.github)).catch(() => null);
  $('#gh').addEventListener('click', async () => {
    const githubOn = await githubP;
    if (githubOn === false) { setMsg('GitHub sign-in is being switched on. Please use email for now.', true); return; }
    const back = location.origin + location.pathname;
    location.href = `${SB}/auth/v1/authorize?provider=github&redirect_to=${encodeURIComponent(back)}`;
  });

  const showAuth = () => {
    loading.hidden = true; dash.hidden = true; authCard.hidden = false;
    $('#acct-title').innerHTML = 'One key for <em class="accent">every model.</em>';
    $('#acct-lead').textContent = LEAD;
    document.querySelectorAll('[data-account-link]').forEach((a) => { a.textContent = 'Sign in'; });
    if (oauthError) { setMsg(oauthError, true); oauthError = null; }
  };

  /* ------------------------------------------------------------ dashboard */
  let state = null, me = null;
  const catById = (id) => (state && state.catalog.find((c) => c.id === id)) || { id, name: id, requires: [] };

  const showDash = async () => {
    loading.hidden = false; authCard.hidden = true;
    try {
      const s = await fresh(); if (!s) return showAuth();
      [me, state] = await Promise.all([gotrue('/user', null, s.access_token), api('list')]);
    } catch (err) {
      toast(err.message, true);
      if (!session) return;
      loading.textContent = '';
      loading.append(h('span', { text: 'We couldn\u2019t load your account. ' }), h('button', { class: 'btn btn-ghost btn-sm', type: 'button', text: 'Try again', onclick: () => location.reload() }));
      return;
    }
    loading.hidden = true; dash.hidden = false;
    document.querySelectorAll('[data-account-link]').forEach((a) => { a.textContent = 'Account'; });
    $('#acct-title').innerHTML = 'Your <em class="accent">Astro AI</em> account';
    $('#acct-lead').textContent = 'Connect your provider keys, create an Astro key, and call every model through one endpoint.';
    render();
  };

  const render = () => {
    const email = state.user.email || (me && me.email) || '';
    const via = (me && me.app_metadata && me.app_metadata.provider) || 'email';
    const name = (me && me.user_metadata && (me.user_metadata.user_name || me.user_metadata.full_name)) || '';
    $('#who-email').textContent = email || name;
    $('#who-via').textContent = via === 'github' ? `Signed in with GitHub${name ? ' as @' + name : ''}` : 'Signed in with email';
    $('#avatar').textContent = (name || email || '?').trim().charAt(0).toUpperCase();
    $('#endpoint').textContent = state.endpoint;

    const active = state.keys.filter((k) => !k.revoked_at);
    $('#st-today').textContent = Number(state.today || 0).toLocaleString();
    $('#st-limit').textContent = DAILY_LIMIT.toLocaleString();
    $('#st-providers').textContent = state.providers.filter((p) => p.enabled).length;
    $('#st-keys').textContent = active.length;

    // usage bars, last 14 days
    const byDay = Object.fromEntries(state.usage.map((u) => [u.day, u.requests]));
    const days = [...Array(14)].map((_, i) => { const d = new Date(Date.now() - (13 - i) * 864e5).toISOString().slice(0, 10); return [d, byDay[d] || 0]; });
    const max = Math.max(1, ...days.map((d) => d[1]));
    const usage = $('#usage'); usage.textContent = '';
    for (const [d, n] of days) usage.append(h('div', { class: 'bar', title: `${d}: ${n} requests` }, h('i', { style: `height:${Math.max(3, Math.round((n / max) * 100))}%` }), h('span', { text: d.slice(8) })));

    // keys
    const keys = $('#keys'); keys.textContent = '';
    if (!state.keys.length) keys.append(h('p', { class: 'empty small muted', text: 'No keys yet. Create one above to start calling the API.' }));
    for (const k of state.keys) {
      keys.append(h('div', { class: 'tr' + (k.revoked_at ? ' revoked' : '') },
        h('div', { class: 'td grow' }, h('b', { text: k.name }), h('code', { class: 'small', text: k.prefix + '…' })),
        h('div', { class: 'td small muted', text: `Created ${fmtDate(k.created_at)}` }),
        h('div', { class: 'td small muted', text: k.revoked_at ? `Revoked ${fmtDate(k.revoked_at)}` : `Last used ${fmtDate(k.last_used_at)}` }),
        h('div', { class: 'td' }, k.revoked_at ? h('span', { class: 'badge', text: 'revoked' }) :
          h('button', { class: 'btn btn-ghost btn-sm', type: 'button', text: 'Revoke', onclick: async (e) => {
            if (!confirm(`Revoke "${k.name}"? Apps using it will stop working immediately.`)) return;
            e.target.disabled = true;
            try { await api('revoke_key', { id: k.id }); toast('Key revoked.'); await refresh(); } catch (err) { toast(err.message, true); e.target.disabled = false; }
          } }))));
    }

    // providers
    const list = $('#providers'); list.textContent = '';
    if (!state.providers.length) list.append(h('p', { class: 'empty small muted' }, 'No providers yet. Add at least one key below. Groq, Google Gemini, Mistral, Cerebras and OpenRouter all have free tiers.'));
    const sorted = [...state.providers].sort((a, b) => catById(a.provider).name.localeCompare(catById(b.provider).name));
    for (const p of sorted) {
      const c = catById(p.provider);
      const status = h('span', { class: 'small muted td-status' });
      list.append(h('div', { class: 'tr' + (p.enabled ? '' : ' off') },
        h('div', { class: 'td grow' }, h('b', { text: c.name }), h('span', { class: 'badge ' + (c.paid ? 'paid' : 'free'), text: c.paid ? 'paid' : 'free tier' }), h('code', { class: 'small muted', text: '•••• ' + (p.last4 || '') })),
        h('div', { class: 'td' }, status),
        h('div', { class: 'td actions' },
          h('label', { class: 'switch', title: p.enabled ? 'Enabled' : 'Disabled' },
            h('input', { type: 'checkbox', checked: p.enabled, 'aria-label': `Use ${c.name}`, onchange: async (e) => {
              try { await api('toggle_provider', { provider: p.provider, enabled: e.target.checked }); await refresh(); } catch (err) { toast(err.message, true); e.target.checked = !e.target.checked; }
            } }), h('i')),
          h('button', { class: 'btn btn-ghost btn-sm', type: 'button', text: 'Test', onclick: async (e) => {
            e.target.disabled = true; status.textContent = 'Testing…'; status.className = 'small muted td-status';
            try { const r = await api('test_provider', { provider: p.provider }); status.textContent = r.ok ? `✓ ${r.models} models · ${r.ms} ms` : 'No models found. Check the key.'; status.classList.add(r.ok ? 'ok' : 'bad'); }
            catch (err) { status.textContent = err.message; status.classList.add('bad'); }
            e.target.disabled = false;
          } }),
          h('button', { class: 'btn btn-ghost btn-sm', type: 'button', text: 'Remove', onclick: async (e) => {
            if (!confirm(`Remove your ${c.name} key from Astro AI?`)) return;
            e.target.disabled = true;
            try { await api('delete_provider', { provider: p.provider }); toast(`${c.name} removed.`); await refresh(); } catch (err) { toast(err.message, true); e.target.disabled = false; }
          } }))));
    }
    renderPicker();
  };

  const refresh = async () => { try { state = await api('list'); render(); } catch (err) { toast(err.message, true); } };

  /* key create */
  $('#key-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const name = e.target.name.value.trim(); if (!name) return;
    const btn = e.target.querySelector('button'); btn.disabled = true;
    try {
      const r = await api('create_key', { name });
      $('#new-key-value').textContent = r.key; $('#new-key').hidden = false; e.target.reset();
      await refresh();
    } catch (err) { toast(err.message, true); }
    btn.disabled = false;
  });
  $('#new-key-done').addEventListener('click', () => { $('#new-key').hidden = true; $('#new-key-value').textContent = ''; });

  /* provider picker */
  let picked = null;
  const search = $('#prov-search'), freeFirst = $('#prov-free');
  const renderPicker = () => {
    if (!state) return;
    const q = search.value.trim().toLowerCase();
    const saved = new Set(state.providers.map((p) => p.provider));
    let items = state.catalog.filter((c) => !q || c.name.toLowerCase().includes(q) || c.id.includes(q));
    items.sort((a, b) => (freeFirst.checked ? (a.paid - b.paid) : 0) || a.name.localeCompare(b.name));
    const box = $('#prov-list'); box.textContent = '';
    for (const c of items) {
      box.append(h('button', { type: 'button', role: 'option', class: 'prov-item' + (picked && picked.id === c.id ? ' on' : ''), 'aria-selected': !!(picked && picked.id === c.id), onclick: () => pick(c) },
        h('span', { text: c.name }), saved.has(c.id) ? h('span', { class: 'badge', text: 'connected' }) : h('span', { class: 'badge ' + (c.paid ? 'paid' : 'free'), text: c.paid ? 'paid' : 'free' })));
    }
    if (!items.length) box.append(h('p', { class: 'small muted', text: 'No provider matches that search.' }));
  };
  search.addEventListener('input', renderPicker);
  freeFirst.addEventListener('change', renderPicker);
  const pretty = (n) => n.replace(/_/g, ' ').toLowerCase().replace(/\b\w/g, (m) => m.toUpperCase()).replace(/\bAws\b/, 'AWS').replace(/\bOpenai\b/, 'OpenAI').replace(/\bUrl\b/, 'URL').replace(/\bId\b/, 'ID');
  const pick = (c) => {
    picked = c;
    const f = $('#prov-form'); f.hidden = false; f.reset();
    $('#pf-name').textContent = c.name;
    const badge = $('#pf-badge'); badge.textContent = c.paid ? 'paid · off in astro/auto unless you allow it' : 'free tier'; badge.className = 'badge ' + (c.paid ? 'paid' : 'free');
    const su = $('#pf-signup'); if (/^https:\/\//.test(c.signup || '')) { su.href = c.signup; su.hidden = false; } else su.hidden = true;
    $('#pf-keylabel').textContent = c.key ? pretty(c.key) : 'API key';
    const ex = $('#pf-extra'); ex.textContent = '';
    for (const r of c.requires || []) ex.append(h('label', { class: 'field' }, h('span', { text: pretty(r) }), h('input', { name: 'x_' + r, required: true, maxlength: 80, pattern: '[A-Za-z0-9._-]{1,80}', autocomplete: 'off', spellcheck: 'false' })));
    $('#pf-msg').textContent = '';
    renderPicker();
    f.key.focus();
  };
  $('#pf-cancel').addEventListener('click', () => { picked = null; $('#prov-form').hidden = true; renderPicker(); });
  $('#prov-form').addEventListener('submit', async (e) => {
    e.preventDefault(); if (!picked) return;
    const f = e.target, m = $('#pf-msg'), btn = f.querySelector('[type=submit]');
    const extra = {}; for (const r of picked.requires || []) extra[r] = f['x_' + r].value.trim();
    btn.disabled = true; m.className = 'form-msg'; m.textContent = 'Saving…';
    try {
      await api('save_provider', { provider: picked.id, key: f.key.value.trim(), extra });
      m.textContent = 'Saved. Testing the key…';
      const t = await api('test_provider', { provider: picked.id }).catch((err) => ({ ok: false, err: err.message }));
      toast(t.ok ? `${picked.name} connected: ${t.models} models available.` : `${picked.name} saved, but the test failed${t.err ? ': ' + t.err : '. Check the key.'}`, !t.ok);
      picked = null; f.reset(); f.hidden = true; $('#add-prov').open = false; await refresh();
    } catch (err) { m.textContent = err.message; m.classList.add('bad'); }
    btn.disabled = false;
  });

  /* sign out + delete */
  $('#signout').addEventListener('click', async () => {
    const s = session; session = save(null);
    if (s) fetch(SB + '/auth/v1/logout', { method: 'POST', headers: { apikey: PK, authorization: 'Bearer ' + s.access_token } }).catch(() => {});
    state = null; me = null; setMode('signin'); showAuth(); toast('Signed out.');
  });
  $('#del-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (e.target.confirm.value !== 'DELETE') { toast('Type DELETE (in capitals) to confirm.', true); return; }
    if (!confirm('Permanently delete your Astro AI account and all keys?')) return;
    try { await api('delete_account', { confirm: 'DELETE' }); session = save(null); state = null; showAuth(); toast('Your account has been deleted.'); }
    catch (err) { toast(err.message, true); }
  });

  /* boot */
  setMode(query.get('mode') === 'signup' ? 'signup' : 'signin');
  if (session) showDash(); else showAuth();
})();

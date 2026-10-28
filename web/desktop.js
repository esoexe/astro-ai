// A.S.T.R.O AI desktop shell: OpenCode-style session tabs, a projects/sessions sidebar and a review
// toggle on top of the normal app. It only runs when <html data-shell="desktop"> is set, which happens
// inside the desktop app (or in a browser with ?desktop=1 for previews). Nothing changes on the web.
(() => {
  'use strict';
  const root = document.documentElement;
  if (root.dataset.shell !== 'desktop') return;
  const A = window.AstroApp;
  if (!A) return;
  const native = window.__ASTRO_DESKTOP__ || null; // set by the desktop launcher before page scripts run
  const $ = (s, r = document) => r.querySelector(s);
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const ico = (id) => `<svg aria-hidden="true"><use href="#${id}"/></svg>`;
  const SVG = {
    side: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3.5" y="4.5" width="17" height="15" rx="2.5"/><path d="M9.5 4.5v15"/></svg>',
    review: '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3.5" y="4.5" width="17" height="15" rx="2.5"/><path d="M14.5 4.5v15"/></svg>',
    // Same mark as the app icon: a peach core inside an elliptical orbit, with one moon.
    mark: '<svg class="dt-mark" viewBox="0 0 24 24" aria-hidden="true"><defs><linearGradient id="dt-core" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffd2b0"/><stop offset="1" stop-color="#f08a4b"/></linearGradient></defs><ellipse cx="12" cy="12.4" rx="10.2" ry="6.6" fill="none" stroke="#f3a679" stroke-width="1.3"/><circle cx="11.7" cy="12.2" r="4.3" fill="url(#dt-core)" stroke="none"/><circle cx="16.4" cy="6.45" r="1.55" fill="#ffd2b0" stroke="none"/></svg>',
  };
  const VIEWS = {
    assistant: ['Chat', 'i-chat'], workspace: ['Workspace', 'i-code'], agent: ['Agent', 'i-agent'], notes: ['Notes', 'i-note'],
    tasks: ['Tasks', 'i-task'], reader: ['Reader', 'i-read'], lab: ['Model Lab', 'i-flask'],
  };
  const KEY = 'astro_dt_tabs_v1';
  const uid = () => Math.random().toString(36).slice(2, 10);

  // ---------------------------------------------------------------------------------------------
  // Title bar with tabs
  // ---------------------------------------------------------------------------------------------
  const bar = document.createElement('div');
  bar.id = 'dt-bar';
  bar.innerHTML = `
    <div class="dt-brand" title="${esc(native?.name || 'A.S.T.R.O AI')}"><img class="dt-logo" src="/icon-192.png" alt="" width="20" height="20"><span>astro</span>${native?.edition === 'clean' ? '<em>clean</em>' : ''}</div>
    <div class="dt-tabs" role="tablist" aria-label="Open sessions"></div>
    <button type="button" class="dt-ib dt-newtab" id="dt-newtab" title="New session (Ctrl+T)" aria-label="New session">${ico('i-plus')}</button>
    <div class="dt-fill"></div>
    <button type="button" class="dt-cmd" id="dt-cmd" title="Search sessions, files and commands (Ctrl+K)">${ico('i-search')}<span>Search</span><kbd>Ctrl K</kbd></button>
    <button type="button" class="dt-ib" id="dt-side" title="Toggle sidebar (Ctrl+B)" aria-label="Toggle sidebar">${SVG.side}</button>
    <button type="button" class="dt-ib" id="dt-review" title="Toggle review panel (Ctrl+Shift+E)" aria-label="Toggle review panel">${SVG.review}</button>
    <span class="dt-sep"></span>`;
  document.body.prepend(bar);
  // Reuse the real theme + settings buttons (their listeners come along with them).
  for (const id of ['theme-toggle', 'top-settings']) { const b = document.getElementById(id); if (b) bar.append(b); }
  const tabsEl = $('.dt-tabs', bar);

  let st = { tabs: [], active: '' };
  try { const s = JSON.parse(localStorage.getItem(KEY) || 'null'); if (s && Array.isArray(s.tabs)) st = s; } catch { /* fresh */ }
  st.tabs = st.tabs.filter((t) => t && t.id && (t.kind === 'chat' || (t.kind === 'view' && VIEWS[t.view]))).slice(0, 24);
  const save = () => { try { localStorage.setItem(KEY, JSON.stringify(st)); } catch { /* ignore */ } };
  const convTitle = (id) => A.conversations().find((c) => c.id === id)?.title;

  function tabInfo(t) {
    if (t.kind === 'chat') return { label: t.ref ? (convTitle(t.ref) || 'Session') : 'New session', icon: 'i-chat' };
    const [label, icon] = VIEWS[t.view] || ['Tab', 'i-chat'];
    if (t.view === 'workspace' && A.project) return { label: `${label} · ${A.project}`, icon };
    return { label, icon };
  }
  function render() {
    const frag = document.createDocumentFragment();
    for (const t of st.tabs) {
      const { label, icon } = tabInfo(t);
      const b = document.createElement('div');
      b.className = `dt-tab${t.id === st.active ? ' active' : ''}`;
      b.dataset.id = t.id; b.setAttribute('role', 'tab'); b.tabIndex = 0;
      b.setAttribute('aria-selected', String(t.id === st.active));
      b.title = label;
      b.innerHTML = `${ico(icon)}<span class="dt-tab-t">${esc(label)}</span><span class="dt-busy"></span><button type="button" class="dt-x" aria-label="Close tab" title="Close (Ctrl+W)">${ico('i-x')}</button>`;
      frag.append(b);
    }
    tabsEl.replaceChildren(frag);
    tabsEl.querySelector('.dt-tab.active')?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    $('#dt-review').hidden = !['agent', 'workspace'].includes(A.view);
    markBusy();
  }
  function insertTab(t) {
    const i = st.tabs.findIndex((x) => x.id === st.active);
    st.tabs.splice(i < 0 ? st.tabs.length : i + 1, 0, t);
  }
  // Follow the app: whatever it is showing must be the active tab.
  function sync() {
    const v = A.view; let t;
    if (v === 'assistant') {
      const id = A.activeId;
      t = id ? st.tabs.find((x) => x.kind === 'chat' && x.ref === id) : st.tabs.find((x) => x.kind === 'chat' && !x.ref);
      if (!t && id) { const cur = st.tabs.find((x) => x.id === st.active); if (cur && cur.kind === 'chat' && !cur.ref) { cur.ref = id; t = cur; } }
      if (!t) { t = { id: uid(), kind: 'chat', ref: id || null }; insertTab(t); }
    } else {
      t = st.tabs.find((x) => x.kind === 'view' && x.view === v);
      if (!t) { t = { id: uid(), kind: 'view', view: v }; insertTab(t); }
    }
    st.active = t.id;
    // Drop tabs whose chat was deleted.
    const ids = new Set(A.conversations().map((c) => c.id));
    st.tabs = st.tabs.filter((x) => x.kind !== 'chat' || !x.ref || ids.has(x.ref) || x.id === st.active);
    save(); render();
  }
  function activate(t) {
    if (!t) return;
    if (t.kind === 'chat') { if (t.ref) { if (A.activeId !== t.ref || A.view !== 'assistant') A.openConversation(t.ref); } else if (A.activeId || A.view !== 'assistant') A.newConversation(); else { st.active = t.id; render(); } }
    else A.switchView(t.view);
  }
  function closeTab(id) {
    const i = st.tabs.findIndex((x) => x.id === id);
    if (i < 0) return;
    const t = st.tabs[i];
    if (t.kind === 'chat' && t.id === st.active && A.loading) { A.toast('Stop or wait for the current answer first'); return; }
    st.tabs.splice(i, 1);
    if (id === st.active) {
      const next = st.tabs[i] || st.tabs[i - 1];
      if (next) activate(next); else { st.active = ''; A.newConversation(); }
    }
    save(); render();
  }
  function newTab() {
    const blank = st.tabs.find((x) => x.kind === 'chat' && !x.ref);
    if (blank && blank.id === st.active && A.view === 'assistant' && !A.activeId) { $('#message-input')?.focus(); return; }
    A.newConversation();
  }
  function markBusy() {
    const busy = A.loading;
    for (const el of tabsEl.children) el.classList.toggle('busy', busy && el.classList.contains('active') && st.tabs.find((t) => t.id === el.dataset.id)?.kind === 'chat');
  }
  setInterval(markBusy, 700);

  tabsEl.addEventListener('click', (e) => {
    const tab = e.target.closest('.dt-tab'); if (!tab) return;
    if (e.target.closest('.dt-x')) { e.stopPropagation(); closeTab(tab.dataset.id); return; }
    activate(st.tabs.find((t) => t.id === tab.dataset.id));
  });
  tabsEl.addEventListener('auxclick', (e) => { const tab = e.target.closest('.dt-tab'); if (tab && e.button === 1) { e.preventDefault(); closeTab(tab.dataset.id); } });
  tabsEl.addEventListener('keydown', (e) => { const tab = e.target.closest('.dt-tab'); if (tab && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); activate(st.tabs.find((t) => t.id === tab.dataset.id)); } });
  tabsEl.addEventListener('wheel', (e) => { if (Math.abs(e.deltaY) > Math.abs(e.deltaX)) { tabsEl.scrollLeft += e.deltaY; e.preventDefault(); } }, { passive: false });
  $('#dt-newtab').addEventListener('click', newTab);
  $('#dt-cmd').addEventListener('click', () => A.openPalette());
  $('#dt-side').addEventListener('click', toggleSide);
  $('#dt-review').addEventListener('click', toggleReview);

  function toggleSide() {
    const off = root.dataset.dtSide === 'off';
    if (off) delete root.dataset.dtSide; else root.dataset.dtSide = 'off';
    try { localStorage.setItem('astro_dt_side_v1', off ? 'on' : 'off'); } catch { /* ignore */ }
  }
  if (localStorage.getItem('astro_dt_side_v1') === 'off') root.dataset.dtSide = 'off';
  function toggleReview() {
    if (A.view === 'agent') $('#ag-side-btn')?.click();
    else if (A.view === 'workspace') $('#ws-toggle-team')?.click();
  }

  // Keyboard: OpenCode-style tab keys. Ctrl+B is taken over here so it toggles the whole sidebar.
  document.addEventListener('keydown', (e) => {
    const mod = e.ctrlKey || e.metaKey;
    if (!mod || e.altKey) return;
    const k = e.key.toLowerCase();
    let handled = true;
    if (k === 't' && !e.shiftKey) newTab();
    else if (k === 'w' && !e.shiftKey) { if (st.active) closeTab(st.active); }
    else if (k === 'b' && !e.shiftKey) toggleSide();
    else if (k === 'e' && e.shiftKey) toggleReview();
    else if (e.key === 'Tab') { const i = st.tabs.findIndex((t) => t.id === st.active); const n = st.tabs.length; if (n > 1) activate(st.tabs[(i + (e.shiftKey ? n - 1 : 1)) % n]); }
    else if (/^[1-9]$/.test(e.key) && !e.shiftKey) { const t = e.key === '9' ? st.tabs[st.tabs.length - 1] : st.tabs[Number(e.key) - 1]; if (t) activate(t); }
    else handled = false;
    if (handled) { e.preventDefault(); e.stopImmediatePropagation(); }
  }, true);

  // ---------------------------------------------------------------------------------------------
  // Sidebar: spaces, projects, sessions
  // ---------------------------------------------------------------------------------------------
  const rail = $('#sidebar');
  const nav = $('.rail-nav', rail);
  const recents = $('.rail-recents', rail);
  const spacesH = document.createElement('div');
  spacesH.className = 'dt-h'; spacesH.textContent = 'Spaces';
  nav.before(spacesH);
  const proj = document.createElement('div');
  proj.className = 'dt-projects';
  proj.innerHTML = `<div class="dt-h"><span>Projects</span><button type="button" class="dt-mini" id="dt-proj-add" title="New or manage projects" aria-label="New project">${ico('i-plus')}</button></div><div class="dt-plist" id="dt-plist"><div class="dt-empty">Loading…</div></div>`;
  recents.before(proj);
  const sessH = $('.rail-section-head', recents);
  const sessT = sessH?.querySelector('span'); if (sessT) sessT.textContent = 'Sessions';
  let projCache = []; let projAt = 0;
  async function loadProjects(force = false) {
    if (!force && Date.now() - projAt < 15000) { renderProjects(); return; }
    projAt = Date.now();
    try { projCache = await A.projects(); } catch { projCache = projCache || []; }
    renderProjects();
  }
  function renderProjects() {
    const list = $('#dt-plist');
    if (!projCache.length) { list.innerHTML = '<div class="dt-empty">No projects yet</div>'; return; }
    const cur = A.view === 'workspace' ? A.project : '';
    list.innerHTML = projCache.slice(0, 30).map((p) => `<button type="button" class="dt-prow${p.name === cur ? ' on' : ''}" data-name="${esc(p.name)}" title="Open ${esc(p.name)} in the Workspace">${ico('i-folder')}<span>${esc(p.name)}</span></button>`).join('');
  }
  $('#dt-plist').addEventListener('click', (e) => { const r = e.target.closest('.dt-prow'); if (r) A.openProject(r.dataset.name); });
  $('#dt-proj-add').addEventListener('click', () => { A.switchView('workspace'); setTimeout(() => $('#ws-proj')?.click(), 350); });

  // ---------------------------------------------------------------------------------------------
  // Native bridges: external links, downloads and blob pages go through the launcher.
  // ---------------------------------------------------------------------------------------------
  const hasNative = typeof window.astroOpenExternal === 'function';
  async function blobToB64(blob) {
    const buf = new Uint8Array(await blob.arrayBuffer());
    let s = ''; for (let i = 0; i < buf.length; i += 0x8000) s += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
    return btoa(s);
  }
  async function saveHref(href, name) {
    try {
      const blob = await (await fetch(href)).blob();
      const r = await window.astroSaveFile(name || 'download', await blobToB64(blob));
      if (r && r.ok) A.toast(`Saved to ${r.dir_name || 'Downloads'}: ${r.name}`, '', { label: 'Show', run: () => window.astroReveal(r.path) });
      else A.toast(`Could not save: ${r?.error || 'unknown error'}`, 'error');
    } catch (err) { A.toast(`Could not save: ${err.message}`, 'error'); }
  }
  async function openBlob(href, name) {
    try { const blob = await (await fetch(href)).blob(); await window.astroOpenData(name, await blobToB64(blob)); }
    catch (err) { A.toast(`Could not open: ${err.message}`, 'error'); }
  }
  function external(url) {
    let u; try { u = new URL(url, location.href); } catch { return false; }
    if (u.protocol === 'blob:') { openBlob(u.href, 'page.html'); return true; }
    if (!/^(https?|mailto):$/.test(u.protocol)) return false;
    window.astroOpenExternal(u.href);
    return true;
  }
  if (hasNative) {
    document.addEventListener('click', (e) => {
      const a = e.target.closest?.('a[href]'); if (!a || e.defaultPrevented) return;
      const href = a.getAttribute('href') || '';
      if (a.hasAttribute('download')) { e.preventDefault(); saveHref(a.href, a.getAttribute('download') || href.split('/').pop()); return; }
      if (href.startsWith('#') || href.startsWith('javascript:')) return;
      let u; try { u = new URL(a.href); } catch { return; }
      if (u.origin !== location.origin || a.target === '_blank') { e.preventDefault(); external(a.href); }
    }, true);
    const click = HTMLAnchorElement.prototype.click;
    HTMLAnchorElement.prototype.click = function () {
      if (this.hasAttribute('download') && !this.isConnected) { saveHref(this.href, this.getAttribute('download')); return; }
      return click.call(this);
    };
    window.open = (url) => { if (url) external(String(url)); return null; };
  }

  // ---------------------------------------------------------------------------------------------
  // Boot: restore tabs, then follow the app.
  // ---------------------------------------------------------------------------------------------
  // Several events can fire inside one app action (switchView, then the list render); sync once, at the end.
  let syncQueued = false;
  document.addEventListener('astro:change', () => {
    if (syncQueued) return;
    syncQueued = true;
    queueMicrotask(() => { syncQueued = false; sync(); if (A.view === 'workspace') loadProjects(); else renderProjects(); });
  });
  const start = st.tabs.find((t) => t.id === st.active);
  if (start && start.kind === 'chat' && start.ref && convTitle(start.ref)) A.openConversation(start.ref);
  else if (start && start.kind === 'view') A.switchView(start.view);
  else sync();
  loadProjects(true);
  if (native) root.dataset.dtOs = native.platform || '';
})();

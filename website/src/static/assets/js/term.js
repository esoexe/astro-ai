/* A.S.T.R.O AI terminal player.
   Replays REAL recordings of the `astro` terminal app (captured with `script`, see trailer3/mksessions.js)
   through a tiny terminal emulator: text, colours, \r, cursor-up redraws and line clears.
   Works in the browser ([data-term] elements) and in Node (require('./term.js').Screen) for tests. */
(function (root) {
  'use strict';

  // ------------------------------------------------------------------ emulator
  const PAL = { 30: '#3f3f46', 31: '#f87171', 32: '#6ee7b7', 33: '#fbbf24', 34: '#8b9cff', 35: '#c4a7ff', 36: '#67e8f9', 37: '#e4e4e7',
    90: '#71717a', 91: '#fca5a5', 92: '#a7f3d0', 93: '#fde68a', 94: '#a5b4fc', 95: '#ddd6fe', 96: '#a5f3fc', 97: '#fafafa' };

  function Screen(cols) {
    this.cols = cols || 96; this.lines = [[]]; this.x = 0; this.y = 0;
    this.st = { b: 0, d: 0, i: 0, fg: '' }; this.pending = '';
  }
  Screen.prototype.line = function (y) { while (this.lines.length <= y) this.lines.push([]); return this.lines[y]; };
  Screen.prototype.put = function (ch) {
    if (this.x >= this.cols) { this.x = 0; this.y++; }
    const L = this.line(this.y);
    while (L.length < this.x) L.push({ c: ' ', s: null });
    L[this.x] = { c: ch, s: (this.st.b || this.st.d || this.st.i || this.st.fg) ? Object.assign({}, this.st) : null };
    this.x++;
  };
  Screen.prototype.sgr = function (params) {
    const p = params.length ? params.split(';').map(Number) : [0];
    for (let i = 0; i < p.length; i++) {
      const n = p[i];
      if (n === 0) this.st = { b: 0, d: 0, i: 0, fg: '' };
      else if (n === 1) this.st.b = 1;
      else if (n === 2) this.st.d = 1;
      else if (n === 3) this.st.i = 1;
      else if (n === 22) { this.st.b = 0; this.st.d = 0; }
      else if (n === 23) this.st.i = 0;
      else if (n === 39) this.st.fg = '';
      else if (PAL[n]) this.st.fg = 'c' + n;   // colour classes (no inline styles: CSP-friendly)
      else if (n === 38 && p[i + 1] === 2) { const rgb = `${p[i + 2]},${p[i + 3]},${p[i + 4]}`; this.st.fg = rgb === '243,166,121' ? 'cp' : `rgb(${rgb})`; i += 4; }
      else if (n === 38 && p[i + 1] === 5) { i += 2; }
    }
  };
  Screen.prototype.write = function (s) {
    s = this.pending + s; this.pending = '';
    for (let i = 0; i < s.length; i++) {
      const ch = s[i];
      if (ch === '\x1b') {
        if (s[i + 1] !== '[') { i++; continue; }
        let j = i + 2; while (j < s.length && !/[A-Za-z]/.test(s[j])) j++;
        if (j >= s.length) { this.pending = s.slice(i); return; }
        const params = s.slice(i + 2, j).replace(/^\?/, ''), cmd = s[j], n = parseInt(params, 10) || 1;
        if (cmd === 'm') this.sgr(s.slice(i + 2, j));
        else if (cmd === 'A') this.y = Math.max(0, this.y - n);
        else if (cmd === 'B') this.y += n;
        else if (cmd === 'C') this.x = Math.min(this.cols - 1, this.x + n);
        else if (cmd === 'D') this.x = Math.max(0, this.x - n);
        else if (cmd === 'G') this.x = Math.max(0, n - 1);
        else if (cmd === 'K') { const L = this.line(this.y); const m = parseInt(params, 10) || 0; if (m === 2) L.length = 0; else if (m === 0) L.length = Math.min(L.length, this.x); }
        else if (cmd === 'J') { this.lines.length = Math.min(this.lines.length, this.y + 1); this.line(this.y).length = Math.min(this.line(this.y).length, this.x); }
        i = j; continue;
      }
      if (ch === '\r') { this.x = 0; continue; }
      if (ch === '\n') { this.y++; this.x = 0; this.line(this.y); continue; }
      if (ch === '\b') { this.x = Math.max(0, this.x - 1); continue; }
      if (ch === '\x07' || ch < ' ') continue;
      this.put(ch);
    }
  };
  const escH = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  Screen.prototype.lineHTML = function (L) {
    let out = '', cur = undefined, buf = '';
    const key = (s) => s ? `${s.b}${s.d}${s.i}${s.fg}` : '';
    const flush = () => {
      if (!buf) return;
      if (!cur) out += escH(buf);
      else {
        const isCls = /^c(p|\d+)$/.test(cur.fg || '');
        const cls = [cur.b ? 'b' : '', cur.d ? 'd' : '', cur.i ? 'i' : '', isCls ? cur.fg : ''].filter(Boolean).join(' ');
        out += `<span${cls ? ` class="${cls}"` : ''}${cur.fg && !isCls ? ` style="color:${cur.fg}"` : ''}>${escH(buf)}</span>`;
      }
      buf = '';
    };
    for (const cell of L) { const c = cell || { c: ' ', s: null }; if (key(c.s) !== key(cur)) { flush(); cur = c.s; } buf += c.c; }
    flush();
    return out;
  };
  Screen.prototype.html = function (from) { return this.lines.slice(from || 0).map((L) => this.lineHTML(L)).join('\n'); };
  Screen.prototype.text = function () { return this.lines.map((L) => L.map((c) => (c ? c.c : ' ')).join('').replace(/\s+$/, '')).join('\n'); };

  if (typeof module !== 'undefined' && module.exports) { module.exports = { Screen }; return; }
  root.AstroTermScreen = Screen;

  // ------------------------------------------------------------------ player (browser)
  const reduced = root.matchMedia && root.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function Player(el) {
    const all = root.ASTRO_TERM_SESSIONS || {};
    const ids = (el.getAttribute('data-term') || '').split(',').map((s) => s.trim()).filter((id) => all[id]);
    if (!ids.length) return;
    const body = el.querySelector('[data-term-body]'), tabs = el.querySelector('[data-term-tabs]');
    const title = el.querySelector('[data-term-title]'), speed = Number(el.getAttribute('data-term-speed')) || 1;
    const rows = Number(el.getAttribute('data-term-rows')) || 22, loop = el.getAttribute('data-term-loop') !== 'off';
    let idx = 0, run = 0, visible = false, started = false;

    if (tabs) {
      tabs.innerHTML = ids.map((id, i) => `<button type="button" role="tab" aria-selected="${i === 0}" data-i="${i}">${escH(all[id].tab || all[id].title)}</button>`).join('');
      tabs.addEventListener('click', (e) => { const b = e.target.closest('button[data-i]'); if (!b) return; idx = Number(b.dataset.i); play(); });
    }
    function mark() { if (tabs) tabs.querySelectorAll('button').forEach((b, i) => b.setAttribute('aria-selected', String(i === idx))); }
    function paint(scr, prompt, typed, caret) {
      const total = scr ? scr.lines.length : 0;
      const head = `<span class="t-head"><span class="t-prompt">${escH(prompt)}</span>${escH(typed)}${caret ? '<i class="t-caret"></i>' : ''}</span>`;
      let html = head;
      if (scr) { const from = Math.max(0, total - rows + 1); html = (from === 0 ? head + '\n' : '') + scr.html(from).replace(/\s+$/, ''); }
      body.innerHTML = html;
    }
    async function play() {
      const my = ++run; mark();
      const s = all[ids[idx]];
      if (title) title.textContent = s.title;
      const prompt = s.prompt || '~/todo-api $ ';
      if (reduced) { const scr = new Screen(s.cols); s.events.forEach((e) => scr.write(e[1])); paint(scr, prompt, s.cmd, false); return; }
      paint(null, prompt, '', true); await sleep(700);
      for (let i = 1; i <= s.cmd.length; i++) { if (my !== run) return; paint(null, prompt, s.cmd.slice(0, i), true); await sleep(26 + Math.random() * 34); }
      await sleep(380);
      const scr = new Screen(s.cols);
      for (const [dt, chunk] of s.events) {
        if (my !== run) return;
        while (!visible || document.hidden) { await sleep(300); if (my !== run) return; }
        const wait = Math.min(dt, s.cap || 1400) / (speed * (s.speed || 1));
        if (wait > 8) await sleep(wait);
        scr.write(chunk); paint(scr, prompt, s.cmd, false);
      }
      await sleep(4200);
      if (my !== run || !loop) return;
      idx = (idx + 1) % ids.length; play();
    }
    if ('IntersectionObserver' in root) {
      new IntersectionObserver((es) => { visible = es[0].isIntersecting; if (visible && !started) { started = true; play(); } }, { threshold: 0.2 }).observe(el);
    } else { visible = true; started = true; play(); }
  }
  const boot = () => document.querySelectorAll('[data-term]').forEach((el) => Player(el));
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})(typeof window !== 'undefined' ? window : this);

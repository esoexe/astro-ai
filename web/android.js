// A.S.T.R.O AI for Android: adapts the web app to the Android shell (android/), which runs the same C++ engine
// on the phone and shows this page in a WebView. Does nothing anywhere else (no window.AstroAndroid bridge).
//
// The Android WebView has no Web Speech API, so this file provides speechSynthesis / SpeechSynthesisUtterance
// and SpeechRecognition on top of Android's own TextToSpeech and SpeechRecognizer (through the bridge). The rest
// of the app uses them exactly as in a browser. It also saves downloads to the phone's Downloads folder and
// opens outside links in the browser.
(() => {
  'use strict';
  const B = window.AstroAndroid;
  if (!B) return;
  // One-time secret for the calls that touch the microphone, files or other apps. Only this page gets it: the
  // shell hands it out once per page load, before any frame (such as a preview of code you run) exists.
  const NONCE = B.claim ? B.claim() : '';
  let info = {};
  try { info = JSON.parse(B.info() || '{}'); } catch (_e) { info = {}; }
  window.__ASTRO_ANDROID__ = Object.assign({ platform: 'android', edition: 'clean' }, info);
  document.documentElement.dataset.android = '1';

  const handlers = new Map();
  let seq = 0;
  // The shell calls this with every TTS / speech-recognition event.
  window.__astroAndroidEvent = (e) => { const h = e && handlers.get(e.id); if (h) h(e); };

  const fire = (target, type, ev) => {
    ev = ev || new Event(type);
    try { const fn = target['on' + type]; if (typeof fn === 'function') fn.call(target, ev); } catch (err) { setTimeout(() => { throw err; }); }
    try { target.dispatchEvent(ev); } catch (_e) { /* not an EventTarget */ }
  };

  // ---- Speaking: speechSynthesis on Android TextToSpeech ---------------------------------------------------
  class AndroidUtterance extends EventTarget {
    constructor(text) {
      super();
      this.text = text == null ? '' : String(text);
      this.lang = ''; this.rate = 1; this.pitch = 1; this.volume = 1; this.voice = null;
      this.onstart = null; this.onend = null; this.onerror = null; this.onboundary = null; this.onpause = null; this.onresume = null; this.onmark = null;
    }
  }
  let voices = [];
  const loadVoices = () => {
    try {
      voices = JSON.parse(B.ttsVoices() || '[]').map((v) => ({ name: v.name, lang: v.lang, voiceURI: v.name, localService: v.local !== false, default: !!v.default }));
    } catch (_e) { voices = []; }
  };
  const queue = [];
  let current = null;
  const synth = new EventTarget();
  synth.onvoiceschanged = null;
  synth.speaking = false; synth.pending = false; synth.paused = false;
  synth.getVoices = () => { if (!voices.length) loadVoices(); return voices.slice(); };
  synth.speak = (u) => { queue.push(u); synth.pending = true; next(); };
  synth.cancel = () => {
    queue.length = 0;
    const c = current; current = null;
    synth.speaking = false; synth.pending = false;
    try { B.ttsStop(); } catch (_e) { /* engine gone */ }
    if (c) fire(c, 'end', Object.assign(new Event('end'), { utterance: c, charIndex: 0, elapsedTime: 0 }));
  };
  synth.pause = () => {};
  synth.resume = () => {};
  function next() {
    if (current || !queue.length) { synth.pending = queue.length > 0; return; }
    const u = queue.shift();
    synth.pending = queue.length > 0;
    current = u; synth.speaking = true;
    const id = 't' + (++seq);
    handlers.set(id, (e) => {
      const done = e.ev === 'end' || e.ev === 'error';
      if (current !== u) { if (done) handlers.delete(id); return; }   // cancelled: its end was already sent
      const ev = Object.assign(new Event(e.ev === 'error' ? 'error' : e.ev), { utterance: u, charIndex: 0, elapsedTime: 0 });
      if (e.ev === 'start' || e.ev === 'boundary') { fire(u, e.ev, ev); return; }
      if (!done) return;
      handlers.delete(id);
      current = null; synth.speaking = false;
      if (e.ev === 'error') ev.error = e.error || 'synthesis-failed';
      fire(u, e.ev, ev);
      next();
    });
    const lang = String(u.lang || (u.voice && u.voice.lang) || navigator.language || 'en-US');
    try { B.ttsSpeak(id, String(u.text || ''), lang, Number(u.rate) || 1, (u.voice && u.voice.name) || ''); }
    catch (err) { handlers.get(id)({ id, ev: 'error', error: String(err && err.message || err) }); }
  }
  // The shell calls this when the TextToSpeech engine is ready (voices load a moment after start).
  window.__astroAndroidVoices = () => { loadVoices(); fire(synth, 'voiceschanged'); };
  try {
    Object.defineProperty(window, 'speechSynthesis', { value: synth, configurable: true });
    Object.defineProperty(window, 'SpeechSynthesisUtterance', { value: AndroidUtterance, configurable: true, writable: true });
  } catch (_e) { /* keep the WebView's own */ }

  // ---- Listening: SpeechRecognition on Android SpeechRecognizer --------------------------------------------
  const makeResult = (text, isFinal) => {
    const r = [{ transcript: text, confidence: 0.9 }];
    r.isFinal = isFinal; r.item = (i) => r[i];
    return r;
  };
  class AndroidRecognition extends EventTarget {
    constructor() {
      super();
      this.lang = ''; this.continuous = false; this.interimResults = false; this.maxAlternatives = 1;
      this.onstart = null; this.onresult = null; this.onerror = null; this.onend = null; this.onspeechstart = null; this.onspeechend = null;
      this._id = null;
    }
    start() {
      if (this._id) throw new DOMException('Recognition has already started.', 'InvalidStateError');
      const id = 'r' + (++seq);
      this._id = id;
      const finals = [];
      handlers.set(id, (e) => {
        if (e.ev === 'start') fire(this, 'start');
        else if (e.ev === 'speechstart' || e.ev === 'speechend') fire(this, e.ev);
        else if (e.ev === 'result') {
          if (e.final) finals.push(String(e.text || ''));
          else if (!this.interimResults) return;
          const list = finals.map((t) => makeResult(t, true));
          if (!e.final && e.text) list.push(makeResult(String(e.text), false));
          list.item = (i) => list[i];
          fire(this, 'result', Object.assign(new Event('result'), { results: list, resultIndex: Math.max(0, list.length - 1) }));
        } else if (e.ev === 'error') {
          fire(this, 'error', Object.assign(new Event('error'), { error: e.error || 'no-speech', message: e.message || '' }));
        } else if (e.ev === 'end') {
          handlers.delete(id);
          this._id = null;
          fire(this, 'end');
        }
      });
      B.sttStart(NONCE, id, String(this.lang || navigator.language || 'en-US'), !!this.continuous, !!this.interimResults);
    }
    stop() { if (this._id) B.sttStop(this._id); }
    abort() { if (this._id) B.sttAbort(this._id); }
  }
  if (B.sttAvailable && B.sttAvailable() === 'true') {
    window.SpeechRecognition = AndroidRecognition;
    window.webkitSpeechRecognition = AndroidRecognition;
  }

  // ---- Status bar colours follow the app's light / dark theme --------------------------------------------
  const syncTheme = () => { try { B.setTheme(document.documentElement.dataset.theme || 'auto'); } catch (_e) { /* old shell */ } };
  new MutationObserver(syncTheme).observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
  document.addEventListener('DOMContentLoaded', syncTheme);

  // ---- Links, new windows and downloads --------------------------------------------------------------------
  const openExt = (url) => B.openExternal(NONCE, String(url));
  window.astroOpenExternal = openExt;
  const sameOrigin = (u) => u.origin === location.origin;

  // Pages the app opens in a new tab (run results, previews) show in a full-screen panel instead.
  function overlay(href) {
    document.getElementById('and-overlay')?.remove();
    const box = document.createElement('div');
    box.id = 'and-overlay';
    box.setAttribute('role', 'dialog'); box.setAttribute('aria-modal', 'true'); box.setAttribute('aria-label', 'Preview');
    box.style.cssText = 'position:fixed;inset:0;z-index:2147483000;display:flex;flex-direction:column;background:var(--bg,#fff)';
    box.innerHTML = '<div style="display:flex;align-items:center;gap:8px;padding:8px 10px;border-bottom:1px solid var(--border,#ddd)">'
      + '<button type="button" class="btn" data-close>Close</button><span style="opacity:.7;font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap"></span></div>'
      + '<iframe style="flex:1;border:0;width:100%;background:#fff" sandbox="allow-scripts allow-forms allow-modals allow-popups allow-same-origin"></iframe>';
    box.querySelector('span').textContent = href.startsWith('blob:') ? 'Result' : href.replace(location.origin, '');
    box.querySelector('iframe').src = href;
    const close = () => { box.remove(); document.removeEventListener('keydown', onKey, true); };
    const onKey = (ev) => { if (ev.key === 'Escape') { ev.stopPropagation(); close(); } };
    box.querySelector('[data-close]').addEventListener('click', close);
    document.addEventListener('keydown', onKey, true);
    document.body.append(box);
    window.__astroAndroidBack = () => { if (document.getElementById('and-overlay')) { close(); return true; } return false; };
    return { closed: false, close, focus() {}, location: { href } };
  }
  window.__astroAndroidBack = () => false;

  window.open = (url, _target, _features) => {
    let u;
    try { u = new URL(String(url || ''), location.href); } catch (_e) { return null; }
    if (u.protocol === 'blob:' || u.protocol === 'data:' || sameOrigin(u)) return overlay(u.href);
    if (/^(https?|mailto|tel):$/.test(u.protocol)) openExt(u.href);
    return null;
  };

  const toB64 = (blob) => new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(String(r.result).split(',')[1] || '');
    r.onerror = () => reject(r.error || new Error('read failed'));
    r.readAsDataURL(blob);
  });
  const note = (msg, bad) => {
    const t = document.createElement('div');
    t.setAttribute('role', 'status');
    t.textContent = msg;
    t.style.cssText = 'position:fixed;left:50%;bottom:24px;transform:translateX(-50%);z-index:2147483001;max-width:88vw;padding:10px 16px;border-radius:12px;font-size:14px;'
      + 'box-shadow:0 6px 24px rgba(0,0,0,.18);' + (bad ? 'background:#b42323;color:#fff' : 'background:#16181a;color:#f3f4f3');
    document.body.append(t);
    setTimeout(() => t.remove(), 3500);
  };

  document.addEventListener('click', (ev) => {
    const a = ev.target && ev.target.closest ? ev.target.closest('a[href]') : null;
    if (!a) return;
    let u;
    try { u = new URL(a.href, location.href); } catch (_e) { return; }
    if (a.hasAttribute('download') && (u.protocol === 'blob:' || u.protocol === 'data:' || sameOrigin(u))) {
      ev.preventDefault(); ev.stopImmediatePropagation();
      const name = a.getAttribute('download') || u.pathname.split('/').pop() || 'download';
      fetch(u.href).then((r) => r.blob()).then(async (blob) => {
        const res = JSON.parse(B.saveFile(NONCE, name, blob.type || 'application/octet-stream', await toB64(blob)) || '{}');
        if (res.ok) note('Saved to Downloads: ' + (res.name || name)); else note('Could not save the file: ' + (res.error || 'unknown error'), true);
      }).catch((err) => note('Could not save the file: ' + (err && err.message || err), true));
      return;
    }
    if (!sameOrigin(u) && /^(https?|mailto|tel):$/.test(u.protocol)) {
      ev.preventDefault(); ev.stopImmediatePropagation();
      openExt(u.href);
      return;
    }
    if (sameOrigin(u) && a.target === '_blank') {
      ev.preventDefault(); ev.stopImmediatePropagation();
      overlay(u.href);
    }
  }, true);
})();

// Tests web/android.js (the Android app's bridge adapter) in Chromium with a mock of the Java bridge
// (android/app/src/main/java/.../MainActivity.java, class Bridge). Needs a running engine.
//   NODE_PATH=~/.cache/pw/node_modules PLAYWRIGHT_BROWSERS_PATH=~/.cache/pw/browsers node tests/android_shim_test.js http://127.0.0.1:4380
const { chromium } = require('playwright');
const BASE = process.argv[2] || 'http://127.0.0.1:4380';
let failed = 0;
const check = (ok, name, extra = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${ok || !extra ? '' : '  -> ' + extra}`); if (!ok) failed++; };

// Behaves like the Java Bridge: one-time nonce, async events through window.__astroAndroidEvent.
const MOCK = () => {
  const calls = []; window.__calls = calls;
  let claimed = false; const NONCE = 'n0nce-' + Math.random().toString(36).slice(2);
  window.__nonce = NONCE;
  const ev = (o) => setTimeout(() => window.__astroAndroidEvent && window.__astroAndroidEvent(o), 0);
  const later = (ms, o) => setTimeout(() => window.__astroAndroidEvent && window.__astroAndroidEvent(o), ms);
  window.AstroAndroid = {
    info: () => JSON.stringify({ platform: 'android', edition: 'clean', arch: 'arm64', version: '9.9.9', sdk: 36, device: 'Mock Phone' }),
    claim: () => { if (claimed) return ''; claimed = true; return NONCE; },
    openExternal: (n, url) => calls.push(['openExternal', n, url]),
    saveFile: (n, name, mime, b64) => { calls.push(['saveFile', n, name, mime, atob(b64)]); return n === NONCE ? JSON.stringify({ ok: true, name: 'Download/A.S.T.R.O AI/' + name }) : JSON.stringify({ ok: false, error: 'not allowed from this frame' }); },
    setTheme: (t) => calls.push(['setTheme', t]),
    ttsVoices: () => JSON.stringify([{ name: 'en-us-x-iol-local', lang: 'en-US', local: true, default: true }, { name: 'fr-fr-x-frd-local', lang: 'fr-FR', local: true }]),
    ttsSpeak: (id, text, lang, rate, voice) => { calls.push(['ttsSpeak', id, text, lang, rate, voice]); later(5, { id, ev: 'start' }); later(15, { id, ev: 'boundary' }); later(40, { id, ev: 'end' }); },
    ttsStop: () => calls.push(['ttsStop']),
    sttAvailable: () => 'true',
    sttStart: (n, id, lang, cont, interim) => {
      calls.push(['sttStart', n, id, lang, cont, interim]);
      if (n !== NONCE) { ev({ id, ev: 'error', error: 'not-allowed' }); ev({ id, ev: 'end' }); return; }
      later(5, { id, ev: 'start' }); later(10, { id, ev: 'speechstart' });
      later(20, { id, ev: 'result', text: 'hello from', final: false });
      later(40, { id, ev: 'result', text: 'hello from android', final: true });
      later(50, { id, ev: 'end' });
    },
    sttStop: (id) => calls.push(['sttStop', id]),
    sttAbort: (id) => calls.push(['sttAbort', id]),
  };
};

(async () => {
  const browser = await chromium.launch();
  const ctx = await browser.newContext({ viewport: { width: 412, height: 860 }, serviceWorkers: 'block' });
  await ctx.addInitScript(MOCK);
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto(BASE + '/?nowelcome', { waitUntil: 'load' });
  await page.waitForTimeout(1500);

  // Basics
  const basics = await page.evaluate(() => ({
    info: window.__ASTRO_ANDROID__, flag: document.documentElement.dataset.android,
    secondClaim: window.AstroAndroid.claim(),
    voices: speechSynthesis.getVoices().map((v) => v.name + '|' + v.lang),
    rec: typeof window.webkitSpeechRecognition === 'function' && window.SpeechRecognition === window.webkitSpeechRecognition,
    utt: new SpeechSynthesisUtterance('x').constructor.name,
  }));
  check(basics.info && basics.info.platform === 'android' && basics.info.version === '9.9.9', 'bridge info read (window.__ASTRO_ANDROID__)');
  check(basics.flag === '1', '<html data-android="1">');
  check(basics.secondClaim === '', 'nonce is handed out only once per page (frames cannot get it)');
  check(basics.voices.length === 2 && basics.voices[0].startsWith('en-us-x-iol-local|en-US'), 'speechSynthesis.getVoices() from Android TTS', JSON.stringify(basics.voices));
  check(basics.rec, 'SpeechRecognition provided');
  check(basics.utt === 'AndroidUtterance', 'SpeechSynthesisUtterance replaced');

  // Speaking: queue order, events, cancel
  const speak = await page.evaluate(() => new Promise((resolve) => {
    const log = [];
    const a = new SpeechSynthesisUtterance('first'); a.lang = 'en-US'; a.rate = 1.25;
    const b = new SpeechSynthesisUtterance('second'); b.voice = speechSynthesis.getVoices()[1];
    a.onstart = () => log.push('a:start'); a.onboundary = () => log.push('a:boundary'); a.onend = () => log.push('a:end');
    b.onstart = () => log.push('b:start'); b.onend = () => { log.push('b:end'); resolve({ log, calls: window.__calls.filter((c) => c[0] === 'ttsSpeak') }); };
    speechSynthesis.speak(a); speechSynthesis.speak(b);
    log.push('speaking=' + speechSynthesis.speaking);
  }));
  check(speak.log.join(',') === 'speaking=true,a:start,a:boundary,a:end,b:start,b:end', 'utterances speak in order with start/boundary/end', speak.log.join(','));
  check(speak.calls.length === 2 && speak.calls[0][3] === 'en-US' && speak.calls[0][4] === 1.25 && speak.calls[1][5] === 'fr-fr-x-frd-local' && speak.calls[1][3] === 'fr-FR', 'language, rate and voice passed to Android', JSON.stringify(speak.calls));
  const cancel = await page.evaluate(() => new Promise((resolve) => {
    const log = [];
    const a = new SpeechSynthesisUtterance('long'); a.onend = () => log.push('a:end');
    const b = new SpeechSynthesisUtterance('never'); b.onstart = () => log.push('b:start');
    speechSynthesis.speak(a); speechSynthesis.speak(b);
    setTimeout(() => { speechSynthesis.cancel(); setTimeout(() => resolve({ log, speaking: speechSynthesis.speaking, stop: window.__calls.some((c) => c[0] === 'ttsStop') }), 120); }, 8);
  }));
  check(cancel.log.join(',') === 'a:end' && !cancel.speaking && cancel.stop, 'cancel() stops, ends the current one once, drops the queue', JSON.stringify(cancel));

  // Listening through the real chat microphone button
  await page.fill('#message-input', '');
  const mic = page.locator('#voice-button');
  if (await mic.count()) {
    await mic.click();
    await page.waitForTimeout(400);
    const val = await page.inputValue('#message-input');
    const st = await page.evaluate(() => window.__calls.filter((c) => c[0] === 'sttStart').pop());
    check(val === 'hello from android', 'chat mic button dictates through Android SpeechRecognizer', JSON.stringify(val));
    check(st && st[1] === await page.evaluate(() => window.__nonce) && st[5] === true, 'sttStart carries the page nonce and interim flag', JSON.stringify(st));
  } else check(false, 'voice button present');
  const interim = await page.evaluate(() => new Promise((resolve) => {
    const r = new webkitSpeechRecognition(); r.interimResults = true; const seen = [];
    r.onresult = (e) => seen.push(Array.from(e.results).map((x) => (x.isFinal ? 'F:' : 'I:') + x[0].transcript).join('|'));
    r.onend = () => resolve(seen);
    r.start();
  }));
  check(interim.join(' / ') === 'I:hello from / F:hello from android', 'interim then final results', interim.join(' / '));

  // Downloads through the bridge
  await page.evaluate(() => { const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob(['col1,col2\n1,2\n'], { type: 'text/csv' })); a.download = 'table.csv'; document.body.append(a); a.click(); a.remove(); });
  await page.waitForTimeout(400);
  const save = await page.evaluate(() => window.__calls.find((c) => c[0] === 'saveFile'));
  check(save && save[1] === await page.evaluate(() => window.__nonce) && save[2] === 'table.csv' && save[3] === 'text/csv' && save[4] === 'col1,col2\n1,2\n', 'a[download] blob saved to Downloads with the right name, type and bytes', JSON.stringify(save));
  check(await page.locator('[role="status"]', { hasText: 'Saved to Downloads' }).count() > 0, '"Saved to Downloads" note shown');

  // Links and windows
  await page.evaluate(() => { const a = document.createElement('a'); a.href = 'https://example.com/docs'; a.textContent = 'x'; document.body.append(a); a.click(); a.remove(); window.open('https://github.com/esoexe/astro-ai', '_blank'); });
  const ext = await page.evaluate(() => window.__calls.filter((c) => c[0] === 'openExternal').map((c) => (c[1] === window.__nonce ? 'ok:' : 'BAD:') + c[2]));
  check(ext.join(' ') === 'ok:https://example.com/docs ok:https://github.com/esoexe/astro-ai', 'outside links and window.open go to the browser (with nonce)', ext.join(' '));
  const stillHere = await page.evaluate(() => location.origin);
  check(stillHere === new URL(BASE).origin, 'the app page did not navigate away');
  await page.evaluate(() => window.open(URL.createObjectURL(new Blob(['<h1 id=r>result</h1>'], { type: 'text/html' })), '_blank'));
  await page.waitForTimeout(300);
  check(await page.locator('#and-overlay iframe').count() === 1, 'blob / same-origin window.open shows the in-app preview panel');
  const frameText = await page.frameLocator('#and-overlay iframe').locator('#r').textContent().catch(() => '');
  check(frameText === 'result', 'preview panel shows the page');
  const back = await page.evaluate(() => window.__astroAndroidBack());
  check(back === true && await page.locator('#and-overlay').count() === 0, 'Android back closes the preview panel first');
  check(await page.evaluate(() => window.__astroAndroidBack()) === false, 'back with nothing open lets Android handle it');

  // Theme sync
  await page.evaluate(() => { document.documentElement.dataset.theme = 'dark'; });
  await page.waitForTimeout(100);
  check(await page.evaluate(() => window.__calls.some((c) => c[0] === 'setTheme' && c[1] === 'dark')), 'status bar follows the app theme');

  // A frame cannot use the sensitive calls (it has no nonce)
  const frameTry = await page.evaluate(() => new Promise((resolve) => {
    const f = document.createElement('iframe'); document.body.append(f);
    const w = f.contentWindow; const out = JSON.parse(window.AstroAndroid.saveFile(w.AstroAndroid ? '' : '', 'x.txt', 'text/plain', btoa('x')));
    f.remove(); resolve(out);
  }));
  check(frameTry.ok === false, 'saveFile without the nonce is refused');

  check(errors.length === 0, 'no page errors', errors.join(' | '));
  await browser.close();
  console.log(failed ? `\n${failed} Android shim check(s) failed` : '\nall Android shim checks passed');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });

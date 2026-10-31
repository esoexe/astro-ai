// pair.html: the QR code from Settings > Phone opens this page on the phone. Everything it needs is in the URL
// fragment (#c=code&n=computer&u=address...), which browsers never send to a server; it is removed from the address
// bar as soon as it is read. The button hands the same values to the Android app (astroai://pair).
(() => {
  'use strict';
  const q = new URLSearchParams(location.hash.slice(1));
  const code = (q.get('c') || '').trim();
  const name = (q.get('n') || '').slice(0, 60);
  // only plain-http addresses on private networks: the same rule the app applies
  const privateUrl = (u) => {
    const m = /^http:\/\/(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}):(\d{2,5})$/.exec(u || '');
    if (!m) return false;
    const [a, b] = [Number(m[1]), Number(m[2])];
    if ([m[1], m[2], m[3], m[4]].some((x) => Number(x) > 255) || Number(m[5]) > 65535) return false;
    return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 100 && b >= 64 && b <= 127) || (a === 169 && b === 254);
  };
  const urls = q.getAll('u').filter(privateUrl).slice(0, 3);
  if (!/^\d{8}$/.test(code) || !urls.length) return;
  if (history.replaceState) history.replaceState(null, '', location.pathname);

  const frag = new URLSearchParams();
  frag.set('c', code);
  if (name) frag.set('n', name);
  urls.forEach((u) => frag.append('u', u));
  const fallback = new URL('download.html#android', location.href).href;
  const open = document.getElementById('pair-open');
  open.href = /android/i.test(navigator.userAgent)
    ? `intent://pair?${frag}#Intent;scheme=astroai;package=io.github.esoexe.astroai;S.browser_fallback_url=${encodeURIComponent(fallback)};end`
    : `astroai://pair?${frag}`;
  if (name) document.getElementById('pair-computer').textContent = name;
  document.getElementById('pair-url').textContent = urls[0];
  document.getElementById('pair-code').textContent = `${code.slice(0, 4)} ${code.slice(4)}`;
  document.getElementById('pair-ok').hidden = false;
  document.getElementById('pair-none').hidden = true;
})();

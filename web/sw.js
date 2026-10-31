// A.S.T.R.O AI service worker: caches the app shell so notes, tasks and past chats open offline.
// API calls are never cached (they need the server and carry your session token).
const CACHE = 'astro-shell-v33';
const SHELL = ['/', '/index.html', '/styles.css', '/orbit.css', '/desktop.css', '/agent.css', '/themes.js', '/android.js', '/i18n.js', '/agent_audit.js', '/dev.js', '/logo.js', '/lsp.js', '/exthost.js', '/agents.js', '/app.js', '/desktop.js', '/icon.svg', '/favicon-32.png', '/icon-192.png', '/icon-512.png', '/manifest.webmanifest'];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener('activate', (event) => {
  event.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/api/') || url.pathname.startsWith('/preview/')) return;
  // Network first (always the newest version), cache as the offline fallback.
  event.respondWith(fetch(event.request).then((response) => {
    if (response.ok) { const copy = response.clone(); caches.open(CACHE).then((c) => c.put(event.request, copy)); }
    return response;
  }).catch(() => caches.match(event.request).then((hit) => hit || caches.match('/index.html'))));
});
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  event.waitUntil(self.clients.matchAll({ type: 'window' }).then((list) => (list[0] ? list[0].focus() : self.clients.openWindow('/'))));
});

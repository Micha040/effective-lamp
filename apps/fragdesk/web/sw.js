// FragDesk Handy-App – Service Worker (nur mit HTTPS aktiv): startet auch ohne Verbindung mit den zuletzt geladenen
// Daten. Immer zuerst das Netz, der Speicher ist nur der Rückfall. Videos werden nie gespeichert.
const CACHE = 'fragdesk-1.6.0';
const SHELL = ['./', 'index.html', 'app.css', 'manifest.webmanifest', 'icon-180.png', 'icon-192.png', 'js/app.js', 'js/api.js', 'js/ui.js',
  'js/chart.js', 'js/filters.js', 'js/radar.js', 'js/pages/pair.js', 'js/pages/overview.js', 'js/pages/matches.js', 'js/pages/match.js', 'js/pages/stats.js',
  'js/pages/moments.js', 'js/pages/settings.js', 'js/pages/player.js', 'js/pages/admin.js', 'js/pages/invite.js', 'css/match.css', 'css/stats.css',
  'js/heatmap.js', 'css/heat.css', 'js/pages/replay.js', 'css/replay.css', 'js/pages/autodl.js'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => Promise.all(SHELL.map(u => c.add(u).catch(() => {})))).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim()));
});

self.addEventListener('fetch', e => {
  const req = e.request;
  const url = new URL(req.url);
  if (req.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/media/')) return;
  if (url.pathname === '/api/v1/session' || url.pathname === '/api/v1/pair') return;   // Anmeldung nie aus dem Speicher
  if (url.pathname === '/api/v1/heat') return;   // Heatmaps: viele große Varianten je Match – nicht ansammeln
  if (url.pathname.startsWith('/api/v1/replay/')) return;   // 2D-Ansicht: 30–300 KB je Runde – nicht alle Runden speichern
  e.respondWith((async () => {
    try {
      const res = await fetch(req);
      if (res.ok) {
        const copy = res.clone();
        caches.open(CACHE).then(c => c.put(req, copy)).catch(() => {});
      }
      return res;
    } catch (err) {
      const hit = await caches.match(req, {ignoreSearch: false});
      if (hit) return hit;
      if (url.pathname.startsWith('/api/')) {
        return new Response(JSON.stringify({error: 'Keine Verbindung zu FragDesk – und diese Ansicht ist noch nicht gespeichert.'}),
          {status: 503, headers: {'Content-Type': 'application/json; charset=utf-8'}});
      }
      return (await caches.match('index.html')) || Response.error();
    }
  })());
});

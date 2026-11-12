// STAMMTISCH – Service Worker (nur mit HTTPS aktiv)
//
// 1. Die App selbst: immer zuerst aus dem Netz (so ist sie nach jedem Update sofort aktuell), der Speicher ist nur der
//    Rückfall ohne Verbindung.
// 2. Bilder: einmal geladen, kommen sie aus dem Speicher. Das spart Datenvolumen – auf dem Handy und beim kostenlosen
//    Kontingent des Servers. Die Adressen privater Bilder tragen einen wechselnden Zugangsschlüssel; gespeichert wird
//    deshalb unter der Adresse ohne ihn. Videos und Sprachnachrichten werden nie gespeichert.
// Daten (Treffen, Chat) laufen nicht über den Service Worker; ihren letzten Stand merkt sich die App selbst.
const VERSION = '1.0.0';
const CACHE = 'stammtisch-' + VERSION;
const MEDIA = 'stammtisch-bilder-1';
const MEDIA_MAX = 500;            // so viele Bilder höchstens, die ältesten fliegen zuerst raus
const SHELL = ['./', 'index.html', 'app.css', 'config.js', 'manifest.webmanifest', 'icon-180.png', 'icon-192.png', 'icon-512.png',
  'js/app.js', 'js/ui.js', 'js/util.js', 'js/media.js', 'js/map.js', 'js/backend/index.js', 'js/backend/supabase.js', 'js/backend/demo.js',
  'js/pages/welcome.js', 'js/pages/events.js', 'js/pages/event.js', 'js/pages/event-edit.js', 'js/pages/chat.js', 'js/pages/photos.js',
  'js/pages/settings.js', 'js/pages/changelog.js', 'vendor/supabase.js', 'vendor/leaflet.js', 'vendor/leaflet.css'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => Promise.all(SHELL.map(u => c.add(u).catch(() => {})))).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k.startsWith('stammtisch-') && k !== CACHE && k !== MEDIA).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});

async function trim(cache) {
  const keys = await cache.keys();
  for (const k of keys.slice(0, Math.max(0, keys.length - MEDIA_MAX))) await cache.delete(k);
}

// Private Bilder aus dem Speicher von Supabase: …/storage/v1/object/sign/media/<pfad>?token=…
async function image(req, url) {
  const key = url.origin + url.pathname;
  const cache = await caches.open(MEDIA);
  const hit = await cache.match(key);
  if (hit) return hit;
  const res = await fetch(req);
  if (res.ok && res.type !== 'opaque' && /^image\//.test(res.headers.get('Content-Type') || '')) {
    cache.put(key, res.clone()).then(() => trim(cache)).catch(() => {});
  }
  return res;
}

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.pathname.includes('/storage/v1/object/sign/media/')) {
    if (req.headers.has('range') || req.destination === 'video' || req.destination === 'audio') return;
    e.respondWith(image(req, url).catch(() => fetch(req)));
    return;
  }
  if (url.origin !== location.origin) return;     // Datenbank, Karten: unverändert durchlassen
  e.respondWith((async () => {
    try {
      const res = await fetch(req);
      if (res.ok) {
        const copy = res.clone();
        caches.open(CACHE).then(c => c.put(req, copy)).catch(() => {});
      }
      return res;
    } catch (err) {
      const hit = await caches.match(req, {ignoreSearch: true});
      if (hit) return hit;
      return (await caches.match('index.html')) || Response.error();
    }
  })());
});

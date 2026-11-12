// STAMMTISCH – Karte (Leaflet + OpenStreetMap) und Ortssuche. Wird erst geladen, wenn eine Seite wirklich eine Karte zeigt.
import {h} from './ui.js';

let loading = null;
function loadLeaflet() {
  if (window.L) return Promise.resolve(window.L);
  if (!loading) {
    loading = new Promise((resolve, reject) => {
      document.head.append(h('link', {rel: 'stylesheet', href: 'vendor/leaflet.css'}));
      const s = document.createElement('script');
      s.src = 'vendor/leaflet.js';
      s.onload = () => resolve(window.L);
      s.onerror = () => { loading = null; reject(new Error('Die Karte konnte nicht geladen werden.')); };
      document.head.append(s);
    });
  }
  return loading;
}

const PIN = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 22s-7-6.2-7-12a7 7 0 0 1 14 0c0 5.8-7 12-7 12z"/><circle cx="12" cy="10" r="2.6" fill="#5b3a00"/></svg>';

// Karte mit einer Stecknadel in el zeichnen.
// opts.interactive: verschieben und zoomen erlaubt (sonst ein ruhiges Bild); opts.onPick(lat, lon): Tippen versetzt die Nadel
export async function showMap(el, lat, lon, opts = {}) {
  // ?nomap: für Bilder der Oberfläche auf einem Rechner, der keine Kartenbilder laden kann (tools/render.sh)
  if (new URLSearchParams(location.search).has('nomap')) throw new Error('Karten sind abgeschaltet.');
  const L = await loadLeaflet();
  const on = !!opts.interactive;
  const map = L.map(el, {
    center: [lat, lon], zoom: opts.zoom || 15, zoomControl: on, dragging: on, touchZoom: on, doubleClickZoom: on,
    scrollWheelZoom: false, boxZoom: false, keyboard: false,
  });
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19, attribution: '© <a href="https://www.openstreetmap.org/copyright" target="_blank" rel="noopener">OpenStreetMap</a>',
  }).addTo(map);
  const marker = L.marker([lat, lon], {icon: L.divIcon({className: 'mappin', html: PIN, iconSize: [34, 34], iconAnchor: [17, 32]}), interactive: false}).addTo(map);
  if (opts.onPick) {
    map.on('click', ev => {
      marker.setLatLng(ev.latlng);
      opts.onPick(ev.latlng.lat, ev.latlng.lng);
    });
  }
  let gone = false;
  const timer = setTimeout(() => map.invalidateSize(), 60);   // die Größe steht erst fest, wenn die Seite gezeichnet ist
  return {
    set(newLat, newLon) {
      if (gone) return;
      marker.setLatLng([newLat, newLon]);
      map.setView([newLat, newLon], Math.max(map.getZoom(), 15), {animate: false});
    },
    remove() {
      if (gone) return;
      gone = true;
      clearTimeout(timer);
      map.remove();
    },
  };
}

function shortAddress(a, fallback) {
  if (!a) return fallback;
  const street = [a.road || a.pedestrian || a.footway || '', a.house_number || ''].join(' ').trim();
  const town = [a.postcode || '', a.city || a.town || a.village || a.municipality || ''].join(' ').trim();
  return [street, town].filter(Boolean).join(', ') || fallback;
}

// Ortssuche über OpenStreetMap (Nominatim): kostenlos und ohne Schlüssel. Bewusst nur auf Knopfdruck und nicht bei
// jedem Buchstaben – so verlangen es deren Nutzungsregeln. near = {lat, lon} bevorzugt Treffer in der Nähe.
export async function searchPlaces(query, near) {
  const params = new URLSearchParams({format: 'jsonv2', q: query, limit: '6', addressdetails: '1', 'accept-language': 'de'});
  if (near && typeof near.lat === 'number') {
    params.set('viewbox', [near.lon - 0.6, near.lat + 0.35, near.lon + 0.6, near.lat - 0.35].join(','));
  }
  let list;
  try {
    const res = await fetch('https://nominatim.openstreetmap.org/search?' + params, {headers: {Accept: 'application/json'}});
    if (!res.ok) throw new Error(String(res.status));
    list = await res.json();
  } catch (e) {
    throw new Error('Die Ortssuche ist gerade nicht erreichbar. Du kannst Name und Adresse auch einfach eintippen.');
  }
  return list.map(r => {
    const full = String(r.display_name || '');
    return {name: r.name || full.split(',')[0], address: shortAddress(r.address, full), lat: Number(r.lat), lon: Number(r.lon)};
  }).filter(r => isFinite(r.lat) && isFinite(r.lon));
}

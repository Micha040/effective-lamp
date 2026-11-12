// STAMMTISCH – reine Hilfsfunktionen ohne Browser-Abhängigkeit (Datum, Codes, Texte). Getestet in tests/util.test.js.

// ---- Codes ------------------------------------------------------------------------------------------------------------
// Einladungscode: 10 Zeichen (XXXXX-XXXXX), Gerätecode: 8 Zeichen (XXXX-XXXX). Zeichen ohne 0/O und 1/I.
export function cleanCode(s) {
  return String(s || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
}

export function codeKind(s) {
  const c = cleanCode(s);
  if (c.length === 10) return 'invite';
  if (c.length === 8) return 'device';
  return null;
}

export function formatCode(s) {
  const c = cleanCode(s);
  if (c.length <= 4) return c;
  const half = c.length > 8 ? 5 : 4;
  return c.slice(0, half) + '-' + c.slice(half);
}

// Aus der Adresse: …/#einladung=XXXXX-XXXXX oder …/#geraet=XXXX-XXXX; auch ein ganzer eingefügter Link wird verstanden
export function parseCodeInput(text) {
  const s = String(text || '');
  const m = s.match(/[#&?](?:einladung|geraet)=([A-Za-z0-9-]+)/);
  return cleanCode(m ? m[1] : s);
}

export function inviteLink(base, code) {
  return base.replace(/[#?].*$/, '') + '#einladung=' + formatCode(code);
}

// ---- Datum und Zeit ---------------------------------------------------------------------------------------------------
const WEEKDAYS = ['So.', 'Mo.', 'Di.', 'Mi.', 'Do.', 'Fr.', 'Sa.'];
const WEEKDAYS_LONG = ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag'];
const MONTHS = ['Jan.', 'Feb.', 'März', 'Apr.', 'Mai', 'Juni', 'Juli', 'Aug.', 'Sep.', 'Okt.', 'Nov.', 'Dez.'];
const two = x => String(x).padStart(2, '0');
const startOfDay = d => new Date(d.getFullYear(), d.getMonth(), d.getDate());

// Ganze Kalendertage zwischen zwei Zeitpunkten (morgen = 1, gestern = -1)
export function dayDiff(date, now = new Date()) {
  return Math.round((startOfDay(new Date(date)) - startOfDay(now)) / 86400000);
}

export function fmtTime(date) {
  const d = new Date(date);
  return two(d.getHours()) + ':' + two(d.getMinutes());
}

// "Do., 19. Nov." – mit Jahr, wenn es nicht das laufende ist
export function fmtDate(date, now = new Date()) {
  const d = new Date(date);
  const base = `${WEEKDAYS[d.getDay()]}, ${d.getDate()}. ${MONTHS[d.getMonth()]}`;
  return d.getFullYear() === now.getFullYear() ? base : base + ' ' + d.getFullYear();
}

export function fmtDateLong(date) {
  const d = new Date(date);
  return `${WEEKDAYS_LONG[d.getDay()]}, ${d.getDate()}. ${MONTHS[d.getMonth()]} ${d.getFullYear()}`;
}

// "Heute", "Morgen", "in 3 Tagen", "Gestern", "vor 5 Tagen", ab einer Woche "in 2 Wochen" / "vor 3 Wochen"
export function relDay(date, now = new Date()) {
  const n = dayDiff(date, now);
  if (n === 0) return 'Heute';
  if (n === 1) return 'Morgen';
  if (n === -1) return 'Gestern';
  const abs = Math.abs(n);
  const text = abs < 14 ? abs + ' Tagen' : abs < 60 ? Math.round(abs / 7) + ' Wochen' : Math.round(abs / 30) + ' Monaten';
  return (n > 0 ? 'in ' : 'vor ') + text;
}

// Für den Chat: "Heute", "Gestern", sonst "Mo., 9. Nov."
export function fmtChatDay(date, now = new Date()) {
  const n = dayDiff(date, now);
  if (n === 0) return 'Heute';
  if (n === -1) return 'Gestern';
  return fmtDate(date, now);
}

// Für Listen: "Heute, 21:13" / "Gestern, 21:13" / "06.11., 21:13" / "06.11.2025"
export function fmtStamp(date, now = new Date()) {
  const d = new Date(date);
  const n = dayDiff(d, now);
  if (n === 0) return 'Heute, ' + fmtTime(d);
  if (n === -1) return 'Gestern, ' + fmtTime(d);
  const day = two(d.getDate()) + '.' + two(d.getMonth() + 1) + '.';
  return d.getFullYear() === now.getFullYear() ? day + ', ' + fmtTime(d) : day + d.getFullYear();
}

export function fmtDuration(seconds) {
  const s = Math.max(0, Math.round(seconds || 0));
  return Math.floor(s / 60) + ':' + two(s % 60);
}

// Der nächste Donnerstag um die übliche Uhrzeit (heute zählt nur, wenn die Uhrzeit noch nicht vorbei ist)
export function nextThursday(now = new Date(), hour = 19, minute = 0) {
  const d = new Date(now.getFullYear(), now.getMonth(), now.getDate(), hour, minute, 0, 0);
  let add = (4 - d.getDay() + 7) % 7;
  if (add === 0 && d <= now) add = 7;
  d.setDate(d.getDate() + add);
  return d;
}

// Werte für <input type="date"> und <input type="time"> (Ortszeit) – und zurück
export function toDateInput(date) {
  const d = new Date(date);
  return `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}`;
}
export function toTimeInput(date) {
  return fmtTime(date);
}
export function fromInputs(dateStr, timeStr) {
  const [y, m, d] = String(dateStr).split('-').map(Number);
  const [hh, mm] = String(timeStr || '19:00').split(':').map(Number);
  const out = new Date(y, (m || 1) - 1, d || 1, hh || 0, mm || 0, 0, 0);
  return isNaN(out) ? null : out;
}

// ---- Treffen ----------------------------------------------------------------------------------------------------------
// Ein Treffen gilt bis 6 Stunden nach Beginn als "das nächste" – so steht es am Abend selbst noch oben.
export const EVENT_GRACE_MS = 6 * 3600 * 1000;

export function splitEvents(events, now = new Date()) {
  const cut = now.getTime() - EVENT_GRACE_MS;
  const live = events.filter(e => !e.deleted_at);
  const upcoming = live.filter(e => new Date(e.starts_at).getTime() >= cut).sort((a, b) => new Date(a.starts_at) - new Date(b.starts_at));
  const past = live.filter(e => new Date(e.starts_at).getTime() < cut).sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at));
  // Abgesagte rücken nicht an die erste Stelle, solange es ein anderes kommendes Treffen gibt
  const next = upcoming.find(e => !e.canceled) || upcoming[0] || null;
  return {next, upcoming: upcoming.filter(e => e !== next), past};
}

export function eventTitle(e) {
  return (e && e.title && e.title.trim()) || 'Stammtisch';
}

export const PLACE_KINDS = {bar: 'Bar', home: 'Zuhause', other: 'Woanders'};

export function placeLine(e) {
  if (!e || !e.place_name) return 'Ort noch offen';
  return e.place_name;
}

// Zusagen eines Treffens nach Antwort sortiert: {yes: [member…], maybe: […], no: […], open: […]}
export function rsvpGroups(rsvps, members, eventId) {
  const by = new Map();
  for (const r of rsvps) if (r.event_id === eventId && r.status !== 'none') by.set(r.member_id, r.status);
  const out = {yes: [], maybe: [], no: [], open: []};
  for (const m of members) {
    if (m.removed_at) { if (by.get(m.id) === 'yes') out.yes.push(m); continue; }   // Ehemalige nur, wenn sie zugesagt hatten
    out[by.get(m.id) || 'open'].push(m);
  }
  return out;
}

export function rsvpLine(groups) {
  const parts = [];
  if (groups.yes.length) parts.push(groups.yes.length + ' dabei');
  if (groups.maybe.length) parts.push(groups.maybe.length + ' vielleicht');
  if (groups.no.length) parts.push(groups.no.length + ' raus');
  return parts.join(' · ') || 'Noch keine Antworten';
}

// Links zu den Karten-Apps
export function mapsLinks(place) {
  const name = place.place_name || place.name || '';
  const address = place.place_address || place.address || '';
  const lat = place.place_lat ?? place.lat, lon = place.place_lon ?? place.lon;
  const q = encodeURIComponent([name, address].filter(Boolean).join(', '));
  const has = typeof lat === 'number' && typeof lon === 'number';
  return {
    apple: 'https://maps.apple.com/?q=' + q + (has ? `&ll=${lat},${lon}` : ''),
    google: 'https://www.google.com/maps/search/?api=1&query=' + (has && !q ? `${lat},${lon}` : q || `${lat},${lon}`),
    osm: has ? `https://www.openstreetmap.org/?mlat=${lat}&mlon=${lon}#map=17/${lat}/${lon}` : 'https://www.openstreetmap.org/search?query=' + q,
  };
}

// ---- Verlauf ----------------------------------------------------------------------------------------------------------
const FIELD_LABELS = {
  event: {starts_at: 'Zeit', title: 'Titel', place_name: 'Ort', place_address: 'Adresse', place_kind: 'Art des Orts', note: 'Notiz', canceled: 'Fällt aus'},
  place: {name: 'Name', address: 'Adresse', kind: 'Art', note: 'Notiz'},
  media: {event_id: 'Album'},
  group: {name: 'Name'},
  member: {name: 'Name', role: 'Rolle'},
};
const ENTITY = {
  event: ['das Treffen', 'ein Treffen'], place: ['den Ort', 'einen Ort'], media: ['ein Bild', 'ein Bild'],
  group: ['die Gruppe', 'die Gruppe'], member: ['', ''], invite: ['', ''], device: ['', ''],
};

function showValue(entity, key, v) {
  if (v == null || v === '') return '–';
  if (key === 'starts_at') return fmtDate(v) + ', ' + fmtTime(v);
  if (key === 'canceled') return v ? 'ja' : 'nein';
  if (key === 'place_kind' || (entity === 'place' && key === 'kind')) return PLACE_KINDS[v] || v;
  if (key === 'role') return v === 'admin' ? 'Admin' : 'Mitglied';
  if (key === 'event_id') return 'anderes Treffen';
  return String(v);
}

// Welche sichtbaren Felder hat ein Eintrag geändert? → [{label, from, to}]
export function changedFields(change) {
  const labels = FIELD_LABELS[change.entity] || {};
  const before = change.before || {}, after = change.after || {};
  const out = [];
  for (const key of Object.keys(labels)) {
    if (JSON.stringify(before[key] ?? null) === JSON.stringify(after[key] ?? null)) continue;
    out.push({key, label: labels[key], from: showValue(change.entity, key, before[key]), to: showValue(change.entity, key, after[key])});
  }
  return out;
}

// Worum geht es? "Stammtisch am Do., 19. Nov." / "Zum Goldenen Hahn" / Name
export function changeSubject(change) {
  const row = change.after || change.before || {};
  if (change.entity === 'event') return `${eventTitle(row)} am ${fmtDate(row.starts_at)}`;
  if (change.entity === 'place' || change.entity === 'group' || change.entity === 'member' || change.entity === 'device') return row.name || '';
  if (change.entity === 'media') return row.kind === 'video' ? 'Video' : 'Bild';
  return '';
}

// Ein Satz für die Liste: "hat das Treffen geändert", "ist beigetreten", …
export function changeVerb(change) {
  const a = change.action, e = change.entity;
  const row = change.after || {};
  if (e === 'member') {
    if (a === 'create') return 'ist beigetreten';
    if (a === 'delete') return change.member_id === change.entity_id ? 'ist ausgetreten' : `hat ${row.name || 'jemanden'} entfernt`;
    if (a === 'restore') return `hat ${row.name || 'jemanden'} wieder aufgenommen`;
    const f = changedFields(change);
    if (f.some(x => x.key === 'role')) return `hat ${row.name || 'jemanden'} ${row.role === 'admin' ? 'zum Admin gemacht' : 'die Admin-Rechte genommen'}`;
    return 'hat den Namen geändert';
  }
  if (e === 'invite') return 'hat einen neuen Einladungscode erzeugt';
  if (e === 'device') return `hat einen Gerätecode für ${row.name || 'jemanden'} erzeugt`;
  if (e === 'media') {
    const what = row.kind === 'video' ? 'ein Video' : 'ein Bild';
    if (a === 'create') return `hat ${what} hochgeladen`;
    if (a === 'delete') return `hat ${what} gelöscht`;
    if (a === 'restore') return `hat ${what} wiederhergestellt`;
    return `hat ${what} in ein anderes Album verschoben`;
  }
  if (e === 'event' && a === 'update') {
    const before = change.before || {};
    if (!before.canceled && row.canceled) return 'hat das Treffen abgesagt';
    if (before.canceled && !row.canceled) return 'lässt das Treffen doch stattfinden';
  }
  const names = ENTITY[e] || ['den Eintrag', 'einen Eintrag'];
  if (a === 'create') return `hat ${names[1]} angelegt`;
  if (a === 'delete') return `hat ${names[0]} gelöscht`;
  if (a === 'restore') return `hat ${names[0]} wiederhergestellt`;
  return `hat ${names[0]} geändert`;
}

// Lässt sich der Eintrag rückgängig machen (und darf diese Person das)?
export function canUndo(change, isAdmin) {
  if (change.undone_at) return false;
  if (change.entity === 'invite' || change.entity === 'device') return false;
  if (change.entity === 'member') {
    if (change.action === 'create') return false;
    if (change.action !== 'update') return !!isAdmin;
    return changedFields(change).some(f => f.key === 'role') ? !!isAdmin : true;
  }
  if (change.action === 'update') return changedFields(change).length > 0;
  return true;
}

// Viele Bilder nacheinander von derselben Person werden in der Liste zu einem Eintrag ("hat 12 Bilder hochgeladen").
// Gibt Gruppen zurück: [{first: change, items: [change…]}], Reihenfolge wie übergeben (neueste zuerst).
export function groupChanges(changes, windowMs = 10 * 60 * 1000) {
  const out = [];
  for (const c of changes) {
    const last = out[out.length - 1];
    const same = last && c.entity === 'media' && last.first.entity === 'media' && last.first.action === c.action
      && last.first.member_id === c.member_id && !c.undo_of && !last.first.undo_of
      && Math.abs(new Date(last.items[last.items.length - 1].at) - new Date(c.at)) <= windowMs;
    if (same) last.items.push(c);
    else out.push({first: c, items: [c]});
  }
  return out;
}

// ---- Verschiedenes ----------------------------------------------------------------------------------------------------
const GiB = 1024 ** 3;
// 870 KB, 87 MB, 3,2 GB (wie in FragDesk)
export function fmtBytes(n) {
  if (!(n >= 0)) return '–';
  if (n < 1024 * 1024) return Math.ceil(n / 1024).toLocaleString('de-DE') + ' KB';
  if (n < GiB) return Math.round(n / (1024 * 1024)).toLocaleString('de-DE') + ' MB';
  if (n % GiB === 0) return (n / GiB).toLocaleString('de-DE') + ' GB';
  return (n / GiB).toLocaleString('de-DE', {minimumFractionDigits: 1, maximumFractionDigits: 1}) + ' GB';
}

export function initials(name) {
  const parts = String(name || '?').trim().split(/\s+/).filter(Boolean);
  if (!parts.length) return '?';
  const chars = parts.length > 1 ? [parts[0], parts[parts.length - 1]] : [parts[0]];
  return chars.map(p => Array.from(p)[0].toUpperCase()).join('');
}

// Feste Farbe je Person (aus der ID), für Profilbilder ohne Foto
const AVATAR_COLORS = ['#e07a5f', '#3d9a8b', '#6a8fd8', '#c779d0', '#d9a441', '#5fa8d3', '#8fb339', '#e56b8a'];
export function colorFor(id) {
  let hash = 0;
  for (const ch of String(id || '')) hash = (hash * 31 + ch.charCodeAt(0)) >>> 0;
  return AVATAR_COLORS[hash % AVATAR_COLORS.length];
}

// Größe so verkleinern, dass die längere Seite höchstens max ist (nie vergrößern)
export function fitSize(width, height, max) {
  const scale = Math.min(1, max / Math.max(width, height, 1));
  return {width: Math.max(1, Math.round(width * scale)), height: Math.max(1, Math.round(height * scale))};
}

const EXT = {'image/jpeg': 'jpg', 'image/png': 'png', 'image/webp': 'webp', 'video/mp4': 'mp4', 'video/quicktime': 'mov', 'video/webm': 'webm',
  'audio/mp4': 'm4a', 'audio/webm': 'webm', 'audio/ogg': 'ogg', 'audio/mpeg': 'mp3', 'audio/aac': 'aac'};
export function extFor(mime, fallback = 'bin') {
  return EXT[String(mime || '').split(';')[0].trim().toLowerCase()] || fallback;
}

// Ablage im Speicher: <gruppe>/<bereich>/<jahr-monat>/<id>.<endung> – die Gruppe steht vorn, daran hängen die Zugriffsregeln
export function mediaPath(groupId, scope, id, ext, date = new Date()) {
  return `${groupId}/${scope}/${date.getFullYear()}-${two(date.getMonth() + 1)}/${id}.${ext}`;
}

export function uuid() {
  if (globalThis.crypto && crypto.randomUUID) return crypto.randomUUID();
  const b = crypto.getRandomValues(new Uint8Array(16));
  b[6] = (b[6] & 0x0f) | 0x40;
  b[8] = (b[8] & 0x3f) | 0x80;
  const hex = Array.from(b, x => x.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

// Text in Stücke teilen, Links erkennen: [{text}, {href, text}, …] (die Seite baut daraus echte Links – nie innerHTML)
export function linkify(text) {
  const out = [];
  const re = /https?:\/\/[^\s<>"']+/g;
  let last = 0, m;
  const s = String(text || '');
  while ((m = re.exec(s))) {
    let url = m[0];
    const trail = url.match(/[.,;:!?)\]]+$/);
    if (trail) url = url.slice(0, -trail[0].length);
    if (m.index > last) out.push({text: s.slice(last, m.index)});
    out.push({href: url, text: url});
    last = m.index + url.length;
    re.lastIndex = last;
  }
  if (last < s.length) out.push({text: s.slice(last)});
  return out;
}

// Welches Treffen passt zu einem Foto von "jetzt"? Das laufende oder gerade vergangene (bis 36 h her), sonst das letzte.
export function guessAlbumEvent(events, now = new Date()) {
  const live = events.filter(e => !e.deleted_at && !e.canceled);
  const t = now.getTime();
  const near = live.filter(e => { const s = new Date(e.starts_at).getTime(); return s <= t + 2 * 3600e3 && s >= t - 36 * 3600e3; })
    .sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at))[0];
  if (near) return near;
  return live.filter(e => new Date(e.starts_at).getTime() < t).sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at))[0] || null;
}

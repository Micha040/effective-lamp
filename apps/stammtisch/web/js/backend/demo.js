// STAMMTISCH – Demo-Anbindung: erfundene Daten im Arbeitsspeicher, nichts verlässt das Gerät, nach dem Neuladen ist alles
// wieder wie vorher. Aufruf: Adresse mit ?demo (z. B. index.html?demo). Dient zum Ausprobieren, für die Tests und für
// Bilder der Oberfläche – und zeigt, welche Funktionen eine Anbindung anbieten muss (dieselben wie supabase.js).
import {uuid, nextThursday, changedFields} from '../util.js';

const HOUR = 3600e3, DAY = 24 * HOUR;

// Ein „Foto“ als gezeichnetes Bild (Farbverlauf mit ein paar Kreisen), damit die Demo ohne echte Dateien auskommt
function fakePhoto(seed, w = 400, h = 400) {
  let x = 0;
  for (const ch of String(seed)) x = (x * 31 + ch.charCodeAt(0)) >>> 0;
  const rnd = () => { x = (x * 1664525 + 1013904223) >>> 0; return x / 4294967296; };
  const hue = Math.floor(rnd() * 360);
  let dots = '';
  for (let i = 0; i < 7; i++) {
    dots += `<circle cx="${Math.round(rnd() * w)}" cy="${Math.round(rnd() * h)}" r="${Math.round(20 + rnd() * 70)}" fill="hsl(${(hue + 40 + rnd() * 80) % 360} 70% ${40 + rnd() * 30}%)" opacity="${(0.25 + rnd() * 0.4).toFixed(2)}"/>`;
  }
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">`
    + `<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="hsl(${hue} 55% 32%)"/><stop offset="1" stop-color="hsl(${(hue + 50) % 360} 60% 18%)"/></linearGradient></defs>`
    + `<rect width="${w}" height="${h}" fill="url(#g)"/>${dots}</svg>`;
  return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
}

function seed(now) {
  const gid = 'd0000000-0000-4000-8000-000000000001';
  const gid2 = 'd0000000-0000-4000-8000-000000000002';
  const names = ['Micha', 'Tom', 'Jonas', 'Lena', 'Kai', 'Sophie', 'Ben', 'Nils', 'Paula'];
  const members = names.map((name, i) => ({
    id: `d0000000-0000-4000-8000-0000000001${String(i).padStart(2, '0')}`, group_id: gid, name, avatar_path: null,
    role: i === 0 ? 'admin' : 'member', joined_at: new Date(now - (200 - i) * DAY).toISOString(), removed_at: null,
  }));
  members.push({id: 'd0000000-0000-4000-8000-000000000201', group_id: gid2, name: 'Micha', avatar_path: null, role: 'admin',
    joined_at: new Date(now - 30 * DAY).toISOString(), removed_at: null});
  const m = i => members[i].id;

  const places = [
    {name: 'Zum Goldenen Hahn', address: 'Schulterblatt 41, 20357 Hamburg', lat: 53.5627, lon: 9.9612, kind: 'bar'},
    {name: 'Kleine Freiheit', address: 'Große Freiheit 7, 22767 Hamburg', lat: 53.5512, lon: 9.9577, kind: 'bar'},
    {name: 'Bei Tom', address: 'Eppendorfer Weg 112, 20259 Hamburg', lat: 53.5778, lon: 9.9581, kind: 'home'},
    {name: 'Kegelbahn Altona', address: 'Max-Brauer-Allee 60, 22765 Hamburg', lat: 53.5539, lon: 9.9386, kind: 'other'},
  ].map((p, i) => ({id: `d0000000-0000-4000-8000-0000000003${String(i).padStart(2, '0')}`, group_id: gid, note: null, created_by: m(0),
    created_at: new Date(now - 100 * DAY).toISOString(), updated_at: new Date(now - 100 * DAY).toISOString(), deleted_at: null, ...p}));

  const thursday = nextThursday(new Date(now), 19, 0).getTime();
  const ev = (i, start, place, extra = {}) => ({
    id: `d0000000-0000-4000-8000-0000000004${String(i).padStart(2, '0')}`, group_id: gid, starts_at: new Date(start).toISOString(), title: null,
    place_id: place ? place.id : null, place_name: place ? place.name : null, place_address: place ? place.address : null,
    place_lat: place ? place.lat : null, place_lon: place ? place.lon : null, place_kind: place ? place.kind : null,
    note: null, canceled: false, created_by: m(i % 3), created_at: new Date(start - 10 * DAY).toISOString(),
    updated_at: new Date(start - 10 * DAY).toISOString(), deleted_at: null, ...extra,
  });
  const events = [
    ev(0, thursday, places[0], {note: 'Tisch ist ab 19 Uhr auf „Stammtisch“ reserviert.'}),
    ev(1, thursday + 7 * DAY, places[2], {title: 'Spieleabend', note: 'Jeder bringt was zu knabbern mit.'}),
    ev(2, thursday + 14 * DAY, null),
    ev(3, thursday + 21 * DAY, places[3], {title: 'Weihnachts-Stammtisch', starts_at: new Date(thursday + 21 * DAY - HOUR).toISOString()}),
    ev(4, thursday - 7 * DAY, places[1]),
    ev(5, thursday - 14 * DAY, places[0]),
    ev(6, thursday - 21 * DAY, places[0], {canceled: true}),
    ev(7, thursday - 28 * DAY, places[2], {title: 'Grillen'}),
  ];

  const rsvps = [];
  const answer = (e, list) => list.forEach(([i, status]) => rsvps.push({event_id: events[e].id, member_id: m(i), group_id: gid, status}));
  answer(0, [[1, 'yes'], [2, 'yes'], [3, 'yes'], [5, 'yes'], [6, 'maybe'], [7, 'no']]);
  answer(1, [[0, 'yes'], [1, 'yes'], [4, 'maybe']]);
  answer(4, [[0, 'yes'], [1, 'yes'], [2, 'yes'], [3, 'yes'], [4, 'yes'], [8, 'yes']]);
  answer(5, [[0, 'yes'], [2, 'yes'], [5, 'yes'], [6, 'yes']]);
  answer(7, [[0, 'yes'], [1, 'yes'], [2, 'yes'], [3, 'yes'], [5, 'yes'], [7, 'yes'], [8, 'yes']]);

  const media = [];
  const photo = (e, i, by, kind = 'image') => media.push({
    id: uuid(), group_id: gid, event_id: e == null ? null : events[e].id, member_id: m(by), scope: 'album', kind,
    path: `${gid}/album/demo/${e}-${i}.jpg`, thumb_path: `${gid}/album/demo/${e}-${i}_t.jpg`, mime: kind === 'video' ? 'video/mp4' : 'image/jpeg',
    width: 1600, height: 1200, duration: kind === 'video' ? 14 : null, size_bytes: kind === 'video' ? 9_400_000 : 340_000,
    taken_at: null, created_at: new Date(new Date(e == null ? now - 3 * DAY : events[e].starts_at).getTime() + (3 + i * 0.1) * HOUR).toISOString(), deleted_at: null,
  });
  for (let i = 0; i < 11; i++) photo(4, i, i % 4, i === 3 ? 'video' : 'image');
  for (let i = 0; i < 5; i++) photo(5, i, (i + 2) % 5);
  for (let i = 0; i < 8; i++) photo(7, i, i % 3);
  for (let i = 0; i < 2; i++) photo(null, i, 1);

  const messages = [];
  const say = (ago, by, body, extra = {}) => {
    const msg = {id: uuid(), group_id: gid, member_id: m(by), body, media_id: null, reply_to: null,
      created_at: new Date(now - ago).toISOString(), edited_at: null, deleted_at: null, media: null, reactions: [], ...extra};
    messages.push(msg);
    return msg;
  };
  say(2 * DAY + 5 * HOUR, 2, 'War wieder top letzte Woche 🍻');
  const pic = {id: uuid(), group_id: gid, event_id: null, member_id: m(3), scope: 'chat', kind: 'image', path: `${gid}/chat/demo/1.jpg`,
    thumb_path: `${gid}/chat/demo/1_t.jpg`, mime: 'image/jpeg', width: 1200, height: 900, duration: null, size_bytes: 280_000,
    taken_at: null, created_at: new Date(now - 2 * DAY).toISOString(), deleted_at: null};
  say(2 * DAY + 4.9 * HOUR, 3, 'Beweisfoto', {media_id: pic.id, media: pic, reactions: [{member_id: m(0), emoji: '😂', active: true}, {member_id: m(2), emoji: '😂', active: true}]});
  const q = say(26 * HOUR, 0, 'Donnerstag wieder im Hahn? Ich reserviere dann.');
  say(25.6 * HOUR, 1, 'Bin dabei', {reply_to: q.id, reactions: [{member_id: m(0), emoji: '👍', active: true}]});
  say(25.5 * HOUR, 5, 'Ich auch, komme aber erst gegen acht.');
  const voice = {id: uuid(), group_id: gid, event_id: null, member_id: m(4), scope: 'chat', kind: 'audio', path: `${gid}/chat/demo/v.m4a`,
    thumb_path: null, mime: 'audio/mp4', width: null, height: null, duration: 12, size_bytes: 96_000, taken_at: null,
    created_at: new Date(now - 5 * HOUR).toISOString(), deleted_at: null};
  say(5 * HOUR, 4, '', {media_id: voice.id, media: voice});
  say(4.8 * HOUR, 6, 'Vielleicht, muss noch schauen wie lange ich arbeite.');
  say(3 * HOUR, 0, 'Alles klar. Tisch steht ab 19 Uhr, Karte ist in der App: https://example.org/karte', {reactions: [{member_id: m(1), emoji: '🍻', active: true}, {member_id: m(3), emoji: '🍻', active: true}, {member_id: m(5), emoji: '❤️', active: true}]});
  say(40 * 60e3, 7, 'Bin diesmal raus, bin im Urlaub 🏖️');

  const changelog = [];
  let cid = 0;
  const log = (ago, by, entity, action, before, after) => changelog.push({id: ++cid, group_id: gid, member_id: m(by), at: new Date(now - ago).toISOString(),
    entity, entity_id: (after || before).id, action, before, after, undo_of: null, undone_at: null});
  log(10 * DAY, 0, 'event', 'create', null, events[0]);
  log(9 * DAY, 1, 'event', 'create', null, events[1]);
  log(6 * DAY, 2, 'event', 'update', {...events[3], title: null, starts_at: new Date(thursday + 21 * DAY).toISOString()}, events[3]);
  for (let i = 0; i < 6; i++) log(5 * DAY + i * 20e3, 3, 'media', 'create', null, media[i]);
  log(3 * DAY, 1, 'place', 'create', null, places[3]);
  log(2 * DAY, 0, 'event', 'update', {...events[0], note: null}, events[0]);
  log(30 * HOUR, 8, 'member', 'update', {...members[8], name: 'Paula M.'}, members[8]);
  changelog.reverse();

  const groups = [{id: gid, name: 'STAMMTISCH'}, {id: gid2, name: 'Kegelclub'}];
  return {groups, members, places, events, rsvps, media, messages, changelog, cid, me: {[gid]: m(0), [gid2]: members[members.length - 1].id}};
}

export async function create() {
  const db = seed(Date.now());
  const listeners = new Map();   // Gruppe → Set von Empfängern
  const emit = (gid, table, row) => { for (const fn of listeners.get(gid) || []) fn({table, row}); };
  const clone = x => JSON.parse(JSON.stringify(x));
  const myId = gid => db.me[gid];
  const tables = {event: 'events', place: 'places', media: 'media', member: 'members', group: 'groups'};
  const delKey = entity => (entity === 'member' ? 'removed_at' : 'deleted_at');

  function log(gid, entity, action, before, after, undoOf = null) {
    const c = {id: ++db.cid, group_id: gid, member_id: myId(gid), at: new Date().toISOString(), entity, entity_id: (after || before).id, action,
      before: before ? clone(before) : null, after: after ? clone(after) : null, undo_of: undoOf, undone_at: null};
    db.changelog.unshift(c);
    return c;
  }
  // Zeile ändern und im Verlauf festhalten (wie die Trigger in der echten Datenbank)
  function patchRow(entity, row, patch, undoOf) {
    const before = clone(row);
    Object.assign(row, patch);
    if ('updated_at' in row) row.updated_at = new Date().toISOString();
    const key = delKey(entity);
    const action = !before[key] && row[key] ? 'delete' : before[key] && !row[key] ? 'restore' : 'update';
    const gid = entity === 'group' ? row.id : row.group_id;
    if (entity !== 'media' || row.scope === 'album') log(gid, entity, action, before, row, undoOf);
    emit(gid, tables[entity], clone(row));
  }
  const find = (table, id) => {
    const row = db[table].find(r => r.id === id);
    if (!row) throw new Error('Kein Zugriff.');
    return row;
  };
  const code = n => Array.from({length: n}, () => 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'[Math.floor(Math.random() * 32)]).join('');
  const invites = new Map();

  return {
    kind: 'demo',
    async init() { return true; },
    async signUp() {},
    async signOut() {},

    async myGroups() {
      return db.groups.map(g => {
        const me = db.members.find(x => x.id === myId(g.id));
        return {id: g.id, name: g.name, me: {id: me.id, name: me.name, role: me.role, avatar_path: me.avatar_path}};
      });
    },
    async joinGroup() { return db.groups[0].id; },
    async linkDevice() { return db.groups[0].id; },
    async createGroup(name, myName) {
      const g = {id: uuid(), name};
      const me = {id: uuid(), group_id: g.id, name: myName || 'Micha', avatar_path: null, role: 'admin', joined_at: new Date().toISOString(), removed_at: null};
      db.groups.push(g);
      db.members.push(me);
      db.me[g.id] = me.id;
      return g.id;
    },
    async renameGroup(gid, name) { patchRow('group', find('groups', gid), {name}); },
    async members(gid) { return clone(db.members.filter(x => x.group_id === gid)); },
    async updateMe(memberId, patch) { patchRow('member', find('members', memberId), patch); },
    async setRole(memberId, role) { patchRow('member', find('members', memberId), {role}); },
    async removeMember(memberId) { patchRow('member', find('members', memberId), {removed_at: new Date().toISOString()}); },
    async restoreMember(memberId) { patchRow('member', find('members', memberId), {removed_at: null}); },
    async invite(gid) { if (!invites.has(gid)) invites.set(gid, code(10)); return invites.get(gid); },
    async rotateInvite(gid) { invites.set(gid, code(10)); return invites.get(gid); },
    async deviceCode() { return code(8); },

    async places(gid) { return clone(db.places.filter(p => p.group_id === gid && !p.deleted_at).sort((a, b) => a.name.localeCompare(b.name, 'de'))); },
    async savePlace(gid, place) {
      if (place.id) { const row = find('places', place.id); patchRow('place', row, place); return clone(row); }
      const row = {id: uuid(), group_id: gid, name: '', address: null, lat: null, lon: null, kind: 'bar', note: null, created_by: myId(gid),
        created_at: new Date().toISOString(), updated_at: new Date().toISOString(), deleted_at: null, ...place};
      db.places.push(row);
      log(gid, 'place', 'create', null, row);
      return clone(row);
    },
    async patchPlace(id, patch) { patchRow('place', find('places', id), patch); },

    async events(gid, from) {
      const t = new Date(from).getTime();
      return clone(db.events.filter(e => e.group_id === gid && !e.deleted_at && new Date(e.starts_at).getTime() >= t)
        .sort((a, b) => new Date(a.starts_at) - new Date(b.starts_at)));
    },
    async pastEvents(gid, before, limit = 30) {
      const t = new Date(before).getTime();
      return clone(db.events.filter(e => e.group_id === gid && !e.deleted_at && new Date(e.starts_at).getTime() < t)
        .sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at)).slice(0, limit));
    },
    async event(id) { return clone(db.events.find(e => e.id === id) || null); },
    async saveEvent(gid, ev) {
      if (ev.id) { const row = find('events', ev.id); patchRow('event', row, ev); return clone(row); }
      const row = {id: uuid(), group_id: gid, title: null, place_id: null, place_name: null, place_address: null, place_lat: null, place_lon: null,
        place_kind: null, note: null, canceled: false, created_by: myId(gid), created_at: new Date().toISOString(),
        updated_at: new Date().toISOString(), deleted_at: null, ...ev};
      db.events.push(row);
      log(gid, 'event', 'create', null, row);
      emit(gid, 'events', clone(row));
      return clone(row);
    },
    async patchEvent(id, patch) { patchRow('event', find('events', id), patch); },
    async rsvps(gid, eventIds) { return clone(db.rsvps.filter(r => r.group_id === gid && eventIds.includes(r.event_id))); },
    async setRsvp(eventId, status) {
      const e = find('events', eventId);
      let r = db.rsvps.find(x => x.event_id === eventId && x.member_id === myId(e.group_id));
      if (r) r.status = status;
      else db.rsvps.push(r = {event_id: eventId, member_id: myId(e.group_id), group_id: e.group_id, status});
      emit(e.group_id, 'rsvps', clone(r));
    },

    async messages(gid, {before, limit = 50} = {}) {
      let list = db.messages.filter(x => x.group_id === gid);
      if (before) list = list.filter(x => x.created_at < before);
      return clone(list.slice(-limit));
    },
    async message(id) { return clone(db.messages.find(x => x.id === id) || null); },
    async sendMessage(gid, msg) {
      const row = {id: uuid(), group_id: gid, member_id: myId(gid), body: '', media_id: null, reply_to: null, created_at: new Date().toISOString(),
        edited_at: null, deleted_at: null, reactions: [], ...msg};
      row.media = row.media_id ? clone(db.media.find(x => x.id === row.media_id) || null) : null;
      db.messages.push(row);
      emit(gid, 'messages', clone(row));
      return clone(row);
    },
    async editMessage(id, body) { const row = find('messages', id); row.body = body; row.edited_at = new Date().toISOString(); emit(row.group_id, 'messages', clone(row)); },
    async deleteMessage(id) { const row = find('messages', id); row.deleted_at = new Date().toISOString(); emit(row.group_id, 'messages', clone(row)); },
    async react(messageId, emoji, on) {
      const row = find('messages', messageId);
      const me = myId(row.group_id);
      let r = row.reactions.find(x => x.member_id === me && x.emoji === emoji);
      if (r) r.active = on;
      else row.reactions.push(r = {member_id: me, emoji, active: on});
      emit(row.group_id, 'reactions', {message_id: messageId, group_id: row.group_id, ...clone(r)});
    },

    async media(gid, {eventId, limit = 1000} = {}) {
      return clone(db.media.filter(x => x.group_id === gid && x.scope === 'album' && !x.deleted_at
        && (eventId === undefined || (eventId === null ? x.event_id == null : x.event_id === eventId)))
        .sort((a, b) => (a.created_at < b.created_at ? 1 : -1)).slice(0, limit));
    },
    async addMedia(gid, row) {
      const m = {id: uuid(), group_id: gid, event_id: null, member_id: myId(gid), scope: 'album', thumb_path: null, mime: null, width: null,
        height: null, duration: null, size_bytes: 0, taken_at: null, created_at: new Date().toISOString(), deleted_at: null, ...row};
      db.media.push(m);
      if (m.scope === 'album') log(gid, 'media', 'create', null, m);
      emit(gid, 'media', clone(m));
      return clone(m);
    },
    async patchMedia(id, patch) { patchRow('media', find('media', id), patch); },
    async upload(path, blob, onProgress) {
      for (let p = 0.25; p <= 1; p += 0.25) { await new Promise(r => setTimeout(r, 60)); if (onProgress) onProgress(p); }
      if (typeof URL !== 'undefined' && URL.createObjectURL && blob) uploaded.set(path, URL.createObjectURL(blob));
    },
    async removeFiles() {},
    async url(path) { return path ? uploaded.get(path) || fakePhoto(path, /_t\./.test(path) ? 400 : 1200, /_t\./.test(path) ? 400 : 900) : null; },
    async usage(gid) {
      const sum = list => list.reduce((n, x) => n + (x.size_bytes || 0), 0);
      return {group_bytes: sum(db.media.filter(x => x.group_id === gid)), total_bytes: sum(db.media) + 212 * 1024 * 1024};
    },

    async changelog(gid, {before, limit = 60} = {}) {
      return clone(db.changelog.filter(c => c.group_id === gid && (!before || c.id < before)).slice(0, limit));
    },
    async undo(id) {
      const c = db.changelog.find(x => x.id === id);
      if (!c || c.undone_at) throw new Error('Das wurde schon rückgängig gemacht.');
      const table = tables[c.entity];
      if (!table) throw new Error('Das lässt sich nicht rückgängig machen.');
      const row = find(table, c.entity_id);
      const key = delKey(c.entity);
      if (c.action === 'create' || c.action === 'restore') patchRow(c.entity, row, {[key]: new Date().toISOString()}, c.id);
      else if (c.action === 'delete') patchRow(c.entity, row, {[key]: null}, c.id);
      else {
        const patch = {};
        for (const f of changedFields(c)) patch[f.key] = c.before[f.key];
        if (c.entity === 'event' && 'place_name' in patch) for (const k of ['place_id', 'place_lat', 'place_lon']) patch[k] = c.before[k];
        patchRow(c.entity, row, patch, c.id);
      }
      c.undone_at = new Date().toISOString();
    },
    async trash(gid) {
      const gone = list => clone(list.filter(x => x.group_id === gid && x.deleted_at).sort((a, b) => (a.deleted_at < b.deleted_at ? 1 : -1)));
      return {events: gone(db.events), places: gone(db.places), media: gone(db.media.filter(x => x.scope === 'album'))};
    },
    async purge() { return 0; },

    subscribe(gid, handler) {
      if (!listeners.has(gid)) listeners.set(gid, new Set());
      listeners.get(gid).add(handler);
      return () => listeners.get(gid).delete(handler);
    },
  };
}

const uploaded = new Map();   // in der Demo hochgeladene Dateien (nur bis zum Neuladen)

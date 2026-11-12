// STAMMTISCH – Anbindung an Supabase (Anmeldung, Datenbank, Dateispeicher, Live-Aktualisierung).
//
// Wer was sehen und ändern darf, entscheidet die Datenbank (supabase/migrations) – nicht dieser Code. Hier werden nur
// Anfragen gestellt und Fehlermeldungen verständlich gemacht.

const CRED_KEY = 'stammtisch.cred';          // erfundene Adresse + Zufallspasswort dieses Geräts (siehe Edge Function „konto“)
const MSG_SELECT = '*, media(*), reactions(member_id, emoji, active)';
const SIGN_SECONDS = 24 * 3600;              // so lange gilt eine Bildadresse

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error('Die App konnte nicht vollständig geladen werden. Bitte neu laden.'));
    document.head.append(s);
  });
}

function isNetworkError(e) {
  const msg = (e && (e.message || e.msg)) || String(e || '');
  return /failed to fetch|load failed|networkerror|fetch failed|network request failed/i.test(msg) || (e && e.name === 'AuthRetryableFetchError');
}

// Fehler von Supabase in etwas Lesbares übersetzen. Meldungen aus unseren eigenen Funktionen sind schon deutsch.
function toError(e) {
  const msg = (e && (e.message || e.error_description || e.msg)) || String(e);
  let text = msg;
  if (isNetworkError(e)) text = 'Keine Verbindung. Bitte nochmal versuchen, sobald du wieder Netz hast.';
  else if (/row-level security|permission denied/i.test(msg)) text = 'Kein Zugriff.';
  else if (/violates check constraint/i.test(msg)) text = 'Die Eingabe ist zu lang oder passt nicht.';
  else if (/exceeded the maximum allowed size|payload too large/i.test(msg)) text = 'Die Datei ist zu groß (höchstens 50 MB).';
  else if (/mime type .* is not supported/i.test(msg)) text = 'Dieser Dateityp geht nicht (nur Bilder, Videos und Ton).';
  else if (/invalid login credentials/i.test(msg)) text = 'Die Anmeldung dieses Geräts gilt nicht mehr.';
  const err = new Error(text);
  err.offline = isNetworkError(e);
  err.cause = e;
  return err;
}

export async function create(config) {
  if (!window.supabase) await loadScript('vendor/supabase.js');
  const sb = window.supabase.createClient(config.supabaseUrl, config.supabaseKey, {
    auth: {storageKey: 'stammtisch.auth', persistSession: true, autoRefreshToken: true, detectSessionInUrl: false},
  });
  const bucket = () => sb.storage.from('media');

  // Anfrage ausführen: Daten zurückgeben oder verständlichen Fehler werfen
  async function run(query) {
    let res;
    try { res = await query; } catch (e) { throw toError(e); }
    if (res.error) throw toError(res.error);
    return res.data;
  }
  const rpc = (name, args) => run(sb.rpc(name, args || {}));

  const loadCred = () => { try { return JSON.parse(localStorage.getItem(CRED_KEY) || 'null'); } catch (e) { return null; } };
  const saveCred = c => { try { localStorage.setItem(CRED_KEY, JSON.stringify(c)); } catch (e) { /* privat: egal */ } };
  const clearCred = () => { try { localStorage.removeItem(CRED_KEY); } catch (e) { /* egal */ } };

  // ---- Bildadressen: privat, deshalb jeweils signiert; viele Anfragen in einem Rutsch --------------------------------
  const urlCache = new Map();                // Pfad → {url, until}
  let pending = new Map();                   // Pfad → [{resolve, reject}]
  let flushTimer = 0;
  async function flushUrls() {
    const batch = pending;
    pending = new Map();
    flushTimer = 0;
    const paths = [...batch.keys()];
    try {
      const data = await run(bucket().createSignedUrls(paths, SIGN_SECONDS));
      const until = Date.now() + (SIGN_SECONDS - 3600) * 1000;
      for (const item of data || []) {
        if (item.signedUrl) urlCache.set(item.path, {url: item.signedUrl, until});
      }
      for (const [path, waiters] of batch) {
        const hit = urlCache.get(path);
        for (const w of waiters) w.resolve(hit ? hit.url : null);
      }
    } catch (e) {
      for (const waiters of batch.values()) for (const w of waiters) w.reject(e);
    }
  }
  function url(path) {
    if (!path) return Promise.resolve(null);
    const hit = urlCache.get(path);
    if (hit && hit.until > Date.now()) return Promise.resolve(hit.url);
    return new Promise((resolve, reject) => {
      if (!pending.has(path)) pending.set(path, []);
      pending.get(path).push({resolve, reject});
      if (!flushTimer) flushTimer = setTimeout(flushUrls, 15);
    });
  }

  return {
    kind: 'supabase',

    // ---- Anmeldung --------------------------------------------------------------------------------------------------
    // true = dieses Gerät ist angemeldet (oder war es und ist gerade nur ohne Netz)
    async init() {
      let session = null;
      try { session = (await sb.auth.getSession()).data.session; } catch (e) { /* unten */ }
      if (session) return true;
      const cred = loadCred();
      if (!cred) return false;
      const res = await sb.auth.signInWithPassword(cred).catch(error => ({error}));
      if (!res.error) return true;
      if (isNetworkError(res.error)) return true;
      clearCred();
      return false;
    },

    // Neues Gerät: Die Konto-Funktion legt eine Anmeldung an, wenn der Code gilt – dann sofort anmelden
    async signUp(code) {
      let res, body;
      try {
        res = await fetch(config.supabaseUrl + '/functions/v1/konto', {
          method: 'POST',
          headers: {'Content-Type': 'application/json', apikey: config.supabaseKey},
          body: JSON.stringify({code}),
        });
        body = await res.json();
      } catch (e) {
        throw toError(e);
      }
      if (!res.ok || !body.email) throw new Error((body && body.error) || 'Die Anmeldung hat nicht geklappt.');
      saveCred({email: body.email, password: body.password});
      await run(sb.auth.signInWithPassword({email: body.email, password: body.password}));
    },

    async signOut() {
      clearCred();
      await sb.auth.signOut().catch(() => {});
    },

    // ---- Gruppen und Mitglieder ----------------------------------------------------------------------------------------
    myGroups: () => rpc('my_groups'),
    joinGroup: (code, name) => rpc('join_group', {p_code: code, p_name: name}),
    linkDevice: code => rpc('link_device', {p_code: code}),
    createGroup: (name, myName) => rpc('create_group', {p_name: name, p_my_name: myName || ''}),
    renameGroup: (gid, name) => run(sb.from('groups').update({name}).eq('id', gid)),
    members: gid => run(sb.from('members').select('*').eq('group_id', gid).order('joined_at')),
    updateMe: (memberId, patch) => run(sb.from('members').update(patch).eq('id', memberId)),
    setRole: (memberId, role) => rpc('set_member_role', {p_member: memberId, p_role: role}),
    removeMember: memberId => rpc('remove_member', {p_member: memberId}),
    restoreMember: memberId => rpc('restore_member', {p_member: memberId}),
    invite: gid => rpc('get_invite', {p_group: gid}),
    rotateInvite: gid => rpc('rotate_invite', {p_group: gid}),
    deviceCode: memberId => rpc('create_device_code', {p_member: memberId}),

    // ---- Orte und Treffen ----------------------------------------------------------------------------------------------
    places: gid => run(sb.from('places').select('*').eq('group_id', gid).is('deleted_at', null).order('name')),
    async savePlace(gid, place) {
      const {id, ...fields} = place;
      if (id) return run(sb.from('places').update(fields).eq('id', id).select().single());
      return run(sb.from('places').insert({group_id: gid, ...fields}).select().single());
    },
    patchPlace: (id, patch) => run(sb.from('places').update(patch).eq('id', id)),

    events: (gid, from) => run(sb.from('events').select('*').eq('group_id', gid).is('deleted_at', null)
      .gte('starts_at', new Date(from).toISOString()).order('starts_at')),
    pastEvents(gid, before, limit = 30) {
      return run(sb.from('events').select('*').eq('group_id', gid).is('deleted_at', null)
        .lt('starts_at', new Date(before).toISOString()).order('starts_at', {ascending: false}).limit(limit));
    },
    event: id => run(sb.from('events').select('*').eq('id', id).maybeSingle()),
    async saveEvent(gid, ev) {
      const {id, ...fields} = ev;
      if (id) return run(sb.from('events').update(fields).eq('id', id).select().single());
      return run(sb.from('events').insert({group_id: gid, ...fields}).select().single());
    },
    patchEvent: (id, patch) => run(sb.from('events').update(patch).eq('id', id)),
    rsvps(gid, eventIds) {
      if (!eventIds.length) return Promise.resolve([]);
      return run(sb.from('rsvps').select('event_id, member_id, status').eq('group_id', gid).in('event_id', eventIds));
    },
    setRsvp: (eventId, status) => rpc('set_rsvp', {p_event: eventId, p_status: status}),

    // ---- Chat ----------------------------------------------------------------------------------------------------------
    // Die neuesten Nachrichten (oder die vor einem Zeitpunkt), älteste zuerst
    async messages(gid, {before, limit = 50} = {}) {
      let q = sb.from('messages').select(MSG_SELECT).eq('group_id', gid).order('created_at', {ascending: false}).limit(limit);
      if (before) q = q.lt('created_at', before);
      return (await run(q)).reverse();
    },
    message: id => run(sb.from('messages').select(MSG_SELECT).eq('id', id).maybeSingle()),
    sendMessage: (gid, msg) => run(sb.from('messages').insert({group_id: gid, ...msg}).select(MSG_SELECT).single()),
    editMessage: (id, body) => run(sb.from('messages').update({body}).eq('id', id)),
    deleteMessage: id => run(sb.from('messages').update({deleted_at: new Date().toISOString()}).eq('id', id)),
    react: (messageId, emoji, on) => rpc('react', {p_message: messageId, p_emoji: emoji, p_on: on}),

    // ---- Bilder, Videos, Sprachnachrichten -----------------------------------------------------------------------------
    // eventId: undefined = alle Alben, null = ohne Treffen, sonst das Album dieses Treffens
    media(gid, {eventId, limit = 1000} = {}) {
      let q = sb.from('media').select('*').eq('group_id', gid).eq('scope', 'album').is('deleted_at', null)
        .order('created_at', {ascending: false}).limit(limit);
      if (eventId === null) q = q.is('event_id', null);
      else if (eventId) q = q.eq('event_id', eventId);
      return run(q);
    },
    addMedia: (gid, row) => run(sb.from('media').insert({group_id: gid, ...row}).select().single()),
    patchMedia: (id, patch) => run(sb.from('media').update(patch).eq('id', id)),

    // Datei hochladen (mit Fortschritt 0…1). Der Pfad beginnt mit der Gruppe – daran hängen die Zugriffsregeln.
    async upload(path, blob, onProgress) {
      const session = (await sb.auth.getSession()).data.session;
      if (!session) throw new Error('Nicht angemeldet.');
      await new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', `${config.supabaseUrl}/storage/v1/object/media/${path.split('/').map(encodeURIComponent).join('/')}`);
        xhr.setRequestHeader('Authorization', 'Bearer ' + session.access_token);
        xhr.setRequestHeader('apikey', config.supabaseKey);
        xhr.setRequestHeader('Content-Type', blob.type || 'application/octet-stream');
        xhr.setRequestHeader('Cache-Control', 'max-age=31536000');
        xhr.setRequestHeader('x-upsert', 'false');
        xhr.upload.onprogress = ev => { if (onProgress && ev.lengthComputable) onProgress(ev.loaded / ev.total); };
        xhr.onload = () => {
          if (xhr.status >= 200 && xhr.status < 300) return resolve();
          let detail = null;
          try { detail = JSON.parse(xhr.responseText); } catch (e) { /* kein JSON */ }
          reject(toError(detail || {message: 'Hochladen fehlgeschlagen (' + xhr.status + ').'}));
        };
        xhr.onerror = () => reject(toError({message: 'Failed to fetch'}));
        xhr.send(blob);
      });
    },
    removeFiles: paths => (paths.length ? run(bucket().remove(paths)) : Promise.resolve()),
    url,
    usage: gid => rpc('storage_usage', {p_group: gid}),

    // ---- Verlauf und Papierkorb ----------------------------------------------------------------------------------------
    changelog(gid, {before, limit = 60} = {}) {
      let q = sb.from('changelog').select('*').eq('group_id', gid).order('id', {ascending: false}).limit(limit);
      if (before) q = q.lt('id', before);
      return run(q);
    },
    undo: id => rpc('undo_change', {p_id: id}),
    async trash(gid) {
      const [events, places, media] = await Promise.all([
        run(sb.from('events').select('*').eq('group_id', gid).not('deleted_at', 'is', null).order('deleted_at', {ascending: false})),
        run(sb.from('places').select('*').eq('group_id', gid).not('deleted_at', 'is', null).order('deleted_at', {ascending: false})),
        run(sb.from('media').select('*').eq('group_id', gid).eq('scope', 'album').not('deleted_at', 'is', null).order('deleted_at', {ascending: false})),
      ]);
      return {events, places, media};
    },
    // Was länger als 30 Tage im Papierkorb liegt, endgültig entfernen: erst die Dateien, dann die Einträge
    async purge(gid) {
      const old = await rpc('trash_expired', {p_group: gid});
      const paths = [];
      for (const m of old) { paths.push(m.path); if (m.thumb_path) paths.push(m.thumb_path); }
      if (paths.length) await run(bucket().remove(paths));
      return rpc('purge_trash', {p_group: gid, p_media: old.map(m => m.id)});
    },

    // ---- Live: Änderungen der Gruppe sofort mitbekommen ---------------------------------------------------------------
    // handler({table, row}) für neue und geänderte Zeilen; gibt eine Funktion zum Abmelden zurück
    subscribe(gid, handler) {
      const channel = sb.channel('gruppe-' + gid);
      for (const table of ['members', 'places', 'events', 'rsvps', 'media', 'messages', 'reactions']) {
        channel.on('postgres_changes', {event: '*', schema: 'public', table, filter: 'group_id=eq.' + gid},
          payload => handler({table, row: payload.new}));
      }
      channel.on('postgres_changes', {event: 'UPDATE', schema: 'public', table: 'groups', filter: 'id=eq.' + gid},
        payload => handler({table: 'groups', row: payload.new}));
      channel.subscribe();
      return () => { sb.removeChannel(channel); };
    },
  };
}

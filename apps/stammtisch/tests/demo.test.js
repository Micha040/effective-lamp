// Tests für die Demo-Anbindung (web/js/backend/demo.js): Sie muss sich so verhalten, wie die Seiten es von jeder
// Anbindung erwarten – das ist zugleich die Probe für die Liste der Funktionen, die ein eigener Server später braucht.
import test from 'node:test';
import assert from 'node:assert/strict';
import {create} from '../web/js/backend/demo.js';
import {readFileSync} from 'node:fs';

const day = 24 * 3600e3;

test('Demo: Gruppen, Mitglieder, Treffen', async () => {
  const be = await create();
  assert.equal(await be.init(), true);
  const groups = await be.myGroups();
  assert.equal(groups[0].name, 'STAMMTISCH');
  assert.equal(groups[0].me.role, 'admin');
  const gid = groups[0].id;
  assert.equal((await be.members(gid)).length, 9);

  const upcoming = await be.events(gid, new Date());
  assert.ok(upcoming.length >= 3);
  assert.deepEqual(upcoming.map(e => e.starts_at), [...upcoming.map(e => e.starts_at)].sort());
  const past = await be.pastEvents(gid, new Date(Date.now() - 6 * 3600e3), 2);
  assert.equal(past.length, 2);
  assert.ok(past[0].starts_at > past[1].starts_at);
});

test('Demo: Treffen anlegen, ändern, löschen, rückgängig – alles steht im Verlauf', async () => {
  const be = await create();
  const gid = (await be.myGroups())[0].id;
  const before = (await be.changelog(gid)).length;
  const ev = await be.saveEvent(gid, {starts_at: new Date(Date.now() + 40 * day).toISOString(), place_name: 'Testbar', place_kind: 'bar'});
  assert.ok(ev.id);
  await be.saveEvent(gid, {id: ev.id, title: 'Umbenannt'});
  assert.equal((await be.event(ev.id)).title, 'Umbenannt');
  let log = await be.changelog(gid);
  assert.equal(log.length, before + 2);
  assert.deepEqual([log[0].action, log[1].action], ['update', 'create']);

  await be.undo(log[0].id);                                   // Änderung zurück
  assert.equal((await be.event(ev.id)).title, null);
  await assert.rejects(be.undo(log[0].id), /schon rückgängig/);

  await be.patchEvent(ev.id, {deleted_at: new Date().toISOString()});
  assert.ok(!(await be.events(gid, new Date())).some(e => e.id === ev.id));
  assert.ok((await be.trash(gid)).events.some(e => e.id === ev.id));
  log = await be.changelog(gid);
  assert.equal(log[0].action, 'delete');
  await be.undo(log[0].id);                                   // Löschen zurück
  assert.ok((await be.events(gid, new Date())).some(e => e.id === ev.id));
  assert.equal((await be.changelog(gid))[0].undo_of, log[0].id);
});

test('Demo: Zusagen, Chat, Reaktionen, Live-Meldungen', async () => {
  const be = await create();
  const g = (await be.myGroups())[0];
  const seen = [];
  const off = be.subscribe(g.id, ev => seen.push(ev.table));
  const ev = (await be.events(g.id, new Date()))[0];
  await be.setRsvp(ev.id, 'maybe');
  await be.setRsvp(ev.id, 'yes');
  const mine = (await be.rsvps(g.id, [ev.id])).filter(r => r.member_id === g.me.id);
  assert.deepEqual(mine.map(r => r.status), ['yes']);          // ersetzt, nicht verdoppelt

  const n = (await be.messages(g.id)).length;
  const msg = await be.sendMessage(g.id, {body: 'Hallo', reply_to: null});
  assert.equal(msg.member_id, g.me.id);
  await be.react(msg.id, '🍻', true);
  await be.react(msg.id, '🍻', false);
  await be.editMessage(msg.id, 'Hallo zusammen');
  const all = await be.messages(g.id);
  assert.equal(all.length, n + 1);
  const last = all[all.length - 1];
  assert.equal(last.body, 'Hallo zusammen');
  assert.ok(last.edited_at);
  assert.deepEqual(last.reactions.map(r => r.active), [false]);
  assert.equal((await be.messages(g.id, {limit: 3})).length, 3);
  assert.ok(seen.includes('rsvps') && seen.includes('messages') && seen.includes('reactions'));
  off();
  seen.length = 0;
  await be.sendMessage(g.id, {body: 'nach dem Abmelden'});
  assert.equal(seen.length, 0);
});

test('Demo: Bilder nach Album, verschieben, Papierkorb', async () => {
  const be = await create();
  const gid = (await be.myGroups())[0].id;
  const all = await be.media(gid);
  assert.equal(all.length, 26);
  const none = await be.media(gid, {eventId: null});
  assert.equal(none.length, 2);
  const one = all.find(m => m.event_id);
  const inAlbum = (await be.media(gid, {eventId: one.event_id})).length;
  await be.patchMedia(one.id, {event_id: null});
  assert.equal((await be.media(gid, {eventId: one.event_id})).length, inAlbum - 1);
  assert.equal((await be.media(gid, {eventId: null})).length, 3);
  await be.patchMedia(one.id, {deleted_at: new Date().toISOString()});
  assert.equal((await be.media(gid)).length, 25);
  assert.equal((await be.trash(gid)).media.length, 1);
  assert.match(await be.url(one.path), /^data:image\/svg\+xml/);
});

// Jede Anbindung muss dieselben Funktionen anbieten: Was die Seiten von Supabase aufrufen, gibt es auch in der Demo.
test('Demo und Supabase bieten dieselben Funktionen an', async () => {
  const be = await create();
  const source = readFileSync(new URL('../web/js/backend/supabase.js', import.meta.url), 'utf8');
  const body = source.slice(source.indexOf("kind: 'supabase'"));
  const names = new Set([...body.matchAll(/^ {4}(?:async )?([a-zA-Z]+)(?::|\()/gm)].map(m => m[1]).filter(n => n !== 'kind'));
  names.add('url');
  assert.ok(names.size > 40, 'Funktionen von supabase.js nicht gefunden');
  const missing = [...names].filter(n => typeof be[n] !== 'function');
  assert.deepEqual(missing, [], 'fehlt in demo.js');
  const extra = Object.keys(be).filter(k => typeof be[k] === 'function' && !names.has(k));
  assert.deepEqual(extra, [], 'fehlt in supabase.js');
});

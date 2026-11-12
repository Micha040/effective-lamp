// Tests für die Hilfsfunktionen der Web-App (web/js/util.js). Aufruf: node --test tests/*.test.js
import test from 'node:test';
import assert from 'node:assert/strict';
import * as u from '../web/js/util.js';

const at = s => new Date(s);   // Ortszeit, damit die Tests in jeder Zeitzone gleich laufen

test('Codes: aufräumen, erkennen, schön schreiben', () => {
  assert.equal(u.cleanCode(' h8u8z-9sdrh '), 'H8U8Z9SDRH');
  assert.equal(u.codeKind('H8U8Z-9SDRH'), 'invite');
  assert.equal(u.codeKind('abcd efgh'), 'device');
  assert.equal(u.codeKind('ABC'), null);
  assert.equal(u.formatCode('h8u8z9sdrh'), 'H8U8Z-9SDRH');
  assert.equal(u.formatCode('abcdefgh'), 'ABCD-EFGH');
  assert.equal(u.formatCode('ab'), 'AB');
});

test('Codes: aus einem Link herauslesen', () => {
  assert.equal(u.parseCodeInput('https://micha040.github.io/effective-lamp/stammtisch/#einladung=H8U8Z-9SDRH'), 'H8U8Z9SDRH');
  assert.equal(u.parseCodeInput('#geraet=ABCD-EFGH'), 'ABCDEFGH');
  assert.equal(u.parseCodeInput('h8u8z 9sdrh'), 'H8U8Z9SDRH');
  assert.equal(u.inviteLink('https://example.org/stammtisch/?x=1#/chat', 'H8U8Z9SDRH'), 'https://example.org/stammtisch/#einladung=H8U8Z-9SDRH');
});

test('Datum: Texte', () => {
  const now = at('2026-11-12T23:00:00');   // ein Donnerstag
  assert.equal(u.fmtDate('2026-11-19T19:00:00', now), 'Do., 19. Nov.');
  assert.equal(u.fmtDate('2027-01-07T19:00:00', now), 'Do., 7. Jan. 2027');
  assert.equal(u.fmtTime('2026-11-19T19:05:00'), '19:05');
  assert.equal(u.relDay('2026-11-12T19:00:00', now), 'Heute');
  assert.equal(u.relDay('2026-11-13T00:30:00', now), 'Morgen');
  assert.equal(u.relDay('2026-11-11T08:00:00', now), 'Gestern');
  assert.equal(u.relDay('2026-11-19T19:00:00', now), 'in 7 Tagen');
  assert.equal(u.relDay('2026-12-03T19:00:00', now), 'in 3 Wochen');
  assert.equal(u.relDay('2026-11-05T19:00:00', now), 'vor 7 Tagen');
  assert.equal(u.fmtChatDay('2026-11-12T10:00:00', now), 'Heute');
  assert.equal(u.fmtChatDay('2026-11-09T10:00:00', now), 'Mo., 9. Nov.');
  assert.equal(u.fmtStamp('2026-11-11T21:13:00', now), 'Gestern, 21:13');
  assert.equal(u.fmtStamp('2026-11-06T21:13:00', now), '06.11., 21:13');
  assert.equal(u.fmtStamp('2025-11-06T21:13:00', now), '06.11.2025');
  assert.equal(u.fmtDuration(72.4), '1:12');
});

test('Datum: nächster Donnerstag', () => {
  const d = (s, h, m) => u.nextThursday(at(s), h, m);
  assert.equal(u.toDateInput(d('2026-11-09T10:00:00')), '2026-11-12');       // Montag → Donnerstag derselben Woche
  assert.equal(u.toDateInput(d('2026-11-12T10:00:00')), '2026-11-12');       // Donnerstagmorgen → heute Abend
  assert.equal(u.toDateInput(d('2026-11-12T23:00:00')), '2026-11-19');       // Donnerstagnacht → nächste Woche
  assert.equal(u.toTimeInput(d('2026-11-09T10:00:00', 20, 30)), '20:30');
  const back = u.fromInputs('2026-11-19', '19:30');
  assert.equal(back.getFullYear() + '-' + (back.getMonth() + 1) + '-' + back.getDate() + ' ' + back.getHours() + ':' + back.getMinutes(), '2026-11-19 19:30');
  assert.equal(u.fromInputs('quatsch', '19:00'), null);
});

test('Treffen: nächstes, kommende, vergangene', () => {
  const now = at('2026-11-12T22:00:00');
  const ev = (id, start, extra = {}) => ({id, starts_at: at(start).toISOString(), ...extra});
  const parts = u.splitEvents([
    ev('später', '2026-11-26T19:00:00'), ev('heute', '2026-11-12T19:00:00'), ev('alt', '2026-11-05T19:00:00'),
    ev('gelöscht', '2026-11-13T19:00:00', {deleted_at: 'x'}), ev('nächste', '2026-11-19T19:00:00'), ev('uralt', '2026-10-01T19:00:00'),
  ], now);
  assert.equal(parts.next.id, 'heute');                       // läuft seit 3 Stunden: steht am Abend selbst noch oben
  assert.deepEqual(parts.upcoming.map(e => e.id), ['nächste', 'später']);
  assert.deepEqual(parts.past.map(e => e.id), ['alt', 'uralt']);
  // 6 Stunden nach Beginn rutscht es ins Archiv
  assert.equal(u.splitEvents([ev('heute', '2026-11-12T19:00:00')], at('2026-11-13T01:30:00')).next, null);
  // ein abgesagtes Treffen steht nicht oben, wenn es ein anderes gibt
  const c = u.splitEvents([ev('abgesagt', '2026-11-13T19:00:00', {canceled: true}), ev('echt', '2026-11-19T19:00:00')], now);
  assert.equal(c.next.id, 'echt');
  assert.deepEqual(c.upcoming.map(e => e.id), ['abgesagt']);
  assert.equal(u.eventTitle({title: '  '}), 'Stammtisch');
  assert.equal(u.eventTitle({title: 'Grillen'}), 'Grillen');
});

test('Zusagen: nach Antwort sortiert', () => {
  const members = [{id: 'a', name: 'A'}, {id: 'b', name: 'B'}, {id: 'c', name: 'C'}, {id: 'd', name: 'D'}, {id: 'x', name: 'Ex', removed_at: 'x'}, {id: 'y', name: 'Ey', removed_at: 'x'}];
  const rsvps = [
    {event_id: 'e1', member_id: 'a', status: 'yes'}, {event_id: 'e1', member_id: 'b', status: 'maybe'}, {event_id: 'e1', member_id: 'c', status: 'none'},
    {event_id: 'e1', member_id: 'x', status: 'yes'}, {event_id: 'e1', member_id: 'y', status: 'no'}, {event_id: 'e2', member_id: 'd', status: 'yes'},
  ];
  const g = u.rsvpGroups(rsvps, members, 'e1');
  assert.deepEqual(g.yes.map(m => m.id), ['a', 'x']);          // Ehemalige zählen nur, wenn sie zugesagt hatten
  assert.deepEqual(g.maybe.map(m => m.id), ['b']);
  assert.deepEqual(g.open.map(m => m.id), ['c', 'd']);         // zurückgezogen = wieder offen
  assert.equal(u.rsvpLine(g), '2 dabei · 1 vielleicht');
  assert.equal(u.rsvpLine({yes: [], maybe: [], no: [], open: []}), 'Noch keine Antworten');
});

test('Karten-Links', () => {
  const l = u.mapsLinks({place_name: 'Zum Goldenen Hahn', place_address: 'Schulterblatt 41', place_lat: 53.5627, place_lon: 9.9612});
  assert.match(l.apple, /^https:\/\/maps\.apple\.com\/\?q=Zum%20Goldenen%20Hahn%2C%20Schulterblatt%2041&ll=53\.5627,9\.9612$/);
  assert.match(l.google, /query=Zum%20Goldenen%20Hahn/);
  assert.match(u.mapsLinks({name: 'Bei Tom'}).osm, /search\?query=Bei%20Tom/);
});

test('Verlauf: Texte und Rückgängig', () => {
  const ev = {id: 'e', starts_at: at('2026-11-19T19:00:00').toISOString(), title: null, place_name: 'Hahn', canceled: false, note: null};
  const upd = {entity: 'event', action: 'update', before: ev, after: {...ev, title: 'Weihnachten', note: 'Tisch reserviert', updated_at: 'egal'}};
  assert.deepEqual(u.changedFields(upd).map(f => f.label), ['Titel', 'Notiz']);
  assert.deepEqual(u.changedFields(upd)[0], {key: 'title', label: 'Titel', from: '–', to: 'Weihnachten'});
  assert.equal(u.changeVerb(upd), 'hat das Treffen geändert');
  assert.equal(u.changeSubject(upd), `Weihnachten am ${u.fmtDate(ev.starts_at)}`);
  assert.equal(u.changeVerb({entity: 'event', action: 'update', before: ev, after: {...ev, canceled: true}}), 'hat das Treffen abgesagt');
  assert.equal(u.changeVerb({entity: 'event', action: 'create', after: ev}), 'hat ein Treffen angelegt');
  assert.equal(u.changeVerb({entity: 'place', action: 'delete', before: {}, after: {name: 'Hahn'}}), 'hat den Ort gelöscht');
  assert.equal(u.changeVerb({entity: 'member', action: 'create', after: {name: 'Tom'}}), 'ist beigetreten');
  assert.equal(u.changeVerb({entity: 'member', action: 'delete', member_id: 'm1', entity_id: 'm1', after: {name: 'Tom'}}), 'ist ausgetreten');
  assert.equal(u.changeVerb({entity: 'member', action: 'delete', member_id: 'm0', entity_id: 'm1', after: {name: 'Tom'}}), 'hat Tom entfernt');
  assert.equal(u.changeVerb({entity: 'member', action: 'update', before: {name: 'Tom', role: 'member'}, after: {name: 'Tom', role: 'admin'}}), 'hat Tom zum Admin gemacht');
  assert.equal(u.changeVerb({entity: 'media', action: 'create', after: {kind: 'video'}}), 'hat ein Video hochgeladen');
  assert.equal(u.changeVerb({entity: 'device', action: 'create', after: {name: 'Tom'}}), 'hat einen Gerätecode für Tom erzeugt');

  assert.equal(u.canUndo(upd, false), true);
  assert.equal(u.canUndo({...upd, undone_at: 'x'}, true), false);
  assert.equal(u.canUndo({entity: 'invite', action: 'update'}, true), false);
  assert.equal(u.canUndo({entity: 'member', action: 'create'}, true), false);
  assert.equal(u.canUndo({entity: 'member', action: 'delete'}, false), false);
  assert.equal(u.canUndo({entity: 'member', action: 'delete'}, true), true);
  assert.equal(u.canUndo({entity: 'member', action: 'update', before: {name: 'A', role: 'member'}, after: {name: 'B', role: 'member'}}, false), true);
  assert.equal(u.canUndo({entity: 'member', action: 'update', before: {role: 'member'}, after: {role: 'admin'}}, false), false);
  assert.equal(u.canUndo({entity: 'event', action: 'update', before: ev, after: {...ev, updated_at: 'neu'}}, true), false);   // nichts Sichtbares geändert
});

test('Verlauf: viele Bilder am Stück werden ein Eintrag', () => {
  const t = s => new Date(Date.UTC(2026, 10, 12, 20, 0, s)).toISOString();
  const m = (id, member, sec, action = 'create') => ({id, entity: 'media', action, member_id: member, at: t(sec)});
  const groups = u.groupChanges([m(6, 'a', 50), m(5, 'a', 40), m(4, 'b', 30), m(3, 'a', 20, 'delete'), {id: 2, entity: 'event', action: 'create', member_id: 'a', at: t(10)}, m(1, 'a', 0)]);
  assert.deepEqual(groups.map(g => g.items.map(c => c.id)), [[6, 5], [4], [3], [2], [1]]);
});

test('Verschiedenes: Größen, Namen, Pfade, Links', () => {
  assert.equal(u.fmtBytes(300 * 1024), '300 KB');
  assert.equal(u.fmtBytes(87 * 1024 * 1024), '87 MB');
  assert.equal(u.fmtBytes(1024 ** 3), '1 GB');
  assert.equal(u.initials('Micha Kröger'), 'MK');
  assert.equal(u.initials('tom'), 'T');
  assert.equal(u.initials(''), '?');
  assert.equal(u.colorFor('abc'), u.colorFor('abc'));
  assert.deepEqual(u.fitSize(4000, 3000, 1600), {width: 1600, height: 1200});
  assert.deepEqual(u.fitSize(800, 600, 1600), {width: 800, height: 600});         // nie vergrößern
  assert.equal(u.extFor('audio/mp4;codecs=mp4a.40.2'), 'm4a');
  assert.equal(u.extFor('video/quicktime'), 'mov');
  assert.equal(u.mediaPath('g1', 'album', 'id1', 'jpg', at('2026-03-05T12:00:00')), 'g1/album/2026-03/id1.jpg');
  assert.match(u.uuid(), /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
  assert.deepEqual(u.linkify('Schau: https://example.org/x. Ok'), [{text: 'Schau: '}, {href: 'https://example.org/x', text: 'https://example.org/x'}, {text: '. Ok'}]);
  assert.deepEqual(u.linkify('<b>kein HTML</b>'), [{text: '<b>kein HTML</b>'}]);
});

test('Bilder: passendes Album vorschlagen', () => {
  const ev = (id, start, extra = {}) => ({id, starts_at: at(start).toISOString(), ...extra});
  const events = [ev('letzte', '2026-11-05T19:00:00'), ev('heute', '2026-11-12T19:00:00'), ev('kommt', '2026-11-19T19:00:00')];
  assert.equal(u.guessAlbumEvent(events, at('2026-11-12T22:30:00')).id, 'heute');   // am Abend selbst
  assert.equal(u.guessAlbumEvent(events, at('2026-11-13T11:00:00')).id, 'heute');   // am Morgen danach
  assert.equal(u.guessAlbumEvent(events, at('2026-11-16T11:00:00')).id, 'heute');   // Tage später: das letzte vergangene
  assert.equal(u.guessAlbumEvent([ev('kommt', '2026-11-19T19:00:00')], at('2026-11-12T12:00:00')), null);
});

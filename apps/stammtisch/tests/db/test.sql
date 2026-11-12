-- Datenbank-Tests für STAMMTISCH: Spielt mit vier Geräten (Micha, Tom, Kim, Toms neues Handy) und zwei Gruppen durch,
-- wer was sehen und ändern darf. Läuft gegen einen frischen PostgreSQL (siehe tests/db.sh).

create schema t;
create table t.count (n integer);
insert into t.count values (0);

-- als dieses Gerät weitermachen (null = nicht angemeldet)
create function t.be(u uuid) returns void language plpgsql as $$
begin
  perform set_config('request.jwt.claim.sub', coalesce(u::text, ''), false);
  if u is null then execute 'set role anon'; else execute 'set role authenticated'; end if;
end $$;
-- als Datenbank-Besitzer weitermachen (darf alles)
create function t.root() returns void language plpgsql as $$
begin
  execute 'reset role';
  perform set_config('request.jwt.claim.sub', '', false);
end $$;
create function t.ok(cond boolean, label text) returns void language plpgsql security definer as $$
begin
  if cond is not true then raise exception 'FEHLGESCHLAGEN: %', label; end if;
  update t.count set n = n + 1;
end $$;
-- true, wenn die Anweisung mit einem Fehler abbricht (z. B. von einer Zugriffsregel abgelehnt)
create function t.fails(q text) returns boolean language plpgsql as $$
begin
  execute q;
  return false;
exception when others then
  return true;
end $$;
-- Anzahl der Zeilen, die eine Abfrage liefert bzw. eine Änderung trifft
create function t.rows(q text) returns bigint language plpgsql as $$
declare n bigint;
begin
  execute q;
  get diagnostics n = row_count;
  return n;
end $$;
grant usage on schema t to anon, authenticated;
grant execute on all functions in schema t to anon, authenticated;

do $$
declare
  micha uuid := '00000000-0000-4000-8000-00000000000a';
  tom   uuid := '00000000-0000-4000-8000-00000000000b';
  kim   uuid := '00000000-0000-4000-8000-00000000000c';
  tom2  uuid := '00000000-0000-4000-8000-00000000000d';   -- Toms neues Handy
  g1 uuid; g2 uuid;                    -- STAMMTISCH, Kegelclub
  m_micha uuid; m_tom uuid; m_kim uuid;
  founder text := 'GRUENDER22';
  code1 text; code2 text; dcode text;
  ev uuid; ev2 uuid; pl uuid; msg_a uuid; msg_b uuid; med uuid; chg bigint;
  n integer;
begin
  -- ---- Einrichtung: Geräte, erste Gruppe, Gründer-Link ---------------------------------------------------------------
  insert into auth.users (id) values (micha), (tom), (kim), (tom2);
  insert into public.groups (name) values ('STAMMTISCH') returning id into g1;
  insert into public.invites (code, group_id, role, single_use) values (founder, g1, 'admin', true);

  perform t.ok(private.new_code(10) ~ '^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{10}$', 'Codes: 10 gut lesbare Zeichen');
  perform t.ok(private.new_code(8) <> private.new_code(8), 'Codes: jedes Mal anders');
  perform t.ok(private.clean_code(' abcd-efgh ') = 'ABCDEFGH', 'Codes: Eingabe wird aufgeräumt');
  perform t.ok(public.code_valid('gruen-der22'), 'Gründer-Link gilt');
  perform t.ok(not public.code_valid('FALSCH1234'), 'falscher Code gilt nicht');

  -- ---- Nicht angemeldet: nichts außer dem Lebenszeichen --------------------------------------------------------------
  perform t.be(null);
  perform t.ok(public.ping() is not null, 'nicht angemeldet: ping geht');
  perform t.ok(t.fails('select * from public.groups'), 'nicht angemeldet: keine Gruppen lesbar');
  perform t.ok(t.fails('select * from public.messages'), 'nicht angemeldet: keine Nachrichten lesbar');
  perform t.ok(t.fails('select public.my_groups()'), 'nicht angemeldet: my_groups gesperrt');
  perform t.ok(t.fails('select public.code_valid(''GRUENDER22'')'), 'nicht angemeldet: code_valid gesperrt');
  perform t.ok(t.fails('select public.join_group(''GRUENDER22'', ''Niemand'')'), 'nicht angemeldet: kein Beitritt');

  -- ---- Micha tritt mit dem Gründer-Link bei und wird Admin ----------------------------------------------------------
  perform t.be(micha);
  perform t.ok(t.rows('select * from public.groups') = 0, 'vor dem Beitritt: keine Gruppe sichtbar');
  perform t.ok(t.fails('select public.code_valid(''GRUENDER22'')'), 'App darf code_valid nicht aufrufen');
  perform t.ok(t.fails($q$select public.join_group('GRUENDER22', '')$q$), 'Beitritt ohne Namen abgelehnt');
  perform t.ok(public.join_group('gruen-der22', ' Micha ') = g1, 'Micha tritt bei');
  perform t.ok(jsonb_array_length(public.my_groups()) = 1, 'Micha: eine Gruppe');
  perform t.ok(public.my_groups() -> 0 -> 'me' ->> 'role' = 'admin' and public.my_groups() -> 0 -> 'me' ->> 'name' = 'Micha',
               'Micha ist Admin, Name ohne Leerzeichen');
  m_micha := (public.my_groups() -> 0 -> 'me' ->> 'id')::uuid;
  code1 := public.get_invite(g1);
  perform t.ok(char_length(code1) = 10 and public.get_invite(g1) = code1, 'Einladungscode bleibt gleich');
  perform t.ok(public.join_group(code1, 'Nochmal') = g1, 'nochmal beitreten: schon dabei');
  perform t.ok(t.rows('select * from public.members') = 1, 'nochmal beitreten: kein Doppelgänger');

  perform t.be(tom);
  perform t.ok(t.fails($q$select public.join_group('GRUENDER22', 'Tom')$q$), 'Gründer-Link gilt nur einmal');
  perform t.ok(t.fails($q$select public.create_group('Fremd', 'Tom')$q$), 'ohne Gruppe keine neue Gruppe gründen');
  perform t.ok(public.join_group(code1, 'Tom') = g1, 'Tom tritt bei');
  m_tom := (public.my_groups() -> 0 -> 'me' ->> 'id')::uuid;
  perform t.ok(public.my_groups() -> 0 -> 'me' ->> 'role' = 'member', 'Tom ist normales Mitglied');
  perform t.ok(t.rows('select * from public.members') = 2, 'Tom sieht beide Mitglieder');

  -- ---- Zweite Gruppe: Micha gründet den Kegelclub, Kim tritt nur dort bei --------------------------------------------
  perform t.be(micha);
  g2 := public.create_group('Kegelclub', '');
  perform t.ok(jsonb_array_length(public.my_groups()) = 2, 'Micha: zwei Gruppen');
  code2 := public.get_invite(g2);
  perform t.be(kim);
  perform t.ok(t.fails(format('select public.get_invite(%L)', g1)), 'Kim: kein Einladungscode für fremde Gruppe');
  perform t.ok(public.join_group(code2, 'Kim') = g2, 'Kim tritt dem Kegelclub bei');
  m_kim := (public.my_groups() -> 0 -> 'me' ->> 'id')::uuid;
  perform t.ok(t.rows('select * from public.groups') = 1, 'Kim sieht nur den Kegelclub');
  perform t.ok(t.rows('select * from public.members') = 2, 'Kim sieht nur dessen Mitglieder');
  perform t.ok(t.rows('select * from public.invites') = 1, 'Kim sieht nur dessen Einladung');

  -- ---- Orte und Treffen ----------------------------------------------------------------------------------------------
  perform t.be(micha);
  insert into public.places (group_id, name, address, lat, lon, kind) values (g1, 'Zum Goldenen Hahn', 'Hauptstr. 1', 53.55, 9.99, 'bar') returning id into pl;
  insert into public.events (group_id, starts_at, place_id, place_name, place_kind) values (g1, now() + interval '2 days', pl, 'Zum Goldenen Hahn', 'bar') returning id into ev;
  perform t.ok((select created_by from public.events where id = ev) = m_micha, 'Treffen: Urheber wird eingetragen');
  perform t.ok(t.fails(format('insert into public.events (group_id, starts_at, created_by) values (%L, now(), %L)', g1, m_tom)),
               'Treffen: Urheber lässt sich nicht fälschen');
  perform t.ok(t.fails(format('insert into public.events (group_id, starts_at, place_id) values (%L, now(), %L)', g2, pl)),
               'Treffen: Ort aus anderer Gruppe abgelehnt');

  perform t.be(tom);
  perform t.ok(t.rows(format($q$update public.events set title = 'Weihnachts-Stammtisch' where id = %L$q$, ev)) = 1, 'Tom darf das Treffen ändern');
  perform t.ok(t.fails(format('delete from public.events where id = %L', ev)), 'endgültig löschen geht nicht (nur Papierkorb)');
  perform public.set_rsvp(ev, 'yes');
  perform t.ok((select status from public.rsvps where event_id = ev and member_id = m_tom) = 'yes', 'Tom sagt zu');
  perform public.set_rsvp(ev, 'maybe');
  perform t.ok(t.rows('select * from public.rsvps') = 1, 'Zusage wird ersetzt, nicht verdoppelt');
  perform t.ok(t.fails(format($q$insert into public.rsvps (event_id, member_id, group_id, status) values (%L, %L, %L, 'yes')$q$, ev, m_micha, g1)),
               'Zusagen für andere eintragen geht nicht');
  perform t.ok(t.fails(format($q$select public.set_rsvp(%L, 'vielleicht')$q$, ev)), 'unbekannter Zusage-Wert abgelehnt');

  perform t.be(kim);
  perform t.ok(t.rows('select * from public.events') = 0, 'Kim sieht keine Treffen der anderen Gruppe');
  perform t.ok(t.rows('select * from public.places') = 0, 'Kim sieht keine Orte der anderen Gruppe');
  perform t.ok(t.rows(format($q$update public.events set title = 'gekapert' where id = %L$q$, ev)) = 0, 'Kim kann fremdes Treffen nicht ändern');
  perform t.ok(t.fails(format('insert into public.events (group_id, starts_at) values (%L, now())', g1)), 'Kim kann in fremder Gruppe nichts anlegen');
  perform t.ok(t.fails(format($q$select public.set_rsvp(%L, 'yes')$q$, ev)), 'Kim kann für fremdes Treffen nicht zusagen');
  perform t.ok(t.rows('select * from public.rsvps') = 0, 'Kim sieht keine fremden Zusagen');

  -- ---- Chat ----------------------------------------------------------------------------------------------------------
  perform t.be(micha);
  insert into public.messages (group_id, body) values (g1, 'Wer ist Donnerstag dabei?') returning id into msg_a;
  perform t.be(tom);
  insert into public.messages (group_id, body, reply_to) values (g1, 'Ich!', msg_a) returning id into msg_b;
  perform t.ok((select member_id from public.messages where id = msg_b) = m_tom, 'Nachricht: Absender wird eingetragen');
  perform t.ok(t.fails(format($q$insert into public.messages (group_id, body, member_id) values (%L, 'als Micha', %L)$q$, g1, m_micha)),
               'Nachricht: Absender lässt sich nicht fälschen');
  perform t.ok(t.fails(format($q$insert into public.messages (group_id, body) values (%L, '   ')$q$, g1)), 'leere Nachricht abgelehnt');
  perform public.react(msg_a, '🍻', true);
  perform public.react(msg_a, '🍻', true);
  perform t.ok(t.rows('select * from public.reactions where active') = 1, 'Reaktion gesetzt (nur einmal)');
  perform public.react(msg_a, '🍻', false);
  perform t.ok(t.rows('select * from public.reactions where active') = 0, 'Reaktion zurückgenommen');
  perform t.ok(t.fails(format($q$update public.messages set body = 'umgeschrieben' where id = %L$q$, msg_a)), 'fremde Nachricht nicht änderbar');
  perform t.ok(t.fails(format('update public.messages set deleted_at = now() where id = %L', msg_a)), 'fremde Nachricht nicht löschbar (kein Admin)');
  perform t.ok(t.rows(format($q$update public.messages set body = 'Ich auch!' where id = %L$q$, msg_b)) = 1, 'eigene Nachricht ändern');
  perform t.ok((select edited_at from public.messages where id = msg_b) is not null, '„bearbeitet“ wird gesetzt');

  perform t.be(kim);
  perform t.ok(t.rows('select * from public.messages') = 0, 'Kim liest keinen fremden Chat');
  perform t.ok(t.fails(format($q$insert into public.messages (group_id, body) values (%L, 'Hallo?')$q$, g1)), 'Kim schreibt nicht in fremden Chat');
  perform t.ok(t.fails(format($q$select public.react(%L, '👍', true)$q$, msg_a)), 'Kim reagiert nicht in fremdem Chat');
  perform t.ok(t.fails(format($q$insert into public.messages (group_id, body, reply_to) values (%L, 'quer', %L)$q$, g2, msg_a)),
               'Antwort auf Nachricht einer anderen Gruppe abgelehnt');

  perform t.be(micha);
  perform t.ok(t.rows(format('update public.messages set deleted_at = now() where id = %L', msg_b)) = 1, 'Admin darf fremde Nachricht löschen');
  perform t.ok(t.fails(format($q$update public.messages set body = 'vom Admin' where id = %L$q$, msg_b)), 'Admin darf fremde Nachricht nicht umschreiben');

  -- ---- Bilder und Dateispeicher -------------------------------------------------------------------------------------
  perform t.be(tom);
  insert into storage.objects (bucket_id, name, owner_id, metadata) values ('media', g1 || '/album/2026-11/a.jpg', tom::text, '{"size": 300000}');
  insert into public.media (group_id, event_id, kind, path, size_bytes) values (g1, ev, 'image', g1 || '/album/2026-11/a.jpg', 300000) returning id into med;
  perform t.ok((select member_id from public.media where id = med) = m_tom, 'Bild: wer hochlädt, wird eingetragen');
  perform t.ok(t.fails(format($q$insert into public.media (group_id, kind, path) values (%L, 'image', %L)$q$, g1, g2 || '/album/x.jpg')),
               'Bild: Pfad einer anderen Gruppe abgelehnt');
  perform t.ok(t.fails(format($q$insert into storage.objects (bucket_id, name, owner_id) values ('media', %L, %L)$q$, g2 || '/album/x.jpg', tom)),
               'Speicher: kein Hochladen in fremde Gruppe');
  perform t.ok(t.fails($q$insert into storage.objects (bucket_id, name) values ('media', 'irgendwo/x.jpg')$q$), 'Speicher: Pfad ohne Gruppe abgelehnt');
  perform t.ok(t.rows('delete from storage.objects') = 0, 'Speicher: Datei eines vorhandenen Bildes nicht löschbar');
  perform t.ok((public.storage_usage(g1) ->> 'group_bytes')::bigint = 300000, 'Speicher: Verbrauch der Gruppe');

  perform t.be(kim);
  perform t.ok(t.rows('select * from public.media') = 0 and t.rows('select * from storage.objects') = 0, 'Kim sieht keine fremden Bilder');
  perform t.ok(t.fails(format('select public.storage_usage(%L)', g1)), 'Kim: kein Speicherverbrauch fremder Gruppen');
  perform t.ok((public.storage_usage(g2) ->> 'group_bytes')::bigint = 0 and (public.storage_usage(g2) ->> 'total_bytes')::bigint = 300000,
               'Speicher: eigene Gruppe leer, Gesamtverbrauch zählt alle');

  -- Papierkorb: erst nach 30 Tagen lässt sich die Datei wirklich löschen
  perform t.be(micha);
  perform t.ok(t.rows(format('update public.media set deleted_at = now() where id = %L', med)) = 1, 'Micha legt Toms Bild in den Papierkorb');
  perform t.ok(t.rows('select * from public.trash_expired(''' || g1 || ''')') = 0, 'frisch gelöscht: noch nichts zum Aufräumen');
  perform t.ok(t.rows('delete from storage.objects') = 0, 'Speicher: Datei im Papierkorb noch geschützt');
  perform t.root();
  update public.media set deleted_at = now() - interval '31 days' where id = med;
  update public.messages set deleted_at = now() - interval '31 days' where id = msg_b;
  perform t.be(kim);
  perform t.ok(t.rows('select * from public.trash_expired(''' || g1 || ''')') = 0, 'Kim: kein Aufräumen in fremder Gruppe');
  perform t.ok(t.fails(format('select public.purge_trash(%L, null)', g1)), 'Kim: purge_trash in fremder Gruppe abgelehnt');
  perform t.be(micha);
  perform t.ok(t.rows('select * from public.trash_expired(''' || g1 || ''')') = 1, 'nach 31 Tagen: ein Bild zum Aufräumen');
  perform t.ok(t.rows('delete from storage.objects') = 1, 'Speicher: Datei nach 30 Tagen löschbar');
  perform t.ok(public.purge_trash(g1, array[med]) = 2, 'endgültig gelöscht: Bild und alte Nachricht');
  perform t.ok(t.rows('select * from public.messages') = 1, 'die andere Nachricht bleibt');

  -- ---- Verlauf und rückgängig ----------------------------------------------------------------------------------------
  perform t.ok(t.rows($q$select * from public.changelog where entity = 'event' and action = 'create'$q$) = 1, 'Verlauf: Treffen angelegt');
  select id into chg from public.changelog where entity = 'event' and action = 'update' order by id desc limit 1;
  perform t.ok((select member_id from public.changelog where id = chg) = m_tom
               and (select after ->> 'title' from public.changelog where id = chg) = 'Weihnachts-Stammtisch', 'Verlauf: Toms Änderung mit vorher/nachher');
  perform t.be(kim);
  perform t.ok(t.rows('select * from public.changelog where group_id = ''' || g1 || '''') = 0, 'Kim sieht keinen fremden Verlauf');
  perform t.ok(t.fails(format('select public.undo_change(%s)', chg)), 'Kim kann nichts Fremdes rückgängig machen');
  perform t.be(micha);
  perform t.rows(format($q$update public.events set note = 'Tisch ist reserviert' where id = %L$q$, ev));
  perform public.undo_change(chg);
  perform t.ok((select title from public.events where id = ev) is null, 'rückgängig: Titel wieder wie vorher');
  perform t.ok((select note from public.events where id = ev) = 'Tisch ist reserviert', 'rückgängig: spätere Änderung an anderem Feld bleibt');
  perform t.ok((select undo_of from public.changelog order by id desc limit 1) = chg, 'Verlauf: Eintrag verweist auf das Rückgängig-Gemachte');
  perform t.ok(t.fails(format('select public.undo_change(%s)', chg)), 'zweimal rückgängig geht nicht');
  select id into chg from public.changelog where entity = 'event' and action = 'create';
  perform public.undo_change(chg);
  perform t.ok((select deleted_at from public.events where id = ev) is not null, 'rückgängig von „angelegt“: Treffen im Papierkorb');
  select id into chg from public.changelog where entity = 'event' and action = 'delete' order by id desc limit 1;
  perform public.undo_change(chg);
  perform t.ok((select deleted_at from public.events where id = ev) is null, 'rückgängig von „gelöscht“: Treffen wieder da');
  perform t.rows(format($q$update public.groups set name = 'STAMMTISCH e. V.' where id = %L$q$, g1));
  perform t.ok(t.rows($q$select * from public.changelog where entity = 'group' and action = 'update'$q$) = 1, 'Verlauf: Gruppenname geändert');
  perform t.ok(t.fails('insert into public.changelog (group_id, entity, action) values (''' || g1 || ''', ''event'', ''create'')'),
               'Verlauf lässt sich von außen nicht beschreiben');

  -- ---- Profil und Rollen ---------------------------------------------------------------------------------------------
  perform t.be(tom);
  perform t.ok(t.rows(format($q$update public.members set name = 'Tommy' where id = %L$q$, m_tom)) = 1, 'eigenen Namen ändern');
  perform t.ok(t.rows(format($q$update public.members set name = 'Chef' where id = %L$q$, m_micha)) = 0, 'fremden Namen nicht ändern');
  perform t.ok(t.fails(format($q$update public.members set role = 'admin' where id = %L$q$, m_tom)), 'sich nicht selbst zum Admin machen');
  perform t.ok(t.fails(format($q$select public.set_member_role(%L, 'admin')$q$, m_tom)), 'Rollen vergibt nur ein Admin');
  perform t.ok(t.fails(format('select public.remove_member(%L)', m_micha)), 'entfernen darf nur ein Admin');
  perform t.be(micha);
  perform t.ok(t.fails(format($q$select public.set_member_role(%L, 'member')$q$, m_micha)), 'letzter Admin bleibt Admin');
  perform t.ok(t.fails(format('select public.remove_member(%L)', m_micha)), 'letzter Admin kann nicht einfach austreten');
  perform t.ok(t.fails(format($q$select public.set_member_role(%L, 'admin')$q$, m_kim)) = false, 'Micha ist auch im Kegelclub Admin');
  perform public.set_member_role(m_tom, 'admin');
  perform public.set_member_role(m_tom, 'member');

  -- ---- Neues Handy: Gerätecode ---------------------------------------------------------------------------------------
  perform t.be(kim);
  perform t.ok(t.fails(format('select public.create_device_code(%L)', m_tom)), 'Kim: kein Gerätecode für fremde Gruppe');
  perform t.be(micha);
  dcode := public.create_device_code(m_tom);
  perform t.ok(char_length(dcode) = 8, 'Gerätecode: 8 Zeichen');
  perform t.ok(t.rows($q$select * from public.changelog where entity = 'device'$q$) = 1, 'Verlauf: Gerätecode für jemand anderen');
  perform t.be(tom2);
  perform t.ok(t.rows('select * from public.events') = 0, 'neues Handy sieht vorher nichts');
  perform t.ok(public.link_device(dcode) = g1, 'neues Handy gekoppelt');
  perform t.ok(public.my_groups() -> 0 -> 'me' ->> 'name' = 'Tommy', 'neues Handy ist Tom');
  perform t.ok(t.rows('select * from public.events') = 1, 'neues Handy sieht die Treffen');
  perform t.be(kim);
  perform t.ok(t.fails(format('select public.link_device(%L)', dcode)), 'Gerätecode gilt nur einmal');
  perform t.be(micha);
  dcode := public.create_device_code(m_micha);
  perform t.root();
  update public.device_codes set expires_at = now() - interval '1 minute' where code = dcode;
  perform t.be(kim);
  perform t.ok(t.fails(format('select public.link_device(%L)', dcode)), 'abgelaufener Gerätecode abgelehnt');

  -- ---- Entfernen, wieder aufnehmen, neuer Einladungscode -------------------------------------------------------------
  perform t.be(micha);
  perform public.remove_member(m_tom);
  perform t.be(tom);
  perform t.ok(t.rows('select * from public.events') = 0 and jsonb_array_length(public.my_groups()) = 0, 'entfernt: Tom sieht nichts mehr');
  perform t.ok(t.fails(format('select public.join_group(%L, ''Tom'')', code1)), 'entfernt: kein Wiedereintritt per Code');
  perform t.be(tom2);
  perform t.ok(t.rows('select * from public.messages') = 0, 'entfernt: auch Toms zweites Gerät sieht nichts');
  perform t.be(micha);
  perform t.ok(t.rows('select * from public.members where removed_at is not null') = 1, 'entfernte Person bleibt für alte Beiträge sichtbar');
  perform public.restore_member(m_tom);
  perform t.be(tom);
  perform t.ok(t.rows('select * from public.events') = 1, 'wieder aufgenommen: Tom sieht wieder alles');
  perform public.remove_member(m_tom);
  perform t.ok(jsonb_array_length(public.my_groups()) = 0, 'selbst austreten geht');

  perform t.be(micha);
  perform t.ok(public.rotate_invite(g1) <> code1, 'neuer Einladungscode');
  perform t.root();
  perform t.ok(not public.code_valid(code1), 'alter Einladungscode gilt nicht mehr');

  -- ---- Live-Aktualisierung: nur neu/geändert, alle Tabellen angemeldet ---------------------------------------------
  perform t.ok((select count(*) from pg_publication_tables where pubname = 'supabase_realtime' and schemaname = 'public') = 8, 'Live: acht Tabellen');
  perform t.ok((select pubinsert and pubupdate and not pubdelete from pg_publication where pubname = 'supabase_realtime'), 'Live: keine Lösch-Ereignisse');
  -- jede Tabelle in public hat Zugriffsregeln eingeschaltet
  perform t.ok((select bool_and(c.relrowsecurity) from pg_class c join pg_namespace ns on ns.oid = c.relnamespace
                where ns.nspname = 'public' and c.relkind = 'r'), 'jede Tabelle hat Zugriffsregeln');

  select count.n into n from t.count;
  raise notice '% Prüfungen bestanden', n;
end $$;

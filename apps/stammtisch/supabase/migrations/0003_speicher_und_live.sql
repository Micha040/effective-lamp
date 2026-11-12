-- STAMMTISCH – Datenbank, Teil 3: Dateispeicher und Live-Aktualisierung (beides Supabase-eigen)

-- ---- Speicher: ein privater Bucket, Dateien liegen unter <gruppe>/… ---------------------------------------------------
-- Höchstens 50 MB je Datei (Grenze des kostenlosen Tarifs), nur Bilder, Videos und Ton.
insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values ('media', 'media', false, 52428800, array['image/*', 'video/*', 'audio/*'])
on conflict (id) do nothing;

-- Lesen und hochladen: nur Mitglieder der Gruppe, die im Pfad vorn steht
create policy stammtisch_media_select on storage.objects for select to authenticated
  using (bucket_id = 'media' and private.is_member(private.path_group(name)));
create policy stammtisch_media_insert on storage.objects for insert to authenticated
  with check (bucket_id = 'media' and private.is_member(private.path_group(name)));
-- Löschen: nur, was 30 Tage im Papierkorb lag, das eigene Profilbild oder ein eigener abgebrochener Upload
create policy stammtisch_media_delete on storage.objects for delete to authenticated
  using (bucket_id = 'media' and private.is_member(private.path_group(name)) and private.may_delete_file(name, owner_id));

-- ---- Live: Änderungen an diesen Tabellen kommen sofort bei allen an, die sie lesen dürfen -----------------------------
alter publication supabase_realtime add table public.groups, public.members, public.places, public.events, public.rsvps,
  public.media, public.messages, public.reactions;
-- Nur "neu" und "geändert" senden: Für gelöschte Zeilen kann Supabase die Leseregeln nicht prüfen. Die App löscht
-- ohnehin nichts sofort (Papierkorb), das endgültige Aufräumen nach 30 Tagen muss niemand live sehen.
alter publication supabase_realtime set (publish = 'insert, update');

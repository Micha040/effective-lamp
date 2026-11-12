-- STAMMTISCH – Datenbank, Teil 2: Funktionen, die die App aufruft
--
-- Alles, was mehr ist als "Zeile lesen/schreiben" (beitreten, Gerät koppeln, Rollen, rückgängig, aufräumen), läuft über
-- diese Funktionen. Jede prüft selbst, ob der Aufrufer das darf.

-- ---- Anmeldung und Gruppen -------------------------------------------------------------------------------------------

-- Lebenszeichen: hält das kostenlose Supabase-Projekt wach (siehe .github/workflows/stammtisch-wecker.yml)
create function public.ping() returns timestamptz
language sql stable set search_path = '' as $$
  select now()
$$;

-- Gilt dieser Einladungs- oder Gerätecode? Nur für die Konto-Funktion (Edge Function "konto"), nicht für die App.
create function public.code_valid(p_code text) returns boolean
language sql stable security definer set search_path = '' as $$
  select exists (select 1 from public.invites i
                 where i.code = private.clean_code(p_code) and i.revoked_at is null and (not i.single_use or i.used_at is null))
      or exists (select 1 from public.device_codes d
                 where d.code = private.clean_code(p_code) and d.used_at is null and d.expires_at > now())
$$;

-- Meine Gruppen samt meinem Eintrag darin
create function public.my_groups() returns jsonb
language sql stable security definer set search_path = '' as $$
  select coalesce(jsonb_agg(jsonb_build_object(
           'id', g.id, 'name', g.name,
           'me', jsonb_build_object('id', m.id, 'name', m.name, 'role', m.role, 'avatar_path', m.avatar_path))
           order by g.created_at), '[]'::jsonb)
  from public.member_users mu
  join public.members m on m.id = mu.member_id
  join public.groups g on g.id = mu.group_id
  where mu.user_id = auth.uid() and m.removed_at is null
$$;

-- Mit einem Einladungscode beitreten. Gibt die Gruppe zurück.
create function public.join_group(p_code text, p_name text) returns uuid
language plpgsql security definer set search_path = '' as $$
declare
  v_uid uuid := auth.uid();
  v_code text := private.clean_code(p_code);
  v_name text := btrim(coalesce(p_name, ''));
  v_inv public.invites;
  v_old public.members;
  v_member uuid;
begin
  if v_uid is null then raise exception 'Nicht angemeldet.'; end if;
  select * into v_inv from public.invites i
   where i.code = v_code and i.revoked_at is null and (not i.single_use or i.used_at is null)
   for update;
  if v_inv.code is null then raise exception 'Dieser Einladungscode gilt nicht (mehr).'; end if;

  select m.* into v_old from public.member_users mu join public.members m on m.id = mu.member_id
   where mu.user_id = v_uid and mu.group_id = v_inv.group_id;
  if v_old.id is not null then
    if v_old.removed_at is not null then
      raise exception 'Du wurdest aus dieser Gruppe entfernt. Ein Admin kann dich wieder aufnehmen.';
    end if;
    return v_inv.group_id;                      -- schon dabei
  end if;

  if char_length(v_name) not between 1 and 40 then raise exception 'Bitte einen Namen mit 1 bis 40 Zeichen angeben.'; end if;
  insert into public.members (group_id, name, role) values (v_inv.group_id, v_name, v_inv.role) returning id into v_member;
  insert into public.member_users (user_id, member_id, group_id) values (v_uid, v_member, v_inv.group_id);
  if v_inv.single_use then update public.invites set used_at = now() where code = v_inv.code; end if;
  return v_inv.group_id;
end $$;

-- Ein weiteres Gerät mit einer vorhandenen Person verbinden (Gerätecode, 15 Minuten gültig, gilt einmal)
create function public.link_device(p_code text) returns uuid
language plpgsql security definer set search_path = '' as $$
declare
  v_uid uuid := auth.uid();
  v_code text := private.clean_code(p_code);
  v_dc public.device_codes;
  v_existing uuid;
begin
  if v_uid is null then raise exception 'Nicht angemeldet.'; end if;
  select * into v_dc from public.device_codes d
   where d.code = v_code and d.used_at is null and d.expires_at > now()
   for update;
  if v_dc.code is null then raise exception 'Dieser Gerätecode gilt nicht (mehr). Er ist nur 15 Minuten gültig.'; end if;
  if exists (select 1 from public.members m where m.id = v_dc.member_id and m.removed_at is not null) then
    raise exception 'Diese Person ist nicht mehr in der Gruppe.';
  end if;
  select mu.member_id into v_existing from public.member_users mu where mu.user_id = v_uid and mu.group_id = v_dc.group_id;
  if v_existing is not null and v_existing <> v_dc.member_id then
    raise exception 'Dieses Gerät ist in der Gruppe schon mit einem anderen Namen angemeldet.';
  end if;
  if v_existing is null then
    insert into public.member_users (user_id, member_id, group_id) values (v_uid, v_dc.member_id, v_dc.group_id);
  end if;
  update public.device_codes set used_at = now() where code = v_dc.code;
  return v_dc.group_id;
end $$;

-- Neue Gruppe gründen: Das darf, wer schon in einer Gruppe ist. Der Gründer wird Admin.
create function public.create_group(p_name text, p_my_name text) returns uuid
language plpgsql security definer set search_path = '' as $$
declare
  v_uid uuid := auth.uid();
  v_name text := btrim(coalesce(p_name, ''));
  v_my_name text := btrim(coalesce(p_my_name, ''));
  v_group uuid;
  v_member uuid;
begin
  if v_uid is null then raise exception 'Nicht angemeldet.'; end if;
  if not exists (select 1 from public.member_users mu join public.members m on m.id = mu.member_id
                 where mu.user_id = v_uid and m.removed_at is null) then
    raise exception 'Neue Gruppen kann nur gründen, wer schon in einer Gruppe ist.';
  end if;
  if char_length(v_name) not between 1 and 60 then raise exception 'Bitte einen Gruppennamen mit 1 bis 60 Zeichen angeben.'; end if;
  if v_my_name = '' then
    select m.name into v_my_name from public.member_users mu join public.members m on m.id = mu.member_id
     where mu.user_id = v_uid and m.removed_at is null order by m.joined_at limit 1;
  end if;
  insert into public.groups (name) values (v_name) returning id into v_group;
  insert into public.members (group_id, name, role) values (v_group, v_my_name, 'admin') returning id into v_member;
  insert into public.member_users (user_id, member_id, group_id) values (v_uid, v_member, v_group);
  return v_group;
end $$;

-- ---- Einladung und Geräte -------------------------------------------------------------------------------------------

-- Aktueller Einladungscode der Gruppe (legt einen an, falls es keinen gibt)
create function public.get_invite(p_group uuid) returns text
language plpgsql security definer set search_path = '' as $$
declare
  v_code text;
begin
  if not private.is_member(p_group) then raise exception 'Kein Zugriff.'; end if;
  select i.code into v_code from public.invites i
   where i.group_id = p_group and i.revoked_at is null and not i.single_use
   order by i.created_at desc limit 1;
  if v_code is null then
    v_code := private.new_code(10);
    insert into public.invites (code, group_id, created_by) values (v_code, p_group, private.my_member_id(p_group));
  end if;
  return v_code;
end $$;

-- Neuen Einladungscode erzeugen; alle bisherigen gelten ab sofort nicht mehr
create function public.rotate_invite(p_group uuid) returns text
language plpgsql security definer set search_path = '' as $$
declare
  v_me uuid := private.my_member_id(p_group);
  v_code text := private.new_code(10);
begin
  if v_me is null then raise exception 'Kein Zugriff.'; end if;
  update public.invites set revoked_at = now() where group_id = p_group and revoked_at is null;
  insert into public.invites (code, group_id, created_by) values (v_code, p_group, v_me);
  insert into public.changelog (group_id, member_id, entity, action) values (p_group, v_me, 'invite', 'update');
  return v_code;
end $$;

-- Gerätecode für eine Person der eigenen Gruppe (für sich selbst: zweites Gerät; für andere: neues Handy)
create function public.create_device_code(p_member uuid) returns text
language plpgsql security definer set search_path = '' as $$
declare
  v_target public.members;
  v_me uuid;
  v_code text := private.new_code(8);
begin
  select * into v_target from public.members m where m.id = p_member and m.removed_at is null;
  if v_target.id is null then raise exception 'Kein Zugriff.'; end if;
  v_me := private.my_member_id(v_target.group_id);
  if v_me is null then raise exception 'Kein Zugriff.'; end if;
  delete from public.device_codes where expires_at < now() - interval '1 day';   -- alte Codes wegräumen
  insert into public.device_codes (code, member_id, group_id, created_by, expires_at)
  values (v_code, v_target.id, v_target.group_id, v_me, now() + interval '15 minutes');
  if v_me <> v_target.id then                   -- für jemand anderen: gehört in den Verlauf
    insert into public.changelog (group_id, member_id, entity, entity_id, action, after)
    values (v_target.group_id, v_me, 'device', v_target.id, 'create', jsonb_build_object('name', v_target.name));
  end if;
  return v_code;
end $$;

-- ---- Mitglieder (Admin) --------------------------------------------------------------------------------------------

create function public.set_member_role(p_member uuid, p_role text) returns void
language plpgsql security definer set search_path = '' as $$
declare
  v_target public.members;
begin
  select * into v_target from public.members m where m.id = p_member and m.removed_at is null;
  if v_target.id is null or not private.is_admin(v_target.group_id) then raise exception 'Das darf nur ein Admin.'; end if;
  if p_role not in ('admin', 'member') then raise exception 'Unbekannte Rolle.'; end if;
  if v_target.role = 'admin' and p_role <> 'admin'
     and (select count(*) from public.members m where m.group_id = v_target.group_id and m.role = 'admin' and m.removed_at is null) <= 1 then
    raise exception 'Die Gruppe braucht mindestens einen Admin.';
  end if;
  update public.members set role = p_role where id = p_member;
end $$;

-- Jemanden entfernen (Admin) oder selbst austreten
create function public.remove_member(p_member uuid) returns void
language plpgsql security definer set search_path = '' as $$
declare
  v_target public.members;
  v_me uuid;
begin
  select * into v_target from public.members m where m.id = p_member and m.removed_at is null;
  if v_target.id is null then raise exception 'Kein Zugriff.'; end if;
  v_me := private.my_member_id(v_target.group_id);
  if v_me is null or (v_me <> v_target.id and not private.is_admin(v_target.group_id)) then
    raise exception 'Das darf nur ein Admin.';
  end if;
  if v_target.role = 'admin'
     and (select count(*) from public.members m where m.group_id = v_target.group_id and m.role = 'admin' and m.removed_at is null) <= 1
     and (select count(*) from public.members m where m.group_id = v_target.group_id and m.removed_at is null) > 1 then
    raise exception 'Mach zuerst jemand anderen zum Admin.';
  end if;
  update public.members set removed_at = now() where id = p_member;
end $$;

create function public.restore_member(p_member uuid) returns void
language plpgsql security definer set search_path = '' as $$
declare
  v_group uuid;
begin
  select m.group_id into v_group from public.members m where m.id = p_member;
  if v_group is null or not private.is_admin(v_group) then raise exception 'Das darf nur ein Admin.'; end if;
  update public.members set removed_at = null where id = p_member;
end $$;

-- ---- Zusagen und Reaktionen ------------------------------------------------------------------------------------------

-- Meine Zusage zu einem Treffen: yes, maybe, no – oder none (zurückgezogen)
create function public.set_rsvp(p_event uuid, p_status text) returns void
language plpgsql security definer set search_path = '' as $$
declare
  v_group uuid;
  v_me uuid;
begin
  select e.group_id into v_group from public.events e where e.id = p_event and e.deleted_at is null;
  v_me := private.my_member_id(v_group);
  if v_me is null then raise exception 'Kein Zugriff.'; end if;
  insert into public.rsvps (event_id, member_id, group_id, status) values (p_event, v_me, v_group, p_status)
  on conflict (event_id, member_id) do update set status = excluded.status;
end $$;

-- Reaktion an- oder abschalten
create function public.react(p_message uuid, p_emoji text, p_on boolean) returns void
language plpgsql security definer set search_path = '' as $$
declare
  v_group uuid;
  v_me uuid;
begin
  select m.group_id into v_group from public.messages m where m.id = p_message and m.deleted_at is null;
  v_me := private.my_member_id(v_group);
  if v_me is null then raise exception 'Kein Zugriff.'; end if;
  insert into public.reactions (message_id, member_id, group_id, emoji, active) values (p_message, v_me, v_group, p_emoji, coalesce(p_on, true))
  on conflict (message_id, member_id, emoji) do update set active = excluded.active;
end $$;

-- ---- Verlauf: rückgängig -------------------------------------------------------------------------------------------

-- Macht einen Eintrag aus dem Verlauf rückgängig. Bei "geändert" werden nur die Felder zurückgesetzt, die dieser
-- Eintrag geändert hat – spätere Änderungen an anderen Feldern bleiben.
create function public.undo_change(p_id bigint) returns void
language plpgsql security definer set search_path = '' as $$
declare
  c public.changelog;
  v_table text;
  v_delcol text := 'deleted_at';
  v_allowed text[];
  v_sets text;
begin
  select * into c from public.changelog l where l.id = p_id for update;
  if c.id is null or not private.is_member(c.group_id) then raise exception 'Kein Zugriff.'; end if;
  if c.undone_at is not null then raise exception 'Das wurde schon rückgängig gemacht.'; end if;

  if c.entity = 'event' then
    v_table := 'events';
    v_allowed := array['starts_at', 'title', 'place_id', 'place_name', 'place_address', 'place_lat', 'place_lon', 'place_kind', 'note', 'canceled'];
  elsif c.entity = 'place' then
    v_table := 'places';
    v_allowed := array['name', 'address', 'lat', 'lon', 'kind', 'note'];
  elsif c.entity = 'media' then
    v_table := 'media';
    v_allowed := array['event_id', 'taken_at'];
  elsif c.entity = 'group' then
    v_table := 'groups';
    v_allowed := array['name'];
  elsif c.entity = 'member' then
    v_table := 'members';
    v_delcol := 'removed_at';
    v_allowed := array['name', 'role'];
    if c.action = 'create' then
      raise exception 'Ein Beitritt lässt sich nicht rückgängig machen. Ein Admin kann die Person entfernen.';
    end if;
    if (c.action <> 'update' or c.before ->> 'role' is distinct from c.after ->> 'role') and not private.is_admin(c.group_id) then
      raise exception 'Das darf nur ein Admin rückgängig machen.';
    end if;
  else
    raise exception 'Das lässt sich nicht rückgängig machen.';
  end if;

  perform set_config('stammtisch.undo_of', c.id::text, true);   -- der Trigger merkt sich, was hier rückgängig gemacht wird
  if c.action in ('create', 'restore') then
    execute format('update public.%I set %I = now() where id = $1 and %I is null', v_table, v_delcol, v_delcol) using c.entity_id;
  elsif c.action = 'delete' then
    execute format('update public.%I set %I = null where id = $1', v_table, v_delcol) using c.entity_id;
  else
    select string_agg(format('%I = r.%I', e.key, e.key), ', ') into v_sets
    from jsonb_each(c.before) e
    where e.key = any (v_allowed) and e.value is distinct from c.after -> e.key;
    if v_sets is null then raise exception 'Hier gibt es nichts zurückzusetzen.'; end if;
    execute format('update public.%I t set %s from jsonb_populate_record(null::public.%I, $1) r where t.id = $2', v_table, v_sets, v_table)
      using c.before, c.entity_id;
  end if;
  perform set_config('stammtisch.undo_of', '', true);
  update public.changelog set undone_at = now() where id = c.id;
end $$;

-- ---- Papierkorb und Speicher ---------------------------------------------------------------------------------------

-- Bilder, Videos und Sprachnachrichten, die länger als 30 Tage im Papierkorb liegen. Die App löscht erst die Dateien
-- aus dem Speicher und ruft dann purge_trash.
create function public.trash_expired(p_group uuid) returns table (id uuid, path text, thumb_path text)
language sql stable security definer set search_path = '' as $$
  select m.id, m.path, m.thumb_path
  from public.media m
  where m.group_id = p_group and private.is_member(p_group) and m.deleted_at < now() - interval '30 days'
  order by m.deleted_at
  limit 200
$$;

-- Endgültig löschen, was länger als 30 Tage im Papierkorb liegt. Gibt zurück, wie viele Zeilen weg sind.
create function public.purge_trash(p_group uuid, p_media uuid[]) returns integer
language plpgsql security definer set search_path = '' as $$
declare
  v_n integer := 0;
  v_step integer;
begin
  if not private.is_member(p_group) then raise exception 'Kein Zugriff.'; end if;
  delete from public.media where group_id = p_group and id = any (coalesce(p_media, '{}')) and deleted_at < now() - interval '30 days';
  get diagnostics v_step = row_count; v_n := v_n + v_step;
  delete from public.messages where group_id = p_group and deleted_at < now() - interval '30 days';
  get diagnostics v_step = row_count; v_n := v_n + v_step;
  delete from public.events where group_id = p_group and deleted_at < now() - interval '30 days';
  get diagnostics v_step = row_count; v_n := v_n + v_step;
  delete from public.places where group_id = p_group and deleted_at < now() - interval '30 days';
  get diagnostics v_step = row_count; v_n := v_n + v_step;
  return v_n;
end $$;

-- Belegter Speicher: diese Gruppe und alle Gruppen zusammen (das kostenlose Kontingent gilt für alle gemeinsam)
create function public.storage_usage(p_group uuid) returns jsonb
language plpgsql stable security definer set search_path = '' as $$
declare
  v_group bigint;
  v_total bigint;
begin
  if not private.is_member(p_group) then raise exception 'Kein Zugriff.'; end if;
  select coalesce(sum((o.metadata ->> 'size')::bigint) filter (where private.path_group(o.name) = p_group), 0),
         coalesce(sum((o.metadata ->> 'size')::bigint), 0)
    into v_group, v_total
  from storage.objects o where o.bucket_id = 'media';
  return jsonb_build_object('group_bytes', v_group, 'total_bytes', v_total);
end $$;

-- Darf diese Datei aus dem Speicher gelöscht werden? (für die Speicher-Regeln in Teil 3)
create function private.may_delete_file(p_name text, p_owner text) returns boolean
language sql stable security definer set search_path = '' as $$
  select case
    -- gehört zu einem Bild/Video/einer Sprachnachricht: erst nach 30 Tagen im Papierkorb
    when exists (select 1 from public.media m where m.path = p_name or m.thumb_path = p_name)
      then exists (select 1 from public.media m
                   where (m.path = p_name or m.thumb_path = p_name) and m.deleted_at < now() - interval '30 days')
    -- Profilbild (<gruppe>/avatars/<mitglied>/…): nur das eigene
    when split_part(p_name, '/', 2) = 'avatars'
      then split_part(p_name, '/', 3) = private.my_member_id(private.path_group(p_name))::text
    -- sonst: Datei ohne Eintrag (abgebrochener Upload) – nur wer sie hochgeladen hat
    else p_owner = (select auth.uid())::text
  end
$$;

-- ---- Rechte ---------------------------------------------------------------------------------------------------------

revoke all on all functions in schema public from public, anon, authenticated;
revoke all on all functions in schema private from public, anon, authenticated;

grant execute on function private.my_member_id(uuid), private.is_member(uuid), private.is_admin(uuid), private.path_group(text),
  private.may_delete_file(text, text) to authenticated;

grant execute on function public.ping() to anon, authenticated;
grant execute on function public.code_valid(text) to service_role;
grant execute on function public.my_groups(), public.join_group(text, text), public.link_device(text), public.create_group(text, text),
  public.get_invite(uuid), public.rotate_invite(uuid), public.create_device_code(uuid),
  public.set_member_role(uuid, text), public.remove_member(uuid), public.restore_member(uuid),
  public.set_rsvp(uuid, text), public.react(uuid, text, boolean), public.undo_change(bigint),
  public.trash_expired(uuid), public.purge_trash(uuid, uuid[]), public.storage_usage(uuid) to authenticated;

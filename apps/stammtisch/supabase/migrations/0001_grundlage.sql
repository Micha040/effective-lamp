-- STAMMTISCH – Datenbank (PostgreSQL bei Supabase), Teil 1: Tabellen, Zugriffsregeln, Verlauf
--
-- Grundsatz: Jede Zeile gehört zu genau einer Gruppe (group_id). Lesen und schreiben darf nur, wer Mitglied dieser
-- Gruppe ist. Das prüft die Datenbank selbst (Row Level Security) – nicht die App. Der Quelltext der App ist
-- öffentlich, die Inhalte sind es nicht.
--
-- Begriffe: Ein "user" ist eine Anmeldung (ein Gerät, auth.users). Ein "member" ist eine Person in einer Gruppe.
-- member_users verbindet beides: Eine Person kann mehrere Geräte haben und in mehreren Gruppen sein.

create schema if not exists private;   -- Hilfsfunktionen, die die Schnittstelle nicht nach außen zeigt

-- ---- Tabellen -------------------------------------------------------------------------------------------------------

create table public.groups (
  id uuid primary key default gen_random_uuid(),
  name text not null check (char_length(btrim(name)) between 1 and 60),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.members (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null references public.groups (id) on delete cascade,
  name text not null check (char_length(btrim(name)) between 1 and 40),
  avatar_path text,
  role text not null default 'member' check (role in ('admin', 'member')),
  joined_at timestamptz not null default now(),
  removed_at timestamptz                      -- entfernt: bleibt für alte Nachrichten und Bilder sichtbar
);
create index members_group on public.members (group_id);

create table public.member_users (
  user_id uuid not null references auth.users (id) on delete cascade,
  member_id uuid not null references public.members (id) on delete cascade,
  group_id uuid not null references public.groups (id) on delete cascade,
  created_at timestamptz not null default now(),
  primary key (user_id, group_id)             -- je Gerät höchstens eine Person pro Gruppe
);
create index member_users_member on public.member_users (member_id);

create table public.invites (
  code text primary key,
  group_id uuid not null references public.groups (id) on delete cascade,
  role text not null default 'member' check (role in ('admin', 'member')),
  single_use boolean not null default false,  -- gilt nur einmal (z. B. der Gründer-Link)
  created_by uuid references public.members (id) on delete set null,
  created_at timestamptz not null default now(),
  used_at timestamptz,
  revoked_at timestamptz
);
create index invites_group on public.invites (group_id);

create table public.device_codes (
  code text primary key,
  member_id uuid not null references public.members (id) on delete cascade,
  group_id uuid not null references public.groups (id) on delete cascade,
  created_by uuid references public.members (id) on delete set null,
  expires_at timestamptz not null,
  used_at timestamptz
);

create table public.places (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null references public.groups (id) on delete cascade,
  name text not null check (char_length(btrim(name)) between 1 and 80),
  address text check (char_length(address) <= 200),
  lat double precision check (lat between -90 and 90),
  lon double precision check (lon between -180 and 180),
  kind text not null default 'bar' check (kind in ('bar', 'home', 'other')),
  note text check (char_length(note) <= 500),
  created_by uuid references public.members (id) on delete set null,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  deleted_at timestamptz
);
create index places_group on public.places (group_id);

-- Ein Treffen trägt seinen Ort selbst (Kopie): Ändert jemand später einen gespeicherten Ort, bleibt das Archiv richtig.
create table public.events (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null references public.groups (id) on delete cascade,
  starts_at timestamptz not null,
  title text check (char_length(title) <= 80),
  place_id uuid references public.places (id) on delete set null,
  place_name text check (char_length(place_name) <= 80),
  place_address text check (char_length(place_address) <= 200),
  place_lat double precision check (place_lat between -90 and 90),
  place_lon double precision check (place_lon between -180 and 180),
  place_kind text check (place_kind in ('bar', 'home', 'other')),
  note text check (char_length(note) <= 1000),
  canceled boolean not null default false,    -- fällt aus (bleibt sichtbar, durchgestrichen)
  created_by uuid references public.members (id) on delete set null,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  deleted_at timestamptz
);
create index events_group_start on public.events (group_id, starts_at);

create table public.rsvps (
  event_id uuid not null references public.events (id) on delete cascade,
  member_id uuid not null references public.members (id) on delete cascade,
  group_id uuid not null references public.groups (id) on delete cascade,
  status text not null check (status in ('yes', 'maybe', 'no', 'none')),   -- none = zurückgezogen
  updated_at timestamptz not null default now(),
  primary key (event_id, member_id)
);
create index rsvps_group on public.rsvps (group_id);

-- Bilder, Videos und Sprachnachrichten. Die Dateien liegen im Speicher (Bucket "media"), hier steht, was wozu gehört.
create table public.media (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null references public.groups (id) on delete cascade,
  event_id uuid references public.events (id) on delete set null,
  member_id uuid references public.members (id) on delete set null,
  scope text not null default 'album' check (scope in ('album', 'chat')),
  kind text not null check (kind in ('image', 'video', 'audio')),
  path text not null unique,
  thumb_path text,
  mime text,
  width integer,
  height integer,
  duration real,
  size_bytes bigint not null default 0 check (size_bytes >= 0),
  taken_at timestamptz,
  created_at timestamptz not null default now(),
  deleted_at timestamptz
);
create index media_group_event on public.media (group_id, event_id);

create table public.messages (
  id uuid primary key default gen_random_uuid(),
  group_id uuid not null references public.groups (id) on delete cascade,
  member_id uuid not null references public.members (id) on delete cascade,
  body text not null default '' check (char_length(body) <= 4000),
  media_id uuid references public.media (id) on delete set null,
  reply_to uuid references public.messages (id) on delete set null,
  created_at timestamptz not null default now(),
  edited_at timestamptz,
  deleted_at timestamptz
);
create index messages_group_time on public.messages (group_id, created_at desc);

-- Reaktionen werden nie gelöscht, nur abgeschaltet (active = false): So kommen alle Änderungen als "geändert" bei den
-- anderen an und unterliegen den Leseregeln (gelöschte Zeilen tun das bei Supabase Realtime nicht).
create table public.reactions (
  message_id uuid not null references public.messages (id) on delete cascade,
  member_id uuid not null references public.members (id) on delete cascade,
  group_id uuid not null references public.groups (id) on delete cascade,
  emoji text not null check (char_length(emoji) between 1 and 16),
  active boolean not null default true,
  updated_at timestamptz not null default now(),
  primary key (message_id, member_id, emoji)
);
create index reactions_group on public.reactions (group_id);

-- Verlauf: wer hat wann was geändert (vorher → nachher). Schreibt nur die Datenbank selbst (Trigger).
create table public.changelog (
  id bigint generated always as identity primary key,
  group_id uuid not null references public.groups (id) on delete cascade,
  member_id uuid references public.members (id) on delete set null,
  at timestamptz not null default now(),
  entity text not null,                       -- event, place, media, member, group, invite, device
  entity_id uuid,
  action text not null check (action in ('create', 'update', 'delete', 'restore')),
  before jsonb,
  after jsonb,
  undo_of bigint,                             -- diese Änderung hat Eintrag … rückgängig gemacht
  undone_at timestamptz                       -- wurde selbst rückgängig gemacht
);
create index changelog_group on public.changelog (group_id, id desc);

-- ---- Hilfsfunktionen (private) -----------------------------------------------------------------------------------------
-- "security definer": läuft mit den Rechten des Besitzers und darf deshalb member_users lesen, obwohl die App das nicht darf.

create function private.my_member_id(g uuid) returns uuid
language sql stable security definer set search_path = '' as $$
  select mu.member_id
  from public.member_users mu
  join public.members m on m.id = mu.member_id
  where mu.user_id = (select auth.uid()) and mu.group_id = g and m.removed_at is null
$$;

create function private.is_member(g uuid) returns boolean
language sql stable security definer set search_path = '' as $$
  select private.my_member_id(g) is not null
$$;

create function private.is_admin(g uuid) returns boolean
language sql stable security definer set search_path = '' as $$
  select exists (select 1 from public.members m where m.id = private.my_member_id(g) and m.role = 'admin')
$$;

-- Zufallscode aus 32 gut lesbaren Zeichen (ohne 0/O und 1/I). 10 Zeichen = 50 Bit.
create function private.new_code(n integer) returns text
language plpgsql volatile set search_path = '' as $$
declare
  alphabet constant text := 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  raw bytea;
  result text := '';
begin
  while char_length(result) < n loop
    raw := decode(replace(gen_random_uuid()::text, '-', ''), 'hex');
    for i in 0..15 loop
      continue when i in (6, 8);              -- diese Bytes einer UUID tragen feste Bits (Version, Variante)
      exit when char_length(result) >= n;
      result := result || substr(alphabet, (get_byte(raw, i) % 32) + 1, 1);
    end loop;
  end loop;
  return result;
end $$;

-- Eingabe aufräumen: Großbuchstaben, Bindestriche und Leerzeichen weg ("abcd-efgh" → "ABCDEFGH")
create function private.clean_code(c text) returns text
language sql immutable set search_path = '' as $$
  select regexp_replace(upper(coalesce(c, '')), '[^A-Z0-9]', '', 'g')
$$;

-- Erster Ordner eines Dateipfads = Gruppe ("<gruppe>/album/2026-11/<id>.jpg"); null, wenn das keine UUID ist
create function private.path_group(name text) returns uuid
language sql immutable set search_path = '' as $$
  select case when split_part(name, '/', 1) ~ '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
              then split_part(name, '/', 1)::uuid end
$$;

-- ---- Trigger: Zeitstempel, Urheber, Zusammengehörigkeit -------------------------------------------------------------

create function private.touch() returns trigger
language plpgsql set search_path = '' as $$
begin
  new.updated_at := now();
  return new;
end $$;

-- Wer anlegt, steht fest: Die App kann niemand anderen eintragen. (Ohne Anmeldung – Einrichtung, Tests – bleibt der
-- übergebene Wert.)
create function private.stamp_creator() returns trigger
language plpgsql security definer set search_path = '' as $$
declare
  me uuid := private.my_member_id(new.group_id);
begin
  if tg_argv[0] = 'created_by' then
    new.created_by := coalesce(me, new.created_by);
  else
    new.member_id := coalesce(me, new.member_id);
  end if;
  return new;
end $$;

create function private.check_event() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  if new.place_id is not null and not exists (select 1 from public.places p where p.id = new.place_id and p.group_id = new.group_id) then
    raise exception 'Der Ort gehört nicht zu dieser Gruppe.';
  end if;
  return new;
end $$;

create function private.check_media() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  if private.path_group(new.path) is distinct from new.group_id
     or (new.thumb_path is not null and private.path_group(new.thumb_path) is distinct from new.group_id) then
    raise exception 'Der Dateipfad gehört nicht zu dieser Gruppe.';
  end if;
  if new.event_id is not null and not exists (select 1 from public.events e where e.id = new.event_id and e.group_id = new.group_id) then
    raise exception 'Das Treffen gehört nicht zu dieser Gruppe.';
  end if;
  return new;
end $$;

create function private.check_message() returns trigger
language plpgsql security definer set search_path = '' as $$
declare
  me uuid := private.my_member_id(new.group_id);
begin
  if tg_op = 'INSERT' then
    if new.reply_to is not null and not exists (select 1 from public.messages m where m.id = new.reply_to and m.group_id = new.group_id) then
      raise exception 'Die beantwortete Nachricht gehört nicht zu dieser Gruppe.';
    end if;
    if new.media_id is not null and not exists (select 1 from public.media m where m.id = new.media_id and m.group_id = new.group_id) then
      raise exception 'Der Anhang gehört nicht zu dieser Gruppe.';
    end if;
    if btrim(new.body) = '' and new.media_id is null then
      raise exception 'Leere Nachricht.';
    end if;
    return new;
  end if;
  -- Ändern: nur die eigene Nachricht. Fremde darf nur ein Admin löschen (nicht umschreiben).
  if me is not null and me <> old.member_id then
    if not private.is_admin(old.group_id) or new.body is distinct from old.body then
      raise exception 'Das ist nicht deine Nachricht.';
    end if;
  end if;
  if new.body is distinct from old.body then
    new.edited_at := now();
  end if;
  return new;
end $$;

-- Gelöschte Nachricht: der Anhang (Bild, Sprachnachricht) wandert mit in den Papierkorb
create function private.message_deleted() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  if old.deleted_at is null and new.deleted_at is not null and new.media_id is not null then
    update public.media set deleted_at = new.deleted_at where id = new.media_id and scope = 'chat' and deleted_at is null;
  end if;
  return null;
end $$;

-- Zusagen und Reaktionen: die Gruppe ergibt sich aus Treffen bzw. Nachricht, nicht aus dem, was die App schickt
create function private.fill_rsvp() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  select e.group_id into new.group_id from public.events e where e.id = new.event_id;
  new.updated_at := now();
  return new;
end $$;

create function private.fill_reaction() returns trigger
language plpgsql security definer set search_path = '' as $$
begin
  select m.group_id into new.group_id from public.messages m where m.id = new.message_id;
  new.updated_at := now();
  return new;
end $$;

-- ---- Trigger: Verlauf ----------------------------------------------------------------------------------------------

create function private.log_change() returns trigger
language plpgsql security definer set search_path = '' as $$
declare
  v_entity text := tg_argv[0];
  v_old jsonb := case when tg_op = 'INSERT' then null else to_jsonb(old) end;
  v_new jsonb := to_jsonb(new);
  v_delkey text := case when tg_argv[0] = 'member' then 'removed_at' else 'deleted_at' end;
  v_group uuid;
  v_actor uuid;
  v_action text;
begin
  v_group := case when v_entity = 'group' then (v_new ->> 'id')::uuid else (v_new ->> 'group_id')::uuid end;
  v_actor := private.my_member_id(v_group);
  if tg_op = 'INSERT' then
    v_action := 'create';
    if v_entity = 'member' then v_actor := (v_new ->> 'id')::uuid; end if;   -- beigetreten: das war die Person selbst
  else
    -- nichts Sichtbares geändert (nur Zeitstempel, Profilbild)? Dann kein Eintrag.
    if (v_old - 'updated_at' - 'avatar_path') = (v_new - 'updated_at' - 'avatar_path') then return new; end if;
    if v_old ->> v_delkey is null and v_new ->> v_delkey is not null then v_action := 'delete';
    elsif v_old ->> v_delkey is not null and v_new ->> v_delkey is null then v_action := 'restore';
    else v_action := 'update';
    end if;
  end if;
  insert into public.changelog (group_id, member_id, entity, entity_id, action, before, after, undo_of)
  values (v_group, v_actor, v_entity, (v_new ->> 'id')::uuid, v_action, v_old, v_new,
          nullif(current_setting('stammtisch.undo_of', true), '')::bigint);
  return new;
end $$;

create trigger groups_touch before update on public.groups for each row execute function private.touch();
create trigger places_touch before update on public.places for each row execute function private.touch();
create trigger events_touch before update on public.events for each row execute function private.touch();

create trigger places_creator before insert on public.places for each row execute function private.stamp_creator('created_by');
create trigger events_creator before insert on public.events for each row execute function private.stamp_creator('created_by');
create trigger media_creator before insert on public.media for each row execute function private.stamp_creator('member_id');
create trigger messages_creator before insert on public.messages for each row execute function private.stamp_creator('member_id');

create trigger events_check before insert or update on public.events for each row execute function private.check_event();
create trigger media_check before insert or update on public.media for each row execute function private.check_media();
create trigger messages_check before insert or update on public.messages for each row execute function private.check_message();
create trigger messages_deleted after update on public.messages for each row execute function private.message_deleted();
create trigger rsvps_fill before insert or update on public.rsvps for each row execute function private.fill_rsvp();
create trigger reactions_fill before insert or update on public.reactions for each row execute function private.fill_reaction();

create trigger groups_log after update on public.groups for each row execute function private.log_change('group');
create trigger members_log after insert or update on public.members for each row execute function private.log_change('member');
create trigger places_log after insert or update on public.places for each row execute function private.log_change('place');
create trigger events_log after insert or update on public.events for each row execute function private.log_change('event');
create trigger media_log_insert after insert on public.media for each row when (new.scope = 'album') execute function private.log_change('media');
create trigger media_log_update after update on public.media for each row when (new.scope = 'album') execute function private.log_change('media');

-- ---- Rechte: was die App (angemeldet) überhaupt anfassen darf ------------------------------------------------------
-- Erst alles wegnehmen, dann gezielt erlauben – bis auf einzelne Spalten. "anon" (nicht angemeldet) bekommt nichts.

revoke all on all tables in schema public from anon, authenticated;
revoke all on all functions in schema public from public, anon, authenticated;
revoke all on all functions in schema private from public, anon;

grant usage on schema private to authenticated;
grant execute on function private.my_member_id(uuid), private.is_member(uuid), private.is_admin(uuid), private.path_group(text) to authenticated;

grant select on public.groups, public.members, public.invites, public.places, public.events, public.rsvps, public.media,
  public.messages, public.reactions, public.changelog to authenticated;

grant update (name) on public.groups to authenticated;
grant update (name, avatar_path) on public.members to authenticated;

grant insert (id, group_id, name, address, lat, lon, kind, note) on public.places to authenticated;
grant update (name, address, lat, lon, kind, note, deleted_at) on public.places to authenticated;

grant insert (id, group_id, starts_at, title, place_id, place_name, place_address, place_lat, place_lon, place_kind, note, canceled)
  on public.events to authenticated;
grant update (starts_at, title, place_id, place_name, place_address, place_lat, place_lon, place_kind, note, canceled, deleted_at)
  on public.events to authenticated;

grant insert (id, group_id, event_id, scope, kind, path, thumb_path, mime, width, height, duration, size_bytes, taken_at)
  on public.media to authenticated;
grant update (event_id, taken_at, deleted_at) on public.media to authenticated;

grant insert (id, group_id, body, media_id, reply_to) on public.messages to authenticated;
grant update (body, deleted_at) on public.messages to authenticated;

-- ---- Zugriffsregeln (Row Level Security) ---------------------------------------------------------------------------

alter table public.groups enable row level security;
alter table public.members enable row level security;
alter table public.member_users enable row level security;   -- keine Regel: nur über Funktionen erreichbar
alter table public.invites enable row level security;
alter table public.device_codes enable row level security;   -- keine Regel: nur über Funktionen erreichbar
alter table public.places enable row level security;
alter table public.events enable row level security;
alter table public.rsvps enable row level security;
alter table public.media enable row level security;
alter table public.messages enable row level security;
alter table public.reactions enable row level security;
alter table public.changelog enable row level security;

create policy groups_select on public.groups for select to authenticated using (private.is_member(id));
create policy groups_update on public.groups for update to authenticated
  using (private.is_member(id)) with check (private.is_member(id));

create policy members_select on public.members for select to authenticated using (private.is_member(group_id));
create policy members_update_self on public.members for update to authenticated
  using (id = private.my_member_id(group_id)) with check (id = private.my_member_id(group_id));

create policy invites_select on public.invites for select to authenticated using (private.is_member(group_id));

create policy places_select on public.places for select to authenticated using (private.is_member(group_id));
create policy places_insert on public.places for insert to authenticated with check (private.is_member(group_id));
create policy places_update on public.places for update to authenticated
  using (private.is_member(group_id)) with check (private.is_member(group_id));

create policy events_select on public.events for select to authenticated using (private.is_member(group_id));
create policy events_insert on public.events for insert to authenticated with check (private.is_member(group_id));
create policy events_update on public.events for update to authenticated
  using (private.is_member(group_id)) with check (private.is_member(group_id));

-- Zusagen und Reaktionen: sehen dürfen alle in der Gruppe; eingetragen wird über set_rsvp() und react() (nur die eigene)
create policy rsvps_select on public.rsvps for select to authenticated using (private.is_member(group_id));

create policy media_select on public.media for select to authenticated using (private.is_member(group_id));
create policy media_insert on public.media for insert to authenticated with check (private.is_member(group_id));
create policy media_update on public.media for update to authenticated
  using (private.is_member(group_id)) with check (private.is_member(group_id));

create policy messages_select on public.messages for select to authenticated using (private.is_member(group_id));
create policy messages_insert on public.messages for insert to authenticated with check (member_id = private.my_member_id(group_id));
create policy messages_update on public.messages for update to authenticated
  using (private.is_member(group_id)) with check (private.is_member(group_id));   -- wer was darf: Trigger messages_check

create policy reactions_select on public.reactions for select to authenticated using (private.is_member(group_id));

create policy changelog_select on public.changelog for select to authenticated using (private.is_member(group_id));

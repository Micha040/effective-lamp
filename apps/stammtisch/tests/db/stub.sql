-- Nachbau der Supabase-Umgebung für die Datenbank-Tests: Rollen, auth.uid(), Speicher-Tabellen, Live-Veröffentlichung.
-- Nur so viel, wie die Migrationen in supabase/migrations/ brauchen.
create role anon nologin;
create role authenticated nologin;
create role service_role nologin bypassrls;

create schema auth;
create table auth.users (id uuid primary key default gen_random_uuid(), email text);
-- wie bei Supabase: die Anmeldung steht in den Einstellungen der Anfrage
create function auth.uid() returns uuid language sql stable as $$
  select nullif(current_setting('request.jwt.claim.sub', true), '')::uuid
$$;

create schema storage;
create table storage.buckets (id text primary key, name text, public boolean, file_size_limit bigint, allowed_mime_types text[]);
create table storage.objects (
  id uuid primary key default gen_random_uuid(), bucket_id text references storage.buckets (id), name text, owner_id text,
  metadata jsonb, created_at timestamptz default now());
alter table storage.objects enable row level security;

grant usage on schema public, auth, storage to anon, authenticated, service_role;
grant execute on function auth.uid() to anon, authenticated, service_role;
grant select, insert, update, delete on storage.objects to authenticated, service_role;
grant select on storage.buckets to authenticated, service_role;
-- wie bei Supabase: neue Tabellen und Funktionen in public bekommen erst einmal alle Rechte (die Migrationen nehmen sie weg)
alter default privileges in schema public grant all on tables to anon, authenticated, service_role;
alter default privileges in schema public grant all on functions to anon, authenticated, service_role;

create publication supabase_realtime;

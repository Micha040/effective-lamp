#!/bin/sh
# Datenbank-Tests: startet einen frischen PostgreSQL nur für diesen Lauf (eigener Ordner, eigener Socket, kein Netz),
# spielt die Migrationen ein und prüft die Zugriffsregeln mit tests/db/test.sql.
# Aufruf: sh tests/db.sh            (braucht PostgreSQL ≥ 15: initdb, pg_ctl, psql)
set -e
cd "$(dirname "$0")/.."

PGBIN=""
for d in /usr/lib/postgresql/*/bin /usr/local/opt/postgresql*/bin /opt/homebrew/opt/postgresql*/bin; do
  [ -x "$d/initdb" ] && PGBIN="$d"
done
if [ -z "$PGBIN" ] && command -v initdb >/dev/null 2>&1; then PGBIN="$(dirname "$(command -v initdb)")"; fi
if [ -z "$PGBIN" ]; then
  echo "Hinweis: PostgreSQL fehlt (initdb) – Datenbank-Tests übersprungen"
  [ -n "$GITHUB_ACTIONS" ] && echo "::warning::STAMMTISCH: PostgreSQL fehlt auf dem Runner – Datenbank-Tests übersprungen"
  exit 0
fi

WORK="$(mktemp -d)"
chmod 755 "$WORK"
# PostgreSQL weigert sich, als root zu laufen: dann als Benutzer "postgres" (z. B. im Container)
RUN=""
if [ "$(id -u)" = 0 ]; then
  if id postgres >/dev/null 2>&1 && command -v runuser >/dev/null 2>&1; then
    chown postgres "$WORK"
    RUN="runuser -u postgres --"
  else
    echo "Hinweis: als root ohne Benutzer postgres – Datenbank-Tests übersprungen"; exit 0
  fi
fi

cleanup() {
  $RUN "$PGBIN/pg_ctl" -D "$WORK/data" -m immediate stop >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT

$RUN "$PGBIN/initdb" -D "$WORK/data" -U postgres -A trust --locale=C -E UTF8 >/dev/null
$RUN "$PGBIN/pg_ctl" -D "$WORK/data" -o "-c listen_addresses='' -c wal_level=logical -k $WORK" -l "$WORK/log" -w start >/dev/null

PSQL="$PGBIN/psql -X -q -v ON_ERROR_STOP=1 -h $WORK -U postgres -d postgres"
run_sql() { $RUN $PSQL -f - < "$1"; }

run_sql tests/db/stub.sql
for f in supabase/migrations/*.sql; do
  echo "  Migration $(basename "$f")"
  run_sql "$f"
done
run_sql tests/db/test.sql
echo "Datenbank-Tests bestanden"

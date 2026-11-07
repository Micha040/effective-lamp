#!/bin/sh
# Test für server/install.sh ohne echtes System: alles läuft in einem Schein-Wurzelordner (FRAGDESK_ROOT), apt, systemd,
# ufw, ip und curl sind Attrappen, die ihre Aufrufe mitschreiben. Die Attrappe von systemctl startet den installierten
# fragdesk-server wirklich (auf einem freien Port) – Gesundheitsprüfung und erste Einladung laufen also echt.
# Mit CADDY_BIN=<caddy> prüft der echte Caddy die erzeugte Konfiguration (wie „caddy validate“ im Skript).
# Aufruf: sh tests/install_test.sh <leerer Ordner>   (braucht build/fragdesk-server-linux-x64, siehe tests/server.sh)
set -e
cd "$(dirname "$0")/.."
T=${1:?Ordner fehlt}
APP=$PWD
INSTALL=$APP/server/install.sh
SERVER=$APP/build/fragdesk-server-linux-x64
[ -x "$SERVER" ] || { echo "FEHLER: erst sh server/build.sh"; exit 1; }
rm -rf "$T"
mkdir -p "$T/bin" "$T/release" "$T/state"
CHECKS=0
FAILS=0
check() {   # check <Beschreibung> <Befehl …>
  what=$1
  shift
  CHECKS=$((CHECKS + 1))
  if ! "$@" >/dev/null 2>&1; then
    FAILS=$((FAILS + 1))
    echo "FEHLER: $what"
  fi
}
has() { grep -qF -- "$2" "$1"; }      # Datei enthält Text
hasnt() { ! grep -qF -- "$2" "$1"; }

PORT=${FD_TEST_PORT:-$(python3 -c '
import socket
for p in range(7459, 7450, -1):
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", p)); print(p); break
    except OSError: pass
    finally: s.close()
')}
[ -n "$PORT" ] || { echo "FEHLER: kein freier Port"; exit 1; }

# ---- Attrappen ----------------------------------------------------------------------------------------------------
export CALLS="$T/calls.log" STATE="$T/state" SHIMS="$T/bin" FAKE_RELEASE="$T/release" REAL_CURL REAL_ID SERVER_LOG="$T/server.log"
REAL_CURL=$(command -v curl)
REAL_ID=$(command -v id)
cat >"$T/bin/apt-get" <<'EOF'
#!/bin/sh
echo "apt-get $*" >>"$CALLS"
case " $* " in *" install "*" caddy"*) cp "$SHIMS/caddy.paket" "$SHIMS/caddy"; chmod +x "$SHIMS/caddy" ;; esac
exit 0
EOF
cat >"$T/bin/apt-cache" <<'EOF'
#!/bin/sh
if [ -n "$FAKE_NO_CADDY" ]; then printf 'caddy:\n  Installed: (none)\n  Candidate: (none)\n'; else printf 'caddy:\n  Installed: (none)\n  Candidate: 2.6.2-5\n'; fi
EOF
cat >"$T/bin/caddy.paket" <<'EOF'
#!/bin/sh
echo "caddy $*" >>"$CALLS"
[ "$1" = validate ] || exit 0
[ -n "$CADDY_BIN" ] || exit 0
# Pfade des Schein-Wurzelordners einsetzen und mit dem echten Caddy prüfen
d=$(mktemp -d)
sed "s#/etc/caddy/#$FRAGDESK_ROOT/etc/caddy/#g" "$3" >"$d/Caddyfile"
XDG_DATA_HOME=$d XDG_CONFIG_HOME=$d HOME=$d "$CADDY_BIN" validate --config "$d/Caddyfile" --adapter caddyfile >"$STATE/caddy-validate.log" 2>&1
rc=$?
rm -rf "$d"
exit $rc
EOF
cat >"$T/bin/useradd" <<'EOF'
#!/bin/sh
echo "useradd $*" >>"$CALLS"
touch "$STATE/user-fragdesk"
EOF
cat >"$T/bin/id" <<'EOF'
#!/bin/sh
if [ "$1" = fragdesk ]; then [ -f "$STATE/user-fragdesk" ]; exit; fi
exec "$REAL_ID" "$@"
EOF
cat >"$T/bin/chown" <<'EOF'
#!/bin/sh
echo "chown $*" >>"$CALLS"
EOF
cat >"$T/bin/ufw" <<'EOF'
#!/bin/sh
if [ "$1" = status ]; then echo "Status: active"; exit 0; fi
echo "ufw $*" >>"$CALLS"
EOF
cat >"$T/bin/ip" <<'EOF'
#!/bin/sh
printf '1.1.1.1 via 192.0.2.1 dev eth0 src 203.0.113.5 uid 0\n    cache\n'
EOF
cat >"$T/bin/gpg" <<'EOF'
#!/bin/sh
echo "gpg $*" >>"$CALLS"
while [ $# -gt 0 ]; do [ "$1" = -o ] && { cat >"$2"; exit 0; }; shift; done
EOF
cat >"$T/bin/journalctl" <<'EOF'
#!/bin/sh
tail -n 20 "$SERVER_LOG" 2>/dev/null
EOF
cat >"$T/bin/curl" <<'EOF'
#!/bin/sh
# 127.0.0.1: echt; Release-Dateien aus $FAKE_RELEASE; https://<domain>: Caddy läuft (FAKE_HTTPS=0: läuft nicht)
url="" out="" prev=""
for a in "$@"; do
  [ "$prev" = -o ] && out=$a
  case "$a" in http://* | https://*) url=$a ;; esac
  prev=$a
done
case "$url" in
  http://127.0.0.1:*) exec "$REAL_CURL" "$@" ;;
esac
echo "curl $url" >>"$CALLS"
case "$url" in
  https://github.com/*/releases/*)
    f=$FAKE_RELEASE/${url##*/}
    [ -f "$f" ] || exit 22
    if [ -n "$out" ]; then cp "$f" "$out"; else cat "$f"; fi ;;
  https://dl.cloudsmith.io/*)
    if [ -n "$out" ]; then echo "deb [signed-by=/usr/share/keyrings/caddy-stable-archive-keyring.gpg] https://dl.cloudsmith.io/public/caddy/stable/deb/debian any-version main" >"$out"
    else echo "SCHLÜSSEL"; fi ;;
  https://api.ipify.org*) echo 203.0.113.5 ;;
  https://*) [ "${FAKE_HTTPS:-1}" = 1 ] || exit 7; echo '{"ok":true}' ;;
  *) exit 6 ;;
esac
EOF
cat >"$T/bin/systemctl" <<'EOF'
#!/bin/sh
# startet den installierten fragdesk-server wirklich (im Hintergrund), alles andere wird nur mitgeschrieben
echo "systemctl $*" >>"$CALLS"
pidf=$STATE/server.pid
alive() { [ -f "$pidf" ] && kill -0 "$(cat "$pidf")" 2>/dev/null; }
stop() {
  if alive; then kill "$(cat "$pidf")"; for _ in 1 2 3 4 5 6 7 8 9 10; do alive || break; sleep 0.2; done; fi
  rm -f "$pidf"
}
case "$1 $2" in
  "is-active --quiet") [ "$3" = fragdesk-server ] || exit 0; alive; exit ;;
  "restart fragdesk-server" | "start fragdesk-server")
    stop
    url=$(sed -n 's/^FRAGDESK_URL=//p' "$FRAGDESK_ROOT/etc/default/fragdesk")
    TZ=$(sed -n 's/^TZ=//p' "$FRAGDESK_ROOT/etc/default/fragdesk") setsid "$FRAGDESK_ROOT/usr/local/bin/fragdesk-server" serve \
      --data "$FRAGDESK_ROOT/var/lib/fragdesk" --listen "127.0.0.1:$FRAGDESK_PORT" --url "$url" --min-free 1 >>"$SERVER_LOG" 2>&1 &
    echo $! >"$pidf" ;;
  "stop fragdesk-server" | "disable --now") stop ;;
esac
exit 0
EOF
chmod +x "$T"/bin/*
rm -f "$T/bin/caddy"   # kommt erst mit „apt-get install caddy“
PATH="$T/bin:$PATH"
export PATH FRAGDESK_PORT="$PORT"

# Release wie auf GitHub: Programm + SHA256SUMS.txt
cp "$SERVER" "$T/release/fragdesk-server-linux-x64"
(cd "$T/release" && sha256sum fragdesk-server-linux-x64 >SHA256SUMS.txt && echo "0000  EfficientLamp.exe" >>SHA256SUMS.txt)

newroot() {   # newroot <Ordner>: frisches Debian 12 mit Caddy-Standarddatei
  rm -rf "$1"
  mkdir -p "$1/etc/caddy" "$1/etc/apt/sources.list.d" "$1/usr/share/keyrings"
  printf 'PRETTY_NAME="Debian GNU/Linux 12 (bookworm)"\nNAME="Debian GNU/Linux"\nVERSION_ID="12"\nID=debian\n' >"$1/etc/os-release"
  printf '# The Caddyfile is an easy way to configure your Caddy web server.\n:80 {\n\t# Set this path to your site'"'"'s directory.\n\troot * /usr/share/caddy\n\n\t# Enable the static file server.\n\tfile_server\n}\n' >"$1/etc/caddy/Caddyfile"
}
stopserver() { if [ -f "$STATE/server.pid" ]; then kill "$(cat "$STATE/server.pid")" 2>/dev/null || true; rm -f "$STATE/server.pid"; sleep 0.3; fi; }
trap stopserver EXIT

R=$T/root
newroot "$R"
export FRAGDESK_ROOT="$R"

# 1) Probelauf: ändert nichts
bash "$INSTALL" --dry-run >"$T/dry.out" 2>&1 || { cat "$T/dry.out"; exit 1; }
check "dry-run: Domain aus der IP" has "$T/dry.out" "Domain: 203-0-113-5.sslip.io"
check "dry-run: zeigt apt" has "$T/dry.out" "würde ausführen: apt-get install -y -q --no-install-recommends caddy"
check "dry-run: zeigt Dienst" has "$T/dry.out" "würde schreiben: /etc/systemd/system/fragdesk-server.service"
check "dry-run: zeigt Download" has "$T/dry.out" "releases/latest/download/fragdesk-server-linux-x64"
check "dry-run: nichts geschrieben" test ! -e "$R/etc/systemd/system/fragdesk-server.service" -a ! -e "$R/usr/local/bin/fragdesk-server" -a ! -e "$R/var/lib/fragdesk"
check "dry-run: nichts ausgeführt" test ! -s "$CALLS"
check "dry-run: Caddyfile unverändert" hasnt "$R/etc/caddy/Caddyfile" "import"

# 2) Installieren (Download + Prüfsumme)
bash "$INSTALL" >"$T/run1.out" 2>&1 || { cat "$T/run1.out"; exit 1; }
check "Programm installiert" cmp "$SERVER" "$R/usr/local/bin/fragdesk-server"
check "Prüfsumme geprüft" has "$T/run1.out" "Prüfsumme stimmt."
check "Caddy aus apt" has "$CALLS" "apt-get install -y -q --no-install-recommends caddy"
check "Benutzer angelegt" has "$CALLS" "useradd --system --user-group --home-dir /var/lib/fragdesk --no-create-home --shell /usr/sbin/nologin fragdesk"
check "Datenordner 0700" test "$(stat -c %a "$R/var/lib/fragdesk")" = 700
check "Datenordner gehört fragdesk" has "$CALLS" "chown -R fragdesk:fragdesk $R/var/lib/fragdesk"
check "ufw 80" has "$CALLS" "ufw allow 80/tcp"
check "ufw 443" has "$CALLS" "ufw allow 443/tcp"
check "Dienst aktiviert" has "$CALLS" "systemctl enable fragdesk-server"
check "Caddy aktiviert" has "$CALLS" "systemctl enable caddy"
check "Konfiguration: Domain" has "$R/etc/default/fragdesk" "FRAGDESK_DOMAIN=203-0-113-5.sslip.io"
check "Konfiguration: URL" has "$R/etc/default/fragdesk" "FRAGDESK_URL=https://203-0-113-5.sslip.io/"
check "Konfiguration: Zeitzone" has "$R/etc/default/fragdesk" "TZ=Europe/Berlin"
U=$R/etc/systemd/system/fragdesk-server.service
check "Dienst: ExecStart" has "$U" "ExecStart=/usr/local/bin/fragdesk-server serve --data /var/lib/fragdesk --listen 127.0.0.1:$PORT --url \${FRAGDESK_URL}"
check "Dienst: Benutzer" has "$U" "User=fragdesk"
check "Dienst: Härtung" has "$U" "ProtectSystem=strict"
check "Dienst: nur Datenordner beschreibbar" has "$U" "ReadWritePaths=/var/lib/fragdesk"
check "Dienst: Neustart bei Fehlern" has "$U" "Restart=on-failure"
C=$R/etc/caddy
check "Caddy: Seite" has "$C/fragdesk.caddy" "203-0-113-5.sslip.io {"
check "Caddy: 16 MB" has "$C/fragdesk.caddy" "max_size 16MB"
check "Caddy: Proxy" has "$C/fragdesk.caddy" "reverse_proxy 127.0.0.1:$PORT"
check "Caddy: lokale Befehle gesperrt" has "$C/fragdesk.caddy" "@local path /api/v1/local/*"
check "Caddyfile bindet ein" has "$C/Caddyfile" "import /etc/caddy/fragdesk.caddy"
check "Caddyfile: Standardseite ersetzt" hasnt "$C/Caddyfile" "file_server"
check "Caddyfile gesichert" has "$C/Caddyfile.vor-fragdesk" "file_server"
check "Caddy geprüft" has "$CALLS" "caddy validate --config $C/Caddyfile --adapter caddyfile"
if [ -n "$CADDY_BIN" ]; then check "Caddy (echt) akzeptiert die Konfiguration" has "$STATE/caddy-validate.log" "Valid configuration"; fi
check "Server läuft" "$REAL_CURL" -fsS "http://127.0.0.1:$PORT/api/v1/health"
check "HTTPS geprüft" has "$T/run1.out" "HTTPS geht."
LINK=$(grep -o 'https://203-0-113-5.sslip.io/#invite=[0-9a-f]*' "$T/run1.out" | tail -n 1)
check "Einladung (Admin) ausgegeben" test -n "$LINK"
check "Hinweis FragDesk" has "$T/run1.out" "Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“"

# Einladung einlösen (wie FragDesk), dann aktualisieren: kein neuer Admin-Link, Daten bleiben, Domain wechselt
CODE=${LINK##*#invite=}
"$REAL_CURL" -fsS -X POST -H 'X-FragDesk: 1' -H 'Content-Type: application/json' \
  -d "{\"invite\":\"$CODE\",\"name\":\"Admin\",\"steamId\":\"76561190000000001\"}" "http://127.0.0.1:$PORT/api/v1/account/register" >"$T/register.json"
check "Einladung aus install.sh gilt" has "$T/register.json" '"admin":true'
: >"$CALLS"
bash "$INSTALL" --domain Stats.Example.org --binary "$SERVER" >"$T/run2.out" 2>&1 || { cat "$T/run2.out"; exit 1; }
check "Update: kein neuer Admin-Link" hasnt "$T/run2.out" "#invite="
check "Update: Admin erkannt" has "$T/run2.out" "Es gibt schon einen Admin"
check "Update: Programm aktuell" has "$T/run2.out" "Schon aktuell"
check "Update: kein apt" hasnt "$CALLS" "apt-get"
check "Update: kein useradd" hasnt "$CALLS" "useradd"
check "Update: neue Domain" has "$R/etc/default/fragdesk" "FRAGDESK_DOMAIN=stats.example.org"
check "Update: Caddy mit neuer Domain" has "$C/fragdesk.caddy" "stats.example.org {"
check "Update: Caddy neu geladen" has "$CALLS" "systemctl reload caddy"
check "Update: Dienst neu gestartet (URL geändert)" has "$CALLS" "systemctl restart fragdesk-server"
check "Update: import nur einmal" test "$(grep -c 'import /etc/caddy/fragdesk.caddy' "$C/Caddyfile")" = 1
check "Update: Konto noch da" sh -c "'$R/usr/local/bin/fragdesk-server' users --json --data '$R/var/lib/fragdesk' | grep -q '\"name\":\"Admin\"'"
check "Update: neue URL im Server" has "$R/var/lib/fragdesk/server.json" '"url":"https://stats.example.org/"'

# 3) Nochmal ohne Angaben: eigene Domain bleibt, nichts ändert sich, kein Neustart
: >"$CALLS"
bash "$INSTALL" --binary "$SERVER" >"$T/run3.out" 2>&1 || { cat "$T/run3.out"; exit 1; }
check "3. Lauf: Domain bleibt" has "$R/etc/default/fragdesk" "FRAGDESK_DOMAIN=stats.example.org"
check "3. Lauf: kein Neustart" hasnt "$CALLS" "systemctl restart fragdesk-server"
check "3. Lauf: Caddy unverändert" has "$T/run3.out" "Caddy-Konfiguration unverändert."

# 4) Beschädigter Download: Prüfsumme passt nicht, installiertes Programm bleibt
printf 'kaputt' >>"$T/release/fragdesk-server-linux-x64"
if bash "$INSTALL" >"$T/run4.out" 2>&1; then check "falsche Prüfsumme bricht ab" false; fi
check "falsche Prüfsumme: Meldung" has "$T/run4.out" "Prüfsumme stimmt nicht"
check "falsche Prüfsumme: Programm unverändert" cmp "$SERVER" "$R/usr/local/bin/fragdesk-server"
cp "$SERVER" "$T/release/fragdesk-server-linux-x64"

# 5) Entfernen: Daten bleiben
stopserver
: >"$CALLS"
bash "$INSTALL" --uninstall >"$T/run5.out" 2>&1 || { cat "$T/run5.out"; exit 1; }
check "Entfernen: Dienst weg" test ! -e "$U" -a ! -e "$R/usr/local/bin/fragdesk-server" -a ! -e "$R/etc/default/fragdesk" -a ! -e "$C/fragdesk.caddy"
check "Entfernen: import weg" hasnt "$C/Caddyfile" "fragdesk"
check "Entfernen: altes Caddyfile zurück" has "$C/Caddyfile" "file_server"
check "Entfernen: Daten bleiben" test -f "$R/var/lib/fragdesk/server.json"
check "Entfernen: Dienst gestoppt" has "$CALLS" "systemctl disable --now fragdesk-server"

# 6) Eigenes Caddyfile mit anderer Seite, Zeitzone des Systems, Caddy nicht in den Paketquellen
R2=$T/root2
newroot "$R2"
printf 'example.net {\n\trespond "hallo"\n}\n' >"$R2/etc/caddy/Caddyfile"
printf 'Europe/Vienna\n' >"$R2/etc/timezone"
rm -f "$T/bin/caddy" "$STATE/user-fragdesk"
: >"$CALLS"
FRAGDESK_ROOT=$R2 FAKE_NO_CADDY=1 FAKE_HTTPS=1 bash "$INSTALL" >"$T/run6.out" 2>&1 || { cat "$T/run6.out"; exit 1; }
check "eigenes Caddyfile bleibt" has "$R2/etc/caddy/Caddyfile" 'respond "hallo"'
check "eigenes Caddyfile: import angehängt" has "$R2/etc/caddy/Caddyfile" "import /etc/caddy/fragdesk.caddy"
check "Zeitzone des Systems" has "$R2/etc/default/fragdesk" "TZ=Europe/Vienna"
check "Caddy aus den Paketquellen von Caddy" has "$CALLS" "curl https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt"
check "Paketquelle eingetragen" has "$R2/etc/apt/sources.list.d/caddy-stable.list" "dl.cloudsmith.io/public/caddy/stable"
if [ -n "$CADDY_BIN" ]; then check "Caddy (echt) akzeptiert beide Seiten" has "$STATE/caddy-validate.log" "Valid configuration"; fi
stopserver

# 7) Falsche Angaben
check "--domain wird geprüft" sh -c "! bash '$INSTALL' --dry-run --domain 'bad domain'"
check "unbekannte Option" sh -c "! bash '$INSTALL' --gibtsnicht"
check "--help" sh -c "bash '$INSTALL' --help | grep -q -- --dry-run"

echo "install_test: $CHECKS Prüfungen, $FAILS Fehler (Port $PORT)"
[ "$FAILS" = 0 ]

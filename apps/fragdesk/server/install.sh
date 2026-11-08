#!/usr/bin/env bash
# FragDesk-Server installieren oder aktualisieren – Debian/Ubuntu (x86_64), als root, mit einem Befehl:
#
#   curl -fsSL https://raw.githubusercontent.com/Micha040/effective-lamp/main/apps/fragdesk/server/install.sh | bash
#   curl -fsSL …/install.sh | bash -s -- --domain stats.example.org      (eigene Domain)
#   curl -fsSL …/install.sh | bash -s -- --dry-run                        (nur zeigen, was passieren würde)
#   (als root; als normaler Benutzer jeweils „| sudo bash“)
#
# Was passiert: Caddy (HTTPS mit automatischem Zertifikat) aus apt, Benutzer „fragdesk“, Daten in /var/lib/fragdesk
# (0700), fragdesk-server aus dem neuesten GitHub-Release (Prüfsumme aus SHA256SUMS.txt), systemd-Dienst mit
# Härtung, Caddy-Konfiguration, Ports 80/443 in ufw (falls aktiv), Start, Gesundheitsprüfung – und zum Schluss die
# erste Einladung (Admin) als Link. Erneut ausführen = aktualisieren: Daten, Domain und Zeitzone bleiben.
# Für „Demos automatisch laden“ außerdem Node.js (≥ 18) aus apt und aus dem Release den Helfer fragdesk-fetch.js und
# fdanalyze (nach /usr/local/lib/fragdesk) mit den systemd-Einheiten fragdesk-fetch.service/.timer/.path. Fehlt
# Node.js 18, bleibt nur diese Funktion aus – der Rest wird ganz normal installiert.
#
# Optionen:
#   --domain <name>    eigene Domain (DNS-A-Eintrag auf diesen Server); ohne: <ip-mit-bindestrichen>.sslip.io
#   --timezone <Zone>  Zeitzone für Uhrzeiten in der Handy-App (Standard: die des Servers, bei UTC Europe/Berlin)
#   --version <tag>    bestimmtes Release statt des neuesten, z. B. v1.4.0
#   --binary <datei>   fragdesk-server von hier nehmen statt herunterladen (z. B. selbst gebaut)
#   --dry-run          nichts ändern, nur anzeigen
#   --uninstall        Dienst, Programm und Caddy-Eintrag entfernen (die Daten bleiben)
# shellcheck disable=SC1111   # „…“ sind deutsche Anführungszeichen in Texten, kein Shell-Code
set -euo pipefail

REPO=Micha040/effective-lamp
ROOT=${FRAGDESK_ROOT:-}           # nur für Tests: alle Dateien unter diesem Ordner statt unter /
PORT=${FRAGDESK_PORT:-7420}       # der Server lauscht nur auf 127.0.0.1:<PORT>, davor Caddy
SVC=fragdesk-server
BIN_PATH=/usr/local/bin/fragdesk-server
DATA_PATH=/var/lib/fragdesk
BIN=$ROOT$BIN_PATH
DATA=$ROOT$DATA_PATH
UNIT=$ROOT/etc/systemd/system/$SVC.service
CONF=$ROOT/etc/default/fragdesk
CADDY_DIR=$ROOT/etc/caddy
SITE_PATH=/etc/caddy/fragdesk.caddy
IMPORT_LINE="import $SITE_PATH"
# Demos automatisch laden: Helfer (Node.js) und fdanalyze, eigener Dienst mit Timer und Path-Unit
FETCH_SVC=fragdesk-fetch
LIB_PATH=/usr/local/lib/fragdesk
LIB=$ROOT$LIB_PATH
FETCH_UNITS="$FETCH_SVC.service $FETCH_SVC.timer $FETCH_SVC.path"
FETCH_FILES="fragdesk-fetch.js fragdesk-fetch-NOTICES.txt fdanalyze-linux-x64"
NODE_MIN=18

DRY=0 UNINSTALL=0 DOMAIN="" TZONE="" VERSION="" LOCAL_BIN=""
CHANGED=0 TMP="" INVITE="" HTTPS_OK=0
# FETCH: on = Helfer wird eingerichtet; sonst der Grund, warum nicht (steht als FRAGDESK_FETCH in /etc/default/fragdesk,
# die Handy-App zeigt dem Admin dazu einen Satz): node (Node.js ≥ 18 fehlt), release (Release ohne Helfer), verify
# (systemd-analyze verify meldet Fehler)
FETCH=on NODE_BIN="" NODE_VER="" FETCH_CHANGED=0
# „sudo “ nur in Hinweisen, wenn das Skript mit sudo lief – als root direkt (z. B. netcup, oft ohne sudo) ohne
SUDO=""
if [ -n "${SUDO_USER:-}" ] || [ "$(id -u)" != 0 ]; then SUDO="sudo "; fi

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
warn() { printf 'Achtung: %s\n' "$*" >&2; }
die() {
  printf 'FEHLER: %s\n' "$*" >&2
  exit 1
}

# Befehl ausführen – oder bei --dry-run nur anzeigen
run() {
  if [ "$DRY" = 1 ]; then
    say "  würde ausführen: $*"
    return 0
  fi
  "$@"
}

# write_file <pfad> <rechte> <inhalt>: schreibt (atomar) nur bei Änderungen; Rückgabe 0 = geändert, 1 = war schon so
write_file() {
  local path=$1 mode=$2 content=$3 tmp
  if [ -f "$path" ] && [ "$(cat "$path")" = "$content" ]; then return 1; fi
  if [ "$DRY" = 1 ]; then
    say "  würde schreiben: ${path#"$ROOT"} ($mode)"
    printf '%s\n' "$content" | sed 's/^/    | /'
    return 0
  fi
  mkdir -p "$(dirname "$path")"
  tmp=$(mktemp "$path.XXXXXX")
  printf '%s\n' "$content" >"$tmp"
  chmod "$mode" "$tmp"
  mv -f "$tmp" "$path"
  say "Geschrieben: ${path#"$ROOT"}"
  return 0
}

usage() {
  cat <<'EOF'
FragDesk-Server installieren oder aktualisieren (Debian/Ubuntu, x86_64, als root):

  curl -fsSL https://raw.githubusercontent.com/Micha040/effective-lamp/main/apps/fragdesk/server/install.sh | bash
  … | bash -s -- [Optionen]          (als normaler Benutzer: … | sudo bash)

  --domain <name>    eigene Domain (DNS-A-Eintrag auf diesen Server); ohne: <ip-mit-bindestrichen>.sslip.io
  --timezone <Zone>  Zeitzone für Uhrzeiten in der Handy-App (Standard: die des Servers, bei UTC Europe/Berlin)
  --version <tag>    bestimmtes Release statt des neuesten, z. B. v1.4.0
  --binary <datei>   fragdesk-server von hier nehmen statt herunterladen
  --dry-run          nichts ändern, nur anzeigen
  --uninstall        Dienst, Programm und Caddy-Eintrag entfernen (die Daten bleiben)

Erneut ausführen = aktualisieren: Daten, Domain und Zeitzone bleiben. Für „Demos automatisch laden“ kommt Node.js
(≥ 18) aus apt dazu; fehlt es dort, bleibt nur diese Funktion aus.
EOF
}

parse_args() {
  while [ $# -gt 0 ]; do
    case "$1" in
      --domain) DOMAIN=${2:-}; shift ;;
      --timezone) TZONE=${2:-}; shift ;;
      --version) VERSION=${2:-}; shift ;;
      --binary) LOCAL_BIN=${2:-}; shift ;;
      --dry-run) DRY=1 ;;
      --uninstall) UNINSTALL=1 ;;
      -h | --help)
        usage
        exit 0
        ;;
      *) die "Unbekannte Option: $1 (Hilfe: --help)" ;;
    esac
    shift
  done
  if [ -n "$DOMAIN" ]; then
    DOMAIN=${DOMAIN,,}
    DOMAIN=${DOMAIN#https://}
    DOMAIN=${DOMAIN%%/*}
    [[ "$DOMAIN" =~ ^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)+$ ]] || die "Ungültige Domain: $DOMAIN"
  fi
  if [ -n "$VERSION" ] && ! [[ "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then die "--version wie v1.4.0"; fi
  if [ -n "$LOCAL_BIN" ] && [ ! -f "$LOCAL_BIN" ]; then die "--binary: $LOCAL_BIN gibt es nicht"; fi
  return 0
}

# ---- Prüfen ---------------------------------------------------------------------------------------------------------
preflight() {
  if [ "$(id -u)" != 0 ] && [ "$DRY" = 0 ] && [ -z "$ROOT" ]; then die "Bitte als root ausführen (mit sudo)."; fi
  [ -r "$ROOT/etc/os-release" ] || die "/etc/os-release fehlt – unbekanntes System."
  local id like pretty
  id=$(sed -n 's/^ID=//p' "$ROOT/etc/os-release" | tr -d '"')
  like=$(sed -n 's/^ID_LIKE=//p' "$ROOT/etc/os-release" | tr -d '"')
  pretty=$(sed -n 's/^PRETTY_NAME=//p' "$ROOT/etc/os-release" | tr -d '"')
  case " $id $like " in
    *" debian "* | *" ubuntu "*) ;;
    *) die "install.sh ist für Debian und Ubuntu (gefunden: ${pretty:-$id})." ;;
  esac
  [ "$(uname -m)" = x86_64 ] || die "fragdesk-server gibt es nur für x86_64 (dieser Server: $(uname -m))."
  command -v systemctl >/dev/null || die "systemd fehlt."
  say "System: ${pretty:-$id}"
}

# Frühere Installation: Domain und Zeitzone übernehmen (nur lesen, nichts ausführen)
prev_get() { sed -n "s/^$1=//p" "$CONF" 2>/dev/null | tail -n 1 | tr -d "\"'" || true; }

is_private_ip() {
  case "$1" in
    10.* | 127.* | 0.* | 169.254.* | 192.168.* | 172.1[6-9].* | 172.2[0-9].* | 172.3[01].*) return 0 ;;
    100.6[4-9].* | 100.[7-9][0-9].* | 100.1[01][0-9].* | 100.12[0-7].*) return 0 ;;
  esac
  return 1
}

public_ipv4() {
  local ip
  # Adresse, über die der Server ins Internet geht – bei einem VPS (z. B. netcup) die öffentliche
  ip=$(ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -n 1 || true)
  if [ -n "$ip" ] && ! is_private_ip "$ip"; then
    say "$ip"
    return 0
  fi
  # hinter NAT: einmal nachfragen (liefert nur die eigene Adresse)
  ip=$(curl -4 -fsS --max-time 8 https://api.ipify.org 2>/dev/null || true)
  if [[ "$ip" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]] && ! is_private_ip "$ip"; then
    say "$ip"
    return 0
  fi
  return 1
}

pick_domain() {
  local prev ip
  prev=$(prev_get FRAGDESK_DOMAIN)
  if [ -z "$DOMAIN" ] && [ -n "$prev" ] && [[ "$prev" != *.sslip.io ]]; then DOMAIN=$prev; fi
  if [ -z "$DOMAIN" ]; then
    ip=$(public_ipv4) || die "Die öffentliche IPv4-Adresse dieses Servers ist nicht zu finden. Gib eine Domain an: --domain stats.example.org"
    DOMAIN="${ip//./-}.sslip.io"
  fi
  if [ -n "$prev" ] && [ "$prev" != "$DOMAIN" ]; then say "Domain: $DOMAIN (vorher $prev)"; else say "Domain: $DOMAIN"; fi
}

pick_timezone() {
  [ -n "$TZONE" ] || TZONE=$(prev_get TZ)
  if [ -z "$TZONE" ]; then
    TZONE=$(cat "$ROOT/etc/timezone" 2>/dev/null || readlink "$ROOT/etc/localtime" 2>/dev/null | sed 's|.*/zoneinfo/||' || true)
    case "$TZONE" in '' | UTC | Etc/* | Universal | Zulu | GMT) TZONE=Europe/Berlin ;; esac
  fi
  [[ "$TZONE" =~ ^[A-Za-z0-9_+/-]+$ ]] || die "Ungültige Zeitzone: $TZONE"
  say "Zeitzone für die Handy-App: $TZONE"
}

# ---- Pakete -----------------------------------------------------------------------------------------------------------
apt_has() { [[ "$(apt-cache policy "$1" 2>/dev/null)" =~ Candidate:\ [0-9] ]]; }

# Hauptversion einer Paketversion bzw. von „node --version“: "18.19.0+dfsg-6~deb12u2", "1:20.1.0-1", "v22.3.0" -> 18, 20, 22
major_of() {
  local v=${1#v}
  v=${v#*:}
  v=${v%%.*}
  [[ "$v" =~ ^[0-9]+$ ]] && printf '%s' "$v"
}

apt_candidate_major() { major_of "$(apt-cache policy "$1" 2>/dev/null | sed -n 's/^ *Candidate: *//p' | head -n 1)"; }

# Node.js für den Helfer suchen: NODE_BIN und NODE_VER setzen; 0 = brauchbar (≥ 18 und systemweit – unter /root, /home
# oder /snap kommt der Dienst nicht hin)
find_node() {
  local n v
  NODE_BIN="" NODE_VER=""
  hash -r 2>/dev/null || true
  for n in "$(command -v node 2>/dev/null || true)" "$ROOT/usr/bin/node"; do
    [ -n "$n" ] && [ -x "$n" ] || continue
    case "$n" in /root/* | /home/* | /snap/*) continue ;; esac
    v=$("$n" --version 2>/dev/null || true)
    [ -n "$(major_of "$v")" ] || continue
    NODE_BIN=$n NODE_VER=$v
    [ "$(major_of "$v")" -ge "$NODE_MIN" ] && return 0
  done
  return 1
}

node_hint() {   # warum „Demos automatisch laden“ aus bleibt
  warn "Für „Demos automatisch laden“ fehlt Node.js $NODE_MIN oder neuer ($1). Diese Funktion bleibt aus – alles andere" \
    "läuft ganz normal. Abhilfe: Node.js ≥ $NODE_MIN systemweit installieren (z. B. Debian 12/13 oder Ubuntu 24.04 haben es in" \
    "apt, sonst https://nodejs.org/de/download – Paket von NodeSource), dann dieses Skript noch einmal ausführen."
}

add_caddy_repo() {
  say "Caddy fehlt in den Paketquellen – nehme die offiziellen von Caddy (dl.cloudsmith.io)."
  run apt-get install -y -q --no-install-recommends debian-keyring debian-archive-keyring apt-transport-https gnupg curl
  if [ "$DRY" = 1 ]; then
    say "  würde Schlüssel und Paketquelle von https://dl.cloudsmith.io/public/caddy/stable/ eintragen"
  else
    curl -fsSL https://dl.cloudsmith.io/public/caddy/stable/gpg.key | gpg --dearmor --yes -o "$ROOT/usr/share/keyrings/caddy-stable-archive-keyring.gpg"
    curl -fsSL https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt -o "$ROOT/etc/apt/sources.list.d/caddy-stable.list"
  fi
  run apt-get update -q
}

install_packages() {
  step "Pakete"
  local need=() node=0 major
  command -v curl >/dev/null || need+=(curl ca-certificates)
  command -v caddy >/dev/null || need+=(caddy)
  [ -d /usr/share/zoneinfo ] || need+=(tzdata)
  if find_node; then
    say "Node.js $NODE_VER ist da (für „Demos automatisch laden“)."
  else
    node=1   # nodejs aus apt – aber nur, wenn es dort neu genug ist
  fi
  if [ ${#need[@]} -eq 0 ] && [ "$node" = 0 ]; then
    say "Caddy und curl sind schon da."
    return 0
  fi
  export DEBIAN_FRONTEND=noninteractive
  run apt-get update -q
  if [ "$node" = 1 ]; then
    major=$(apt_candidate_major nodejs)
    if [ -n "$major" ] && [ "$major" -ge "$NODE_MIN" ]; then
      need+=(nodejs)
    else
      FETCH=node
      node_hint "${NODE_VER:+gefunden: $NODE_VER, }in apt: ${major:+Version $major}${major:-keins}"
    fi
  fi
  [ ${#need[@]} -gt 0 ] || return 0
  say "Installiere: ${need[*]}"
  if [[ " ${need[*]} " == *" caddy "* ]] && ! apt_has caddy; then add_caddy_repo; fi
  run apt-get install -y -q --no-install-recommends "${need[@]}"
  if [[ " ${need[*]} " == *" nodejs "* ]]; then
    if [ "$DRY" = 1 ]; then
      NODE_BIN=/usr/bin/node NODE_VER="(aus apt)"
    elif find_node; then
      say "Node.js $NODE_VER installiert."
    else
      FETCH=node
      node_hint "${NODE_VER:-nach apt-get install nicht gefunden}"
    fi
  fi
}

# ---- Benutzer, Programm, Dienst ---------------------------------------------------------------------------------------
create_user() {
  step "Benutzer und Daten"
  if id fragdesk >/dev/null 2>&1; then
    say "Benutzer fragdesk gibt es schon."
  else
    run useradd --system --user-group --home-dir "$DATA_PATH" --no-create-home --shell /usr/sbin/nologin fragdesk
  fi
  run mkdir -p "$DATA/fetch"   # fetch/: Ordner des Helfers (wake, tmp/, lock)
  run chown -R fragdesk:fragdesk "$DATA"
  run chmod 0700 "$DATA" "$DATA/fetch"
  say "Daten: $DATA_PATH (nur für fragdesk lesbar)"
}

# Prüfsumme einer heruntergeladenen Datei aus SHA256SUMS.txt; 0 = stimmt, 1 = Datei fehlt dort, sonst Abbruch
sum_of() { awk -v n="$1" '$2 == n || $2 == "*" n { print $1 }' "$2"; }
check_sum() {
  local file=$1 name=$2 sums=$3 want got
  want=$(sum_of "$name" "$sums")
  [ -n "$want" ] || return 1
  got=$(sha256sum "$file" | cut -d ' ' -f 1)
  [ "$got" = "$want" ] || die "Prüfsumme stimmt nicht ($name: erwartet $want, bekommen $got) – Download beschädigt? Nichts geändert."
  return 0
}

# Helfer für „Demos automatisch laden“ aus dem Release nach $TMP/fetch – erst prüfen, installiert wird später
download_fetch() {
  local base=$1 f
  [ "$FETCH" = on ] || return 0
  if [ "$DRY" = 1 ]; then
    say "  würde ${FETCH_FILES// /, } herunterladen, mit SHA256SUMS.txt prüfen und nach $LIB_PATH legen"
    return 0
  fi
  mkdir -p "$TMP/fetch"
  if [ ! -f "$TMP/SHA256SUMS.txt" ]; then
    curl -fsSL --retry 3 -o "$TMP/SHA256SUMS.txt" "$base/SHA256SUMS.txt" || die "Download fehlgeschlagen ($base/SHA256SUMS.txt)."
  fi
  for f in $FETCH_FILES; do
    if [ -z "$(sum_of "$f" "$TMP/SHA256SUMS.txt")" ]; then
      FETCH=release
      warn "Dieses Release enthält „Demos automatisch laden“ noch nicht ($f fehlt). Die Funktion bleibt aus, alles andere läuft."
      return 0
    fi
    curl -fsSL --retry 3 -o "$TMP/fetch/$f" "$base/$f" || die "Download fehlgeschlagen ($base/$f)."
    check_sum "$TMP/fetch/$f" "$f" "$TMP/SHA256SUMS.txt" || die "SHA256SUMS.txt kennt $f nicht."
  done
  chmod 0755 "$TMP/fetch/fdanalyze-linux-x64"
  "$TMP/fetch/fdanalyze-linux-x64" -version >/dev/null 2>&1 || die "fdanalyze aus dem Release läuft auf diesem System nicht – nichts geändert."
  say "Helfer für „Demos automatisch laden“ geladen, Prüfsummen stimmen."
}

install_binary() {
  step "fragdesk-server"
  local base="https://github.com/$REPO/releases/latest/download" file
  [ -z "$VERSION" ] || base="https://github.com/$REPO/releases/download/$VERSION"
  if [ "$DRY" = 1 ]; then
    if [ -n "$LOCAL_BIN" ]; then say "  würde $LOCAL_BIN nach $BIN_PATH kopieren"; else
      say "  würde $base/fragdesk-server-linux-x64 herunterladen, mit $base/SHA256SUMS.txt prüfen und nach $BIN_PATH legen"
    fi
    download_fetch "$base"
    return 0
  fi
  TMP=$(mktemp -d)
  file="$TMP/fragdesk-server-linux-x64"
  if [ -n "$LOCAL_BIN" ]; then
    cp "$LOCAL_BIN" "$file"
  else
    say "Lade $base/fragdesk-server-linux-x64 …"
    curl -fsSL --retry 3 -o "$file" "$base/fragdesk-server-linux-x64" || die "Download fehlgeschlagen ($base/fragdesk-server-linux-x64)."
    curl -fsSL --retry 3 -o "$TMP/SHA256SUMS.txt" "$base/SHA256SUMS.txt" || die "Download fehlgeschlagen ($base/SHA256SUMS.txt)."
    check_sum "$file" fragdesk-server-linux-x64 "$TMP/SHA256SUMS.txt" || die "SHA256SUMS.txt kennt fragdesk-server-linux-x64 nicht."
    say "Prüfsumme stimmt."
  fi
  chmod 0755 "$file"
  "$file" version >/dev/null 2>&1 || die "Die neue Datei läuft auf diesem System nicht – nichts geändert."
  download_fetch "$base"   # alles geprüft, bevor etwas ersetzt wird
  if [ -f "$BIN" ] && cmp -s "$file" "$BIN"; then
    say "Schon aktuell: $("$BIN" version)"
  else
    mkdir -p "$(dirname "$BIN")"
    install -m 0755 "$file" "$BIN.new"
    mv -f "$BIN.new" "$BIN"
    CHANGED=1
    say "Installiert: $("$BIN" version)"
  fi
}

write_config() {
  local conf
  conf=$(
    cat <<EOF
# FragDesk-Server – geschrieben von install.sh (wird beim nächsten Lauf übernommen bzw. ersetzt)
FRAGDESK_DOMAIN=$DOMAIN
FRAGDESK_URL=https://$DOMAIN/
TZ=$TZONE
# Demos automatisch laden: on = Helfer eingerichtet, sonst warum nicht (node, release, verify)
FRAGDESK_FETCH=$FETCH
EOF
  )
  if write_file "$CONF" 0644 "$conf"; then CHANGED=1; fi
}

# ---- Demos automatisch laden: Helfer, fdanalyze, systemd-Einheiten ----------------------------------------------------
install_fetch() {
  step "Demos automatisch laden"
  if [ "$FETCH" != on ]; then
    say "Bleibt aus (siehe Hinweis oben) – alles andere wird ganz normal eingerichtet."
    return 0
  fi
  if [ "$DRY" = 1 ]; then
    say "  würde fragdesk-fetch.js und fdanalyze nach $LIB_PATH legen (Node.js $NODE_VER: $NODE_BIN)"
    return 0
  fi
  local f dst mode
  mkdir -p "$LIB"
  chmod 0755 "$LIB"
  for f in $FETCH_FILES; do
    dst="$LIB/$f" mode=0644
    if [ "$f" = fdanalyze-linux-x64 ]; then dst="$LIB/fdanalyze" mode=0755; fi
    if [ -f "$dst" ] && cmp -s "$TMP/fetch/$f" "$dst"; then continue; fi
    install -m "$mode" "$TMP/fetch/$f" "$dst.new"
    mv -f "$dst.new" "$dst"
    FETCH_CHANGED=1
  done
  say "Helfer: $LIB_PATH/fragdesk-fetch.js mit Node.js $NODE_VER ($NODE_BIN), $("$LIB/fdanalyze" -version 2>/dev/null | head -n 1)$([ "$FETCH_CHANGED" = 1 ] || printf ' – schon aktuell')"
}

# fragdesk-fetch.service/.timer/.path schreiben und mit systemd-analyze prüfen
write_fetch_units() {
  if [ "$FETCH" != on ]; then
    remove_fetch
    return 0
  fi
  step "Demos automatisch laden: systemd"
  local dir=$ROOT/etc/systemd/system svc timer path
  svc=$(
    cat <<EOF
# FragDesk-Server: Demos automatisch laden – geschrieben von install.sh (wird beim nächsten Lauf ersetzt)
# Holt neue Premier-/Wettkampf-/Wingman-Demos bei Valve, wertet sie aus und legt sie ins Konto. Gestartet von
# $FETCH_SVC.timer (alle 15 Minuten) und $FETCH_SVC.path (der Server bittet darum), arbeitet ab und beendet sich.
[Unit]
Description=FragDesk: neue Demos bei Valve holen und auswerten
Documentation=https://github.com/$REPO/tree/main/apps/fragdesk#eigener-server
After=network-online.target $SVC.service
Wants=network-online.target

[Service]
Type=oneshot
User=fragdesk
Group=fragdesk
# --jitless: JavaScript ohne JIT-Compiler – nur so geht MemoryDenyWriteExecute (unten) mit Node.js
ExecStart=$NODE_BIN --jitless --no-expose-wasm $LIB_PATH/fragdesk-fetch.js run --server http://127.0.0.1:$PORT --data $DATA_PATH --analyzer $LIB_PATH/fdanalyze
# Zeitzone (TZ) für Uhrzeiten in Meldungen, wie beim Server
EnvironmentFile=-/etc/default/fragdesk
Environment=GOMEMLIMIT=512MiB
# libuv: kein io_uring (wird unten ohnehin abgewiesen)
Environment=UV_USE_IO_URING=0
Nice=19
IOSchedulingClass=idle
TimeoutStartSec=2h
UMask=0077
# Sicherheitsnetz: eine kaputte Demo darf den Server nicht in den Speichermangel treiben (normal: 200–400 MB)
MemoryMax=1G

# Härtung wie beim Server – nur darf der Helfer ins Internet (Steam, Valve) und nur in fetch/ schreiben
NoNewPrivileges=yes
CapabilityBoundingSet=
AmbientCapabilities=
ProtectSystem=strict
ReadWritePaths=$DATA_PATH/fetch
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
PrivateUsers=yes
ProtectHostname=yes
ProtectClock=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectProc=invisible
ProcSubset=pid
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RemoveIPC=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged
# Nicht erlaubte Systemaufrufe scheitern mit EPERM, statt den Prozess zu beenden: Node.js 20 fragt beim Start
# pkey_alloc (nicht in @system-service) und kommt mit einer Absage gut zurecht
SystemCallErrorNumber=EPERM
EOF
  )
  timer=$(
    cat <<EOF
# FragDesk-Server: alle 15 Minuten nach neuen Demos schauen – geschrieben von install.sh (wird beim nächsten Lauf ersetzt)
[Unit]
Description=FragDesk: alle 15 Minuten nach neuen Demos schauen

[Timer]
OnBootSec=5min
OnUnitInactiveSec=15min
RandomizedDelaySec=60

[Install]
WantedBy=timers.target
EOF
  )
  path=$(
    cat <<EOF
# FragDesk-Server: Demos holen, sobald der Server darum bittet (er schreibt fetch/wake neu) – von install.sh
[Unit]
Description=FragDesk: Demos holen, sobald der Server darum bittet

[Path]
PathModified=$DATA_PATH/fetch/wake

[Install]
WantedBy=paths.target
EOF
  )
  if write_file "$dir/$FETCH_SVC.service" 0644 "$svc"; then FETCH_CHANGED=1; fi
  if write_file "$dir/$FETCH_SVC.timer" 0644 "$timer"; then FETCH_CHANGED=1; fi
  if write_file "$dir/$FETCH_SVC.path" 0644 "$path"; then FETCH_CHANGED=1; fi
  run systemctl daemon-reload
  if ! verify_fetch_units; then
    FETCH=verify
    remove_fetch
  fi
}

verify_fetch_units() {
  local dir=$ROOT/etc/systemd/system out
  if [ "$DRY" = 1 ]; then
    say "  würde prüfen: systemd-analyze verify $FETCH_UNITS"
    return 0
  fi
  if ! command -v systemd-analyze >/dev/null; then
    warn "systemd-analyze fehlt – die Einheiten von $FETCH_SVC bleiben ungeprüft."
    return 0
  fi
  if out=$(systemd-analyze verify "$dir/$FETCH_SVC.service" "$dir/$FETCH_SVC.timer" "$dir/$FETCH_SVC.path" 2>&1); then
    say "systemd-analyze verify: Einheiten in Ordnung."
    # Warnungen (z. B. eine Einstellung, die dieses systemd noch nicht kennt) nur zeigen – laufen tut es trotzdem
    if [ -n "$out" ]; then printf '%s\n' "$out" | tail -n 8 | sed 's/^/  Hinweis: /'; fi
    return 0
  fi
  printf '%s\n' "$out" | tail -n 8 >&2
  warn "systemd-analyze verify meldet Fehler in den Einheiten von $FETCH_SVC – „Demos automatisch laden“ bleibt aus."
  return 1
}

start_fetch() {
  [ "$FETCH" = on ] || return 0
  if [ "$DRY" = 1 ]; then
    say "  würde $FETCH_SVC.timer und $FETCH_SVC.path einschalten"
    return 0
  fi
  systemctl enable --now "$FETCH_SVC.timer" "$FETCH_SVC.path" --quiet
  if [ "$FETCH_CHANGED" = 1 ]; then systemctl restart "$FETCH_SVC.timer" "$FETCH_SVC.path" || true; fi
  say "„Demos automatisch laden“ ist bereit (alle 15 Minuten und sobald der Server darum bittet; Protokoll: journalctl -u $FETCH_SVC)."
}

# Helfer und Einheiten entfernen (Funktion aus, --uninstall); die Daten in fetch/ bleiben
remove_fetch() {
  local any=0 u
  for u in $FETCH_UNITS; do if [ -e "$ROOT/etc/systemd/system/$u" ]; then any=1; fi; done
  if [ -d "$LIB" ]; then any=1; fi
  [ "$any" = 1 ] || return 0
  run systemctl disable --now "$FETCH_SVC.timer" "$FETCH_SVC.path" --quiet || true
  run systemctl stop "$FETCH_SVC.service" || true
  for u in $FETCH_UNITS; do run rm -f "$ROOT/etc/systemd/system/$u"; done
  run rm -rf "$LIB"
  run systemctl daemon-reload
  say "Helfer für „Demos automatisch laden“ entfernt."
}

write_unit() {
  step "systemd-Dienst"
  local unit
  unit=$(
    cat <<EOF
# FragDesk-Server – geschrieben von install.sh (wird beim nächsten Lauf ersetzt)
[Unit]
Description=FragDesk-Server (CS2-Stats und Clips für die Handy-App)
Documentation=https://github.com/$REPO/tree/main/apps/fragdesk#eigener-server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=fragdesk
Group=fragdesk
EnvironmentFile=/etc/default/fragdesk
# glibc: wenige Speicher-Arenen – nach großen Heatmaps gibt der Server den Speicher wieder ganz her
Environment=MALLOC_ARENA_MAX=2
ExecStart=$BIN_PATH serve --data $DATA_PATH --listen 127.0.0.1:$PORT --url \${FRAGDESK_URL}
Restart=on-failure
RestartSec=5
TimeoutStopSec=20
UMask=0077
LimitNOFILE=4096

# Härtung: nur der Datenordner ist beschreibbar, keine Rechte, keine Geräte, nur Netz über 127.0.0.1
NoNewPrivileges=yes
CapabilityBoundingSet=
AmbientCapabilities=
ProtectSystem=strict
ReadWritePaths=$DATA_PATH
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
PrivateUsers=yes
ProtectHostname=yes
ProtectClock=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectProc=invisible
ProcSubset=pid
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RemoveIPC=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged
IPAddressDeny=any
IPAddressAllow=localhost

[Install]
WantedBy=multi-user.target
EOF
  )
  if write_file "$UNIT" 0644 "$unit"; then CHANGED=1; fi
  run systemctl daemon-reload
  run systemctl enable "$SVC" --quiet
}

# ---- Caddy ------------------------------------------------------------------------------------------------------------
# Unverändertes Caddyfile aus dem Paket (nur die Begrüßungsseite auf :80)?
is_stock_caddyfile() {
  local body
  body=$( (grep -v '^[[:space:]]*#' "$1" || true) | tr -d ' \t\n')
  [ "$body" = ":80{root*/usr/share/caddyfile_server}" ] || [ -z "$body" ]
}

write_caddy() {
  step "Caddy (HTTPS)"
  local site main="$CADDY_DIR/Caddyfile" backup=""
  site=$(
    cat <<EOF
# FragDesk-Server – geschrieben von install.sh (wird beim nächsten Lauf ersetzt)
$DOMAIN {
	# JSON und Text komprimieren, Videos und Bilder nicht
	encode {
		gzip
		zstd
		match {
			header Content-Type application/json*
			header Content-Type application/manifest+json*
			header Content-Type text/*
			header Content-Type image/svg+xml*
		}
	}
	# FragDesk lädt Videos in Stücken zu 4 MB hoch
	request_body {
		max_size 16MB
	}
	# Befehle von „fragdesk-server invite/users“ gehen nur direkt auf dem Server, nie von außen
	@local path /api/v1/local/*
	respond @local 404
	header {
		Strict-Transport-Security "max-age=31536000"
		-Server
	}
	# höchstens 16 Verbindungen zum Server (er nimmt 24 an), weitere Anfragen warten kurz; ruhende Verbindungen
	# schließt Caddy vor dem Server (der nach 20 s)
	reverse_proxy 127.0.0.1:$PORT {
		transport http {
			keepalive 15s
			max_conns_per_host 16
		}
	}
}
EOF
  )
  local changed=0
  if write_file "$CADDY_DIR/fragdesk.caddy" 0644 "$site"; then changed=1; fi
  if [ -f "$main" ] && grep -qxF "$IMPORT_LINE" "$main"; then
    say "Caddyfile bindet fragdesk.caddy schon ein."
  elif [ ! -f "$main" ] || is_stock_caddyfile "$main"; then
    if [ -f "$main" ]; then
      backup="$main.vor-fragdesk"
      run cp -p "$main" "$backup"
    fi
    write_file "$main" 0644 "$(printf '%s\n%s\n\n%s' '# Caddy – FragDesk-Server steht in fragdesk.caddy (von install.sh).' \
      '# Weitere eigene Seiten einfach hier darunter eintragen.' "$IMPORT_LINE")" || true
    changed=1
  else
    backup="$main.vor-fragdesk"
    run cp -p "$main" "$backup"
    say "Vorhandenes Caddyfile bleibt, FragDesk wird nur eingebunden (Sicherung: ${backup#"$ROOT"})."
    if [ "$DRY" = 1 ]; then say "  würde anhängen: $IMPORT_LINE"; else printf '\n# FragDesk-Server (install.sh)\n%s\n' "$IMPORT_LINE" >>"$main"; fi
    changed=1
  fi
  if [ "$DRY" = 0 ] && ! caddy validate --config "$main" --adapter caddyfile >/dev/null 2>&1; then
    caddy validate --config "$main" --adapter caddyfile 2>&1 | tail -n 5 >&2 || true
    if [ -n "$backup" ]; then cp -p "$backup" "$main"; fi
    die "Die Caddy-Konfiguration ist ungültig – das alte Caddyfile ist wiederhergestellt."
  fi
  run systemctl enable caddy --quiet
  if [ "$DRY" = 1 ]; then
    say "  würde Caddy neu laden"
  elif ! systemctl is-active --quiet caddy; then
    systemctl restart caddy
  elif [ "$changed" = 1 ]; then
    systemctl reload caddy || systemctl restart caddy
  else
    say "Caddy-Konfiguration unverändert."
  fi
}

open_firewall() {
  local st
  command -v ufw >/dev/null || return 0
  st=$(LC_ALL=C ufw status 2>/dev/null || true)
  [[ "$st" == *"Status: active"* ]] || return 0
  step "Firewall (ufw)"
  run ufw allow 80/tcp
  run ufw allow 443/tcp
  run ufw allow 443/udp
  say "Ports 80 und 443 sind offen (für HTTPS und das Zertifikat)."
}

# ---- Start, Prüfung, erste Einladung ----------------------------------------------------------------------------------
start_service() {
  step "Start"
  if [ "$DRY" = 1 ]; then
    say "  würde $SVC (neu) starten"
    return 0
  fi
  if [ "$CHANGED" = 1 ] || ! systemctl is-active --quiet "$SVC"; then
    systemctl restart "$SVC"
  fi
  for _ in $(seq 1 40); do
    if curl -fsS --max-time 2 "http://127.0.0.1:$PORT/api/v1/health" >/dev/null 2>&1; then
      say "fragdesk-server läuft ($("$BIN" version))."
      return 0
    fi
    sleep 0.5
  done
  journalctl -u "$SVC" -n 30 --no-pager >&2 || true
  die "fragdesk-server antwortet nicht. Protokoll: journalctl -u $SVC"
}

check_https() {
  [ "$DRY" = 0 ] || return 0
  say "Prüfe https://$DOMAIN/ (beim ersten Mal holt Caddy das Zertifikat – meist Sekunden, selten ein paar Minuten) …"
  # Let's Encrypt, sonst ZeroSSL (beides macht Caddy selbst); höchstens etwa 4 Minuten warten
  for _ in $(seq 1 60); do
    if curl -fsS --max-time 5 "https://$DOMAIN/api/v1/health" >/dev/null 2>&1; then
      HTTPS_OK=1
      say "HTTPS geht."
      return 0
    fi
    sleep 3
  done
  if journalctl -u caddy --since "-15min" --no-pager 2>/dev/null | grep -qiE 'rateLimited|too many certificates'; then
    warn "Let's Encrypt gibt für $DOMAIN gerade keine Zertifikate aus (Limit erreicht – bei sslip.io kommt das vor). Caddy" \
      "versucht es von selbst weiter, auch bei ZeroSSL; in ein paar Minuten ist es meist da. Dauerhaft unabhängig davon:" \
      "eigene Domain, z. B. kostenlos bei duckdns.org, dann diesen Befehl mit „bash -s -- --domain <name>.duckdns.org“."
  else
    warn "https://$DOMAIN/ antwortet noch nicht. Sind die Ports 80 und 443 beim Anbieter offen (Firewall im Kundenmenü)?" \
      "Zeigt die Domain auf diesen Server? Caddy versucht es weiter – Protokoll: journalctl -u caddy"
  fi
}

first_invite() {
  step "Einladung"
  if [ "$DRY" = 1 ]; then
    say "  würde – falls es noch keinen Admin gibt – die erste Einladung (Admin) erstellen: $BIN_PATH invite --admin"
    return 0
  fi
  local users out
  users=$("$BIN" users --json --data "$DATA") || die "Konten lassen sich nicht abfragen."
  if [[ "$users" == *'"admin":true'* ]]; then
    say "Es gibt schon einen Admin – keine neue Einladung. Weitere: ${SUDO}fragdesk-server invite (oder in FragDesk „Freund einladen“)."
    return 0
  fi
  out=$("$BIN" invite --admin --data "$DATA") || die "Die Einladung lässt sich nicht erstellen."
  INVITE=$(printf '%s\n' "$out" | grep '#invite=' | tail -n 1)
  [ -n "$INVITE" ] || die "Die Einladung lässt sich nicht erstellen."
}

summary() {
  say ""
  if [ "$DRY" = 1 ]; then
    say "Probelauf fertig – nichts wurde geändert. Ohne --dry-run ausführen zum Installieren."
    return 0
  fi
  say "============================================================================"
  say "FragDesk-Server läuft: https://$DOMAIN/$([ "$HTTPS_OK" = 1 ] || printf ' (HTTPS noch nicht erreichbar, siehe oben)')"
  say "Daten: $DATA_PATH · Dienst: systemctl status $SVC · Protokoll: journalctl -u $SVC"
  if [ -n "$INVITE" ]; then
    say ""
    say "Deine Einladung als Admin – gilt 7 Tage und nur einmal:"
    say ""
    say "  $INVITE"
    say ""
    say "1. Link auf deinen PC bringen und kopieren."
    say "2. In FragDesk: Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“."
    say "3. Freunde einladen: in FragDesk „Freund einladen“ oder hier: ${SUDO}fragdesk-server invite"
  fi
  case "$FETCH" in
    on) say "Demos automatisch laden: bereit – den Steam-Bot richtest du in der Handy-App unter Server verwalten › Steam-Bot ein." ;;
    *) say "Demos automatisch laden: aus (siehe Hinweis oben)." ;;
  esac
  say "Aktualisieren: denselben Befehl noch einmal ausführen (Daten bleiben)."
  say "============================================================================"
}

uninstall() {
  step "Entfernen"
  remove_fetch
  run systemctl disable --now "$SVC" --quiet || true
  run rm -f "$UNIT" "$BIN" "$CONF" "$CADDY_DIR/fragdesk.caddy"
  local main="$CADDY_DIR/Caddyfile" rest
  if [ -f "$main" ] && grep -qxF "$IMPORT_LINE" "$main"; then
    rest=$( (grep -v '^[[:space:]]*#' "$main" || true) | (grep -vxF "$IMPORT_LINE" || true) | tr -d ' \t\n')
    if [ -z "$rest" ] && [ -f "$main.vor-fragdesk" ]; then
      run mv -f "$main.vor-fragdesk" "$main"   # das Caddyfile von vorher
    else
      run sed -i "\\#^$IMPORT_LINE\$#d; /^# FragDesk-Server (install.sh)\$/d" "$main"
    fi
    if [ "$DRY" = 0 ]; then systemctl reload caddy 2>/dev/null || true; fi
  fi
  run systemctl daemon-reload
  say ""
  say "fragdesk-server ist entfernt. Die Daten liegen weiter in $DATA_PATH."
  say "Ganz weg damit (unwiderruflich): ${SUDO}rm -rf $DATA_PATH && ${SUDO}userdel fragdesk"
  say "Caddy bleibt installiert (${SUDO}apt remove caddy, falls nicht mehr gebraucht)."
}

cleanup() { if [ -n "$TMP" ]; then rm -rf "$TMP"; fi; }

main() {
  parse_args "$@"
  trap cleanup EXIT
  [ "$DRY" = 0 ] || say "Probelauf (--dry-run): es wird nichts geändert."
  if [ "$UNINSTALL" = 1 ]; then
    uninstall
    return 0
  fi
  step "FragDesk-Server installieren"
  preflight
  pick_domain
  pick_timezone
  install_packages
  create_user
  install_binary
  install_fetch
  write_fetch_units
  write_config
  write_unit
  write_caddy
  open_firewall
  start_service
  start_fetch
  check_https
  first_invite
  summary
}

# erst hier starten: ein abgebrochener Download (curl | bash) führt so nie ein halbes Skript aus
main "$@"

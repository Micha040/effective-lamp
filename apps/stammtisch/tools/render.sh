#!/bin/sh
# Oberfläche als Bilder zeichnen – ohne Handy, ohne Anmeldung: Die App startet mit Beispieldaten (?demo), Chrome
# zeichnet jede Seite ohne Fenster in eine PNG-Datei. Praktisch, um Änderungen am Aussehen zu prüfen.
#
# Aufruf: sh tools/render.sh [Ausgabeordner] [Seite …]
#   sh tools/render.sh                       alle Seiten, dunkel und hell, nach out/
#   sh tools/render.sh /tmp/bilder /chat     nur der Chat
# Braucht: python3 und Chrome oder Chromium. Umgebungsvariablen: THEMES="dark light", SIZE=390,844, NOMAP=1 (ohne Karten,
# z. B. wenn der Rechner keine Kartenbilder laden kann).
set -e
cd "$(dirname "$0")/.."
OUT="${1:-out}"
[ $# -gt 0 ] && shift
PAGES="${*:-/ /chat /bilder /einstellungen /verlauf /treffen/neu /archiv /orte}"
THEMES="${THEMES:-dark light}"
SIZE="${SIZE:-390,844}"
PORT="${PORT:-8791}"

CHROME=""
for c in google-chrome chromium chromium-browser chrome; do
  if command -v "$c" >/dev/null 2>&1; then CHROME="$c"; break; fi
done
[ -n "$CHROME" ] || { echo "Chrome oder Chromium fehlt."; exit 1; }

mkdir -p "$OUT"
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory web >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 1

EXTRA="&noscroll"   # Chrome ohne Fenster zeichnet gescrollte Seiten falsch: der Chat bleibt für das Bild oben stehen
[ -n "$NOMAP" ] && EXTRA="$EXTRA&nomap"
for theme in $THEMES; do
  for page in $PAGES; do
    name=$(echo "$page" | sed 's#^/##; s#/#-#g')
    [ -n "$name" ] || name="treffen"
    "$CHROME" --headless=new --no-sandbox --hide-scrollbars --window-size="$SIZE" --force-device-scale-factor=2 \
      --virtual-time-budget=6000 --screenshot="$OUT/$name-$theme.png" \
      "http://127.0.0.1:$PORT/index.html?demo&theme=$theme$EXTRA#$page" >/dev/null 2>&1
    echo "geschrieben: $OUT/$name-$theme.png"
  done
done

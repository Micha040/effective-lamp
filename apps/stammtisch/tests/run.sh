#!/bin/sh
# Tests für STAMMTISCH (laufen unter Linux, auch in GitHub Actions):
#   1. Hilfsfunktionen und Demo-Anbindung der Web-App            (Node.js ≥ 18)
#   2. passen die Dateien zusammen? keine geheimen Schlüssel?     (Node.js)
#   3. Datenbank: Zugriffsregeln je Gruppe, Verlauf, Papierkorb   (PostgreSQL ≥ 15, eigener Wegwerf-Server)
# Aufruf: sh tests/run.sh
set -e
cd "$(dirname "$0")/.."
if command -v node >/dev/null 2>&1; then
  node --test tests/*.test.js
  node tests/check.mjs
else
  echo "Hinweis: node fehlt – Tests der Web-App übersprungen"
fi
sh tests/db.sh

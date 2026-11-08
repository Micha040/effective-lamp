#!/bin/sh
# fragdesk-fetch bauen: npm ci (genau die Versionen aus package-lock.json, ohne Installationsskripte), dann esbuild →
#   <ausgabeordner>/fragdesk-fetch.js           eine Datei für Node ≥ 18 (läuft allein, ohne node_modules)
#   <ausgabeordner>/fragdesk-fetch-NOTICES.txt  Lizenztexte aller gebündelten Pakete
# Die Version kommt aus apps/fragdesk/app.json (wie beim Server).
# Aufruf (aus dem Repo): sh apps/fragdesk/server/fetch/build.sh <ausgabeordner>
set -e
if [ -z "$1" ]; then
  echo "Aufruf: sh build.sh <ausgabeordner>" >&2
  exit 2
fi
mkdir -p "$1"
OUT=$(cd "$1" && pwd)
cd "$(dirname "$0")"
command -v node >/dev/null || { echo "Fehler: node fehlt (Node.js 18 oder neuer)" >&2; exit 1; }
command -v npm >/dev/null || { echo "Fehler: npm fehlt" >&2; exit 1; }
npm ci --no-audit --no-fund --ignore-scripts --loglevel=error
node build.js "$OUT"

#!/bin/sh
# Baut alles, was kompiliert werden muss: DeskCats, SnowDesk, FragDesk und den Launcher (MinGW-w64 unter Linux,
# für FragDesk zusätzlich Go) sowie fragdesk-server (Linux x86_64, statisch) und für „Demos automatisch laden“ auf dem
# eigenen Server fdanalyze für Linux (Go) und den Helfer fragdesk-fetch (Node.js, npm). TriggerSwitch und WaveMuteSync
# werden erst auf dem PC kompiliert (Quelltext + Setup).
set -e
cd "$(dirname "$0")/.."
for d in apps/deskcats apps/snowdesk apps/fragdesk launcher; do
  echo "== $d"
  sh "$d/build.sh"
done
echo "== apps/fragdesk/server"
sh apps/fragdesk/server/build.sh
echo "== apps/fragdesk: fdanalyze für Linux"
sh apps/fragdesk/build.sh analyzer-linux
if [ -f apps/fragdesk/server/fetch/build.sh ]; then
  echo "== apps/fragdesk/server/fetch (fragdesk-fetch)"
  sh apps/fragdesk/server/fetch/build.sh apps/fragdesk/build
else
  echo "== apps/fragdesk/server/fetch fehlt noch – fragdesk-fetch wird nicht gebaut"
fi

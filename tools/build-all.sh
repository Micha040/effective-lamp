#!/bin/sh
# Baut alles, was kompiliert werden muss: DeskCats, SnowDesk, FragDesk und den Launcher (MinGW-w64 unter Linux,
# für FragDesk zusätzlich Go). TriggerSwitch und WaveMuteSync werden erst auf dem PC kompiliert (Quelltext + Setup).
set -e
cd "$(dirname "$0")/.."
for d in apps/deskcats apps/snowdesk apps/fragdesk launcher; do
  echo "== $d"
  sh "$d/build.sh"
done

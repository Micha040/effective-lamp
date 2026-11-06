#!/bin/sh
# Baut alles, was kompiliert werden muss: DeskCats, SnowDesk und den Launcher (MinGW-w64 unter Linux).
# TriggerSwitch und WaveMuteSync werden erst auf dem PC kompiliert (Quelltext + Setup) – hier nichts zu tun.
set -e
cd "$(dirname "$0")/.."
for d in apps/deskcats apps/snowdesk launcher; do
  echo "== $d"
  sh "$d/build.sh"
done

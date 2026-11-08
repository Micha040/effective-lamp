#!/bin/sh
# Baut fragdesk-server für Linux x86_64 (statisch gelinkt, ohne Fremdbibliotheken) nach build/fragdesk-server-linux-x64.
# Die Handy-App (web/) wird eingebettet, die Version kommt aus app.json (wie bei FragDesk).
# Aufruf (aus dem Repo oder apps/fragdesk): sh apps/fragdesk/server/build.sh
set -e
cd "$(dirname "$0")/.."
OUT=build/server
mkdir -p $OUT/obj
VERSION=$(python3 -c 'import json; print(json.load(open("app.json", encoding="utf-8"))["version"])')
python3 tools/make_web.py --cpp $OUT/web_assets.cpp >/dev/null
export FD_CXX="${CXX:-g++}" FD_OUT=$OUT
export FD_FLAGS="-std=c++17 -O2 -Wall -Wextra -pthread -Iserver -DFD_SERVER_VERSION=\"$VERSION\""
SRCS="src/core/json.cpp src/core/model.cpp src/core/stats.cpp src/core/format.cpp src/core/radar.cpp src/core/heat.cpp
  src/core/replay.cpp src/api/http.cpp src/api/mobile.cpp src/api/view_common.cpp src/api/view_match.cpp src/api/view_matches.cpp
  src/api/view_moments.cpp src/api/view_overview.cpp src/api/view_stats.cpp src/api/view_replay.cpp server/sha256.cpp server/files.cpp server/store.cpp
  server/source.cpp server/app.cpp server/main.cpp $OUT/web_assets.cpp"
# parallel übersetzen; Objektnamen mit Ordner, damit gleiche Dateinamen nicht kollidieren
# shellcheck disable=SC2016   # wird erst in sh -c ausgewertet
for f in $SRCS; do echo "$f"; done | xargs -P "$(nproc 2>/dev/null || echo 2)" -I{} sh -c \
  'o="$FD_OUT/obj/$(echo "{}" | tr / _ | sed "s/\.cpp$/.o/")"; $FD_CXX $FD_FLAGS -c "{}" -o "$o" || exit 255'
OBJS=""
for f in $SRCS; do OBJS="$OBJS $OUT/obj/$(echo "$f" | tr / _ | sed 's/\.cpp$/.o/')"; done
# shellcheck disable=SC2086   # OBJS: eine Datei pro Wort
$FD_CXX -static -pthread -s -o build/fragdesk-server-linux-x64 $OBJS
ls -la build/fragdesk-server-linux-x64

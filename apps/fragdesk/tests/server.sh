#!/bin/sh
# Tests für fragdesk-server (Linux): baut den Server wie fürs Release (server/build.sh), dann
#   server_test      Einzelteile (SHA-256, Konten, Dateien, Kontingent, Kopplung, Ansichten pro Konto)
#   server_e2e.py    der ganze Ablauf über HTTP mit dem fertigen Programm (freier Port 7451–7459 oder FD_TEST_PORT)
#   install_test.sh  install.sh in einem Schein-Wurzelordner (Installieren, Aktualisieren, --dry-run)
# Testdaten: FD_TEST_DATA=<Ordner mit matches/*.json, clips.json und videos/> (z. B. von make_testdata.py) –
# ohne erzeugt server_e2e.py kleine künstliche Matches und Videos.
# FD_TEST_DEMO=<CS2-Demo>: der nachgebaute Helfer (Demos automatisch laden) wertet diese Demo wirklich aus – mit
# build/fdanalyze-linux-x64 (sh build.sh analyzer-linux); ohne lädt er ein künstliches Match hoch.
# Aufruf: sh tests/server.sh   (läuft auch am Ende von tests/run.sh)
set -e
cd "$(dirname "$0")/.."
sh server/build.sh >/dev/null
mkdir -p build/tests
OBJS=""
for o in build/server/obj/*.o; do [ "$o" = build/server/obj/server_main.o ] || OBJS="$OBJS $o"; done
# shellcheck disable=SC2086
g++ -std=c++17 -O1 -Wall -Wextra -pthread -Iserver -o build/tests/server_test tests/server_test.cpp $OBJS
TMP=${TMPDIR:-/tmp}/fragdesk-server-tests
rm -rf "$TMP"
mkdir -p "$TMP"
DATA=$FD_TEST_DATA
if [ -z "$DATA" ]; then
  DATA=$TMP/data
  python3 tests/server_e2e.py --make-data "$DATA"
fi
./build/tests/server_test "$TMP/unit" "$DATA/matches"
if [ -n "$FD_TEST_DEMO" ] && [ -x build/fdanalyze-linux-x64 ]; then
  python3 tests/server_e2e.py --bin build/fragdesk-server-linux-x64 --data "$DATA" --tmp "$TMP/e2e" --analyzer build/fdanalyze-linux-x64 --demo "$FD_TEST_DEMO"
else
  python3 tests/server_e2e.py --bin build/fragdesk-server-linux-x64 --data "$DATA" --tmp "$TMP/e2e"
fi
sh tests/install_test.sh "$TMP/install"

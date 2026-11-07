#!/bin/sh
# Tests für den plattformunabhängigen Teil von FragDesk (Linux, g++).
# Aufruf: sh tests/run.sh [Ordner mit Ausgaben von fdanalyze]
set -e
cd "$(dirname "$0")/.."
mkdir -p build/tests
g++ -std=c++17 -O1 -Wall -Wextra -o build/tests/core_test tests/core_test.cpp src/core/json.cpp src/core/model.cpp \
  src/core/stats.cpp src/core/format.cpp src/core/vdf.cpp src/core/replay.cpp src/core/radar.cpp src/core/heat.cpp -lpthread
./build/tests/core_test "$@"
# Handy-App: Schnittstelle und Server
g++ -std=c++17 -O1 -Wall -Wextra -o build/tests/api_test tests/api_test.cpp src/api/http.cpp src/api/mobile.cpp src/api/view_*.cpp \
  src/core/json.cpp src/core/model.cpp src/core/stats.cpp src/core/format.cpp src/core/radar.cpp src/core/heat.cpp src/core/replay.cpp -lpthread
./build/tests/api_test
# Eigener Server (fragdesk-server): Einzelteile, ganzer Ablauf über HTTP, install.sh
sh tests/server.sh

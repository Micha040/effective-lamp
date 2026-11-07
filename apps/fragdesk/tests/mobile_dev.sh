#!/bin/sh
# Baut den Handy-Testserver (Linux) und startet ihn mit den angegebenen Argumenten.
# Aufruf: sh tests/mobile_dev.sh --data <Ordner mit matches/ und settings.json> [--port 7420] [--code 123456]
set -e
cd "$(dirname "$0")/.."
mkdir -p build/tests
SRC="tests/mobile_dev.cpp src/api/http.cpp src/api/mobile.cpp src/api/view_*.cpp src/core/json.cpp src/core/model.cpp src/core/stats.cpp
  src/core/format.cpp src/core/radar.cpp src/core/heat.cpp src/core/replay.cpp"
# shellcheck disable=SC2086
g++ -std=c++17 -O1 -g -Wall -Wextra -o build/tests/mobile_dev $SRC -lpthread
[ "$1" = "--build-only" ] && exit 0
exec ./build/tests/mobile_dev --web web "$@"

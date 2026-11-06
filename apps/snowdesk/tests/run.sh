#!/bin/sh
# SnowDesk-Tests unter Linux (g++): Schnee-Simulation und "Echtes Wetter". Aufruf: ./tests/run.sh
set -e
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/snowdesk-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -o "$OUT/test_core" tests/test_core.cpp src/snowcore.cpp
g++ -std=c++17 -O2 -Wall -o "$OUT/weather_test" tests/weather_test.cpp src/weathercore.cpp
for t in test_core weather_test; do
  "$OUT/$t" > "$OUT/$t.txt" || { cat "$OUT/$t.txt"; exit 1; }
  printf "%s: %s\n" "$t" "$(tail -n 1 "$OUT/$t.txt")"
done

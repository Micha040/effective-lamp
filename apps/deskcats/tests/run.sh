#!/bin/sh
# Schnelle DeskCats-Tests unter Linux (g++): Dauertest mit drei Monitoren und sechs Startwerten.
# Aufruf: ./tests/run.sh
set -e
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/deskcats-tests
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Wall -o "$OUT/multimon" tests/multimon.cpp src/core/brain.cpp src/core/world.cpp src/core/cat.cpp src/core/anim.cpp src/core/props.cpp
for seed in 1 2 3 4 5 6; do
  "$OUT/multimon" "$seed" > "$OUT/multimon.txt" || { cat "$OUT/multimon.txt"; exit 1; }
  printf "Startwert %s: %s\n" "$seed" "$(tail -n 1 "$OUT/multimon.txt")"
done

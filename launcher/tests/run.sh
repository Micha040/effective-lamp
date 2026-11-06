#!/bin/sh
# Kern-Tests des Launchers unter Linux: ./tests/run.sh [catalog.json]
set -e
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/efficient-lamp-tests
mkdir -p "$OUT"
python3 make_fixtures.py "$OUT/data" >/dev/null
g++ -std=c++17 -O2 -Wall -Wextra -o "$OUT/test_core" test_core.cpp ../src/core/json.cpp ../src/core/sha256.cpp ../src/core/zip.cpp ../src/core/catalog.cpp
"$OUT/test_core" "$OUT/data" "$@"

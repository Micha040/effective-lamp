#!/bin/sh
# Baut EfficientLamp.exe (MinGW-w64, x64, statisch gelinkt). Aufruf: ./build.sh
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT/obj
CXX=x86_64-w64-mingw32-g++
WINDRES=x86_64-w64-mingw32-windres
FLAGS="-O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"

$WINDRES -c 65001 -I res res/launcher.rc -O coff -o $OUT/obj/launcher_res.o
OBJS="$OUT/obj/launcher_res.o"
for f in src/core/*.cpp src/win/*.cpp; do
  o=$OUT/obj/$(basename "$f" .cpp).o
  $CXX -std=c++17 $FLAGS -municode -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
$CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -s -o $OUT/EfficientLamp.exe $OBJS \
  -lgdiplus -lwinhttp -ldwmapi -lshlwapi -lole32 -luuid -lshell32 -lversion -ladvapi32 -lgdi32 -luser32
ls -la $OUT/EfficientLamp.exe

#!/bin/sh
# Baut DeskCats.exe und DeskCatsMenu.dll (MinGW-w64, x64, statisch gelinkt). Aufruf: ./build.sh
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT/obj
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres
FLAGS="-O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"

$WINDRES -c 65001 -I res res/deskcats.rc -O coff -o $OUT/obj/deskcats_res.o
OBJS="$OUT/obj/deskcats_res.o"
for f in src/core/cat.cpp src/core/catdraw_cpu.cpp src/core/pixcat.cpp src/core/anim.cpp src/core/world.cpp src/core/brain.cpp src/core/props.cpp src/win/*.cpp; do
  o=$OUT/obj/$(basename "$f" .cpp).o
  $CXX -std=c++17 $FLAGS -municode -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
$CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -s -o $OUT/DeskCats.exe $OBJS \
  -ldwmapi -lshcore -lole32 -loleaut32 -luuid -lshlwapi -lurlmon -lwtsapi32 -lcomctl32 -lpsapi -lgdi32 -luser32 -lshell32 -ladvapi32

$WINDRES -c 65001 -I res res/menu.rc -O coff -o $OUT/obj/menu_res.o
$CC -std=c11 $FLAGS -shared -static-libgcc -s -o $OUT/DeskCatsMenu.dll src/menu/menu.c src/menu/menu.def $OUT/obj/menu_res.o \
  -lole32 -lshell32 -lshlwapi -luser32 -ladvapi32 -luuid
ls -la $OUT/DeskCats.exe $OUT/DeskCatsMenu.dll

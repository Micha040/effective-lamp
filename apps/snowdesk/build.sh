#!/bin/sh
# Baut SnowDesk.exe und SnowDeskMenu.dll (MinGW-w64, x64, statisch gelinkt). Aufruf: ./build.sh
set -e
cd "$(dirname "$0")"
OUT=build
mkdir -p $OUT/obj
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres
FLAGS="-O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"

$WINDRES -c 65001 -I res res/snowdesk.rc -O coff -o $OUT/obj/snowdesk_res.o
OBJS="$OUT/obj/snowdesk_res.o"
for f in src/snowcore.cpp src/weathercore.cpp src/app/*.cpp; do
  o=$OUT/obj/$(basename "$f" .cpp).o
  $CXX -std=c++17 $FLAGS -municode -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
$CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -s -o $OUT/SnowDesk.exe $OBJS \
  -ld3d11 -ldxgi -ldcomp -ld3dcompiler_47 -ldwmapi -lshcore -lgdiplus -lole32 -loleaut32 -luuid -lshlwapi -lurlmon \
  -lwtsapi32 -lwinmm -luxtheme -lwinhttp -lgdi32 -luser32 -lshell32 -ladvapi32

$WINDRES -c 65001 -I res res/menu.rc -O coff -o $OUT/obj/menu_res.o
$CC -std=c11 $FLAGS -shared -static-libgcc -s -o $OUT/SnowDeskMenu.dll src/menu/menu.c src/menu/menu.def $OUT/obj/menu_res.o \
  -lole32 -lshell32 -lshlwapi -luser32 -ladvapi32 -luuid
ls -la $OUT/SnowDesk.exe $OUT/SnowDeskMenu.dll

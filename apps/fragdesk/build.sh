#!/bin/sh
# Baut FragDesk.exe (MinGW-w64, x64, statisch gelinkt) und fdanalyze.exe (Go, für Windows übersetzt).
# Aufruf: ./build.sh            beides bauen
#         ./build.sh app        nur FragDesk.exe
#         ./build.sh analyzer   nur fdanalyze.exe
set -e
cd "$(dirname "$0")"
OUT=build
WHAT=${1:-all}
mkdir -p $OUT/obj

if [ "$WHAT" = all ] || [ "$WHAT" = app ]; then
  CXX=x86_64-w64-mingw32-g++
  WINDRES=x86_64-w64-mingw32-windres
  export FD_FLAGS="-std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -municode"
  export FD_CXX=$CXX FD_OUT=$OUT
  python3 tools/make_web.py >/dev/null   # Handy-App (web/) als Ressourcen
  $WINDRES -c 65001 -I res res/fragdesk.rc -O coff -o $OUT/obj/fragdesk_res.o
  # parallel übersetzen; xargs meldet einen Fehler, sobald eine Datei nicht durchgeht
  ls src/core/*.cpp src/api/*.cpp src/third_party/*.cpp src/win/*.cpp | xargs -P "$(nproc 2>/dev/null || echo 2)" -I{} sh -c \
    'o="$FD_OUT/obj/$(basename "{}" .cpp).o"; $FD_CXX $FD_FLAGS -c "{}" -o "$o" || exit 255'
  OBJS="$OUT/obj/fragdesk_res.o"
  for f in src/core/*.cpp src/api/*.cpp src/third_party/*.cpp src/win/*.cpp; do OBJS="$OBJS $OUT/obj/$(basename "$f" .cpp).o"; done
  $CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -s -o $OUT/FragDesk.exe $OBJS \
    -lgdiplus -ldwmapi -lshcore -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -lcomctl32 -lcomdlg32 \
    -ld3d11 -ldxgi -lmfplat -lmfreadwrite -lmfuuid -lwindowscodecs -liphlpapi -lbcrypt -lws2_32 -lgdi32 -luser32 -ladvapi32 -lwinmm
  ls -la $OUT/FragDesk.exe
fi

if [ "$WHAT" = all ] || [ "$WHAT" = analyzer ]; then
  command -v go >/dev/null || { echo "FEHLER: Go fehlt (https://go.dev/dl/, Version laut analyzer/go.mod)" >&2; exit 1; }
  cd analyzer
  # Ohne go.sum (frisch ausgecheckt): Prüfsummen holen – Go gleicht sie mit sum.golang.org ab
  if [ ! -f go.sum ] && [ -z "$GOFLAGS" ]; then go mod download && go mod tidy; fi
  CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -trimpath -ldflags="-s -w -buildid=" -o ../$OUT/fdanalyze.exe .
  cd ..
  # Lizenztexte aller eingebauten Go-Module (liegt dem Paket bei)
  python3 tools/notices.py $OUT/THIRD-PARTY-NOTICES.txt
  ls -la $OUT/fdanalyze.exe
fi

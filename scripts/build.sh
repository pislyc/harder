#!/bin/bash
# Build HARDER. usage: scripts/build.sh linux|windows
# Windows is cross-compiled with MinGW-w64 (posix threads), fully static: no runtime DLLs needed.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$1" in
  linux)
    make -C "$ROOT/plugins/Harder" -j2 ;;
  windows)
    W=/tmp/harder-win-build
    rm -rf $W && mkdir -p $W && (cd "$ROOT" && tar cf - --exclude=./bin --exclude=./build --exclude="./bin-*" --exclude=./dist .) | (cd $W && tar xf -)
    rm -rf $W/dpf/build
    CXXFLAGS="-DDPF_WIN_SAVE_DEFAULT_NAME" make -C $W/plugins/Harder -j2 WINDOWS=true WINDOWS_ICON_ID=1 $EXTRA CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix AR=x86_64-w64-mingw32-ar
    rm -rf "$ROOT/bin-windows" && mkdir -p "$ROOT/bin-windows" && cp -r $W/bin/HARDER.vst3 $W/bin/HARDER.exe "$ROOT/bin-windows/" ;;
  *) echo "usage: $0 linux|windows"; exit 1 ;;
esac

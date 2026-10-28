#!/bin/bash
# Build HARDER. usage: scripts/build.sh linux|windows|macos
# Windows is cross-compiled with MinGW-w64 (posix threads), fully static: no runtime DLLs needed.
# macOS runs on a Mac (Xcode command line tools): HARDER.app, Universal (arm64 + x86_64), macOS 10.15 or later,
# ad-hoc signed. MACOS_ARCHS="-arch arm64" builds one architecture only.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$1" in
  linux)
    make -C "$ROOT/plugins/Harder" -j2 ;;
  windows)
    W=/tmp/harder-win-build
    rm -rf $W && mkdir -p $W && (cd "$ROOT" && tar cf - --exclude=./bin --exclude=./build --exclude="./bin-*" --exclude=./dist .) | (cd $W && tar xf -)
    rm -rf $W/dpf/build
    CXXFLAGS="-DDPF_WIN_SAVE_DEFAULT_NAME $HARDER_CXXFLAGS" make -C $W/plugins/Harder -j2 WINDOWS=true WINDOWS_ICON_ID=1 $EXTRA CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix AR=x86_64-w64-mingw32-ar
    rm -rf "$ROOT/bin-windows" && mkdir -p "$ROOT/bin-windows" && cp -r $W/bin/HARDER.vst3 $W/bin/HARDER.exe "$ROOT/bin-windows/" ;;
  macos)
    V=$(awk '/define HARDER_VER_MAJOR/{a=$3} /define HARDER_VER_MINOR/{b=$3} /define HARDER_VER_PATCH/{c=$3} END{print a"."b"."c}' "$ROOT/plugins/Harder/HarderShared.hpp")
    ARCHS=${MACOS_ARCHS:-"-arch arm64 -arch x86_64"}
    MF="$ARCHS -mmacosx-version-min=10.15 -DMAC_OS_X_VERSION_MIN_REQUIRED=MAC_OS_X_VERSION_10_15 -DMAC_OS_X_VERSION_MAX_ALLOWED=MAC_OS_X_VERSION_10_15"
    rm -rf "$ROOT/build" "$ROOT/bin/HARDER.app" "$ROOT/dpf/build"
    # the defines also reach DGL (built by its own make): app menu / Quit, the save panel's suggested name
    CFLAGS="$MF" CXXFLAGS="$MF -DDPF_MACOS_APP_MENU -DDPF_MAC_SAVE_DEFAULT_NAME $HARDER_CXXFLAGS" \
      make -C "$ROOT/plugins/Harder" -j"$(sysctl -n hw.ncpu)" PKG_CONFIG=/usr/bin/false PKG_CONFIG_PATH=/NOT
    A=$ROOT/bin/HARDER.app; C=$A/Contents
    mkdir -p "$C/Resources" && cp "$ROOT/plugins/Harder/res/harder.icns" "$C/Resources/harder.icns"
    sed -e "s/@VERSION@/$V/g" "$ROOT/plugins/Harder/res/Info.plist" > "$C/Info.plist"
    printf 'APPL????' > "$C/PkgInfo"
    codesign --force --deep --sign - "$A"
    rm -rf "$ROOT/bin-macos" && mkdir -p "$ROOT/bin-macos" && ditto "$A" "$ROOT/bin-macos/HARDER.app"
    lipo -archs "$ROOT/bin-macos/HARDER.app/Contents/MacOS/HARDER" ;;
  *) echo "usage: $0 linux|windows|macos"; exit 1 ;;
esac

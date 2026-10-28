#!/bin/bash
# Packages the macOS standalone (run on the Mac after scripts/build.sh macos): dist/HARDER-<v>-macos.zip
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); D=$ROOT/dist
V=$(awk '/define HARDER_VER_MAJOR/{a=$3} /define HARDER_VER_MINOR/{b=$3} /define HARDER_VER_PATCH/{c=$3} END{print a"."b"."c}' "$ROOT/plugins/Harder/HarderShared.hpp")
P=$D/HARDER-$V-macos; rm -rf "$P" "$P.zip" && mkdir -p "$P/licenses" "$P/docs"
ditto "$ROOT/bin-macos/HARDER.app" "$P/HARDER.app"
cp "$ROOT/INSTALL-macOS.txt" "$ROOT/INSTALL.txt" "$ROOT/VERIFY.md" "$P/"
cp "$ROOT/docs/LIBRARY.md" "$P/docs/"
cp "$ROOT/LICENSE" "$P/LICENSE.txt"
cp "$ROOT"/fonts/OFL-*.txt "$P/licenses/"
cp "$ROOT/dpf/LICENSE" "$P/licenses/DPF-LICENSE.txt"
cp "$ROOT/plugins/Harder/hmath/COPYRIGHT.musl" "$P/licenses/musl-COPYRIGHT.txt"
cp "$ROOT/dpf/distrho/src/jackbridge/rtaudio/LICENSE" "$P/licenses/RtAudio-LICENSE.txt"
cp "$ROOT/dpf/distrho/src/jackbridge/rtmidi/LICENSE" "$P/licenses/RtMidi-LICENSE.txt"
codesign --verify --deep --strict "$P/HARDER.app"
(cd "$D" && ditto -c -k --sequesterRsrc --keepParent "HARDER-$V-macos" "HARDER-$V-macos.zip")
(cd "$D" && shasum -a 256 "HARDER-$V-macos.zip")

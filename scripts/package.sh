#!/bin/bash
# Packages the Windows plugin and the source.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); V=1.3.0; D=$ROOT/dist
rm -rf $D && mkdir -p $D/HARDER-$V-win64/licenses
cp -r $ROOT/bin-windows/HARDER.vst3 $ROOT/bin-windows/HARDER.exe $D/HARDER-$V-win64/
cp $ROOT/INSTALL.txt $ROOT/VERIFY.md $D/HARDER-$V-win64/
mkdir -p $D/HARDER-$V-win64/docs && cp $ROOT/docs/LIBRARY.md $D/HARDER-$V-win64/docs/
cp $ROOT/LICENSE $D/HARDER-$V-win64/LICENSE.txt
cp $ROOT/fonts/OFL-*.txt $D/HARDER-$V-win64/licenses/
cp $ROOT/dpf/LICENSE $D/HARDER-$V-win64/licenses/DPF-LICENSE.txt
cp $ROOT/plugins/Harder/hmath/COPYRIGHT.musl $D/HARDER-$V-win64/licenses/musl-COPYRIGHT.txt
cp $ROOT/dpf/distrho/src/jackbridge/rtaudio/LICENSE $D/HARDER-$V-win64/licenses/RtAudio-LICENSE.txt
cp $ROOT/dpf/distrho/src/jackbridge/rtmidi/LICENSE $D/HARDER-$V-win64/licenses/RtMidi-LICENSE.txt
(cd $D && zip -qr HARDER-$V-win64.zip HARDER-$V-win64)
# source (without build output; dpf without its git history)
S=$D/harder-vst-src-$V; mkdir -p $S
(cd $ROOT && tar cf - --exclude=./bin --exclude=./build --exclude="./bin-*" --exclude=./dist --exclude=./dpf/.git --exclude=./dpf/build --exclude="*/.git" --exclude=./.gitmodules .) | (cd $S && tar xf -)
(cd $D && zip -qr harder-vst-src-$V.zip harder-vst-src-$V)
ls -la $D/*.zip

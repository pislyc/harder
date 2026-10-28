#!/bin/bash
# builds the test tools (native) against the same engine + private libm as the plugin
# usage: tools/build_tools.sh [outdir]   env: CC, CXX, AR (compilers), TOOLS_ARCH (extra flags, e.g. "-arch arm64"),
#        NO_VST3_HOST=1 (skip the VST3 host test, e.g. on macOS where only the standalone is built)
set -e
T=$(cd "$(dirname "$0")" && pwd); H=$T/../plugins/Harder/hmath; O=${1:-/home/claude/work}
CC=${CC:-gcc}; CXX=${CXX:-g++}; AR=${AR:-ar}; A=${TOOLS_ARCH:-}; FSL=
[ "$(uname)" = Darwin ] && FSL="-framework Carbon -framework CoreFoundation -lobjc"   # FileSys.cpp: key names
mkdir -p $O/hm && for f in $H/hm*.c; do $CC $A -std=gnu99 -O3 -fno-math-errno -ffp-contract=off -I$H -I$H/src -c $f -o $O/hm/$(basename $f).o; done
rm -f $O/libhmath.a && $AR rcs $O/libhmath.a $O/hm/*.o
F="$A -std=gnu++17 -O3 -fno-math-errno -ffp-contract=off"
$CXX $F -o $O/render $T/render.cpp $O/libhmath.a
$CXX $F -o $O/stress $T/stress.cpp $O/libhmath.a
$CXX $F -pthread -o $O/core_test $T/core_test.cpp $O/libhmath.a
$CXX $A -std=gnu++17 -O1 -o $O/gen_test $T/gen_test.cpp $O/libhmath.a
$CXX $F -pthread -o $O/preset_path_test $T/preset_path_test.cpp $O/libhmath.a
$CXX $F -pthread -I$T/../plugins/Harder -o $O/host_echo_test $T/host_echo_test.cpp $O/libhmath.a
$CXX $F -o $O/preview_test $T/preview_test.cpp $O/libhmath.a
$CXX $F -o $O/sync_test $T/sync_test.cpp $O/libhmath.a
[ -n "$NO_VST3_HOST" ] || $CXX $F -I$T/../dpf/distrho/src -o $O/vst3_host_test $T/vst3_host_test.cpp $O/libhmath.a -ldl
$CXX $F -o $O/export_test $T/export_test.cpp $O/libhmath.a
$CXX $F -o $O/live_expect $T/live_expect.cpp $O/libhmath.a
# 2.0 / library / parsers (also built with sanitizers for the release checks, see README.md)
$CXX $A -std=gnu++17 -O1 -pthread -o $O/sample_test $T/sample_test.cpp $T/../plugins/Harder/FileSys.cpp $O/libhmath.a $FSL
$CXX $A -std=gnu++17 -O1 -pthread -o $O/v2_test $T/v2_test.cpp $T/../plugins/Harder/FileSys.cpp $O/libhmath.a $FSL
$CXX $A -std=gnu++17 -O1 -pthread -o $O/fuzz_parsers $T/fuzz_parsers.cpp $T/../plugins/Harder/FileSys.cpp $O/libhmath.a $FSL
$CXX $A -std=gnu++17 -o $O/control_test $T/control_test.cpp
echo tools built in $O

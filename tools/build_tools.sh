#!/bin/bash
# builds the test tools (native) against the same engine + private libm as the plugin
set -e
T=$(cd "$(dirname "$0")" && pwd); H=$T/../plugins/Harder/hmath; O=${1:-/home/claude/work}
mkdir -p $O/hm && for f in $H/hm*.c; do gcc -std=gnu99 -O3 -fno-math-errno -ffp-contract=off -I$H -I$H/src -c $f -o $O/hm/$(basename $f).o; done
ar rcs $O/libhmath.a $O/hm/*.o
F="-std=gnu++17 -O3 -fno-math-errno -ffp-contract=off"
g++ $F -o $O/render $T/render.cpp $O/libhmath.a
g++ $F -o $O/stress $T/stress.cpp $O/libhmath.a
g++ $F -pthread -o $O/core_test $T/core_test.cpp $O/libhmath.a
g++ -std=gnu++17 -O1 -o $O/gen_test $T/gen_test.cpp $O/libhmath.a
g++ $F -pthread -o $O/preset_path_test $T/preset_path_test.cpp $O/libhmath.a
g++ $F -pthread -I$T/../plugins/Harder -o $O/host_echo_test $T/host_echo_test.cpp $O/libhmath.a
g++ $F -o $O/preview_test $T/preview_test.cpp $O/libhmath.a
g++ $F -o $O/sync_test $T/sync_test.cpp $O/libhmath.a
g++ $F -I$T/../dpf/distrho/src -o $O/vst3_host_test $T/vst3_host_test.cpp $O/libhmath.a -ldl
g++ $F -o $O/export_test $T/export_test.cpp $O/libhmath.a
g++ $F -o $O/live_expect $T/live_expect.cpp $O/libhmath.a
echo tools built in $O

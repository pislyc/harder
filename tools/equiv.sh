#!/bin/bash
# Engine equivalence on this machine. The C++ engine (tools/render) renders 12 scenarios at 44.1 / 48 / 96 kHz.
#  * with the web build ($HARDER_WEB, see README): the scenarios and the web engine's (JS) renders come from
#    tools/scen.js, and every C++ render must be within 1e-6 of the JS one (5.96e-8, one float LSB, since 1.0);
#  * without it (CI): the scenarios come from tools/expected/scenarios.tar.gz.
# Given a list of expected SHA-256s (tools/expected/render-sha256.txt, from the Linux x86-64 build, which passes the
# JS comparison), every render must also be bit-identical to it.
# usage: tools/equiv.sh <render binary> <work dir> [expected sha256 list]   env RUN: a runner prefix (qemu-aarch64)
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$1; W=$2; EXP=$3
[ -x "$R" ] && [ -n "$W" ] || { echo "usage: $0 <render> <workdir> [expected]"; exit 2; }
if command -v sha256sum >/dev/null; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
WEB=${HARDER_WEB:-/home/claude/strobe}; JS=0; [ -f "$WEB/engine.js" ] && command -v node >/dev/null && JS=1
mkdir -p "$W"; : > "$W/sha256.txt"; : > "$W/compare.txt"; fail=0
[ $JS = 1 ] || [ -f "$W/.unpacked" ] || { tar xzf "$T/expected/scenarios.tar.gz" -C "$W" && touch "$W/.unpacked"; }
for sr in 44100 48000 96000; do
  D="$W/s$sr"
  [ $JS = 0 ] || [ -f "$D/.done" ] || { node "$T/scen.js" "$D" $sr > /dev/null && touch "$D/.done"; }
  for j in "$D"/*.json; do
    n=$(basename "$j" .json)
    NO_BLOCK_TIMING=1 $RUN "$R" "$j" "$D/$n.cpp.f32" > /dev/null
    (cd "$W" && $SHA "s$sr/$n.cpp.f32") >> "$W/sha256.txt"
    if [ -f "$D/$n.js.f32" ]; then
      c=$(node "$T/compare.js" "$D/$n.js.f32" "$D/$n.cpp.f32" $sr "$sr/$n"); echo "$c" >> "$W/compare.txt"
      node -e 'const r=JSON.parse(process.argv[1]); if(!(r.maxAbsDiff<=1e-6)) { console.log("JS MISMATCH", r.name, r.maxAbsDiff); process.exit(1); }' "$c" || fail=1
    fi
  done
done
N=$(wc -l < "$W/sha256.txt" | tr -d ' ')
if [ -s "$W/compare.txt" ]; then
  MAX=$(node -e 'const l=require("fs").readFileSync(process.argv[1],"utf8").trim().split("\n").map(JSON.parse); console.log(Math.max(...l.map(r=>r.maxAbsDiff)).toExponential(2))' "$W/compare.txt")
  echo "renders: $N, max difference vs the web engine: $MAX"
else echo "renders: $N (no web build here: bit-identity check only)"; fi
if [ -n "$EXP" ]; then
  E=$(wc -l < "$EXP" | tr -d ' ')
  same=$(awk 'NR==FNR{e[$2]=$1;next} e[$2]==$1{c++} END{print c+0}' "$EXP" "$W/sha256.txt")
  echo "bit-identical to the expected (Linux x86-64) renders: $same of $E"
  [ "$same" = "$E" ] || { awk 'NR==FNR{e[$2]=$1;next} e[$2]!=$1{print "  differs: " $2}' "$EXP" "$W/sha256.txt"; fail=1; }
fi
exit $fail

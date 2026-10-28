#!/bin/sh
# The SSE4.2 and AVX2 builds (QUARK_ISA v2 / v3, src/client/isa.h) must simulate bit for bit the same: a lobby mixes
# them, and prediction has to match the server. Builds quark_tests with -DQUARK_ISA=v3 into build-avx2 (needs an AVX2
# CPU to run) and compares quark_tests simhash (seeded FFAs, every tick's players and projectiles hashed) with build/.
# Usage: tools/isa_check.sh [seeds]     (run from the repo root, after a build). Exit code 0 = identical.
cd "$(dirname "$0")/.." || exit 2
seeds=${1:-3}
grep -q avx2 /proc/cpuinfo 2>/dev/null || { echo "isa check skipped: this CPU has no AVX2"; exit 0; }
cmake -S . -B build-avx2 -DCMAKE_BUILD_TYPE=Release -DQUARK_ISA=v3 -DQUARK_BUILD_CLIENT=OFF > /dev/null || exit 2
cmake --build build-avx2 -j --target quark_tests > /dev/null || exit 2
[ -e build-avx2/maps ] || ln -s ../maps build-avx2/maps   # (the tests load maps/ beside them, as in build/)
a=$(cd build && ./quark_tests simhash "$seeds") || exit 1
b=$(cd build-avx2 && ./quark_tests simhash "$seeds") || exit 1
if [ "$a" = "$b" ]; then echo "isa ok: SSE4.2 and AVX2 builds simulate the same ($seeds seeds)"; exit 0; fi
echo "isa FAILED: the AVX2 build simulates differently"; echo "SSE4.2:"; echo "$a"; echo "AVX2:"; echo "$b"; exit 1

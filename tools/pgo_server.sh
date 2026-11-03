#!/bin/sh
# The dedicated server's profile-guided build (the Linux release job, .github/workflows/release.yml).
#   1. an instrumented, server-only build (QUARK_PGO=GEN, no client);
#   2. its training runs: tools/pgo_train.sh (quark_tests serverbench over five maps / modes, simhash);
#   3. the same sources built with the profile (QUARK_PGO=USE): quark_server and quark_tests;
#   4. a check that the profiled build simulates exactly as the normal one: quark_tests simhash, 10 seeds, the same
#      hashes and kills as <normal build>'s. PGO only changes code layout and inlining, never the float maths
#      (the -fno-fast-math / -ffp-contract=off rules hold), so a difference is a bug: the release fails.
# Leaves <out>-use/quark_server. Server only: the client gains nothing measurable (GPU-bound) and a Windows build
# can't reuse a Linux profile (docs in CMakeLists.txt, QUARK_PGO). About 6 minutes on a GitHub runner.
# Measured (4-core VM, 10 interleaved serverbench pairs): server tick avg -3.7%, bot think -6%.
# usage: tools/pgo_server.sh <normal build dir, with quark_tests> <out prefix> [extra cmake args...]
cd "$(dirname "$0")/.." || exit 2
BASE=${1:?usage: tools/pgo_server.sh <normal build> <out prefix> [cmake args]}
OUT=${2:?usage: tools/pgo_server.sh <normal build> <out prefix> [cmake args]}
shift 2
PROF=$PWD/$OUT-profile
cmake -S . -B "$OUT-gen" -DCMAKE_BUILD_TYPE=Release -DQUARK_BUILD_CLIENT=OFF -DQUARK_PGO=GEN -DQUARK_PGO_DIR="$PROF" "$@" > /dev/null || exit 1
cmake --build "$OUT-gen" -j --target quark_tests || exit 1
sh tools/pgo_train.sh "$OUT-gen" || exit 1
cmake -S . -B "$OUT-use" -DCMAKE_BUILD_TYPE=Release -DQUARK_BUILD_CLIENT=OFF -DQUARK_PGO=USE -DQUARK_PGO_DIR="$PROF" "$@" > /dev/null || exit 1
cmake --build "$OUT-use" -j --target quark_server quark_tests || exit 1
"$BASE/quark_tests" simhash 10 | grep '^seed' > "$OUT-simhash-base.txt" || exit 1
"$OUT-use/quark_tests" simhash 10 | grep '^seed' > "$OUT-simhash-pgo.txt" || exit 1
[ "$(wc -l < "$OUT-simhash-base.txt")" -eq 10 ] || { echo "pgo: simhash printed no seeds"; exit 1; }
if ! cmp -s "$OUT-simhash-base.txt" "$OUT-simhash-pgo.txt"; then
  echo "pgo: the profiled server simulates DIFFERENTLY from the normal build:"
  diff "$OUT-simhash-base.txt" "$OUT-simhash-pgo.txt"
  exit 1
fi
echo "pgo: profiled quark_server simulates the same (10 seeds): $OUT-use/quark_server"

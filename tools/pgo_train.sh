#!/bin/sh
# Profile-guided optimisation training run (CMake QUARK_PGO, CMakeLists.txt). Runs a QUARK_PGO=GEN build's
# instrumented binaries over the hot paths, headless, and leaves the profile in its QUARK_PGO_DIR:
#   1. quark --benchmark: the shipped BENCHMARK demo played back (renderer, demo/snapshot decode, HUD)
#   2. quark --host ffa --bench: a hosted FFA with bots, a slow look around (client + listen server + bots, prediction)
#   3. quark_tests serverbench: the dedicated server's tick with bots and clients, five maps / modes
#   4. quark_tests simhash: seeded FFAs vs expert bots (movement, weapons, bots)
# The client's light bake is NOT trained: instrumented, its ray casts run 30x+ slower (the counters of 3 bake threads
# fight over cache lines; one thread still took over 10 minutes for Hamlet), so the two maps' light is baked first with
# a normal build ($PGO_BAKE_BUILD, default build/) into the runs' data folder, and the instrumented runs load it.
# About 6 minutes on a 4-core VM with software GL. Then build the same sources and flags with QUARK_PGO=USE:
#   cmake -S . -B build-pgo-gen -DQUARK_PGO=GEN -DQUARK_PGO_DIR=$PWD/pgo-profile && cmake --build build-pgo-gen -j
#   tools/pgo_train.sh build-pgo-gen
#   cmake -S . -B build-pgo-use -DQUARK_PGO=USE -DQUARK_PGO_DIR=$PWD/pgo-profile && cmake --build build-pgo-use -j
# usage: [PGO_BAKE_BUILD=<normal build dir>] tools/pgo_train.sh <GEN build dir> [seconds per client run, default 25]
# Needs a display for the client runs: uses $DISPLAY, else starts Xvfb :99 (left running).
cd "$(dirname "$0")/.." || exit 2
ROOT=$PWD
B=${1:?usage: tools/pgo_train.sh <GEN build dir> [seconds]}
SECS=${2:-25}
B=$(cd "$B" && pwd) || exit 2
grep -q '^QUARK_PGO:STRING=GEN' "$B/CMakeCache.txt" 2>/dev/null || { echo "pgo: $B is not a QUARK_PGO=GEN build"; exit 2; }
DIR=$(sed -n 's/^QUARK_PGO_DIR:PATH=//p' "$B/CMakeCache.txt")
[ -n "$DIR" ] || { echo "pgo: no QUARK_PGO_DIR in $B/CMakeCache.txt"; exit 2; }
CLANG=0; grep -q 'CMAKE_CXX_COMPILER_ID "\(Apple\)\{0,1\}Clang"' "$B/CMakeFiles/"*/CMakeCXXCompiler.cmake 2>/dev/null && CLANG=1

# a fresh profile: old counters from other sources would be merged in
rm -rf "$DIR"; mkdir -p "$DIR" || exit 2
T=$(mktemp -d) || exit 2
trap 'rm -rf "$T"' EXIT
export QUARK_MASTER=off
[ "$CLANG" = 1 ] && export LLVM_PROFILE_FILE="$DIR/quark-%p-%m.profraw"
if [ -z "$DISPLAY" ]; then
  pgrep -x Xvfb > /dev/null || { Xvfb :99 > /dev/null 2>&1 & sleep 2; }
  export DISPLAY=:99
fi
fail=0
run() {   # name, then the command (run in the build folder: maps/ beside the binaries)
  name=$1; shift
  start=$(date +%s)
  (cd "$B" && timeout 600 "$@") > "$T/$name.log" 2>&1
  rc=$?
  echo "pgo: $name: exit $rc, $(( $(date +%s) - start )) s"
  [ $rc -eq 0 ] || { tail -n 20 "$T/$name.log"; fail=1; }
}
DEMO="$ROOT/maps/benchmark/hamlet.qdemo"
if [ -x "$B/quark" ]; then
  BAKE=${PGO_BAKE_BUILD:-$ROOT/build}
  if [ -x "$BAKE/quark" ] && ! grep -q '^QUARK_PGO:STRING=GEN' "$BAKE/CMakeCache.txt" 2>/dev/null; then
    start=$(date +%s)
    (cd "$BAKE" && timeout 600 ./quark --data-dir "$T/d" --benchmark "$DEMO" 1 && timeout 600 ./quark --data-dir "$T/d" --host ffa --map sewer --bench 1) > "$T/bake.log" 2>&1 \
      || { tail -n 20 "$T/bake.log"; echo "pgo: baking the light with $BAKE failed"; exit 1; }
    rm -rf "$T/d/benchmarks"
    echo "pgo: light baked with $BAKE, $(( $(date +%s) - start )) s"
  else
    echo "pgo: no normal build to bake the light with (PGO_BAKE_BUILD=<dir>): the instrumented bake takes a long time"
  fi
  run benchmark ./quark --data-dir "$T/d" --benchmark "$DEMO" "$SECS"
  grep '^BENCHMARK ' "$T/benchmark.log" || { echo "pgo: the benchmark printed no result"; fail=1; }
  run hostffa ./quark --data-dir "$T/d" --host ffa --map sewer --bench "$SECS"
  grep '^BENCH ' "$T/hostffa.log" || { echo "pgo: the hosted FFA printed no result"; fail=1; }
else
  echo "pgo: no client in $B (QUARK_BUILD_CLIENT=OFF?): server and tests only"
fi
if [ -x "$B/quark_tests" ]; then
  run serverbench ./quark_tests serverbench 10
  grep 'SERVERBENCH' "$T/serverbench.log"
  run simhash ./quark_tests simhash 3
  cat "$T/simhash.log"
fi
if [ "$CLANG" = 1 ]; then
  llvm-profdata merge -o "$DIR/quark.profdata" "$DIR"/*.profraw || fail=1
  rm -f "$DIR"/*.profraw
fi
echo "pgo: profile in $DIR: $(find "$DIR" -type f | wc -l) files, $(du -sh "$DIR" | cut -f1)"
[ $fail -eq 0 ] || { echo "pgo: a training run FAILED (the profile is partial)"; exit 1; }
echo "pgo: done"

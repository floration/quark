#!/bin/sh
# The BENCHMARK end to end (ctest's "benchmark" entry; docs/RELEASE_CHECKLIST.md): a short Hamlet demo with 31 bots
# made now (quark_tests benchdemo: never committed), played by `quark --benchmark <demo> 4` in a data folder of its own,
# then its printed numbers and its saved run checked. Then the shipped demo for 5 s, if it's there. Skipped (77)
# without a display: run it under Xvfb (xvfb-run -a ctest -R benchmark).
# usage: tools/benchmark_check.sh <build dir>
B=${1:-build}
if [ -z "$DISPLAY" ]; then echo "benchmark: no DISPLAY - skipped"; exit 77; fi
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "benchmark: FAIL: $*"; exit 1; }
"$B/quark_tests" benchdemo "$T/bench.qdemo" 8 > "$T/rec.log" 2>&1 || { cat "$T/rec.log"; fail "couldn't record a demo"; }
QUARK_MASTER=off "$B/quark" --data-dir "$T/data" --benchmark "$T/bench.qdemo" 4 > "$T/run.log" 2>&1 || { cat "$T/run.log"; fail "quark --benchmark failed"; }
grep '^BENCHMARK ' "$T/run.log" || { cat "$T/run.log"; fail "no results printed"; }
grep -Eq '^BENCHMARK .*: [1-9][0-9]* frames in [0-9.]+ s  avg [0-9.]+ fps  1% low [0-9.]+  0.1% low [0-9.]+  min [0-9.]+ fps .* stdev [0-9.]+ ms' "$T/run.log" || fail "the results line"
f=$(ls "$T/data/benchmarks/"*.qbench 2>/dev/null | head -1)
[ -n "$f" ] || fail "no saved run"
for k in name label date version demo uploaded frames avg_fps low_1pct_fps low_0.1pct_fps min_fps stdev_ms setup:GPU setup:CPU setup:RESOLUTION; do
  grep -q "^# $k," "$f" || fail "the saved run has no $k"
done
n=$(sed -n 's/^# frames,//p' "$f"); l=$(sed -n '/^frame_time_us$/,$p' "$f" | tail -n +2 | wc -l)
[ "$n" = "$l" ] || fail "frames $n but $l frame times saved"
if [ -f "$(dirname "$0")/../maps/benchmark/hamlet.qdemo" ]; then   # (the shipped demo plays through the same path)
  QUARK_MASTER=off "$B/quark" --data-dir "$T/data2" --benchmark 5 > "$T/ship.log" 2>&1 || { cat "$T/ship.log"; fail "the shipped demo"; }
  grep '^BENCHMARK ' "$T/ship.log" || fail "the shipped demo printed nothing"
fi
echo "benchmark: PASS"

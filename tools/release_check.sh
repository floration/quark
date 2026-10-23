#!/bin/bash
# The pre-push check used for 1.17.0 (docs/HANDOVER.md section 0): every quark_tests group, 4 at a time, then the
# timing-sensitive ones alone, then every map hosted headless in FFA and screenshotted (it loads and the game runs).
# Usage: tools/release_check.sh [--skip "bots botaim botfalls maps"]     (run from the repo root, after a build)
# Needs Xvfb on Linux for the map check (skipped with a note if there's no Xvfb). Exit code 0 = everything passed.
cd "$(dirname "$0")/.." || exit 2
SKIP=" "
[ "$1" = "--skip" ] && SKIP=" $2 "
export TMPDIR="${TMPDIR:-/tmp}/quark_release_check.$$"; mkdir -p "$TMPDIR"
fail=0
alone="netcode tickrate governor resetmaps userdata"   # (timing-sensitive or rewrites shared files: on an idle machine)
par=""
for g in $(build/quark_tests groups); do case "$SKIP$alone " in *" $g "*) ;; *) par="$par $g";; esac; done
echo $par | tr ' ' '\n' | xargs -P4 -I{} sh -c 'g={}; build/quark_tests group $g > "$TMPDIR/$g.log" 2>&1; echo "$g rc=$? $(grep "^group " "$TMPDIR/$g.log")"' | tee "$TMPDIR/par.txt"
grep -q 'rc=[^0]' "$TMPDIR/par.txt" && fail=1
for g in $alone; do
  case "$SKIP" in *" $g "*) continue;; esac
  build/quark_tests group $g > "$TMPDIR/$g.log" 2>&1; rc=$?
  echo "$g rc=$rc $(grep '^group ' "$TMPDIR/$g.log")"; [ $rc = 0 ] || fail=1
done
if command -v Xvfb >/dev/null; then
  Xvfb :97 >/dev/null 2>&1 & xpid=$!
  for i in $(seq 50); do [ -S /tmp/.X11-unix/X97 ] && break; sleep 0.2; done   # (until the display is up)
  for f in maps/*.qmap; do n=$(basename "$f" .qmap)
    # (the biggest maps need over a minute for their first light bake on a loaded machine with software GL)
    DISPLAY=:97 timeout 300 build/quark --host ffa --map "$n" --screenshot "$TMPDIR/$n.png" > "$TMPDIR/$n.map.log" 2>&1; rc=$?
    if [ $rc = 0 ] && [ -s "$TMPDIR/$n.png" ]; then echo "map $n ok"; else echo "map $n FAILED rc=$rc"; fail=1; fi
  done
  kill $xpid
else
  echo "(no Xvfb: map load check skipped; host each map once by hand instead)"
fi
echo "logs: $TMPDIR"
[ $fail = 0 ] && echo "RESULT: all passed" || echo "RESULT: FAILURES (see the logs; timing groups: re-run alone on an idle machine)"
exit $fail

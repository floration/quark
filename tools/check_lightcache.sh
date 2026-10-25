#!/bin/sh
# CI (release.yml): every map embedded in this build has its light bake in the package (quark --bake-maps and
# tools/pack_lightcache.py, the lightcache job).
#   sh tools/check_lightcache.sh <build dir> <the package's maps/lightcache>
# A missing one means this job's maps aren't the ones the lightcache job baked (make_maps.py's output should be the same
# on every runner). Only a warning: the game still runs, and bakes that map on its first join as it did before.
set -eu
n=0; missing=0
for h in $(grep -o '"[0-9a-f]\{64\}"' "$1/generated/official_maps.h" | tr -d '"'); do
  n=$((n + 1))
  [ -f "$2/$h.qlbz" ] || { echo "::warning::no light bake in $2 for the embedded map $h (the lightcache job baked other maps): it bakes on first join"; missing=$((missing + 1)); }
done
[ "$n" -gt 0 ] || echo "::warning::no maps in $1/generated/official_maps.h"
echo "light bakes: $((n - missing)) of $n maps, $(du -sh "$2" 2>/dev/null | cut -f1)"

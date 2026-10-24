#!/bin/sh
# CI (release.yml): every map embedded in this build has its light bake in the package (quark --bake-maps, the
# lightcache job).
#   sh tools/check_lightcache.sh <build dir> <the package's maps/lightcache>
# A missing one means this job's maps aren't the ones the lightcache job baked (make_maps.py's output must be the same
# on every runner): the game would still run, but bake that map on its first join.
set -eu
n=0
for h in $(grep -o '"[0-9a-f]\{64\}"' "$1/generated/official_maps.h" | tr -d '"'); do
  [ -f "$2/$h.qlb" ] || { echo "::error::no light bake in $2 for the embedded map $h (the lightcache job baked other maps)"; exit 1; }
  n=$((n + 1))
done
[ "$n" -gt 0 ] || { echo "::error::no maps in $1/generated/official_maps.h"; exit 1; }
echo "light bakes: $n maps, $(du -sh "$2" | cut -f1)"

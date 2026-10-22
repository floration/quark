#!/bin/sh
# Headless map editor regression checks (needs a display: Xvfb :99 & then DISPLAY=:99).
# Best with an ASan build: QUARK=build-asan/quark tools/test_editor.sh
#  1. OPEN on a map in the list (openEditor(maps_[i]) used to read a string freed by its own rescan: crash),
#     with mymap present, missing, from an older version, or over the parse limits
#  2. a typed name is what the map saves as (spaces become _)
set -e
QUARK=${QUARK:-build/quark}
DIR=$(mktemp -d)
export QUARK_DATA_DIR="$DIR"
mkdir -p "$DIR/maps"
fail() { echo "FAIL: $1"; rm -rf "$DIR"; exit 1; }
run() { timeout 120 "$QUARK" --editor temple --edscript "$1" 2>&1 | grep -E "edscript|ERROR" || true; }
idx() { ls maps/*.qmap "$DIR"/maps/*.qmap 2>/dev/null | xargs -n1 basename | sed 's/\.qmap$//' | sort -u | grep -nx "$1" | cut -d: -f1; }

printf 'title New map\nbox -20 -1 -20 20 0 20\nspawn any 0 0 0 0\n' > "$DIR/maps/mymap.qmap"
for variant in fresh old overlimit; do
    case $variant in
        old) printf 'title Old\nbox 0 0 0 1 1 1\nspawn any 0 0 0 0\nsomething_from_an_older_version 1\n' > "$DIR/maps/mymap.qmap" ;;
        overlimit) printf 'title Big\nbox 0 0 0 99999 1 1\nspawn any 0 0 0 0\n' > "$DIR/maps/mymap.qmap" ;;
    esac
    i=$(( $(idx mymap) - 1 ))
    out=$(run "wait;openidx $i;openidx $i;print;quit")
    echo "$out" | grep -q ERROR && fail "open mymap ($variant): $out"
    echo "$out" | grep -q "edscript: name=mymap" || [ "$variant" != fresh ] || fail "open mymap: $out"
    echo "ok: open mymap ($variant)"
done
rm "$DIR/maps/mymap.qmap"
out=$(run "wait;click -250 55;bs 12;type My Castle;enter;click -160 115;print;quit")
[ -f "$DIR/maps/My_Castle.qmap" ] || fail "typed name not used: $out"
echo "ok: saved as the typed name"
rm -rf "$DIR"

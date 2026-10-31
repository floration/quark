#!/bin/sh
# Headless map editor regression checks (needs a display: Xvfb :99 & then DISPLAY=:99).
# Best with an ASan build: QUARK=build-asan/quark tools/test_editor.sh
#  1. OPEN on a map in the list (openEditor(maps_[i]) used to read a string freed by its own rescan: crash),
#     with mymap present, missing, from an older version, or over the parse limits
#  2. a typed name is what the map saves as (spaces become _)
#  3. a built-in map you edited is never saved over (B3): saving or leaving asks; yes keeps the edits as a new map
#     (qk1_edit), no drops them, and nothing named like the built-in map appears in your maps folder
set -e
QUARK=${QUARK:-build/quark}
DIR=$(mktemp -d)
export QUARK_DATA_DIR="$DIR"
mkdir -p "$DIR/maps"
fail() { echo "FAIL: $1"; rm -rf "$DIR"; exit 1; }
run() { timeout 120 "$QUARK" --editor qk1 --edscript "$1" 2>&1 | grep -E "edscript|ERROR" || true; }
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
rm -f "$DIR"/maps/*.qmap
out=$(run "wait;sel 0 5;key 77 0;key 83 2;print;key 257 0;print;quit")   # M: material, Ctrl+S: save, Enter: yes
echo "$out" | grep -q "prompt=qk1_edit" || fail "no question on saving an edited built-in map: $out"
[ -f "$DIR/maps/qk1_edit.qmap" ] && [ ! -f "$DIR/maps/qk1.qmap" ] || fail "yes didn't keep the edits as qk1_edit: $out"
grep -q "^handmade" "$DIR/maps/qk1_edit.qmap" || fail "the saved copy isn't marked handmade"
echo "ok: edits to a built-in map kept as a new map"
rm -f "$DIR"/maps/*.qmap
out=$(run "wait;sel 0 5;key 77 0;key 256 0;key 256 0;print;key 256 0;print;quit")   # ESC: deselect, ESC: leave (asks), ESC: cancel
echo "$out" | grep -q "prompt=qk1_edit" || fail "no question on leaving an edited built-in map: $out"
echo "$out" | tail -1 | grep -q "prompt=" && fail "ESC didn't cancel the question: $out"
ls "$DIR"/maps/*.qmap >/dev/null 2>&1 && fail "something was saved without asking: $(ls "$DIR/maps")"
echo "ok: leaving an edited built-in map asks, saves nothing by itself"
rm -rf "$DIR"

#!/bin/sh
# 16 screenshots per map for docs/MAP_REPORT.md: 8 orbits around it (every 45 degrees, from above) and 8 eye-level
# views from spread-out floor spots (docs/map_shots/<map>.cams, written by `build/quark_tests mapreport`), combined
# into one 4x4 contact sheet per map: docs/map_shots/<map>.jpg. Needs a display (Xvfb works) and python3 + Pillow.
#   sh tools/map_shots.sh [map ...]
set -e
cd "$(dirname "$0")/.."
OUT=docs/map_shots
TMP=$(mktemp -d)
[ -f "$OUT/temple.cams" ] || build/quark_tests mapreport >/dev/null
MAPS=${*:-$(ls maps/*.qmap | xargs -n1 basename | sed 's/\.qmap$//')}
for m in $MAPS; do
  [ -f "$OUT/$m.cams" ] || continue
  i=0
  for a in 0 45 90 135 180 225 270 315; do
    build/quark --backdrop "$m" --orbit "$a" --screenshot "$TMP/$m-$i.png" >/dev/null 2>&1; i=$((i + 1))
  done
  while read -r x y z yaw pitch; do
    build/quark --backdrop "$m" --cam "$x" "$y" "$z" "$yaw" "$pitch" --screenshot "$TMP/$m-$i.png" >/dev/null 2>&1; i=$((i + 1))
  done < "$OUT/$m.cams"
  python3 - "$TMP" "$m" "$OUT/$m.jpg" <<'PY'
import sys
from PIL import Image, ImageDraw
tmp, m, out = sys.argv[1:4]
W, H = 480, 270
sheet = Image.new("RGB", (W * 4, H * 4))
for i in range(16):
    try: im = Image.open(f"{tmp}/{m}-{i}.png").convert("RGB").resize((W, H))
    except OSError: continue
    ImageDraw.Draw(im).text((6, 4), ("orbit %d" % (i * 45)) if i < 8 else "eye %d" % (i - 7), fill=(255, 255, 0))
    sheet.paste(im, ((i % 4) * W, (i // 4) * H))
sheet.save(out, quality=72)
PY
  echo "$m: $OUT/$m.jpg"
done
rm -rf "$TMP"

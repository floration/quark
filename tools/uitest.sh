#!/bin/sh
# ctest's "uitest" entry (docs/UI_TEST.md): quark --uitest <report> when there's a display, else skipped (77).
# usage: tools/uitest.sh <path to quark> <report.json>
if [ -z "$DISPLAY" ]; then
  echo "uitest: no DISPLAY - skipped (run it under Xvfb: xvfb-run -a ctest -R uitest)"
  exit 77
fi
exec "$1" --uitest "$2"

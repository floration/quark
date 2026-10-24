#!/bin/bash
# Fixture release check.
cd "$(dirname "$0")/.." || exit 2
alone="delta epsilon"
for g in $(build/quark_tests groups); do echo $g; done
echo $par | xargs -P4 -I{} sh -c 'build/quark_tests group {}'

if [ -n "$X" ]; then
  echo yes
fi
exit 0

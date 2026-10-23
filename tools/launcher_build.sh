#!/bin/sh
# The launcher that ships (release.yml's windows / linux / macos jobs): quark_launcher built from the `launcher` branch
# (docs/HANDOVER.md section 4), not from the commit being released, so a release only changes the launcher when that
# branch changes. Run after the job's own game build in build/; the launcher ends up in build-launcher/.
#   sh tools/launcher_build.sh <the platform's cmake options>
# QUARK_VERSION_SUFFIX is never passed on: a launcher carries its branch's VERSION (its marker, selfupdate.h), and
# installed launchers are only replaced when the launcher branch raises it. On the launcher branch itself, or while
# that branch doesn't exist, the launcher the game build just made is used.
set -eu
OUT=build-launcher
rc=0
[ "${GITHUB_REF:-}" = refs/heads/launcher ] || { git ls-remote --exit-code --heads origin launcher > /dev/null || rc=$?; }
if [ "${GITHUB_REF:-}" = refs/heads/launcher ] || [ "$rc" = 2 ]; then
    echo "launcher: from this commit (build/)"
    mkdir -p "$OUT" && cp build/quark_launcher* "$OUT/"
    exit 0
fi
[ "$rc" = 0 ] || { echo "::error::can't read the launcher branch"; exit 1; }
git fetch -q --depth 1 origin launcher
rm -rf launcher-src && git worktree add -q --detach launcher-src FETCH_HEAD
echo "launcher: from the launcher branch ($(git -C launcher-src rev-parse --short HEAD), version $(head -n1 launcher-src/VERSION))"
cmake -S launcher-src -B "$OUT" -DQUARK_BUILD_TESTS=OFF "$@" > /dev/null
cmake --build "$OUT" -j --target quark_launcher

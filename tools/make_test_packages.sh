#!/bin/bash
# Unsigned Windows + Linux packages of the working tree, for the owner to try by hand before a push or a tag
# (signing happens only in CI). Built like release.yml's windows / linux jobs. Needs MinGW-w64 (posix threads)
# for the Windows half; zip and tar for the archives.
#   tools/make_test_packages.sh [out_dir]      (default: build/test-packages)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$(mkdir -p "${1:-build/test-packages}" && cd "${1:-build/test-packages}" && pwd)"
V="$(head -n1 VERSION | tr -d '[:space:]')"
rm -rf "$OUT/win" "$OUT/lin"; mkdir -p "$OUT/win/quark-windows" "$OUT/lin/quark-linux"
# Windows (MinGW, static, as CI)
cmake -S . -B build-win -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix \
  -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres -DCMAKE_BUILD_TYPE=Release -DQUARK_BUILD_TESTS=OFF >/dev/null
cmake --build build-win -j --target quark quark_server quark_launcher >/dev/null
cp build-win/quark.exe build-win/quark_server.exe build-win/quark_launcher.exe "$OUT/win/quark-windows/"
cp -r maps mods README.md "$OUT/win/quark-windows/"
(cd "$OUT/win" && rm -f "../quark-$V-windows-test.zip" && zip -qr "../quark-$V-windows-test.zip" quark-windows)
# Linux (as CI's linux job)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j --target quark quark_server quark_launcher >/dev/null
cp build/quark build/quark_server build/quark_launcher "$OUT/lin/quark-linux/"
cp -r maps mods README.md "$OUT/lin/quark-linux/"
(cd "$OUT/lin" && rm -f "../quark-$V-linux-test.tar.gz" && tar czf "../quark-$V-linux-test.tar.gz" quark-linux)
rm -rf "$OUT/win" "$OUT/lin"
ls -la "$OUT"/quark-"$V"-*
echo "commit $(git rev-parse --short HEAD)$(git diff --quiet HEAD || echo ' (plus uncommitted changes)')"

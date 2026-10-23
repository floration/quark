#!/bin/sh
# CI (release.yml): signs release files with the QUARK_SIGNING_KEY secret (docs/SIGNING.md), writing <file>.sig next
# to each, and checks every signature against the public key built into the launcher, so a wrong secret fails here
# instead of shipping files no launcher accepts.
#   tools/sign/sign_release.sh FILE...                 sign each file (<file>.sig)
#   tools/sign/sign_release.sh --launchers PACKAGE...   sign the launcher inside each .qpak and add its .sig to the
#                                                      package (qpak_launcher_sig.py); before the manifest and the
#                                                      packages themselves are signed, since it changes the package
# quark_sign is built once per job (in $QUARK_SIGN_BUILD, default a folder in the temp dir) and reused.
set -eu
MODE=files
if [ "${1:-}" = --launchers ]; then MODE=launchers; shift; fi
[ "$#" -gt 0 ] || { echo "::error::nothing to sign"; exit 1; }
if [ -z "${QUARK_SIGNING_KEY:-}" ]; then
    echo "::error::QUARK_SIGNING_KEY secret is missing: refusing to publish unsigned files (see docs/SIGNING.md)"
    exit 1
fi
SRC=$(cd "$(dirname "$0")/../.." && pwd)
B=${QUARK_SIGN_BUILD:-${RUNNER_TEMP:-${TMPDIR:-/tmp}}/quark-sign-build}
if [ ! -x "$B/quark_sign" ]; then
    cmake -S "$SRC" -B "$B" -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$B" --target quark_sign -j >/dev/null
fi
if [ "$MODE" = launchers ]; then
    python3 "$SRC/tools/sign/qpak_launcher_sig.py" "$B/quark_sign" "$@"
    exit 0
fi
for f in "$@"; do
    "$B/quark_sign" sign "$f"
    "$B/quark_sign" verify "$f"
done

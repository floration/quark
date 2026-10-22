#!/bin/sh
# CI (release.yml): signs release files with the QUARK_SIGNING_KEY secret (docs/SIGNING.md), writing <file>.sig next
# to each, and checks every signature against the public key built into the launcher, so a wrong secret fails here
# instead of shipping packages no launcher accepts.
#   tools/sign/sign_release.sh FILE...
set -eu
[ "$#" -gt 0 ] || { echo "::error::nothing to sign"; exit 1; }
if [ -z "${QUARK_SIGNING_KEY:-}" ]; then
    echo "::error::QUARK_SIGNING_KEY secret is missing: refusing to publish unsigned files (see docs/SIGNING.md)"
    exit 1
fi
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
cmake -S "$(dirname "$0")/../.." -B "$B" -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$B" --target quark_sign -j >/dev/null
for f in "$@"; do
    "$B/quark_sign" sign "$f"
    "$B/quark_sign" verify "$f"
done

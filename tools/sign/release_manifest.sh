#!/bin/sh
# CI (release.yml): writes a release manifest to stdout, which sign_release.sh then signs (docs/SIGNING.md).
#   tools/sign/release_manifest.sh CHANNEL TAG VERSION COMMIT PACKAGE...
# Servers (tools/server/autoupdate.sh) check its channel / tag / commit before building; the launcher checks its
# tag / version / channel and the sha256 of the package it downloaded, so a validly signed package can't be
# installed as another release or channel.
set -eu
[ "$#" -ge 5 ] || { echo "usage: $0 CHANNEL TAG VERSION COMMIT PACKAGE..." >&2; exit 2; }
CHANNEL=$1 TAG=$2 VER=$3 COMMIT=$4
shift 4
case "$CHANNEL" in production|testing) ;; *) echo "bad channel $CHANNEL" >&2; exit 1 ;; esac
printf 'quark release\nchannel %s\ntag %s\nversion %s\ncommit %s\n' "$CHANNEL" "$TAG" "$VER" "$COMMIT"
for f in "$@"; do
    [ -f "$f" ] || { echo "no package $f" >&2; exit 1; }
    printf 'sha256 %s %s\n' "$(basename "$f")" "$(sha256sum -- "$f" | cut -d' ' -f1)"
done

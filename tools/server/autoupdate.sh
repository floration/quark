#!/bin/sh
# Run every minute by quark-update.timer (see install.sh). If the branch has new commits it builds them
# next to the running server, swaps the binary in, and drops update.pending: the server then restarts
# itself as soon as no match is being played (telling players first). Nothing happens when up to date.
set -e
cd "$(dirname "$0")/../.."
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git fetch -q origin "$BRANCH"
TARGET=$(git rev-parse "origin/$BRANCH")
[ "$TARGET" = "$(cat .built-commit 2>/dev/null)" ] && exit 0
# Wait until that version's release is finished: every platform's update package uploaded (that's what
# players' launchers install), so everyone moves to the new version together.
NEWVER=$(git show "origin/$BRANCH:VERSION" | tr -d '[:space:]')
REPO=$(git remote get-url origin | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')
ASSETS=$(curl -sfL "https://api.github.com/repos/$REPO/releases/tags/v$NEWVER" || true)
[ -n "$ASSETS" ] || exit 0
# Which platforms this version ships: the build jobs the release job waits for at that commit (a platform that
# is switched off, like macOS was, isn't waited for), plus any qpak the release already lists. Every one of
# them must be uploaded, and the Linux one (what servers and most players run) is always required.
WF=$(git show "origin/$BRANCH:.github/workflows/release.yml" 2>/dev/null || true)
NEEDS=""
for P in $(echo "$WF" | sed -n '/^  release:/,/^  [a-z]*:$/p' | sed -n 's/^ *needs: *\[\(.*\)\].*/\1/p' | tr ',' ' '); do
    echo "$WF" | sed -n "/^  $P:/,/^  [a-z]*:\$/p" | grep -q '^    if: *false' || NEEDS="$NEEDS $P"
done
LISTED=$(echo "$ASSETS" | grep -o '"name": *"quark-[a-z0-9]*\.qpak"' | sed 's/.*"quark-\([a-z0-9]*\)\.qpak"/\1/')
for P in linux $NEEDS $LISTED; do
    case "$P" in windows|linux|macos) ;; *) continue ;; esac
    echo "$ASSETS" | grep -q "\"quark-$P.qpak\"" || exit 0
    echo "$ASSETS" | grep -q "\"quark-$P.qpak.sig\"" || exit 0   # signed in the release job (docs/SIGNING.md)
done
echo "$ASSETS" | grep -q '"state": *"open"' && exit 0   # an asset is still uploading
git checkout -q -- maps 2>/dev/null || true   # a server runs the shipped maps: drop stray local changes so the update applies
git merge -q --ff-only "origin/$BRANCH"
cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
nice cmake --build build-update -j1 --target quark_server quark_sign >/dev/null
# Only install a version whose released Linux package is signed by the release key: a release whose assets
# were swapped or tampered with is refused (and retried next minute, so a fixed release still goes through).
SIGDIR=$(mktemp -d)
trap 'rm -rf "$SIGDIR"' EXIT
curl -sfL -o "$SIGDIR/quark-linux.qpak" "https://github.com/$REPO/releases/download/v$NEWVER/quark-linux.qpak"
curl -sfL -o "$SIGDIR/quark-linux.qpak.sig" "https://github.com/$REPO/releases/download/v$NEWVER/quark-linux.qpak.sig"
if ! build-update/quark_sign verify "$SIGDIR/quark-linux.qpak"; then
    echo "refusing $NEWVER ($TARGET): quark-linux.qpak signature does not verify" >&2
    exit 1
fi
mkdir -p bin
cp build-update/quark_server bin/quark_server.new
mv -f bin/quark_server.new bin/quark_server     # atomic: the running server keeps its old copy
echo "$TARGET" > .built-commit
touch update.pending
echo "built $(cat VERSION) ($TARGET), server restarts when the current match ends"

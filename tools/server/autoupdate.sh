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
git checkout -q -- maps 2>/dev/null || true   # a server runs the shipped maps: drop stray local changes so the update applies
git merge -q --ff-only "origin/$BRANCH"
cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
nice cmake --build build-update -j1 >/dev/null
mkdir -p bin
cp build-update/quark_server bin/quark_server.new
mv -f bin/quark_server.new bin/quark_server     # atomic: the running server keeps its old copy
echo "$TARGET" > .built-commit
touch update.pending
echo "built $(cat VERSION) ($TARGET), server restarts when the current match ends"

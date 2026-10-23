#!/bin/sh
# Discord bot auto-update (docs/OPERATIONS.md), run every 10 minutes by quark-discord-update.timer (install.sh sets it
# up, as root: it restarts the bot's service). The git and pip work runs as the bot's own user ($QUARK_BOT_USER).
#
# It follows PRODUCTION releases only: the newest tag v<x.y.z> that is on the repository's default branch (a tag is
# only made there when the owner ships a release, CLAUDE.md). When the bot's folder differs at that tag it
#   1. checks the tag out (never an older commit than the one running: a tag already in HEAD's history is a no-op);
#   2. reinstalls the hash-pinned requirements if requirements.txt changed;
#   3. runs the offline tests (test_quarkdata) and a dry run (reads live data, posts nothing, gets no secrets);
#   4. restarts the bot. If 2 or 3 fails it goes back to the commit it was on and exits 1 (journal; the bot keeps
#      running the old code).
# After the bot moves to its own repository (MOVING.md), that repository has no release tags: set
# QUARK_BOT_FOLLOW=branch:main in the unit to follow its main branch instead.
#
# By hand: sudo sh tools/discordbot/autoupdate.sh       logs: journalctl -u quark-discord-update
set -eu
main() {
cd "$(dirname "$0")"
HERE=$(pwd)
USER_=${QUARK_BOT_USER:-$(stat -c %U "$HERE")}
FOLLOW=${QUARK_BOT_FOLLOW:-tag}
as() { if [ "$(id -un)" = "$USER_" ]; then "$@"; else runuser -u "$USER_" -- "$@"; fi; }
log() { echo "quark-discord-update: $*"; }
die() { echo "quark-discord-update: ERROR: $*" >&2; exit 1; }
TOP=$(as git rev-parse --show-toplevel)
REL=$(as git rev-parse --show-prefix)          # the bot's folder inside the checkout ("" when it's the repo root)
REL=${REL%/}
cd "$TOP"
as git fetch -q --tags --prune origin || { log "can't reach the repository (will retry)"; exit 0; }
DEF=$(as git ls-remote --symref origin HEAD | sed -n 's#^ref: refs/heads/\([^[:space:]]*\)[[:space:]]*HEAD#\1#p')
[ -n "$DEF" ] || die "can't tell the repository's default branch"
as git fetch -q origin "+refs/heads/$DEF:refs/remotes/origin/$DEF"
case "$FOLLOW" in
    tag)
        TARGET=$(as git tag -l 'v*' | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n1)
        [ -n "$TARGET" ] || { log "no production release tag yet"; exit 0; }
        as git merge-base --is-ancestor "$TARGET^{commit}" "origin/$DEF" \
            || die "$TARGET isn't on the default branch $DEF: not following it" ;;
    branch:*) TARGET="origin/${FOLLOW#branch:}" ;;
    *) die "QUARK_BOT_FOLLOW must be 'tag' or 'branch:<name>', not '$FOLLOW'" ;;
esac
NEW=$(as git rev-parse "$TARGET^{commit}")
OLD=$(as git rev-parse HEAD)
if as git merge-base --is-ancestor "$NEW" "$OLD"; then exit 0; fi   # (already there, or newer: never go back)
if as git diff --quiet "$OLD" "$NEW" -- "${REL:-.}"; then
    as git checkout -q --detach "$NEW"; log "moved to $TARGET (the bot didn't change)"; exit 0
fi
log "updating the bot: $(as git rev-parse --short "$OLD") -> $TARGET ($(as git rev-parse --short "$NEW"))"
REQ_CHANGED=0
as git diff --quiet "$OLD" "$NEW" -- "${REL:+$REL/}requirements.txt" || REQ_CHANGED=1
as git checkout -q --detach "$NEW"
check() {
    cd "$HERE"
    [ "$REQ_CHANGED" = 0 ] || as "$HERE/.venv/bin/pip" install -q --require-hashes -r requirements.txt || return 1
    as "$HERE/.venv/bin/python" -m unittest -q test_quarkdata || return 1
    # (the dry run gets no secrets: env -i, and it never reads config.json's token for a dry run)
    as env -i PATH=/usr/bin:/bin HOME="$HERE" timeout 120 "$HERE/.venv/bin/python" bot.py --dry-run >/dev/null || return 1
}
if ! check; then
    cd "$TOP"
    as git checkout -q --detach "$OLD"
    [ "$REQ_CHANGED" = 0 ] || as "$HERE/.venv/bin/pip" install -q --require-hashes -r "$HERE/requirements.txt" || true
    die "the bot at $TARGET failed its tests or dry run: kept $(as git rev-parse --short "$OLD") (still running)"
fi
systemctl restart quark-discord
log "updated to $TARGET and restarted"
}
main "$@"

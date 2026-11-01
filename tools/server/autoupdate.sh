#!/bin/sh
# Dedicated-server auto-update, run every minute or two by a systemd timer (install.sh sets one up per instance):
#   quark-update.timer          the PRODUCTION server (/opt/quark): QUARK_CHANNEL=production (the default) follows
#                               the latest production release, exactly as before;
#   quark-testing-update.timer  the TESTING server (/opt/quark-testing): QUARK_CHANNEL=testing follows the newest
#                               testing prerelease (tag test-X.Y.Z.N from 1.19.1 on, test-X.Y.Z.testing.N from
#                               1.19.0, test-X.Y.Z-test.N before; signed manifest channel "testing") while it's newer than production: by
#                               version, and not already in production (X.Y.Z.N sorts above X.Y.Z, so a build
#                               the production release was merged from counts as caught up by its commit). When it
#                               isn't (production caught up), the testing server is stopped instead ($STATE/active,
#                               below) until a newer one comes.
# When there's a newer release it
#   1. waits until the release is complete: every platform's qpak that release lists has its .sig, and the Linux one
#      (what most servers and players run) is there;
#   2. checks the release's signed manifest (quark-release.txt + .sig, signed in CI with the release key) with the
#      quark_sign this server already trusts: its channel is the release's (production for vX.Y.Z, testing for
#      test-*), its tag is this release, its version is the tag's and its commit is what the git tag points at; also
#      checks the signed Linux package, like the launcher does (docs/SIGNING.md);
#   3. checks out exactly that commit, builds quark_server as that version (a testing build gets its .N suffix,
#      checked against the commit's VERSION file) next to the running one, swaps the binary in and drops
#      update.pending: the server restarts itself as soon as no match is being played (telling players first).
# Never moves to an older version. Nothing happens when up to date.
#
# Both instances' updaters share one copy of GitHub's release list (QUARK_RELEASE_CACHE, the governor directory:
# unauthenticated GitHub allows 60 requests an hour per address, and two updaters each asking every minute would run
# out). An optional GITHUB_TOKEN in /etc/quark/update.env raises that limit. Builds take QUARK_BUILD_LOCK (flock), so
# the two never compile at once (a 1 GB machine would run out of memory).
#
# Exit status: 0 = up to date / updated / waiting a little for a release to finish uploading / GitHub unreachable.
# Nonzero = the update is stuck (bad signature, build failure, a release incomplete for over an hour, ...): every
# such line starts with "quark-update: ERROR", and systemd runs that instance's alert service (OnFailure), which logs at
# critical priority naming the instance and posts to QUARK_ALERT_WEBHOOK (a Discord webhook URL) if
# /etc/quark/update.env sets one.
#
# Layout (install.sh): code + build in the checkout this script is in (/opt/quark, /opt/quark-testing), state in
# $QUARK_STATE (/var/lib/quark, /var/lib/quark-testing). Run it by hand with
#   sudo -u quark sh /opt/quark/tools/server/autoupdate.sh
#   sudo -u quark-testing env QUARK_STATE=/var/lib/quark-testing QUARK_CHANNEL=testing sh /opt/quark-testing/tools/server/autoupdate.sh
# (Everything is inside main(), parsed before it runs: the checkout below replaces this very file.)
set -eu
main() {
cd "$(dirname "$0")/../.."
SRC=$(pwd)
STATE=${QUARK_STATE:-/var/lib/quark}
CHANNEL=${QUARK_CHANNEL:-production}
STALL_SECS=${QUARK_STALL_SECS:-3600}
API=${QUARK_RELEASE_API:-}       # (tests only: another release-list JSON URL, e.g. file://...)
CACHE=${QUARK_RELEASE_CACHE:-}   # (shared with the other instance's updater; empty: none)
LOCK=${QUARK_BUILD_LOCK:-}
log() { echo "quark-update: $*"; }
die() { echo "quark-update: ERROR: $*" >&2; exit 1; }
case "$CHANNEL" in production|testing) ;; *) die "QUARK_CHANNEL must be production or testing, not '$CHANNEL'" ;; esac
command -v python3 >/dev/null || die "python3 is needed (to read GitHub's JSON)"
# Run by an install from before 1.17 (its unit sets no QUARK_STATE): that unit's old script already built this
# release; this one needs the new layout, so it waits for the operator instead of failing every minute.
if [ -z "${QUARK_STATE:-}" ] && [ ! -w /var/lib/quark ]; then
    log "this server was installed by an older install.sh: run 'sh tools/server/install.sh' once to keep getting updates"
    exit 0
fi
mkdir -p "$STATE"
PICK="python3 -I $SRC/tools/server/pick_release.py"
locked() { if [ -n "$LOCK" ] && command -v flock >/dev/null; then flock -w 10800 "$LOCK" "$@"; else "$@"; fi; }

REPO=$(git remote get-url origin | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# A release we've seen but can't install yet: fine for a while (assets still uploading), an alert after STALL_SECS.
waiting() {
    now=$(date +%s)
    first=$(sed -n "s/^$TAG //p" "$STATE/update-waiting" 2>/dev/null || true)
    [ -n "$first" ] || { first=$now; echo "$TAG $now" > "$STATE/update-waiting"; }
    if [ $((now - first)) -ge "$STALL_SECS" ]; then die "STALLED on $TAG for $(( (now - first) / 60 )) min: $*"; fi
    log "waiting for $TAG: $*"
    exit 0
}
gh() {   # gh URL OUT: a GitHub API GET (false if unreachable / rate-limited)
    curl -sfL --max-time 30 -H "Accept: application/vnd.github+json" ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} "$1" -o "$2"
}

# --- 1. the release this instance should run ---------------------------------------------------------------------
# GitHub's release list (newest first): one fetch serves both instances for 70 s (each writes its own copy into the
# shared cache; the freshest is used). It's only a pointer: everything installed is checked against signatures below.
fresh=""
if [ -z "$API" ] && [ -n "$CACHE" ] && [ -d "$CACHE" ]; then
    now=$(date +%s)
    for f in "$CACHE"/releases.*.json; do
        [ -f "$f" ] || continue
        age=$((now - $(stat -c %Y "$f" 2>/dev/null || echo 0)))
        if [ "$age" -ge 0 ] && [ "$age" -lt 70 ] && cp "$f" "$TMP/releases.json" 2>/dev/null; then fresh=1; break; fi
    done
fi
if [ -z "$fresh" ]; then
    if ! gh "${API:-https://api.github.com/repos/$REPO/releases?per_page=100}" "$TMP/releases.json"; then
        log "GitHub unreachable (or rate-limited); trying again next minute"
        exit 0
    fi
    if [ -z "$API" ] && [ -n "$CACHE" ] && [ -d "$CACHE" ] && [ -w "$CACHE" ]; then
        me=$(id -un)
        { cp "$TMP/releases.json" "$CACHE/releases.$me.json.tmp" && mv -f "$CACHE/releases.$me.json.tmp" "$CACHE/releases.$me.json"; } 2>/dev/null || true
    fi
fi
$PICK pick "$CHANNEL" "$TMP/releases.json" > "$TMP/release.txt" || die "can't read GitHub's release list"
if ! grep -q '^channel production' "$TMP/release.txt" && [ -z "$API" ]; then
    # (no production release on the first page: more testing builds than that since the last one) GitHub's latest
    gh "https://api.github.com/repos/$REPO/releases/latest" "$TMP/latest.json" || { log "GitHub unreachable (or rate-limited); trying again next minute"; exit 0; }
    $PICK pick "$CHANNEL" "$TMP/releases.json" "$TMP/latest.json" > "$TMP/release.txt" || die "can't read GitHub's latest release"
fi
TAG=$(sed -n 's/^tag //p' "$TMP/release.txt")
[ -n "$TAG" ] || { log "no $CHANNEL release on GitHub yet"; exit 0; }
NEWVER=$(sed -n 's/^version //p' "$TMP/release.txt")
WANTCH=$(sed -n 's/^channel //p' "$TMP/release.txt")
case "$WANTCH" in
    production) echo "$TAG" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || die "release tag '$TAG' isn't a production tag (vX.Y.Z)" ;;
    # (any suffix, as pick_release.py: this script is the last installed build's, and must keep up with later numberings)
    testing) echo "$TAG" | grep -Eq '^test-[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z]+)+$' || die "release tag '$TAG' isn't a testing tag (test-X.Y.Z.N)" ;;
    *) die "release $TAG: unknown channel" ;;
esac
[ "$CHANNEL" = testing ] || [ "$WANTCH" = production ] || die "a production server was offered testing release $TAG"
# The testing server runs only while there's a testing build newer than production (owner, 1.17.0: "if the testing
# branch and main branch are on the same update, you dont need to run a testing server"). $STATE/active is the switch:
# install.sh's quark-testing-active.path starts the server when it appears, and the service's ExecCondition keeps it
# down when it's gone; removing it also asks the running server to restart (update.pending) once its match ends.
# From 1.19.0 on a testing build keeps the production VERSION (X.Y.Z.N > X.Y.Z; 1.19.0 to 1.19.1 X.Y.Z.testing.N), so the build a production
# release was merged from still sorts above it: one whose commit the newest production tag contains has caught up too.
# (Checked once per pair of tags, $STATE/caught-up; a failed fetch decides nothing and is tried again next time.)
# A testing-only box (install.sh --testing-only, QUARK_TESTING_ALWAYS=1) skips all this: its testing server always
# runs, on the newest testing build or, once production is newer, production.
if [ "$CHANNEL" = testing ] && [ "${QUARK_TESTING_ALWAYS:-0}" != 1 ]; then
    PROD=$(sed -n 's/^production //p' "$TMP/release.txt")
    CAUGHT=0
    if [ "$WANTCH" = testing ] && [ -n "$PROD" ]; then
        if [ "$(sed -n 1p "$STATE/caught-up" 2>/dev/null)" = "$TAG $PROD" ]; then
            CAUGHT=$(sed -n 2p "$STATE/caught-up")
        elif git fetch -q --force origin "+refs/tags/$PROD:refs/tags/$PROD" "+refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null; then
            rc=0; git merge-base --is-ancestor "$TAG^{commit}" "$PROD^{commit}" 2>/dev/null || rc=$?
            case "$rc" in 0) CAUGHT=1 ;; 1) CAUGHT=0 ;; *) CAUGHT=x ;; esac
            [ "$CAUGHT" = x ] || printf '%s\n%s\n' "$TAG $PROD" "$CAUGHT" > "$STATE/caught-up"
        fi
    fi
    if [ "$WANTCH" = production ] || [ "$CAUGHT" = 1 ]; then
        STOPPING=1
        if [ -e "$STATE/active" ]; then
            rm -f "$STATE/active"
            touch "$STATE/update.pending"
            if [ "$CAUGHT" = 1 ]; then log "production $PROD contains the newest testing build $TAG: the testing server stops when its current match ends"
            else log "no testing build newer than production $TAG: the testing server stops when its current match ends"; fi
        fi
        # Caught up: the newest testing build's commit is in production, and it's installed or will be as before.
        # Production newer than every testing build: install production here too (the server stays stopped), so this
        # checkout, and with it this script and pick_release.py, is never older than production. Without that a
        # testing updater would run the code of its last testing build for good: one that can't read a later testing
        # numbering would never update again without install.sh (owner, 2026-10-28: install.sh once, never again).
        if [ "$WANTCH" != production ]; then rm -f "$STATE/update-waiting"; exit 0; fi
    fi
    [ -n "${STOPPING:-}" ] || [ -e "$STATE/active" ] || { touch "$STATE/active"; log "testing build $TAG is newer than production: the testing server starts"; }
fi
CUR=$(cat "$STATE/built-tag" 2>/dev/null || true)
[ "$TAG" = "$CUR" ] && { rm -f "$STATE/update-waiting"; exit 0; }
if [ -n "$CUR" ]; then
    CURVER=$($PICK version "$CUR")
    if [ -n "$CURVER" ] && [ "$($PICK cmp "$NEWVER" "$CURVER")" != 1 ]; then
        [ "$($PICK cmp "$NEWVER" "$CURVER")" = 0 ] && { rm -f "$STATE/update-waiting"; exit 0; }
        die "latest $CHANNEL release $TAG is older than the installed $CUR: refusing to downgrade"
    fi
fi

# --- 2. complete? (platforms come from the release itself, not a hard-coded list) ---------------------------------
asset() { awk -v n="$1" '$1 == "asset" && $2 == n { print $3; exit }' "$TMP/release.txt"; }
url() { awk -v n="$1" '$1 == "asset" && $2 == n { print $4; exit }' "$TMP/release.txt"; }
# (releases are published whole, so one without a manifest was made before signed manifests: never installable)
[ -n "$(asset quark-release.txt)" ] || { log "$TAG has no signed manifest (made before they existed): staying on ${CUR:-this build}"; exit 0; }
for a in quark-linux.qpak quark-release.txt; do
    [ "$(asset "$a")" = uploaded ] || waiting "$a isn't uploaded yet"
    [ "$(asset "$a.sig")" = uploaded ] || waiting "$a.sig isn't uploaded yet"
done
for a in $(awk '$1 == "asset" && $2 ~ /^quark-[a-z0-9]+\.qpak$/ { print $2 }' "$TMP/release.txt"); do
    [ "$(asset "$a")" = uploaded ] && [ "$(asset "$a.sig")" = uploaded ] || waiting "$a or its signature isn't uploaded yet"
done

# --- 3. verify with the quark_sign (and so the release key) this server already runs ------------------------------
SIGN="$SRC/build-update/quark_sign"
if [ ! -x "$SIGN" ]; then   # first run (install.sh): the checkout the operator installed from is the trust anchor
    cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null \
        || die "cmake failed"
    locked nice cmake --build build-update -j1 --target quark_sign >/dev/null || die "building quark_sign failed"
fi
cp "$SIGN" "$TMP/quark_sign"   # (the build below replaces it)
for a in quark-release.txt quark-release.txt.sig quark-linux.qpak quark-linux.qpak.sig; do
    curl -sfL --max-time 300 -o "$TMP/$a" "$(url "$a")" || waiting "can't download $a"
done
"$TMP/quark_sign" verify "$TMP/quark-release.txt" >/dev/null || die "$TAG: quark-release.txt signature does not verify"
"$TMP/quark_sign" verify "$TMP/quark-linux.qpak" >/dev/null || die "$TAG: quark-linux.qpak signature does not verify"
field() { sed -n "s/^$1 //p" "$TMP/quark-release.txt"; }
[ "$(field channel)" = "$WANTCH" ] || die "$TAG: signed manifest isn't for $WANTCH ($(field channel))"
[ "$(field tag)" = "$TAG" ] || die "$TAG: signed manifest is for tag $(field tag)"
[ "$(field version)" = "$NEWVER" ] || die "$TAG: signed manifest is for version $(field version)"
COMMIT=$(field commit)
echo "$COMMIT" | grep -Eq '^[0-9a-f]{40}$' || die "$TAG: signed manifest has no commit"

# --- 4. that exact commit, built as that version -----------------------------------------------------------------
if [ "$WANTCH" = production ]; then
    git fetch -q --force origin "+refs/tags/$TAG:refs/tags/$TAG" || waiting "git fetch of $TAG failed"
    [ "$(git rev-parse "$TAG^{commit}")" = "$COMMIT" ] || die "$TAG: the git tag isn't the signed commit $COMMIT"
else   # (testing prereleases are tagged by CI on the testing branch's commit; the signed manifest names it)
    git fetch -q --force origin "+refs/tags/$TAG:refs/tags/$TAG" 2>/dev/null || git fetch -q origin "$COMMIT" || waiting "git fetch of $TAG failed"
    if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
        [ "$(git rev-parse "$TAG^{commit}")" = "$COMMIT" ] || die "$TAG: the git tag isn't the signed commit $COMMIT"
    fi
fi
# A server runs the shipped code: local edits in the checkout are dropped (data lives in $STATE, never here).
git -c advice.detachedHead=false checkout -q --force --detach "$COMMIT" || die "checkout of $COMMIT failed"
[ "$(git rev-parse HEAD)" = "$COMMIT" ] || die "checkout didn't land on $COMMIT"
BASE=$(head -n1 VERSION | tr -d '[:space:]')
SUFFIX=${NEWVER#"$BASE"}
case "$WANTCH:$SUFFIX" in
    production:) ;;
    testing:?*) echo "$SUFFIX" | grep -Eq '^([.-][0-9A-Za-z]+)+$' || die "$TAG: version $NEWVER isn't $BASE plus a testing suffix" ;;
    *) die "$TAG: version $NEWVER doesn't match the commit's VERSION $BASE" ;;
esac

# (QUARK_VERSION_SUFFIX always given: the build dir's cache would otherwise keep the last one)
cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
    -DQUARK_VERSION_SUFFIX="$SUFFIX" >/dev/null || die "$TAG: cmake failed"
locked nice cmake --build build-update -j1 --target quark_server quark_sign >/dev/null || die "$TAG: build failed"
mkdir -p bin
cp build-update/quark_server bin/quark_server.new
mv -f bin/quark_server.new bin/quark_server     # atomic: the running server keeps its old copy
echo "$TAG" > "$STATE/built-tag"
rm -f "$STATE/update-waiting"
touch "$STATE/update.pending"
log "built $TAG ($NEWVER, $COMMIT) for the $CHANNEL server; it restarts when the current match ends"
}
main "$@"
exit

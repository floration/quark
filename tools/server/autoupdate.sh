#!/bin/sh
# Dedicated-server auto-update, run every minute by quark-update.timer as the `quark` user (install.sh sets it up).
# It follows PRODUCTION releases only: the latest non-prerelease GitHub release. When there's a newer one it
#   1. waits until the release is complete: every platform's qpak that release lists has its .sig, and the Linux one
#      (what most servers and players run) is there;
#   2. checks the release's signed manifest (quark-release.txt + .sig, signed in CI with the release key) with the
#      quark_sign this server already trusts, and that its tag is this release and its commit is what the git tag
#      points at; also checks the signed Linux package, like the launcher does (docs/SIGNING.md);
#   3. checks out exactly that commit, builds quark_server next to the running one, swaps the binary in and drops
#      update.pending: the server restarts itself as soon as no match is being played (telling players first).
# Never moves to an older version. Nothing happens when up to date.
#
# Exit status: 0 = up to date / updated / waiting a little for a release to finish uploading / GitHub unreachable.
# Nonzero = the update is stuck (bad signature, build failure, a release incomplete for over an hour, ...): every
# such line starts with "quark-update: ERROR", and systemd runs quark-update-alert.service (OnFailure), which logs at
# critical priority and posts to QUARK_ALERT_WEBHOOK (a Discord webhook URL) if /etc/quark/update.env sets one.
#
# Layout (install.sh): code + build in the checkout this script is in (default /opt/quark), state in $QUARK_STATE
# (default /var/lib/quark). Run it by hand with:  sudo -u quark sh /opt/quark/tools/server/autoupdate.sh
set -eu
cd "$(dirname "$0")/../.."
SRC=$(pwd)
STATE=${QUARK_STATE:-/var/lib/quark}
STALL_SECS=${QUARK_STALL_SECS:-3600}
API=${QUARK_RELEASE_API:-}   # (tests only: another release JSON URL, e.g. file://...)
log() { echo "quark-update: $*"; }
die() { echo "quark-update: ERROR: $*" >&2; exit 1; }
command -v python3 >/dev/null || die "python3 is needed (to read GitHub's JSON)"
# Run by an install from before 1.17 (its unit sets no QUARK_STATE): that unit's old script already built this
# release; this one needs the new layout, so it waits for the operator instead of failing every minute.
if [ -z "${QUARK_STATE:-}" ] && [ ! -w /var/lib/quark ]; then
    log "this server was installed by an older install.sh: run 'sh tools/server/install.sh' once to keep getting updates"
    exit 0
fi
mkdir -p "$STATE"

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

# --- 1. the latest production release ---------------------------------------------------------------------------
if ! curl -sfL --max-time 30 -H "Accept: application/vnd.github+json" \
        "${API:-https://api.github.com/repos/$REPO/releases/latest}" -o "$TMP/release.json"; then
    log "GitHub unreachable (or rate-limited); trying again next minute"
    exit 0
fi
python3 - "$TMP/release.json" > "$TMP/release.txt" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
print("tag", r.get("tag_name", ""))
print("prerelease", int(bool(r.get("prerelease")) or bool(r.get("draft"))))
for a in r.get("assets", []):
    print("asset", a.get("name", ""), a.get("state", ""), a.get("browser_download_url", ""))
PY
TAG=$(sed -n 's/^tag //p' "$TMP/release.txt")
echo "$TAG" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || die "latest release tag '$TAG' isn't a production tag (vX.Y.Z)"
[ "$(sed -n 's/^prerelease //p' "$TMP/release.txt")" = 0 ] || die "GitHub's latest release $TAG is a prerelease/draft"
CUR=$(cat "$STATE/built-tag" 2>/dev/null || true)
[ "$TAG" = "$CUR" ] && { rm -f "$STATE/update-waiting"; exit 0; }
NEWVER=${TAG#v}
if [ -n "$CUR" ] && [ "$(printf '%s\n%s\n' "${CUR#v}" "$NEWVER" | sort -V | tail -n1)" != "$NEWVER" ]; then
    die "latest release $TAG is older than the installed $CUR: refusing to downgrade"
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
    nice cmake --build build-update -j1 --target quark_sign >/dev/null || die "building quark_sign failed"
fi
cp "$SIGN" "$TMP/quark_sign"   # (the build below replaces it)
for a in quark-release.txt quark-release.txt.sig quark-linux.qpak quark-linux.qpak.sig; do
    curl -sfL --max-time 300 -o "$TMP/$a" "$(url "$a")" || waiting "can't download $a"
done
"$TMP/quark_sign" verify "$TMP/quark-release.txt" >/dev/null || die "$TAG: quark-release.txt signature does not verify"
"$TMP/quark_sign" verify "$TMP/quark-linux.qpak" >/dev/null || die "$TAG: quark-linux.qpak signature does not verify"
field() { sed -n "s/^$1 //p" "$TMP/quark-release.txt"; }
[ "$(field channel)" = production ] || die "$TAG: signed manifest isn't for production ($(field channel))"
[ "$(field tag)" = "$TAG" ] || die "$TAG: signed manifest is for tag $(field tag)"
COMMIT=$(field commit)
echo "$COMMIT" | grep -Eq '^[0-9a-f]{40}$' || die "$TAG: signed manifest has no commit"

# --- 4. that exact commit ----------------------------------------------------------------------------------------
git fetch -q --force origin "+refs/tags/$TAG:refs/tags/$TAG" || waiting "git fetch of $TAG failed"
[ "$(git rev-parse "$TAG^{commit}")" = "$COMMIT" ] || die "$TAG: the git tag isn't the signed commit $COMMIT"
# A server runs the shipped code: local edits in the checkout are dropped (data lives in $STATE, never here).
git -c advice.detachedHead=false checkout -q --force --detach "$COMMIT" || die "checkout of $COMMIT failed"
[ "$(git rev-parse HEAD)" = "$COMMIT" ] || die "checkout didn't land on $COMMIT"

cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null \
    || die "$TAG: cmake failed"
nice cmake --build build-update -j1 --target quark_server quark_sign >/dev/null || die "$TAG: build failed"
mkdir -p bin
cp build-update/quark_server bin/quark_server.new
mv -f bin/quark_server.new bin/quark_server     # atomic: the running server keeps its old copy
echo "$TAG" > "$STATE/built-tag"
rm -f "$STATE/update-waiting"
touch "$STATE/update.pending"
log "built $TAG ($COMMIT); the server restarts when the current match ends"

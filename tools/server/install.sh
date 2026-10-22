#!/bin/sh
# Sets up (or upgrades) a dedicated server on Linux with systemd. Run it from a checkout, as a user with sudo:
#   tools/server/install.sh --ranked --variant vanilla --map temple --public --name "my server"
# Everything after install.sh is passed to quark_server (run it again to change the options; with none, the options
# of the installed service are kept, or --public on a first install).
#
# What it sets up:
#   user `quark` (system user, no login shell)  runs both the server and the updater
#   /opt/quark       a clone of this checkout, owned by quark: the code, build-update/ and bin/quark_server
#   /var/lib/quark   state: data/ (logs, edited maps, custom modes), built-tag, update.pending
#   quark.service          the server, hardened: it can't write /opt/quark (so not the code the updater runs)
#   quark-update.timer     every minute: tools/server/autoupdate.sh (production releases only, signed; see there)
#   quark-update-alert.service  runs when an update fails or is stuck: a critical log line, and a post to
#                          QUARK_ALERT_WEBHOOK (Discord webhook URL) if /etc/quark/update.env sets it
#
# Upgrading an older install (the server ran as your user, from your checkout, built from a branch): update your
# checkout (git pull) and run this again. It stops the old units, copies the old server data over (originals kept),
# and replaces the units. Your old checkout isn't used afterwards.
set -eu
cd "$(dirname "$0")/../.."
FROM=$(pwd)
PREFIX=${QUARK_PREFIX:-/opt/quark}
STATE=${QUARK_STATE:-/var/lib/quark}
SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
asquark() { if [ "$(id -u)" = 0 ]; then runuser -u quark -- "$@"; else sudo -u quark "$@"; fi; }
for c in git cmake c++ curl python3 systemctl; do
    command -v "$c" >/dev/null || { echo "install.sh: $c is missing (e.g. sudo apt install git cmake g++ curl python3)"; exit 1; }
done

# server options: these arguments, else the installed service's, else --public
ARGS=""
for a in "$@"; do ARGS="${ARGS:+$ARGS }\"$(printf '%s' "$a" | sed 's/["\\]/\\&/g')\""; done   # quoted for systemd (names with spaces)
OLDUNIT=/etc/systemd/system/quark.service
if [ -z "$ARGS" ] && [ -f "$OLDUNIT" ]; then
    ARGS=$(sed -n 's#^ExecStart=[^ ]*quark_server --restart-flag [^ ]*\( --data-dir [^ ]*\)\{0,1\}##p' "$OLDUNIT")
fi
[ -n "$ARGS" ] || ARGS="--public"

$SUDO systemctl stop quark-update.timer quark-update.service 2>/dev/null || true   # (no update check mid-install)

# the dedicated user
if ! id quark >/dev/null 2>&1; then
    $SUDO useradd --system --home-dir "$STATE" --create-home --shell /usr/sbin/nologin --user-group quark
fi
$SUDO mkdir -p "$STATE/data"

# an old install (User=<you>, WorkingDirectory=<your checkout>): stop it and bring its data along (cp -n: no overwrite)
if [ -f "$OLDUNIT" ] && ! grep -q '^User=quark$' "$OLDUNIT"; then
    OLDUSER=$(sed -n 's/^User=//p' "$OLDUNIT")
    OLDDIR=$(sed -n 's/^WorkingDirectory=//p' "$OLDUNIT")
    echo "upgrading the old install (user ${OLDUSER:-?}, $OLDDIR)"
    OLDINSTALL=1   # (its units are stopped once the new build is ready, so a failed build leaves it running)
    OLDHOME=$(getent passwd "$OLDUSER" | cut -d: -f6 || true)
    for d in "$OLDHOME/.local/share/quark" "$OLDDIR/logs" "$OLDDIR/custom_modes" "$OLDDIR/groups"; do
        [ -n "$d" ] && [ -d "$d" ] || continue
        case "$d" in */.local/share/quark) dest="$STATE/data" ;; *) dest="$STATE/data/$(basename "$d")" ;; esac
        $SUDO mkdir -p "$dest" && $SUDO cp -rn "$d/." "$dest/" && echo "  copied $d -> $dest"
    done
fi

# the code: a clone of this checkout at the same commit (via a bundle, so no git ownership checks between users),
# pointing at the same GitHub repository
URL=$(git remote get-url origin)
BUNDLE=$(mktemp -d)
git bundle create -q "$BUNDLE/quark.bundle" HEAD
chmod -R a+rX "$BUNDLE"
[ -d "$PREFIX/.git" ] || $SUDO git -c init.defaultBranch=main init -q "$PREFIX"
$SUDO git -C "$PREFIX" -c safe.directory='*' fetch -q "$BUNDLE/quark.bundle" HEAD
$SUDO git -C "$PREFIX" -c safe.directory='*' -c advice.detachedHead=false checkout -q --force --detach FETCH_HEAD
$SUDO git -C "$PREFIX" -c safe.directory='*' remote remove origin 2>/dev/null || true
$SUDO git -C "$PREFIX" -c safe.directory='*' remote add origin "$URL"
rm -rf "$BUNDLE"
$SUDO chown -R quark:quark "$PREFIX" "$STATE"
$SUDO chmod 750 "$STATE"

# build what's checked out now (the server can start right away), then let the updater move it to the latest release
TAG=$(git -C "$FROM" describe --exact-match --tags --match 'v[0-9]*.[0-9]*.[0-9]*' HEAD 2>/dev/null || true)
asquark sh -c "cd '$PREFIX' && cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release >/dev/null && nice cmake --build build-update -j\$(nproc) --target quark_server quark_sign >/dev/null \
    && mkdir -p bin && cp build-update/quark_server bin/quark_server.new && mv -f bin/quark_server.new bin/quark_server" \
    || { echo "build failed (any old install is still running)"; exit 1; }
[ -n "${OLDINSTALL:-}" ] && { $SUDO systemctl disable --now quark-update.timer quark.service 2>/dev/null || true; }
if [ -n "$TAG" ]; then echo "$TAG" | asquark tee "$STATE/built-tag" >/dev/null; else $SUDO rm -f "$STATE/built-tag"; fi
asquark env QUARK_STATE="$STATE" HOME="$STATE" sh "$PREFIX/tools/server/autoupdate.sh" || echo "(the update check failed: see above; the server starts on the build from this checkout)"
$SUDO rm -f "$STATE/update.pending"

# alert settings (kept across re-installs)
$SUDO mkdir -p /etc/quark
[ -f /etc/quark/update.env ] || printf '# QUARK_ALERT_WEBHOOK=https://discord.com/api/webhooks/...   (posted to when an update fails or stalls)\n' \
    | $SUDO tee /etc/quark/update.env >/dev/null
$SUDO chmod 640 /etc/quark/update.env

HARDEN="NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
SystemCallArchitectures=native
CapabilityBoundingSet=
AmbientCapabilities=
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX AF_NETLINK
UMask=0027"

$SUDO tee /etc/systemd/system/quark.service >/dev/null <<UNIT
[Unit]
Description=quark server
After=network-online.target
Wants=network-online.target
[Service]
User=quark
Group=quark
WorkingDirectory=$STATE
Environment=QUARK_DATA_DIR=$STATE/data
ExecStart=$PREFIX/bin/quark_server --restart-flag $STATE/update.pending --data-dir $STATE/data $ARGS
Restart=always
RestartSec=2
$HARDEN
MemoryDenyWriteExecute=yes
ReadWritePaths=$STATE
ReadOnlyPaths=$PREFIX
[Install]
WantedBy=multi-user.target
UNIT
$SUDO tee /etc/systemd/system/quark-update.service >/dev/null <<UNIT
[Unit]
Description=quark update check (production releases)
After=network-online.target
Wants=network-online.target
OnFailure=quark-update-alert.service
[Service]
Type=oneshot
User=quark
Group=quark
Environment=QUARK_STATE=$STATE HOME=$STATE
ExecStart=/bin/sh $PREFIX/tools/server/autoupdate.sh
Nice=10
TimeoutStartSec=2h
$HARDEN
ReadWritePaths=$PREFIX $STATE
UNIT
$SUDO tee /etc/systemd/system/quark-update.timer >/dev/null <<UNIT
[Unit]
Description=check for quark updates every minute
[Timer]
OnBootSec=1min
OnUnitInactiveSec=1min
[Install]
WantedBy=timers.target
UNIT
$SUDO tee /etc/systemd/system/quark-update-alert.service >/dev/null <<UNIT
[Unit]
Description=quark update failed or stalled: alert
[Service]
Type=oneshot
User=quark
Group=quark
SupplementaryGroups=systemd-journal
EnvironmentFile=-/etc/quark/update.env
Environment=QUARK_STATE=$STATE
ExecStart=/bin/sh $PREFIX/tools/server/update-alert.sh
$HARDEN
ReadWritePaths=$STATE
UNIT
$SUDO systemctl daemon-reload
$SUDO systemctl enable --now quark.service quark-update.timer
$SUDO systemctl restart quark.service
echo "running as user quark from $PREFIX. status: systemctl status quark | logs: journalctl -u quark -f"
echo "updates: journalctl -u quark-update (failures also: journalctl -p crit -t quark-update; webhook: /etc/quark/update.env)"

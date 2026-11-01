#!/bin/sh
# Sets up (or upgrades) the dedicated servers on a Linux box with systemd: one PRODUCTION server and one TESTING
# server, each always running the latest release of its channel. Run it from a checkout, as a user with sudo:
#   tools/server/install.sh --mode 1v1 --variant vampire --map dojo --public --name "my server"
# Every argument but install.sh's own (below) is passed to quark_server (run it again to change them; with none, the
# options of the installed production service are kept, or unranked laser vampire 1v1 on dojo, public, on a first install:
# owner, 2026-10-31). The testing server gets the
# same options, on its own port, with " [TESTING]" added to its name.
#   --no-testing        no testing server (an existing one is stopped and disabled; its data is kept)
#   --testing           a testing server again (the default; remembered in /etc/quark/install.conf)
#   --testing-only      only a testing server, on the production port (--port, default 27500): no production server
#                       (an existing one is stopped and disabled; its data is kept), and the testing server runs all
#                       the time, on the newest testing build or production release, whichever is newer
#   --both              production and testing again (the default) after --testing-only or --no-testing
#   --testing-port N    the testing server's UDP port (default: the production port + 1; kept across re-installs)
#
# What it sets up:
#   production (exactly as before; an existing install keeps its options and data and becomes this instance):
#     user `quark`                  runs the production server and its updater (system user, no login shell)
#     /opt/quark                    a clone of this checkout, owned by quark: the code, build-update/, bin/quark_server
#     /var/lib/quark                state: data/ (logs, edited maps, custom modes), built-tag, update.pending
#     quark.service                 the server (--channel prod), hardened: it can't write /opt/quark
#     quark-update.timer            every minute: tools/server/autoupdate.sh, latest PRODUCTION release (signed)
#     quark-update-alert.service    an update failed / stalled: a critical log line naming "production", and a post
#                                   to QUARK_ALERT_WEBHOOK (Discord webhook URL) if /etc/quark/update.env sets it
#   testing (unless --no-testing):
#     user `quark-testing`          runs the testing server and its updater: testing code never runs as `quark`
#     /opt/quark-testing            its own clone and build
#     /var/lib/quark-testing        its own state and data
#     quark-testing.service         the server (--channel testing: ranked is played but never rated), at a lower CPU
#                                   priority than production (Nice=5, CPUWeight=50)
#     quark-testing-update.timer    every 2 minutes: the newest TESTING prerelease (or production release, if newer)
#     quark-testing-update-alert.service   as above, naming "testing"
#   both:
#     group `quark-governor`, /var/lib/quark-governor (sticky, group-writable): each server writes its status there
#       once a second and reads the other's (--governor-dir; src/game/governor.h): when the box can't carry both,
#       the empty server is throttled first, then the one not playing a ranked match, then the other, down to 125 Hz;
#       then the testing server closes until production is calm again. Production never closes.
#       The updaters share GitHub's release list there (one request a minute for both) and a build lock (never two
#       compiles at once).
#
# Upgrading an older install (the server ran as your user, from your checkout, built from a branch): update your
# checkout (git pull) and run this again. It stops the old units, copies the old server data over (originals kept),
# and replaces the units. Your old checkout isn't used afterwards.
set -eu
cd "$(dirname "$0")/../.."
FROM=$(pwd)
PREFIX=${QUARK_PREFIX:-/opt/quark}
STATE=${QUARK_STATE:-/var/lib/quark}
TPREFIX=${QUARK_TESTING_PREFIX:-/opt/quark-testing}
TSTATE=${QUARK_TESTING_STATE:-/var/lib/quark-testing}
GOV=${QUARK_GOVERNOR_DIR:-/var/lib/quark-governor}
CONF=/etc/quark/install.conf
SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
asuser() { u=$1; shift; if [ "$(id -u)" = 0 ]; then runuser -u "$u" -- "$@"; else sudo -u "$u" "$@"; fi; }
for c in git cmake c++ curl python3 systemctl; do
    command -v "$c" >/dev/null || { echo "install.sh: $c is missing (e.g. sudo apt install git cmake g++ curl python3)"; exit 1; }
done

# install.sh's own options; everything else goes to quark_server (quoted for systemd: names with spaces; % doubled)
TESTING=""; PRODUCTION=""; TPORT=""; ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --no-testing) TESTING=0; PRODUCTION=1 ;;
        --testing) TESTING=1 ;;
        --testing-only) TESTING=1; PRODUCTION=0 ;;
        --both) TESTING=1; PRODUCTION=1 ;;
        --testing-port) [ $# -ge 2 ] || { echo "install.sh: --testing-port needs a port"; exit 1; }; TPORT=$2; shift ;;
        *) ARGS="${ARGS:+$ARGS }\"$(printf '%s' "$1" | sed 's/["\\]/\\&/g; s/%/%%/g')\"" ;;
    esac
    shift
done
TPORT_ARG=$TPORT
conf() { sed -n "s/^$1=\([0-9]*\)$/\1/p" "$CONF" 2>/dev/null | tail -n1; }
[ -n "$TESTING" ] || TESTING=$(conf TESTING)
[ -n "$TESTING" ] || TESTING=1
[ -n "$PRODUCTION" ] || PRODUCTION=$(conf PRODUCTION)
[ -n "$PRODUCTION" ] || PRODUCTION=1
# (only both together share a load governor: alone, the testing server has no production server to make room for)
BOTH=0; [ "$PRODUCTION" = 1 ] && [ "$TESTING" = 1 ] && BOTH=1
# server options: these arguments, else the installed production service's (a testing-only box: the testing
# service's), else --public
OLDUNIT=/etc/systemd/system/quark.service
TUNIT=/etc/systemd/system/quark-testing.service
unitargs() { sed -n 's#^ExecStart=[^ ]*quark_server --restart-flag [^ ]*\( --data-dir [^ ]*\)\{0,1\}\( --channel [^ ]*\)\{0,1\}\( --governor-dir [^ ]*\)\{0,1\}##p' "$1"; }
if [ -z "$ARGS" ] && [ -f "$OLDUNIT" ]; then
    ARGS=$(unitargs "$OLDUNIT")
elif [ -z "$ARGS" ] && [ "$PRODUCTION" = 0 ] && [ -f "$TUNIT" ]; then
    ARGS=$(unitargs "$TUNIT")
fi
[ -n "$ARGS" ] || ARGS="\"--mode\" \"1v1\" \"--variant\" \"vampire\" \"--map\" \"dojo\" \"--public\""   # (owner, 2026-10-31: "default server position should be dojo ranked lg vamp")
# What the testing server runs: the same options on its own port, its name marked [TESTING].
argtool() {
    python3 -I - "$@" <<'PY'
import shlex, sys
def words(s):   # ExecStart words as written above ("..." with \" \\ and %% escapes)
    return [w.replace("%%", "%") for w in shlex.split(s)]
def q(w):
    return '"' + w.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'
args = words(sys.argv[2])
if sys.argv[1] == "port":
    port = "27500"
    for i, a in enumerate(args):
        if a == "--port" and i + 1 < len(args): port = args[i + 1]
    print(port)
else:
    out, i, named = [], 0, False
    while i < len(args):
        a = args[i]
        if a in ("--port", "--channel", "--governor-dir", "--restart-flag", "--data-dir"): i += 2; continue
        if a == "--name" and i + 1 < len(args):
            n = args[i + 1]
            out += ["--name", n if n.endswith(" [TESTING]") else n + " [TESTING]"]; named = True; i += 2; continue
        out.append(a); i += 1
    if not named: out += ["--name", "quark server [TESTING]"]
    print(" ".join(q(w) for w in ["--port", sys.argv[3]] + out))
PY
}
PORT=$(argtool port "$ARGS")
if [ "$PRODUCTION" = 0 ]; then   # (testing alone: the production port, unless --testing-port says otherwise)
    [ -n "$TPORT" ] || TPORT=$PORT
    case "$TPORT" in ''|*[!0-9]*) echo "install.sh: bad testing port '$TPORT'"; exit 1 ;; esac
    TARGS=$(argtool testing "$ARGS" "$TPORT")
elif [ "$TESTING" = 1 ]; then
    [ -n "$TPORT" ] || TPORT=$(conf TESTING_PORT)
    [ -n "$TPORT" ] || { [ -f "$TUNIT" ] && TPORT=$(sed -n 's#^ExecStart=.*quark_server .*"--port" "\([0-9]*\)".*#\1#p' "$TUNIT"); } || true
    [ -n "$TPORT" ] || TPORT=$((PORT + 1))
    # (a saved port from a testing-only install is production's own: the next one, as on a fresh install)
    [ "$TPORT" != "$PORT" ] || [ -n "$TPORT_ARG" ] || TPORT=$((PORT + 1))
    case "$TPORT" in ''|*[!0-9]*) echo "install.sh: bad testing port '$TPORT'"; exit 1 ;; esac
    [ "$TPORT" != "$PORT" ] || { echo "install.sh: the testing server needs its own port (production is on $PORT)"; exit 1; }
    TARGS=$(argtool testing "$ARGS" "$TPORT")
fi

$SUDO systemctl stop quark-update.timer quark-update.service quark-testing-update.timer quark-testing-update.service quark-testing-active.path 2>/dev/null || true   # (no update check mid-install)

# the dedicated users, and the group both servers share the governor directory through
getent group quark-governor >/dev/null || $SUDO groupadd --system quark-governor
mkuser() {
    id "$1" >/dev/null 2>&1 || $SUDO useradd --system --home-dir "$2" --create-home --shell /usr/sbin/nologin --user-group "$1"
    id -nG "$1" | tr ' ' '\n' | grep -qx quark-governor || $SUDO usermod -a -G quark-governor "$1"
}
if [ "$PRODUCTION" = 1 ]; then
    mkuser quark "$STATE"
    $SUDO mkdir -p "$STATE/data"
fi
# (sticky: neither can replace or delete the other's files; setgid: files are the group's, so each can read the other's)
$SUDO install -d -m 3770 -o root -g quark-governor "$GOV"
[ -f "$GOV/build.lock" ] || $SUDO install -m 0660 -o root -g quark-governor /dev/null "$GOV/build.lock"

# an old install (User=<you>, WorkingDirectory=<your checkout>): stop it and bring its data along (cp -n: no overwrite)
if [ "$PRODUCTION" = 1 ] && [ -f "$OLDUNIT" ] && ! grep -q '^User=quark$' "$OLDUNIT"; then
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
clone() {   # clone DIR OWNER
    [ -d "$1/.git" ] || $SUDO git -c init.defaultBranch=main init -q "$1"
    $SUDO git -C "$1" -c safe.directory='*' fetch -q "$BUNDLE/quark.bundle" HEAD
    $SUDO git -C "$1" -c safe.directory='*' -c advice.detachedHead=false checkout -q --force --detach FETCH_HEAD
    $SUDO git -C "$1" -c safe.directory='*' remote remove origin 2>/dev/null || true
    $SUDO git -C "$1" -c safe.directory='*' remote add origin "$URL"
    $SUDO chown -R "$2:$2" "$1"
}
# build what's checked out now (the server can start right away), then let the updater move it to the latest release
# (in the production instance; on a testing-only box in the testing one)
TAG=$(git -C "$FROM" describe --exact-match --tags --match 'v[0-9]*.[0-9]*.[0-9]*' HEAD 2>/dev/null || true)
build() {   # build USER PREFIX
    asuser "$1" sh -c "cd '$2' && cmake -S . -B build-update -DQUARK_BUILD_CLIENT=OFF -DQUARK_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release -DQUARK_VERSION_SUFFIX= >/dev/null && nice cmake --build build-update -j\$(nproc) --target quark_server quark_sign >/dev/null \
    && mkdir -p bin && cp build-update/quark_server bin/quark_server.new && mv -f bin/quark_server.new bin/quark_server" \
        || { echo "build failed (any old install is still running)"; rm -rf "$BUNDLE"; exit 1; }
}
if [ "$PRODUCTION" = 1 ]; then
    clone "$PREFIX" quark
    $SUDO chown -R quark:quark "$STATE"
    $SUDO chmod 750 "$STATE"
    build quark "$PREFIX"
    [ -n "${OLDINSTALL:-}" ] && { $SUDO systemctl disable --now quark-update.timer quark.service 2>/dev/null || true; }
    if [ -n "$TAG" ]; then echo "$TAG" | asuser quark tee "$STATE/built-tag" >/dev/null; else $SUDO rm -f "$STATE/built-tag"; fi
    asuser quark env QUARK_STATE="$STATE" HOME="$STATE" QUARK_CHANNEL=production QUARK_RELEASE_CACHE="$GOV" QUARK_BUILD_LOCK="$GOV/build.lock" \
        sh "$PREFIX/tools/server/autoupdate.sh" || echo "(the update check failed: see above; the server starts on the build from this checkout)"
    $SUDO rm -f "$STATE/update.pending"
fi

# the testing instance: its own user, clone and state; it starts on this checkout's build (copied from production's,
# same commit; built here on a testing-only box) and its updater moves it to the newest testing build within a couple
# of minutes
if [ "$TESTING" = 1 ]; then
    mkuser quark-testing "$TSTATE"
    $SUDO mkdir -p "$TSTATE/data"
    clone "$TPREFIX" quark-testing
    $SUDO chown -R quark-testing:quark-testing "$TSTATE"
    $SUDO chmod 750 "$TSTATE"
    if [ "$PRODUCTION" = 1 ]; then
        $SUDO install -d -o quark-testing -g quark-testing "$TPREFIX/bin"
        $SUDO install -m 755 -o quark-testing -g quark-testing "$PREFIX/bin/quark_server" "$TPREFIX/bin/quark_server"
    else
        build quark-testing "$TPREFIX"
    fi
    if [ -n "$TAG" ]; then echo "$TAG" | asuser quark-testing tee "$TSTATE/built-tag" >/dev/null; else $SUDO rm -f "$TSTATE/built-tag"; fi
    if [ "$PRODUCTION" = 0 ]; then
        asuser quark-testing env QUARK_STATE="$TSTATE" HOME="$TSTATE" QUARK_CHANNEL=testing QUARK_TESTING_ALWAYS=1 \
            QUARK_RELEASE_CACHE="$GOV" QUARK_BUILD_LOCK="$GOV/build.lock" sh "$TPREFIX/tools/server/autoupdate.sh" \
            || echo "(the update check failed: see above; the server starts on the build from this checkout)"
    fi
    $SUDO rm -f "$TSTATE/update.pending"
fi
rm -rf "$BUNDLE"

# alert settings and install choices (kept across re-installs)
$SUDO mkdir -p /etc/quark
[ -f /etc/quark/update.env ] || printf '%s\n' \
    '# QUARK_ALERT_WEBHOOK=https://discord.com/api/webhooks/...   (posted to when an update fails or stalls)' \
    '# GITHUB_TOKEN=...   (optional, read-only: raises GitHub'"'"'s 60 requests an hour per address for the update checks)' \
    | $SUDO tee /etc/quark/update.env >/dev/null
$SUDO chmod 640 /etc/quark/update.env
printf 'PRODUCTION=%s\nTESTING=%s\nTESTING_PORT=%s\n' "$PRODUCTION" "$TESTING" "${TPORT:-}" | $SUDO tee "$CONF" >/dev/null

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

# One instance's units: units NAME USER PREFIX STATE CHANNEL(production|testing) "SERVER ARGS" EXTRA_SERVICE_LINES TIMER_SECS
#   [EXTRA_UPDATE_ENV]
units() {
    name=$1 user=$2 prefix=$3 state=$4 ch=$5 sargs=$6 extra=$7 every=$8 uenv=${9:-}
    short=prod; [ "$ch" = testing ] && short=testing
    gov=""; [ "$BOTH" = 1 ] && gov=" --governor-dir $GOV"
    $SUDO tee "/etc/systemd/system/$name.service" >/dev/null <<UNIT
[Unit]
Description=quark $ch server
After=network-online.target
Wants=network-online.target
[Service]
User=$user
Group=$user
SupplementaryGroups=quark-governor
WorkingDirectory=$state
Environment=QUARK_DATA_DIR=$state/data
ExecStart=$prefix/bin/quark_server --restart-flag $state/update.pending --data-dir $state/data --channel $short$gov $sargs
Restart=always
RestartSec=2
$extra
$HARDEN
MemoryDenyWriteExecute=yes
ReadWritePaths=$state $GOV
ReadOnlyPaths=$prefix
[Install]
WantedBy=multi-user.target
UNIT
    $SUDO tee "/etc/systemd/system/$name-update.service" >/dev/null <<UNIT
[Unit]
Description=quark update check ($ch server)
After=network-online.target
Wants=network-online.target
OnFailure=$name-update-alert.service
[Service]
Type=oneshot
User=$user
Group=$user
SupplementaryGroups=quark-governor
EnvironmentFile=-/etc/quark/update.env
Environment=QUARK_STATE=$state HOME=$state QUARK_CHANNEL=$ch QUARK_RELEASE_CACHE=$GOV QUARK_BUILD_LOCK=$GOV/build.lock$uenv
ExecStart=/bin/sh $prefix/tools/server/autoupdate.sh
Nice=10
TimeoutStartSec=4h
$HARDEN
ReadWritePaths=$prefix $state $GOV
UNIT
    $SUDO tee "/etc/systemd/system/$name-update.timer" >/dev/null <<UNIT
[Unit]
Description=check for quark updates ($ch server)
[Timer]
OnBootSec=1min
OnUnitInactiveSec=$every
[Install]
WantedBy=timers.target
UNIT
    $SUDO tee "/etc/systemd/system/$name-update-alert.service" >/dev/null <<UNIT
[Unit]
Description=quark update ($ch server) failed or stalled: alert
[Service]
Type=oneshot
User=$user
Group=$user
SupplementaryGroups=systemd-journal
EnvironmentFile=-/etc/quark/update.env
Environment=QUARK_STATE=$state QUARK_INSTANCE=$ch QUARK_UPDATE_UNIT=$name-update
ExecStart=/bin/sh $prefix/tools/server/update-alert.sh
$HARDEN
ReadWritePaths=$state
UNIT
}
[ "$PRODUCTION" = 1 ] && units quark quark "$PREFIX" "$STATE" production "$ARGS" "" 1min
[ "$PRODUCTION" = 0 ] && units quark-testing quark-testing "$TPREFIX" "$TSTATE" testing "$TARGS" "" 2min " QUARK_TESTING_ALWAYS=1"
[ "$BOTH" = 1 ] && units quark-testing quark-testing "$TPREFIX" "$TSTATE" testing "$TARGS" "Nice=5
CPUWeight=50
ExecCondition=/bin/test -e $TSTATE/active" 2min
# the testing server runs only while there's a testing build newer than production (autoupdate.sh makes / removes
# $TSTATE/active): this starts it when that file appears; its ExecCondition keeps it down while it's missing
[ "$BOTH" = 1 ] && $SUDO tee /etc/systemd/system/quark-testing-active.path >/dev/null <<UNIT
[Unit]
Description=start the quark testing server while there's a testing build newer than production
[Path]
PathExists=$TSTATE/active
Unit=quark-testing.service
[Install]
WantedBy=paths.target
UNIT
if [ "$PRODUCTION" = 0 ] && [ -f /etc/systemd/system/quark-testing-active.path ]; then
    $SUDO systemctl disable --now quark-testing-active.path 2>/dev/null || true
    $SUDO rm -f /etc/systemd/system/quark-testing-active.path
fi
$SUDO systemctl daemon-reload
if [ "$PRODUCTION" = 1 ]; then
    $SUDO systemctl enable --now quark.service quark-update.timer
    $SUDO systemctl restart quark.service
    echo "production: running as user quark from $PREFIX on UDP $PORT. status: systemctl status quark | logs: journalctl -u quark -f"
else
    [ -f "$OLDUNIT" ] && { $SUDO systemctl disable --now quark.service quark-update.timer 2>/dev/null || true
        echo "production: stopped and disabled (--testing-only); its data stays in $STATE. Bring it back: install.sh --both"; }
fi
if [ "$PRODUCTION" = 0 ]; then
    $SUDO systemctl enable --now quark-testing.service quark-testing-update.timer
    $SUDO systemctl restart quark-testing.service
    echo "testing only: user quark-testing from $TPREFIX on UDP $TPORT, always running the newest testing build or"
    echo "  production release, whichever is newer. logs: journalctl -u quark-testing -u quark-testing-update -f"
elif [ "$TESTING" = 1 ]; then
    $SUDO systemctl enable --now quark-testing.service quark-testing-update.timer quark-testing-active.path
    $SUDO systemctl restart quark-testing.service
    echo "testing: user quark-testing from $TPREFIX on UDP $TPORT (forward it too). It runs only while there's a testing"
    echo "  build newer than production (checked every 2 min). logs: journalctl -u quark-testing -u quark-testing-update -f"
elif [ -f "$TUNIT" ]; then
    $SUDO systemctl disable --now quark-testing.service quark-testing-update.timer quark-testing-active.path 2>/dev/null || true
    echo "testing: stopped and disabled (--no-testing); its data stays in $TSTATE. Bring it back: install.sh --testing"
fi
echo "updates: journalctl -u quark-update -u quark-testing-update (failures: journalctl -p crit -t quark-update; webhook: /etc/quark/update.env)"
if [ "$BOTH" = 1 ]; then
    echo "load governor: cat $GOV/*.status; its decisions are logged by both servers (journalctl -u quark -u quark-testing | grep governor)"
fi

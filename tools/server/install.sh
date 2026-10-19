#!/bin/sh
# One-time setup on a Linux server (systemd): builds quark_server, installs it as a service that starts
# on boot and restarts on crashes, and a timer that checks for updates every minute.
#   tools/server/install.sh --ranked --variant vanilla --map temple --public --name "my server"
# (everything after install.sh is passed to quark_server; run again to change the options)
set -e
cd "$(dirname "$0")/../.."
DIR=$(pwd)
ARGS=""
for a in "$@"; do ARGS="$ARGS \"$(printf '%s' "$a" | sed 's/["\\]/\\&/g')\""; done   # quoted for systemd (names with spaces)
[ -z "$ARGS" ] && ARGS="--public"
rm -f .built-commit
sh tools/server/autoupdate.sh || { echo "build failed"; exit 1; }
rm -f update.pending
sudo tee /etc/systemd/system/quark.service >/dev/null <<UNIT
[Unit]
Description=quark server
After=network-online.target
[Service]
User=$(id -un)
WorkingDirectory=$DIR
ExecStart=$DIR/bin/quark_server --restart-flag $DIR/update.pending $ARGS
Restart=always
RestartSec=2
[Install]
WantedBy=multi-user.target
UNIT
sudo tee /etc/systemd/system/quark-update.service >/dev/null <<UNIT
[Unit]
Description=quark update check
[Service]
Type=oneshot
User=$(id -un)
ExecStart=/bin/sh $DIR/tools/server/autoupdate.sh
UNIT
sudo tee /etc/systemd/system/quark-update.timer >/dev/null <<UNIT
[Unit]
Description=check for quark updates every minute
[Timer]
OnBootSec=1min
OnUnitInactiveSec=1min
[Install]
WantedBy=timers.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable --now quark.service quark-update.timer
sudo systemctl restart quark.service
echo "running. status: systemctl status quark | logs: journalctl -u quark -f | updates: journalctl -u quark-update"

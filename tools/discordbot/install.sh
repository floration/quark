#!/usr/bin/env bash
# Installs the quark Discord bot as a service on a Linux server (Ubuntu / Debian / Oracle Linux), from this folder:
#   sudo ./install.sh
# It asks for the bot token, server (guild) id and BOT_KEY once, keeps them in /etc/quark-discord.env (root only),
# makes a Python venv here, and runs the bot under systemd: it starts at boot and restarts if it crashes.
# Re-run it after pulling updates (it keeps the saved settings unless you type new ones).
set -euo pipefail
cd "$(dirname "$0")"
HERE="$(pwd)"
ENVFILE=/etc/quark-discord.env
RUNAS="${SUDO_USER:-$(whoami)}"

if [ "$(id -u)" -ne 0 ]; then echo "run it with sudo: sudo ./install.sh"; exit 1; fi

echo "== python"
if command -v apt-get >/dev/null; then
    apt-get update -qq && apt-get install -y -qq python3 python3-venv python3-pip >/dev/null
elif command -v dnf >/dev/null; then
    dnf install -y -q python3 python3-pip >/dev/null
fi
python3 -c 'import sys; assert sys.version_info >= (3, 9), "python 3.9+ needed"'

echo "== settings (Enter keeps the saved value)"
old() { [ -f "$ENVFILE" ] && grep -E "^$1=" "$ENVFILE" | head -1 | cut -d= -f2- || true; }
ask() { # name, prompt, secret
    local cur; cur="$(old "$1")"
    local shown="${cur:+(saved)}"
    if [ "${3:-}" = secret ]; then read -r -s -p "$2 $shown: " v; echo; else read -r -p "$2 ${cur:+[$cur]}: " v; fi
    printf '%s' "${v:-$cur}"
}
TOKEN="$(ask DISCORD_TOKEN 'Discord bot token' secret)"
GUILD="$(ask GUILD_ID 'Discord server id')"
BOTKEY="$(ask BOT_KEY 'BOT_KEY (same as the Cloudflare worker secret)' secret)"
[ -n "$TOKEN" ] && [ -n "$GUILD" ] || { echo "token and server id are required"; exit 1; }
umask 077
printf 'DISCORD_TOKEN=%s\nGUILD_ID=%s\nBOT_KEY=%s\n' "$TOKEN" "$GUILD" "$BOTKEY" > "$ENVFILE"
chmod 600 "$ENVFILE"

echo "== venv and requirements"
sudo -u "$RUNAS" python3 -m venv "$HERE/.venv"
sudo -u "$RUNAS" "$HERE/.venv/bin/pip" install -q --upgrade pip
sudo -u "$RUNAS" "$HERE/.venv/bin/pip" install -q -r "$HERE/requirements.txt"

echo "== dry run (reads the live server list, leaderboards and latest release; nothing is posted)"
sudo -u "$RUNAS" env $(cat "$ENVFILE" | xargs) "$HERE/.venv/bin/python" "$HERE/bot.py" --dry-run | head -40 || true

echo "== service"
cat > /etc/systemd/system/quark-discord.service <<UNIT
[Unit]
Description=quark discord bot
After=network-online.target
Wants=network-online.target

[Service]
User=$RUNAS
WorkingDirectory=$HERE
EnvironmentFile=$ENVFILE
ExecStart=$HERE/.venv/bin/python $HERE/bot.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now quark-discord
systemctl restart quark-discord
sleep 3
systemctl --no-pager --lines=15 status quark-discord || true
echo
echo "done. live log: journalctl -u quark-discord -f    restart: sudo systemctl restart quark-discord"

#!/usr/bin/env bash
# Installs the quark Discord bot as a service on a Linux server (Ubuntu / Debian / Oracle Linux), from this folder:
#   sudo ./install.sh
# It asks for the bot token, server (guild) id and BOT_KEY once, keeps them in /etc/quark-discord.env (mode 600, owned
# by the user the bot runs as; systemd reads it with EnvironmentFile=, so the secrets never appear on a command line
# or in the unit file), makes a Python venv here with hash-checked requirements, and runs the bot under systemd: it
# starts at boot and restarts if it crashes.
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
    if [ "${3:-}" = secret ]; then read -r -s -p "$2 $shown: " v; echo >&2; [ -n "$v" ] && echo "  (got ${#v} characters - a bot token is about 72)" >&2; else read -r -p "$2 ${cur:+[$cur]}: " v; fi
    printf '%s' "${v:-$cur}"
}
TOKEN="$(ask DISCORD_TOKEN 'Discord bot token' secret)"
GUILD="$(ask GUILD_ID 'Discord server id')"
BOTKEY="$(ask BOT_KEY 'BOT_KEY (same as the Cloudflare worker secret)' secret)"
[ -n "$TOKEN" ] && [ -n "$GUILD" ] || { echo "token and server id are required"; exit 1; }
umask 077
# (printf is a shell builtin: the values never show up in a process list. The file is made 600 before anything is written.)
TMPENV="$(mktemp "$ENVFILE.XXXXXX")"
chmod 600 "$TMPENV"
printf 'DISCORD_TOKEN=%s\nGUILD_ID=%s\nBOT_KEY=%s\n' "$TOKEN" "$GUILD" "$BOTKEY" > "$TMPENV"
chown "$RUNAS:" "$TMPENV"
mv -f "$TMPENV" "$ENVFILE"
unset TOKEN BOTKEY
[ "$RUNAS" = root ] && echo "note: the bot will run as root; run this with sudo from a normal user instead (sudo ./install.sh) to run it as that user"

echo "== venv and requirements"
sudo -u "$RUNAS" python3 -m venv "$HERE/.venv"
sudo -u "$RUNAS" "$HERE/.venv/bin/pip" install -q --upgrade pip
sudo -u "$RUNAS" "$HERE/.venv/bin/pip" install -q --require-hashes -r "$HERE/requirements.txt"

echo "== dry run (reads the live server list, leaderboards and latest release; nothing is posted)"
# (the dry run needs none of the secrets: they aren't passed to it)
( sudo -u "$RUNAS" "$HERE/.venv/bin/python" "$HERE/bot.py" --dry-run 2>&1 | head -40 ) || true

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
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now quark-discord
systemctl restart quark-discord
sleep 3
systemctl --no-pager --lines=15 status quark-discord || true
echo
echo "If this replaced an older install that passed the token on the command line: reset the bot token"
echo "(Discord developer portal -> Bot -> Reset Token) and BOT_KEY (Cloudflare worker secret), then re-run this."
echo "done. live log: journalctl -u quark-discord -f    restart: sudo systemctl restart quark-discord"

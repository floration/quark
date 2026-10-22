#!/bin/sh
# Run by quark-update-alert.service when an update check fails (quark-update.service OnFailure; install.sh): logs
# the failure at critical priority every time, and posts it to QUARK_ALERT_WEBHOOK (a Discord webhook URL, set in
# /etc/quark/update.env) at most once an hour.
STATE=${QUARK_STATE:-/var/lib/quark}
WHY=$(journalctl -u quark-update -n 50 --no-pager -o cat 2>/dev/null | grep 'quark-update: ERROR' | tail -n1)
MSG="quark server $(hostname): auto-update failed: ${WHY:-see journalctl -u quark-update}"
echo "$MSG" | systemd-cat -t quark-update -p crit 2>/dev/null || echo "$MSG" >&2
[ -n "${QUARK_ALERT_WEBHOOK:-}" ] || exit 0
[ -z "$(find "$STATE/alerted" -mmin -60 2>/dev/null)" ] || exit 0   # (already told them within the hour)
touch "$STATE/alerted"
python3 -c 'import json, sys; print(json.dumps({"content": sys.argv[1][:1900], "allowed_mentions": {"parse": []}}))' "$MSG" \
    | curl -sf -m 20 -H "Content-Type: application/json" -d @- "$QUARK_ALERT_WEBHOOK" >/dev/null || true

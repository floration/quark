#!/bin/sh
# Run by an instance's alert service when its update check fails (OnFailure; install.sh): quark-update-alert.service
# for the production server, quark-testing-update-alert.service for the testing one. Logs the failure at critical
# priority every time (journalctl -p crit -t quark-update shows both instances), naming the instance, and posts it to
# QUARK_ALERT_WEBHOOK (a Discord webhook URL, set in /etc/quark/update.env) at most once an hour per instance.
STATE=${QUARK_STATE:-/var/lib/quark}
UNIT=${QUARK_UPDATE_UNIT:-quark-update}      # the update service whose log says why
WHO=${QUARK_INSTANCE:-production}            # production / testing
WHY=$(journalctl -u "$UNIT" -n 50 --no-pager -o cat 2>/dev/null | grep 'quark-update: ERROR' | tail -n1)
MSG="quark $WHO server $(hostname): auto-update failed: ${WHY:-see journalctl -u $UNIT}"
echo "$MSG" | systemd-cat -t quark-update -p crit 2>/dev/null || echo "$MSG" >&2
[ -n "${QUARK_ALERT_WEBHOOK:-}" ] || exit 0
[ -z "$(find "$STATE/alerted" -mmin -60 2>/dev/null)" ] || exit 0   # (already told them within the hour)
touch "$STATE/alerted"
python3 -c 'import json, sys; print(json.dumps({"content": sys.argv[1][:1900], "allowed_mentions": {"parse": []}}))' "$MSG" \
    | curl -sf -m 20 -H "Content-Type: application/json" -d @- "$QUARK_ALERT_WEBHOOK" >/dev/null || true

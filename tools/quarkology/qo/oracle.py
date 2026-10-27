"""Source `oracle` (600 s; on demand at most once per 120 s): health of the Oracle box (production game server,
testing server, Discord bot and their update timers) over ONE read-only SSH connection.

Live players are on that box, so the check is tiny: a fixed command (built here, no input from anywhere in it),
everything under `nice -n 19 ionice -c3`, nothing is ever started, stopped or written there.
"""
import os
import re
import shutil

from . import util

KEY = "oracle"
INTERVAL = 600
MIN_GAP = 120
UNITS = ("quark", "quark-testing", "quark-discord",
         "quark-update.timer", "quark-testing-update.timer", "quark-discord-update.timer")

# One shell line; the @@ markers only label the sections for the parser. No double quotes (Windows argv quoting).
REMOTE_COMMAND = (
    "nice -n 19 ionice -c3 sh -c '"
    "echo @@uptime; uptime -p; "
    "echo @@load; cat /proc/loadavg; "
    "echo @@mem; free -m | sed -n 2p; "
    "for u in " + " ".join(UNITS) + "; do echo @@unit $u; "
    "systemctl show -p Id,ActiveState,SubState,ActiveEnterTimestamp $u; done; "
    "echo @@botupdate; journalctl -u quark-discord-update -n 3 --no-pager -o cat 2>/dev/null; "
    "echo @@serverupdate; journalctl -u quark-update -n 3 --no-pager -o cat 2>/dev/null; "
    "echo @@end'"
)
HOST_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9.\-]{0,252})$")
USER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.\-]{0,31}$")


def ssh_exe():
    if util.IS_WINDOWS:
        sysroot = os.environ.get("SystemRoot", "C:\\Windows")
        p = os.path.join(sysroot, "System32", "OpenSSH", "ssh.exe")
        if os.path.isfile(p):
            return p
    return shutil.which("ssh")


def build_args(exe, host, user, key):
    """The whole ssh command line. host / user are checked against strict patterns first (no option injection)."""
    return [exe, "-T", "-i", key,
            "-o", "BatchMode=yes", "-o", "GSSAPIAuthentication=no", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=accept-new",
            "-o", "IdentitiesOnly=yes", "-o", "LogLevel=ERROR",
            "%s@%s" % (user, host), REMOTE_COMMAND]


def parse_timestamp(s):
    """systemd 'Sat 2026-10-24 05:01:02 UTC' -> ISO (UTC / GMT only; otherwise None, the raw text is kept)."""
    m = re.search(r"(\d{4}-\d{2}-\d{2}) (\d{2}:\d{2}:\d{2})(?: (UTC|GMT))?", s or "")
    if m and m.group(3):
        return "%sT%sZ" % (m.group(1), m.group(2))
    return None


def parse(text):
    data = {"uptime": None, "load": None, "memUsedMb": None, "memTotalMb": None, "units": {},
            "botUpdate": [], "serverUpdate": [], "complete": False}
    section, unit = None, None
    for raw in text.splitlines():
        line = raw.rstrip()
        if line.startswith("@@"):
            parts = line[2:].split(None, 1)
            section = parts[0] if parts else None
            if section == "unit" and len(parts) > 1:
                unit = parts[1].strip()
                data["units"][unit] = {"active": None, "sub": None, "since": None, "sinceRaw": None}
            if section == "end":
                data["complete"] = True
            continue
        if not line.strip():
            continue
        if section == "uptime":
            data["uptime"] = line.strip()
        elif section == "load":
            nums = line.split()[:3]
            try:
                data["load"] = [float(x) for x in nums]
            except ValueError:
                pass
        elif section == "mem":
            p = line.split()
            if len(p) >= 3 and p[1].isdigit() and p[2].isdigit():
                data["memTotalMb"], data["memUsedMb"] = int(p[1]), int(p[2])
        elif section == "unit" and unit and "=" in line:
            k, v = line.split("=", 1)
            u = data["units"][unit]
            if k == "ActiveState":
                u["active"] = v
            elif k == "SubState":
                u["sub"] = v
            elif k == "ActiveEnterTimestamp":
                u["sinceRaw"] = v or None
                u["since"] = parse_timestamp(v) or (v or None)
            elif k == "Id":
                u["id"] = v
        elif section == "botupdate":
            data["botUpdate"].append(line)
        elif section == "serverupdate":
            data["serverUpdate"].append(line)
    return data


def fetch(ctx, runner=None):
    cfg = ctx.config().get("oracle") or {}
    host, user = str(cfg.get("host") or ""), str(cfg.get("user") or "")
    key = os.path.expandvars(os.path.expanduser(str(cfg.get("key") or "")))
    if not HOST_RE.match(host) or not USER_RE.match(user):
        raise util.SourceError("config.json oracle host / user is not valid")
    if not key or not os.path.isfile(key):
        raise util.SourceError("SSH key not found")
    exe = ssh_exe()
    if not exe:
        raise util.SourceError("ssh.exe not found (Windows: Settings > Optional features > OpenSSH Client)")
    code, out, err = (runner or util.run)(build_args(exe, host, user, key), timeout=45.0)
    if "@@uptime" not in out:
        msg = (err.strip().splitlines() or ["exit code %s" % code])[-1]
        raise util.SourceError("SSH failed: %s" % msg[:200])
    data = parse(out)
    data["host"] = "%s@%s" % (user, host)
    return data

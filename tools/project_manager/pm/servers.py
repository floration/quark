"""Source `servers` (120 s): the ranking Worker's public server list for the production and testing versions,
and its ranked queue. Three small GETs, 5 s timeout each."""
import urllib.parse

from . import util

KEY = "servers"
INTERVAL = 120
BASE = "https://quark-master.jamesrandallmcamis.workers.dev"


def parse_list(text):
    return [l.strip() for l in text.splitlines() if l.strip()]


def parse_queue(text):
    """'<mode> <queued> <match size>' per line -> [{mode, queued, size}] (the Worker's /queue)."""
    out = []
    for line in text.splitlines():
        p = line.split()
        if len(p) >= 2 and p[1].isdigit():
            out.append({"mode": p[0], "queued": int(p[1]), "size": int(p[2]) if len(p) > 2 and p[2].isdigit() else None})
    return out


def _text(url):
    _, body = util.http_get(url, timeout=5.0, local=False)
    return body.decode("utf-8", errors="replace")


def fetch(ctx):
    v = ctx.versions()
    wanted = {"production": v.get("production"), "testing": v.get("testingServer") or v.get("testing")}
    if not any(wanted.values()):
        raise util.Retry("waiting for the production / testing versions from the other sources", 2)
    data, errors = {}, []
    for ch, ver in wanted.items():
        if not ver:
            data[ch] = {"version": None, "count": None, "addresses": [], "error": "version unknown"}
            continue
        try:
            addrs = parse_list(_text(BASE + "/list?v=" + urllib.parse.quote(ver[:16], safe="")))
            data[ch] = {"version": ver, "count": len(addrs), "addresses": addrs}
        except util.SourceError as e:
            data[ch] = {"version": ver, "count": None, "addresses": [], "error": str(e)}
            errors.append(str(e))
    try:
        raw = _text(BASE + "/queue")
        q = parse_queue(raw)
        data["queue"] = raw.strip()
        data["queueModes"] = q
        data["queued"] = sum(x["queued"] for x in q)
    except util.SourceError as e:
        data["queue"] = None
        data["queueError"] = str(e)
        errors.append(str(e))
    if len(errors) == 3 or (errors and all(data[c].get("count") is None for c in wanted)):
        raise util.SourceError("ranking Worker unreachable (%s)" % errors[0])
    return data

"""Source `editors` (30 s): the Editor Manager's summary (8765) (versions, provenance, programs)."""
from . import util

KEY = "editors"
INTERVAL = 30
_CACHE = util.CachedJSON()
URL = "http://127.0.0.1:8765/api/summary"


def fetch(ctx):
    try:
        return _CACHE.get(URL, timeout=2.5)
    except util.SourceError as e:
        if "unreachable" in str(e):
            raise util.SourceError("Editor Manager not running")
        raise util.SourceError("Editor Manager is running but /api/summary failed (%s)" % str(e)[:80])

"""Source `docs` (30 s): the Claude Manager's summary (8766)."""
from . import util

KEY = "docs"
INTERVAL = 30
_CACHE = util.CachedJSON()
URL = "http://127.0.0.1:8766/api/summary"


def fetch(ctx):
    try:
        return _CACHE.get(URL, timeout=2.5)
    except util.SourceError as e:
        if "unreachable" in str(e):
            raise util.SourceError("Claude Manager not running")
        raise util.SourceError("Claude Manager is running but /api/summary failed (%s)" % str(e)[:80])

"""Source `docs` (30 s): the Claude Docs manager's summary."""
from . import util

KEY = "docs"
INTERVAL = 30
URL = "http://127.0.0.1:8766/api/summary"


def fetch(ctx):
    try:
        return util.http_json(URL, timeout=2.5)
    except util.SourceError as e:
        if "unreachable" in str(e):
            raise util.SourceError("Claude Docs manager not running")
        raise util.SourceError("Claude Docs manager is running but /api/summary failed (%s)" % str(e)[:80])

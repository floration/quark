"""Source `editors` (30 s): the editor dashboard's summary (versions, provenance, programs)."""
from . import util

KEY = "editors"
INTERVAL = 30
URL = "http://127.0.0.1:8765/api/summary"


def fetch(ctx):
    try:
        return util.http_json(URL, timeout=2.5)
    except util.SourceError as e:
        if "unreachable" in str(e):
            raise util.SourceError("Editor dashboard not running")
        raise util.SourceError("Editor dashboard is running but /api/summary failed (%s)" % str(e)[:80])

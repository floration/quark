"""Source `github` (30 s): the GitHub manager's summary, else its on-disk cache."""
import os

from . import util

KEY = "github"
INTERVAL = 30
URL = "http://127.0.0.1:8767/api/summary"


def fetch(ctx):
    try:
        return util.http_json(URL, timeout=2.5), {"via": "live"}
    except util.SourceError as live_err:
        cache = ctx.paths.q("github-manager", "cache", "github.json")
        try:
            data = util.read_json_file(cache)
            mtime = os.path.getmtime(cache)
        except FileNotFoundError:
            if "unreachable" in str(live_err):
                raise util.SourceError("GitHub manager not running")
            raise util.SourceError("GitHub manager is running but /api/summary failed (%s)" % str(live_err)[:80])
        except (OSError, ValueError):
            raise util.SourceError("GitHub manager not running (its cache file is unreadable)")
        return data, {"via": "cache", "stale": True, "cacheTime": util.iso(mtime),
                      "note": "GitHub manager not running; showing its cache (%s)" % str(live_err)[:80]}

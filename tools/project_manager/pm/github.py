"""Source `github` (30 s): the GitHub Manager's /api/summary (tools/github_manager/API.md), else its on-disk cache
(cache/github.json: the /api/state object minus `local`)."""
import os

from . import util

KEY = "github"
INTERVAL = 30
_CACHE = util.CachedJSON()
URL = "http://127.0.0.1:8767/api/summary"


def fetch(ctx):
    try:
        return _CACHE.get(URL, timeout=2.5), {"via": "live"}
    except util.SourceError as live_err:
        cache = ctx.paths.q("github-manager", "cache", "github.json")
        try:
            data = util.read_json_file(cache)
            mtime = os.path.getmtime(cache)
        except FileNotFoundError:
            if "unreachable" in str(live_err):
                raise util.SourceError("GitHub Manager not running")
            raise util.SourceError("GitHub Manager is running but /api/summary failed (%s)" % str(live_err)[:80])
        except (OSError, ValueError):
            raise util.SourceError("GitHub Manager not running (its cache file is unreadable)")
        return data, {"via": "cache", "stale": True, "cacheTime": util.iso(mtime),
                      "note": "GitHub Manager not running; showing its cache (%s)" % str(live_err)[:80]}

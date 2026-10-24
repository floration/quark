"""Source `checklist` (300 s): the section headings of repository\\docs\\RELEASE_CHECKLIST.md."""
import os
import re

from . import util

KEY = "checklist"
INTERVAL = 300
URL_BASE = "https://github.com/floration/quark/blob/testing/docs/RELEASE_CHECKLIST.md"


def parse(text):
    """'## 4. Settings' lines -> ['4. Settings', ...] (level-2 headings only; fenced code skipped)."""
    out, fence = [], False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        if not fence:
            m = re.match(r"##\s+(.+?)\s*#*\s*$", line)
            if m and not line.startswith("###"):
                out.append(m.group(1))
    return out


def fetch(ctx):
    path = ctx.paths.q("repository", "docs", "RELEASE_CHECKLIST.md")
    try:
        text = util.read_text(path)
        mtime = os.path.getmtime(path)
    except FileNotFoundError:
        raise util.SourceError("repository\\docs\\RELEASE_CHECKLIST.md not found")
    except OSError as e:
        raise util.SourceError("RELEASE_CHECKLIST.md unreadable (%s)" % type(e).__name__)
    title = next((l[2:].strip() for l in text.splitlines() if l.startswith("# ")), None)
    return {"title": title, "headings": parse(text), "mtime": util.iso(mtime), "path": path, "url": URL_BASE}

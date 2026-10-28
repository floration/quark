"""Source `builds` (60 s): the local game folders and what their BUILD.txt says is in them.

BUILD.txt is one line whose format depends on what wrote it (docs/LOCAL_BUILD.md), for example
  v1.18.5 (GitHub release, published ...)                         tools/fetch_release.ps1
  quark-1.18.6-9c2e1f0-dirty (local build, 2026-10-24 18:58)      tools/build_windows.ps1 (build-working.cmd)
  testing origin/testing <40-hex sha>.3 (built on this PC ...; unsigned)   tools/build_release.ps1
                                                    (1.19.0 to 1.19.1: <sha>.testing.341, before: <sha>-test.231)
so it is parsed loosely and the raw lines are always kept.
"""
import os
import re

from . import util

KEY = "builds"
INTERVAL = 60
FOLDERS = ("production", "testing", "working", "testing-f3")


def parse_build_txt(text):
    lines = [l.rstrip() for l in text.splitlines() if l.strip()][:20]
    joined = "\n".join(lines)
    out = {"lines": lines, "version": None, "tag": None, "commit": None, "dirty": None, "kind": None}
    m = re.search(r"(?<![\w-])(v\d+\.\d+\.\d+|test-\d+\.\d+\.\d+[\w.\-]*)", joined)
    if m:
        out["tag"] = m.group(1)
    out["version"] = util.parse_version(joined)
    if out["version"] is None:
        # build_release.ps1's testing marker has no VERSION, only ".<n>" (earlier ".testing.<n>", "-test.<n>") after the sha
        s = re.search(r"[0-9a-f]{7,40}((?:\.testing|-test)?\.\d+)", joined)
        if s:
            out["suffix"] = s.group(1)
    c = (re.search(r"\b([0-9a-f]{40})", joined) or
         re.search(r"quark-\d+\.\d+\.\d+(?:-[A-Za-z]+(?:\.\d+)*)?-([0-9a-f]{7,40})\b", joined) or
         re.search(r"(?i)\bcommit\W+([0-9a-f]{7,40})\b", joined) or
         re.search(r"(?<![\w.:/-])([0-9a-f]{7,40})(?![\w.:/])", joined))
    if c:
        sha = c.group(1)
        out["commit"] = sha[:7]
        out["sha"] = sha if len(sha) == 40 else None
    # fetch_release.ps1: "(GitHub release, published 2026-10-20T18:00:00Z)" (ISO only; anything else is left out)
    p = re.search(r"(?i)\bpublished\s+(\d{4}-\d\d-\d\dT[\d:.]+(?:Z|[+-]\d\d:?\d\d)?)", joined)
    out["publishedAt"] = util.iso(util.parse_time(p.group(1))) if p and util.parse_time(p.group(1)) else None
    low = joined.lower()
    out["dirty"] = "-dirty" in low or "(dirty)" in low
    if "github release" in low:
        out["kind"] = "release"
    elif "local build" in low:
        out["kind"] = "local build"
    elif "built on this pc" in low:
        out["kind"] = "local release build"
    return out


def read_folder(ctx, name):
    folder = ctx.paths.q(name)
    entry = {"name": name, "path": folder, "present": os.path.isdir(folder), "version": None, "commit": None,
             "mtime": None, "lines": []}
    if not entry["present"]:
        entry["note"] = "folder missing"
        return entry
    marker = os.path.join(folder, "BUILD.txt")
    try:
        text = util.read_text(marker, 64 * 1024)
        entry["mtime"] = util.iso(os.path.getmtime(marker))
    except FileNotFoundError:
        entry["note"] = "no BUILD.txt"
        return entry
    except OSError as e:
        entry["note"] = "BUILD.txt unreadable (%s)" % type(e).__name__
        return entry
    entry.update(parse_build_txt(text))
    if entry["version"] is None:
        # (the source it was built from: Quark\.src\<channel>\VERSION, when build_release.ps1 made it)
        try:
            v = util.parse_version(util.read_text(ctx.paths.q(".src", name, "VERSION"), 256))
            if v:
                entry["version"] = v + entry.get("suffix", "")
                entry["versionFrom"] = ".src\\%s\\VERSION" % name
        except OSError:
            pass
    return entry


def fetch(ctx):
    return {"folders": [read_folder(ctx, n) for n in FOLDERS]}

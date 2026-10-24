"""Source `repo` (60 s): Quark\\repository\\ (the mirror of testing) and the local clones' git state.

Only read-only git commands, with --no-optional-locks so not even the index is refreshed.
"""
import os
import re

from . import util

KEY = "repo"
INTERVAL = 60
LOCAL_COPIES = ("repository", "working-repo")
LOG_FORMAT = "%H%x1f%h%x1f%cI%x1f%s"


def git(path, *args, timeout=10.0):
    code, out, err = util.run(["git", "--no-optional-locks", "-C", path] + list(args), timeout=timeout)
    if code != 0:
        raise util.SourceError("git %s failed in %s: %s" % (args[0], os.path.basename(path), err.strip()[:160]))
    return out


def parse_log(text):
    out = []
    for line in text.splitlines():
        parts = line.split("\x1f")
        if len(parts) >= 4:
            out.append({"sha": parts[0], "short": parts[1], "time": parts[2], "subject": "\x1f".join(parts[3:])})
    return out


def parse_status(text):
    """`git status --porcelain=v1 -b` -> {branch, upstream, ahead, behind, files:[paths]}."""
    res = {"branch": None, "upstream": None, "ahead": 0, "behind": 0, "files": []}
    for line in text.splitlines():
        if line.startswith("## "):
            head = line[3:]
            m = re.match(r"(?:No commits yet on |Initial commit on )?(.+?)(?:\.\.\.(\S+))?(?: \[(.*)\])?$", head)
            if m:
                res["branch"] = None if m.group(1).startswith("HEAD (no branch)") else m.group(1)
                res["upstream"] = m.group(2)
                for k, n in re.findall(r"(ahead|behind) (\d+)", m.group(3) or ""):
                    res[k] = int(n)
                if "HEAD (no branch)" in head:
                    res["branch"] = None
                    res["detached"] = True
        elif len(line) > 3:
            p = line[3:]
            if " -> " in p:
                p = p.split(" -> ", 1)[1]
            res["files"].append(p.strip('"'))
    return res


def local_copy(ctx, name):
    path = ctx.paths.q(name)
    entry = {"name": name, "path": path, "present": os.path.isdir(os.path.join(path, ".git"))}
    if not entry["present"]:
        entry["note"] = "not a git clone" if os.path.isdir(path) else "folder missing"
        return entry
    try:
        st = parse_status(git(path, "status", "--porcelain=v1", "-b", "--untracked-files=normal"))
        head = parse_log(git(path, "log", "-1", "--format=" + LOG_FORMAT))
    except util.SourceError as e:
        entry["error"] = str(e)
        return entry
    files = st.pop("files")
    entry.update(st)
    entry["dirty"] = bool(files)
    entry["dirtyFiles"] = len(files)
    oldest = None
    for p in files[:200]:  # (the oldest change decides "uncommitted for more than a day")
        try:
            t = os.path.getmtime(os.path.join(path, p))
        except OSError:
            continue
        oldest = t if oldest is None or t < oldest else oldest
    entry["dirtySince"] = util.iso(oldest)
    if head:
        entry["sha"] = head[0]["sha"]
        entry["time"] = head[0]["time"]
        entry["subject"] = head[0]["subject"]
    return entry


def changelogs(root, limit=8):
    folder = os.path.join(root, "changelogs")
    try:
        names = [n for n in os.listdir(folder) if n.endswith(".md") and util.parse_version(n[:-3]) == n[:-3]]
    except OSError:
        return []
    names.sort(key=lambda n: util.version_key(n[:-3]), reverse=True)
    out = []
    for n in names[:limit]:
        p = os.path.join(folder, n)
        try:
            lines = util.read_text(p, 64 * 1024).splitlines()[:12]
            out.append({"version": n[:-3], "mtime": util.iso(os.path.getmtime(p)), "lines": lines})
        except OSError:
            continue
    return out


def fetch(ctx):
    root = ctx.paths.q("repository")
    if not os.path.isdir(root):
        raise util.SourceError("Quark\\repository\\ not found")
    data = {"path": root, "version": None, "fullChangelog": False}
    try:
        data["version"] = util.read_text(os.path.join(root, "VERSION"), 256).strip().splitlines()[0].strip()
    except (OSError, IndexError):
        data["versionError"] = "no VERSION file"
    data["changelogs"] = changelogs(root)
    if data["version"]:
        data["fullChangelog"] = os.path.isfile(os.path.join(root, "changelogs", "full", data["version"] + ".md"))
        data["shortChangelog"] = os.path.isfile(os.path.join(root, "changelogs", data["version"] + ".md"))
    try:
        data["log"] = parse_log(git(root, "log", "-12", "--format=" + LOG_FORMAT))
    except util.SourceError as e:
        data["log"] = []
        data["logError"] = str(e)
    data["locals"] = [local_copy(ctx, n) for n in LOCAL_COPIES]
    return data

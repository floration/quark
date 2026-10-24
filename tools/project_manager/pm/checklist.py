"""Source `checklist` (300 s): the section headings of repository\\docs\\RELEASE_CHECKLIST.md; and q's ticks of those
sections per testing version (`Ticks`, POST /api/checklist)."""
import os
import re
import threading

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


# ---- q's ticks: which sections of the checklist he has hand-tested, per testing VERSION ----------------------------
# Quark\project-manager\checklist-ticks.json: {"<version>": {"<section key>": {"at": iso, "build": "<testing build>"}}}.
# A section is keyed by its heading without the number ("4. Settings" -> "Settings"), so renumbering the checklist
# keeps the ticks. Only the newest KEEP_VERSIONS versions are kept.
TICKS_FILE = "checklist-ticks.json"
KEEP_VERSIONS = 20
MAX_SECTION = 200


def section_key(heading):
    return re.sub(r"^\s*\d+[.)]\s*", "", str(heading or "")).strip()[:MAX_SECTION]


class Ticks:
    def __init__(self, paths):
        self.paths = paths
        self.path = paths.pm(TICKS_FILE)
        self.lock = threading.Lock()
        try:
            d = util.read_json_file(self.path)
        except (OSError, ValueError):
            d = {}
        self.data = {v: s for v, s in d.items() if isinstance(s, dict)} if isinstance(d, dict) else {}

    def view(self, version):
        """{version, done: {section key: {at, build}}} for one version (None when the version is unknown)."""
        if not version:
            return None
        with self.lock:
            return {"version": version, "done": dict(self.data.get(version) or {})}

    def set(self, version, section, done, build=None):
        """Tick (done=True) or untick one section of one version. -> the version's view. Raises ValueError."""
        if not isinstance(version, str) or util.parse_version(version) != version or len(version) > 40:
            raise ValueError("not a version")
        key = section_key(section)
        if not key:
            raise ValueError("no section")
        with self.lock:
            secs = dict(self.data.get(version) or {})
            if done:
                secs[key] = {"at": util.now_iso(), "build": build if isinstance(build, str) else None}
            else:
                secs.pop(key, None)
            data = dict(self.data)
            if secs:
                data[version] = secs
            else:
                data.pop(version, None)
            for old in sorted(data, key=util.version_key)[:-KEEP_VERSIONS]:
                data.pop(old, None)
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self.path, data)  # (OSError goes to the caller: nothing changed in memory)
            self.data = data
            return {"version": version, "done": dict(secs)}

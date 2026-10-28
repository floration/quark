"""Source `checklist` (300 s): the section headings of repository\\docs\\RELEASE_CHECKLIST.md and each section's check
items; and q's ticks of those sections per testing version (`Ticks`, POST /api/checklist). The game checklist's
section "Quarkology and the editors" gets its items from this repository's CHECKLIST.md (Quark\\quarkology-repo\\
CHECKLIST.md), where the app's and the editors' checks live since they moved to floration/quarkology."""
import os
import re
import threading

from . import util

KEY = "checklist"
INTERVAL = 300
URL_BASE = "https://github.com/floration/quark/blob/testing/docs/RELEASE_CHECKLIST.md"
APP_URL = "https://github.com/floration/quarkology/blob/main/CHECKLIST.md"
APP_FILE = ("quarkology-repo", "CHECKLIST.md")
APP_SECTION = "Quarkology and the editors"   # (its key: section_key)


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


BOX = ("☐", "☑", "[ ]", "[x]", "[X]")  # (not ✓: the tables' header row is "| ✓ | Check |")
MAX_ITEM = 600


def parse_sections(text):
    """Each level-2 section with its check items: [{heading, items: [{text, group}]}]. An item is a table row whose
    first cell is a box (☐; its other cells joined with " · ") or a bullet; `group` is the ### heading above it."""
    out, fence, group = [], False, None
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        m = re.match(r"##\s+(.+?)\s*#*\s*$", line)
        if m and not line.startswith("###"):
            out.append({"heading": m.group(1), "items": []})
            group = None
            continue
        if not out:
            continue
        m = re.match(r"###\s+(.+?)\s*#*\s*$", line)
        if m:
            group = m.group(1)
            continue
        t = line.strip()
        if t.startswith("|"):
            cells = [c.strip() for c in t.strip("|").split("|")]
            if len(cells) >= 2 and cells[0] in BOX:
                item = " · ".join(c for c in cells[1:] if c)
            else:
                continue
        elif re.match(r"[-*]\s+\S", t):
            item = t[1:].strip()
        else:
            continue
        if item:
            out[-1]["items"].append({"text": item[:MAX_ITEM], "group": group})
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
    headings, sections = parse(text), parse_sections(text)
    app = merge_app(ctx.paths.q(*APP_FILE), headings, sections)
    # (`sections` holds every item: GET /api/checklist sends it; /api/pm sends only the headings)
    return {"title": title, "headings": headings, "sections": sections, "mtime": util.iso(mtime),
            "path": path, "url": URL_BASE, "app": app}


def merge_app(path, headings, sections):
    """The app repository's CHECKLIST.md into the game checklist's "Quarkology and the editors" section (in place): its
    items replace that section's (the game file only points at it), each in its ### group; a game checklist without
    the section gets it at the end, before the sign-off. -> {path, url, items, error} (None: no such file here)."""
    try:
        text = util.read_text(path)
    except FileNotFoundError:
        return None
    except OSError as e:
        return {"path": path, "url": APP_URL, "items": 0, "error": "unreadable (%s)" % type(e).__name__}
    items = []
    for sec in parse_sections(text):
        for it in sec["items"]:
            items.append({"text": it["text"], "group": it["group"] or sec["heading"], "from": "quarkology"})
    keys = [section_key(s["heading"]) for s in sections]
    if APP_SECTION in keys:
        sections[keys.index(APP_SECTION)]["items"] = items
    else:
        at = next((i for i, k in enumerate(keys) if k.lower().startswith("sign-off")), len(sections))
        sections.insert(at, {"heading": APP_SECTION, "items": items})
        headings.insert(min(at, len(headings)), APP_SECTION)
    return {"path": path, "url": APP_URL, "items": len(items), "error": None}


# ---- q's ticks: which sections of the checklist he has hand-tested, per testing VERSION ----------------------------
# Quark\quarkology\checklist-ticks.json: {"<version>": {"<section key>": {"at": iso, "build": "<testing build>"}}}.
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

    def set(self, version, section, done, build=None, sections=None, newest=None):
        """Tick (done=True) or untick one section of one version. -> the version's view. Raises ValueError.
        `sections`: the checklist's headings (a tick for any other section is refused); `newest`: the testing VERSION
        (a later version is refused: it would push real versions out of the KEEP_VERSIONS kept)."""
        if not isinstance(version, str) or util.parse_version(version) != version or len(version) > 40:
            raise ValueError("not a version")
        if newest and util.version_key(version) > util.version_key(newest):
            raise ValueError("version %s is newer than testing (%s)" % (version, newest))
        if not isinstance(section, str) or not isinstance(done, bool):
            raise ValueError("section must be text and done true or false")
        key = section_key(section)
        if not key:
            raise ValueError("no section")
        if sections is not None and key not in {section_key(h) for h in sections}:
            raise ValueError("no section %r in RELEASE_CHECKLIST.md" % key[:60])
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

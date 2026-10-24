"""tabs.json (Quark\\project-manager\\tabs.json) resolved the same way as the shell's main.js (SPEC 1, "tabs.json").

- `folder` is relative to Quark\\; %PM% = Quark\\project-manager, %QUARK% = Quark\\ in url / start / folder / file.
- A server tab (an http(s) url; probed / started only on 127.0.0.1 / localhost) without `start` uses the first *.cmd in its folder whose name contains the folder name,
  else start.cmd. A relative `start` program (`run.cmd`, `serve.py` after py -3) resolves against the tab's folder,
  which is also its working directory.
- A file tab (a file:// url, or folder + file) without `file` uses <folder>.html, else index.html, else the
  first *.html / *.htm at the folder's top.
- A missing folder or file -> installed: false, note "not installed".
- `keepAlive` (bool): the shell starts that local server when the app starts and restarts it whenever it stops
  (watchdog). `lowPriority` (bool): the shell runs it at low (idle) CPU priority, like `start /low`.
`up` comes from a background probe (GET http://127.0.0.1:<port>/, 500 ms); requests never wait for it.
"""
import http.client
import os
import pathlib
import re
import threading
import urllib.parse
import urllib.request

from . import util

# Used only when Quark\project-manager\tabs.json and shell\tabs.default.json are both missing; kept identical to
# shell/tabs.default.json (a test checks it).
DEFAULT_TABS = {"tabs": [
    {"id": "project", "name": "Supervisor Board", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
     "start": "py -3 \"%PM%\\server.py\"", "keepAlive": True},
    # (the Play tab is a page of the Supervisor Board server: same port, kept up by the project tab, so no keepAlive)
    {"id": "play", "name": "Play", "parent": "project", "url": "http://127.0.0.1:8768/play?embed=1", "port": 8768,
     "start": "py -3 \"%PM%\\server.py\""},
    {"id": "github", "name": "GitHub Manager", "parent": "project", "url": "http://127.0.0.1:8767/?embed=1", "port": 8767,
     "folder": "github-manager", "start": "py -3 -B server.py --data \"%QUARK%\\github-manager\"", "keepAlive": True,
     "lowPriority": True},
    {"id": "tests", "name": "Tests", "parent": "github", "url": "http://127.0.0.1:8775/?embed=1", "port": 8775,
     "folder": "tests-editor", "start": "py -3 serve.py --port 8775 --no-browser", "keepAlive": True},
    {"id": "docs", "name": "Claude Manager", "parent": "project", "url": "http://127.0.0.1:8766/?embed=1", "port": 8766,
     "folder": "docs-manager", "start": "py -3 serve.py --docs \"%QUARK%\\Claude Documents\" --port 8766 --quiet",
     "keepAlive": True},
    {"id": "dashboard", "name": "Editor Manager", "parent": "project", "url": "http://127.0.0.1:8765/?embed=1", "port": 8765,
     "folder": "editor-dashboard", "start": "py -3 dashboard.py --quark \"%QUARK%\" --no-browser", "keepAlive": True,
     "childLabel": "Editors"},
    {"id": "sound", "name": "Sound Editor", "parent": "dashboard", "folder": "sound-editor", "file": "sound-editor.html"},
    {"id": "ui", "name": "UI Editor", "parent": "dashboard", "folder": "ui-customizer", "file": "ui-customizer.html"},
    {"id": "map", "name": "Map Editor", "parent": "dashboard", "url": "http://127.0.0.1:8770/", "port": 8770,
     "folder": "map-editor", "start": "py -3 serve.py --port 8770 --no-browser", "keepAlive": True},
    {"id": "bot", "name": "Bot Editor", "parent": "dashboard", "url": "http://127.0.0.1:8769/?embed=1", "port": 8769,
     "folder": "bot-editor", "start": "py -3 serve.py --port 8769 --no-open", "keepAlive": True},
    {"id": "model", "name": "Model Editor", "parent": "dashboard", "folder": "model-editor"},
    {"id": "gamemode", "name": "Gamemode Editor", "parent": "dashboard", "url": "http://127.0.0.1:8771/?embed=1",
     "port": 8771, "folder": "gamemode-editor", "start": "py -3 serve.py --port 8771 --no-open", "keepAlive": True},
    {"id": "physics", "name": "Physics Editor", "parent": "dashboard", "url": "http://127.0.0.1:8772/?embed=1",
     "port": 8772, "folder": "physics-editor", "start": "py -3 serve.py --port 8772 --no-open", "keepAlive": True},
]}
PROBE_INTERVAL = 5.0


def expand(s, paths):
    if not isinstance(s, str):
        return s
    pm, quark = paths.pm_dir.rstrip("\\/"), paths.quark.rstrip("\\/")
    s = re.sub(r"%PM%", lambda m: pm, s, flags=re.I)
    return re.sub(r"%QUARK%", lambda m: quark, s, flags=re.I)


def _listdir(folder):
    try:
        return sorted(os.listdir(folder))  # (code-point order, like main.js's sort())
    except OSError:
        return []


def default_start(folder):
    """First *.cmd whose name contains the folder name, else start.cmd (None when neither exists)."""
    base = os.path.basename(folder.rstrip("\\/")).lower()
    names = _listdir(folder)
    for n in names:
        if n.lower().endswith(".cmd") and base in n.lower() and os.path.isfile(os.path.join(folder, n)):
            return os.path.join(folder, n)
    p = os.path.join(folder, "start.cmd")
    return p if os.path.isfile(p) else None


def default_file(folder):
    base = os.path.basename(folder.rstrip("\\/"))
    for n in (base + ".html", "index.html"):
        if os.path.isfile(os.path.join(folder, n)):
            return n
    for n in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if re.search(r"\.html?$", n, re.I) and os.path.isfile(os.path.join(folder, n)):
            return n
    return base + ".html"


def resolve_start(start, cwd):
    """'run.cmd' -> '"<folder>\\run.cmd"' when that file is in the tab's folder; other commands unchanged (they run
    with cwd = the tab's folder)."""
    m = re.match(r'^\s*(?:"([^"]+)"|(\S+))(.*)$', start, re.S)
    prog = m and (m.group(1) or m.group(2))
    if prog and cwd and not re.search(r"[\\/]", prog) and os.path.isfile(os.path.join(cwd, prog)):
        return '"%s"%s' % (os.path.join(cwd, prog), m.group(3))
    return start


def resolve_tab(raw, paths, index=0):
    """One tabs.json entry -> what the shell (main.js resolveTab) makes of it, plus every field q wrote."""
    t = {k: v for k, v in raw.items()}
    t["id"] = re.sub(r"[^A-Za-z0-9_-]", "-", str(raw.get("id") or "tab%d" % index))
    t["name"] = str(raw.get("name") or raw.get("id") or "Tab %d" % (index + 1))
    t["parent"] = re.sub(r"[^A-Za-z0-9_-]", "-", str(raw["parent"])) if raw.get("parent") else None
    t["childLabel"] = str(raw.get("childLabel") or "")
    t["keepAlive"] = raw.get("keepAlive") is True
    t["lowPriority"] = raw.get("lowPriority") is True
    t["depth"] = 0
    folder_rel = expand(raw.get("folder"), paths)
    folder = os.path.normpath(os.path.join(paths.quark, folder_rel)) if folder_rel else None
    t["folderPath"] = folder
    t.update(kind="file", installed=True, up=None, port=None, startCommand=None)
    url = expand(raw.get("url"), paths)
    file_path = None
    if url:
        u = urllib.parse.urlsplit(url)
        t["url"] = url
        if u.scheme in ("http", "https"):
            t["kind"] = "server"
            try:
                url_port = u.port
            except ValueError:
                url_port = None
            t["port"] = util.to_int(raw.get("port")) or url_port or (443 if u.scheme == "https" else 80)
            t["local"] = (u.hostname or "").lower() in ("127.0.0.1", "localhost", "::1")
            cwd = folder if folder and os.path.isdir(folder) else paths.pm_dir
            t["startCwd"] = cwd
            if t["local"]:
                start = expand(raw.get("start"), paths)
                if start:
                    t["startCommand"] = resolve_start(start, folder)
                elif folder and os.path.isdir(folder):
                    p = default_start(folder)
                    t["startCommand"] = '"%s"' % p if p else None
            if folder and not os.path.isdir(folder):
                t["installed"] = False
                t["note"] = "not installed"
        elif u.scheme == "file":
            file_path = urllib.request.url2pathname(u.path)
        else:
            t["installed"] = False
            t["note"] = "unsupported url"
    if t["kind"] == "file":
        if not file_path and t["installed"]:
            name = expand(raw.get("file"), paths)
            if not folder:
                t["installed"] = False
                t["note"] = "not installed"
            elif not os.path.isdir(folder):
                t["installed"] = False
                t["note"] = "not installed"
            else:
                file_path = os.path.join(folder, name) if name else os.path.join(folder, default_file(folder))
        t["filePath"] = file_path
        if t["installed"] and not (file_path and os.path.isfile(file_path)):
            t["installed"] = False
            t["note"] = "not installed"
        if file_path:
            t["fileUrl"] = pathlib.Path(os.path.abspath(file_path)).as_uri()
            t["url"] = t["fileUrl"]
    return t


def tree_order(tabs):
    """Depth-first through `parent` (siblings keep their tabs.json order), with `depth`; an unknown parent, a self
    parent or a cycle makes a tab a root. Same as main.js treeOrder (Ctrl+1..9 follow this order)."""
    by_id = {t["id"]: t for t in tabs}
    kids = {}
    for t in tabs:
        if not t.get("parent") or t["parent"] == t["id"] or t["parent"] not in by_id:
            t["parent"] = None
            continue
        kids.setdefault(t["parent"], []).append(t)
    out, seen = [], set()

    def visit(t, depth):
        if t["id"] in seen:
            return
        seen.add(t["id"])
        t["depth"] = depth
        out.append(t)
        for c in kids.get(t["id"], []):
            visit(c, depth + 1)
    for t in tabs:
        if not t.get("parent"):
            visit(t, 0)
    for t in tabs:
        if t["id"] not in seen:
            t["parent"] = None
            visit(t, 0)
    return out


def load_raw(paths):
    """(tabs list, where from). tabs.json, else shell\\tabs.default.json next to server.py, else the built-in list."""
    for path, where in ((paths.pm("tabs.json"), "tabs.json"),
                        (os.path.join(paths.shell_dir, "tabs.default.json"), "shell/tabs.default.json")):
        try:
            data = util.read_json_file(path)
        except FileNotFoundError:
            continue
        except (OSError, ValueError) as e:
            return DEFAULT_TABS["tabs"], "built-in (%s unreadable: %s)" % (where, type(e).__name__)
        tabs = data.get("tabs") if isinstance(data, dict) else data
        if isinstance(tabs, list):
            return [x for x in tabs if isinstance(x, dict)], where
    return DEFAULT_TABS["tabs"], "built-in"


def probe(port, timeout=0.5):
    """Any HTTP answer on 127.0.0.1:<port>/ counts as up."""
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        c.request("GET", "/", headers={"User-Agent": util.USER_AGENT})
        c.getresponse().read(1)
        return True
    except (OSError, http.client.HTTPException):
        return False
    finally:
        c.close()


class Tabs:
    def __init__(self, paths, own_port=None, prober=probe):
        self.paths = paths
        self.own_port = own_port
        self.prober = prober
        self.lock = threading.Lock()
        self.up = {}
        self.probedAt = None
        self.wake = threading.Event()

    def resolved(self):
        raw, where = load_raw(self.paths)
        tabs = tree_order([resolve_tab(t, self.paths, i) for i, t in enumerate(raw)])
        with self.lock:
            for t in tabs:
                if t["kind"] == "server" and t.get("local"):
                    t["up"] = True if t["port"] == self.own_port else self.up.get(t["port"])
        return {"tabs": tabs, "from": where, "probedAt": self.probedAt}

    def probe_all(self):
        raw, _ = load_raw(self.paths)
        ports = sorted({t["port"] for t in (resolve_tab(x, self.paths, i) for i, x in enumerate(raw))
                        if t["kind"] == "server" and t.get("local") and t.get("port")})
        res = {}

        def one(p):
            res[p] = True if p == self.own_port else self.prober(p)
        threads = [threading.Thread(target=one, args=(p,), daemon=True) for p in ports]
        for th in threads:
            th.start()
        for th in threads:
            th.join(3.0)
        with self.lock:
            self.up = res
            self.probedAt = util.now_iso()


def write_default(paths):
    """tabs.json only when missing (from shell\\tabs.default.json when present). True when written."""
    path = paths.pm("tabs.json")
    if os.path.exists(path):
        return False
    paths.ensure_pm_dir()
    src = os.path.join(paths.shell_dir, "tabs.default.json")
    try:
        data = util.read_json_file(src)
    except (OSError, ValueError):
        data = DEFAULT_TABS
    if isinstance(data, dict):
        data = dict(data, defaultsSeen=sorted(t["id"] for t in DEFAULT_TABS["tabs"]))
    util.write_json_atomic(path, data)
    return True


# Start commands an older tabs.default.json gave a tab (by id). `upgrade` replaces exactly these (q's own commands stay).
OLD_DEFAULT_STARTS = {"dashboard": {None, "dashboard.cmd"}, "github": {None}, "docs": {None}, "bot": {None, "run.cmd"}}
# The default tab ids before tabs.json recorded `defaultsSeen` (q's tabs.json may have dropped some on purpose).
# Default tabs that were retired: upgrade() drops them from q's tabs.json while they still point where the old
# default did. (ui-redesign: the redesign shipped in 1.18.13, so the UI Editor tab is the only UI tool.)
RETIRED_DEFAULTS = {"ui-redesign": {"folder": "ui-customizer", "file": "ui-customizer-redesign.html"}}
LEGACY_DEFAULT_IDS = ("project", "github", "docs", "dashboard", "sound", "ui", "ui-redesign", "map", "bot", "model")


def upgrade(paths):
    """Bring q's tabs.json up to the current defaults without touching what q changed:
    - a default tab that is new since this file was written (its id is neither in the file nor in its `defaultsSeen`
      list) is appended, so a new tool appears after setup; a default tab q removed stays removed;
    - a retired default tab (RETIRED_DEFAULTS, still pointing where it did) is removed;
    - for a default tab (same id, url, port and folder as the default), a start command that an older default used
      (or none) becomes the new one, and keepAlive / lowPriority are added when the tab does not set them.
    `defaultsSeen` (top level, next to `tabs`) lists the default ids already offered. The old file is kept as
    tabs.json.old. Returns the ids added or changed (empty when nothing changed or the file is missing / unreadable)."""
    path = paths.pm("tabs.json")
    try:
        data = util.read_json_file(path)
    except (OSError, ValueError):
        return []
    if isinstance(data, list):
        data = {"tabs": data}
    lst = data.get("tabs") if isinstance(data, dict) else None
    if not isinstance(lst, list):
        return []
    defaults = {d["id"]: d for d in DEFAULT_TABS["tabs"]}
    seen = data.get("defaultsSeen")
    seen = set(seen) if isinstance(seen, list) else set(LEGACY_DEFAULT_IDS)
    changed = []
    for t in list(lst):
        r = RETIRED_DEFAULTS.get(t.get("id")) if isinstance(t, dict) else None
        if r and all(t.get(k) == v for k, v in r.items()):
            lst.remove(t)
            changed.append(t["id"])
    have = {t.get("id") for t in lst if isinstance(t, dict)}
    for t in lst:
        d = defaults.get(t.get("id")) if isinstance(t, dict) else None
        if not d or any(t.get(k) != d.get(k) for k in ("url", "port", "folder")):
            continue
        before = dict(t)
        if "start" in d and t.get("start") != d["start"] and t.get("start") in OLD_DEFAULT_STARTS.get(d["id"], ()):
            t["start"] = d["start"]
        for k in ("keepAlive", "lowPriority"):
            if k in d and k not in t:
                t[k] = d[k]
        if t != before:
            changed.append(t["id"])
    for d in DEFAULT_TABS["tabs"]:
        if d["id"] not in have and d["id"] not in seen:
            lst.append(dict(d))
            changed.append(d["id"])
    all_seen = sorted((seen | set(defaults)) - set(RETIRED_DEFAULTS))
    if changed or data.get("defaultsSeen") != all_seen:
        data["defaultsSeen"] = all_seen
        try:
            with open(path, "rb") as f:
                old = f.read()
            if changed:
                with open(path + ".old", "wb") as f:
                    f.write(old)
        except OSError:
            pass
        util.write_json_atomic(path, data)
    return changed

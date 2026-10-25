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
# shell/tabs.default.json (a test checks it; DEFAULT_MARKS below adds the bookkeeping keys).
DEFAULT_TABS = {"tabs": [
    {"id": "project", "name": "Supervisor Board", "url": "http://127.0.0.1:8768/?embed=1", "port": 8768,
     "start": "py -3 \"%PM%\\server.py\"", "keepAlive": True},
    # (the Play tab is a page of the Supervisor Board server: same port, kept up by the project tab, so no keepAlive)
    {"id": "play", "name": "Play", "parent": "project", "url": "http://127.0.0.1:8768/play?embed=1", "port": 8768,
     "start": "py -3 \"%PM%\\server.py\""},
    {"id": "docs", "name": "Claude Manager", "parent": "project", "url": "http://127.0.0.1:8766/?embed=1", "port": 8766,
     "folder": "docs-manager", "start": "py -3 serve.py --docs \"%QUARK%\\Claude Documents\" --port 8766 --quiet",
     "keepAlive": True},
    {"id": "github", "name": "GitHub Manager", "parent": "project", "url": "http://127.0.0.1:8767/?embed=1", "port": 8767,
     "folder": "github-manager", "start": "py -3 -B server.py --data \"%QUARK%\\github-manager\"", "keepAlive": True,
     "lowPriority": True},
    {"id": "tests", "name": "Tests", "parent": "github", "url": "http://127.0.0.1:8775/?embed=1", "port": 8775,
     "folder": "tests-editor", "start": "py -3 serve.py --port 8775 --no-browser", "keepAlive": True},
    {"id": "dashboard", "name": "Editor Manager", "parent": "project", "url": "http://127.0.0.1:8765/?embed=1", "port": 8765,
     "folder": "editor-dashboard", "start": "py -3 dashboard.py --quark \"%QUARK%\" --no-browser", "keepAlive": True,
     "childLabel": "Editors"},
    {"id": "sound", "name": "Sound Editor", "parent": "dashboard", "folder": "sound-editor", "file": "sound-editor.html"},
    {"id": "ui", "name": "UI Editor", "parent": "dashboard", "folder": "ui-customizer", "file": "ui-customizer.html"},
    {"id": "map", "name": "Map Editor", "parent": "dashboard", "url": "http://127.0.0.1:8770/?embed=1", "port": 8770,
     "folder": "map-editor", "start": "py -3 serve.py --port 8770 --no-browser", "keepAlive": True},
    {"id": "bot", "name": "Bot Editor", "parent": "dashboard", "url": "http://127.0.0.1:8769/?embed=1", "port": 8769,
     "folder": "bot-editor", "start": "py -3 serve.py --port 8769 --no-open", "keepAlive": True},
    {"id": "model", "name": "Model Editor", "parent": "dashboard", "folder": "model-editor"},
    {"id": "gamemode", "name": "Gamemode Editor", "parent": "dashboard", "url": "http://127.0.0.1:8771/?embed=1",
     "port": 8771, "folder": "gamemode-editor", "start": "py -3 serve.py --port 8771 --no-open", "keepAlive": True},
    {"id": "physics", "name": "Physics Editor", "parent": "dashboard", "url": "http://127.0.0.1:8772/?embed=1",
     "port": 8772, "folder": "physics-editor", "start": "py -3 serve.py --port 8772 --no-open", "keepAlive": True},
    {"id": "weapons", "name": "Weapons Editor", "parent": "dashboard", "url": "http://127.0.0.1:8773/?embed=1",
     "port": 8773, "folder": "weapons-editor", "start": "py -3 serve.py --port 8773 --no-open", "keepAlive": True},
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
        data = dict(data, **DEFAULT_MARKS)
    util.write_json_atomic(path, data)
    return True


# Start commands an older tabs.default.json gave a tab (by id). `upgrade` replaces exactly these (q's own commands stay).
OLD_DEFAULT_STARTS = {"dashboard": {None, "dashboard.cmd"}, "github": {None}, "docs": {None}, "bot": {None, "run.cmd"}}
# The default tab ids before tabs.json recorded `defaultsSeen` (q's tabs.json may have dropped some on purpose).
# Default tabs that were retired: upgrade() drops them from q's tabs.json while they still point where the old
# default did. (ui-redesign: the redesign shipped in 1.18.13, so the UI Editor tab is the only UI tool.)
RETIRED_DEFAULTS = {"ui-redesign": {"folder": "ui-customizer", "file": "ui-customizer-redesign.html"}}
LEGACY_DEFAULT_IDS = ("project", "github", "docs", "dashboard", "sound", "ui", "ui-redesign", "map", "bot", "model")


# Default tabs whose place in q's file is fixed once by `upgrade` (see there).
PLACE_ONCE = ("play",)
# Reorders of the default tabs that `upgrade` applies once to a tabs.json that still has the older default order (q's
# own order stays): name -> (tab moved, the tab it now goes right before, the default order before the change).
# docs-before-github-2 (q, 2026-10-24): the bar reads Supervisor Board | Play, Claude Manager, GitHub Manager, Editor
# Manager. ("docs-before-github", the first try, compared every tab and skipped files where an older upgrade had appended
# Tests after the editors, q's among them; this one compares siblings only, so it runs again where that one skipped.)
MOVE_ONCE = {"docs-before-github-2": ("docs", "github", ("play", "github", "docs", "dashboard"))}
# Default urls that changed: `upgrade` gives a tab the new default url once (`defaultsUpgraded` records it) while it still
# has the old default url, port and folder; a url q set himself, or set back later, stays. name -> (tab id, old urls).
# map-embed (2026-10-24): the Map Editor tab loads ?embed=1 like every other tool, so the page drops its own brand /
# title row, which repeated the shell's tab name.
URL_ONCE = {"map-embed": ("map", ("http://127.0.0.1:8770/",))}
# A fresh tabs.json starts with every current default id and one-time fix marked as done, so `upgrade` never takes a
# later change by q (a dragged tab order) for an older default. write_default adds these; shell\tabs.default.json
# carries the same keys (a test checks it), because setup.ps1 and the shell's tab drag copy that file as it is.
DEFAULT_MARKS = {"defaultsSeen": sorted(t["id"] for t in DEFAULT_TABS["tabs"]), "defaultsPlaced": list(PLACE_ONCE),
                 "defaultsMoved": sorted(MOVE_ONCE), "defaultsUpgraded": sorted(URL_ONCE)}
DEFAULT_TABS.update(DEFAULT_MARKS)


def _insert_in_default_place(lst, tab, order):
    """Put `tab` right after the nearest default tab before it (in DEFAULT_TABS order) that q's list has (else at the
    end): a new child of the Supervisor Board lands among its siblings where the defaults have it (the tab bar keeps
    siblings in file order), not after the editors at the end."""
    ids = [x.get("id") if isinstance(x, dict) else None for x in lst]
    k = order.index(tab["id"]) if tab["id"] in order else len(order)
    for prev in reversed(order[:k]):
        if prev in ids:
            lst.insert(ids.index(prev) + 1, tab)
            return
    lst.append(tab)


def _move_once(lst, moved):
    """MOVE_ONCE: each move runs once per tabs.json (`defaultsMoved` records it). It moves the tab only while q's file
    still has the older default order for those tabs (the two tabs share a parent, and the siblings named in the old
    order keep that relative order; tabs elsewhere in the file don't count), so a list q reordered himself is left as
    it is. Returns (ids moved, the new defaultsMoved list)."""
    out = []
    for name, (tid, before, old_order) in MOVE_ONCE.items():
        if name in moved:
            continue
        moved = moved + [name]
        ids = [x.get("id") if isinstance(x, dict) else None for x in lst]
        if tid not in ids or before not in ids:
            continue
        a, b = lst[ids.index(tid)], lst[ids.index(before)]
        if a.get("parent") != b.get("parent"):
            continue
        sib = [x.get("id") for x in lst if isinstance(x, dict) and x.get("parent") == a.get("parent")]
        if [i for i in sib if i in old_order] != [i for i in old_order if i in sib]:
            continue  # (q's own order)
        lst.remove(a)
        lst.insert(lst.index(b), a)
        out.append(tid)
    return out, moved


def _url_once(lst, done, defaults):
    """URL_ONCE: each url change runs once per tabs.json (`defaultsUpgraded` records it), only on a tab that still has
    an old default url and the default port and folder. Returns (ids changed, the new defaultsUpgraded list)."""
    out = []
    for name, (tid, old_urls) in URL_ONCE.items():
        if name in done:
            continue
        done = done + [name]
        d = defaults.get(tid)
        t = next((x for x in lst if isinstance(x, dict) and x.get("id") == tid), None)
        if d is None or t is None or t.get("url") not in old_urls or any(t.get(k) != d.get(k) for k in ("port", "folder")):
            continue
        t["url"] = d["url"]
        out.append(tid)
    return out, done


def upgrade(paths):
    """Bring q's tabs.json up to the current defaults without touching what q changed:
    - a default tab that is new since this file was written (its id is neither in the file nor in its `defaultsSeen`
      list) is put in its default place (right after the nearest default tab before it that the file has), so a new
      tool appears after setup; a default tab q removed stays removed;
    - a retired default tab (RETIRED_DEFAULTS, still pointing where it did) is removed;
    - a default url that changed (URL_ONCE: the Map Editor's ?embed=1) is given once to a tab that still has the old
      one, before the checks below, which tell default tabs by their url;
    - a default reorder (MOVE_ONCE: Claude Manager before GitHub Manager) is applied once while the file still has
      the older default order;
    - for a default tab (same id, url, port and folder as the default), a start command that an older default used
      (or none) becomes the new one, and keepAlive / lowPriority are added when the tab does not set them.
    `defaultsSeen` (top level, next to `tabs`) lists the default ids already offered; `defaultsPlaced`, `defaultsMoved`
    and `defaultsUpgraded` the one-time fixes already done (PLACE_ONCE, MOVE_ONCE, URL_ONCE). The old file is kept as
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
    upgraded = data.get("defaultsUpgraded") if isinstance(data.get("defaultsUpgraded"), list) else []
    ids_url, upgraded = _url_once(lst, upgraded, defaults)
    changed += ids_url
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
        if d["id"] in PLACE_ONCE and "parent" in d and not t.get("parent"):
            t["parent"] = d["parent"]  # (the Play tab belongs under the Supervisor Board; other tabs keep q's layout)
        if t != before and t["id"] not in changed:
            changed.append(t["id"])
    moved = data.get("defaultsMoved") if isinstance(data.get("defaultsMoved"), list) else []
    ids_moved, moved = _move_once(lst, moved)
    changed += [i for i in ids_moved if i not in changed]
    order = [d["id"] for d in DEFAULT_TABS["tabs"]]
    for d in DEFAULT_TABS["tabs"]:
        if d["id"] not in have and d["id"] not in seen:
            _insert_in_default_place(lst, dict(d), order)
            changed.append(d["id"])
    # a tab an older upgrade appended at the end (the Play tab went after the editors, so it looked top level at the
    # right end of the tab bar): moved once to its default place, then left where q puts it (`defaultsPlaced`)
    placed = data.get("defaultsPlaced") if isinstance(data.get("defaultsPlaced"), list) else []
    for tid in PLACE_ONCE:
        if tid in placed:
            continue
        t = next((x for x in lst if isinstance(x, dict) and x.get("id") == tid), None)
        d = defaults.get(tid)
        placed = placed + [tid]
        if t is None or d is None or any(t.get(k) != d.get(k) for k in ("url", "port", "parent")):
            continue
        i = lst.index(t)
        lst.remove(t)
        _insert_in_default_place(lst, t, order)
        if lst.index(t) != i and tid not in changed:
            changed.append(tid)
    all_seen = sorted((seen | set(defaults)) - set(RETIRED_DEFAULTS))
    if changed or data.get("defaultsSeen") != all_seen or data.get("defaultsPlaced") != placed \
            or data.get("defaultsMoved") != moved or data.get("defaultsUpgraded") != upgraded:
        data["defaultsSeen"] = all_seen
        data["defaultsPlaced"] = placed
        data["defaultsMoved"] = moved
        data["defaultsUpgraded"] = upgraded
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

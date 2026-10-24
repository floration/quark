#!/usr/bin/env python3
"""Quark Editor Manager: the owner's local page for the editors (sound, UI, map, bot and model editor) only.
GitHub, Claude Documents, game code and tooling changes live in the Supervisor Board (127.0.0.1:8768), not here.

    python3 dashboard.py                          start on 127.0.0.1:8765 (or the next free port) and open the browser
    python3 dashboard.py --quark <root>           the Quark folder (default: found from where this is installed)
    python3 dashboard.py --repo <clone>           the clone whose assets/provenance/ is the registry
                                                  (default: working-repo\\ if it has the registry, else repository\\)
    python3 dashboard.py --install [<dir>]        copy the program to <dir> (default: <quark>\\editor-dashboard) and stop
    python3 dashboard.py --dump                   print every endpoint's JSON once and stop (tests, no server)

Other options: --port N, --no-browser, --data-dir <the game's user data folder> (default: %APPDATA%\\quark,
~/.local/share/quark, ~/Library/Application Support/quark), --sidecar <file> (where the dashboard keeps its marks).

What it reads (README.md has the views):
  - every working-repo* clone in the Quark folder and every worktree `git worktree list` gives for them: the editors'
    files in their commits ahead of origin/testing and their uncommitted files (other files are not shown);
  - the editors' files that differ between the production tag (the newest v<semver>) and origin/testing;
  - each editor folder (sound-editor\\, model-editor\\, map-editor\\, bot-editor\\, gamemode-editor\\, physics-editor\\, ui-customizer\\): its exported files
    and its provenance.json sidecar (docs/PROVENANCE.md), and the game's own overrides folder on this PC;
  - the provenance registry, <repo>/assets/provenance/*.json.

What it writes: only its own sidecar, <install dir>\\provenance.json (tool "dashboard"), when the owner marks items.
It never writes into a git clone: git runs with --no-optional-locks (status doesn't even refresh the index), there is
no checkout / commit / push anywhere, and `git fetch` only runs when Refresh is clicked with "fetch" ticked.
Git calls are batched per clone and run in parallel; results are cached until Refresh.
Python 3 standard library only; runs on Windows and Linux.
"""
import argparse, concurrent.futures, datetime, fnmatch, functools, gzip, hashlib, json, math, mimetypes, os, re, shutil
import subprocess, sys, threading, time, urllib.error, urllib.parse, urllib.request, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
APP_ID = "quark-editor-dashboard"
DEFAULT_PORT = 8765
SHELL_ORIGINS = ("http://127.0.0.1:8768", "http://localhost:8768")          # the Supervisor Board may read GET /api/*
STATIC = {"/": "index.html", "/index.html": "index.html", "/dashboard.css": "dashboard.css", "/dashboard.js": "dashboard.js",
          "/quark-tools.css": "quark-tools.css"}
MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8"}
PROGRAM_FILES = ["dashboard.py", "index.html", "quark-tools.css", "dashboard.css", "dashboard.js", "README.md", "dashboard.cmd", "install.cmd"]
ORIGINS = ("human", "third-party", "ai")
# the other local tool in the nav bar: (id, label, url, how to start it), checked when the page asks. GitHub, Claude
# Documents, game code and tooling are the Supervisor Board's, not this page's.
SIBLINGS = [("manager", "Supervisor Board", "http://127.0.0.1:8768/", "")]

# --- what belongs to which program -----------------------------------------------------------------------------------

# The editors, in the order the page shows them: (folder in the Quark folder, program, the `tool` its sidecar entries
# carry). The folder is also the program's id (links, /api/summary). Missing folders are skipped.
EDITORS = [
    ("sound-editor", "Sound editor", "sound-editor"),
    ("ui-customizer", "UI editor", "ui-customizer"),
    ("map-editor", "Map editor", "map-editor"),
    ("bot-editor", "Bot editor", "bot-editor"),
    ("model-editor", "Model editor", "model-editor"),
    ("gamemode-editor", "Gamemode editor", "mode-editor"),   # (its page: http://127.0.0.1:8771/, tools/mode_editor/serve.py)
    ("physics-editor", "Physics editor", "physics-editor"),
    ("weapons-editor", "Weapons editor", "weapons-editor"),
]
PROGRAMS = [p for _, p, _ in EDITORS]
PROGRAM_ID = {p: f for f, p, _ in EDITORS}

# Path rules, first match wins: repository paths (lower case, '/'), a pattern ending in '/' is that folder and everything
# under it, anything else is an fnmatch glob ('*' crosses '/'). A file no rule matches belongs to no editor and is not
# shown (game code, tooling and CI are the Supervisor Board's). Edit this table to move files between editors.
PATH_RULES = [
    ("Sound editor", ["tools/sound_editor/", "assets/sfx/", "assets/voice/", "assets/overrides/sounds/",
                      "assets/overrides/voices/", "*sound-levels*", "mods/sounds/", "src/client/audio.*",
                      "src/client/sfx_recorded.h", "src/client/voice_data.h", "tools/import_sfx.py",
                      "tools/import_voice.py", "tools/make_voice.py"]),
    ("UI editor", ["tools/ui_customizer/", "ui-customizer/", "*ui-customizer*", "src/client/ui*",
                   "src/client/hudwidgets.*", "src/client/fontatlas.*", "src/client/font_ui.h", "assets/fonts/",
                   "assets/overrides/fonts/", "assets/overrides/ui/", "tools/make_font_atlas.py"]),
    ("Map editor", ["tools/map_editor/", "tools/maps/", "tools/make_maps.py", "tools/embed_maps.py",
                    "assets/overrides/maps/", "src/client/editor.cpp", "docs/map_*"]),
    ("Bot editor", ["tools/bot_editor/", "src/game/bots.*"]),
    ("Model editor", ["tools/model_editor/", "assets/overrides/models/", "src/client/models.*", "src/client/gltf.*"]),
    ("Gamemode editor", ["tools/mode_editor/", "tools/modes/"]),
    ("Physics editor", ["tools/physics_editor/", "src/game/pmove.*"]),
    ("Weapons editor", ["tools/weapon_editor/", "src/game/weaponset.*"]),
]


# the same rules as git pathspecs (git's default matching: '*' crosses '/', a trailing '/' is the folder), so git reads
# and lists only the editors' files; program_of() still decides which editor a file is
EDITOR_PATHSPEC = ["--"] + [":(icase)" + pat for _, pats in PATH_RULES for pat in pats]


@functools.lru_cache(maxsize=None)
def program_of(path):
    """the editor a repository path belongs to, or None"""
    p = path.replace("\\", "/").lower()
    for prog, pats in PATH_RULES:
        for pat in pats:
            if (p.startswith(pat) if pat.endswith("/") else fnmatch.fnmatchcase(p, pat)): return prog
    return None


# --- git ---------------------------------------------------------------------------------------------------------------

class Git:
    """Runs git read-only and counts the calls (shown on the page: the owner watches how much a refresh costs)."""

    def __init__(self):
        self.calls, self.ms, self.lock = 0, 0.0, threading.Lock()
        self.env = dict(os.environ, GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0", LC_ALL="C", LANG="C")
        self.flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

    def __call__(self, cwd, *args, stdin=None, timeout=60, ok=(0,)):
        """stdout as text, or None when git fails (missing ref, not a repository...)."""
        t = time.perf_counter()
        try:
            r = subprocess.run(["git", "--no-optional-locks", "-c", "core.quotepath=off", "-C", cwd] + list(args),
                               input=stdin.encode("utf-8") if stdin is not None else None, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=self.env, timeout=timeout, creationflags=self.flags)
        except (OSError, subprocess.TimeoutExpired):
            r = None
        with self.lock:
            self.calls += 1
            self.ms += (time.perf_counter() - t) * 1000
        if r is None or r.returncode not in ok: return None
        return r.stdout.decode("utf-8", "replace")


GIT = Git()
LOG_FORMAT = "--format=%x1e%H%x1f%ct%x1f%an%x1f%s"
MAX_TESTING_COMMITS = 2000                                               # (testing since production: the newest ones)


def parse_log(out):
    """git log LOG_FORMAT --name-status -> [{sha, h, t, a, s, files: [[letter, path]]}]"""
    commits = []
    for rec in (out or "").split("\x1e")[1:]:
        lines = rec.split("\n")
        head = lines[0].split("\x1f")
        if len(head) < 4: continue
        files = []
        for ln in lines[1:]:
            if not ln.strip(): continue
            parts = ln.split("\t")
            if len(parts) >= 2: files.append([parts[0][:1], parts[-1]])
        commits.append({"sha": head[0], "h": head[0][:9], "t": int(head[1] or 0), "a": head[2], "s": head[3], "files": files})
    return commits


def parse_name_status_z(out):
    """git diff --name-status -z -> {path: letter} (a rename gives its new path)"""
    res, toks, i = {}, (out or "").split("\0"), 0
    while i < len(toks):
        st = toks[i]
        if not st: i += 1; continue
        if st[0] in "RC" and i + 2 < len(toks):
            res[toks[i + 2]] = st[0]; i += 3
        elif i + 1 < len(toks):
            res[toks[i + 1]] = st[0]; i += 2
        else: break
    return res


def parse_status_z(out):
    """git status --porcelain=v1 -z -> [[xy, path]]"""
    files, toks, i = [], (out or "").split("\0"), 0
    while i < len(toks):
        t = toks[i]; i += 1
        if not t: continue
        xy, path = t[:2], t[3:]
        if xy[0] in "RC": i += 1                                        # (-z puts the old name next)
        files.append([xy, path])
    return files


def semver_key(tag):
    m = re.match(r"^v(\d+)\.(\d+)\.(\d+)$", tag)
    return tuple(int(x) for x in m.groups()) if m else None


def git_common_dir(path):
    """the .git folder shared by a clone and its worktrees, found without running git"""
    dotgit = os.path.join(path, ".git")
    if os.path.isdir(dotgit): return os.path.normcase(os.path.realpath(dotgit))
    try:
        line = open(dotgit, encoding="utf-8").read().strip()
    except OSError:
        return None
    if not line.startswith("gitdir:"): return None
    gd = os.path.normpath(os.path.join(path, line[7:].strip()))
    try:
        gd = os.path.join(gd, open(os.path.join(gd, "commondir"), encoding="utf-8").read().strip())
    except OSError:
        pass
    return os.path.normcase(os.path.realpath(gd))


# --- the Quark folder --------------------------------------------------------------------------------------------------

def looks_like_quark(d):
    return os.path.isdir(d) and any(n.lower() == "repository" or n.lower().startswith("working-repo") for n in os.listdir(d))


def inside_git(d):
    while True:
        if os.path.exists(os.path.join(d, ".git")): return d
        up = os.path.dirname(d)
        if up == d: return None
        d = up


def default_quark():
    """the Quark folder: where this is installed (Quark\\editor-dashboard\\), the folder holding the clone this runs from
    (Quark\\working-repo\\tools\\editor_dashboard\\), or %USERPROFILE%\\Desktop\\Quark"""
    cands = [os.path.dirname(HERE)]
    clone = inside_git(HERE)
    if clone: cands.append(os.path.dirname(clone))
    cands.append(os.path.join(os.path.expanduser("~"), "Desktop", "Quark"))
    for c in cands:
        if looks_like_quark(c): return c
    return cands[-1]


def default_data_dir():
    if os.name == "nt": return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "quark")
    if sys.platform == "darwin": return os.path.expanduser("~/Library/Application Support/quark")
    return os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "quark")


class Config:
    def __init__(self, a):
        self.quark = os.path.abspath(a.quark or default_quark())
        repo = a.repo
        if not repo:
            # the first clone that has the registry (working-repo\ can lag behind testing), else the first that exists
            have = [n for n in ("working-repo", "repository") if os.path.isdir(os.path.join(self.quark, n))]
            repo = next((n for n in have if os.path.isdir(os.path.join(self.quark, n, "assets", "provenance"))),
                        have[0] if have else "working-repo")
        self.repo = os.path.abspath(repo if os.path.isabs(repo) else os.path.join(self.quark, repo))
        self.data_dir = os.path.abspath(a.data_dir or default_data_dir())
        # the dashboard's own sidecar: next to the program, unless the program runs from inside a git clone (then it
        # goes to Quark\editor-dashboard\, so it never lands in a repository)
        if a.sidecar: self.sidecar = os.path.abspath(a.sidecar)
        elif inside_git(HERE): self.sidecar = os.path.join(self.quark, "editor-dashboard", "provenance.json")
        else: self.sidecar = os.path.join(HERE, "provenance.json")


def discover_clones(cfg):
    """[{name, path, role, common}] for repository\\, every working-repo* folder and every worktree of those"""
    found, seen = [], set()

    def add(path, role, name=None):
        path = os.path.normpath(path)
        key = os.path.normcase(os.path.realpath(path))
        if key in seen or not os.path.exists(os.path.join(path, ".git")): return
        seen.add(key)
        inside = os.path.normcase(os.path.dirname(path)) == os.path.normcase(cfg.quark)
        name = name or os.path.basename(path)
        if any(f["name"] == name for f in found): name = path                 # (two worktrees with one folder name)
        found.append({"name": name, "path": path, "role": role, "common": git_common_dir(path), "outside": not inside})

    try:
        names = sorted(os.listdir(cfg.quark), key=str.lower)
    except OSError:
        names = []
    for n in names:
        if n.lower() == "repository": add(os.path.join(cfg.quark, n), "mirror")
    work = [n for n in names if n.lower().startswith("working-repo")]
    for n in work: add(os.path.join(cfg.quark, n), "work")
    # worktrees of the working clones (they can live anywhere): one `git worktree list` per working-repo* clone
    mains = [c for c in found if c["role"] == "work" and os.path.isdir(os.path.join(c["path"], ".git"))]
    outs = run_parallel([(GIT, (c["path"], "worktree", "list", "--porcelain")) for c in mains])
    for out in outs:
        for line in (out or "").splitlines():
            if line.startswith("worktree "):
                add(line[9:].strip(), "work")
    if os.path.exists(os.path.join(cfg.repo, ".git")): add(cfg.repo, "work")
    return found


# Two pools so a task never waits on a job queued behind itself: LEAF runs single git calls / file reads (they never
# submit anything), TASKS runs the per-clone collectors, which submit their git calls to LEAF.
LEAF = concurrent.futures.ThreadPoolExecutor(max_workers=max(4, min(12, os.cpu_count() or 4)))
TASKS = concurrent.futures.ThreadPoolExecutor(max_workers=8)


def run_parallel(jobs, pool=None):
    """[(fn, args)] -> results in order (git calls are I/O bound: threads are enough)"""
    futs = [(pool or LEAF).submit(fn, *args) for fn, args in jobs]
    return [f.result() for f in futs]


# --- collecting --------------------------------------------------------------------------------------------------------

def repo_refs(cwd):
    """one for-each-ref: origin/testing (else a local testing) and the production tag (the newest v<semver>)"""
    out = GIT(cwd, "for-each-ref", "--format=%(refname)%00%(objectname)%00%(*objectname)",
              "refs/remotes/origin/testing", "refs/heads/testing", "refs/tags/v*")
    refs, tags = {}, []
    for line in (out or "").splitlines():
        p = line.split("\0")
        if len(p) < 3: continue
        refs[p[0]] = p[2] or p[1]
        if p[0].startswith("refs/tags/") and semver_key(p[0][10:]): tags.append(p[0][10:])
    testing_ref = ("origin/testing" if "refs/remotes/origin/testing" in refs else
                   "testing" if "refs/heads/testing" in refs else None)
    prod = max(tags, key=semver_key) if tags else None
    return {"testing_ref": testing_ref, "testing_sha": refs.get("refs/remotes/origin/testing") or refs.get("refs/heads/testing"),
            "prod_tag": prod, "prod_sha": refs.get("refs/tags/" + prod) if prod else None}


def read_versions(cwd, revs):
    """VERSION at each rev, in one `git cat-file --batch`"""
    revs = [r for r in revs if r]
    if not revs: return {}
    out = subprocess_bytes(cwd, ["cat-file", "--batch"], "".join(r + ":VERSION\n" for r in revs))
    res, pos = {}, 0
    for r in revs:
        nl = out.find(b"\n", pos)
        if nl < 0: break
        head = out[pos:nl].split()
        pos = nl + 1
        if len(head) == 3 and head[1] == b"blob":
            n = int(head[2]); res[r] = out[pos:pos + n].decode("utf-8", "replace").strip(); pos += n + 1
    return res


def subprocess_bytes(cwd, args, stdin):
    """like GIT() but keeps stdout as bytes (cat-file sizes are in bytes)"""
    t = time.perf_counter()
    try:
        r = subprocess.run(["git", "--no-optional-locks", "-C", cwd] + args, input=stdin.encode("utf-8"),
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=GIT.env, timeout=60, creationflags=GIT.flags)
        out = r.stdout if r.returncode == 0 else b""
    except (OSError, subprocess.TimeoutExpired):
        out = b""
    with GIT.lock:
        GIT.calls += 1; GIT.ms += (time.perf_counter() - t) * 1000
    return out


def collect_repo(cwd, refs, is_ref):
    """per pair of (testing, production) commits: the editors' files testing changed since production; for the reference
    clone also those commits and VERSION at both"""
    res = {"tp_files": {}, "testing_commits": [], "versions": {}}
    t, p = refs["testing_ref"], refs["prod_tag"]
    jobs = [(read_versions, (cwd, [t, p]))] if is_ref else []
    if t and p:
        jobs.append((GIT, (cwd, "diff", "--name-status", "-z", "--no-renames", p, t, *EDITOR_PATHSPEC)))
        if is_ref: jobs.append((GIT, (cwd, "log", "--no-merges", "--full-history", "--max-count=%d" % MAX_TESTING_COMMITS,
                                      LOG_FORMAT, "--name-status", "--no-renames", p + ".." + t, *EDITOR_PATHSPEC)))
    outs = run_parallel(jobs)
    if is_ref: res["versions"] = outs.pop(0)
    if outs: res["tp_files"] = {f: l for f, l in parse_name_status_z(outs[0]).items() if program_of(f)}
    if len(outs) > 1: res["testing_commits"] = editor_commits(outs[1])
    return res


def editor_commits(out):
    """parse_log, keeping only the editors' files (and the commits that touch one)"""
    keep = []
    for cm in parse_log(out):
        cm["files"] = [lf for lf in cm["files"] if program_of(lf[1])]
        if cm["files"]: keep.append(cm)
    return keep


def collect_worktree(c, refs, tp_files):
    """one clone / worktree, the editors' files only: commits ahead of testing, uncommitted files, and each file's state:
    local (the change isn't in testing), testing (in testing, not production) or both (in testing and production).
    Four git calls, in parallel, each limited to the editors' paths."""
    cwd, t, ps = c["path"], refs["testing_ref"], EDITOR_PATHSPEC
    jobs = [(GIT, (cwd, "status", "--porcelain=v1", "-z", "--untracked-files=all", *ps))]
    if t:
        jobs += [(GIT, (cwd, "log", "--no-merges", "--full-history", LOG_FORMAT, "--name-status", "--no-renames", t + "..HEAD", *ps)),
                 (GIT, (cwd, "diff", "--name-only", "-z", "--no-renames", t, *ps)),
                 # the working tree vs the merge base with testing (git 2.30+; older git: merge-base, then diff)
                 (GIT, (cwd, "diff", "--name-status", "-z", "--no-renames", "--merge-base", t, *ps))]
    outs = run_parallel(jobs)
    unc = [xf for xf in parse_status_z(outs[0]) if program_of(xf[1])]
    w = dict(name=c["name"], path=c["path"], role=c["role"], outside=c.get("outside", False),
             commits=[], uncommitted=unc, status={}, errors=[])
    if outs[0] is None: w["errors"].append("not a readable git clone")
    if t:
        w["commits"] = editor_commits(outs[1])
        d_t = set(n for n in (outs[2] or "").split("\0") if n)          # the working tree vs testing's tip
        local_out = outs[3]
        if local_out is None:
            mb = (GIT(cwd, "merge-base", t, "HEAD") or "").strip()
            local_out = GIT(cwd, "diff", "--name-status", "-z", "--no-renames", mb, *ps) if mb else ""
        local = {f: l for f, l in parse_name_status_z(local_out).items() if program_of(f)}
        local_set = set(local) | {f for xy, f in unc if xy == "??"}
        for f in local_set | {f for cm in w["commits"] for _, f in cm["files"]} | {f for _, f in unc}:
            if f in local_set and (f in d_t or f not in local): st = "local"     # (untracked: never in testing)
            elif f in local_set: st = "testing" if f in tp_files else "both"     # (the same content is in testing)
            else: st = "none"                                                    # (changed, then changed back)
            w["status"][f] = st
    else:
        w["errors"].append("no origin/testing in this clone: run Refresh with fetch")
        for _, f in unc: w["status"][f] = "local"
    return w


# --- files outside git: editor exports, the game's overrides folder ------------------------------------------------------

EXPORT_DIRS = re.compile(r"^(exports?|overrides|output|out)$", re.I)
EXPORT_EXT = {".zip", ".wav", ".ogg", ".flac", ".mp3", ".gltf", ".glb", ".bin", ".qmap", ".ttf", ".otf", ".png", ".jpg",
              ".jpeg", ".json", ".txt", ".cfg", ".ini"}
SKIP_DIRS = {".git", "node_modules", "__pycache__", "data", "js", "css", "old"}
MAX_FILES = 3000


def repo_overrides_index(repo):
    """{lower-case path under assets/overrides without extension: (path, size)} of the registry clone's shipped overrides"""
    base, idx = os.path.join(repo, "assets", "overrides"), {}
    for d, _, names in os.walk(base):
        for n in names:
            f = os.path.join(d, n)
            rel = os.path.relpath(f, base).replace(os.sep, "/")
            try: idx[os.path.splitext(rel)[0].lower()] = (f, os.path.getsize(f))
            except OSError: pass
    return idx


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""): h.update(chunk)
    return h.hexdigest()


def compare_to_repo(f, rel, size, ov_idx):
    """an exported override vs the registry clone's assets/overrides: same / differs / missing"""
    hit = ov_idx.get(os.path.splitext(rel)[0].lower())
    if not hit: return "missing"
    if hit[1] != size: return "differs"
    try: return "same" if sha_file(f) == sha_file(hit[0]) else "differs"
    except OSError: return "differs"


def scan_exports(folder, program, ov_idx, overrides_root=False):
    """files an editor wrote: its provenance.json, anything under an export / overrides folder, output-looking files at
    its top level. overrides_root: the folder itself is an overrides folder (the game's per-user one); its files go to
    the editor their assets/overrides/ path belongs to, and those of no editor are left out."""
    files, more = [], 0
    for d, dirs, names in os.walk(folder):
        rel_d = os.path.relpath(d, folder)
        depth = 0 if rel_d == "." else rel_d.count(os.sep) + 1
        parts = [] if rel_d == "." else rel_d.split(os.sep)
        in_export = overrides_root or any(EXPORT_DIRS.match(x) for x in parts)
        dirs[:] = [x for x in dirs if (in_export or x.lower() not in SKIP_DIRS) and not x.startswith(".") and depth < 6]
        for n in names:
            ext = os.path.splitext(n)[1].lower()
            if n.lower() in ("readme.md", "readme.txt"): continue
            if not (in_export or (depth == 0 and (ext in EXPORT_EXT or n.lower() == "provenance.json"))): continue
            f = os.path.join(d, n)
            rel = os.path.relpath(f, folder).replace(os.sep, "/")
            # the part after the last overrides/ folder is the asset's override path
            ov = rel if overrides_root else (rel.lower().split("overrides/", 1)[1] if "overrides/" in rel.lower() else None)
            if ov is not None: ov = rel[len(rel) - len(ov):]
            prog = program_of("assets/overrides/" + ov) if overrides_root else program
            if not prog: continue
            if len(files) >= MAX_FILES: more += 1; continue
            try: st = os.stat(f)
            except OSError: continue
            e = {"path": rel, "mtime": int(st.st_mtime), "size": st.st_size, "program": prog}
            if ov is not None: e["asset"], e["repo"] = ov, compare_to_repo(f, ov, st.st_size, ov_idx)
            files.append(e)
    files.sort(key=lambda e: -e["mtime"])
    return files, more


# --- provenance ----------------------------------------------------------------------------------------------------------

def read_json(path):
    try:
        with open(path, encoding="utf-8") as f: return json.load(f), None
    except FileNotFoundError:
        return None, None
    except (OSError, ValueError) as e:
        return None, "%s: %s" % (path, e)


def load_registry(repo):
    """{id: entry with 'category'}, the files read, errors"""
    base, items, files, errors = os.path.join(repo, "assets", "provenance"), {}, [], []
    try: names = sorted(n for n in os.listdir(base) if n.endswith(".json"))
    except OSError: return items, files, ["no registry: %s is missing" % base]
    for n in names:
        doc, err = read_json(os.path.join(base, n))
        if err: errors.append(err); continue
        if not isinstance(doc, dict): continue
        cat = doc.get("category") or n[:-5]
        files.append(n)
        for k, v in (doc.get("items") or {}).items():
            if isinstance(v, dict): items[k] = dict(v, category=cat)
    return items, files, errors


def category_of(asset_id, registry):
    """the registry's category, or the docs/PROVENANCE.md table's for an id the registry doesn't have yet"""
    if asset_id in registry: return registry[asset_id]["category"]
    top = asset_id.split("/", 1)[0]
    return {"website": "images"}.get(top, top + "s" if top in ("sound", "voice", "model", "map", "font", "image", "bot") else top)


def differs(entry, reg):
    """a sidecar entry is pending when any of its fields (except `tool`, which a merge fills in) isn't the registry's"""
    if reg is None: return True
    return any(k != "tool" and reg.get(k) != v for k, v in entry.items())


def sidecar_paths(cfg):
    out = [(os.path.join(cfg.quark, folder, "provenance.json"), tool, prog) for folder, prog, tool in EDITORS]
    if all(os.path.normcase(cfg.sidecar) != os.path.normcase(p) for p, _, _ in out):
        out.append((cfg.sidecar, "dashboard", "Dashboard"))
    return out


def rel_or_abs(path, base):
    """path relative to base when that is shorter and on the same drive, else as it is"""
    try: rel = os.path.relpath(path, base)
    except ValueError: return path                                       # (another drive on Windows)
    return rel if len(rel) < len(path) else path


def assets_payload(cfg):
    registry, files, errors = load_registry(cfg.repo)
    pending, sidecars = {}, []
    for path, tool, _ in sidecar_paths(cfg):
        doc, err = read_json(path)
        if err: errors.append(rel_or_abs(path, cfg.quark) + ": skipped" + err[len(path):])
        if not isinstance(doc, dict): continue
        tool = doc.get("tool") or tool
        n_items, n_pending = 0, 0
        for k, v in (doc.get("items") or {}).items():
            if not isinstance(v, dict): continue
            n_items += 1
            if differs(v, registry.get(k)):
                n_pending += 1
                pending.setdefault(k, []).append(dict(v, tool=v.get("tool") or tool, sidecar=path))
        try: mtime = int(os.path.getmtime(path))
        except OSError: mtime = 0
        sidecars.append({"path": path, "tool": tool, "items": n_items, "pending": n_pending, "mtime": mtime,
                         "own": os.path.normcase(path) == os.path.normcase(cfg.sidecar)})
    for v in pending.values(): v.sort(key=lambda e: e.get("date") or "", reverse=True)    # (the latest first)
    rows = []
    for k in sorted(set(registry) | set(pending)):
        r = registry.get(k)
        row = {"id": k, "cat": category_of(k, registry), "o": r.get("origin", "ai") if r else None}
        for f in ("by", "date", "tool", "source", "note", "wip", "version"):
            if r and r.get(f): row[f] = r[f]
        if k in pending: row["p"] = [{f: e[f] for f in ("origin", "by", "date", "note", "tool", "sidecar") if e.get(f)} for e in pending[k]]
        rows.append(row)
    stats = {}
    for row in rows:
        s = stats.setdefault(row["cat"], {"human": 0, "third-party": 0, "ai": 0, "pending": 0, "eff": {"human": 0, "third-party": 0, "ai": 0}})
        if row["o"] in ORIGINS: s[row["o"]] += 1
        eff = row["p"][0].get("origin") if row.get("p") else row["o"]
        if eff in ORIGINS: s["eff"][eff] += 1
        if row.get("p"): s["pending"] += 1
    # the merge command, run in the registry clone: sidecar paths relative to it when they can be (..\\sound-editor\\...)
    merge_from = [rel_or_abs(s["path"], cfg.repo) for s in sidecars if s["pending"]]
    py = "py -3" if os.name == "nt" else "python3"
    cmd = (py + " tools/provenance.py merge " + " ".join('"%s"' % p if " " in p else p for p in merge_from)) if merge_from else ""
    for s in sidecars: s["short"] = rel_or_abs(s["path"], cfg.quark)
    return {"repo": cfg.repo, "registryFiles": files, "errors": errors, "rows": rows, "stats": stats, "sidecars": sidecars,
            "ownSidecar": cfg.sidecar, "mergeCmd": cmd, "mergeCwd": cfg.repo,
            "ownPending": sum(s["pending"] for s in sidecars if s["own"])}


def pct_human(h, n):
    """one decimal, rounded toward "not done yet" like tools/provenance.py summary: 100 only when every item is human-made"""
    if not n: return None
    if h == n: return 100.0
    v = math.floor(h * 1000.0 / n) / 10
    return 0.1 if h and v == 0 else v


def summary_pct(stats):
    tot = {k: sum(s[k] for s in stats.values()) for k in ORIGINS}
    n = sum(tot.values())
    return {"counts": tot, "total": n, "humanPct": pct_human(tot["human"] + tot["third-party"], n)}


SIDECAR_LOCK = threading.Lock()


def write_marks(cfg, ids, origin, by, note, clear):
    """the dashboard's own sidecar: add / replace (or, clear=True, remove) the entries for ids; unknown fields kept"""
    with SIDECAR_LOCK:
        doc, err = read_json(cfg.sidecar)
        if err: raise ValueError("can't read %s: %s" % (cfg.sidecar, err))
        if not isinstance(doc, dict): doc = {}
        doc.setdefault("format", 1)
        doc["tool"] = "dashboard"
        items = doc.get("items") if isinstance(doc.get("items"), dict) else {}
        today = datetime.date.today().isoformat()
        for k in ids:
            if clear: items.pop(k, None); continue
            e = {"origin": origin, "date": today}
            if by: e["by"] = by
            if note: e["note"] = note
            items[k] = e
        doc["items"] = {k: items[k] for k in sorted(items)}
        os.makedirs(os.path.dirname(cfg.sidecar), exist_ok=True)
        tmp = cfg.sidecar + ".tmp"
        with open(tmp, "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False); f.write("\n")
        os.replace(tmp, cfg.sidecar)


# --- the whole picture --------------------------------------------------------------------------------------------------

def by_program(clones, repos, ref_key, exports):
    """regroups everything per editor: a commit shows under each editor it touches, with only that editor's files"""
    progs = {p: {"id": PROGRAM_ID[p], "name": p, "clones": [], "testing": [], "testingFiles": [], "exports": [],
                 "files": {}} for p in PROGRAMS}
    for w in clones:
        per = {}
        for cm in w["commits"]:
            for letter, f in cm["files"]:
                g = per.setdefault(program_of(f), {"commits": {}, "uncommitted": []})
                g["commits"].setdefault(cm["sha"], dict(cm, files=[]))["files"].append([letter, f, w["status"].get(f, "local")])
        for xy, f in w["uncommitted"]:
            per.setdefault(program_of(f), {"commits": {}, "uncommitted": []})["uncommitted"].append([xy, f, w["status"].get(f, "local")])
        for prog, g in per.items():
            progs[prog]["clones"].append({"clone": w["name"], "commits": list(g["commits"].values()),
                                          "uncommitted": g["uncommitted"]})
            fl = progs[prog]["files"]
            for cm in g["commits"].values():
                for _, f, st in cm["files"]: fl.setdefault(f, {}).setdefault(w["name"], st)
            for xy, f, st in g["uncommitted"]: fl.setdefault(f, {})[w["name"]] = st
    ref = repos.get(ref_key) or {}
    for cm in ref.get("testing_commits", []):
        per = {}
        for letter, f in cm["files"]: per.setdefault(program_of(f), []).append([letter, f])
        for prog, fl in per.items(): progs[prog]["testing"].append(dict(cm, files=fl))
    for f, letter in sorted(ref.get("tp_files", {}).items()):
        progs[program_of(f)]["testingFiles"].append([letter, f])
    for src in exports:
        for e in src["files"]:
            progs[e["program"]]["exports"].append(dict(e, source=src["name"]))
    out = []
    for p in PROGRAMS:
        g = progs[p]
        g["files"] = [[f, sts] for f, sts in sorted(g["files"].items())]
        g["exports"].sort(key=lambda e: -e["mtime"])
        out.append(g)
    return out


class State:
    """the cached snapshot; rebuilt by refresh()"""

    def __init__(self, cfg):
        self.cfg, self.lock, self.ready = cfg, threading.Lock(), threading.Event()
        self.status = self.changes = self.assets = None

    def refresh(self, fetch=False):
        with self.lock:
            self.ready.clear()
            try: self._refresh(fetch)
            finally: self.ready.set()

    def _refresh(self, fetch):
        cfg, t0 = self.cfg, time.perf_counter()
        calls0, ms0 = GIT.calls, GIT.ms
        program_of.cache_clear()
        clones = discover_clones(cfg)
        by_common = {}
        for c in clones: by_common.setdefault(c["common"], c)
        fetch_errors = []
        if fetch:
            fetcher = functools.partial(GIT, timeout=300)
            outs = run_parallel([(fetcher, (c["path"], "fetch", "--prune", "origin")) for c in by_common.values()])
            fetch_errors = [c["name"] for c, o in zip(by_common.values(), outs) if o is None]
            clones = discover_clones(cfg)                                # (fetch can't add worktrees, but be exact)
        mirror = next((c for c in clones if c["role"] == "mirror"), None)
        ref_clone = mirror or next((c for c in clones if os.path.normcase(c["path"]) == os.path.normcase(cfg.repo)), None) \
            or (clones[0] if clones else None)
        commons = list(by_common.keys())
        refs = dict(zip(commons, run_parallel([(repo_refs, (by_common[k]["path"],)) for k in commons])))
        ref_key = ref_clone["common"] if ref_clone else None
        ov_idx = repo_overrides_index(cfg.repo)
        # testing vs production once per pair of commits (clones whose origin/testing and production tag are the
        # reference's, the usual case, share its result), then every worktree and editor folder at once
        pair = lambda k: (refs[k]["testing_sha"], refs[k]["prod_sha"])
        firsts = {}
        for k in ([ref_key] if ref_key else []) + commons: firsts.setdefault(pair(k), k)
        pair_res = dict(zip(firsts, run_parallel([(collect_repo, (by_common[k]["path"], refs[k], k == ref_key))
                                                  for k in firsts.values()], TASKS)))
        repos = {k: pair_res[pair(k)] for k in commons}
        jobs = [(collect_worktree, (c, refs[c["common"]], repos[c["common"]]["tp_files"])) for c in clones]
        export_srcs = []
        for folder, prog, tool in EDITORS:
            d = os.path.join(cfg.quark, folder)
            if os.path.isdir(d): export_srcs.append((folder, d, prog, False))
        if os.path.isdir(os.path.join(cfg.data_dir, "overrides")):
            export_srcs.append(("this PC: overrides", os.path.join(cfg.data_dir, "overrides"), None, True))
        jobs += [(scan_exports, (d, prog, ov_idx, root)) for _, d, prog, root in export_srcs]
        res = run_parallel(jobs, TASKS)
        worktrees = res[:len(clones)]
        exports = [{"name": n, "path": d, "files": r[0], "more": r[1]} for (n, d, _, _), r in zip(export_srcs, res[len(clones):])]
        rr = refs.get(ref_key) or {"testing_ref": None, "testing_sha": None, "prod_tag": None, "prod_sha": None}
        vers = (repos.get(ref_key) or {}).get("versions", {})
        for w, c in zip(worktrees, clones):                          # (a clone whose origin/testing isn't the reference's)
            w["testingSha"] = (refs[c["common"]]["testing_sha"] or "")[:9]
            w["stale"] = bool(rr["testing_sha"] and refs[c["common"]]["testing_sha"] != rr["testing_sha"])
        assets = assets_payload(cfg)
        editors = [{"folder": f, "program": p, "present": os.path.isdir(os.path.join(cfg.quark, f))} for f, p, _ in EDITORS]
        self.changes = {"programs": by_program(worktrees, repos, ref_key, exports),
                        "clones": [{k: w[k] for k in w if k not in ("commits", "uncommitted", "status")} |
                                   {"commits": len(w["commits"]), "uncommitted": len(w["uncommitted"]),
                                    "local": sum(1 for x in w["status"].values() if x == "local")} for w in worktrees],
                        "exports": [{"name": e["name"], "path": e["path"], "files": len(e["files"]), "more": e["more"]} for e in exports],
                        "editors": editors, "rules": PATH_RULES}
        self.assets = assets
        ms = (time.perf_counter() - t0) * 1000
        self.status = {
            "app": APP_ID, "quark": cfg.quark, "repo": cfg.repo, "refClone": ref_clone["name"] if ref_clone else None,
            "testing": {"ref": rr["testing_ref"], "sha": (rr["testing_sha"] or "")[:9], "version": vers.get(rr["testing_ref"])},
            "production": {"tag": rr["prod_tag"], "sha": (rr["prod_sha"] or "")[:9], "version": vers.get(rr["prod_tag"])},
            "human": summary_pct(assets["stats"]), "refreshed": int(time.time()), "ms": round(ms),
            "gitCalls": GIT.calls - calls0, "gitMs": round(GIT.ms - ms0), "fetched": fetch, "fetchErrors": fetch_errors,
            "noClones": not clones, "ownSidecar": cfg.sidecar}
        self.status["links"] = links(cfg)

    def reload_assets(self):
        """after a mark: only the sidecars and the registry are read again (no git)"""
        self.ready.wait()
        self.assets = assets_payload(self.cfg)
        self.status = dict(self.status, human=summary_pct(self.assets["stats"]))

    def summary(self):
        """GET /api/summary: the compact, stable shape other tools read (the Supervisor Board on 8768). Keep its keys."""
        st, a = self.status, self.assets
        cats, tot = {}, {"human": 0, "third_party": 0, "ai": 0}
        for c, v in sorted(a["stats"].items()):
            cats[c] = {"total": v["human"] + v["third-party"] + v["ai"], "human": v["human"], "third_party": v["third-party"], "ai": v["ai"]}
            for k in tot: tot[k] += cats[c][k]
        n = sum(tot.values())
        progs = []
        for p in self.changes["programs"]:
            local = {cm["sha"] for c in p["clones"] for cm in c["commits"]}
            progs.append({"id": p["id"], "name": p["name"], "ahead_testing": len(local),
                          "ahead_production": len(local) + len(p["testing"]),
                          "uncommitted": sum(len(c["uncommitted"]) for c in p["clones"]),
                          "files_changed": sum(1 for _, sts in p["files"] if "local" in sts.values())})
        return {"testing": {"version": st["testing"]["version"], "ref": st["testing"]["ref"]},
                "production": {"version": st["production"]["version"], "tag": st["production"]["tag"]},
                "provenance": dict(total=n, pct_human=pct_human(tot["human"] + tot["third_party"], n),
                                   by_category=cats, **tot),
                "pending_marks": sum(1 for r in a["rows"] if r.get("p")), "programs": progs, "remaining": tot["ai"],
                "generated_at": datetime.datetime.fromtimestamp(st["refreshed"], datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}


# --- the server ---------------------------------------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkDashboard/1"
    state = None

    def log_request(self, code="-", size="-"):                           # (quiet: errors only)
        if str(code)[:1] in ("4", "5"): super().log_request(code, size)

    def host_ok(self, read_api=False):
        """only this machine's own page (blocks DNS rebinding by Host, other sites by Origin); the Supervisor Board's page
        (SHELL_ORIGINS) may also read GET /api/* (read_api), which then answers with its CORS header"""
        port = self.server.server_address[1]
        self.cors = None
        if self.headers.get("Host", "") not in ("127.0.0.1:%d" % port, "localhost:%d" % port): return False
        o = self.headers.get("Origin")
        if read_api and o in SHELL_ORIGINS: self.cors = o; return True
        return o is None or o in ("http://127.0.0.1:%d" % port, "http://localhost:%d" % port)

    def send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, (dict, list)): body = json.dumps(body, separators=(",", ":")).encode("utf-8")
        gz = len(body) > 2048 and "gzip" in self.headers.get("Accept-Encoding", "")
        if gz: body = gzip.compress(body, 5)
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if gz: self.send_header("Content-Encoding", "gzip")
        if getattr(self, "cors", None): self.send_header("Access-Control-Allow-Origin", self.cors); self.send_header("Vary", "Origin")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):                                               # (a CORS preflight from the Supervisor Board)
        path = self.path.split("?", 1)[0]
        if not path.startswith("/api/") or not self.host_ok(read_api=True) or not self.cors:
            return self.send(403, {"error": "forbidden"})
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", self.cors)
        self.send_header("Access-Control-Allow-Methods", "GET")
        self.send_header("Access-Control-Max-Age", "600")
        self.send_header("Vary", "Origin")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if not self.host_ok(read_api=path.startswith("/api/")): return self.send(403, {"error": "forbidden"})
        if path in STATIC:
            f = os.path.join(HERE, STATIC[path])
            try: data = open(f, "rb").read()
            except OSError: return self.send(404, {"error": "missing " + STATIC[path]})
            return self.send(200, data, MIME[os.path.splitext(f)[1]])
        if path == "/api/ping": return self.send(200, {"app": APP_ID})
        if path == "/api/links": return self.send(200, links(self.state.cfg))
        if path.startswith("/editors/"): return self.send_editor_file(path)
        st = self.state
        if path in ("/api/status", "/api/changes", "/api/assets"):
            st.ready.wait()
            return self.send(200, getattr(st, path[5:]))
        if path == "/api/summary":
            st.ready.wait()
            return self.send(200, st.summary())
        self.send(404, {"error": "not found"})

    def send_editor_file(self, path):
        """GET /editors/<folder>/<file>: an editor's own page and its files, read-only, only inside the five editor
        folders (Chrome / Edge won't follow a file:// link from this page, so the editors are served from here)"""
        parts = urllib.parse.unquote(path).split("/", 3)                   # ['', 'editors', folder, rest]
        if len(parts) < 4 or parts[2] not in [f for f, _, _ in EDITORS] or not parts[3]:
            return self.send(404, {"error": "not found"})
        base = os.path.realpath(os.path.join(self.state.cfg.quark, parts[2]))
        f = os.path.realpath(os.path.join(base, *parts[3].split("/")))
        if not f.startswith(base + os.sep) or not os.path.isfile(f):
            return self.send(404, {"error": "not found"})
        ctype = mimetypes.guess_type(f)[0] or "application/octet-stream"
        if ctype.startswith("text/") or ctype in ("application/javascript", "application/json"): ctype += "; charset=utf-8"
        try: data = open(f, "rb").read()
        except OSError: return self.send(404, {"error": "not found"})
        self.send(200, data, ctype)

    def do_POST(self):
        if not self.host_ok() or not self.headers.get("Content-Type", "").startswith("application/json"):
            return self.send(403, {"error": "forbidden"})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            req = json.loads(self.rfile.read(min(n, 4 << 20)) or b"{}")
        except ValueError:
            return self.send(400, {"error": "bad json"})
        st, path = self.state, self.path.split("?", 1)[0]
        if path == "/api/refresh":
            st.refresh(bool(req.get("fetch")))
            return self.send(200, st.status)
        if path == "/api/mark":
            ids = [i for i in req.get("ids") or [] if isinstance(i, str) and 0 < len(i) < 300]
            origin, clear = req.get("origin"), bool(req.get("clear"))
            if not ids or (not clear and origin not in ORIGINS):
                return self.send(400, {"error": "need ids and an origin (human, third-party, ai)"})
            by, note = str(req.get("by") or "").strip()[:80], str(req.get("note") or "").strip()[:500]
            try: write_marks(st.cfg, ids, origin, by, note, clear)
            except (OSError, ValueError) as e: return self.send(500, {"error": str(e)})
            st.reload_assets()
            return self.send(200, {"assets": st.assets, "status": st.status})
        self.send(404, {"error": "not found"})


def editor_pages(cfg):
    """each editor folder's pages: the *.html at its root (the one named like the folder first)"""
    out = []
    for folder, prog, _ in EDITORS:
        d = os.path.join(cfg.quark, folder)
        try: pages = sorted((n for n in os.listdir(d) if n.lower().endswith(".html")), key=lambda n: (not n.lower().startswith(folder), n.lower()))
        except OSError: continue
        if pages: out.append({"folder": folder, "program": prog, "pages": pages, "url": "/editors/%s/%s" % (folder, urllib.parse.quote(pages[0])),
                              "file": os.path.join(d, pages[0])})
    return out


# local requests never go through a proxy (the system's, or HTTP_PROXY in the environment)
LOCAL = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def answers(url):
    """does anything answer at url (any HTTP status counts)"""
    try:
        with LOCAL.open(url, timeout=0.3): return True
    except urllib.error.HTTPError:
        return True
    except Exception:
        return False


def links(cfg):
    ups = run_parallel([(answers, (url,)) for _, _, url, _ in SIBLINGS])
    return {"siblings": [{"id": i, "label": l, "url": u, "start": st, "up": up} for (i, l, u, st), up in zip(SIBLINGS, ups)],
            "editors": editor_pages(cfg)}


def already_running(port):
    try:
        with LOCAL.open("http://127.0.0.1:%d/api/ping" % port, timeout=0.5) as r:
            return json.loads(r.read()).get("app") == APP_ID
    except Exception:
        return False


def bind(port):
    """the first free port from `port` (20 tries), else any"""
    for p in list(range(port, port + 20)) + [0]:
        try: return ThreadingHTTPServer(("127.0.0.1", p), Handler)
        except OSError: continue
    raise SystemExit("no free port")


def install(dest):
    os.makedirs(dest, exist_ok=True)
    for n in PROGRAM_FILES:
        src = os.path.join(HERE, n)
        if os.path.isfile(src): shutil.copyfile(src, os.path.join(dest, n))
    print("Editor Manager installed in %s (your marks, provenance.json there, are kept)" % dest)
    print("start it: %s" % os.path.join(dest, "dashboard.cmd" if os.name == "nt" else "dashboard.py"))


def main():
    ap = argparse.ArgumentParser(description="Quark Editor Manager (local, read-only on the git clones)")
    ap.add_argument("--quark", help="the Quark folder (holds repository\\, working-repo\\, the editors)")
    ap.add_argument("--repo", help="the clone whose assets/provenance/ is the registry (a name in the Quark folder or a path)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--data-dir", help="the game's user data folder (its overrides\\ is listed)")
    ap.add_argument("--sidecar", help="the dashboard's own provenance.json (default: next to the program)")
    ap.add_argument("--install", nargs="?", const="", metavar="DIR", help="copy the program to DIR (default <quark>\\editor-dashboard)")
    ap.add_argument("--dump", action="store_true", help="print summary, changes and assets as JSON and stop")
    a = ap.parse_args()
    cfg = Config(a)
    if a.install is not None:
        return install(os.path.abspath(a.install) if a.install else os.path.join(cfg.quark, "editor-dashboard"))
    if shutil.which("git") is None: print("warning: git is not on PATH: the Changes view will be empty")
    state = State(cfg)
    if a.dump:
        state.refresh()
        json.dump({"summary": state.summary(), "status": state.status, "changes": state.changes, "assets": state.assets}, sys.stdout, indent=1)
        return
    if os.environ.get("QUARK_SHELL") == "1": a.no_browser = True           # (the Quark Project app shows the page itself)
    if already_running(a.port):                                         # (a second double-click: show the open one)
        if not a.no_browser: webbrowser.open("http://127.0.0.1:%d/" % a.port)
        print("already running on port %d" % a.port); return
    Handler.state = state
    srv = bind(a.port)
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    print("Quark Editor Manager: %s\n  Quark folder: %s\n  registry:     %s\n  your marks:   %s\nCtrl+C stops it."
          % (url, cfg.quark, os.path.join(cfg.repo, "assets", "provenance"), cfg.sidecar))
    threading.Thread(target=state.refresh, daemon=True).start()          # (the page waits for the first snapshot)
    if not a.no_browser: threading.Timer(0.2, webbrowser.open, (url,)).start()
    try: srv.serve_forever()
    except KeyboardInterrupt: pass


if __name__ == "__main__":
    main()

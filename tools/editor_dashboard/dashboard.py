#!/usr/bin/env python3
"""Quark Editor Manager: the owner's local page for the editors (sound, UI, map, bot, model, gamemode, physics and
weapons editor) only. GitHub, Claude Documents, game code and tooling changes live in the Supervisor Board
(127.0.0.1:8768), not here.

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
    files in their commits ahead of testing and their uncommitted files (other files are not shown);
  - the editors' files that differ between the production tag (the newest v<semver>) and origin/testing;
  - each editor folder (EDITORS below): its exported files and its provenance.json sidecar (docs/PROVENANCE.md), and
    the game's own overrides folder on this PC;
  - the provenance registry, <repo>/assets/provenance/*.json;
  - the Quark Project app's tab list (tabs.json / tabs.default.json: each editor's tab and port, checked for up / down)
    and its installs-state.json (the version of each editor installed, compared with testing's).

What it writes: its own sidecar, <install dir>\\provenance.json (tool "dashboard"), when the owner marks items; its
merge file (merge-plan.json next to it) and, only when the owner clicks "Merge into <clone>" and confirms,
`tools/provenance.py merge` of that file run hidden in the registry clone (never in repository\\, the mirror the app
keeps fast-forwarded); a local commit of assets/provenance/ only, when the owner clicks "Commit locally" (never a push);
a stopped server editor started hidden on "Start" (its log in logs\\). Otherwise it never writes into a git clone: git
runs with --no-optional-locks (status doesn't even refresh the index), there is no checkout or push anywhere, and
`git fetch` only runs when Refresh is clicked with "fetch" ticked.
Git calls are batched per clone and run in parallel; the snapshot is cached, served while a refresh runs, and rebuilt
by itself (without fetch) when a cheap mtime check sees a sidecar, the registry, a clone's HEAD / refs or an editor's
file in a working tree change.
Python 3 standard library only; runs on Windows and Linux.
"""
import argparse, ast, concurrent.futures, datetime, fnmatch, functools, gzip, hashlib, json, math, mimetypes, os, re, shutil
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
# which editor makes each provenance category's assets (the Assets view's Editor column, the human-made % per editor);
# images (pictures, the website's) belong to none
CATEGORY_EDITOR = {"sounds": "sound-editor", "voices": "sound-editor", "ui": "ui-customizer", "fonts": "ui-customizer",
                   "maps": "map-editor", "bots": "bot-editor", "models": "model-editor", "gamemodes": "gamemode-editor"}

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


# each editor's source folder in the repository (its first PATH_RULES folder under tools/): how installs.json names it
TOOL_SOURCE = {PROGRAM_ID[prog]: next((p.rstrip("/") for p in pats if p.startswith("tools/") and p.endswith("/")), None)
               for prog, pats in PATH_RULES}

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


def read_text(path):
    """a small file's text, stripped (OSError when it can't be read)"""
    with open(path, encoding="utf-8") as f: return f.read().strip()


def git_dir(path):
    """a clone's or worktree's own git folder (.git, or where a worktree's .git file points), found without git"""
    dotgit = os.path.join(path, ".git")
    if os.path.isdir(dotgit): return dotgit
    try:
        line = read_text(dotgit)
    except OSError:
        return None
    return os.path.normpath(os.path.join(path, line[7:].strip())) if line.startswith("gitdir:") else None


def git_common_dir(path):
    """the .git folder shared by a clone and its worktrees, found without running git"""
    gd = git_dir(path)
    if not gd: return None
    if os.path.basename(gd) == ".git" and os.path.isdir(gd): return os.path.normcase(os.path.realpath(gd))
    try:
        gd = os.path.join(gd, read_text(os.path.join(gd, "commondir")))
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

TEST_TAG = re.compile(r"^test-(\d+\.\d+\.\d+(?:\.testing|-test)\.(\d+))$")   # (.testing.<run> from 1.19.0 on)


def repo_refs(cwd):
    """one for-each-ref: origin/testing (else a local testing), the production tag (the newest v<semver>) and the
    testing build on testing's tip (its test-<VERSION>.testing.<run> prerelease tag, when fetched; before 1.19.0
    test-<VERSION>-test.<run>)"""
    out = GIT(cwd, "for-each-ref", "--format=%(refname)%00%(objectname)%00%(*objectname)",
              "refs/remotes/origin/testing", "refs/heads/testing", "refs/tags/v*", "refs/tags/test-*")
    refs, tags, tests = {}, [], []
    for line in (out or "").splitlines():
        p = line.split("\0")
        if len(p) < 3: continue
        refs[p[0]] = p[2] or p[1]
        if p[0].startswith("refs/tags/") and semver_key(p[0][10:]): tags.append(p[0][10:])
        m = TEST_TAG.match(p[0][10:]) if p[0].startswith("refs/tags/") else None
        if m: tests.append((int(m.group(2)), m.group(1), refs[p[0]]))
    testing_ref = ("origin/testing" if "refs/remotes/origin/testing" in refs else
                   "testing" if "refs/heads/testing" in refs else None)
    testing_sha = refs.get("refs/remotes/origin/testing") or refs.get("refs/heads/testing")
    prod = max(tags, key=semver_key) if tags else None
    build = max((t for t in tests if t[2] == testing_sha), default=None)
    return {"testing_ref": testing_ref, "testing_sha": testing_sha, "testing_build": build[1] if build else None,
            "prod_tag": prod, "prod_sha": refs.get("refs/tags/" + prod) if prod else None}


def local_testing_build(quark, testing_sha, version):
    """the testing build in Quark\\testing\\ (its BUILD.txt, docs/LOCAL_BUILD.md) when it is testing's tip:
    "<VERSION>.testing.<run>" (before 1.19.0 "<VERSION>-test.<run>"), else None"""
    try:
        with open(os.path.join(quark, "testing", "BUILD.txt"), encoding="utf-8", errors="replace") as f: text = f.read(65536)
    except OSError:
        return None
    m = re.search(r"\b(\d+\.\d+\.\d+(?:\.testing|-test)\.\d+)\b", text)     # (a release download names it)
    if m: return m.group(1) if version and m.group(1).startswith((version + "-", version + ".")) else None
    m = re.search(r"\b([0-9a-f]{40})((?:\.testing|-test)\.\d+)", text)           # (tools/build_release.ps1: sha.testing.<run>)
    return version + m.group(2) if m and version and m.group(1) == testing_sha else None


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
    clone also those commits and VERSION at both (and at its HEAD: what the app last installed the editors from)"""
    res = {"tp_files": {}, "testing_commits": [], "versions": {}}
    t, p = refs["testing_ref"], refs["prod_tag"]
    jobs = [(read_versions, (cwd, [t, p, "HEAD"]))] if is_ref else []
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


def testing_base(cwd, own_ref, own_sha, ref_sha):
    """what a clone is compared with: (rev, "own" / "reference" / "stale"). A clone whose own origin/testing is older
    than the reference clone's would count testing's newer commits as its local work, so it is compared with the
    reference's testing commit when it has that commit ("reference"); when it doesn't, its counts can't be trusted and
    are left out of the totals until a fetch ("stale"). Costs nothing for the usual clone (same sha), else one or two
    git calls."""
    if not ref_sha or own_sha == ref_sha: return own_ref, "own"
    if own_sha and GIT(cwd, "merge-base", "--is-ancestor", ref_sha, own_sha) is not None:
        return own_ref, "own"                                           # (this clone's testing is the newer one)
    if GIT(cwd, "cat-file", "-e", ref_sha + "^{commit}") is not None: return ref_sha, "reference"
    return own_ref, "stale"


def collect_worktree(c, refs, tp_files, ref=None):
    """one clone / worktree, the editors' files only: commits ahead of testing, uncommitted files, and each file's state:
    local (the change isn't in testing), testing (in testing, not production) or both (in testing and production).
    Four git calls, in parallel, each limited to the editors' paths. ref: the reference clone's {sha, tp_files}
    (testing_base)."""
    cwd, t, ps = c["path"], refs["testing_ref"], EDITOR_PATHSPEC
    base = "own"
    if ref:
        t, base = testing_base(cwd, t, refs["testing_sha"], ref["sha"])
        if base == "reference": tp_files = ref["tp_files"]
    jobs = [(GIT, (cwd, "status", "--porcelain=v1", "-z", "--untracked-files=all", *ps))]
    if t:
        jobs += [(GIT, (cwd, "log", "--no-merges", "--full-history", LOG_FORMAT, "--name-status", "--no-renames", t + "..HEAD", *ps)),
                 (GIT, (cwd, "diff", "--name-only", "-z", "--no-renames", t, *ps)),
                 # the working tree vs the merge base with testing (git 2.30+; older git: merge-base, then diff)
                 (GIT, (cwd, "diff", "--name-status", "-z", "--no-renames", "--merge-base", t, *ps))]
    outs = run_parallel(jobs)
    unc = [xf for xf in parse_status_z(outs[0]) if program_of(xf[1])]
    w = dict(name=c["name"], path=c["path"], role=c["role"], outside=c.get("outside", False),
             commits=[], uncommitted=unc, status={}, errors=[], base=base, stale=base == "stale")
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
    """{lower-case path under assets/overrides without extension: (path, size)} of the registry clone's overrides
    (working-repo\\ can hold commits testing doesn't have yet)"""
    base, idx = os.path.join(repo, "assets", "overrides"), {}
    for d, _, names in os.walk(base):
        for n in names:
            f = os.path.join(d, n)
            rel = os.path.relpath(f, base).replace(os.sep, "/")
            try: idx[os.path.splitext(rel)[0].lower()] = (f, os.path.getsize(f))
            except OSError: pass
    return idx


def testing_overrides_index(cwd, rev):
    """{lower-case path under assets/overrides without extension: (blob id, size)} at testing's tip (one ls-tree in
    the reference clone), or None without a clone / testing"""
    if not cwd or not rev: return None
    out = GIT(cwd, "ls-tree", "-r", "-z", "--long", rev, "--", "assets/overrides/")
    if out is None: return None
    idx = {}
    for rec in out.split("\0"):
        meta, _, path = rec.partition("\t")
        m = meta.split()
        if len(m) == 4 and m[1] == "blob" and path.startswith("assets/overrides/"):
            idx[os.path.splitext(path[17:])[0].lower()] = (m[2], int(m[3]) if m[3].isdigit() else -1)
    return idx


def git_blob_id(path):
    """the id git gives a file's content (sha1 of "blob <size>\\0" + the bytes): compared with ls-tree's, no git call"""
    h = hashlib.sha1()
    h.update(b"blob %d\0" % os.path.getsize(path))
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""): h.update(chunk)
    return h.hexdigest()


def same_file(a, b):
    try:
        if os.path.getsize(a) != os.path.getsize(b): return False
        with open(a, "rb") as fa, open(b, "rb") as fb:
            while True:
                x, y = fa.read(1 << 20), fb.read(1 << 20)
                if x != y: return False
                if not x: return True
    except OSError:
        return False


def compare_to_repo(f, rel, size, ov):
    """an exported override vs testing's assets/overrides: same (shipped to testing) / local (only the registry clone
    has it: an unpushed commit or an uncommitted file) / differs / missing. ov: (testing index or None, clone index)"""
    t_idx, w_idx = ov
    key = os.path.splitext(rel)[0].lower()
    hit, w = (t_idx or {}).get(key), w_idx.get(key)
    try:
        if hit and hit[1] == size and git_blob_id(f) == hit[0]: return "same"
    except OSError:
        return "differs"
    if w and same_file(f, w[0]): return "local"
    return "differs" if hit or (t_idx is None and w) else "missing"


# an editor folder's own files at its top level that are no export: its sidecar (the Assets view shows it), the
# installers' stamps, and (scan_exports' skip) the program files its installer copies, i.e. the names in its source
# folder (serve.py, bot_editor.example.json...); <name>.json next to a <name>.example.json is its settings
NOT_EXPORTS = {"provenance.json", "installed.txt", "installed.json", "installed_from.json", "readme.md", "readme.txt"}


def program_files(repo, folder):
    """the lower-case names at the top of an editor's source folder in the registry clone (TOOL_SOURCE)"""
    src = TOOL_SOURCE.get(folder)
    try: return {n.lower() for n in os.listdir(os.path.join(repo, *src.split("/")))} if src else set()
    except OSError: return set()


def scan_exports(folder, program, ov, overrides_root=False, skip=()):
    """files an editor wrote: anything under an export / overrides folder, output-looking files at its top level (not
    its program files: NOT_EXPORTS, skip, *.example.json and the settings next to one). overrides_root: the folder
    itself is an overrides folder (the game's per-user one); its files go to the editor their assets/overrides/ path
    belongs to, and those of no editor are listed apart (unowned). -> (files, how many more weren't listed, unowned paths)"""
    files, more, unowned = [], 0, []
    for d, dirs, names in os.walk(folder):
        rel_d = os.path.relpath(d, folder)
        depth = 0 if rel_d == "." else rel_d.count(os.sep) + 1
        parts = [] if rel_d == "." else rel_d.split(os.sep)
        in_export = overrides_root or any(EXPORT_DIRS.match(x) for x in parts)
        dirs[:] = [x for x in dirs if (in_export or x.lower() not in SKIP_DIRS) and not x.startswith(".") and depth < 6]
        low = {n.lower() for n in names} if depth == 0 and not in_export else ()
        for n in names:
            ext, ln = os.path.splitext(n)[1].lower(), n.lower()
            if not in_export:
                if depth or ext not in EXPORT_EXT or ln in NOT_EXPORTS or ln in skip or ln.endswith(".example.json") \
                        or (ext == ".json" and ln[:-5] + ".example.json" in low | set(skip)): continue
            f = os.path.join(d, n)
            rel = os.path.relpath(f, folder).replace(os.sep, "/")
            # the part after the last overrides/ folder is the asset's override path
            ov_path = rel if overrides_root else (rel.lower().split("overrides/", 1)[1] if "overrides/" in rel.lower() else None)
            if ov_path is not None: ov_path = rel[len(rel) - len(ov_path):]
            prog = program_of("assets/overrides/" + ov_path) if overrides_root else program
            if not prog:
                if len(unowned) < 200: unowned.append(rel)
                continue
            if len(files) >= MAX_FILES: more += 1; continue
            try: st = os.stat(f)
            except OSError: continue
            e = {"path": rel, "mtime": int(st.st_mtime), "size": st.st_size, "program": prog}
            if ov_path is not None: e["asset"], e["repo"] = ov_path, compare_to_repo(f, ov_path, st.st_size, ov)
            files.append(e)
    files.sort(key=lambda e: -e["mtime"])
    return files, more, unowned


# --- previews: a playable / viewable file for an asset id (the Assets view's play / view button) ---------------------------

AUDIO_EXT = {".wav": "audio/wav", ".ogg": "audio/ogg", ".flac": "audio/flac", ".mp3": "audio/mpeg"}
IMAGE_EXT = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif", ".webp": "image/webp"}


def preview_index(cfg, export_srcs, exports, ov_idx):
    """{lower-case asset id without extension: file}: the newest exported override first (the human-made replacement),
    then the registry clone's assets/overrides/, then the game's own asset export (build*/assets_export/, the files
    Asset Studio shows; written by `quark_tests exportassets`). Audio and pictures only."""
    idx = {}

    def add(asset, f):
        if os.path.splitext(f)[1].lower() in AUDIO_EXT or os.path.splitext(f)[1].lower() in IMAGE_EXT:
            idx.setdefault(os.path.splitext(asset)[0].lower(), f)
    for (_, d, _, _), src in zip(export_srcs, exports):
        for e in src["files"]:
            if e.get("asset"): add(e["asset"], os.path.join(d, *e["path"].split("/")))
    for key, (f, _) in ov_idx.items(): add(key + os.path.splitext(f)[1], f)
    for b in ("build", "build-win"):
        base = os.path.join(cfg.repo, b, "assets_export")
        for d, _, names in os.walk(base):
            for n in names:
                f = os.path.join(d, n)
                add(os.path.relpath(f, base).replace(os.sep, "/"), f)
    return idx


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
    """an entry changes the registry when the registry doesn't have the item or gives it another origin (an entry that
    only repeats the registry's origin, with another by / date / note, changes nothing the human-made % counts)"""
    return reg is None or reg.get("origin", "ai") != entry.get("origin")


def sidecar_paths(cfg):
    out = [(os.path.join(cfg.quark, folder, "provenance.json"), tool, prog) for folder, prog, tool in EDITORS]
    if all(os.path.normcase(cfg.sidecar) != os.path.normcase(p) for p, _, _ in out):
        out.append((cfg.sidecar, "dashboard", "Dashboard"))
    return out


# The one rule that decides an item, used by the page (its "pending" origin, the % with the pending marks) and by Merge
# (which folds exactly those choices in, docs/PROVENANCE.md):
#   - the owner's mark (the dashboard's sidecar) wins over every editor's entry;
#   - without one, the editors' newest entry by date wins (same date: the later editor in EDITORS);
#   - the item is pending while the winner's origin isn't the registry's. So once a merge wrote it, nothing an editor
#     still says flips it back: the owner's mark keeps winning, and an editor's entry that lost stays settled;
#   - an editor entry that disagrees with the owner's mark is a conflict (it needs a decision) unless the owner marked the
#     item while seeing that very entry (the mark's "seen": {tool: fingerprint}); two editors that disagree, without a
#     mark, are a conflict too. A new export of the item (another date, note or origin) is a new fingerprint: a conflict
#     again. Marking the item (or removing your mark) decides it.

def entry_fp(e):
    """a sidecar entry's fingerprint (its fields as the editor wrote them): a new export of the item changes it"""
    return hashlib.sha1(json.dumps({k: v for k, v in e.items() if k != "seen"}, sort_keys=True,
                                   ensure_ascii=False).encode("utf-8")).hexdigest()[:12]


def sidecar_entries(cfg, errors=None):
    """every sidecar's entries -> ({id: [entry + tool, sidecar, own, fp, order]}, [{path, tool, items, mtime, own}])"""
    by_id, sidecars = {}, []
    for order, (path, tool, _) in enumerate(sidecar_paths(cfg)):
        doc, err = read_json(path)
        if err and errors is not None: errors.append(rel_or_abs(path, cfg.quark) + ": skipped" + err[len(path):])
        if not isinstance(doc, dict): continue
        tool = doc.get("tool") or tool
        own = os.path.normcase(path) == os.path.normcase(cfg.sidecar)
        n = 0
        for k, v in (doc.get("items") or {}).items():
            if not isinstance(v, dict): continue
            n += 1
            by_id.setdefault(k, []).append(dict(v, tool=v.get("tool") or tool, src=tool, sidecar=path, own=own,
                                                fp=entry_fp(v), order=order, raw=v))
        try: mtime = int(os.path.getmtime(path))
        except OSError: mtime = 0
        sidecars.append({"path": path, "tool": tool, "items": n, "mtime": mtime, "own": own})
    return by_id, sidecars


def decide(ents, reg):
    """the rule above for one item -> (winner, pending, conflict); each entry gets "st": win / same (agrees with the
    winner) / over (the owner's mark overrules it, seen) / conflict"""
    own = next((e for e in ents if e["own"]), None)
    win = own or max(ents, key=lambda e: (str(e.get("date") or ""), e["order"]))
    seen = (own.get("seen") if own and isinstance(own.get("seen"), dict) else None) or {}
    for e in ents:
        if e is win: e["st"] = "win"
        elif e.get("origin") == win.get("origin"): e["st"] = "same"
        elif own and seen.get(e["src"]) == e["fp"]: e["st"] = "over"
        else: e["st"] = "conflict"
    return win, differs(win, reg), any(e["st"] == "conflict" for e in ents)


# an id's first part -> the registry category tools/provenance.py merges it into (its PREFIX, read from the registry
# clone's copy when it can be; this is the fallback). An id of no category can't be merged: it is skipped and flagged.
MERGE_PREFIX = {"sounds": "sounds", "voices": "voices", "models": "models", "maps": "maps", "fonts": "fonts",
                "images": "images", "website": "images", "ui": "ui", "bots": "bots", "gamemodes": "gamemodes"}


@functools.lru_cache(maxsize=4)
def _merge_prefix(path, mtime):
    try:
        with open(path, encoding="utf-8") as f: m = re.search(r"^PREFIX\s*=\s*(\{[^}]*\})", f.read(), re.M)
        v = ast.literal_eval(m.group(1)) if m else None
        return v if isinstance(v, dict) and v else MERGE_PREFIX
    except (OSError, ValueError, SyntaxError):
        return MERGE_PREFIX


def merge_prefix(cfg):
    p = os.path.join(cfg.repo, "tools", "provenance.py")
    try: return _merge_prefix(p, os.path.getmtime(p))
    except OSError: return MERGE_PREFIX


def rel_or_abs(path, base):
    """path relative to base when that is shorter and on the same drive, else as it is"""
    try: rel = os.path.relpath(path, base)
    except ValueError: return path                                       # (another drive on Windows)
    return rel if len(rel) < len(path) else path


def merge_target(cfg):
    """where "Merge" folds the pending marks in: the registry clone, unless that is repository\\ (the mirror the app
    fast-forwards: a merge there would leave it dirty and stop the automatic updates) -> {name, path, ok, why}"""
    name = os.path.basename(os.path.normpath(cfg.repo))
    why = None
    if name.lower() == "repository":
        why = "the registry is read from repository\\ (the mirror the app keeps up to date): make a working-repo clone to merge into"
    elif not os.path.isfile(os.path.join(cfg.repo, "tools", "provenance.py")):
        why = "%s has no tools/provenance.py" % name
    return {"name": name, "path": cfg.repo, "ok": why is None, "why": why}


def plan_path(cfg):
    """the merge file the dashboard writes next to its sidecar: exactly the page's choice per pending item"""
    return os.path.join(os.path.dirname(cfg.sidecar), "merge-plan.json")


def decisions(cfg, registry, errors=None):
    """every item some sidecar names -> {id: (winner, pending, conflict, entries)}, the sidecars, the merge prefixes"""
    by_id, sidecars = sidecar_entries(cfg, errors)
    return {k: (*decide(ents, registry.get(k)), ents) for k, ents in by_id.items()}, sidecars, merge_prefix(cfg)


def unmergeable(k, prefix):
    """why tools/provenance.py can't merge an id (None when it can)"""
    top = k.split("/", 1)[0]
    return None if top in prefix else "%s/ is no registry category (%s): Merge skips it" % (top, ", ".join(sorted(set(prefix.values()))))


def assets_payload(cfg, previews=None):
    registry, files, errors = load_registry(cfg.repo)
    dec, sidecars, prefix = decisions(cfg, registry, errors)
    side_of = {s["path"]: s for s in sidecars}
    for s in sidecars: s.update(pending=0, conflicts=0, over=0, short=rel_or_abs(s["path"], cfg.quark))
    rows, to_merge, own_pending = [], 0, 0
    for k in sorted(set(registry) | set(dec)):
        r = registry.get(k)
        row = {"id": k, "cat": category_of(k, registry), "o": r.get("origin", "ai") if r else None}
        for f in ("by", "date", "tool", "source", "note", "wip", "version"):
            if r and r.get(f): row[f] = r[f]
        if k in dec:
            win, pend, conf, ents = dec[k]
            for e in ents:
                if e["st"] in ("over", "conflict"): side_of[e["sidecar"]]["over" if e["st"] == "over" else "conflicts"] += 1
            if pend or conf:
                # p: the sidecar entries, the winner first (its origin is the page's and Merge's); pm: Merge changes it
                row["p"] = [{f: e[f] for f in ("origin", "by", "date", "note", "tool", "sidecar", "st") if e.get(f)}
                            for e in [win] + [e for e in ents if e is not win]]
                if conf: row["c"] = 1
                bad = pend and unmergeable(k, prefix)
                if bad: row["bad"] = bad
                elif pend:
                    row["pm"] = 1; to_merge += 1; side_of[win["sidecar"]]["pending"] += 1; own_pending += win["own"]
        f = (previews or {}).get(os.path.splitext(k)[0].lower())
        if f: row["pv"] = "a" if os.path.splitext(f)[1].lower() in AUDIO_EXT else "i"
        rows.append(row)
    stats = {}
    for row in rows:
        s = stats.setdefault(row["cat"], {"human": 0, "third-party": 0, "ai": 0, "pending": 0, "conflicts": 0,
                                          "eff": {"human": 0, "third-party": 0, "ai": 0}})
        if row["o"] in ORIGINS: s[row["o"]] += 1
        eff = row["p"][0].get("origin") if row.get("p") else row["o"]
        if eff in ORIGINS: s["eff"][eff] += 1
        if row.get("pm") or row.get("bad"): s["pending"] += 1
        if row.get("c"): s["conflicts"] += 1
    # the command for running the merge yourself in the registry clone: the merge file's path relative to it when it
    # can be (..\editor-dashboard\merge-plan.json); the page writes that file first (POST /api/plan)
    py = "py -3" if os.name == "nt" else "python3"
    plan = rel_or_abs(plan_path(cfg), cfg.repo)
    cmd = (py + " tools/provenance.py merge " + ('"%s"' % plan if " " in plan else plan)) if to_merge else ""
    return {"repo": cfg.repo, "registryFiles": files, "errors": errors, "rows": rows, "stats": stats, "sidecars": sidecars,
            "ownSidecar": cfg.sidecar, "mergeCmd": cmd, "mergeCwd": cfg.repo, "toMerge": to_merge,
            "mergeFrom": [s["path"] for s in sidecars if s["pending"]], "mergeTarget": merge_target(cfg),
            "catEditor": CATEGORY_EDITOR, "ownPending": own_pending, "regDirty": registry_dirty(cfg)}


def registry_dirty(cfg):
    """the registry clone's changed files under assets/provenance/ (a merge not committed yet): [[xy, path]], or None
    when the merge target isn't usable (one git status, limited to that folder)"""
    if not merge_target(cfg)["ok"]: return None
    return parse_status_z(GIT(cfg.repo, "status", "--porcelain=v1", "-z", "--untracked-files=all", "--", "assets/provenance/"))


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


def write_marks(cfg, ids, origin, by, note, clear, registry=None, others=None):
    """the dashboard's own sidecar: add / replace (or, clear=True, remove) the entries for ids; unknown fields kept.
    registry: {id: origin}; others: {id: [(tool, origin, fingerprint)]}, the editors' entries. A mark records the editor
    entries it overrules ("seen", so they aren't conflicts: decide()). A mark that only repeats the registry's origin,
    with no editor saying otherwise, isn't written, and removes an older mark of yours for that item instead; with an
    editor saying otherwise it is written (it overrules that editor). -> {marked, removed, same} counts (removed: your
    entries actually taken out)"""
    res = {"marked": 0, "removed": 0, "same": 0}
    with SIDECAR_LOCK:
        doc, err = read_json(cfg.sidecar)
        if err: raise ValueError("can't read %s: %s" % (cfg.sidecar, err))
        if not isinstance(doc, dict): doc = {}
        doc.setdefault("format", 1)
        doc["tool"] = "dashboard"
        items = doc.get("items") if isinstance(doc.get("items"), dict) else {}
        today = datetime.date.today().isoformat()
        changed = False
        for k in ids:
            against = {t: fp for t, o, fp in (others or {}).get(k, []) if o != origin}
            if clear or ((registry or {}).get(k) == origin and not against):
                had = items.pop(k, None) is not None
                changed |= had
                if clear: res["removed"] += had
                else: res["same"] += 1
                continue
            e = {"origin": origin, "date": today}
            if by: e["by"] = by
            if note: e["note"] = note
            if against: e["seen"] = against
            items[k] = e
            res["marked"] += 1
            changed = True
        if not changed: return res                                       # (nothing to write)
        doc["items"] = {k: items[k] for k in sorted(items)}
        write_json(cfg.sidecar, doc)
    return res


def write_json(path, doc):
    """atomically, in the sidecars' layout"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False); f.write("\n")
    os.replace(tmp, path)


def write_plan(cfg):
    """the merge file (plan_path): for every pending item that can be merged, its winning entry (decide()) with the
    tool it came from, in a sidecar of its own (tool "dashboard") -> (path, items written, [skipped id: why])"""
    registry, _, _ = load_registry(cfg.repo)
    dec, _, prefix = decisions(cfg, {k: v for k, v in registry.items()})
    items, skipped = {}, []
    for k, (win, pend, _, _) in sorted(dec.items()):
        if not pend: continue
        why = unmergeable(k, prefix)
        if why: skipped.append("%s: %s" % (k, why)); continue
        e = {f: v for f, v in win["raw"].items() if f != "seen"}
        e["tool"] = win["raw"].get("tool") or win["src"]
        items[k] = e
    path = plan_path(cfg)
    write_json(path, {"format": 1, "tool": "dashboard", "note": "written by the Editor Manager: each pending item's winning "
                      "sidecar entry (its tool says which); tools/provenance.py merge this file", "items": items})
    return path, len(items), skipped


MERGE_LOCK = threading.Lock()


def run_merge(cfg):
    """write_plan, then `tools/provenance.py merge <the merge file>` in the registry clone, hidden (no console window),
    with this Python. -> (ok, output lines). Only on the owner's click + confirm; an id of no category is skipped (and
    said), so it never blocks the rest."""
    tgt = merge_target(cfg)
    if not tgt["ok"]: return False, [tgt["why"]]
    if not MERGE_LOCK.acquire(blocking=False): return False, ["a merge is already running"]
    try:
        path, n, skipped = write_plan(cfg)
        notes = ["  skipped " + x for x in skipped]
        if not n: return False, notes + ["nothing to merge"]
        r = subprocess.run([sys.executable or "python", os.path.join("tools", "provenance.py"), "merge", path],
                           cwd=cfg.repo, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           env=dict(os.environ, PYTHONIOENCODING="utf-8"), timeout=120, creationflags=GIT.flags)
        lines = r.stdout.decode("utf-8", "replace").splitlines()
        return r.returncode == 0, (notes + lines)[-40:]
    except (OSError, ValueError, subprocess.TimeoutExpired) as e:
        return False, [str(e)]
    finally:
        MERGE_LOCK.release()


def git_write(cwd, *args, timeout=60):
    """a git call that writes (commit): -> (ok, its output with stderr), hidden, never prompting"""
    try:
        r = subprocess.run(["git", "-c", "core.quotepath=off", "-C", cwd] + list(args), stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(GIT.env, GIT_EDITOR="true"),
                           timeout=timeout, creationflags=GIT.flags)
        return r.returncode == 0, r.stdout.decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, str(e)


REG_DIFF_MAX = 400                                                         # (lines of the diff the page shows)


def registry_diff(cfg):
    """what a commit of assets/provenance/ would hold: {files: [[xy, path]], stat, diff (cut at REG_DIFF_MAX lines)}"""
    files = registry_dirty(cfg)
    if files is None: return {"files": None, "why": merge_target(cfg)["why"]}
    new = [f for xy, f in files if xy == "??"]
    diff = GIT(cfg.repo, "diff", "HEAD", "--no-color", "--", "assets/provenance/") or ""
    for f in new:                                                        # (untracked: a category file that's new)
        try:
            with open(os.path.join(cfg.repo, *f.split("/")), encoding="utf-8", errors="replace") as fh: text = fh.read(1 << 20)
        except OSError: continue
        diff += "new file %s\n" % f + "".join("+" + ln + "\n" for ln in text.splitlines())
    lines = diff.splitlines()
    stat = (GIT(cfg.repo, "diff", "HEAD", "--shortstat", "--", "assets/provenance/") or "").strip()
    return {"files": files, "stat": stat + (" (+%d new)" % len(new) if new else ""), "lines": len(lines),
            "diff": "\n".join(lines[:REG_DIFF_MAX])}


def commit_registry(cfg, message):
    """a local commit of assets/provenance/ only, in the registry clone (never repository\\, never a push); other
    staged or changed files stay as they are -> (ok, output lines)"""
    tgt = merge_target(cfg)
    if not tgt["ok"]: return False, [tgt["why"]]
    if not registry_dirty(cfg): return False, ["nothing to commit in assets/provenance/"]
    ok, out = git_write(cfg.repo, "add", "--", "assets/provenance/")
    if ok: ok, out = git_write(cfg.repo, "commit", "-q", "-m", message, "--", "assets/provenance/", timeout=180)
    if ok: out = (GIT(cfg.repo, "log", "-1", "--format=%h %s") or "").strip() + "\n" + out
    return ok, [l for l in out.splitlines() if l.strip()][-40:]


# --- the whole picture --------------------------------------------------------------------------------------------------

def by_program(clones, repos, ref_key, exports):
    """regroups everything per editor: a commit shows under each editor it touches, with only that editor's files. A
    stale clone (testing_base) is listed with stale: true and left out of `files`, so no count includes it."""
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
            # net: the commit still brings a change testing lacks (a file of it is "local"); a commit cherry-picked
            # into testing, or one its revert undid, isn't counted as local work
            for cm in g["commits"].values(): cm["net"] = any(x[2] == "local" for x in cm["files"])
            progs[prog]["clones"].append({"clone": w["name"], "commits": list(g["commits"].values()),
                                          "uncommitted": g["uncommitted"], "stale": w["stale"]})
            if w["stale"]: continue
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


# --- the editors: their Quark Project tabs, links, up / down, the version installed --------------------------------------

def tabs_list(cfg):
    """the Quark Project app's tab list (read-only): q's Quark\\project-manager\\tabs.json, else the app's
    shell\\tabs.default.json, else the repository's copy -> (tabs, the file read or None)"""
    for path in (os.path.join(cfg.quark, "project-manager", "tabs.json"),
                 os.path.join(cfg.quark, "project-manager", "shell", "tabs.default.json"),
                 os.path.join(cfg.quark, "repository", "tools", "project_manager", "shell", "tabs.default.json"),
                 os.path.join(os.path.dirname(HERE), "project_manager", "shell", "tabs.default.json")):
        doc, _ = read_json(path)
        tabs = doc.get("tabs") if isinstance(doc, dict) else doc
        if isinstance(tabs, list): return [t for t in tabs if isinstance(t, dict)], path
    return [], None


def root_pages(d, folder):
    """the *.html at an editor folder's root, the one named like the folder first, then index.html"""
    try: names = [n for n in os.listdir(d) if n.lower().endswith(".html") and os.path.isfile(os.path.join(d, n))]
    except OSError: return []
    return sorted(names, key=lambda n: (n.lower() != folder + ".html", n.lower() != "index.html", n.lower()))


def editor_links(cfg):
    """each editor (EDITORS order): its Quark Project tab and its link, unprobed. A server editor (its tab has a local
    http url: map, bot, gamemode, physics, weapons) runs its own serve.py on the tab's port and is linked there (without
    ?embed=1, which is for the app); a page editor (a folder + file tab: sound, UI, model) is served from here under
    /editors/<folder>/ (Chrome / Edge won't follow a file:// link from an http page). -> (editors, tabs file)"""
    tabs, src = tabs_list(cfg)
    by_folder = {}
    for t in tabs:
        if t.get("folder"): by_folder.setdefault(str(t["folder"]).strip("\\/").lower(), t)
    out = []
    for folder, prog, _ in EDITORS:
        d, t = os.path.join(cfg.quark, folder), by_folder.get(folder) or {}
        e = {"folder": folder, "program": prog, "tab": t.get("id"), "installed": os.path.isdir(d), "kind": None,
             "url": None, "port": None, "start": t.get("start") or "", "pages": root_pages(d, folder)}
        u = urllib.parse.urlsplit(str(t.get("url") or ""))
        try: port = int(t.get("port") or u.port or 0)
        except (TypeError, ValueError): port = 0
        if u.scheme in ("http", "https") and (u.hostname or "").lower() in ("127.0.0.1", "localhost") and port:
            q = urllib.parse.urlencode([kv for kv in urllib.parse.parse_qsl(u.query, keep_blank_values=True) if kv[0] != "embed"])
            e.update(kind="server", port=port, url="%s://127.0.0.1:%d%s%s" % (u.scheme, port, u.path or "/", "?" + q if q else ""))
        elif e["pages"] and not (not t and os.path.isfile(os.path.join(d, "serve.py"))):
            # (a page editor; a folder with its own serve.py but no tab can't be opened from here: its port is unknown)
            page = t.get("file") if t.get("file") in e["pages"] else e["pages"][0]
            e.update(kind="page", url="/editors/%s/%s" % (folder, urllib.parse.quote(page)), file=os.path.join(d, page))
        elif e["installed"]:
            e["note"] = "no tab for it in %s" % (os.path.basename(src) if src else "tabs.json")
        out.append(e)
    return out, src


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
    """the nav bar's links, checked live: the Supervisor Board and every server editor's port, in parallel (0.3 s at
    most); a page editor is up when its page is installed"""
    eds, src = editor_links(cfg)
    probe = [u for _, _, u, _ in SIBLINGS] + ["http://127.0.0.1:%d/" % e["port"] for e in eds if e["kind"] == "server"]
    ups = iter(run_parallel([(answers, (u,)) for u in probe]))
    sibs = [{"id": i, "label": l, "url": u, "start": st, "up": next(ups)} for i, l, u, st in SIBLINGS]
    for e in eds:
        e["up"] = next(ups) if e["kind"] == "server" else bool(e["kind"] == "page" and os.path.isfile(e["file"]))
    return {"siblings": sibs, "editors": eds, "tabsFrom": src}


def start_command(e):
    """a server editor's tab "start" command (py -3 serve.py --port N --no-open) as the argv this Python runs, always on
    the tab's port; None when it isn't a plain Python command (cmd.exe syntax, a .cmd...)"""
    cmd = e.get("start") or ""
    if not cmd or re.search(r"[&|<>^%]", cmd) or cmd.count('"') % 2: return None
    argv = [x.replace('"', "") for x in re.findall(r'(?:"[^"]*"|[^\s"])+', cmd)]
    if argv and re.fullmatch(r"(?i)py(\.exe)?", argv[0]):
        argv = argv[1:]
        if argv and re.fullmatch(r"-[23](\.\d+)?", argv[0]): argv = argv[1:]
    elif argv and re.fullmatch(r"(?i)python3?w?(\.exe)?", argv[0]): argv = argv[1:]
    else: return None
    script = next((a for a in argv if not a.startswith("-")), "")
    if not script.lower().endswith(".py") or os.path.isabs(script) or ".." in script: return None
    argv = [a for a in argv if not a.startswith("--port=")]
    if "--port" in argv:
        i = argv.index("--port"); argv[i + 1:i + 2] = [str(e["port"])]
    else: argv += ["--port", str(e["port"])]
    return [sys.executable or "python"] + argv


STARTED, START_LOCK, START_WAIT = {}, threading.Lock(), 12.0


def start_editor(cfg, folder):
    """POST /api/start: start a stopped server editor the way the Quark Project app does, hidden (no console window),
    in its folder, on its tab's port; its output goes to <the dashboard's folder>/logs/<folder>.log. Waits up to
    START_WAIT s for it to answer -> (ok, message)"""
    e = next((x for x in editor_links(cfg)[0] if x["folder"] == folder), None)
    if not e or e["kind"] != "server": return False, "%s isn't a server editor here" % (e["program"] if e else folder or "?")
    url = "http://127.0.0.1:%d/" % e["port"]
    if answers(url): return True, "%s is already running" % e["program"]
    argv = start_command(e)
    if not argv: return False, "%s: its start command in tabs.json isn't a plain Python command; the Quark Project app starts it" % e["program"]
    log = os.path.join(os.path.dirname(cfg.sidecar), "logs", folder + ".log")
    with START_LOCK:                                                     # (one start at a time per editor)
        p = STARTED.get(folder)
        if not p or p.poll() is not None:
            try:
                os.makedirs(os.path.dirname(log), exist_ok=True)
                with open(log, "ab") as fh:
                    fh.write(("[%s] start: %s\n" % (datetime.datetime.now().isoformat(timespec="seconds"), " ".join(argv))).encode("utf-8"))
                    fh.flush()
                    # (Windows: hidden, its own process group, never DETACHED_PROCESS: a detached python would open a
                    # console for its children; elsewhere its own session)
                    kw = {"creationflags": GIT.flags | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)} if os.name == "nt" \
                        else {"start_new_session": True}
                    p = STARTED[folder] = subprocess.Popen(argv, cwd=os.path.join(cfg.quark, folder), stdin=subprocess.DEVNULL,
                                                           stdout=fh, stderr=subprocess.STDOUT,
                                                           env=dict(os.environ, QUARK_SHELL="1", PYTHONUNBUFFERED="1"), **kw)
            except OSError as x:
                return False, "%s didn't start: %s" % (e["program"], x)
    end = time.monotonic() + START_WAIT
    while time.monotonic() < end:
        if answers(url): return True, "%s is running on port %d" % (e["program"], e["port"])
        if p.poll() is not None: return False, "%s stopped (exit code %s): see %s" % (e["program"], p.returncode, log)
        time.sleep(0.25)
    return False, "%s is still starting (no answer on port %d after %d s): see %s" % (e["program"], e["port"], START_WAIT, log)


def installs_registry(cfg):
    """installs.json (what the app installs from: repository\\'s copy, else the installed one) -> [entries]"""
    for path in (os.path.join(cfg.quark, "repository", "tools", "project_manager", "installs.json"),
                 os.path.join(cfg.quark, "project-manager", "installs.json"),
                 os.path.join(os.path.dirname(HERE), "project_manager", "installs.json")):
        doc, _ = read_json(path)
        lst = doc.get("entries") if isinstance(doc, dict) else doc
        if isinstance(lst, list): return [e for e in lst if isinstance(e, dict) and e.get("id") and isinstance(e.get("sources"), list)]
    return []


def installs_view(cfg, ref_cwd, testing_rev, versions):
    """per editor folder, the version the Quark Project app installed compared with testing's. The app stores, per
    installs.json entry, the git ids of its `sources` when the install ran (Quark\\project-manager\\installs-state.json);
    the same ids at testing's tip and at the reference clone's HEAD come from one `cat-file --batch-check`.
    -> {folder: {entry, name, state, at, version}}, state: current (testing's) / head (repository\\'s HEAD, testing is
    newer: the app pulls on its next pass) / older / failed (the last try at testing's failed) / never (the app has no
    record) / waiting (not in testing yet) / unknown (no git)"""
    entries = installs_registry(cfg)
    saved, _ = read_json(os.path.join(cfg.quark, "project-manager", "installs-state.json"))
    saved = saved.get("entries") if isinstance(saved, dict) and isinstance(saved.get("entries"), dict) else {}
    pick = {}
    for folder, _, _ in EDITORS:
        e = next((x for x in entries if re.split(r"[\\/]+", str(x.get("check") or ""))[0].lower() == folder), None) or \
            next((x for x in entries if TOOL_SOURCE.get(folder) in [str(s).strip("/") for s in x["sources"]]), None)
        if e: pick[folder] = e
    uniq = list({e["id"]: e for e in pick.values()}.values())
    stamps = {}
    if ref_cwd and testing_rev and uniq:
        lines = [(e["id"], rev, s) for e in uniq for rev in (testing_rev, "HEAD") for s in e["sources"]]
        out = subprocess_bytes(ref_cwd, ["cat-file", "--batch-check"], "".join("%s:%s\n" % (rev, s) for _, rev, s in lines))
        res = out.decode("utf-8", "replace").splitlines()
        if len(res) == len(lines):
            for (eid, rev, _), r in zip(lines, res):
                stamps.setdefault((eid, rev), []).append(r.split()[0][:40] if not r.endswith(" missing") else "-")
    view = {}
    for folder, e in pick.items():
        rec = saved.get(e["id"]) if isinstance(saved.get(e["id"]), dict) else {}
        v = {"entry": e["id"], "name": e.get("name") or e["id"], "at": rec.get("lastRunAt"), "version": None}
        cur, head = stamps.get((e["id"], testing_rev)), stamps.get((e["id"], "HEAD"))
        cur, head = cur and "+".join(cur), head and "+".join(head)
        if not cur: v["state"] = "unknown"
        elif cur.startswith("-"): v["state"] = "waiting"
        elif rec.get("stamp") == cur: v.update(state="current", version=versions.get(testing_rev))
        elif rec.get("failedStamp") == cur: v["state"] = "failed"
        elif rec.get("stamp") and rec.get("stamp") == head: v.update(state="head", version=versions.get("HEAD"))
        else: v["state"] = "older" if rec.get("stamp") else "never"
        view[folder] = v
    return view


# --- the cheap change check: stat calls only ------------------------------------------------------------------------------

def stat_sig(paths):
    out = []
    for p in paths:
        try: st = os.stat(p); out.append((st.st_mtime_ns, st.st_size))
        except OSError: out.append(None)
    return tuple(out)


def git_watch_paths(cfg, clones, export_dirs):
    """what tells that git changed for a clone without running git: its HEAD and index, the branch HEAD names, the
    shared packed-refs and origin/testing; plus the Quark folder (a new clone) and the export folders' tops"""
    paths = [cfg.quark] + list(export_dirs)
    for c in clones:
        gd, common = git_dir(c["path"]), c["common"]
        if not gd or not common: continue
        paths += [os.path.join(gd, "HEAD"), os.path.join(gd, "index"), os.path.join(common, "packed-refs"),
                  os.path.join(common, "refs", "remotes", "origin", "testing")]
        try:
            head = read_text(os.path.join(gd, "HEAD"))
            if head.startswith("ref: refs/"): paths.append(os.path.join(common, *head[5:].split("/")))
        except OSError:
            pass
    return paths


def _tree_roots():
    """PATH_RULES as what to scan for uncommitted edits: the folders (recursively) and, per folder, the globs matched
    against its own files (a glob with no folder: the clone's top)"""
    rec, flat = [], {}
    for _, pats in PATH_RULES:
        for pat in pats:
            if pat.endswith("/"): rec.append(pat.rstrip("/")); continue
            head = pat.split("*", 1)[0]
            flat.setdefault(head.rsplit("/", 1)[0] if "/" in head else "", []).append(pat)
    return sorted(set(rec)), flat


TREE_REC, TREE_FLAT = _tree_roots()
TREE_SKIP = {"node_modules", "__pycache__", "old"}                         # (and .* / build*: git ignores those)


def tree_sig(root):
    """the editors' files in a working tree, by scandir (on Windows its entries carry their stat: no call per file):
    (count, sum of mtime + size). An uncommitted edit, a new file or a deleted one changes it; a few ms per clone."""
    n = tot = 0

    def add(e):
        nonlocal n, tot
        st = e.stat(follow_symlinks=False); n += 1; tot += st.st_mtime_ns + st.st_size
    stack = [os.path.join(root, *d.split("/")) for d in TREE_REC]
    while stack:
        try: it = os.scandir(stack.pop())
        except OSError: continue
        with it:
            for e in it:
                try:
                    if not e.is_dir(follow_symlinks=False): add(e)
                    elif not (e.name.startswith(".") or e.name.lower().startswith("build") or e.name in TREE_SKIP): stack.append(e.path)
                except OSError: pass
    for d, pats in TREE_FLAT.items():
        try: it = os.scandir(os.path.join(root, *d.split("/")) if d else root)
        except OSError: continue
        with it:
            for e in it:
                rel = (d + "/" if d else "") + e.name.lower()
                try:
                    if any(fnmatch.fnmatchcase(rel, p) for p in pats) and not e.is_dir(follow_symlinks=False): add(e)
                except OSError: pass
    return n, tot


def assets_sig(cfg):
    """the sidecars and every registry file: (mtime, size) each"""
    sig = list(stat_sig([p for p, _, _ in sidecar_paths(cfg)]))
    try:
        with os.scandir(os.path.join(cfg.repo, "assets", "provenance")) as it:
            sig += sorted((e.name, e.stat().st_mtime_ns, e.stat().st_size) for e in it if e.name.endswith(".json"))
    except OSError:
        pass
    return tuple(sig)


class State:
    """the cached snapshot. refresh() rebuilds it (Refresh, or by itself when watch() sees a change). Readers always get
    the last complete snapshot at once, also while a refresh runs (git fetch can take minutes); only the very first
    snapshot is waited for."""
    WATCH_GAP = 2.0                                                      # (s between two change checks)
    TREE_GAP = 6.0                                                       # (s between two scans of the working trees)

    def __init__(self, cfg):
        self.cfg, self.lock, self.ready = cfg, threading.Lock(), threading.Event()
        self.status = self.changes = self.assets = None
        self.previews, self.git_watch, self.tree_roots, self.git_sig, self.assets_sig = {}, [], [], None, None
        self.watch_lock, self.watched_at, self.trees_at, self.last_fetch = threading.Lock(), 0.0, 0.0, 0

    def refresh(self, fetch=False):
        with self.lock:
            try: self._refresh(fetch)
            finally: self.ready.set()

    def _auto(self):
        if not self.lock.acquire(blocking=False): return                   # (a refresh is running: it reads the change)
        try: self._refresh(False, auto=True)
        finally: self.lock.release()

    def watch(self):
        """the cheap check (on GET /api/summary and /api/stamp): a few dozen stat calls, at most every WATCH_GAP s. A
        changed sidecar or registry file reloads the assets at once (no git, a few ms); a changed HEAD, index, branch or
        testing ref, or (scanned every TREE_GAP s) an uncommitted edit of an editor's file, starts a refresh in the
        background (no fetch) while the last snapshot keeps being served."""
        now = time.monotonic()
        with self.watch_lock:
            if not self.ready.is_set() or self.status is None or self.lock.locked() or now - self.watched_at < self.WATCH_GAP: return
            self.watched_at = now
            trees = now - self.trees_at >= self.TREE_GAP
            if trees: self.trees_at = now
            if self.git_changed(trees):
                threading.Thread(target=self._auto, daemon=True).start()
            elif assets_sig(self.cfg) != self.assets_sig:
                self.reload_assets()

    def git_sig_now(self, watch=None, trees=None):
        """the clones' HEAD / index / refs (stat) and the editors' files in each working tree (tree_sig)"""
        return stat_sig(self.git_watch if watch is None else watch), \
            tuple(tree_sig(r) for r in (self.tree_roots if trees is None else trees))

    def git_changed(self, trees_too=True):
        """the cheap check's git part; the working trees (tree_sig, a few ms per clone) only when trees_too"""
        if stat_sig(self.git_watch) != self.git_sig[0]: return True
        return trees_too and tuple(tree_sig(r) for r in self.tree_roots) != self.git_sig[1]

    def _refresh(self, fetch, auto=False):
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
            self.last_fetch = int(time.time())
        export_srcs = []
        for folder, prog, tool in EDITORS:
            d = os.path.join(cfg.quark, folder)
            if os.path.isdir(d): export_srcs.append((folder, d, prog, False))
        if os.path.isdir(os.path.join(cfg.data_dir, "overrides")):
            export_srcs.append(("this PC: overrides", os.path.join(cfg.data_dir, "overrides"), None, True))
        links_job = TASKS.submit(links, cfg)                             # (the port probes run beside the git calls)
        # (read before git runs: a change made while this refresh reads is seen by the next check)
        git_watch = git_watch_paths(cfg, clones, [d for _, d, _, _ in export_srcs])
        tree_roots = [c["path"] for c in clones if c["role"] == "work"]    # (the mirror is only ever pulled)
        git_sig = self.git_sig_now(git_watch, tree_roots)
        mirror = next((c for c in clones if c["role"] == "mirror"), None)
        ref_clone = mirror or next((c for c in clones if os.path.normcase(c["path"]) == os.path.normcase(cfg.repo)), None) \
            or (clones[0] if clones else None)
        commons = list(by_common.keys())
        refs = dict(zip(commons, run_parallel([(repo_refs, (by_common[k]["path"],)) for k in commons])))
        ref_key = ref_clone["common"] if ref_clone else None
        rr = refs.get(ref_key) or {"testing_ref": None, "testing_sha": None, "testing_build": None, "prod_tag": None, "prod_sha": None}
        ref_cwd = ref_clone["path"] if ref_clone else None
        # testing vs production once per pair of commits (clones whose origin/testing and production tag are the
        # reference's, the usual case, share its result), with testing's assets/overrides (the exports are compared
        # with it); then every worktree and editor folder at once
        pair = lambda k: (refs[k]["testing_sha"], refs[k]["prod_sha"])
        firsts = {}
        for k in ([ref_key] if ref_key else []) + commons: firsts.setdefault(pair(k), k)
        res = run_parallel([(collect_repo, (by_common[k]["path"], refs[k], k == ref_key)) for k in firsts.values()]
                           + [(testing_overrides_index, (ref_cwd, rr["testing_sha"]))], TASKS)
        t_idx = res.pop()
        pair_res = dict(zip(firsts, res))
        repos = {k: pair_res[pair(k)] for k in commons}
        ov_idx = repo_overrides_index(cfg.repo)
        ref = {"sha": rr["testing_sha"], "tp_files": (repos.get(ref_key) or {}).get("tp_files", {})}
        jobs = [(collect_worktree, (c, refs[c["common"]], repos[c["common"]]["tp_files"], ref)) for c in clones]
        jobs += [(scan_exports, (d, prog, (t_idx, ov_idx), root, () if root else program_files(cfg.repo, n)))
                 for n, d, prog, root in export_srcs]
        vers = (repos.get(ref_key) or {}).get("versions", {})
        jobs.append((installs_view, (cfg, ref_cwd, rr["testing_ref"], vers)))
        res = run_parallel(jobs, TASKS)
        installs = res.pop()
        worktrees = res[:len(clones)]
        exports = [{"name": n, "path": d, "files": r[0], "more": r[1], "unowned": r[2]}
                   for (n, d, _, _), r in zip(export_srcs, res[len(clones):])]
        for w, c in zip(worktrees, clones): w["testingSha"] = (refs[c["common"]]["testing_sha"] or "")[:9]
        previews = preview_index(cfg, export_srcs, exports, ov_idx)
        a_sig = assets_sig(cfg)
        assets = assets_payload(cfg, previews)
        editors = [{"folder": f, "program": p, "present": os.path.isdir(os.path.join(cfg.quark, f)), "install": installs.get(f)}
                   for f, p, _ in EDITORS]
        changes = {"programs": by_program(worktrees, repos, ref_key, exports),
                   "clones": [{k: w[k] for k in w if k not in ("commits", "uncommitted", "status")} |
                              {"commits": sum(1 for cm in w["commits"] if any(w["status"].get(f) == "local" for _, f in cm["files"])),
                               "allCommits": len(w["commits"]), "uncommitted": len(w["uncommitted"]),
                               "local": sum(1 for x in w["status"].values() if x == "local")} for w in worktrees],
                   "exports": [{"name": e["name"], "path": e["path"], "files": len(e["files"]), "more": e["more"],
                                "unowned": e["unowned"]} for e in exports],
                   "editors": editors, "rules": PATH_RULES}
        tv = vers.get(rr["testing_ref"])
        nav = links_job.result()
        ms = (time.perf_counter() - t0) * 1000
        status = {
            "app": APP_ID, "quark": cfg.quark, "repo": cfg.repo, "refClone": ref_clone["name"] if ref_clone else None,
            "testing": {"ref": rr["testing_ref"], "sha": (rr["testing_sha"] or "")[:9], "version": tv,
                        "build": rr["testing_build"] or local_testing_build(cfg.quark, rr["testing_sha"], tv)},
            "production": {"tag": rr["prod_tag"], "sha": (rr["prod_sha"] or "")[:9], "version": vers.get(rr["prod_tag"])},
            "human": summary_pct(assets["stats"]), "refreshed": int(time.time()), "assetsAt": int(time.time()),
            "ms": round(ms), "gitCalls": GIT.calls - calls0, "gitMs": round(GIT.ms - ms0), "fetched": fetch, "auto": auto,
            "lastFetch": self.last_fetch, "fetchErrors": fetch_errors, "noClones": not clones, "ownSidecar": cfg.sidecar}
        status["links"] = nav
        # (one switch: a reader never sees half of the new snapshot for long)
        self.changes, self.assets, self.previews, self.status = changes, assets, previews, status
        self.git_watch, self.tree_roots, self.git_sig, self.assets_sig = git_watch, tree_roots, git_sig, a_sig

    def reload_assets(self):
        """after a mark or a merge, or when a sidecar / registry file changed on disk: only those are read again (no git)"""
        self.ready.wait()
        sig = assets_sig(self.cfg)
        self.assets, self.assets_sig = assets_payload(self.cfg, self.previews), sig
        self.status = dict(self.status, human=summary_pct(self.assets["stats"]), assetsAt=int(time.time()))

    def stamp(self):
        """GET /api/stamp: what the page polls to know the snapshot changed"""
        st = self.status or {}
        return {"refreshed": st.get("refreshed"), "assetsAt": st.get("assetsAt"), "refreshing": self.lock.locked(),
                "ready": self.ready.is_set()}

    def summary(self):
        """GET /api/summary: the compact, stable shape other tools read (the Supervisor Board on 8768, the Quark Project
        tab bar). Keep its keys (new ones may be added)."""
        st, a = self.status, self.assets
        cats, tot = {}, {"human": 0, "third_party": 0, "ai": 0}
        for c, v in sorted(a["stats"].items()):
            cats[c] = {"total": v["human"] + v["third-party"] + v["ai"], "human": v["human"], "third_party": v["third-party"], "ai": v["ai"]}
            for k in tot: tot[k] += cats[c][k]
        n = sum(tot.values())
        progs = []
        for p in self.changes["programs"]:
            counted = [c for c in p["clones"] if not c.get("stale")]
            local = {cm["sha"] for c in counted for cm in c["commits"] if cm.get("net", True)}
            mine = [v for c, v in cats.items() if CATEGORY_EDITOR.get(c) == p["id"]]
            an, ah = sum(v["total"] for v in mine), sum(v["human"] + v["third_party"] for v in mine)
            progs.append({"id": p["id"], "name": p["name"], "ahead_testing": len(local),
                          "ahead_production": len(local) + len(p["testing"]),
                          "uncommitted": sum(len(c["uncommitted"]) for c in counted),
                          "files_changed": sum(1 for _, sts in p["files"] if "local" in sts.values()),
                          "assets": an, "pct_human": pct_human(ah, an)})
        return {"testing": {"version": st["testing"]["version"], "ref": st["testing"]["ref"], "sha": st["testing"]["sha"],
                            "build": st["testing"].get("build")},
                "production": {"version": st["production"]["version"], "tag": st["production"]["tag"],
                               "sha": st["production"]["sha"]},
                "fetched": bool(st["fetched"]),             # (the Quark Project tab bar shows these: "with fetch" / "no fetch")
                "provenance": dict(total=n, pct_human=pct_human(tot["human"] + tot["third_party"], n),
                                   by_category=cats, **tot),
                "pending_marks": sum(1 for r in a["rows"] if r.get("pm") or r.get("bad")),
                "conflicts": sum(1 for r in a["rows"] if r.get("c")), "programs": progs, "remaining": tot["ai"],
                "stale_clones": [c["name"] for c in self.changes["clones"] if c.get("stale")],
                "refreshing": self.lock.locked(),
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
        if path == "/api/stamp":
            st.watch()
            return self.send(200, st.stamp())
        st.ready.wait()                                                  # (only the first snapshot is waited for)
        if path in ("/api/status", "/api/changes", "/api/assets"): return self.send(200, getattr(st, path[5:]))
        if path == "/api/summary":
            st.watch()
            return self.send(200, st.summary())
        if path == "/api/preview": return self.send_preview()
        if path == "/api/regdiff": return self.send(200, registry_diff(st.cfg))
        self.send(404, {"error": "not found"})

    def send_preview(self):
        """GET /api/preview?id=<asset id>: the asset's file from the preview index (never a path from the request)"""
        q = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        f = self.state.previews.get(os.path.splitext((q.get("id") or [""])[0])[0].lower())
        ext = os.path.splitext(f or "")[1].lower()
        ctype = AUDIO_EXT.get(ext) or IMAGE_EXT.get(ext)
        try: data = open(f, "rb").read() if ctype else None
        except OSError: data = None
        if data is None: return self.send(404, {"error": "no preview"})
        self.send(200, data, ctype)

    def send_editor_file(self, path):
        """GET /editors/<folder>/<file>: an editor's own page and its files, read-only, only inside the editor folders
        (Chrome / Edge won't follow a file:// link from this page, so the page editors are served from here)"""
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
            st.ready.wait()
            registry = {r["id"]: r["o"] for r in st.assets["rows"] if r["o"]}
            # (an id the registry doesn't have and no category takes can't be merged: never write a mark for it)
            prefix, bad = merge_prefix(st.cfg), []
            if not clear:
                bad = [i for i in ids if i not in registry and unmergeable(i, prefix)]
                ids = [i for i in ids if i not in bad]
                if not ids: return self.send(400, {"error": unmergeable(bad[0], prefix).split(":")[0] + ": can't mark " + bad[0]})
            by_id, _ = sidecar_entries(st.cfg)
            others = {k: [(e["src"], e.get("origin"), e["fp"]) for e in by_id.get(k, []) if not e["own"]] for k in ids}
            try: counts = write_marks(st.cfg, ids, origin, by, note, clear, registry, others)
            except (OSError, ValueError) as e: return self.send(500, {"error": str(e)})
            counts["bad"] = len(bad)
            counts["overruled"] = sum(1 for k in ids for _, o, _ in others[k] if not clear and o != origin)
            st.reload_assets()
            return self.send(200, {"assets": st.assets, "status": st.status, "result": counts})
        if path == "/api/merge":
            # (only with the page's confirmation: the merge file holds the page's choice for each pending item now)
            if req.get("confirm") is not True: return self.send(400, {"error": "needs confirm: true"})
            st.ready.wait()
            ok, lines = run_merge(st.cfg)
            st.reload_assets()
            return self.send(200 if ok else 500, {"ok": ok, "output": lines, "assets": st.assets, "status": st.status,
                                                  **({} if ok else {"error": (lines or ["merge failed"])[-1]})})
        if path == "/api/plan":
            # (the merge file for running the merge yourself: written, then the page copies the command)
            st.ready.wait()
            try: f, n, skipped = write_plan(st.cfg)
            except (OSError, ValueError) as e: return self.send(500, {"error": str(e)})
            return self.send(200, {"path": f, "items": n, "skipped": skipped, "cmd": st.assets["mergeCmd"]})
        if path == "/api/commit":
            if req.get("confirm") is not True: return self.send(400, {"error": "needs confirm: true"})
            msg = str(req.get("message") or "").strip()[:500] or "Provenance: merge the editors' and the owner's marks"
            st.ready.wait()
            ok, lines = commit_registry(st.cfg, msg)
            st.reload_assets()
            return self.send(200 if ok else 500, {"ok": ok, "output": lines, "assets": st.assets, "status": st.status,
                                                  **({} if ok else {"error": (lines or ["commit failed"])[-1]})})
        if path == "/api/start":
            ok, msg = start_editor(st.cfg, str(req.get("folder") or ""))
            return self.send(200 if ok else 500, {"ok": ok, "message": msg, "links": links(st.cfg), **({} if ok else {"error": msg})})
        self.send(404, {"error": "not found"})


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

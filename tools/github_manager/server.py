#!/usr/bin/env python3
"""Quark GitHub manager: a read-only local dashboard server for floration/quark.

    python server.py [--data <folder>] [--port N] [--open] [--once]

Serves the page (index.html and its files, from this script's folder) and the JSON API in API.md on
http://127.0.0.1:<port>/ (127.0.0.1 only). One background thread polls GitHub through `gh api` (REST only) every
60 s while a workflow run is queued / in progress, otherwise every 300 s. It never writes to GitHub or to the
owner's working copies: comparisons happen in its own bare mirror (<data>/mirror.git), and local copies are only
read with `git status --porcelain` / `git worktree list --porcelain` (GIT_OPTIONAL_LOCKS=0) and fetched from into
the mirror. Everything it writes is inside <data> (default %USERPROFILE%\\Desktop\\Quark\\github-manager).
Python 3.8+ standard library only.
"""

import sys

sys.dont_write_bytecode = True  # never leave __pycache__ next to the script (it may run from a working copy)

import argparse
import datetime
import collections
import errno
import gzip
import hashlib
import http.server
import json
import os
import re
import subprocess
import threading
import time
import urllib.parse
import webbrowser

DEFAULT_REPO = "floration/quark"
DEFAULT_PORT = 8767
FAST_SEC = 60            # poll interval while any run is queued / in progress
SLOW_SEC = 300           # otherwise
BACKLOG_SEC = 20         # while old runs' jobs / logs are still being downloaded (first start)
REFRESH_MIN_SEC = 10     # POST /api/refresh: at most one poll per 10 s
RATE_CHECK_SEC = 1800    # `gh api rate_limit` (free) this often; every response's headers update it too
DISCOVER_SEC = 600       # re-scan quarkDir for local copies this often (and on every manual refresh)
JOBS_BUDGET = 30         # completed runs whose jobs are fetched per poll (active runs are always fetched)
LOGS_BUDGET = 10         # failed-job logs downloaded per poll
LOG_RETRY_SEC = 1800     # a log download that failed (not 404 / 410) is retried after this
MAX_TESTING_COMMITS = 200
MAX_LOCAL_COMMITS = 100
MAX_DIRTY_FILES = 50
NEW_SLACK_SEC = 600      # an item first seen now is "new" (gets an event) if it changed after the last poll - this

ACTIVE_STATUSES = frozenset(("queued", "in_progress", "waiting", "pending", "requested"))
FAILED_CONCLUSIONS = frozenset(("failure", "timed_out", "startup_failure"))
INFRA_BRANCHES = frozenset(("launcher", "site", "discord-bot", "gh-pages"))
PROD_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
SKIP_DIRS = frozenset(("claude documents",))  # a different repo next to the working copies
STATIC_EXT = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
              ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
              ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon", ".woff2": "font/woff2",
              ".woff": "font/woff", ".map": "application/json"}
STATIC_BLOCKED = frozenset(("cache", "logs", "mirror.git", "test", "__pycache__"))  # data folders never served
CORS_ORIGIN_RE = re.compile(r"^http://(127\.0\.0\.1|localhost):(876[5-8])$")
WIN = os.name == "nt"
NO_WINDOW = 0x08000000 if WIN else 0  # CREATE_NO_WINDOW: git / gh never flash a console window

UTC = datetime.timezone.utc


# ----------------------------------------------------------------------------------------------------------------
# small helpers

def now_dt():
    return datetime.datetime.now(UTC)


def iso(dt):
    return dt.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ") if dt else None


def parse_time(s):
    if not s or not isinstance(s, str):
        return None
    s = s.strip()
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        dt = datetime.datetime.fromisoformat(s)
    except ValueError:
        return None
    return dt if dt.tzinfo else dt.replace(tzinfo=UTC)


def utc_iso(s):
    """Any ISO 8601 time (git's iso-strict with an offset, GitHub's Z) -> 'YYYY-MM-DDTHH:MM:SSZ'."""
    return iso(parse_time(s))


def seconds_between(a, b):
    da, db = parse_time(a), parse_time(b)
    return int((db - da).total_seconds()) if da and db else None


def short(sha):
    return sha[:7] if sha else None


def plural(n, word):
    return "%d %s%s" % (n, word, "" if n == 1 else "s")


def write_atomic(path, data):
    """Write bytes / str via a temp file + replace. Retries: on Windows another reader may hold the file open."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.tmp%d" % (path, os.getpid())
    with open(tmp, "wb") as f:
        f.write(data)
    for i in range(20):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.1 * (i + 1))
    os.replace(tmp, path)


def read_json(path, default=None):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def dumps(obj, pretty=False):
    if pretty:
        return json.dumps(obj, indent=1, ensure_ascii=False)
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


def version_key(tag):
    m = PROD_TAG_RE.match(tag or "")
    return tuple(int(x) for x in m.groups()) if m else None


def origin_matches(url, repo):
    """True when a remote URL points at owner/repo (https, ssh or scp-like, with or without .git)."""
    u = (url or "").strip().lower().rstrip("/")
    if u.endswith(".git"):
        u = u[:-4]
    r = repo.lower()
    return u.endswith("/" + r) or u.endswith(":" + r)


def branch_kind(name, default_branch):
    if name == default_branch:
        return "production"
    if name == "testing":
        return "testing"
    if name.startswith("claude/"):
        return "claude"
    if name in INFRA_BRANCHES:
        return "infra"
    return "other"


def ref_key(name):
    """A local copy's name as a safe ref component (refs/local/<key>/...)."""
    k = re.sub(r"[^A-Za-z0-9._-]+", "-", name).strip(".-") or "copy"
    return k[:-5] + "-lock" if k.endswith(".lock") else k


def cors_origin(origin):
    return origin if origin and CORS_ORIGIN_RE.match(origin) else None


# ----------------------------------------------------------------------------------------------------------------
# processes: gh and git, list arguments only (no shell, so paths with spaces are safe on Windows)

class Proc:
    def __init__(self):
        env = dict(os.environ)
        env.update({"GIT_OPTIONAL_LOCKS": "0", "GIT_TERMINAL_PROMPT": "0", "GH_PROMPT_DISABLED": "1",
                    "GH_NO_UPDATE_NOTIFIER": "1", "GH_PAGER": "", "PAGER": "", "NO_COLOR": "1",
                    "GH_SPINNER_DISABLED": "1", "CLICOLOR": "0"})
        self.env = env
        self.lock = threading.Lock()
        self.count = 0

    def run(self, args, cwd=None, timeout=90, binary=False):
        """-> (returncode, stdout, stderr). 127 when the program is missing, 124 on timeout."""
        with self.lock:
            self.count += 1
        try:
            p = subprocess.run(args, cwd=cwd, env=self.env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=timeout, creationflags=NO_WINDOW)
        except FileNotFoundError:
            return 127, b"" if binary else "", "%s not found on PATH" % args[0]
        except subprocess.TimeoutExpired:
            return 124, b"" if binary else "", "%s timed out after %d s" % (" ".join(args[:3]), timeout)
        except OSError as e:
            return 126, b"" if binary else "", "%s: %s" % (args[0], e)
        err = p.stderr.decode("utf-8", "replace").strip()
        out = p.stdout if binary else p.stdout.decode("utf-8", "replace")
        return p.returncode, out, err


class GhError(Exception):
    def __init__(self, status, message):
        Exception.__init__(self, message)
        self.status = status


def parse_gh_include(out):
    """`gh api -i` output -> (status, {lower header: value}, body)."""
    m = re.search(r"\r?\n\r?\n", out)
    head, body = (out[:m.start()], out[m.end():]) if m else (out, "")
    lines = head.splitlines()
    status = None
    if lines:
        sm = re.match(r"HTTP/\S+\s+(\d{3})", lines[0])
        status = int(sm.group(1)) if sm else None
    headers = {}
    for line in lines[1:]:
        k, sep, v = line.partition(":")
        if sep:
            headers[k.strip().lower()] = v.strip()
    return status, headers, body


def link_has_next(link):
    return bool(link) and 'rel="next"' in link


def with_query(path, **params):
    base, _, q = path.partition("?")
    qs = urllib.parse.parse_qsl(q, keep_blank_values=True)
    qs = [(k, v) for k, v in qs if k not in params] + [(k, str(v)) for k, v in params.items()]
    return base + "?" + urllib.parse.urlencode(qs)


class GitHub:
    """REST through `gh api`, with ETag conditional requests (a 304 does not count against the rate limit)."""

    def __init__(self, repo, proc):
        self.repo = repo
        self.proc = proc
        self.cache = {}            # path -> (etag, data, headers)
        self.pages = {}            # get_pages path -> all items, reused while page 1 answers 304
        self.rate = {"remaining": None, "limit": None, "reset": None}
        self.calls = 0
        self.changed = 0
        self.fatal = None          # gh missing / signed out: no more calls this poll
        self.escape_flag = None    # raw(): None = not known yet, True / False = whether gh takes the flag

    def _rate_from(self, headers):
        if headers.get("x-ratelimit-resource", "core") != "core":
            return
        try:
            if "x-ratelimit-remaining" in headers:
                self.rate["remaining"] = int(headers["x-ratelimit-remaining"])
            if "x-ratelimit-limit" in headers:
                self.rate["limit"] = int(headers["x-ratelimit-limit"])
            if "x-ratelimit-reset" in headers:
                self.rate["reset"] = int(headers["x-ratelimit-reset"])
        except ValueError:
            pass

    def get(self, path, conditional=True):
        """-> (data, headers, changed). Raises GhError."""
        if self.fatal:
            raise GhError(None, self.fatal)
        args = ["gh", "api", "-i", "-H", "Accept: application/vnd.github+json"]
        cached = self.cache.get(path)
        if conditional and cached and cached[0]:
            args += ["-H", "If-None-Match: " + cached[0]]
        args.append(path)
        self.calls += 1
        code, out, err = self.proc.run(args, timeout=90)
        status, headers, body = parse_gh_include(out)
        self._rate_from(headers)
        if status == 304 and cached:
            return cached[1], cached[2], False
        if code == 0 and status is not None and 200 <= status < 300:
            try:
                data = json.loads(body) if body.strip() else None
            except ValueError:
                raise GhError(status, "gh api %s: bad JSON" % path)
            self.changed += 1
            self.cache[path] = (headers.get("etag"), data, headers)
            return data, headers, True
        msg = err or ("HTTP %s" % status)
        if code == 127:
            self.fatal = msg = "gh (GitHub CLI) is not installed or not on PATH"
            raise GhError(None, msg)
        if status == 401 or "auth login" in (err or ""):
            self.fatal = msg = "gh is not signed in (run: gh auth login)"
            raise GhError(status, msg)
        raise GhError(status, "gh api %s: %s" % (path.split("?")[0], msg.splitlines()[-1] if msg else msg))

    def get_pages(self, path, max_pages=20, item_key=None):
        """Follows rel=next by page number (the Link URLs use repositories/<id>, which some proxies refuse).
        -> (items, changed_any)."""
        items, changed_any = [], False
        for page in range(1, max_pages + 1):
            data, headers, changed = self.get(with_query(path, page=page))
            if page == 1 and not changed and path in self.pages:
                return self.pages[path], False  # page 1 unchanged: new items would be on it, keep the rest
            changed_any = changed_any or changed
            chunk = (data or {}).get(item_key, []) if item_key else (data or [])
            items.extend(chunk)
            if not link_has_next(headers.get("link")) or not chunk:
                break
        self.pages[path] = items
        return items, changed_any

    def raw(self, path, timeout=180):
        """Non-JSON body (job logs). -> bytes. Raises GhError (status 404 / 410 when gone).
        Job logs contain terminal escape sequences, which newer gh (2.10x) refuses to print without
        --allow-escape-sequences; older gh doesn't know that flag ("unknown flag"), so it is dropped once for good."""
        while True:
            use_flag = self.escape_flag is not False
            args = ["gh", "api"] + (["--allow-escape-sequences"] if use_flag else []) + [path]
            self.calls += 1
            code, out, err = self.proc.run(args, timeout=timeout, binary=True)
            if code == 0:
                if use_flag:
                    self.escape_flag = True
                return out
            if use_flag and self.escape_flag is None and "unknown flag" in (err or "") and \
                    "allow-escape-sequences" in (err or ""):
                self.escape_flag = False
                continue
            break
        m = re.search(r"HTTP (\d{3})", err or "")
        first = next((l.strip() for l in (err or "").splitlines() if l.strip()), "failed (exit %s)" % code)
        raise GhError(int(m.group(1)) if m else None, "gh api %s: %s" % (path, first[:300]))

    def check_rate(self):
        data, headers, _ = self.get("rate_limit", conditional=False)
        core = ((data or {}).get("resources") or {}).get("core") or {}
        if "remaining" in core:
            self.rate.update({"remaining": core.get("remaining"), "limit": core.get("limit"),
                              "reset": core.get("reset")})


# ----------------------------------------------------------------------------------------------------------------
# git: the tool's own bare mirror, and read-only looks at local copies

GIT_COMMIT_FMT = "%H%x1f%s%x1f%an%x1f%cI%x1e"


class Git:
    def __init__(self, proc):
        self.proc = proc

    def run(self, args, cwd=None, timeout=90):
        return self.proc.run(["git", "-c", "core.quotepath=off"] + list(args), cwd=cwd, timeout=timeout)

    def ok(self, args, cwd=None, timeout=90):
        code, out, err = self.run(args, cwd=cwd, timeout=timeout)
        if code != 0:
            raise RuntimeError("git %s: %s" % (args[0] if args[0] != "-C" else args[2],
                                               (err or "failed").splitlines()[-1] if err else "failed"))
        return out


def parse_commits(out, repo=None, on_github=None):
    """GIT_COMMIT_FMT log output -> [commit]. on_github(sha) -> bool decides whether the GitHub url is set."""
    res = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\r\n")
        if not rec:
            continue
        f = rec.split("\x1f")
        if len(f) < 4:
            continue
        sha = f[0].strip()
        url = None
        if repo and (on_github is None or on_github(sha)):
            url = "https://github.com/%s/commit/%s" % (repo, sha)
        res.append({"sha": sha, "short": short(sha), "subject": f[1], "author": f[2], "date": utc_iso(f[3]),
                    "url": url})
    return res


class Mirror:
    """<data>/mirror.git: origin's branches as refs/heads/*, its tags as refs/tags/*, each local copy as
    refs/local/<key>/HEAD and refs/local/<key>/heads/*. `fetch --prune` only prunes the refspecs' own sides, so
    refs/local/* survives origin fetches."""

    def __init__(self, path, repo, git, origin_url=None):
        self.path = path
        self.repo = repo
        self.git = git
        self.origin_url = origin_url or "https://github.com/%s.git" % repo
        self.memo = {}

    def g(self, *args, timeout=90):
        return self.git.ok(["--git-dir", self.path] + list(args), timeout=timeout)

    def ensure(self):
        if os.path.isfile(os.path.join(self.path, "HEAD")):
            return
        os.makedirs(self.path, exist_ok=True)
        self.git.ok(["init", "--bare", "--quiet", self.path])
        self.g("config", "remote.origin.url", self.origin_url)
        self.g("config", "remote.origin.fetch", "+refs/heads/*:refs/heads/*")
        self.g("config", "--add", "remote.origin.fetch", "+refs/tags/*:refs/tags/*")
        self.g("config", "remote.origin.tagOpt", "--no-tags")
        self.g("config", "gc.autoDetach", "false")

    def fetch_origin(self):
        self.g("fetch", "--prune", "--quiet", "origin", timeout=900)

    def fetch_local(self, path, key):
        """Copies a local copy's HEAD and branches into refs/local/<key>/ (upload-pack in that repo only reads)."""
        self.g("fetch", "--no-tags", "--prune", "--quiet", path,
               "+refs/heads/*:refs/local/%s/heads/*" % key, "+HEAD:refs/local/%s/HEAD" % key, timeout=300)

    def refs(self):
        # for-each-ref writes a byte as %xx (git log uses %x1f)
        fmt = "%1f".join(["%(refname)", "%(objectname)", "%(*objectname)", "%(subject)", "%(authorname)",
                           "%(committerdate:iso-strict)", "%(*subject)", "%(*authorname)",
                           "%(*committerdate:iso-strict)", "%(creatordate:iso-strict)"])
        out = self.g("for-each-ref", "--format=" + fmt, "refs/heads", "refs/tags", "refs/local")
        heads, tags, local = {}, {}, {}
        for line in out.split("\n"):  # not splitlines(): it also splits at \x1f
            f = line.rstrip("\r").split("\x1f")
            if len(f) < 10:
                continue
            ref = f[0]
            sha = f[2] or f[1]
            info = {"sha": sha, "subject": f[6] if f[2] else f[3], "author": f[7] if f[2] else f[4],
                    "date": utc_iso(f[8] if f[2] else f[5]) or utc_iso(f[9])}
            if ref.startswith("refs/heads/"):
                heads[ref[11:]] = info
            elif ref.startswith("refs/tags/"):
                info["tagDate"] = utc_iso(f[9])
                tags[ref[10:]] = info
            elif ref.startswith("refs/local/") and ref.endswith("/HEAD"):
                local[ref[11:-5]] = sha
        return heads, tags, local

    def has_commit(self, sha):
        code, _, _ = self.git.run(["--git-dir", self.path, "cat-file", "-e", sha + "^{commit}"])
        return code == 0

    def ahead_behind(self, base, head):
        """-> (ahead, behind) of head vs base."""
        key = ("ab", base, head)
        if key not in self.memo:
            out = self.g("rev-list", "--left-right", "--count", "%s...%s" % (base, head))
            b, a = out.split()
            self.memo[key] = (int(a), int(b))
        return self.memo[key]

    def count(self, *revs):
        key = ("count",) + revs
        if key not in self.memo:
            self.memo[key] = int(self.g("rev-list", "--count", *revs).strip() or 0)
        return self.memo[key]

    def is_ancestor(self, a, b):
        code, _, _ = self.git.run(["--git-dir", self.path, "merge-base", "--is-ancestor", a, b])
        return code == 0

    def log(self, revs, max_count, first_parent=False):
        args = ["log", "--format=" + GIT_COMMIT_FMT, "--max-count=%d" % max_count]
        if first_parent:
            args.append("--first-parent")
        return self.g(*(args + list(revs) + ["--"]))

    def not_on_github(self, sha, limit=1000):
        """Commits reachable from sha that no origin branch has."""
        out = self.g("rev-list", "--max-count=%d" % limit, sha, "--not", "--branches")
        return set(out.split())

    def show_file(self, rev, path):
        code, out, _ = self.git.run(["--git-dir", self.path, "show", "%s:%s" % (rev, path)])
        return out.strip() if code == 0 else None

    def trim_memo(self):
        if len(self.memo) > 5000:
            self.memo.clear()


# ----------------------------------------------------------------------------------------------------------------
# local copies (read with `git status --porcelain` and `git worktree list --porcelain` only)

def parse_status_v2(out):
    """`git status --porcelain=v2 --branch -z` -> {oid, branch, upstream, dirtyFiles: [...], dirty}."""
    res = {"oid": None, "branch": None, "upstream": None, "dirtyFiles": [], "dirty": 0}
    recs = out.split("\0")
    i = 0
    while i < len(recs):
        r = recs[i]
        i += 1
        if not r:
            continue
        if r.startswith("# "):
            k, _, v = r[2:].partition(" ")
            if k == "branch.oid":
                res["oid"] = None if v == "(initial)" else v
            elif k == "branch.head":
                res["branch"] = None if v == "(detached)" else v
            elif k == "branch.upstream":
                res["upstream"] = v
            continue
        kind = r[0]
        if kind == "1":
            f = r.split(" ", 8)
            code, path = f[1], f[8] if len(f) > 8 else ""
        elif kind == "2":
            f = r.split(" ", 9)
            code, path = f[1], f[9] if len(f) > 9 else ""
            orig = recs[i] if i < len(recs) else ""
            i += 1
            path = "%s -> %s" % (orig, path)
        elif kind == "u":
            f = r.split(" ", 10)
            code, path = "UU" if f[1] == "UU" else f[1], f[10] if len(f) > 10 else ""
        elif kind == "?":
            code, path = "??", r[2:]
        else:
            continue  # "!" ignored files are never asked for
        code = code.replace(".", "") if code != "??" else code
        res["dirty"] += 1
        if len(res["dirtyFiles"]) < MAX_DIRTY_FILES:
            res["dirtyFiles"].append("%s %s" % (code or "?", path))
    return res


def parse_worktree_list(out):
    """`git worktree list --porcelain` -> [{path, head, branch, bare, detached, prunable}] (first = main)."""
    res, cur = [], None
    for line in out.splitlines():
        if line.startswith("worktree "):
            cur = {"path": os.path.normpath(line[9:]), "head": None, "branch": None, "bare": False,
                   "detached": False, "prunable": False}
            res.append(cur)
        elif cur is None:
            continue
        elif line.startswith("HEAD "):
            cur["head"] = line[5:]
        elif line.startswith("branch "):
            b = line[7:]
            cur["branch"] = b[11:] if b.startswith("refs/heads/") else b
        elif line == "bare":
            cur["bare"] = True
        elif line == "detached":
            cur["detached"] = True
        elif line.startswith("prunable"):
            cur["prunable"] = True
    return res


def norm_path(p):
    return os.path.normcase(os.path.normpath(os.path.abspath(p)))


def is_git_checkout(p):
    g = os.path.join(p, ".git")
    return os.path.isdir(g) or os.path.isfile(g)


def discover_locals(cfg, git, repo):
    """-> [{name, path, key, worktreeOf}] from cfg["locals"], or by scanning quarkDir (plus worktrees)."""
    entries = []
    explicit = cfg.get("locals")
    if isinstance(explicit, list):
        for e in explicit:
            if isinstance(e, str):
                e = {"path": e}
            if isinstance(e, dict) and e.get("path"):
                p = os.path.expandvars(os.path.expanduser(e["path"]))
                entries.append({"name": e.get("name") or os.path.basename(os.path.normpath(p)), "path": p})
    else:
        root = os.path.expandvars(os.path.expanduser(cfg.get("quarkDir") or ""))
        try:
            names = sorted(os.listdir(root), key=str.lower) if root else []
        except OSError:
            names = []
        for n in names:
            p = os.path.join(root, n)
            if n.lower() in SKIP_DIRS or not os.path.isdir(p) or not is_git_checkout(p):
                continue
            code, out, _ = git.run(["-C", p, "config", "--get", "remote.origin.url"])
            if code == 0 and origin_matches(out, repo):
                entries.append({"name": n, "path": p})
    # worktrees: which entry is a linked worktree of which, and (auto mode) worktrees living elsewhere
    seen = {norm_path(e["path"]): e for e in entries}
    mains = {}
    extra = []
    for e in list(entries):
        code, out, _ = git.run(["-C", e["path"], "worktree", "list", "--porcelain"])
        if code != 0:
            continue
        wts = [w for w in parse_worktree_list(out) if not w["bare"]]
        if not wts:
            continue
        main = norm_path(wts[0]["path"])
        for w in wts:
            np_ = norm_path(w["path"])
            mains[np_] = main
            if np_ not in seen and not isinstance(explicit, list) and not w["prunable"] and os.path.isdir(w["path"]):
                ne = {"name": os.path.basename(w["path"]), "path": w["path"]}
                seen[np_] = ne
                extra.append(ne)
    entries.extend(extra)
    used = set()
    for e in entries:
        base, n, k = e["name"], 2, e["name"]
        while k.lower() in used:
            k = "%s (%d)" % (base, n)
            n += 1
        used.add(k.lower())
        e["name"] = k
    keys = set()
    for e in entries:
        k, n = ref_key(e["name"]), 2
        while k in keys:
            k = "%s-%d" % (ref_key(e["name"]), n)
            n += 1
        keys.add(k)
        e["key"] = k
    by_norm = {norm_path(e["path"]): e for e in entries}
    for e in entries:
        np_ = norm_path(e["path"])
        main = mains.get(np_)
        if main and main != np_:
            m = by_norm.get(main)
            e["worktreeOf"] = m["name"] if m else os.path.basename(main)
        else:
            e["worktreeOf"] = None
    return entries


# ----------------------------------------------------------------------------------------------------------------
# GitHub objects -> API shapes

def run_summary(r):
    status = r.get("status")
    started = r.get("run_started_at") or r.get("created_at")
    end = r.get("updated_at") if status == "completed" else iso(now_dt())
    return {"id": r.get("id"), "workflow": r.get("name") or "", "branch": r.get("head_branch") or "",
            "sha": r.get("head_sha") or "", "event": r.get("event") or "", "title": r.get("display_title") or "",
            "status": status, "conclusion": r.get("conclusion"), "createdAt": r.get("created_at"),
            "updatedAt": r.get("updated_at"), "startedAt": started, "durationSec": seconds_between(started, end),
            "attempt": r.get("run_attempt") or 1, "runNumber": r.get("run_number"), "url": r.get("html_url"),
            "prs": [p.get("number") for p in (r.get("pull_requests") or []) if p.get("number")]}


def job_info(j):
    return {"id": j.get("id"), "name": j.get("name") or "", "status": j.get("status"),
            "conclusion": j.get("conclusion"), "startedAt": j.get("started_at"),
            "completedAt": j.get("completed_at"), "url": j.get("html_url"),
            "failedSteps": [s.get("name") for s in (j.get("steps") or []) if s.get("conclusion") == "failure"]}


def last_testing_run(runs):
    """The newest (by createdAt, then id) completed build-and-release run on testing, without jobs. A re-run keeps
    its run id and createdAt, so one that passes on the same sha replaces the red result."""
    best = None
    for r in runs:
        if r.get("branch") == "testing" and r.get("workflow") == "build-and-release" and \
                r.get("status") == "completed":
            if best is None or (r.get("createdAt") or "", r.get("id") or 0) > (best.get("createdAt") or "",
                                                                                 best.get("id") or 0):
                best = r
    return {k: v for k, v in best.items() if k != "jobs"} if best else None


def is_active(run):
    return (run or {}).get("status") in ACTIVE_STATUSES


def checks_state(runs):
    """Combined state of the runs for one commit: failure > pending > success > none (cancelled-only = failure)."""
    if not runs:
        return "none"
    if any(r.get("conclusion") in FAILED_CONCLUSIONS for r in runs):
        return "failure"
    if any(is_active(r) or r.get("status") != "completed" for r in runs):
        return "pending"
    if any(r.get("conclusion") == "success" for r in runs):
        return "success"
    if any(r.get("conclusion") == "cancelled" for r in runs):
        return "failure"
    return "none"


# ----------------------------------------------------------------------------------------------------------------
# history: diffing two snapshots into events

def ev(eid, at, typ, ref, text, data=None):
    return {"id": eid, "at": at, "type": typ, "ref": ref, "text": text, "data": data or {}}


def _recent(t, since_dt):
    d = parse_time(t)
    return bool(d and since_dt and d >= since_dt)


def diff_events(prev, cur, describe=None):
    """Snapshots -> events. A snapshot: {"at", "repo", "branches": {name: sha} | None, "tags": {name: sha} | None,
    "releases": {tag: {prerelease, publishedAt, url}} | None, "pulls": {"N": {...}} | None,
    "runs": {"id": {...}} | None, "local": {name: {sha, branch, dirty}}, "pollError": text | None}.
    A section that is None (unknown this poll) is skipped. prev None (first run) -> one "baseline" event.
    describe(kind, old_sha, new_sha) -> dict or None: "branch" -> {forward, count}; "local" -> {forward, count,
    notOnTesting, subject}."""
    at = cur["at"]
    if prev is None:
        c = lambda k: len(cur.get(k) or {})
        open_prs = sum(1 for p in (cur.get("pulls") or {}).values() if p.get("state") == "open")
        text = "Started watching %s: %s, %s, %s, %s, %s" % (
            cur.get("repo") or "the repo", plural(c("branches"), "branch").replace("branchs", "branches"),
            plural(c("tags"), "tag"), plural(open_prs, "open PR"), plural(c("runs"), "run"),
            plural(c("local"), "local copy").replace("copys", "copies"))
        return [ev("baseline:%s" % at, at, "baseline", {}, text,
                   {"branches": c("branches"), "tags": c("tags"), "openPulls": open_prs, "runs": c("runs"),
                    "local": c("local"), "releases": c("releases")})]
    describe = describe or (lambda kind, a, b: None)
    out = []
    since = parse_time(prev.get("at"))
    since = since - datetime.timedelta(seconds=NEW_SLACK_SEC) if since else None

    # runs
    pr_, cr_ = prev.get("runs"), cur.get("runs")
    if pr_ is not None and cr_ is not None:
        for rid in sorted(cr_, key=lambda x: int(x)):
            r = cr_[rid]
            p = pr_.get(rid)
            att = r.get("attempt") or 1
            new_attempt = p is None or (p.get("attempt") or 1) != att
            if p is None and not _recent(r.get("updatedAt"), since):
                continue  # an old run seen for the first time (it fell into the list), not news
            ref = {"run": int(rid)}
            wf, br = r.get("workflow") or "workflow", r.get("branch") or "?"
            data = {"workflow": wf, "branch": br, "attempt": att, "sha": r.get("sha"), "event": r.get("event"),
                    "url": r.get("url")}
            was_active = p is not None and not new_attempt and p.get("status") != "completed"
            if r.get("status") != "completed":
                if new_attempt:
                    t = ("%s re-run #%d started on %s" % (wf, att, br)) if att > 1 else \
                        ("%s started on %s (%s)" % (wf, br, r.get("event") or "?"))
                    out.append(ev("run.started:%s:%d" % (rid, att), r.get("startedAt") or r.get("createdAt") or at,
                                  "run.started", ref, t, data))
                continue
            if not (new_attempt or was_active):
                continue  # completed before and still the same attempt
            concl = r.get("conclusion")
            data["conclusion"] = concl
            data["durationSec"] = r.get("durationSec")
            when = r.get("updatedAt") or at
            if concl in FAILED_CONCLUSIONS:
                jobs = r.get("failedJobs") or []
                data["failedJobs"] = jobs
                t = "%s on %s failed%s" % (wf, br, (" (%s)" % ", ".join(jobs)) if jobs else "")
                if concl != "failure":
                    t += " [%s]" % concl.replace("_", " ")
                out.append(ev("run.failed:%s:%d" % (rid, att), when, "run.failed", ref, t, data))
            else:
                word = {"success": "passed", "cancelled": "was cancelled", "skipped": "was skipped",
                        "neutral": "finished (neutral)"}.get(concl, "finished: %s" % concl)
                out.append(ev("run.completed:%s:%d" % (rid, att), when, "run.completed", ref,
                              "%s on %s %s" % (wf, br, word), data))

    # pull requests
    pp, cp = prev.get("pulls"), cur.get("pulls")
    if pp is not None and cp is not None:
        for num in sorted(cp, key=lambda x: int(x)):
            c = cp[num]
            p = pp.get(num)
            n = int(num)
            ref = {"pr": n}
            data = {"title": c.get("title"), "head": c.get("head"), "base": c.get("base"), "url": c.get("url"),
                    "author": c.get("author")}
            if p is None:
                if _recent(c.get("createdAt"), since):
                    out.append(ev("pr.opened:%d" % n, c.get("createdAt") or at, "pr.opened", ref,
                                  "PR #%d opened: %s (%s -> %s)" % (n, c.get("title"), c.get("head"), c.get("base")),
                                  data))
                was_open = True
                if not (_recent(c.get("mergedAt"), since) or _recent(c.get("closedAt"), since)):
                    continue
            else:
                was_open = p.get("state") == "open"
            if c.get("state") == "closed" and (was_open or (p is not None and p.get("state") != "closed")):
                if c.get("merged"):
                    out.append(ev("pr.merged:%d" % n, c.get("mergedAt") or at, "pr.merged", ref,
                                  "PR #%d merged into %s: %s" % (n, c.get("base"), c.get("title")), data))
                else:
                    out.append(ev("pr.closed:%d:%s" % (n, c.get("closedAt") or at), c.get("closedAt") or at,
                                  "pr.closed", ref, "PR #%d closed without merging: %s" % (n, c.get("title")), data))
            elif p is not None and c.get("state") == "open" and p.get("state") != "open":
                out.append(ev("pr.reopened:%d:%s" % (n, p.get("closedAt")), at, "pr.opened", ref,
                              "PR #%d reopened: %s" % (n, c.get("title")), data))
            elif p is not None and c.get("state") == "open" and p.get("headSha") and c.get("headSha") and \
                    p.get("headSha") != c.get("headSha"):
                data.update({"from": p.get("headSha"), "to": c.get("headSha")})
                out.append(ev("pr.updated:%d:%s" % (n, c.get("headSha")), c.get("updatedAt") or at, "pr.updated",
                              ref, "PR #%d updated: %s now at %s" % (n, c.get("head"), short(c.get("headSha"))),
                              data))

    # branches
    pb, cb = prev.get("branches"), cur.get("branches")
    if pb is not None and cb is not None:
        for name in sorted(set(pb) | set(cb)):
            old, new = pb.get(name), cb.get(name)
            ref = {"branch": name}
            if old == new:
                continue
            if old is None:
                out.append(ev("branch.created:%s:%s" % (name, new), at, "branch.created", ref,
                              "Branch %s created at %s" % (name, short(new)), {"sha": new}))
            elif new is None:
                out.append(ev("branch.deleted:%s:%s" % (name, old), at, "branch.deleted", ref,
                              "Branch %s deleted (was %s)" % (name, short(old)), {"sha": old}))
            else:
                d = describe("branch", old, new) or {}
                data = {"from": old, "to": new}
                data.update(d)
                if d.get("forward") is False:
                    how = "force-pushed"
                elif d.get("count") is not None:
                    how = "+" + plural(d["count"], "commit")
                else:
                    how = None
                t = "%s moved %s -> %s" % (name, short(old), short(new)) + (" (%s)" % how if how else "")
                out.append(ev("branch.moved:%s:%s" % (name, new), at, "branch.moved", ref, t, data))

    # tags
    pt, ct = prev.get("tags"), cur.get("tags")
    if pt is not None and ct is not None:
        for name in sorted(set(ct) - set(pt)):
            out.append(ev("tag.created:%s" % name, at, "tag.created", {"tag": name},
                          "Tag %s created at %s" % (name, short(ct[name])), {"sha": ct[name]}))

    # releases
    pr2, cr2 = prev.get("releases"), cur.get("releases")
    if pr2 is not None and cr2 is not None:
        for tag in sorted(set(cr2) - set(pr2)):
            r = cr2[tag]
            out.append(ev("release.published:%s" % tag, r.get("publishedAt") or at, "release.published",
                          {"tag": tag}, "Release %s published%s" % (tag, " (prerelease)" if r.get("prerelease")
                                                                     else ""), dict(r)))

    # local copies
    pl, cl = prev.get("local") or {}, cur.get("local") or {}
    for name in sorted(cl):
        c, p = cl[name], pl.get(name)
        if not p or not c.get("sha"):
            continue
        ref = {"local": name}
        if p.get("sha") and c["sha"] != p["sha"]:
            d = describe("local", p["sha"], c["sha"]) or {}
            data = {"from": p["sha"], "to": c["sha"], "branch": c.get("branch"), "fromBranch": p.get("branch")}
            data.update(d)
            if d.get("forward") and (d.get("notOnTesting") or 0) > 0:
                n = d.get("count") or d.get("notOnTesting")
                t = "%s: %s on %s" % (name, plural(n, "new commit"), c.get("branch") or "detached HEAD")
                if d.get("subject"):
                    t += ": " + d["subject"]
                out.append(ev("local.commit:%s:%s" % (name, c["sha"]), at, "local.commit", ref, t, data))
            else:
                if c.get("branch") != p.get("branch"):
                    t = "%s: switched to %s (%s)" % (name, c.get("branch") or "detached HEAD", short(c["sha"]))
                elif d.get("forward"):
                    t = "%s: %s moved %s -> %s (+%s)" % (name, c.get("branch") or "HEAD", short(p["sha"]),
                                                         short(c["sha"]), plural(d.get("count") or 0, "commit"))
                else:
                    t = "%s: %s moved %s -> %s" % (name, c.get("branch") or "HEAD", short(p["sha"]),
                                                   short(c["sha"]))
                out.append(ev("local.moved:%s:%s:%s" % (name, c["sha"], c.get("branch")), at, "local.moved", ref, t,
                              data))
        elif c.get("branch") != p.get("branch") and p.get("sha"):
            out.append(ev("local.moved:%s:%s:%s" % (name, c["sha"], c.get("branch")), at, "local.moved", ref,
                          "%s: switched to %s (%s)" % (name, c.get("branch") or "detached HEAD", short(c["sha"])),
                          {"from": p["sha"], "to": c["sha"], "branch": c.get("branch"),
                           "fromBranch": p.get("branch")}))
        pd, cd = p.get("dirty"), c.get("dirty")
        if pd is not None and cd is not None and (pd > 0) != (cd > 0):
            t = ("%s: %s" % (name, plural(cd, "uncommitted change"))) if cd else ("%s: clean" % name)
            out.append(ev("local.dirty:%s:%d:%s" % (name, 1 if cd else 0, prev.get("at")), at, "local.dirty", ref, t,
                          {"dirty": cd, "was": pd}))

    # poll errors (once per new error text)
    if cur.get("pollError") and cur.get("pollError") != prev.get("pollError"):
        out.append(ev("poll.error:%s" % at, at, "poll.error", {}, "Poll failed: %s" % cur["pollError"],
                      {"error": cur["pollError"]}))
    return out


def merge_snapshot(prev, cur):
    """What to save as state-prev.json: unknown (None) sections keep the previous poll's values."""
    res = dict(cur)
    for k in ("branches", "tags", "releases", "pulls", "runs"):
        if res.get(k) is None and prev:
            res[k] = prev.get(k)
    loc = dict((prev or {}).get("local") or {}) if prev else {}
    for name, v in (cur.get("local") or {}).items():
        if v.get("sha") or name not in loc:
            loc[name] = v
    res["local"] = loc
    return res


class History:
    """history.jsonl: append-only, one event per line. Event ids make appends idempotent across restarts."""

    def __init__(self, path):
        self.path = path
        self.events = []
        self.ids = set()
        self.lock = threading.Lock()
        self._load()

    def _load(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        e = json.loads(line)
                    except ValueError:
                        continue  # a line cut by a crash
                    if isinstance(e, dict):
                        self.events.append(e)
                        if e.get("id"):
                            self.ids.add(e["id"])
        except OSError:
            pass

    def append(self, events):
        with self.lock:
            fresh = [e for e in events if e.get("id") not in self.ids]
            if not fresh:
                return []
            os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
            needs_nl = False
            try:
                with open(self.path, "rb") as f:
                    f.seek(0, os.SEEK_END)
                    if f.tell() > 0:
                        f.seek(-1, os.SEEK_END)
                        needs_nl = f.read(1) != b"\n"
            except OSError:
                pass
            with open(self.path, "a", encoding="utf-8", newline="\n") as f:
                if needs_nl:
                    f.write("\n")
                for e in fresh:
                    f.write(dumps(e) + "\n")
                f.flush()
                try:
                    os.fsync(f.fileno())
                except OSError:
                    pass
            for e in fresh:
                self.ids.add(e["id"])
                self.events.append(e)
            return fresh

    def query(self, since=None, limit=500):
        sd = parse_time(since) if since else None
        with self.lock:
            idx = list(enumerate(self.events))
        if sd:
            idx = [(i, e) for i, e in idx if (parse_time(e.get("at")) or sd) >= sd]
        idx.sort(key=lambda ie: (parse_time(ie[1].get("at")) or datetime.datetime.min.replace(tzinfo=UTC), ie[0]),
                 reverse=True)
        return [e for _, e in idx[:max(0, limit)]]


# ----------------------------------------------------------------------------------------------------------------
# the manager: one poll at a time, state published as ready-made JSON bytes

def default_data_dir():
    return os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "github-manager")


def load_config(data_dir):
    path = os.path.join(data_dir, "github-manager.json")
    cfg = read_json(path)
    if not isinstance(cfg, dict):
        cfg = None
    if cfg is None:
        cfg = {"repo": DEFAULT_REPO, "port": DEFAULT_PORT,
               "quarkDir": os.path.join(os.path.expanduser("~"), "Desktop", "Quark")}
        try:
            write_atomic(path, dumps(cfg, pretty=True) + "\n")
        except OSError:
            pass
    cfg.setdefault("repo", DEFAULT_REPO)
    cfg.setdefault("port", DEFAULT_PORT)
    cfg.setdefault("quarkDir", os.path.join(os.path.expanduser("~"), "Desktop", "Quark"))
    return cfg


class Manager:
    def __init__(self, cfg, data_dir, static_dir, proc=None, quiet=False):
        self.cfg = cfg
        self.repo = cfg["repo"]
        self.data = data_dir
        self.static = static_dir
        self.quiet = quiet
        self.proc = proc or Proc()
        self.gh = GitHub(self.repo, self.proc)
        self.git = Git(self.proc)
        self.mirror = Mirror(os.path.join(data_dir, "mirror.git"), self.repo, self.git, cfg.get("originUrl"))
        self.history = History(os.path.join(data_dir, "history.jsonl"))
        self.poll_lock = threading.Lock()
        self.lock = threading.Lock()
        self.log_locks = {}
        self.wake = threading.Event()
        self.stop = threading.Event()
        self.last_manual = 0.0
        self.last_rate_check = 0.0
        self.last_discover = 0.0
        self.locals_cfg = []
        self.backlog = False
        self.next_at = None
        self.interval = SLOW_SEC
        self.last_ok = None
        self.last_error = None
        # GitHub data, kept in memory (last good) and partly on disk
        self.meta = read_json(self._p("cache", "meta.json"), {}) or {}
        self.default_branch = self.meta.get("defaultBranch")
        self.runs = {}          # id -> {"summary", "jobs", "jobsAttempt", "jobsDone"}
        self.pr_details = read_json(self._p("cache", "pulls.json"), {}) or {}
        self.pulls_open = None
        self.pulls_closed = None
        self.releases = None
        self.refs = None        # (heads, tags, local) from the mirror
        self.api_branches = None
        self.local_cache = {}   # (path, oid, testing, prod) -> derived fields
        self.saved_logs = set()
        self.log_retry = {}     # (run, job) -> no retry before this time; memory only, so a restart retries at once
        self.last_log_error = None
        self._load_runs()
        self._scan_logs()
        self.prev = read_json(self._p("state-prev.json"))
        cached = read_json(self._p("cache", "github.json"))
        self.state = cached if isinstance(cached, dict) else self._empty_state()
        self.state["local"] = []
        self.summary_obj = self.summary(self.state)
        self.rev = 0            # bumped whenever the served state changes (each poll, a log saved on request)
        self.bodies = collections.OrderedDict()  # (etag, gzip) -> bytes: each version serialized / zipped once

    def _p(self, *parts):
        return os.path.join(self.data, *parts)

    def say(self, msg):
        if not self.quiet:
            print("[%s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)

    def _empty_state(self):
        return {"generatedAt": iso(now_dt()), "repo": self.repo, "defaultBranch": self.default_branch,
                "poll": {"lastOk": None, "lastError": None, "nextAt": None, "intervalSec": SLOW_SEC,
                         "rateRemaining": None},
                "production": None, "testing": None, "branches": [], "pulls": [], "runs": [], "local": [],
                "running": [], "releases": []}

    # -- persistence -------------------------------------------------------------------------------------------

    def _load_runs(self):
        d = self._p("cache", "runs")
        try:
            names = os.listdir(d)
        except OSError:
            return
        for n in names:
            if n.endswith(".json"):
                r = read_json(os.path.join(d, n))
                if isinstance(r, dict) and isinstance(r.get("summary"), dict) and r["summary"].get("id"):
                    self.runs[int(r["summary"]["id"])] = r

    def _save_run(self, rid):
        r = self.runs.get(rid)
        if r and r["summary"].get("status") == "completed":
            write_atomic(self._p("cache", "runs", "%d.json" % rid), dumps(r))

    def _scan_logs(self):
        d = self._p("logs")
        try:
            for run_dir in os.scandir(d):
                if run_dir.is_dir():
                    for f in os.scandir(run_dir.path):
                        if f.name.endswith(".txt"):
                            self.saved_logs.add((run_dir.name, f.name[:-4]))
        except OSError:
            pass

    def log_path(self, run_id, job_id):
        return self._p("logs", str(int(run_id)), "%d.txt" % int(job_id))

    # -- GitHub ------------------------------------------------------------------------------------------------

    def _merge_run(self, raw):
        s = run_summary(raw)
        rid = int(s["id"])
        c = self.runs.get(rid)
        if c is None:
            self.runs[rid] = {"summary": s, "jobs": [], "jobsAttempt": None, "jobsDone": False}
            self._save_run(rid)
            return
        old = c["summary"]
        if old.get("status") == "completed" and c.get("jobsDone") and old.get("attempt") == s["attempt"] and \
                s["status"] == "completed":
            return  # completed runs are immutable
        if old.get("attempt") != s["attempt"] or old.get("status") != s["status"]:
            c["jobsDone"] = False
        c["summary"] = s
        self._save_run(rid)

    def fetch_runs(self, errors):
        repo = self.repo
        if not self.meta.get("backfilled"):
            items, _ = self.gh.get_pages("repos/%s/actions/runs?per_page=100" % repo, max_pages=100,
                                         item_key="workflow_runs")
            for r in items:
                self._merge_run(r)
            self.meta["backfilled"] = True
            self._save_meta()
        else:
            data, _, changed = self.gh.get("repos/%s/actions/runs?per_page=30" % repo)
            if changed:
                for r in (data or {}).get("workflow_runs", []):
                    self._merge_run(r)
        seen_active = set()
        for st in ("in_progress", "queued", "waiting"):
            try:
                data, _, _ = self.gh.get("repos/%s/actions/runs?status=%s&per_page=100" % (repo, st))
                for r in (data or {}).get("workflow_runs", []):
                    self._merge_run(r)
                    seen_active.add(int(r["id"]))
            except GhError as e:
                errors.append(str(e))
        # runs we still think are active but no list showed as active: they finished (or need a direct look)
        for rid, c in list(self.runs.items()):
            if is_active(c["summary"]) and rid not in seen_active:
                try:
                    data, _, _ = self.gh.get("repos/%s/actions/runs/%d" % (repo, rid))
                    if data:
                        self._merge_run(data)
                except GhError as e:
                    if e.status == 404:
                        c["summary"]["status"] = "completed"
                        c["summary"]["conclusion"] = c["summary"].get("conclusion") or "cancelled"
                    else:
                        errors.append(str(e))

    def fetch_jobs(self, errors):
        need = [rid for rid, c in self.runs.items() if not c.get("jobsDone")]
        need.sort(key=lambda rid: (not is_active(self.runs[rid]["summary"]), -rid))
        budget = JOBS_BUDGET
        left = 0
        for rid in need:
            c = self.runs[rid]
            active = is_active(c["summary"])
            if not active:
                if budget <= 0:
                    left += 1
                    continue
                budget -= 1
            try:
                data, _, _ = self.gh.get("repos/%s/actions/runs/%d/jobs?per_page=100" % (self.repo, rid))
            except GhError as e:
                if e.status in (404, 410):
                    c["jobsDone"] = c["summary"].get("status") == "completed"
                    self._save_run(rid)
                else:
                    errors.append(str(e))
                continue
            gone = {j["id"]: j.get("logGone") for j in c.get("jobs") or [] if j.get("logGone")}
            jobs = [job_info(j) for j in (data or {}).get("jobs", [])]
            for j in jobs:
                if gone.get(j["id"]):
                    j["logGone"] = True
            c["jobs"] = jobs
            c["jobsAttempt"] = c["summary"].get("attempt")
            if c["summary"].get("status") == "completed":
                c["jobsDone"] = True
                self._save_run(rid)
        return left

    def fetch_logs(self, warnings):
        todo = []
        for rid, c in self.runs.items():
            if c["summary"].get("status") != "completed" or not c.get("jobsDone"):
                continue
            for j in c.get("jobs") or []:
                if j.get("conclusion") in FAILED_CONCLUSIONS and not j.get("logGone") and \
                        (str(rid), str(j["id"])) not in self.saved_logs:
                    todo.append((rid, j))
        now = time.time()
        todo = [t for t in todo if self.log_retry.get((t[0], t[1]["id"]), 0) <= now]
        todo.sort(key=lambda t: -t[0])
        failures = 0
        for rid, j in todo[:LOGS_BUDGET]:
            res = self.download_log(rid, j["id"])
            if res == "error":
                # GitHub (or the storage it redirects to) refused: retry this one in 30 min, not every poll
                self.log_retry[(rid, j["id"])] = now + LOG_RETRY_SEC
                failures += 1
                if failures >= 2:
                    break
        if failures:
            warnings.append("%s could not be downloaded (retrying in %d min, or on restart): %s" % (
                plural(failures, "failed-job log"), LOG_RETRY_SEC // 60, self.last_log_error or "unknown error"))
        return max(0, len(todo) - LOGS_BUDGET) if not failures else 0

    def download_log(self, run_id, job_id):
        """-> "ok" | "gone" | "error". Saved logs are kept forever."""
        key = (str(int(run_id)), str(int(job_id)))
        with self.lock:
            lk = self.log_locks.setdefault(key, threading.Lock())
        with lk:
            if key in self.saved_logs:
                return "ok"
            try:
                body = self.gh.raw("repos/%s/actions/jobs/%d/logs" % (self.repo, int(job_id)))
            except GhError as e:
                if e.status in (404, 410):
                    self._mark_log_gone(int(run_id), int(job_id))
                    return "gone"
                self.last_log_error = str(e)
                return "error"
            write_atomic(self.log_path(run_id, job_id), body)
            self.saved_logs.add(key)
            return "ok"

    def _mark_log_gone(self, rid, jid):
        c = self.runs.get(rid)
        if not c:
            return
        for j in c.get("jobs") or []:
            if j.get("id") == jid:
                j["logGone"] = True
        self._save_run(rid)

    def fetch_pulls(self, errors):
        repo = self.repo
        self.pulls_open, _ = self.gh.get_pages("repos/%s/pulls?state=open&per_page=100" % repo, max_pages=10)
        data, _, _ = self.gh.get("repos/%s/pulls?state=closed&sort=updated&direction=desc&per_page=30" % repo)
        self.pulls_closed = data or []
        changed = False
        for p in (self.pulls_open or []) + self.pulls_closed:
            n = str(p.get("number"))
            d = self.pr_details.get(n)
            if d and d.get("updatedAt") == p.get("updated_at"):
                continue
            try:
                full, _, _ = self.gh.get("repos/%s/pulls/%s" % (repo, n))
            except GhError as e:
                errors.append(str(e))
                continue
            self.pr_details[n] = {"updatedAt": full.get("updated_at"), "additions": full.get("additions"),
                                  "deletions": full.get("deletions"), "changedFiles": full.get("changed_files"),
                                  "merged": bool(full.get("merged"))}
            changed = True
        if changed:
            write_atomic(self._p("cache", "pulls.json"), dumps(self.pr_details))

    def fetch_releases(self):
        items, _ = self.gh.get_pages("repos/%s/releases?per_page=100" % self.repo, max_pages=5)
        self.releases = [r for r in items if not r.get("draft")]

    def fetch_repo(self):
        data, _, _ = self.gh.get("repos/%s" % self.repo)
        db = (data or {}).get("default_branch")
        if db and db != self.default_branch:
            self.default_branch = db
            self.meta["defaultBranch"] = db
            self._save_meta()

    def _save_meta(self):
        write_atomic(self._p("cache", "meta.json"), dumps(self.meta))

    # -- local copies ------------------------------------------------------------------------------------------

    def read_local(self, e, heads, local_refs, testing_sha, prod_sha, errors):
        out = {"name": e["name"], "path": e["path"], "branch": None, "sha": None, "subject": None, "date": None,
               "dirty": None, "dirtyFiles": [], "upstream": None, "aheadOfTesting": None, "behindTesting": None,
               "aheadOfProduction": None, "commits": [], "version": None, "worktreeOf": e.get("worktreeOf"),
               "error": None}
        code, txt, err = self.git.run(["-C", e["path"], "status", "--porcelain=v2", "--branch", "-z"], timeout=120)
        if code != 0:
            out["error"] = "git status failed: %s" % ((err or "").splitlines()[-1] if err else code)
            return out
        st = parse_status_v2(txt)
        out.update({"branch": st["branch"], "sha": st["oid"], "upstream": st["upstream"], "dirty": st["dirty"],
                    "dirtyFiles": st["dirtyFiles"]})
        oid = st["oid"]
        if not oid:
            return out
        try:
            if local_refs.get(e["key"]) != oid:
                self.mirror.fetch_local(e["path"], e["key"])
        except RuntimeError as ex:
            out["error"] = "could not copy its commits into the mirror: %s" % ex
            return out
        ck = (oid, testing_sha, prod_sha)
        d = self.local_cache.get(ck)
        if d is None:
            d = {}
            try:
                info = parse_commits(self.mirror.log([oid], 1), self.repo)
                if info:
                    d["subject"], d["date"] = info[0]["subject"], info[0]["date"]
                local_only = self.mirror.not_on_github(oid)
                gh_url = lambda s: s not in local_only
                if testing_sha:
                    d["aheadOfTesting"], d["behindTesting"] = self.mirror.ahead_behind(testing_sha, oid)
                    d["commits"] = parse_commits(self.mirror.log([oid, "^" + testing_sha], MAX_LOCAL_COMMITS),
                                                 self.repo, gh_url)
                if prod_sha:
                    d["aheadOfProduction"] = self.mirror.count(oid, "^" + prod_sha)
                d["version"] = self.mirror.show_file(oid, "VERSION")
            except (RuntimeError, ValueError) as ex:
                out["error"] = "mirror: %s" % ex
            if not out["error"]:
                self.local_cache[ck] = d
        out.update(d)
        # VERSION edited but not committed: the working file is what the copy will build
        if any(f.split(" ", 1)[-1] == "VERSION" for f in st["dirtyFiles"]):
            try:
                with open(os.path.join(e["path"], "VERSION"), "r", encoding="utf-8") as f:
                    out["version"] = f.read().strip() or out["version"]
            except OSError:
                pass
        return out

    def describe(self, kind, old, new):
        try:
            if not (self.mirror.has_commit(old) and self.mirror.has_commit(new)):
                return None
            fwd = self.mirror.is_ancestor(old, new)
            d = {"forward": fwd}
            if fwd:
                d["count"] = self.mirror.count(new, "^" + old)
            if kind == "local":
                heads = (self.refs or ({}, {}, {}))[0]
                t = heads.get("testing", {}).get("sha")
                revs = [new, "^" + old] + (["^" + t] if t else [])
                d["notOnTesting"] = self.mirror.count(*revs)
                info = parse_commits(self.mirror.log([new], 1))
                if info:
                    d["subject"] = info[0]["subject"]
            return d
        except (RuntimeError, ValueError):
            return None

    # -- one poll ----------------------------------------------------------------------------------------------

    def poll(self, manual=False):
        with self.poll_lock:
            t0 = time.time()
            calls0, changed0 = self.gh.calls, self.gh.changed
            self.gh.fatal = None
            errors = []
            warnings = []  # shown in poll.warnings, not counted as a failed poll
            gh_ok = True
            at = iso(now_dt())

            def step(fn, *a):
                nonlocal gh_ok
                try:
                    return fn(*a)
                except GhError as e:
                    gh_ok = False
                    errors.append(str(e))
                except (RuntimeError, OSError, ValueError, KeyError, TypeError) as e:
                    errors.append("%s: %s" % (fn.__name__, e))
                return None

            if time.time() - self.last_rate_check > RATE_CHECK_SEC:
                step(self.gh.check_rate)
                self.last_rate_check = time.time()
            rate = self.gh.rate
            jobs_left = logs_left = 0
            if rate.get("remaining") == 0 and (rate.get("reset") or 0) > time.time():
                gh_ok = False
                errors.append("GitHub rate limit used up until %s" % iso(
                    datetime.datetime.fromtimestamp(rate["reset"], UTC)))
            else:
                step(self.fetch_repo)
                step(self.fetch_releases)
                step(self.fetch_pulls, errors)
                step(self.fetch_runs, errors)
                jobs_left = step(self.fetch_jobs, errors) or 0
                logs_left = step(self.fetch_logs, warnings) or 0
            # the mirror
            mirror_ok = False
            try:
                self.mirror.ensure()
                self.mirror.fetch_origin()
                mirror_ok = True
            except RuntimeError as e:
                errors.append("mirror fetch: %s" % e)
            refs_from_api = False
            try:
                self.refs = self.mirror.refs()
            except RuntimeError as e:
                errors.append("mirror refs: %s" % e)
            if not mirror_ok and (self.refs is None or not self.refs[0]):
                # no mirror yet: fall back to the branch list from the API (names and shas only)
                try:
                    items, _ = self.gh.get_pages("repos/%s/branches?per_page=100" % self.repo, max_pages=10)
                    self.refs = ({b["name"]: {"sha": b["commit"]["sha"], "subject": None, "author": None,
                                              "date": None} for b in items}, {}, {})
                    refs_from_api = True  # no tags known: the snapshot must not record "no tags"
                except GhError as e:
                    errors.append(str(e))
            # local copies
            if manual or time.time() - self.last_discover > DISCOVER_SEC:
                try:
                    self.locals_cfg = discover_locals(self.cfg, self.git, self.repo)
                    self.last_discover = time.time()
                except (RuntimeError, OSError) as e:
                    errors.append("local copies: %s" % e)
            heads, tags, local_refs = self.refs or ({}, {}, {})
            testing_sha = (heads.get("testing") or {}).get("sha")
            prod_tag = self._production_tag(tags)
            prod_sha = (tags.get(prod_tag) or {}).get("sha") if prod_tag else None
            if testing_sha and not self.mirror.has_commit(testing_sha):
                testing_sha_m = None
            else:
                testing_sha_m = testing_sha
            local = []
            for e in self.locals_cfg:
                try:
                    local.append(self.read_local(e, heads, local_refs, testing_sha_m, prod_sha, errors))
                except (RuntimeError, OSError, ValueError) as ex:
                    local.append({"name": e["name"], "path": e["path"], "error": str(ex),
                                  "worktreeOf": e.get("worktreeOf")})
            errors = list(dict.fromkeys(errors))  # the same failure from several steps once
            # state, events, files
            state = self.build_state(at, heads, tags, prod_tag, local, errors)
            snap = self.snapshot(at, state, heads if self.refs else None,
                                 tags if self.refs and not refs_from_api else None)
            snap["pollError"] = "; ".join(errors) if errors else None
            events = diff_events(self.prev, snap, self.describe)
            fresh = self.history.append(events)
            self.prev = merge_snapshot(self.prev, snap)
            write_atomic(self._p("state-prev.json"), dumps(self.prev))
            self.mirror.trim_memo()
            if len(self.local_cache) > 500:
                self.local_cache.clear()
            # next poll
            any_running = bool(state["running"])
            self.backlog = (jobs_left + logs_left) > 0 and gh_ok
            self.interval = FAST_SEC if any_running else SLOW_SEC
            wait = min(self.interval, BACKLOG_SEC) if self.backlog else self.interval
            self.next_at = time.time() + wait
            if not errors:
                self.last_ok = at
            self.last_error = "; ".join(errors) if errors else None
            state["poll"] = {"lastOk": self.last_ok, "lastError": self.last_error,
                             "nextAt": iso(datetime.datetime.fromtimestamp(self.next_at, UTC)),
                             "intervalSec": self.interval, "rateRemaining": self.gh.rate.get("remaining"),
                             "lastAt": at, "durationSec": round(time.time() - t0, 2),
                             "ghCalls": self.gh.calls - calls0, "ghChanged": self.gh.changed - changed0,
                             "backlog": {"jobs": jobs_left, "logs": logs_left}, "warnings": warnings}
            state["generatedAt"] = iso(now_dt())
            self.publish(state)
            self.say("poll %s in %.1f s: %d gh calls (%d changed), rate %s, %d new events%s" % (
                "ok" if not errors else "with errors", time.time() - t0, self.gh.calls - calls0,
                self.gh.changed - changed0, self.gh.rate.get("remaining"), len(fresh),
                "".join(" - " + x for x in errors + warnings)))
            return state

    def _production_tag(self, tags):
        best = None
        names = list(tags) or [r.get("tag_name") for r in (self.releases or []) if not r.get("prerelease")]
        for t in names:
            k = version_key(t)
            if k and (best is None or k > version_key(best)):
                best = t
        return best

    def publish(self, state):
        github = dict(state)
        github.pop("local", None)
        summ = self.summary(state)
        with self.lock:
            self.state = state
            self.summary_obj = summ
            self.rev += 1
            self.bodies.clear()
        try:
            write_atomic(self._p("cache", "github.json"), dumps(github))
        except OSError as e:
            self.say("could not write cache/github.json: %s" % e)

    def republish(self):
        """Rebuild the served JSON after a change outside a poll (a log downloaded on request)."""
        with self.lock:
            state = self.state
        for r in state.get("runs") or []:
            for j in r.get("jobs") or []:
                j["hasLog"] = (str(r["id"]), str(j["id"])) in self.saved_logs
        with self.lock:
            self.rev += 1
            self.bodies.clear()

    # -- served bodies: one serialization (and one gzip) per version, ETags for 304s ------------------------------

    def version_tag(self):
        with self.lock:
            return "%s.%d" % (re.sub(r"[^0-9A-Za-z]", "", self.state.get("generatedAt") or "0"), self.rev)

    def state_view(self, runs=None):
        """The state, with only the newest `runs` runs when asked (running runs are always in "running")."""
        with self.lock:
            st = self.state
        if runs is None or runs >= len(st.get("runs") or []):
            return st
        v = dict(st)
        v["runs"] = (st.get("runs") or [])[:max(0, runs)]
        v["runsTotal"] = len(st.get("runs") or [])
        return v

    def run_json(self, rid):
        with self.lock:
            st = self.state
        for r in st.get("runs") or []:
            if r.get("id") == rid:
                return r
        return self.run_view(rid) if rid in self.runs else None

    def body(self, etag, gz, make):
        """Bytes for this ETag (gzip-compressed when gz), built at most once per version."""
        key = (etag, gz)
        with self.lock:
            b = self.bodies.get(key)
            if b is not None:
                self.bodies.move_to_end(key)
                return b
            raw = self.bodies.get((etag, False))
        if raw is None:
            raw = make()
            if isinstance(raw, str):
                raw = raw.encode("utf-8")
        b = gzip.compress(raw, 6) if gz else raw
        with self.lock:
            self.bodies[(etag, False)] = raw
            self.bodies[key] = b
            while len(self.bodies) > 24:
                self.bodies.popitem(last=False)
        return b

    # -- building the API objects ------------------------------------------------------------------------------

    def run_view(self, rid):
        c = self.runs[rid]
        s = dict(c["summary"])
        jobs = []
        for j in c.get("jobs") or []:
            jj = dict(j)
            jj["hasLog"] = (str(rid), str(j["id"])) in self.saved_logs
            jj.pop("logGone", None)
            if j.get("logGone"):
                jj["logGone"] = True
            jobs.append(jj)
        s["jobs"] = jobs
        return s

    def build_state(self, at, heads, tags, prod_tag, local, errors):
        repo = self.repo
        rel_by_tag = {r.get("tag_name"): r for r in (self.releases or [])}
        releases = [{"tagName": r.get("tag_name"), "isPrerelease": bool(r.get("prerelease")),
                     "publishedAt": r.get("published_at"), "url": r.get("html_url")} for r in (self.releases or [])]
        releases.sort(key=lambda r: r["publishedAt"] or "", reverse=True)
        commit_url = lambda sha: "https://github.com/%s/commit/%s" % (repo, sha) if sha else None

        # runs
        run_ids = sorted(self.runs, reverse=True)
        runs = [self.run_view(rid) for rid in run_ids]
        summaries = [self.runs[rid]["summary"] for rid in run_ids]
        running = [dict(s) for s in summaries if is_active(s)]
        by_sha = {}
        last_by_branch = {}
        for s in summaries:  # newest first
            by_sha.setdefault(s.get("sha"), []).append(s)
            last_by_branch.setdefault(s.get("branch"), s)

        # production / testing
        production = None
        prod_sha = None
        if prod_tag:
            t = tags.get(prod_tag) or {}
            prod_sha = t.get("sha")
            r = rel_by_tag.get(prod_tag) or {}
            production = {"tag": prod_tag, "sha": prod_sha, "version": prod_tag[1:],
                          "publishedAt": r.get("published_at") or t.get("tagDate") or t.get("date"),
                          "url": r.get("html_url") or "https://github.com/%s/releases/tag/%s" % (repo, prod_tag)}
        testing = None
        th = heads.get("testing")
        if th:
            pre = None
            pres = [r for r in (self.releases or []) if r.get("prerelease") and
                    (r.get("tag_name") or "").startswith("test-")]
            pres.sort(key=lambda r: r.get("published_at") or "", reverse=True)
            if pres:
                p = pres[0]
                pre = {"tag": p.get("tag_name"), "publishedAt": p.get("published_at"), "url": p.get("html_url"),
                       "sha": (tags.get(p.get("tag_name")) or {}).get("sha")}
            testing = {"sha": th["sha"], "version": None, "date": th.get("date"), "subject": th.get("subject"),
                       "prerelease": pre, "aheadOfProduction": None, "commitsSinceProduction": []}
            try:
                if self.mirror.has_commit(th["sha"]):
                    testing["version"] = self.mirror.show_file(th["sha"], "VERSION")
                    revs = [th["sha"]] + (["^" + prod_sha] if prod_sha else [])
                    testing["aheadOfProduction"] = self.mirror.count(*revs)
                    testing["commitsSinceProduction"] = parse_commits(
                        self.mirror.log(revs, MAX_TESTING_COMMITS, first_parent=True), repo)
            except (RuntimeError, ValueError) as e:
                errors.append("testing: %s" % e)

        # pulls
        pulls = []
        pr_by_head = {}
        for p in (self.pulls_open or []) + (self.pulls_closed or []):
            n = p.get("number")
            d = self.pr_details.get(str(n)) or {}
            head = (p.get("head") or {})
            head_sha = head.get("sha")
            cruns = by_sha.get(head_sha, [])
            item = {"number": n, "title": p.get("title"), "state": p.get("state"),
                    "merged": bool(p.get("merged_at")) or bool(d.get("merged")), "mergedAt": p.get("merged_at"),
                    "closedAt": p.get("closed_at"), "draft": bool(p.get("draft")), "head": head.get("ref"),
                    "headSha": head_sha, "base": (p.get("base") or {}).get("ref"),
                    "author": (p.get("user") or {}).get("login"), "createdAt": p.get("created_at"),
                    "updatedAt": p.get("updated_at"), "url": p.get("html_url"),
                    "checks": {"state": checks_state(cruns), "runs": [dict(r) for r in cruns]},
                    "additions": d.get("additions"), "deletions": d.get("deletions"),
                    "changedFiles": d.get("changedFiles")}
            pulls.append(item)
            hr = head.get("ref")
            if (head.get("repo") or {}).get("full_name", repo) == repo and hr:
                cur = pr_by_head.get(hr)
                if cur is None or (item["state"] == "open" and cur["state"] != "open") or \
                        (item["state"] == cur["state"] and n > cur["number"]):
                    pr_by_head[hr] = item

        # branches
        branches = []
        testing_sha = th["sha"] if th else None
        t_in_mirror = bool(testing_sha) and self.mirror.has_commit(testing_sha)
        for name, h in heads.items():
            ahead = behind = None
            if t_in_mirror and h.get("sha"):
                try:
                    ahead, behind = self.mirror.ahead_behind(testing_sha, h["sha"])
                except (RuntimeError, ValueError):
                    pass
            pr = pr_by_head.get(name)
            lr = last_by_branch.get(name)
            branches.append({"name": name, "sha": h.get("sha"), "date": h.get("date"), "subject": h.get("subject"),
                             "author": h.get("author"), "kind": branch_kind(name, self.default_branch),
                             "ahead": ahead, "behind": behind, "pr": pr["number"] if pr else None,
                             "lastRun": dict(lr) if lr else None,
                             "url": "https://github.com/%s/tree/%s" % (repo, urllib.parse.quote(name, safe="/"))})
        order = {"production": 0, "testing": 1}
        branches.sort(key=lambda b: (b["date"] or ""), reverse=True)
        branches.sort(key=lambda b: order.get(b["kind"], 2))

        return {"generatedAt": at, "repo": repo, "defaultBranch": self.default_branch, "poll": {},
                "production": production, "testing": testing, "branches": branches, "pulls": pulls, "runs": runs,
                "local": local, "running": running, "releases": releases}

    def snapshot(self, at, state, heads, tags):
        runs = None
        if self.runs:
            runs = {}
            for rid, c in self.runs.items():
                s = c["summary"]
                runs[str(rid)] = {"status": s.get("status"), "conclusion": s.get("conclusion"),
                                  "attempt": s.get("attempt"), "workflow": s.get("workflow"),
                                  "branch": s.get("branch"), "event": s.get("event"), "sha": s.get("sha"),
                                  "url": s.get("url"), "createdAt": s.get("createdAt"),
                                  "startedAt": s.get("startedAt"), "updatedAt": s.get("updatedAt"),
                                  "durationSec": s.get("durationSec") if s.get("status") == "completed" else None,
                                  "failedJobs": [j["name"] for j in c.get("jobs") or []
                                                 if j.get("conclusion") in FAILED_CONCLUSIONS]}
        pulls = None
        if self.pulls_open is not None:
            pulls = {str(p["number"]): {k: p.get(k) for k in ("state", "merged", "headSha", "title", "head", "base",
                                                               "createdAt", "mergedAt", "closedAt", "updatedAt",
                                                               "url", "author")}
                     for p in state["pulls"]}
        releases = None
        if self.releases is not None:
            releases = {r["tagName"]: {"prerelease": r["isPrerelease"], "publishedAt": r["publishedAt"],
                                       "url": r["url"]} for r in state["releases"]}
        return {"at": at, "repo": self.repo,
                "branches": {n: h.get("sha") for n, h in heads.items()} if heads else None,  # none = unknown
                "tags": {n: t.get("sha") for n, t in tags.items()} if tags is not None else None,
                "releases": releases, "pulls": pulls, "runs": runs,
                "local": {l["name"]: {"sha": l.get("sha"), "branch": l.get("branch"), "dirty": l.get("dirty")}
                          for l in state["local"]}}

    @staticmethod
    def summary(state):
        failed = None
        for r in state.get("runs") or []:
            if r.get("conclusion") in FAILED_CONCLUSIONS:
                failed = {k: v for k, v in r.items() if k != "jobs"}
                break
        t = state.get("testing") or {}
        p = state.get("production") or {}
        return {"generatedAt": state.get("generatedAt"), "repo": state.get("repo"), "poll": state.get("poll"),
                "testing": {"sha": t.get("sha"), "version": t.get("version"), "prerelease": t.get("prerelease")},
                "production": {"tag": p.get("tag"), "version": p.get("version")},
                "openPulls": [{"number": x.get("number"), "title": x.get("title"), "head": x.get("head"),
                               "checks": {"state": (x.get("checks") or {}).get("state")}, "url": x.get("url")}
                              for x in state.get("pulls") or [] if x.get("state") == "open"],
                "running": state.get("running") or [], "lastFailed": failed,
                "lastTesting": last_testing_run(state.get("runs") or []),
                "local": [{"name": l.get("name"), "branch": l.get("branch"), "aheadOfTesting": l.get("aheadOfTesting"),
                           "behindTesting": l.get("behindTesting"), "dirty": l.get("dirty")}
                          for l in state.get("local") or []]}

    # -- background thread -------------------------------------------------------------------------------------

    def loop(self):
        while not self.stop.is_set():
            try:
                self.poll()
            except Exception as e:  # never let the thread die
                self.last_error = "poll crashed: %r" % e
                self.say(self.last_error)
                self.next_at = time.time() + self.interval
            wait = max(1.0, (self.next_at or time.time() + self.interval) - time.time())
            self.wake.wait(wait)
            self.wake.clear()

    def refresh(self):
        """POST /api/refresh: poll now unless one ran in the last 10 s. -> (polled: bool)."""
        with self.lock:
            if time.time() - self.last_manual < REFRESH_MIN_SEC:
                return False
            self.last_manual = time.time()
        self.poll(manual=True)
        return True


# ----------------------------------------------------------------------------------------------------------------
# HTTP

class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "QuarkGitHubManager/1"
    protocol_version = "HTTP/1.1"
    manager = None  # set on the class by make_server

    def log_message(self, fmt, *args):
        pass

    def _host_ok(self):
        host = (self.headers.get("Host") or "").lower()
        port = self.server.server_address[1]
        return host in ("127.0.0.1:%d" % port, "localhost:%d" % port, "127.0.0.1", "localhost")

    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None, cors=False):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        extra = dict(extra or {})
        self.send_header("Cache-Control", extra.pop("Cache-Control", "no-store"))
        self.send_header("X-Content-Type-Options", "nosniff")
        vary = extra.pop("Vary", "Origin" if cors else None)
        if cors:
            o = cors_origin(self.headers.get("Origin"))
            if o:
                self.send_header("Access-Control-Allow-Origin", o)
                self.send_header("Access-Control-Expose-Headers", "ETag, X-Refresh")
        if vary:
            self.send_header("Vary", vary)
        for k, v in extra.items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, code, obj, cors=True):
        self._send(code, dumps(obj), cors=cors)

    @staticmethod
    def _int(q, name):
        try:
            v = (q.get(name) or [""])[0]
            return int(v) if v != "" else None
        except ValueError:
            return None

    def _gzip_ok(self):
        for part in (self.headers.get("Accept-Encoding") or "").lower().split(","):
            enc, _, params = part.strip().partition(";")
            if enc.strip() in ("gzip", "*") and params.replace(" ", "") not in ("q=0", "q=0.0", "q=0.00", "q=0.000"):
                return True
        return False

    def _cached(self, etag, make, code=200, extra=None):
        """JSON with a strong ETag (304 on If-None-Match, no body) and gzip when the client takes it."""
        gz = self._gzip_ok()
        tag = '"%s%s"' % (etag, "-gz" if gz else "")
        inm = self.headers.get("If-None-Match")
        if inm and code == 200 and self.command in ("GET", "HEAD"):
            mine = ('"%s"' % etag, '"%s-gz"' % etag)
            tags = [t.strip() for t in inm.split(",")]
            if "*" in tags or any((t[2:] if t.startswith("W/") else t) in mine for t in tags):
                self.send_response(304)
                self.send_header("ETag", tag)
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Vary", "Origin, Accept-Encoding")
                o = cors_origin(self.headers.get("Origin"))
                if o:
                    self.send_header("Access-Control-Allow-Origin", o)
                    self.send_header("Access-Control-Expose-Headers", "ETag, X-Refresh")
                self.end_headers()
                return
        body = self.manager.body(etag, gz, make)
        hdr = {"ETag": tag, "Cache-Control": "no-cache", "Vary": "Origin, Accept-Encoding"}
        if gz:
            hdr["Content-Encoding"] = "gzip"
        hdr.update(extra or {})
        self._send(code, body, cors=True, extra=hdr)

    def do_OPTIONS(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain")
        o = cors_origin(self.headers.get("Origin"))
        self.send_response(204)
        if o and self.path.startswith("/api/"):
            self.send_header("Access-Control-Allow-Origin", o)
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", self.headers.get("Access-Control-Request-Headers")
                             or "Content-Type")
            self.send_header("Access-Control-Max-Age", "600")
        self.send_header("Vary", "Origin")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain")
        m = self.manager
        url = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(url.query)
        path = url.path
        try:
            if path == "/api/state":
                runs = self._int(q, "runs")
                v = m.version_tag()
                return self._cached("st-%s-%s" % (v, "all" if runs is None else runs),
                                    lambda: dumps(m.state_view(runs)))
            if path == "/api/summary":
                return self._cached("su-" + m.version_tag(), lambda: dumps(m.summary_obj))
            if path == "/api/run":
                rid = self._int(q, "id")
                r = m.run_json(rid) if rid is not None else None
                if r is None:
                    return self._json(404, {"error": "unknown run"})
                return self._cached("run-%s-%d" % (m.version_tag(), rid), lambda: dumps(r))
            if path == "/api/history":
                limit = self._int(q, "limit")
                limit = 500 if limit is None else max(1, min(limit, 1000000))
                since = (q.get("since") or [None])[0]
                with m.history.lock:
                    n = len(m.history.events)
                    last = m.history.events[-1].get("id") if n else ""
                key = hashlib.sha1(("%d|%s|%s|%d" % (n, last, since, limit)).encode("utf-8")).hexdigest()[:16]
                return self._cached("h-%d-%s" % (n, key), lambda: dumps(m.history.query(since, limit)))
            if path == "/api/log":
                return self._log(q)
            if path.startswith("/api/"):
                return self._json(404, {"error": "no such endpoint"})
            return self._static(path)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain")
        n = int(self.headers.get("Content-Length") or 0)
        if n:
            self.rfile.read(min(n, 65536))
        m = self.manager
        url = urllib.parse.urlsplit(self.path)
        path = url.path
        runs = self._int(urllib.parse.parse_qs(url.query), "runs")
        if path != "/api/refresh":
            return self._json(404, {"error": "no such endpoint"})
        try:
            polled = m.refresh()
        except Exception as e:
            return self._json(500, {"error": "refresh failed: %r" % e})
        self._cached("st-%s-%s" % (m.version_tag(), "all" if runs is None else runs),
                     lambda: dumps(m.state_view(runs)),
                     extra={"X-Refresh": "polled" if polled else "skipped (one per 10 s)"})

    def _log(self, q):
        m = self.manager
        try:
            run_id = int((q.get("run") or [""])[0])
            job_id = int((q.get("job") or [""])[0])
        except ValueError:
            return self._json(400, {"error": "run and job must be numbers"})
        p = m.log_path(run_id, job_id)
        if not os.path.isfile(p):
            job = None
            c = m.runs.get(run_id)
            for j in (c or {}).get("jobs") or []:
                if j.get("id") == job_id:
                    job = j
            if job is not None and (job.get("logGone") or job.get("status") != "completed"):
                return self._send(404, "This job's log is not available (%s)." % (
                    "GitHub no longer has it" if job.get("logGone") else "the job has not finished"),
                    "text/plain; charset=utf-8", cors=True)
            res = m.download_log(run_id, job_id)
            if res != "ok":
                return self._send(404, "GitHub no longer has this log and it was never saved." if res == "gone"
                                  else "Could not download the log from GitHub (try again later): %s"
                                  % (m.last_log_error or "unknown error"),
                                  "text/plain; charset=utf-8", cors=True)
            m.republish()
        try:
            with open(p, "rb") as f:
                body = f.read()
        except OSError:
            return self._send(404, "log not found", "text/plain; charset=utf-8", cors=True)
        self._send(200, body, "text/plain; charset=utf-8", cors=True)

    def _static(self, path):
        m = self.manager
        rel = urllib.parse.unquote(path).lstrip("/") or "index.html"
        parts = [p for p in rel.replace("\\", "/").split("/") if p]
        if not parts or any(p in (".", "..") or p.startswith(".") for p in parts) or \
                parts[0].lower() in STATIC_BLOCKED or ":" in rel:
            return self._send(404, "not found", "text/plain")
        ext = os.path.splitext(parts[-1])[1].lower()
        ctype = STATIC_EXT.get(ext)
        full = os.path.join(m.static, *parts)
        base = os.path.realpath(m.static)
        if not ctype or not os.path.realpath(full).startswith(base + os.sep) or not os.path.isfile(full):
            if parts == ["index.html"]:
                return self._send(200, FALLBACK_PAGE, "text/html; charset=utf-8")
            return self._send(404, "not found", "text/plain")
        with open(full, "rb") as f:
            body = f.read()
        self._send(200, body, ctype)


FALLBACK_PAGE = """<!doctype html><meta charset="utf-8"><title>GitHub manager</title>
<body style="font:14px system-ui;margin:2em"><p>The page (index.html) is not next to server.py.
The API is running: <a href="/api/state">/api/state</a>, <a href="/api/summary">/api/summary</a>,
<a href="/api/history">/api/history</a>.</p></body>"""


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = not WIN  # on Windows SO_REUSEADDR would let a second copy share the port


def make_server(manager, port):
    handler = type("BoundHandler", (Handler,), {"manager": manager})
    return Server(("127.0.0.1", port), handler)


def already_running(port):
    import urllib.request
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/summary" % port, timeout=3) as r:
            return r.status == 200
    except Exception:
        return False


def main(argv=None):
    ap = argparse.ArgumentParser(description="Quark GitHub manager (read-only): http://127.0.0.1:%d/" % DEFAULT_PORT)
    ap.add_argument("--data", default=None, help="output folder (default %%USERPROFILE%%\\Desktop\\Quark\\github-manager)")
    ap.add_argument("--port", type=int, default=None, help="port (default: github-manager.json, else %d)" % DEFAULT_PORT)
    ap.add_argument("--open", action="store_true", help="open the page in the default browser")
    ap.add_argument("--once", action="store_true", help="poll once, write the files and exit (no server)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    data = os.path.abspath(os.path.expanduser(a.data or default_data_dir()))
    os.makedirs(data, exist_ok=True)
    cfg = load_config(data)
    port = a.port or int(cfg.get("port") or DEFAULT_PORT)
    static = os.path.dirname(os.path.abspath(__file__))
    url = "http://127.0.0.1:%d/" % port
    m = Manager(cfg, data, static, quiet=a.quiet)
    if a.once:
        m.poll(manual=True)
        return 0
    try:
        srv = make_server(m, port)
    except OSError as e:
        if e.errno in (errno.EADDRINUSE, getattr(errno, "WSAEADDRINUSE", -1), 10048) or "in use" in str(e).lower():
            if already_running(port):
                print("The GitHub manager is already running at %s" % url)
                if a.open:
                    webbrowser.open(url)
                return 0
        print("cannot listen on 127.0.0.1:%d: %s" % (port, e))
        return 1
    m.say("GitHub manager for %s at %s (data: %s)" % (m.repo, url, data))
    t = threading.Thread(target=m.loop, name="poll", daemon=True)
    t.start()
    if a.open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever(poll_interval=1.0)
    except KeyboardInterrupt:
        pass
    finally:
        m.stop.set()
        m.wake.set()
        srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

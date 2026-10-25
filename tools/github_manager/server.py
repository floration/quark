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
import difflib
import errno
import gzip
import hashlib
import http.server
import json
import logging
import logging.handlers
import os
import re
import signal
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
NOT_READ_SECTIONS = ("pulls", "releases", "runs")  # GitHub-only parts of the state (state.notRead: not read yet)

ACTIVE_STATUSES = frozenset(("queued", "in_progress", "waiting", "pending", "requested"))
FAILED_CONCLUSIONS = frozenset(("failure", "timed_out", "startup_failure"))
INFRA_BRANCHES = frozenset(("launcher", "site", "discord-bot", "gh-pages"))
PROD_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
SKIP_DIRS = frozenset(("claude documents",))  # a different repo next to the working copies
STATIC_EXT = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
              ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
              ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon", ".woff2": "font/woff2",
              ".woff": "font/woff", ".map": "application/json"}
# data folders never served (the page may run from the output folder itself)
STATIC_BLOCKED = frozenset(("cache", "logs", "mirror.git", "test", "__pycache__", "editor", "claude-docs"))
CORS_ORIGIN_RE = re.compile(r"^http://(127\.0\.0\.1|localhost):(876[5-8]|8775)$")  # the local tools + tests editor
WIN = os.name == "nt"
NO_WINDOW = 0x08000000 if WIN else 0  # CREATE_NO_WINDOW: git / gh never flash a console window

UTC = datetime.timezone.utc
LOG = logging.getLogger("github-manager")
LOG_BYTES = 1000000      # server.log rotates at ~1 MB, keeping 3 old files
EXIT_PORT_BUSY = 2


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

    def run(self, args, cwd=None, timeout=90, binary=False, input=None):
        """-> (returncode, stdout, stderr). 127 when the program is missing, 124 on timeout. input: bytes for stdin."""
        with self.lock:
            self.count += 1
        stdin = {"stdin": subprocess.DEVNULL} if input is None else {"input": input}
        try:
            p = subprocess.run(args, cwd=cwd, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=timeout, creationflags=NO_WINDOW, **stdin)
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
        first = re.sub(r"(https?://[^\s\"?]+)\?[^\s\"]*", r"\1?...", first)  # drop signed-URL tokens
        raise GhError(int(m.group(1)) if m else None, "gh api %s: %s" % (path, first[:300]))

    def write(self, method, path, body=None):
        """A change to GitHub (only Pending.apply calls this, when q pressed Apply): `gh api --method M path`, the
        JSON body on stdin. -> (status, data). Raises GhError with GitHub's exact answer."""
        call = {"method": method, "path": path, "body": body}
        data = dumps(body).encode("utf-8") if body is not None else None
        self.calls += 1
        code, out, err = self.proc.run(gh_args(call), timeout=120, input=data)
        status, headers, text = parse_gh_include(out)
        self._rate_from(headers)
        if code == 0 and status is not None and 200 <= status < 300:
            try:
                return status, (json.loads(text) if text.strip() else None)
            except ValueError:
                return status, None
        if code == 127:
            raise GhError(None, "gh (GitHub CLI) is not installed or not on PATH")
        if status is None and ("auth login" in (err or "") or "401" in (err or "")):
            raise GhError(401, "gh is not signed in (run: gh auth login)")
        raise GhError(status, gh_write_error(status, text, err))

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

    def related(self, a, b):
        """-> True when a and b share history (an orphan branch such as gh-pages doesn't share any with testing)."""
        key = ("related", a, b)
        if key not in self.memo:
            self.memo[key] = self.git.run(["--git-dir", self.path, "merge-base", a, b])[0] == 0
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
    path = (r.get("path") or "").split("@", 1)[0]  # ".github/workflows/release.yml" (dynamic runs add "@ref")
    return {"id": r.get("id"), "workflow": r.get("name") or "", "workflowFile": path.rsplit("/", 1)[-1] or None,
            "branch": r.get("head_branch") or "",
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
        # a branch gone and a new one at the same commit in the same poll is a rename (one event, not two)
        gone = dict((pb[n], n) for n in sorted(set(pb) - set(cb), reverse=True) if pb[n])
        renamed = {}
        for name in sorted(set(cb) - set(pb)):
            old_name = gone.pop(cb[name], None)
            if old_name:
                renamed[old_name] = name
                out.append(ev("branch.renamed:%s:%s:%s" % (old_name, name, cb[name]), at, "branch.renamed",
                              {"branch": name}, "Branch %s renamed to %s (at %s)" % (old_name, name, short(cb[name])),
                              {"from": old_name, "to": name, "sha": cb[name]}))
        for name in sorted(set(pb) | set(cb)):
            old, new = pb.get(name), cb.get(name)
            ref = {"branch": name}
            if old == new or name in renamed or name in renamed.values():
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
# the editor: q's notes (<data>/editor/notes.json, notes.md); the only thing the page can change, all local

NOTE_KINDS = ("general", "branch", "pr", "run", "commit", "version", "local")
NOTE_STATUSES = ("open", "done")
NOTE_TEXT_MAX = 20 * 1024       # bytes of UTF-8 per note
NOTE_MAX = 5000                 # notes in all
NOTE_AUTHOR_MAX = 80
NOTE_KEY_MAX = 200
NOTE_BODY_MAX = 64 * 1024       # a write request's body
NOTE_NEW_FIELDS = frozenset(("target", "text", "pinned", "status", "author"))
NOTE_EDIT_FIELDS = NOTE_NEW_FIELDS | {"rev"}
NOTE_ID_RE = re.compile(r"^n[0-9a-f]{12}$")
WRITE_EXTRA_PORTS = (8768,)     # the Supervisor Board, which embeds the page
NOTE_HEADING = {"general": "General", "branch": "Branch `%s`", "pr": "Pull request #%s", "run": "CI run %s",
                "commit": "Commit `%s`", "version": "Version %s", "local": "Local copy `%s`"}
NOTE_ON = {"general": "", "branch": " on branch %s", "pr": " on PR #%s", "run": " on run %s",
           "commit": " on commit %s", "version": " on version %s", "local": " on local copy %s"}


class NoteError(Exception):
    """A rejected note write: code is the HTTP status (400 bad input, 404 no such note, 409 conflict / full)."""

    def __init__(self, code, message):
        Exception.__init__(self, message)
        self.code = code


def _printable(s):
    return not any(ord(c) < 32 or ord(c) == 127 for c in s)


def note_target(t):
    """{kind, key} -> the normalized target (raises NoteError). pr / run keys are numbers, commit keys lowercase hex,
    version keys X.Y.Z (a leading v is dropped), general has the key ""."""
    if not isinstance(t, dict) or set(t) - {"kind", "key"} or "kind" not in t:
        raise NoteError(400, "target must be {\"kind\": ..., \"key\": ...}")
    kind, key = t["kind"], t.get("key", "")
    if kind not in NOTE_KINDS:
        raise NoteError(400, "target.kind must be one of %s" % ", ".join(NOTE_KINDS))
    if kind in ("pr", "run") and isinstance(key, int) and not isinstance(key, bool):
        key = str(key)
    if not isinstance(key, str):
        raise NoteError(400, "target.key must be a string")
    key = key.strip()
    if kind == "general":
        if key:
            raise NoteError(400, "a general note has no target.key")
        return {"kind": kind, "key": ""}
    if not key or len(key) > NOTE_KEY_MAX or not _printable(key):
        raise NoteError(400, "target.key must be 1-%d printable characters" % NOTE_KEY_MAX)
    if kind in ("pr", "run"):
        key = key.lstrip("#")
        if not re.match(r"^[0-9]{1,15}$", key):
            raise NoteError(400, "a %s key is a number" % kind)
        key = str(int(key))
    elif kind == "commit":
        key = key.lower()
        if not re.match(r"^[0-9a-f]{4,64}$", key):
            raise NoteError(400, "a commit key is a sha (hex)")
    elif kind == "version":
        key = key[1:] if key[:1] in ("v", "V") else key
        if not re.match(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$", key):
            raise NoteError(400, "a version key looks like 1.18.6")
    elif kind == "branch":
        if key.startswith("origin/"):
            key = key[7:]
        if not re.match(r"^[^\s~^:?*\[\\]+$", key) or ".." in key:
            raise NoteError(400, "not a branch name")
    return {"kind": kind, "key": key}


def note_text(v):
    if not isinstance(v, str) or not v.strip():
        raise NoteError(400, "text must be a non-empty string")
    v = v.replace("\r\n", "\n").replace("\r", "\n")
    if len(v.encode("utf-8")) > NOTE_TEXT_MAX:
        raise NoteError(400, "text is over %d KB" % (NOTE_TEXT_MAX // 1024))
    if "\0" in v:
        raise NoteError(400, "text has a NUL character")
    return v


def note_author(v):
    if v is None:
        return "q"
    if not isinstance(v, str) or len(v) > NOTE_AUTHOR_MAX or not _printable(v):
        raise NoteError(400, "author must be at most %d printable characters" % NOTE_AUTHOR_MAX)
    return v.strip() or "q"


def note_flag(v, name):
    if not isinstance(v, bool):
        raise NoteError(400, "%s must be true or false" % name)
    return v


def note_status(v):
    if v not in NOTE_STATUSES:
        raise NoteError(400, "status must be open or done")
    return v


def note_fields(body, allowed):
    if not isinstance(body, dict):
        raise NoteError(400, "the body must be a JSON object")
    extra = sorted(set(body) - allowed)
    if extra:
        raise NoteError(400, "unknown field%s: %s" % ("s" if len(extra) > 1 else "", ", ".join(extra)))
    out = {}
    if "target" in body:
        out["target"] = note_target(body["target"])
    if "text" in body:
        out["text"] = note_text(body["text"])
    if "pinned" in body:
        out["pinned"] = note_flag(body["pinned"], "pinned")
    if "status" in body:
        out["status"] = note_status(body["status"])
    if "author" in body:
        out["author"] = note_author(body["author"])
    if "rev" in body:
        if not isinstance(body["rev"], int) or isinstance(body["rev"], bool):
            raise NoteError(400, "rev must be a number")
        out["rev"] = body["rev"]
    return out


def note_ref(n):
    """A history event's ref: the note plus its target as the other events name it ({"branch": ...}, {"pr": 8}, ...),
    so a note's events show with that branch / PR / run's own history."""
    ref = {"note": n["id"]}
    kind, key = n["target"]["kind"], n["target"]["key"]
    if kind in ("pr", "run"):
        ref[kind] = int(key)
    elif kind != "general":
        ref[kind] = key
    return ref


def note_on(target):
    kind, key = target["kind"], target["key"]
    return NOTE_ON[kind] % (key[:7] if kind == "commit" else key) if kind != "general" else ""


def note_snippet(text, n=80):
    line = " ".join(text.split())
    return line if len(line) <= n else line[:n - 1].rstrip() + "…"


def write_origin_ok(headers, port):
    """Note writes come only from this page (or the Supervisor Board that embeds it): the Origin header, or with no
    Origin the Referer, must be http://127.0.0.1:<port> or http://localhost:<port>. Anything else (another site,
    a file:// page's "null", no header at all) is refused."""
    allowed = set()
    for p in (port,) + WRITE_EXTRA_PORTS:
        allowed.update(("http://127.0.0.1:%d" % p, "http://localhost:%d" % p))
    origin = headers.get("Origin")
    if origin is not None:
        return origin.strip() in allowed
    u = urllib.parse.urlsplit((headers.get("Referer") or "").strip())
    return bool(u.scheme) and "%s://%s" % (u.scheme, u.netloc) in allowed


def json_content_type(ctype):
    return (ctype or "").split(";", 1)[0].strip().lower() == "application/json"


class Notes:
    """q's notes. notes.json is rewritten on every change through a temp file + os.replace, the previous version kept
    as notes.json.bak; notes.md, a readable copy grouped by target, is rewritten with it. Every change also goes to
    the history log (note.added / note.edited / note.done / note.deleted), a deleted note with its whole text, so
    nothing is lost. folder None keeps them in memory only (the page's fixture server)."""

    def __init__(self, folder, on_events=None, repo=DEFAULT_REPO, now=None):
        self.folder = folder
        self.on_events = on_events
        self.repo = repo
        self.now = now or (lambda: iso(now_dt()))
        self.lock = threading.Lock()
        self.notes = collections.OrderedDict()
        self.load_error = None
        self._built = None      # (etag, body) of GET /api/notes
        self.fresh = True       # nothing written on this PC yet (no notes.json, or none readable): see merge()
        if folder:
            self._load()

    def _p(self, name):
        return os.path.join(self.folder, name)

    def _load(self):
        path = self._p("notes.json")
        if not os.path.exists(path):
            return
        ok = lambda d: isinstance(d, dict) and isinstance(d.get("notes"), list)
        d = read_json(path)
        if not ok(d):
            # unreadable: keep it aside (the next save would otherwise copy it over the good .bak) and use the .bak
            bad = "%s.bad-%s" % (path, now_dt().strftime("%Y%m%d-%H%M%S"))
            try:
                os.replace(path, bad)
            except OSError:
                pass
            d = read_json(self._p("notes.json.bak"))
            self.load_error = "notes.json could not be read (kept as %s); %s" % (
                os.path.basename(bad), "loaded notes.json.bak" if ok(d) else "no readable notes.json.bak either")
            LOG.warning("editor: %s", self.load_error)
            if not ok(d):
                return
        self.fresh = False
        for raw in d["notes"]:
            n = self._clean(raw)
            if n:
                self.notes[n["id"]] = n

    @staticmethod
    def _clean(raw):
        if not isinstance(raw, dict) or not NOTE_ID_RE.match(str(raw.get("id") or "")):
            return None
        try:
            status = raw.get("status") if raw.get("status") in NOTE_STATUSES else "open"
            rev = raw["rev"] if isinstance(raw.get("rev"), int) else 1
            n = {"id": raw["id"], "target": note_target(raw.get("target")), "text": note_text(raw.get("text")),
                 "pinned": bool(raw.get("pinned")), "status": status, "createdAt": raw.get("createdAt"),
                 "updatedAt": raw.get("updatedAt") or raw.get("createdAt"), "author": note_author(raw.get("author")),
                 "rev": rev}
        except NoteError:
            return None
        return n

    def _sorted(self):
        """Newest change first."""
        return sorted((dict(n) for n in self.notes.values()), key=lambda n: (n.get("updatedAt") or "", n["id"]),
                      reverse=True)

    def all(self):
        with self.lock:
            return self._sorted()

    def view(self):
        """(etag, JSON body) of GET /api/notes, built once per change."""
        with self.lock:
            if self._built is None:
                body = dumps({"notes": self._sorted(), "loadError": self.load_error,
                              "limits": {"textBytes": NOTE_TEXT_MAX, "notes": NOTE_MAX}})
                self._built = (hashlib.sha1(body.encode("utf-8")).hexdigest()[:16], body)
            return self._built

    def _new_id(self):
        while True:
            nid = "n" + os.urandom(6).hex()
            if nid not in self.notes:
                return nid

    def _save(self):
        """Caller holds the lock. Raises OSError when notes.json can't be written (the change is then undone)."""
        self._built = None
        self.fresh = False
        if not self.folder:
            return
        os.makedirs(self.folder, exist_ok=True)
        path = self._p("notes.json")
        data = dumps({"version": 1, "notes": list(self.notes.values())}, pretty=True) + "\n"
        if os.path.exists(path):
            with open(path, "rb") as f:
                write_atomic(self._p("notes.json.bak"), f.read())
        write_atomic(path, data)
        try:
            write_atomic(self._p("notes.md"), self.markdown())
        except OSError as e:
            LOG.warning("editor: could not write notes.md: %s", e)

    def _emit(self, events):
        if self.on_events and events:
            try:
                self.on_events(events)
            except Exception:
                LOG.exception("editor: could not log a note event")

    def _event(self, typ, eid, n, at, text, data):
        d = {"target": n["target"], "author": n["author"], "status": n["status"], "pinned": n["pinned"]}
        d.update(data)
        return ev(eid, at, typ, note_ref(n), text, d)

    def create(self, body):
        f = note_fields(body, NOTE_NEW_FIELDS)
        if "target" not in f or "text" not in f:
            raise NoteError(400, "a new note needs target and text")
        with self.lock:
            if len(self.notes) >= NOTE_MAX:
                raise NoteError(409, "there are already %d notes, the most it keeps; delete some first" % NOTE_MAX)
            at = self.now()
            nid = self._new_id()
            n = {"id": nid, "target": f["target"], "text": f["text"], "pinned": f.get("pinned", False),
                 "status": f.get("status", "open"), "createdAt": at, "updatedAt": at,
                 "author": f.get("author", "q"), "rev": 1}
            self.notes[nid] = n
            try:
                self._save()
            except OSError as e:
                del self.notes[nid]
                self._built = None
                raise NoteError(500, "could not save the notes: %s" % e)
            e = self._event("note.added", "note.added:%s" % nid, n, at,
                            "Note%s: %s" % (note_on(n["target"]), note_snippet(n["text"])), {"text": n["text"]})
            out = dict(n)
        self._emit([e])
        return out

    def update(self, nid, body):
        f = note_fields(body, NOTE_EDIT_FIELDS)
        rev = f.pop("rev", None)
        if not f:
            raise NoteError(400, "nothing to change")
        with self.lock:
            n = self.notes.get(nid)
            if n is None:
                raise NoteError(404, "no note %s" % nid)
            if rev is not None and rev != n["rev"]:
                raise NoteError(409, "this note was changed somewhere else since you opened it (now version %d); "
                                     "reload to see it" % n["rev"])
            changed = [k for k in ("text", "target", "status", "pinned", "author") if k in f and f[k] != n[k]]
            if not changed:
                return dict(n)
            old = dict(n)
            at = self.now()
            n.update({k: f[k] for k in changed})
            n["updatedAt"] = at
            n["rev"] = old["rev"] + 1
            try:
                self._save()
            except OSError as e:
                self.notes[nid] = old
                self._built = None
                raise NoteError(500, "could not save the notes: %s" % e)
            done = "status" in changed and n["status"] == "done"
            words = []
            for k in changed:
                if k == "status":
                    words.append("done" if done else "reopened")
                elif k == "pinned":
                    words.append("pinned" if n["pinned"] else "unpinned")
                elif k == "target":
                    words.append("moved from%s" % (note_on(old["target"]) or " general"))
                elif k == "author":
                    words.append("author %s" % n["author"])
                else:
                    words.append("edited")
            data = {"changed": changed, "rev": n["rev"]}
            if "text" in changed:
                data["text"] = n["text"]
            if "target" in changed:
                data["from"] = old["target"]
            typ = "note.done" if done else "note.edited"
            e = self._event(typ, "%s:%s:%d" % (typ, nid, n["rev"]), n, at, "Note %s%s: %s" % (
                ", ".join(words), note_on(n["target"]), note_snippet(n["text"])), data)
            out = dict(n)
        self._emit([e])
        return out

    def delete(self, nid):
        with self.lock:
            n = self.notes.pop(nid, None)
            if n is None:
                raise NoteError(404, "no note %s" % nid)
            try:
                self._save()
            except OSError as e:
                self.notes[nid] = n
                self._built = None
                raise NoteError(500, "could not save the notes: %s" % e)
            at = self.now()
            # the tombstone: the whole note stays in the history log
            e = self._event("note.deleted", "note.deleted:%s" % nid, n, at,
                            "Note deleted%s: %s" % (note_on(n["target"]), note_snippet(n["text"])),
                            {"note": dict(n)})
        self._emit([e])
        return nid

    def merge(self, raws, source):
        """Notes from the Claude Documents merged into this store: on the first sync from a data folder (a new PC, a
        lost data folder, notes written here before that first sync) and while this PC has written none, so q's notes
        come back instead of an empty file being pushed over them. A note both have keeps the newer copy (this PC's on
        a tie). -> how many notes were added or updated (one note.restored event for them)."""
        got = [n for n in (self._clean(r) for r in raws) if n]
        with self.lock:
            added, newer = [], []
            for n in got:
                old = self.notes.get(n["id"])
                if old is None and len(self.notes) + len(added) < NOTE_MAX:
                    added.append(n)
                elif old is not None and (n["updatedAt"] or "") > (old["updatedAt"] or ""):
                    newer.append(n)
            if not added and not newer:
                return 0
            prev = collections.OrderedDict(self.notes)
            fresh = self.fresh
            for n in added + newer:
                self.notes[n["id"]] = n
            try:
                self._save()
            except OSError as e:
                self.notes, self.fresh, self._built = prev, fresh, None
                LOG.warning("editor: could not save the notes restored from the Claude Documents: %s", e)
                return 0
            at = self.now()
            data = {"source": source, "added": [n["id"] for n in added], "updated": [n["id"] for n in newer]}
            e = ev("note.restored:%s" % _digest(data)[:12], at, "note.restored", {},
                   "Notes: %s restored from the Claude Documents (%s)%s" % (
                       plural(len(added), "note"), source, ", %d updated to a newer copy" % len(newer) if newer else ""),
                   data)
        self._emit([e])
        return len(added) + len(newer)

    def export(self, repo):
        """github-notes.json in the Claude Documents: every note as stored, so a new install can merge them back. No
        volatile fields, so the same notes always give the same file."""
        with self.lock:
            return {"generatedBy": "GitHub Manager", "note": DOCS_GENERATED, "repo": repo, "version": 1,
                    "notes": [dict(self.notes[k]) for k in sorted(self.notes)]}

    def markdown(self, for_docs=False):
        """notes.md: every note grouped by target (general first), pinned and open first, newest first. for_docs: the
        Claude Documents copy (11-github-notes.md): the "generated" header and no volatile time, so the same notes
        always give the same file."""
        groups = collections.OrderedDict((k, {}) for k in NOTE_KINDS)
        for n in self.notes.values():
            groups[n["target"]["kind"]].setdefault(n["target"]["key"], []).append(n)
        ns = list(self.notes.values())
        n_open = sum(1 for n in ns if n["status"] == "open")
        if for_docs:
            last = max([n.get("updatedAt") or "" for n in ns] or [""]) or "never"
            out = ["# GitHub notes", "", "> %s q's notes from the GitHub Manager's Editor tab "
                   "(http://127.0.0.1:8767/#tab=editor on q's PC), grouped by what they are about." % DOCS_GENERATED,
                   "", "%s: %d open, %d done, %d pinned. Last change %s." % (
                       plural(len(ns), "note"), n_open, len(ns) - n_open, sum(1 for n in ns if n["pinned"]), last), ""]
        else:
            out = ["# GitHub manager notes", "",
                   "q's notes from the GitHub manager's Editor tab (http://127.0.0.1:8767/#tab=editor), grouped by what "
                   "they are about. The manager rewrites this file on every change, so edit the notes there, not here "
                   "(the data is notes.json next to this file; deleted notes stay in history.jsonl).", "",
                   "%s: %d open, %d done, %d pinned. Written %s." % (plural(len(ns), "note"), n_open, len(ns) - n_open,
                                                                    sum(1 for n in ns if n["pinned"]), self.now()), ""]
        if not ns:
            out += ["No notes yet.", ""]
        gh = "https://github.com/%s" % self.repo
        link = {"branch": gh + "/tree/%s", "pr": gh + "/pull/%s", "run": gh + "/actions/runs/%s",
                "commit": gh + "/commit/%s", "version": gh + "/blob/testing/changelogs/%s.md"}
        for kind, keys in groups.items():
            order = (lambda k: (len(k), k)) if kind in ("pr", "run") else (lambda k: k.lower())
            for key in sorted(keys, key=order):
                head = NOTE_HEADING[kind] % key if kind != "general" else NOTE_HEADING[kind]
                out.append("## " + head)
                out.append("")
                if kind in link:
                    out += ["<%s>" % (link[kind] % urllib.parse.quote(key, safe="/")), ""]
                items = sorted(keys[key], key=lambda n: n.get("updatedAt") or "", reverse=True)
                items.sort(key=lambda n: (not n["pinned"], n["status"] == "done"))
                for n in items:
                    when = "written %s" % n["createdAt"]
                    if n["updatedAt"] != n["createdAt"]:
                        when += ", changed %s" % n["updatedAt"]
                    out.append("### %s%s · by %s · %s · `%s`" % (n["status"], ", pinned" if n["pinned"] else "",
                                                                 n["author"], when, n["id"]))
                    out.append("")
                    out += [("> " + line).rstrip() for line in n["text"].strip("\n").split("\n")]
                    out.append("")
        return "\n".join(out)


NOTE_MD_HEAD_RE = re.compile(r"^### (open|done)(, pinned)? · by (.*) · written (\S+?)(?:, changed (\S+))? · `(n[0-9a-f]{12})`$")
NOTE_MD_TARGET_RES = [(k, re.compile("^## " + re.escape(v).replace(re.escape("%s"), "(.+)") + "$"))
                      for k, v in NOTE_HEADING.items()]
NOTE_MD_COUNT_RE = re.compile(r"^(\d+) notes?: ", re.M)


def notes_from_markdown(text):
    """11-github-notes.md (Notes.markdown's format) -> the notes in it as raw records, for a Claude Documents copy
    written before github-notes.json existed. Trailing spaces on a line are lost (the file strips them)."""
    out, target, cur = [], None, None
    for line in text.split("\n") + ["## end"]:
        if cur is not None:
            if line == ">" or line.startswith("> "):
                cur["lines"].append(line[2:])
                continue
            if not line and not cur["lines"]:   # the blank line under the heading
                continue
            if cur["lines"]:
                n = cur.pop("lines")
                cur["text"] = "\n".join(n)
                out.append(cur)
            cur = None
        if line.startswith("## "):
            target = next(({"kind": k, "key": m.group(1) if m.groups() else ""} for k, rx in NOTE_MD_TARGET_RES
                           for m in [rx.match(line)] if m), None)
            continue
        m = NOTE_MD_HEAD_RE.match(line)
        if m and target is not None:
            cur = {"id": m.group(6), "target": dict(target), "status": m.group(1), "pinned": bool(m.group(2)),
                   "author": m.group(3), "createdAt": m.group(4), "updatedAt": m.group(5) or m.group(4),
                   "lines": []}
    return out


def docs_notes(folder):
    """The notes in a Claude Documents clone -> (raw records, the file they came from, how many notes the files say
    they hold). github-notes.json first; 11-github-notes.md (parsed back) when there is no json; a hand-written
    11-github-notes.md counts as holding notes, so it is never overwritten with an empty file."""
    doc = read_json(os.path.join(folder, "github-notes.json"))
    if isinstance(doc, dict) and isinstance(doc.get("notes"), list):
        return doc["notes"], "github-notes.json", len(doc["notes"])
    try:
        with open(os.path.join(folder, "11-github-notes.md"), encoding="utf-8") as f:
            text = f.read()
    except (OSError, ValueError):
        return [], None, 0
    raws = notes_from_markdown(text)
    m = NOTE_MD_COUNT_RE.search(text)
    count = int(m.group(1)) if m else 0
    if DOCS_GENERATED not in text[:2000] and text.strip():
        count = max(count, 1)
    return raws, "11-github-notes.md", max(count, len(raws))


# ----------------------------------------------------------------------------------------------------------------
# the editor, part 2: the structure (branch roles and descriptions, local), the agent rules (followed by every
# Claude, local + Claude Documents), pending GitHub changes (branches and branch rulesets: previewed here, sent to
# GitHub only when q presses Apply), the production lock, and the Claude Documents copy of the rules and notes

EDIT_BODY_MAX = 256 * 1024      # PUT /api/rules carries the whole list
PROTECTED_BRANCHES = ("testing", "launcher", "site", "discord-bot", "gh-pages")  # never deleted / renamed; plus the default
LOCKED_RULESET_NAMES = ("production rule", "tagging rule")
ROLES = ("production", "testing", "infra", "work", "other")
SETTABLE_ROLES = ("work", "infra", "other")   # production and testing come from the repository itself
ROLE_OF_KIND = {"production": "production", "testing": "testing", "claude": "work", "infra": "infra", "other": "other"}
KIND_OF_ROLE = dict((v, k) for k, v in ROLE_OF_KIND.items())
STRUCT_DESC_MAX = 2000
STRUCT_MAX = 2000               # branches with an entry
RULE_KINDS = ("push", "merge", "version", "ci", "limit", "other")
RULE_SCOPES = ("all", "branch", "tool")
RULE_FIELDS = frozenset(("id", "title", "text", "scope", "kind", "value", "unit", "locked", "updatedAt"))
RULE_TITLE_MAX = 200
RULE_TEXT_MAX = 4000
RULE_UNIT_MAX = 24
LIMIT_MAX = 1000000000          # a limit's value: 0 to this
RULES_MAX = 200
RULE_ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
HARD_RULE_ID = "only-q-production"
STALE_RULE_ID = "limit-stale-days"
STALE_DAYS_DEFAULT = 7
CHANGE_ID_RE = re.compile(r"^c[0-9a-f]{12}$")
CHANGE_TYPES = ("branch.create", "branch.delete", "branch.rename", "ruleset.create", "ruleset.update",
                "ruleset.delete")
CHANGE_PARAMS = {"branch.create": ("name", "from"), "branch.delete": ("name",), "branch.rename": ("name", "newName"),
                 "ruleset.create": ("form",), "ruleset.update": ("id", "form"), "ruleset.delete": ("id",)}
CHANGE_DONE = ("applied",)
PENDING_MAX = 100               # waiting changes
FINISHED_KEEP = 30              # applied changes kept in the list (with their result) until discarded
RULESETS_TTL = 60               # GET /api/github/rulesets re-reads GitHub at most this often (unless ?fresh=1)
SHA_RE = re.compile(r"^[0-9a-f]{7,40}$")
BRANCH_NAME_MAX = 200
STALE_MSG = "changed on GitHub since preview, review again"
RULESET_FORM_FIELDS = frozenset(("name", "enforcement", "include", "exclude", "blockForcePush", "blockDeletion",
                                 "linearHistory", "requirePr", "approvals", "requiredChecks", "strictChecks",
                                 "adminBypass"))
RULESET_MANAGED = ("deletion", "non_fast_forward", "required_linear_history", "pull_request", "required_status_checks")
PR_DEFAULTS = {"required_approving_review_count": 0, "dismiss_stale_reviews_on_push": False,
               "require_code_owner_review": False, "require_last_push_approval": False,
               "required_review_thread_resolution": False}
ADMIN_BYPASS = {"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"}  # role 5 = repository admin
DOCS_REPO = "floration/quark-claude-docs"
DOCS_BRANCH = "main"
DOCS_DEBOUNCE_SEC = 10          # a burst of edits is one commit
DOCS_START_DELAY = 30           # after a start, sync once if the files differ from what was last pushed
DOCS_COMMIT_MSG = "GitHub Manager: rules/notes updated"
DOCS_GENERATED = "Generated by the GitHub Manager; edit there, not here."
DOCS_FILES = ("10-github-rules.md", "github-rules.json", "11-github-notes.md", "github-notes.json")
# Names used before 2026-10-24 (09 is now the owner's answers file); removed from the clone when they still carry
# DOCS_GENERATED, i.e. were written by this tool.
DOCS_LEGACY_FILES = ("09-github-rules.md", "10-github-notes.md")
GH_ACCEPT = "Accept: application/vnd.github+json"
GH_VERSION = "X-GitHub-Api-Version: 2022-11-28"


class EditError(NoteError):
    """A refused editor write (structure, rules, pending changes). Same codes as NoteError, plus 422 for the lock and
    502 when GitHub could not be read."""

    def __init__(self, code, message, change=None):
        NoteError.__init__(self, code, message)
        self.change = change
        self.locked = False


class LockError(EditError):
    """Refused by the production lock: only q pushes to production (the default branch and v* tags)."""

    def __init__(self, reason):
        EditError.__init__(self, 422, "Refused by the production lock (only q pushes to production): " + reason)
        self.locked = True
        self.reason = reason


def edit_fields(body, allowed, what="the body"):
    if not isinstance(body, dict):
        raise EditError(400, "%s must be a JSON object" % what)
    extra = sorted(set(body) - set(allowed))
    if extra:
        raise EditError(400, "unknown field%s in %s: %s" % ("s" if len(extra) > 1 else "", what, ", ".join(extra)))
    return body


def edit_str(v, name, lo=0, hi=200, lines=False):
    """A string field: CRLF -> LF, trimmed, no control characters (lines=True allows newlines and tabs), lo-hi bytes
    of UTF-8."""
    if not isinstance(v, str):
        raise EditError(400, "%s must be a string" % name)
    v = v.replace("\r\n", "\n").replace("\r", "\n")
    v = v.strip("\n").rstrip() if lines else v.strip()
    if any((ord(c) < 32 and not (lines and c in "\n\t")) or ord(c) == 127 for c in v):
        raise EditError(400, "%s has a control character" % name)
    n = len(v.encode("utf-8"))
    if n < lo:
        raise EditError(400, "%s can't be empty" % name)
    if n > hi:
        raise EditError(400, "%s is over %d bytes" % (name, hi))
    return v


def edit_int(v, name, lo, hi):
    if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
        raise EditError(400, "%s must be a whole number from %d to %d" % (name, lo, hi))
    return v


def edit_bool(v, name):
    if not isinstance(v, bool):
        raise EditError(400, "%s must be true or false" % name)
    return v


def branch_name(v, what="branch"):
    """A branch name as git and the refs API take it (refs/heads/ and origin/ prefixes dropped). Strict on purpose:
    the name goes into REST paths unquoted."""
    if not isinstance(v, str):
        raise EditError(400, "%s must be a string" % what)
    v = v.strip()
    for p in ("refs/heads/", "origin/"):
        if v.startswith(p):
            v = v[len(p):]
    if not v or len(v) > BRANCH_NAME_MAX or not re.match(r"^[A-Za-z0-9._/-]+$", v) or ".." in v or "//" in v or \
            "/." in v or v[0] in "/-." or v[-1] in "/." or v.endswith(".lock") or v == "HEAD":
        raise EditError(400, "%s %r is not a valid branch name (letters, digits, . _ / -)" % (what, v[:80]))
    return v


def short_sha(sha):
    return sha[:7] if sha else "?"


def ref_glob_re(pattern):
    """A ruleset ref pattern (fnmatch style) as a regex that matches AT LEAST what GitHub matches: * and ** both cross
    '/', a [class] matches any one character. The lock uses it, so erring wide only ever refuses more."""
    out = []
    i = 0
    while i < len(pattern):
        c = pattern[i]
        if c == "*":
            while i < len(pattern) and pattern[i] == "*":
                i += 1
            out.append(".*")
            continue
        if c == "?":
            out.append(".")
        elif c == "[" and "]" in pattern[i + 1:]:
            i = pattern.index("]", i + 1)
            out.append(".")
        else:
            out.append(re.escape(c))
        i += 1
    return re.compile("".join(out) + r"\Z")


def ref_glob_re_narrow(pattern):
    """A ref pattern as a regex that matches AT MOST what GitHub matches (* stays inside one '/' part, ** crosses
    them, ? is one character but not '/'); None for a [class], which it won't guess at. For an 'Except' entry: an
    exclusion only counts for the lock when it surely covers the branch."""
    if "[" in pattern:
        return None
    out = []
    for part in re.split(r"(\*\*|\*|\?)", pattern):
        out.append({"**": ".*", "*": "[^/]*", "?": "[^/]"}.get(part) or re.escape(part))
    return re.compile("".join(out) + r"\Z")


def pattern_hits_branch(pattern, branch):
    if pattern in ("~ALL",):
        return True
    rx = ref_glob_re(pattern)
    return bool(rx.match("refs/heads/" + branch) or rx.match(branch))


def pattern_surely_hits_branch(pattern, branch):
    """For 'Except': True only when the pattern certainly matches the branch on GitHub (~DEFAULT_BRANCH is resolved by
    the caller)."""
    rx = ref_glob_re_narrow(pattern)
    return bool(rx and (rx.match("refs/heads/" + branch) or (not pattern.startswith("refs/") and rx.match(branch))))


def ruleset_lock_reasons(rs, default_branch):
    """Why a ruleset (GitHub's JSON or a normalized one) must not be changed here. [] when it may be."""
    if not rs:
        return []
    out = []
    name = str(rs.get("name") or "")
    if name.strip().lower() in LOCKED_RULESET_NAMES:
        out.append("the %r ruleset guards production and is locked" % name)
    target = rs.get("target") or "branch"
    ref_name = (rs.get("conditions") or {}).get("ref_name") or {}
    include = ref_name.get("include") or []
    exclude = ref_name.get("exclude") or []
    if target != "branch":
        tags = [p for p in include if p == "~ALL" or ref_glob_re(p).match("refs/tags/v1.0.0") or
                ref_glob_re(p).match("v1.0.0")]
        if target == "tag" and tags:
            out.append("it targets v* tags (%s): only q creates or moves production tags" % ", ".join(tags))
        else:
            out.append("it is a %s ruleset: only branch rulesets are edited here (tag and push rules guard the "
                       "v* production tags)" % target)
    if not default_branch:
        out.append("the default branch could not be read from GitHub, so the production lock can't be checked")
        return out
    # a pattern such as claude/* may cover the default branch (claude/compassionate-ritchie-07ah6i) when 'Except'
    # surely takes it out again; ~ALL and ~DEFAULT_BRANCH stay refused whatever the excludes say
    spared = any(x == "~DEFAULT_BRANCH" or pattern_surely_hits_branch(x, default_branch) for x in exclude)
    for p in include:
        if p == "~DEFAULT_BRANCH":
            out.append("it targets the default branch (~DEFAULT_BRANCH = %s)" % default_branch)
        elif p == "~ALL":
            out.append("it targets every branch, the default branch %s too" % default_branch)
        elif pattern_hits_branch(p, default_branch) and not spared:
            out.append("its pattern %s matches the default branch %s (add it under Except to leave it out)" % (
                p, default_branch))
    return out


def hard_lock(typ, params, default_branch, before=None, after=None):
    """The production lock: raises LockError when a change could touch production. Checked when a change is queued
    and again, against GitHub's state read right then, immediately before it is applied. Never configurable."""
    p = params or {}
    if typ.startswith("tag."):
        name = str(p.get("name") or "")
        if re.match(r"^(refs/tags/)?v", name) or not name:
            raise LockError("only q creates or moves v* tags")
        raise LockError("tags are not changed from the GitHub editor")
    if not default_branch:
        raise LockError("the default branch could not be read from GitHub, so the lock can't be checked")
    guarded = (default_branch,) + PROTECTED_BRANCHES

    def why(b):
        return "%s is the default (production) branch" % b if b == default_branch else \
            "%s is a protected branch (%s)" % (b, ", ".join(PROTECTED_BRANCHES))
    if typ in ("branch.delete", "branch.rename"):
        name = p.get("name")
        if name in guarded:
            raise LockError("%s: it can't be %s here" % (why(name), "deleted" if typ == "branch.delete" else "renamed"))
        if typ == "branch.rename" and p.get("newName") in guarded:
            raise LockError("%s: no branch can be renamed to it" % why(p.get("newName")))
    elif typ == "branch.create":
        if p.get("name") in guarded:
            raise LockError("%s: it can't be created or replaced here" % why(p.get("name")))
    elif typ.startswith("ruleset."):
        for label, rs in (("the ruleset as it is on GitHub", before), ("the ruleset as it would be", after)):
            reasons = ruleset_lock_reasons(rs, default_branch)
            if reasons:
                raise LockError("%s: %s" % (label, "; ".join(reasons)))
    else:
        raise LockError("%s is not a change the GitHub editor makes" % typ)


# ---- rulesets: GitHub's JSON <-> the form ---------------------------------------------------------------------

def _rule_sort(r):
    return (r.get("type") or "", dumps(r.get("parameters")) if r.get("parameters") is not None else "")


def ruleset_norm(d):
    """GitHub's ruleset JSON -> the parts a PUT sends (name, target, enforcement, conditions, rules, bypass_actors),
    rules sorted by type so the comparison and the diff ignore GitHub's ordering."""
    if not d:
        return None
    cond = dict(d.get("conditions") or {})
    rn = cond.get("ref_name") or {}
    cond["ref_name"] = {"include": list(rn.get("include") or []), "exclude": list(rn.get("exclude") or [])}
    rules = []
    for r in d.get("rules") or []:
        x = {"type": r.get("type")}
        if r.get("parameters") is not None:
            x["parameters"] = r["parameters"]
        rules.append(x)
    out = {"name": d.get("name"), "target": d.get("target") or "branch", "enforcement": d.get("enforcement"),
           "conditions": cond, "rules": sorted(rules, key=_rule_sort)}
    if d.get("bypass_actors") is not None:  # GitHub shows it to admins only: when it is missing, a PUT leaves it be
        bypass = [{"actor_id": a.get("actor_id"), "actor_type": a.get("actor_type"),
                   "bypass_mode": a.get("bypass_mode")} for a in d["bypass_actors"]]
        out["bypass_actors"] = sorted(bypass, key=lambda a: (str(a["actor_type"]), str(a["actor_id"])))
    return out


def _strip_heads(p):
    return p[len("refs/heads/"):] if p.startswith("refs/heads/") else p


def ruleset_form(n):
    """A normalized branch ruleset -> the page's form."""
    rules = dict((r["type"], r) for r in n["rules"])
    pr = (rules.get("pull_request") or {}).get("parameters") or {}
    sc = (rules.get("required_status_checks") or {}).get("parameters") or {}
    rn = n["conditions"]["ref_name"]
    return {"name": n["name"], "enforcement": n["enforcement"] if n["enforcement"] in ("active", "disabled") else "active",
            "include": [_strip_heads(p) for p in rn["include"]], "exclude": [_strip_heads(p) for p in rn["exclude"]],
            "blockForcePush": "non_fast_forward" in rules, "blockDeletion": "deletion" in rules,
            "linearHistory": "required_linear_history" in rules, "requirePr": "pull_request" in rules,
            "approvals": int(pr.get("required_approving_review_count") or 0),
            "requiredChecks": [c.get("context") for c in sc.get("required_status_checks") or [] if c.get("context")],
            "strictChecks": bool(sc.get("strict_required_status_checks_policy")),
            "adminBypass": any(a.get("actor_type") == "RepositoryRole" and a.get("actor_id") == 5
                               for a in n.get("bypass_actors") or [])}


def _ref_pattern(v, name):
    v = edit_str(v, name, 1, 200)
    if v in ("~ALL", "~DEFAULT_BRANCH"):
        return v
    if not re.match(r"^[A-Za-z0-9._/*?\[\]!-]+$", v):
        raise EditError(400, "%s %r: use branch names or patterns (letters, digits, . _ / - * ?)" % (name, v[:60]))
    return v if v.startswith("refs/heads/") else "refs/heads/" + v


def ruleset_from_form(form, before=None):
    """The page's form -> the ruleset to send (GitHub's JSON, normalized). Rule types the form doesn't manage and the
    bypass list (other than the admin role) are kept from `before`, so an edit never drops what it doesn't show."""
    f = edit_fields(form, RULESET_FORM_FIELDS, "the ruleset form")
    name = edit_str(f.get("name", ""), "name", 1, 100)
    enf = f.get("enforcement", "active")
    if enf not in ("active", "disabled"):
        raise EditError(400, "enforcement must be active or disabled")
    inc, exc = f.get("include", []), f.get("exclude", [])
    if not isinstance(inc, list) or not 1 <= len(inc) <= 20 or not isinstance(exc, list) or len(exc) > 20:
        raise EditError(400, "list 1-20 branches (include) and at most 20 to leave out (exclude)")
    include = list(dict.fromkeys(_ref_pattern(x, "branch") for x in inc))
    exclude = list(dict.fromkeys(_ref_pattern(x, "excluded branch") for x in exc))
    flags = {}
    for k in ("blockForcePush", "blockDeletion", "linearHistory", "requirePr", "strictChecks", "adminBypass"):
        flags[k] = edit_bool(f.get(k, False), k)
    approvals = edit_int(f.get("approvals", 0), "approvals", 0, 10)
    checks = f.get("requiredChecks", [])
    if not isinstance(checks, list) or len(checks) > 50:
        raise EditError(400, "requiredChecks must be a list of at most 50 check names")
    checks = list(dict.fromkeys(edit_str(c, "check name", 1, 200) for c in checks))
    prev = dict((r["type"], r) for r in (before or {}).get("rules") or [])
    rules = []
    if flags["blockDeletion"]:
        rules.append({"type": "deletion"})
    if flags["blockForcePush"]:
        rules.append({"type": "non_fast_forward"})
    if flags["linearHistory"]:
        rules.append({"type": "required_linear_history"})
    if flags["requirePr"]:
        params = dict(PR_DEFAULTS)
        params.update((prev.get("pull_request") or {}).get("parameters") or {})
        params["required_approving_review_count"] = approvals
        rules.append({"type": "pull_request", "parameters": params})
    if checks:
        old_params = (prev.get("required_status_checks") or {}).get("parameters") or {}
        old = dict((c.get("context"), c) for c in old_params.get("required_status_checks") or [])
        rules.append({"type": "required_status_checks", "parameters": {
            "do_not_enforce_on_create": bool(old_params.get("do_not_enforce_on_create", False)),
            "strict_required_status_checks_policy": flags["strictChecks"],
            "required_status_checks": [old.get(c) or {"context": c} for c in checks]}})
    rules += [r for t, r in prev.items() if t not in RULESET_MANAGED]
    if not rules:
        raise EditError(400, "turn on at least one rule")
    known = before is None or before.get("bypass_actors") is not None
    bypass = [a for a in (before or {}).get("bypass_actors") or []
              if not (a.get("actor_type") == "RepositoryRole" and a.get("actor_id") == 5)]
    if flags["adminBypass"]:
        bypass.append(dict(ADMIN_BYPASS))
    cond = dict((before or {}).get("conditions") or {})
    cond["ref_name"] = {"include": include, "exclude": exclude}
    out = {"name": name, "target": "branch", "enforcement": enf, "conditions": cond, "rules": rules}
    if known or bypass:
        out["bypass_actors"] = bypass
    return ruleset_norm(out)


RULE_WORDS = {"deletion": "blocks deletion", "non_fast_forward": "blocks force pushes",
              "required_linear_history": "requires linear history", "creation": "restricts creation",
              "update": "restricts updates", "required_signatures": "requires signed commits"}


def ruleset_summary(n):
    """['blocks deletion', 'requires a PR (0 approvals)', 'requires checks: meta, linux', ...]"""
    out = []
    for r in n["rules"]:
        t, p = r["type"], r.get("parameters") or {}
        if t == "pull_request":
            out.append("requires a PR (%s)" % plural(int(p.get("required_approving_review_count") or 0), "approval"))
        elif t == "required_status_checks":
            cs = [c.get("context") for c in p.get("required_status_checks") or []]
            out.append("requires checks: %s%s" % (", ".join(cs), " (up to date)"
                                                 if p.get("strict_required_status_checks_policy") else ""))
        else:
            out.append(RULE_WORDS.get(t, t.replace("_", " ")))
    return out


def ruleset_applies(n):
    rn = n["conditions"]["ref_name"]
    word = {"~DEFAULT_BRANCH": "the default branch", "~ALL": "every %s" % ("tag" if n["target"] == "tag" else "branch")}
    strip = (lambda p: p[len("refs/tags/"):] if p.startswith("refs/tags/") else p) if n["target"] == "tag" \
        else _strip_heads
    s = ", ".join(word.get(p, strip(p)) for p in rn["include"]) or "nothing"
    if rn["exclude"]:
        s += " except " + ", ".join(word.get(p, strip(p)) for p in rn["exclude"])
    return s


def pretty_json(obj):
    return json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False)


def json_diff(before, after):
    """Unified diff lines ('-', '+', ' ', '@@') of two JSON values, pretty-printed with sorted keys."""
    a = pretty_json(before).splitlines() if before is not None else []
    b = pretty_json(after).splitlines() if after is not None else []
    return list(difflib.unified_diff(a, b, "before", "after", n=3, lineterm=""))[2:]


def gh_args(call):
    args = ["gh", "api", "-i", "--method", call["method"], "-H", GH_ACCEPT, "-H", GH_VERSION, call["path"]]
    if call.get("body") is not None:
        args += ["--input", "-"]
    return args


def gh_command(call):
    """The exact command apply runs, for the page (the body goes on stdin)."""
    def q(a):
        return a if re.match(r"^[A-Za-z0-9._/:=+-]+$", a) else '"%s"' % a.replace('"', '\\"')
    return " ".join(q(a) for a in gh_args(call))


def gh_write_error(status, body, err):
    """GitHub's answer to a refused write, as exactly as it said it: HTTP status, message, each error."""
    parts = []
    try:
        j = json.loads(body) if body and body.strip() else None
    except ValueError:
        j = None
    if isinstance(j, dict):
        if j.get("message"):
            parts.append(str(j["message"]))
        for e in j.get("errors") or []:
            if isinstance(e, dict):
                parts.append(e.get("message") or ", ".join("%s=%s" % kv for kv in sorted(e.items())))
            else:
                parts.append(str(e))
    elif body and body.strip():
        parts.append(body.strip()[:500])
    line = next((l.strip() for l in (err or "").splitlines() if l.strip()), "")
    if line and not parts:
        parts.append(line)
    return "HTTP %s: %s" % (status if status is not None else "?", "; ".join(parts) or "failed")


# ---- small JSON files in <data>/editor ------------------------------------------------------------------------

class EditorFile:
    """One JSON file of the editor: rewritten through a temp file + replace, the previous version kept as .bak; an
    unreadable file is set aside (<name>.bad-<time>) and the .bak loaded instead. folder None: memory only."""

    def __init__(self, folder, name, valid):
        self.folder, self.name, self.valid = folder, name, valid
        self.load_error = None

    def path(self, suffix=""):
        return os.path.join(self.folder, self.name + suffix)

    def load(self):
        if not self.folder or not os.path.exists(self.path()):
            return None
        d = read_json(self.path())
        if self.valid(d):
            return d
        bad = "%s.bad-%s" % (self.path(), now_dt().strftime("%Y%m%d-%H%M%S"))
        try:
            os.replace(self.path(), bad)
        except OSError:
            pass
        d = read_json(self.path(".bak"))
        self.load_error = "%s could not be read (kept as %s); %s" % (
            self.name, os.path.basename(bad), "loaded %s.bak" % self.name if self.valid(d) else "started empty")
        LOG.warning("editor: %s", self.load_error)
        return d if self.valid(d) else None

    def save(self, obj):
        if not self.folder:
            return
        os.makedirs(self.folder, exist_ok=True)
        if os.path.exists(self.path()):
            with open(self.path(), "rb") as f:
                write_atomic(self.path(".bak"), f.read())
        write_atomic(self.path(), dumps(obj, pretty=True) + "\n")


def _digest(obj):
    return hashlib.sha1(dumps(obj).encode("utf-8")).hexdigest()


class _Store:
    """What Structure, Rules and Pending share: a lock, the history hook, the clock, one built GET body."""

    def __init__(self, folder, name, valid, on_events, now):
        self.file = EditorFile(folder, name, valid)
        self.on_events = on_events
        self.now = now or (lambda: iso(now_dt()))
        self.lock = threading.RLock()
        self._built = None

    def _emit(self, events):
        if self.on_events and events:
            try:
                self.on_events(events)
            except Exception:
                LOG.exception("editor: could not log an event")

    def _write(self, obj):
        """Caller holds the lock. -> raises EditError(500) when the file can't be written."""
        self._built = None
        try:
            self.file.save(obj)
        except OSError as e:
            raise EditError(500, "could not save %s: %s" % (self.file.name, e))


# ---- the structure ---------------------------------------------------------------------------------------------

class Structure(_Store):
    """q's model of the branches: per branch a role override (work / infra / other; production and testing come from
    the repository) and a description. Local only (editor/structure.json); the roles set the Branches tab's kind."""

    def __init__(self, folder, on_events=None, now=None):
        _Store.__init__(self, folder, "structure.json",
                        lambda d: isinstance(d, dict) and isinstance(d.get("branches"), dict), on_events, now)
        self.branches = {}
        self.rev = 0
        self.updated_at = None
        d = self.file.load()
        if d:
            self.rev = d.get("rev") if isinstance(d.get("rev"), int) else 0
            self.updated_at = d.get("updatedAt")
            for name, e in d["branches"].items():
                try:
                    self.branches[branch_name(name)] = self._entry(e, keep_time=True)
                except EditError:
                    pass

    @staticmethod
    def _entry(e, keep_time=False):
        edit_fields(e, ("role", "description", "updatedAt") if keep_time else ("role", "description"), "a branch entry")
        role = e.get("role")
        if role is not None and role not in SETTABLE_ROLES:
            raise EditError(400, "role must be one of %s, or null (automatic)" % ", ".join(SETTABLE_ROLES))
        out = {"role": role, "description": edit_str(e.get("description", ""), "description", 0, STRUCT_DESC_MAX,
                                                     lines=True)}
        if keep_time:
            out["updatedAt"] = e.get("updatedAt")
        return out

    def obj(self):
        with self.lock:
            return {"branches": dict((k, dict(v)) for k, v in sorted(self.branches.items())), "rev": self.rev,
                    "updatedAt": self.updated_at, "loadError": self.file.load_error}

    def entry(self, name):
        with self.lock:
            e = self.branches.get(name)
            return dict(e) if e else None

    def update(self, body, default_branch=None):
        f = edit_fields(body, ("branches", "rev"))
        if not isinstance(f.get("branches"), dict) or not f["branches"]:
            raise EditError(400, "send {\"branches\": {\"<name>\": {\"role\": ..., \"description\": ...} | null}}")
        if len(f["branches"]) > 100:
            raise EditError(400, "at most 100 branches per change")
        changes = {}
        for name, e in f["branches"].items():
            n = branch_name(name)
            if e is not None:
                e = self._entry(e)
                if e["role"] is not None and n in ("testing", default_branch):
                    raise EditError(400, "%s's role comes from the repository and can't be changed" % n)
            changes[n] = e
        with self.lock:
            if "rev" in f and f["rev"] != self.rev:
                raise EditError(409, "the structure was changed somewhere else since you opened it (now version %d); "
                                     "reload to see it" % self.rev)
            at = self.now()
            words, touched = [], []
            new = dict(self.branches)
            for n, e in changes.items():
                old = new.get(n)
                if e is None or (e["role"] is None and not e["description"]):
                    if old:
                        del new[n]
                        words.append("%s back to automatic, no description" % n)
                        touched.append(n)
                    continue
                if old and old["role"] == e["role"] and old["description"] == e["description"]:
                    continue
                w = []
                if not old or old["role"] != e["role"]:
                    w.append("role %s" % (e["role"] or "automatic"))
                if not old or old["description"] != e["description"]:
                    w.append("description %s" % ("set" if e["description"] else "removed"))
                words.append("%s: %s" % (n, ", ".join(w)))
                touched.append(n)
                new[n] = dict(e, updatedAt=at)
            if not touched:
                return self.obj()
            if len(new) > STRUCT_MAX:
                raise EditError(409, "the structure keeps at most %d branches" % STRUCT_MAX)
            old_all = (self.branches, self.rev, self.updated_at)
            self.branches, self.rev, self.updated_at = new, self.rev + 1, at
            try:
                self._write({"version": 1, "rev": self.rev, "updatedAt": at, "branches": self.branches})
            except EditError:
                self.branches, self.rev, self.updated_at = old_all
                raise
            data = {"rev": self.rev, "branches": dict((n, new.get(n)) for n in touched)}
            ref = {"branch": touched[0]} if len(touched) == 1 else {}
            e = ev("structure.changed:%d:%s" % (self.rev, _digest(data)[:8]), at, "structure.changed", ref,
                   "Structure: " + "; ".join(words), data)
            out = self.obj()
        self._emit([e])
        return out

    def rename(self, old, new):
        """A branch renamed on GitHub: its role and description move to the new name. -> True when one moved."""
        with self.lock:
            e = self.branches.get(old)
            if not e or new in self.branches:
                return False
            at = self.now()
            prev = (self.branches, self.rev, self.updated_at)
            self.branches = dict((new if k == old else k, v) for k, v in self.branches.items())
            self.rev, self.updated_at = self.rev + 1, at
            try:
                self._write({"version": 1, "rev": self.rev, "updatedAt": at, "branches": self.branches})
            except EditError as err:
                self.branches, self.rev, self.updated_at = prev
                LOG.warning("editor: %s", err)
                return False
            data = {"rev": self.rev, "from": old, "to": new, "branches": {new: dict(e), old: None}}
            ev_ = ev("structure.changed:%d:%s" % (self.rev, _digest(data)[:8]), at, "structure.changed",
                     {"branch": new}, "Structure: %s's role and description moved to %s (renamed)" % (old, new), data)
        self._emit([ev_])
        return True


def branch_roles(branches, structure, stale_days, default_branch, now=None, stale_scope=None):
    """Sets role / roleSet / description / stale / kind on the state's branches (dicts, changed in place).
    stale: a work or other branch with no commit in stale_days that is merged into testing (nothing ahead) or behind
    it, and inside the stale rule's scope (stale_scope, a rule's scope: a "branch" pattern limits it, e.g. claude/*);
    the default branch, testing and the infra branches are never stale."""
    now = now or now_dt()
    scoped = (stale_scope or {}).get("type") == "branch" and (stale_scope or {}).get("match")
    for b in branches:
        auto = ROLE_OF_KIND.get(branch_kind(b["name"], default_branch), "other")
        e = structure.entry(b["name"]) if structure else None
        role = auto
        if e and e.get("role") and auto not in ("production", "testing"):
            role = e["role"]
        b["role"] = role
        b["roleSet"] = role != auto
        b["description"] = (e or {}).get("description") or ""
        b["kind"] = KIND_OF_ROLE.get(role, "other")
        d = parse_time(b.get("date"))
        old = d is not None and (now - d).total_seconds() > stale_days * 86400
        b["stale"] = bool(role in ("work", "other") and b["name"] not in PROTECTED_BRANCHES and old and
                          (b.get("ahead") == 0 or (b.get("behind") or 0) > 0) and
                          (not scoped or pattern_hits_branch(stale_scope["match"], b["name"])))
    return branches


# ---- the agent rules -------------------------------------------------------------------------------------------

def _rule(id_, title, kind, text, scope=("all", ""), value=None, unit="", locked=False):
    return {"id": id_, "title": title, "text": text, "scope": {"type": scope[0], "match": scope[1]}, "kind": kind,
            "value": value, "unit": unit, "locked": locked}


HARD_RULE = _rule(
    HARD_RULE_ID, "Only q pushes to production", "push",
    "Production is the default branch and the v* tags. Only q pushes, merges or tags there: no Claude pushes to the "
    "default branch, merges into it, or creates or moves a v* tag. The production PR (testing into the default "
    "branch) is opened only after q says the testing build passed docs/RELEASE_CHECKLIST.md. The GitHub Manager "
    "refuses any change to a ruleset on the default branch or v* tags, and deleting or renaming the default branch, "
    "testing, launcher, site, discord-bot or gh-pages.", locked=True)
SEED_RULES = [
    HARD_RULE,
    _rule("claude-pushes-non-production", "Claude may push and merge to testing", "merge",
          "Claude may push and merge to testing and the other non-production branches when it judges it right."),
    _rule("testing-version-bump", "Every testing push takes the next VERSION", "version",
          "Every push to testing takes the next free VERSION (semver) and adds both changelogs, "
          "changelogs/<VERSION>.md (short) and changelogs/full/<VERSION>.md (full). Tooling-only PRs may skip the "
          "bump.", ("branch", "testing")),
    _rule("sanitizers-final-only", "Sanitizers only for a [final] build", "ci",
          "Sanitizers run only for a test build a human marked \"[final]\". They never block."),
    _rule("ci-scope", "CI runs meta, builds and sanitizers", "ci",
          "CI runs only meta, the builds and sanitizers: no gameplay tests in CI."),
    _rule("sync-pc-copies", "Sync q's PC copies after testing changes", "push",
          "After every push or merge to testing, sync q's PC copies.", ("branch", "testing")),
    _rule("never-launch-quark-exe", "Never launch quark.exe on q's PC", "other",
          "Never launch quark.exe on q's PC.", ("tool", "quark.exe")),
    _rule("macos-skip-retry", "macOS CI: skipped after 5 min, retried hourly", "ci",
          "When no macOS runner picks the macOS job up within 5 min it is skipped, and macos-retry.yml retries it "
          "hourly (for up to 2 days)."),
    _rule("limit-open-prs-per-thread", "Open PRs per thread", "limit",
          "The most open PRs one thread (one Claude session) keeps open at a time.", value=2, unit="PRs"),
    _rule(STALE_RULE_ID, "Stale branch age", "limit",
          "A work branch with no commits in this many days that is merged into or behind testing is stale (the "
          "Structure section lists it under Stale). Only branches in this rule's scope go stale.", ("branch", "claude/*"),
          value=STALE_DAYS_DEFAULT, unit="days"),
    _rule("limit-ci-job-minutes", "CI minutes per job", "limit",
          "The longest a CI job may run (its timeout-minutes).", ("tool", "GitHub Actions"), value=120,
          unit="minutes"),
]
RULE_KIND_HEADING = {"push": "Pushing", "merge": "Merging", "version": "Versions and changelogs", "ci": "CI",
                     "limit": "Limits", "other": "Other"}


def _rule_content(r):
    return dict((k, r.get(k)) for k in ("id", "title", "text", "scope", "kind", "value", "unit", "locked"))


def rule_fields(raw):
    """One rule from the page -> the normalized rule (no updatedAt; id None when it is to be made from the title)."""
    edit_fields(raw, RULE_FIELDS, "a rule")
    rid = raw.get("id")
    if rid is not None and rid != "":
        if not isinstance(rid, str) or not RULE_ID_RE.match(rid):
            raise EditError(400, "a rule id is 1-64 of a-z, 0-9 and -, starting with a letter or digit")
    else:
        rid = None
    title = edit_str(raw.get("title", ""), "title", 1, RULE_TITLE_MAX)
    text = edit_str(raw.get("text", ""), "text", 1, RULE_TEXT_MAX, lines=True)
    kind = raw.get("kind", "other")
    if kind not in RULE_KINDS:
        raise EditError(400, "kind must be one of %s" % ", ".join(RULE_KINDS))
    sc = raw.get("scope", {"type": "all"})
    edit_fields(sc, ("type", "match"), "a rule's scope")
    st = sc.get("type", "all")
    if st not in RULE_SCOPES:
        raise EditError(400, "scope.type must be one of %s" % ", ".join(RULE_SCOPES))
    match = "" if st == "all" else edit_str(sc.get("match", ""), "scope.match", 1, 200)
    value, unit = raw.get("value"), raw.get("unit") or ""
    if kind == "limit":
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value != value:
            raise EditError(400, "the limit %r needs a value (a number from 0 to 1000000000)" % title)
        if not 0 <= value <= LIMIT_MAX:
            raise EditError(400, "the limit %r must be %s" % (title, "0 or more" if value < 0 else
                                                               "at most %d" % LIMIT_MAX))
        if isinstance(value, float) and value.is_integer():
            value = int(value)
        unit = edit_str(unit, "unit", 0, RULE_UNIT_MAX)
    else:
        if value is not None:
            raise EditError(400, "only a limit has a value")
        unit = ""
    return {"id": rid, "title": title, "text": text, "scope": {"type": st, "match": match}, "kind": kind,
            "value": value, "unit": unit, "locked": edit_bool(raw.get("locked", False), "locked")}


def _slug(title, taken):
    base = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:50].strip("-") or "rule"
    rid, i = base, 2
    while rid in taken:
        rid = "%s-%d" % (base, i)
        i += 1
    return rid


class Rules(_Store):
    """The agent rules: what every Claude follows (and the limits it keeps), editor/rules.json. Seeded on the first
    start with the rules as they stand, marked "seeded" until q edits them: a seeded set gives way to the
    github-rules.json already in the Claude Documents (adopt), so a new install or a lost data folder never pushes
    the seed over rules edited elsewhere. The production rule (HARD_RULE) is locked for good: it can't be edited,
    unlocked or removed, and its text always comes from this file. Other rules can be locked / unlocked by q."""

    def __init__(self, folder, on_events=None, now=None):
        _Store.__init__(self, folder, "rules.json",
                        lambda d: isinstance(d, dict) and isinstance(d.get("rules"), list), on_events, now)
        self.rules = []
        self.rev = 0
        self.updated_at = None
        self.seeded = False     # the built-in seed, never edited here
        d = self.file.load()
        if d is None:
            at = self.now()
            self.rules = [dict(r, updatedAt=at) for r in SEED_RULES]
            self.rev, self.updated_at, self.seeded = 1, at, not self.file.load_error
            if folder and not self.file.load_error:
                try:
                    self.file.save(self._obj_file())
                except OSError as e:
                    LOG.warning("editor: could not write rules.json: %s", e)
        else:
            self.rev = d.get("rev") if isinstance(d.get("rev"), int) else 1
            self.updated_at = d.get("updatedAt")
            self.seeded = d.get("seeded") is True
            self.rules = self._read_rules(d["rules"])
        # the production rule is always there, first, exactly as written above
        old = next((r for r in self.rules if r["id"] == HARD_RULE_ID), None)
        self.rules = [dict(HARD_RULE, updatedAt=(old or {}).get("updatedAt") or self.now())] + \
            [r for r in self.rules if r["id"] != HARD_RULE_ID]

    @staticmethod
    def _read_rules(raws):
        """Stored rules (rules.json, or github-rules.json in the Claude Documents) -> the valid ones, ids unique."""
        out, seen = [], set()
        for raw in raws:
            try:
                r = rule_fields(dict((k, v) for k, v in raw.items() if k in RULE_FIELDS))
            except (EditError, AttributeError):
                continue
            if r["id"] and r["id"] not in seen:
                seen.add(r["id"])
                out.append(dict(r, updatedAt=raw.get("updatedAt")))
        return out

    def _obj_file(self):
        o = {"version": 1, "rev": self.rev, "updatedAt": self.updated_at, "rules": self.rules}
        if self.seeded:
            o["seeded"] = True
        return o

    def adopt(self, doc):
        """github-rules.json from the Claude Documents replaces a still-seeded rule set (the production rule stays as
        written here). -> True when adopted; False when the rules were edited here already or doc isn't rules."""
        if not isinstance(doc, dict) or not isinstance(doc.get("rules"), list):
            return False
        got = [r for r in self._read_rules(doc["rules"]) if r["id"] != HARD_RULE_ID]
        hard_at = next((r.get("updatedAt") for r in doc["rules"] if isinstance(r, dict) and
                        r.get("id") == HARD_RULE_ID), None)
        with self.lock:
            if not self.seeded:
                return False
            prev = (self.rules, self.rev, self.updated_at, self.seeded)
            self.rules = [dict(HARD_RULE, updatedAt=hard_at or self.rules[0].get("updatedAt"))] + got
            self.rev = doc["rev"] if isinstance(doc.get("rev"), int) and doc["rev"] > 0 else self.rev
            self.updated_at = doc.get("updatedAt") if isinstance(doc.get("updatedAt"), str) else self.now()
            self.seeded = False
            try:
                self._write(self._obj_file())
            except EditError as e:
                self.rules, self.rev, self.updated_at, self.seeded = prev
                LOG.warning("editor: %s", e)
                return False
            data = {"rev": self.rev, "adopted": True, "rules": [r["id"] for r in self.rules]}
            e = ev("rules.changed:%d:adopted:%s" % (self.rev, _digest(data)[:8]), self.now(), "rules.changed", {},
                   "Agent rules: %s loaded from the Claude Documents (github-rules.json, version %d) instead of the "
                   "built-in starting set" % (plural(len(self.rules), "rule"), self.rev), data)
        self._emit([e])
        return True

    def obj(self):
        with self.lock:
            return {"rules": [dict(r) for r in self.rules], "rev": self.rev, "updatedAt": self.updated_at,
                    "loadError": self.file.load_error}

    def scope(self, rid):
        with self.lock:
            for r in self.rules:
                if r["id"] == rid:
                    return dict(r["scope"])
        return None

    def limit(self, rid, default):
        with self.lock:
            for r in self.rules:
                if r["id"] == rid and r["kind"] == "limit" and isinstance(r.get("value"), (int, float)):
                    return r["value"]
        return default

    def replace(self, body):
        f = edit_fields(body, ("rules", "rev"))
        if not isinstance(f.get("rules"), list):
            raise EditError(400, "send {\"rules\": [rule, ...], \"rev\": N}")
        if len(f["rules"]) > RULES_MAX:
            raise EditError(409, "at most %d rules" % RULES_MAX)
        new = [rule_fields(r) for r in f["rules"]]
        ids = [r["id"] for r in new if r["id"]]
        dup = sorted(set(i for i in ids if ids.count(i) > 1))
        if dup:
            raise EditError(400, "two rules have the id %s" % dup[0])
        taken = set(ids)
        for r in new:
            if not r["id"]:
                r["id"] = _slug(r["title"], taken)
                taken.add(r["id"])
        hard = [r for r in new if r["id"] == HARD_RULE_ID]
        if not hard:
            raise LockError("the rule %r is locked: it can't be removed" % HARD_RULE["title"])
        if _rule_content(hard[0]) != _rule_content(HARD_RULE):
            raise LockError("the rule %r is locked: it can't be edited or unlocked" % HARD_RULE["title"])
        with self.lock:
            if "rev" in f and f["rev"] != self.rev:
                raise EditError(409, "the rules were changed somewhere else since you opened them (now version %d); "
                                     "reload to see them" % self.rev)
            cur = dict((r["id"], r) for r in self.rules)
            at = self.now()
            out = []
            added, edited = [], []
            for r in new:
                old = cur.get(r["id"])
                if old and _rule_content(old) == _rule_content(r):
                    out.append(dict(old))
                else:
                    out.append(dict(r, updatedAt=at))
                    (edited if old else added).append(r["id"])
            removed = [i for i in cur if i not in taken]
            moved = [r["id"] for r in out] != [r["id"] for r in self.rules]
            if not (added or edited or removed or moved):
                return self.obj()
            old_all = (self.rules, self.rev, self.updated_at, self.seeded)
            self.rules, self.rev, self.updated_at, self.seeded = out, self.rev + 1, at, False
            try:
                self._write(self._obj_file())
            except EditError:
                self.rules, self.rev, self.updated_at, self.seeded = old_all
                raise
            title = dict((r["id"], r["title"]) for r in list(cur.values()) + out)
            words = [w for w in (
                "added " + ", ".join(title[i] for i in added) if added else "",
                "edited " + ", ".join(title[i] for i in edited) if edited else "",
                "removed " + ", ".join(title[i] for i in removed) if removed else "",
                "reordered" if moved and not (added or removed) else "") if w]
            data = {"rev": self.rev, "added": added, "edited": edited, "removed": removed,
                    "rules": dict((i, next(r for r in out if r["id"] == i)) for i in added + edited),
                    "removedRules": dict((i, cur[i]) for i in removed)}
            e = ev("rules.changed:%d:%s" % (self.rev, _digest(data)[:8]), at, "rules.changed", {},
                   "Agent rules: " + "; ".join(words), data)
            result = self.obj()
        self._emit([e])
        return result

    def export(self, repo, default_branch):
        """github-rules.json in the Claude Documents."""
        with self.lock:
            return {"generatedBy": "GitHub Manager", "note": DOCS_GENERATED, "repo": repo,
                    "defaultBranch": default_branch, "rev": self.rev, "updatedAt": self.updated_at,
                    "rules": [dict(r) for r in self.rules]}

    def markdown(self, repo, default_branch):
        """10-github-rules.md: the rules, grouped by kind, for people and agents. No volatile timestamps, so the same
        rules always give the same file."""
        with self.lock:
            rules = [dict(r) for r in self.rules]
            rev, updated = self.rev, self.updated_at
        out = ["# GitHub rules", "", "> %s The GitHub Manager's Editor tab, Rules & limits "
               "(http://127.0.0.1:8767/#tab=editor&sec=rules on q's PC), is the source; the same rules as data are in "
               "github-rules.json next to this file." % DOCS_GENERATED, "",
               "The rules every Claude follows when it works on %s, and the limits it keeps. Locked rules are q's: never "
               "change them or work around them. The default branch (production) is `%s`." % (
                   repo, default_branch or "unknown"), "",
               "Rules version %d, last changed %s. GitHub itself enforces the rulesets shown in the same section "
               "(\"Enforced by GitHub\"); this file is what agents follow." % (rev, updated or "never"), ""]

        def scope(r):
            s = r["scope"]
            return "everywhere" if s["type"] == "all" else "%s `%s`" % (s["type"], s["match"])
        for kind in RULE_KINDS:
            group = [r for r in rules if r["kind"] == kind]
            if not group:
                continue
            out += ["## %s" % RULE_KIND_HEADING[kind], ""]
            for r in group:
                head = "- **%s**" % r["title"]
                if kind == "limit":
                    head += ": **%s%s**" % (r["value"], " " + r["unit"] if r["unit"] else "")
                head += " (%s%s; `%s`)" % ("locked; " if r["locked"] else "", scope(r), r["id"])
                out.append(head)
                out += ["  " + line if line else "" for line in r["text"].split("\n")]
            out.append("")
        return "\n".join(out)


# ---- pending GitHub changes ------------------------------------------------------------------------------------

def follow_rename(structure, notes, old, new):
    """An applied rename takes the branch's structure entry and its notes along (each note's move is a note.edited
    event with the old target in "from"). The server and the fixture server both use it."""
    structure.rename(old, new)
    for n in notes.all():
        if n["target"] == {"kind": "branch", "key": old}:
            try:
                notes.update(n["id"], {"target": {"kind": "branch", "key": new}, "rev": n["rev"]})
            except NoteError as e:
                LOG.warning("editor: could not move note %s to %s: %s", n["id"], new, e)


class Pending(_Store):
    """Changes to GitHub that q queued in the page: each is previewed (before -> after, the exact `gh api` call) and
    sent only when q presses Apply. Right before the call the server reads the default branch and the affected
    branch / ruleset again: the production lock is checked once more, and when GitHub's state is not what the preview
    was based on the change is refused (STALE_MSG). The call itself is rebuilt from the change's parameters, never
    taken from the file. editor/pending.json; results and history events (github.*) are recorded."""

    def __init__(self, folder, gh, repo, on_events=None, now=None, on_applied=None):
        _Store.__init__(self, folder, "pending.json",
                        lambda d: isinstance(d, dict) and isinstance(d.get("changes"), list), on_events, now)
        self.gh = gh
        self.repo = repo
        self.on_applied = on_applied
        self.apply_lock = threading.Lock()
        self.changes = collections.OrderedDict()
        d = self.file.load()
        for c in (d or {}).get("changes") or []:
            if isinstance(c, dict) and CHANGE_ID_RE.match(str(c.get("id") or "")) and c.get("type") in CHANGE_TYPES:
                if c.get("status") == "applying":  # the server stopped in the middle of the call
                    c["status"] = "failed"
                    c["result"] = {"ok": False, "at": self.now(), "error": "the server stopped while this was being "
                                   "applied: check GitHub, then preview it again"}
                self.changes[c["id"]] = c

    # -- reading GitHub --
    def _get(self, path):
        data, _, _ = self.gh.get(path)
        return data

    def _get_or_none(self, path):
        try:
            return self._get(path)
        except GhError as e:
            if e.status in (404, 422):
                return None
            raise

    def default_branch(self):
        try:
            db = (self._get("repos/%s" % self.repo) or {}).get("default_branch")
        except GhError as e:
            raise EditError(502, "could not read the default branch from GitHub: %s" % e)
        if not db:
            raise EditError(502, "GitHub did not say which branch is the default")
        return db

    def _ref_sha(self, name):
        d = self._get_or_none("repos/%s/git/ref/heads/%s" % (self.repo, name))
        if isinstance(d, dict) and d.get("ref") == "refs/heads/" + name:
            return (d.get("object") or {}).get("sha")
        return None

    def _ruleset(self, rid):
        return ruleset_norm(self._get_or_none("repos/%s/rulesets/%d" % (self.repo, rid)))

    def _preview(self, typ, p, default):
        """Reads GitHub now -> the preview: summary, before / after (+ text), diff, basis (what apply re-checks),
        call, warnings, problems (blocking), ref (for history)."""
        R = "repos/%s" % self.repo
        pv = {"warnings": [], "problems": [], "before": None, "after": None, "beforeText": "", "afterText": "",
              "diff": None, "ref": {}}
        try:
            if typ == "branch.create":
                name, frm = p["name"], p["from"]
                exists = self._ref_sha(name)
                is_sha = bool(SHA_RE.match(frm))
                if is_sha:
                    src = ((self._get_or_none("%s/commits/%s" % (R, frm)) or {}).get("sha"))
                else:
                    src = self._ref_sha(frm)
                if exists:
                    pv["problems"].append("a branch %s already exists on GitHub (at %s)" % (name, short_sha(exists)))
                if not src:
                    pv["problems"].append("no %s %s on GitHub" % ("commit" if is_sha else "branch", frm))
                where = ("commit %s" % short_sha(src or frm)) if is_sha else "%s (%s)" % (frm, short_sha(src))
                pv.update(summary="Create branch %s from %s" % (name, where),
                          beforeText="No branch %s on GitHub." % name,
                          afterText="Branch %s at %s, %s." % (name, short_sha(src), "that commit" if is_sha else
                                                               "where %s is now" % frm),
                          basis={"defaultBranch": default, "exists": bool(exists), "fromSha": src},
                          call={"method": "POST", "path": R + "/git/refs",
                                "body": {"ref": "refs/heads/" + name, "sha": src}},
                          ref={"branch": name}, after={"branch": name, "sha": src})
            elif typ in ("branch.delete", "branch.rename"):
                name = p["name"]
                sha = self._ref_sha(name)
                if not sha:
                    pv["problems"].append("no branch %s on GitHub" % name)
                prs = [x.get("number") for x in self._get_or_none(
                    "%s/pulls?state=open&head=%s:%s&per_page=20" % (R, self.repo.split("/")[0], name)) or []]
                ahead = None
                if sha:
                    cmp_ = self._get_or_none("%s/compare/testing...%s?per_page=1" % (R, sha))
                    ahead = (cmp_ or {}).get("ahead_by")
                state = []
                if ahead:
                    state.append("%s not in testing" % plural(ahead, "commit"))
                elif ahead == 0:
                    state.append("everything on it is in testing")
                if prs:
                    state.append("open PR %s" % ", ".join("#%s" % n for n in prs))
                before_text = "Branch %s at %s%s." % (name, short_sha(sha), " (%s)" % "; ".join(state) if state else "")
                basis = {"defaultBranch": default, "sha": sha, "openPrs": prs}
                if typ == "branch.delete":
                    if ahead:
                        pv["warnings"].append("%s on %s %s not in testing: after the delete they are reachable only "
                                              "by sha, until GitHub drops them" % (
                                                  plural(ahead, "commit"), name, "is" if ahead == 1 else "are"))
                    elif ahead is None and sha:
                        pv["warnings"].append("could not compare %s with testing: check it has nothing unmerged" % name)
                    for n in prs:
                        pv["warnings"].append("open PR #%s comes from %s: deleting the branch closes it" % (n, name))
                    pv.update(summary="Delete branch %s (at %s)" % (name, short_sha(sha)), beforeText=before_text,
                              afterText="No branch %s on GitHub." % name, basis=basis,
                              call={"method": "DELETE", "path": "%s/git/refs/heads/%s" % (R, name), "body": None},
                              before={"branch": name, "sha": sha})
                else:
                    new = p["newName"]
                    taken = self._ref_sha(new)
                    if taken:
                        pv["problems"].append("a branch %s already exists on GitHub" % new)
                    for n in prs:
                        pv["warnings"].append("open PR #%s moves to the new name (GitHub retargets it)" % n)
                    basis["taken"] = bool(taken)
                    pv.update(summary="Rename branch %s to %s" % (name, new), beforeText=before_text,
                              afterText="Branch %s at %s; no branch %s." % (new, short_sha(sha), name), basis=basis,
                              call={"method": "POST", "path": "%s/branches/%s/rename" % (R, name),
                                    "body": {"new_name": new}},
                              before={"branch": name, "sha": sha}, after={"branch": new, "sha": sha})
                pv["ref"] = {"branch": name}
            else:
                cur = None
                if typ == "ruleset.create":
                    names = [str(x.get("name") or "").lower() for x in self._get("%s/rulesets?per_page=100" % R) or []]
                    after = ruleset_from_form(p["form"], None)
                    taken = after["name"].lower() in names
                    if taken:
                        pv["problems"].append("a ruleset named %r already exists" % after["name"])
                    basis = {"defaultBranch": default, "nameTaken": taken}
                    call = {"method": "POST", "path": R + "/rulesets", "body": after}
                    summary = "Create branch ruleset %r: %s, on %s" % (
                        after["name"], ", ".join(ruleset_summary(after)), ruleset_applies(after))
                else:
                    rid = p["id"]
                    cur = self._ruleset(rid)
                    if cur is None:
                        pv["problems"].append("no ruleset %d on GitHub" % rid)
                    basis = {"defaultBranch": default, "ruleset": cur}
                    if typ == "ruleset.update":
                        after = ruleset_from_form(p["form"], cur) if cur is not None else None
                        if cur is not None and after == cur:
                            pv["problems"].append("nothing would change")
                        call = {"method": "PUT", "path": "%s/rulesets/%d" % (R, rid), "body": after}
                        summary = "Change ruleset %r" % ((cur or {}).get("name") or rid)
                        if after and cur and after["name"] != cur["name"]:
                            summary += " (renamed %r)" % after["name"]
                    else:
                        after = None
                        call = {"method": "DELETE", "path": "%s/rulesets/%d" % (R, rid), "body": None}
                        summary = "Delete ruleset %r (%s)" % ((cur or {}).get("name") or rid,
                                                             ruleset_applies(cur) if cur else "not found")
                    pv["ref"] = {"ruleset": rid}
                pv.update(summary=summary, before=cur, after=after, basis=basis, call=call,
                          diff=json_diff(cur, after),
                          beforeText=("Rules: %s; on %s; %s." % (", ".join(ruleset_summary(cur)), ruleset_applies(cur),
                                                                 cur["enforcement"]) if cur else "No such ruleset."),
                          afterText=("Rules: %s; on %s; %s." % (", ".join(ruleset_summary(after)),
                                                                ruleset_applies(after), after["enforcement"])
                                     if after else "No ruleset."))
        except GhError as e:
            raise EditError(502, "could not read GitHub for the preview: %s" % e)
        pv["command"] = gh_command(pv["call"])
        pv["previewHash"] = _digest([pv["basis"], pv["before"], pv["after"], pv["call"]])[:16]
        return pv

    @staticmethod
    def _params(typ, params):
        edit_fields(params, CHANGE_PARAMS[typ], "params")
        p = {}
        if typ.startswith("branch."):
            p["name"] = branch_name(params.get("name"))
            if typ == "branch.create":
                frm = params.get("from", "testing")
                frm = frm.strip().lower() if isinstance(frm, str) and SHA_RE.match(frm.strip().lower()) else \
                    branch_name(frm, "from")
                p["from"] = frm
            if typ == "branch.rename":
                p["newName"] = branch_name(params.get("newName"), "new name")
                if p["newName"] == p["name"]:
                    raise EditError(400, "the new name is the same")
        else:
            if typ != "ruleset.create":
                p["id"] = edit_int(params.get("id"), "id", 1, 2 ** 53)
            if typ != "ruleset.delete":
                form = params.get("form")
                ruleset_from_form(form)  # validate now (400 for a bad form)
                p["form"] = form
        return p

    def _with_preview(self, ch, pv):
        for k in ("summary", "before", "after", "beforeText", "afterText", "diff", "basis", "call", "command",
                  "warnings", "ref", "previewHash"):
            ch[k] = pv[k]
        ch["previewedAt"] = self.now()
        return ch

    def _save(self):
        self._write({"version": 1, "changes": list(self.changes.values())})

    def obj(self):
        with self.lock:
            return {"changes": [dict(c) for c in self.changes.values()],
                    "waiting": sum(1 for c in self.changes.values() if c["status"] != "applied"),
                    "protectedBranches": list(PROTECTED_BRANCHES), "loadError": self.file.load_error}

    def get(self, cid):
        with self.lock:
            c = self.changes.get(cid)
            if c is None:
                raise EditError(404, "no pending change %s" % cid)
            return c

    def queue(self, body):
        f = edit_fields(body, ("type", "params"))
        typ, params = f.get("type"), f.get("params", {})
        if not isinstance(typ, str):
            raise EditError(400, "type must be one of %s" % ", ".join(CHANGE_TYPES))
        if typ.startswith("tag."):
            hard_lock(typ, params if isinstance(params, dict) else {}, None)
        if typ not in CHANGE_TYPES:
            raise EditError(400, "type must be one of %s" % ", ".join(CHANGE_TYPES))
        p = self._params(typ, params)
        default = self.default_branch()
        hard_lock(typ, p, default)
        pv = self._preview(typ, p, default)
        hard_lock(typ, p, default, before=pv["before"] if typ.startswith("ruleset.") else None,
                  after=pv["after"] if typ.startswith("ruleset.") else None)
        if pv["problems"]:
            raise EditError(409, "; ".join(pv["problems"]))
        with self.lock:
            for c in self.changes.values():
                if c["status"] != "applied" and c["type"] == typ and c["params"] == p:
                    raise EditError(409, "the same change is already queued (%s)" % c["id"])
            if sum(1 for c in self.changes.values() if c["status"] != "applied") >= PENDING_MAX:
                raise EditError(409, "%d changes are already waiting; apply or discard some first" % PENDING_MAX)
            cid = "c" + os.urandom(6).hex()
            ch = {"id": cid, "type": typ, "params": p, "status": "pending", "createdAt": self.now(), "result": None}
            self._with_preview(ch, pv)
            self.changes[cid] = ch
            try:
                self._save()
            except EditError:
                del self.changes[cid]
                raise
            return dict(ch)

    def discard(self, cid):
        with self.lock:
            c = self.changes.get(cid)
            if c is None:
                raise EditError(404, "no pending change %s" % cid)
            if c["status"] == "applying":
                raise EditError(409, "it is being applied right now")
            del self.changes[cid]
            try:
                self._save()
            except EditError:
                self.changes[cid] = c
                raise
        return cid

    def repreview(self, cid):
        c = dict(self.get(cid))
        if c["status"] in ("applied", "applying"):
            raise EditError(409, "it was applied already" if c["status"] == "applied" else "it is being applied")
        default = self.default_branch()
        hard_lock(c["type"], c["params"], default)
        pv = self._preview(c["type"], c["params"], default)
        rs = c["type"].startswith("ruleset.")
        hard_lock(c["type"], c["params"], default, before=pv["before"] if rs else None,
                  after=pv["after"] if rs else None)
        if pv["problems"]:
            raise EditError(409, "; ".join(pv["problems"]))
        with self.lock:
            ch = self.changes.get(cid)
            if ch is None:
                raise EditError(404, "no pending change %s" % cid)
            self._with_preview(ch, pv)
            ch["status"], ch["result"] = "pending", None
            self._save()
            return dict(ch)

    def _finish(self, ch, status, at, **result):
        ch["status"] = status
        ch["result"] = dict(result, ok=status == "applied", at=at)
        if status == "applied":
            done = [c for c in self.changes.values() if c["status"] == "applied"]
            for old in done[:max(0, len(done) - FINISHED_KEEP)]:
                self.changes.pop(old["id"], None)
        try:
            self._save()
        except EditError as e:
            LOG.warning("editor: %s", e)
        return dict(ch)

    @staticmethod
    def _what_changed(old, new):
        out = []
        for k in sorted(set(old) | set(new)):
            if old.get(k) == new.get(k):
                continue
            a, b = old.get(k), new.get(k)
            if k == "defaultBranch":
                out.append("the default branch is now %s (was %s)" % (b, a))
            elif k in ("sha", "fromSha"):
                out.append("the branch moved from %s to %s" % (short_sha(a), short_sha(b)))
            elif k == "exists":
                out.append("the branch exists now" if b else "the branch is gone")
            elif k == "openPrs":
                out.append("open PRs are now %s (were %s)" % (", ".join("#%s" % n for n in b) or "none",
                                                              ", ".join("#%s" % n for n in a) or "none"))
            elif k == "ruleset":
                out.append("the ruleset was changed on GitHub" if b else "the ruleset is gone")
            elif k in ("nameTaken", "taken"):
                out.append("the name is taken now")
            else:
                out.append("%s changed" % k)
        return out

    def apply(self, cid, body=None):
        """-> the change, with status applied / stale / refused / failed and its result."""
        f = edit_fields(body if body is not None else {}, ("previewHash",))
        with self.apply_lock:
            with self.lock:
                ch = self.changes.get(cid)
                if ch is None:
                    raise EditError(404, "no pending change %s" % cid)
                if ch["status"] == "applied":
                    raise EditError(409, "it was applied already", dict(ch))
                if f.get("previewHash") is not None and f["previewHash"] != ch.get("previewHash"):
                    raise EditError(409, "this change was previewed again since the page showed it: review it again",
                                    dict(ch))
                typ, p = ch["type"], ch["params"]
            at = self.now()
            try:
                default = self.default_branch()
                hard_lock(typ, p, default)
                pv = self._preview(typ, p, default)
                rs = typ.startswith("ruleset.")
                hard_lock(typ, p, default, before=pv["before"] if rs else None, after=pv["after"] if rs else None)
            except LockError as e:
                return self._not_applied(ch, "refused", at, str(e), locked=True)
            except EditError as e:
                return self._not_applied(ch, "failed", at, str(e))
            if pv["basis"] != ch["basis"] or pv["call"] != ch["call"]:
                what = self._what_changed(ch["basis"] or {}, pv["basis"] or {}) or ["the call would differ"]
                return self._not_applied(ch, "stale", at, "%s: %s" % (STALE_MSG, "; ".join(what)))
            if pv["problems"]:
                return self._not_applied(ch, "failed", at, "; ".join(pv["problems"]))
            call = pv["call"]
            with self.lock:
                ch["status"] = "applying"
                try:
                    self._save()
                except EditError as e:
                    return self._finish(ch, "failed", at, error=str(e))
            try:
                status, data = self.gh.write(call["method"], call["path"], call.get("body"))
            except GhError as e:
                return self._not_applied(ch, "failed", at, str(e), httpStatus=e.status)
            done_at = self.now()
            info = self._result_info(typ, data)
            with self.lock:
                out = self._finish(ch, "applied", done_at, httpStatus=status, **info)
            self._emit([self._applied_event(out, done_at, info)])
        if self.on_applied:
            try:
                self.on_applied(out)
            except Exception:
                LOG.exception("editor: after-apply hook failed")
        return out

    def _not_applied(self, ch, status, at, error, **result):
        """Finishes a change that didn't go through (refused by the lock, out of date, failed) and logs it in the
        history, like an applied one: github.apply.refused / .stale / .failed."""
        with self.lock:
            out = self._finish(ch, status, self.now() if status == "failed" else at, error=error, **result)
        word = {"refused": "refused by the production lock", "stale": "changed on GitHub since the preview"}.get(
            status, "failed")
        self._emit([ev("github.apply.%s:%s:%s" % (status, ch["id"], at), at, "github.apply.%s" % status,
                       ch.get("ref") or {}, "Not applied on GitHub (%s): %s: %s" % (word, ch["summary"], error),
                       {"change": ch["id"], "type": ch["type"], "call": ch.get("call"), "status": status,
                        "error": error})])
        return out

    @staticmethod
    def _result_info(typ, data):
        d = data if isinstance(data, dict) else {}
        if typ.startswith("ruleset.") and d.get("id"):
            return {"rulesetId": d.get("id"), "url": ((d.get("_links") or {}).get("html") or {}).get("href")}
        if typ == "branch.create":
            return {"sha": (d.get("object") or {}).get("sha")}
        if typ == "branch.rename":
            return {"name": d.get("name")}
        return {}

    def _applied_event(self, ch, at, info):
        typ, p = ch["type"], ch["params"]
        name = {"branch.create": "github.branch.created", "branch.delete": "github.branch.deleted",
                "branch.rename": "github.branch.renamed", "ruleset.create": "github.ruleset.created",
                "ruleset.update": "github.ruleset.updated", "ruleset.delete": "github.ruleset.deleted"}[typ]
        ref = dict(ch.get("ref") or {})
        if typ == "ruleset.create" and info.get("rulesetId"):
            ref["ruleset"] = info["rulesetId"]
        first, _, rest = ch["summary"].partition(" ")
        text = "%s %s on GitHub" % ({"Create": "Created", "Delete": "Deleted", "Rename": "Renamed",
                                     "Change": "Changed"}.get(first, first), rest)
        return ev("%s:%s" % (name, ch["id"]), at, name, ref, text,
                  {"change": ch["id"], "type": typ, "params": p, "before": ch.get("before"), "after": ch.get("after"),
                   "call": ch.get("call"), "result": ch.get("result")})

    def apply_all(self, body=None):
        edit_fields(body if body is not None else {}, ())
        with self.lock:
            ids = [c["id"] for c in self.changes.values() if c["status"] in ("pending",)]
        results, stopped = [], None
        for cid in ids:
            try:
                r = self.apply(cid)
            except EditError as e:
                r = e.change or {"id": cid, "status": "failed", "result": {"ok": False, "error": str(e)}}
            results.append(r)
            if r.get("status") != "applied":
                stopped = cid
                break
        return {"results": results, "stoppedAt": stopped, "applied": sum(1 for r in results
                                                                          if r.get("status") == "applied")}


# ---- the Claude Documents copy ---------------------------------------------------------------------------------

def docs_sync_wanted(cfg):
    """(enabled, why not). The Claude Documents belong to floration/quark: another repo (the tests' o/r) syncs only
    with an explicit claudeDocsRepo; GHM_DOCS_SYNC=off (the unit tests set it) or "claudeDocs": false turn it off."""
    if os.environ.get("GHM_DOCS_SYNC", "").lower() in ("off", "0", "no"):
        return False, "off (GHM_DOCS_SYNC=off)"
    if cfg.get("claudeDocs") is False:
        return False, "off (\"claudeDocs\": false in github-manager.json)"
    if cfg.get("repo", DEFAULT_REPO) != DEFAULT_REPO and not cfg.get("claudeDocsRepo"):
        return False, "off (only for %s)" % DEFAULT_REPO
    return True, None


class DocsSync:
    """Keeps <data>/claude-docs, the tool's own clone of the private Claude Documents repo, and on a change of the
    rules or notes writes 10-github-rules.md, github-rules.json and 11-github-notes.md there, commits and pushes
    (10 s after the last change, so a burst is one commit). The clone is the tool's: before writing it is reset to
    origin's branch (the files are regenerated whole, nothing local is kept). A rejected push is retried once after
    `git pull --rebase`. Failures are kept for the page and poll.warnings; nothing here raises into the server."""

    def __init__(self, folder, state_path, proc, render, repo=DOCS_REPO, branch=DOCS_BRANCH, enabled=True,
                 off_reason=None, debounce=DOCS_DEBOUNCE_SEC, adopt=None):
        self.folder, self.state_path, self.proc, self.render = folder, state_path, proc, render
        self.adopt = adopt      # adopt(clone folder, first): called on origin's files right before the files are
        #                         rendered; first: no sync from this data folder has worked yet
        self.repo, self.branch, self.enabled, self.off_reason, self.debounce = repo, branch, enabled, off_reason, debounce
        self.lock = threading.Lock()
        self.sync_lock = threading.Lock()
        self.wake = threading.Event()
        self.thread = None
        self.due = None
        self.saved = read_json(state_path, {}) if state_path else {}
        if not isinstance(self.saved, dict):
            self.saved = {}
        self.state = "idle"
        self.last_error = None
        self.last_attempt = None
        self.failing_since = None   # the first failure of the current run of failures

    def status(self):
        with self.lock:
            return {"enabled": self.enabled, "state": self.state if self.enabled else "off",
                    "offReason": self.off_reason, "repo": self.repo, "branch": self.branch, "files": list(DOCS_FILES),
                    "dueAt": iso(datetime.datetime.fromtimestamp(self.due, UTC)) if self.due else None,
                    "lastOk": self.saved.get("lastOk"), "lastCommit": self.saved.get("lastCommit"),
                    "lastAttempt": self.last_attempt, "lastError": self.last_error, "failingSince": self.failing_since}

    def warnings(self):
        with self.lock:
            if self.enabled and self.state == "failed" and self.last_error:
                return ["Claude Documents sync failed (it retries on every poll): %s" % self.last_error]
        return []

    def retry_if_failed(self):
        """Each poll: a failed sync (gh signed out, offline) is tried again, so it recovers once gh works."""
        with self.lock:
            again = self.enabled and self.state == "failed" and self.due is None
        if again:
            self.schedule(0)
        return again

    def schedule(self, delay=None):
        if not self.enabled:
            return
        with self.lock:
            self.due = time.time() + (self.debounce if delay is None else delay)
            if self.state != "syncing":
                self.state = "waiting"
            if self.thread is None:
                self.thread = threading.Thread(target=self._worker, name="docs-sync", daemon=True)
                self.thread.start()
            else:
                self.wake.set()

    def schedule_if_changed(self, delay=DOCS_START_DELAY):
        """At start: sync once if what would be written differs from what was last pushed."""
        if not self.enabled:
            return False
        try:
            digest = _digest(self.render())
        except Exception:
            LOG.exception("docs sync: could not render the files")
            return False
        if digest != self.saved.get("digest"):
            self.schedule(delay)
            return True
        with self.lock:
            self.state = "ok"
        return False

    def _worker(self):
        while True:
            with self.lock:
                due = self.due
                if due is None:
                    self.thread = None
                    return
                wait = due - time.time()
                if wait <= 0:
                    self.due = None
            if wait > 0:
                self.wake.wait(min(wait, 60))
                self.wake.clear()
                continue
            try:
                self.sync_now()
            except Exception as e:  # never let the thread die
                LOG.exception("docs sync crashed")
                with self.lock:
                    self.state, self.last_error = "failed", "crashed: %r" % e

    def _git(self, *args, timeout=120):
        code, out, err = self.proc.run(["git", "-C", self.folder] + list(args), timeout=timeout)
        return code, out, err

    def _git_ok(self, *args, timeout=120):
        code, out, err = self._git(*args, timeout=timeout)
        if code != 0:
            raise RuntimeError("git %s: %s" % (args[0], (err or out or "exit %d" % code).strip().splitlines()[-1]))
        return out

    def _write_files(self, files):
        for name, text in files.items():
            write_atomic(os.path.join(self.folder, name), text)

    def _drop_legacy(self):
        """-> the legacy generated files removed (git rm) from the clone; a hand-written file of that name stays."""
        gone = []
        for name in DOCS_LEGACY_FILES:
            path = os.path.join(self.folder, name)
            try:
                with open(path, encoding="utf-8") as f:
                    head = f.read(2000)
            except OSError:
                continue
            if DOCS_GENERATED in head:
                self._git_ok("rm", "-q", "--", name)
                gone.append(name)
        return gone

    def _commit(self, files):
        self._write_files(files)
        self._drop_legacy()
        self._git_ok("add", "--", *files)
        if self._git("diff", "--cached", "--quiet")[0] == 0:
            return False
        who = []
        if not self._git("config", "user.email")[1].strip():
            who = ["-c", "user.name=GitHub Manager", "-c", "user.email=github-manager@users.noreply.github.com"]
        code, out, err = self.proc.run(["git", "-C", self.folder] + who + ["commit", "-q", "-m", DOCS_COMMIT_MSG],
                                       timeout=60)
        if code != 0:
            raise RuntimeError("git commit: %s" % (err or out).strip())
        return True

    def sync_now(self):
        """-> the status after one sync (clone if needed, reset to origin, write, commit, push, retry once)."""
        if not self.enabled:
            return self.status()
        with self.sync_lock:
            with self.lock:
                self.state, self.last_attempt = "syncing", iso(now_dt())
            try:
                git_dir = os.path.join(self.folder, ".git")
                if not os.path.isdir(git_dir):
                    if os.path.isdir(self.folder) and os.listdir(self.folder):
                        raise RuntimeError("%s exists but is not a git clone; move it away" % self.folder)
                    parent = os.path.dirname(self.folder)
                    os.makedirs(parent, exist_ok=True)
                    code, out, err = self.proc.run(
                        ["gh", "repo", "clone", self.repo, self.folder, "--", "--quiet", "--branch", self.branch,
                         "-c", "credential.helper=", "-c", "credential.helper=!gh auth git-credential"],
                        cwd=parent, timeout=300)
                    if code != 0:
                        raise RuntimeError("gh repo clone %s: %s" % (self.repo, (err or out or "exit %d" % code)
                                                                    .strip().splitlines()[-1]))
                for d in ("rebase-merge", "rebase-apply"):
                    if os.path.isdir(os.path.join(git_dir, d)):
                        self._git("rebase", "--abort")
                self._git_ok("fetch", "--quiet", "origin", self.branch, timeout=180)
                self._git_ok("checkout", "-q", "-f", "-B", self.branch, "origin/" + self.branch)
                if self.adopt:
                    with self.lock:
                        first = not self.saved.get("lastOk")
                    self.adopt(self.folder, first)
                files = self.render()
                digest = _digest(files)
                committed = self._commit(files)
                if committed:
                    code, out, err = self._git("push", "--quiet", "origin", "HEAD:" + self.branch, timeout=180)
                    if code != 0:
                        # rejected (someone pushed in between): pull --rebase and push once more
                        if self._git("pull", "--quiet", "--rebase", "origin", self.branch, timeout=180)[0] != 0:
                            self._git("rebase", "--abort")
                            self._git_ok("fetch", "--quiet", "origin", self.branch, timeout=180)
                            self._git_ok("checkout", "-q", "-f", "-B", self.branch, "origin/" + self.branch)
                            self._commit(files)
                        code, out, err = self._git("push", "--quiet", "origin", "HEAD:" + self.branch, timeout=180)
                        if code != 0:
                            raise RuntimeError("git push (tried twice): %s" % (err or out).strip().splitlines()[-1])
                head = self._git_ok("rev-parse", "HEAD").strip()
                at = iso(now_dt())
                with self.lock:
                    self.saved = {"digest": digest, "lastOk": at, "lastCommit": head if committed else
                                  self.saved.get("lastCommit") or head}
                    self.state, self.last_error, self.failing_since = "ok", None, None
                if self.state_path:
                    try:
                        write_atomic(self.state_path, dumps(self.saved, pretty=True) + "\n")
                    except OSError as e:
                        LOG.warning("docs sync: could not write %s: %s", self.state_path, e)
                LOG.info("docs sync: %s", "pushed %s" % short_sha(head) if committed else "already up to date")
            except (RuntimeError, OSError, ValueError) as e:
                with self.lock:
                    self.state, self.last_error = "failed", str(e)
                    self.failing_since = self.failing_since or self.last_attempt
                LOG.warning("docs sync failed: %s", e)
        return self.status()


def ruleset_item(d, default_branch, repo):
    """One ruleset for the page: normalized, a summary, and locked (with why) or its form."""
    n = ruleset_norm(d)
    reasons = ruleset_lock_reasons(n, default_branch)
    if d.get("source_type") not in (None, "Repository"):
        reasons.append("it belongs to the %s %s, not to this repository" % (
            str(d.get("source_type")).lower(), d.get("source")))
    return {"id": d.get("id"), "name": n["name"], "target": n["target"], "enforcement": n["enforcement"],
            "sourceType": d.get("source_type"), "createdAt": d.get("created_at"), "updatedAt": d.get("updated_at"),
            "url": ((d.get("_links") or {}).get("html") or {}).get("href") or
            "https://github.com/%s/rules/%s" % (repo, d.get("id")),
            "ruleset": n, "summary": ruleset_summary(n), "applies": ruleset_applies(n),
            "locked": bool(reasons), "lockReasons": reasons, "form": None if reasons else ruleset_form(n),
            "otherRules": [r["type"] for r in n["rules"] if r["type"] not in RULESET_MANAGED],
            "currentUserCanBypass": d.get("current_user_can_bypass")}


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
        editor = os.path.join(data_dir, "editor")
        self.notes = Notes(editor, self._editor_events, self.repo)
        self.structure = Structure(editor, self._editor_events)
        self.rules = Rules(editor, self._editor_events)
        self.pending = Pending(editor, self.gh, self.repo, self._editor_events, on_applied=self._after_apply)
        docs_on, docs_off = docs_sync_wanted(cfg)
        self.docs = DocsSync(os.path.join(data_dir, "claude-docs"), os.path.join(editor, "docs-sync.json"), self.proc,
                             self.docs_files, repo=cfg.get("claudeDocsRepo") or DOCS_REPO,
                             branch=cfg.get("claudeDocsBranch") or DOCS_BRANCH, enabled=docs_on, off_reason=docs_off,
                             adopt=self.adopt_docs)
        self.notes_held = None  # why the Claude Documents' notes are kept as they are (see adopt_docs)
        self.rs_lock = threading.Lock()
        self.rs_view = None     # the last GET /api/github/rulesets answer
        self.rs_at = 0.0
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
        self.runs_read = False  # the run list was read from GitHub since the start (self.runs may be the disk cache)
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
        self.restructure()      # the cached state's branches get their roles before the first poll

    def _p(self, *parts):
        return os.path.join(self.data, *parts)

    # -- the editor: structure, rules, pending GitHub changes, the Claude Documents copy -----------------------

    def _editor_events(self, events):
        """Every editor change goes to the history; rules and notes go to the Claude Documents too, and the
        structure and rules (the stale age) change the branches' roles in the served state."""
        self.history.append(events)
        kinds = set(e["type"].split(".")[0] for e in events)
        if kinds & {"note", "rules"}:
            self.docs.schedule()
        if kinds & {"structure", "rules"}:
            self.restructure()

    def stale_days(self):
        return self.rules.limit(STALE_RULE_ID, STALE_DAYS_DEFAULT)

    def restructure(self):
        """Re-applies roles / stale to the served branches after a structure or rules change (no poll needed)."""
        with self.lock:
            st = dict(self.state)
            st["branches"] = [dict(b) for b in st.get("branches") or []]
        branch_roles(st["branches"], self.structure, self.stale_days(), self.default_branch,
                     stale_scope=self.rules.scope(STALE_RULE_ID))
        with self.lock:
            if self.state.get("generatedAt") == st.get("generatedAt"):  # a poll in between applied it already
                self.state = st
                self.rev += 1
                self.bodies.clear()

    def docs_files(self):
        """The files the Claude Documents get: {name: text}. While the notes there are held (adopt_docs), only the
        rules: the notes files there are left as they are."""
        db = self.default_branch
        files = {"10-github-rules.md": self.rules.markdown(self.repo, db),
                 "github-rules.json": dumps(self.rules.export(self.repo, db), pretty=True) + "\n"}
        if not self.notes_held:
            with self.notes.lock:
                files["11-github-notes.md"] = self.notes.markdown(for_docs=True)
            files["github-notes.json"] = dumps(self.notes.export(self.repo), pretty=True) + "\n"
        return files

    def adopt_docs(self, folder, first=False):
        """Right before a push, on origin's files: a still-seeded rule set takes the Claude Documents' github-rules.json
        instead, and on the first sync from this data folder (or while this PC has written no note) the notes there
        are merged in, so a new PC or a lost data folder gets q's rules and notes back instead of pushing empty ones
        over them. Notes there that can't be read back are never replaced by this PC's empty store: they are held
        (left as they are, with a warning) until a note is written here."""
        if self.rules.seeded:
            doc = read_json(os.path.join(folder, "github-rules.json"))
            if doc is not None and self.rules.adopt(doc):
                LOG.info("editor: agent rules taken from the Claude Documents' github-rules.json")
        held = None
        if first or self.notes.fresh:
            raws, source, count = docs_notes(folder)
            if raws and self.notes.merge(raws, source):
                LOG.info("editor: notes merged from the Claude Documents' %s", source)
            if count and self.notes.fresh and not self.notes.all():
                held = ("Claude Documents: %s there holds %s this PC could not read back, so it is left as it is "
                        "(not replaced by this PC's empty notes) until a note is written here" % (
                            source, plural(count, "note")))
                LOG.warning("editor: %s", held)
        self.notes_held = held

    def _after_apply(self, change):
        with self.rs_lock:
            self.rs_at = 0.0    # the next rulesets read goes to GitHub
        if change.get("type") == "branch.rename" and change.get("status") == "applied":
            self.follow_rename(change["params"]["name"], change["params"]["newName"])
        self.wake.set()         # and the next poll runs now, so the branches show the change

    def follow_rename(self, old, new):
        follow_rename(self.structure, self.notes, old, new)

    def structure_view(self):
        o = self.structure.obj()
        o.update({"roles": list(ROLES), "settableRoles": list(SETTABLE_ROLES),
                  "protectedBranches": list(PROTECTED_BRANCHES), "defaultBranch": self.default_branch,
                  "staleDays": self.stale_days(), "staleScope": self.rules.scope(STALE_RULE_ID),
                  "staleRule": STALE_RULE_ID})
        return o

    def rules_view(self):
        o = self.rules.obj()
        docs = self.docs.status()
        docs["notesHeld"] = self.notes_held
        o.update({"kinds": list(RULE_KINDS), "scopes": list(RULE_SCOPES), "hardRule": HARD_RULE_ID,
                  "defaultBranch": self.default_branch, "docsSync": docs,
                  "limits": {"rules": RULES_MAX, "titleBytes": RULE_TITLE_MAX, "textBytes": RULE_TEXT_MAX}})
        return o

    def pending_view(self):
        o = self.pending.obj()
        o["defaultBranch"] = self.default_branch
        return o

    def check_names(self, items):
        """Status check names q can require: the job names of the recent runs, plus those rulesets already require."""
        names = set()
        with self.lock:
            runs = (self.state.get("runs") or [])[:100]
        for r in runs:
            for j in r.get("jobs") or []:
                if j.get("name"):
                    names.add(j["name"])
        for it in items:
            for r in it["ruleset"]["rules"]:
                if r["type"] == "required_status_checks":
                    for c in (r.get("parameters") or {}).get("required_status_checks") or []:
                        if c.get("context"):
                            names.add(c["context"])
        return sorted(names, key=lambda n: n.lower())

    def rulesets_view(self, fresh=False):
        """GET /api/github/rulesets: the repository's rulesets read from GitHub (each with ETags, so an unchanged one
        costs nothing), at most once a minute unless fresh; each marked locked (and why) or editable (its form)."""
        with self.rs_lock:
            if not fresh and self.rs_view is not None and time.time() - self.rs_at < RULESETS_TTL:
                return self.rs_view
            err, items, default = None, None, self.default_branch
            try:
                default = self.pending.default_branch()
                lst = self.gh.get("repos/%s/rulesets?per_page=100" % self.repo)[0] or []
                items = []
                for r in lst:
                    d = self.gh.get("repos/%s/rulesets/%d" % (self.repo, int(r["id"])))[0] or r
                    items.append(ruleset_item(d, default, self.repo))
            except (GhError, EditError) as e:
                err = str(e)
            except (KeyError, TypeError, ValueError) as e:
                err = "unexpected answer from GitHub: %s" % e
            if items is None:
                items = (self.rs_view or {}).get("rulesets") or []
            view = {"rulesets": items, "defaultBranch": default, "checks": self.check_names(items),
                    "fetchedAt": iso(now_dt()), "error": err, "lockedNames": list(LOCKED_RULESET_NAMES),
                    "protectedBranches": list(PROTECTED_BRANCHES)}
            self.rs_view, self.rs_at = view, time.time() if not err else 0.0
            return view

    def say(self, msg, level=logging.INFO):
        if not self.quiet or level >= logging.WARNING:
            LOG.log(level, msg)

    def _empty_state(self):
        return {"generatedAt": iso(now_dt()), "repo": self.repo, "defaultBranch": self.default_branch,
                "poll": {"lastOk": None, "lastError": None, "nextAt": None, "intervalSec": SLOW_SEC,
                         "rateRemaining": None},
                "production": None, "testing": None, "branches": [], "pulls": [], "runs": [], "local": [],
                "running": [], "releases": [], "notRead": list(NOT_READ_SECTIONS)}

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
        if "workflowFile" not in old and s.get("workflowFile"):  # cached before workflowFile existed
            old["workflowFile"] = s["workflowFile"]
            self._save_run(rid)
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
        self.runs_read = True
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
            if self.prev is None and any(k in state["notRead"] for k in NOT_READ_SECTIONS):
                # the one-time baseline waits for a poll that read GitHub: one written by a failed first poll
                # ("0 open PRs, 0 runs") would make the next good poll skip everything that happened in between
                fresh = []
            else:
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
            warnings += self.docs.warnings() + ([self.notes_held] if self.notes_held else [])
            self.docs.retry_if_failed()
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
            unrelated = False
            if t_in_mirror and h.get("sha"):
                try:
                    # no shared history (gh-pages): ahead / behind would just count both sides' every commit
                    unrelated = not self.mirror.related(testing_sha, h["sha"])
                    if not unrelated:
                        ahead, behind = self.mirror.ahead_behind(testing_sha, h["sha"])
                except (RuntimeError, ValueError):
                    pass
            pr = pr_by_head.get(name)
            if pr and pr["state"] != "open" and pr.get("headSha") != h.get("sha"):
                pr = None  # a closed PR is the branch's only while the branch is still where the PR left it
            lr = last_by_branch.get(name)
            branches.append({"name": name, "sha": h.get("sha"), "date": h.get("date"), "subject": h.get("subject"),
                             "author": h.get("author"), "kind": branch_kind(name, self.default_branch),
                             "ahead": ahead, "behind": behind, "unrelated": unrelated,
                             "pr": pr["number"] if pr else None,
                             "lastRun": dict(lr) if lr else None,
                             "url": "https://github.com/%s/tree/%s" % (repo, urllib.parse.quote(name, safe="/"))})
        branch_roles(branches, self.structure, self.stale_days(), self.default_branch,
                     stale_scope=self.rules.scope(STALE_RULE_ID))
        order = {"production": 0, "testing": 1}
        branches.sort(key=lambda b: (b["date"] or ""), reverse=True)
        branches.sort(key=lambda b: order.get(b["kind"], 2))

        # what GitHub hasn't answered yet since the start: the page shows those parts as unknown, not as "none"
        not_read = [k for k, unread in (("pulls", self.pulls_open is None), ("releases", self.releases is None),
                                        ("runs", not self.runs_read)) if unread]
        return {"generatedAt": at, "repo": repo, "defaultBranch": self.default_branch, "poll": {},
                "production": production, "testing": testing, "branches": branches, "pulls": pulls, "runs": runs,
                "local": local, "running": running, "releases": releases, "notRead": not_read,
                "shell": os.environ.get("QUARK_SHELL") == "1"}

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
                "production": {"tag": p.get("tag"), "version": p.get("version"), "sha": p.get("sha"),
                               "publishedAt": p.get("publishedAt")},
                "openPulls": [{"number": x.get("number"), "title": x.get("title"), "head": x.get("head"),
                               "base": x.get("base"), "draft": bool(x.get("draft")), "updatedAt": x.get("updatedAt"),
                               "checks": {"state": (x.get("checks") or {}).get("state")}, "url": x.get("url")}
                              for x in state.get("pulls") or [] if x.get("state") == "open"],
                "notRead": state.get("notRead") or [],
                "running": state.get("running") or [], "lastFailed": failed,
                "lastTesting": last_testing_run(state.get("runs") or []),
                "local": [{"name": l.get("name"), "branch": l.get("branch"), "aheadOfTesting": l.get("aheadOfTesting"),
                           "behindTesting": l.get("behindTesting"), "dirty": l.get("dirty")}
                          for l in state.get("local") or []]}

    # -- background thread -------------------------------------------------------------------------------------

    def loop(self):
        try:
            self.docs.schedule_if_changed()
        except Exception:
            LOG.exception("docs sync: start check failed")
        while not self.stop.is_set():
            try:
                self.poll()
            except Exception as e:  # never let the thread die: log it and poll again later
                self.last_error = "poll crashed: %r" % e
                LOG.exception("poll crashed (will poll again in %d s)", self.interval)
                self.next_at = time.time() + self.interval
            try:
                wait = max(1.0, (self.next_at or time.time() + self.interval) - time.time())
                self.wake.wait(wait)
                self.wake.clear()
            except Exception:
                LOG.exception("poll thread wait failed")
                time.sleep(5)

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
            self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
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
            if path == "/api/notes":
                tag, body = m.notes.view()
                return self._cached("no-" + tag, lambda: body)
            views = {"/api/structure": ("sr", m.structure_view), "/api/rules": ("ru", m.rules_view),
                     "/api/pending": ("pe", m.pending_view)}
            if path in views:
                prefix, make = views[path]
                body = dumps(make())
                return self._cached("%s-%s" % (prefix, hashlib.sha1(body.encode("utf-8")).hexdigest()[:16]),
                                    lambda: body)
            if path == "/api/github/rulesets":
                fresh = (q.get("fresh") or [""])[0] in ("1", "true", "yes")
                return self._json(200, m.rulesets_view(fresh))
            if path.startswith("/api/"):
                return self._json(404, {"error": "no such endpoint"})
            return self._static(path)
        except (BrokenPipeError, ConnectionResetError):
            pass

    WRITE_PATHS = ("/api/notes", "/api/structure", "/api/rules", "/api/pending")

    @classmethod
    def is_write_path(cls, path):
        return any(path == p or path.startswith(p + "/") for p in cls.WRITE_PATHS)

    def do_PUT(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain")
        path = urllib.parse.urlsplit(self.path).path
        if path == "/api/notes" or not self.is_write_path(path):
            self.close_connection = True
            return self._json(404, {"error": "no such endpoint"})
        self._write()

    def do_DELETE(self):
        self.do_PUT()

    def _write(self):
        """Every change the page makes (notes, structure, rules, pending GitHub changes): only from the page's own
        origin (or the Supervisor Board's), only with a JSON body (Content-Type: application/json), at most 64 KB
        (256 KB for the structure and the rules), only known fields."""
        path = urllib.parse.urlsplit(self.path).path
        limit = EDIT_BODY_MAX if path in ("/api/rules", "/api/structure") else NOTE_BODY_MAX
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = -1
        if n < 0 or n > limit:
            self.close_connection = True  # the body is not read
            return self._json(413 if n > 0 else 400, {"error": "the body must be at most %d KB" % (limit // 1024)})
        raw = self.rfile.read(n) if n else b""
        if not write_origin_ok(self.headers, self.server.server_address[1]):
            what = "notes" if path.startswith("/api/notes") else "the editor"
            return self._json(403, {"error": "%s can only be changed from the GitHub manager's own page" % what})
        if not json_content_type(self.headers.get("Content-Type")):
            return self._json(415, {"error": "send JSON (Content-Type: application/json)"})
        try:
            body = json.loads(raw.decode("utf-8")) if raw.strip() else None
        except (ValueError, UnicodeDecodeError):
            return self._json(400, {"error": "the body is not valid JSON"})
        if path.startswith("/api/notes"):
            return self._notes_write(path, body)
        m, cmd = self.manager, self.command
        try:
            if path == "/api/structure":
                if cmd != "PUT":
                    return self._json(405, {"error": "PUT the structure"})
                m.structure.update(body, m.default_branch)
                return self._json(200, m.structure_view())
            if path == "/api/rules":
                if cmd != "PUT":
                    return self._json(405, {"error": "PUT the rules"})
                m.rules.replace(body)
                return self._json(200, m.rules_view())
            if path == "/api/pending":
                if cmd != "POST":
                    return self._json(405, {"error": "POST a change here; DELETE / apply go to /api/pending/<id>"})
                return self._json(201, {"change": m.pending.queue(body)})
            if path == "/api/pending/apply-all":
                if cmd != "POST":
                    return self._json(405, {"error": "POST to apply every pending change"})
                return self._json(200, m.pending.apply_all(body))
            mm = re.match(r"^/api/pending/([^/]{1,40})(/apply|/preview)?$", path)
            if not mm or not CHANGE_ID_RE.match(mm.group(1)):
                return self._json(404, {"error": "no such pending change"})
            cid, act = mm.group(1), mm.group(2)
            if act is None and cmd == "DELETE":
                if body not in (None, {}):
                    return self._json(400, {"error": "DELETE takes no fields"})
                return self._json(200, {"discarded": m.pending.discard(cid)})
            if act == "/preview" and cmd == "POST":
                edit_fields(body if body is not None else {}, ())
                return self._json(200, {"change": m.pending.repreview(cid)})
            if act == "/apply" and cmd == "POST":
                ch = m.pending.apply(cid, body)
                code = {"applied": 200, "stale": 409, "refused": 422}.get(ch["status"], 502)
                out = {"change": ch}
                if code != 200:
                    out["error"] = (ch.get("result") or {}).get("error") or ch["status"]
                    out["locked"] = ch["status"] == "refused"
                return self._json(code, out)
            return self._json(405, {"error": "DELETE /api/pending/<id>, POST /api/pending/<id>/apply or /preview"})
        except EditError as e:
            out = {"error": str(e)}
            if e.locked:
                out["locked"] = True
            if e.change is not None:
                out["change"] = e.change
            return self._json(e.code, out)
        except NoteError as e:
            return self._json(e.code, {"error": str(e)})

    def _notes_write(self, path, body):
        """POST /api/notes, PUT /api/notes/<id>, DELETE /api/notes/<id> (the checks in _write already passed)."""
        notes = self.manager.notes
        cmd = self.command
        try:
            if path == "/api/notes":
                if cmd != "POST":
                    return self._json(405, {"error": "POST a new note here; PUT / DELETE go to /api/notes/<id>"})
                return self._json(201, {"note": notes.create(body)})
            nid = path[len("/api/notes/"):]
            if not NOTE_ID_RE.match(nid):
                return self._json(404, {"error": "no note %s" % nid[:40]})
            if cmd == "PUT":
                return self._json(200, {"note": notes.update(nid, body)})
            if cmd == "DELETE":
                if body not in (None, {}):
                    return self._json(400, {"error": "DELETE takes no fields"})
                return self._json(200, {"deleted": notes.delete(nid)})
            return self._json(405, {"error": "PUT or DELETE a note here"})
        except NoteError as e:
            return self._json(e.code, {"error": str(e)})

    def do_POST(self):
        if not self._host_ok():
            return self._send(403, "forbidden", "text/plain")
        upath = urllib.parse.urlsplit(self.path).path
        if upath == "/api/notes" or upath.startswith("/api/notes/") or upath == "/api/pending" or \
                upath.startswith("/api/pending/"):
            return self._write()
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

    def handle_error(self, request, client_address):
        # the default prints to sys.stderr, which is None under pythonw
        if sys.exc_info()[0] in (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            return
        LOG.exception("error answering %s", client_address[0] if client_address else "?")
    allow_reuse_address = not WIN  # on Windows SO_REUSEADDR would let a second copy share the port


def make_server(manager, port):
    """Binds 127.0.0.1:port (OSError when busy). manager may be set later: server.RequestHandlerClass.manager."""
    handler = type("BoundHandler", (Handler,), {"manager": manager})
    return Server(("127.0.0.1", port), handler)


def already_running(port, timeout=2):
    import urllib.request
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/summary" % port, timeout=timeout) as r:
            return r.status == 200
    except Exception:
        return False


def port_busy(e):
    return e.errno in (errno.EADDRINUSE, errno.EACCES, getattr(errno, "WSAEADDRINUSE", -1), 10048, 10013) or \
        "in use" in str(e).lower()


def setup_logging(data, console=True):
    """<data>/server.log (rotating), plus the console when there is one (pythonw has no stdout / stderr)."""
    for name in ("stdout", "stderr"):
        if getattr(sys, name) is None:  # pythonw: make stray prints harmless
            setattr(sys, name, open(os.devnull, "w", encoding="utf-8"))
    LOG.setLevel(logging.INFO)
    LOG.propagate = False
    for h in list(LOG.handlers):
        LOG.removeHandler(h)
        h.close()
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s", "%Y-%m-%d %H:%M:%S")
    try:
        fh = logging.handlers.RotatingFileHandler(os.path.join(data, "server.log"), maxBytes=LOG_BYTES,
                                                  backupCount=3, encoding="utf-8", delay=False)
        fh.setFormatter(fmt)
        LOG.addHandler(fh)
    except OSError:
        pass
    if console and getattr(sys.stdout, "name", "") != os.devnull:
        ch = logging.StreamHandler(sys.stdout)
        ch.setFormatter(logging.Formatter("[%(asctime)s] %(message)s", "%H:%M:%S"))
        LOG.addHandler(ch)

    def thread_hook(args):
        if args.exc_type is not SystemExit:
            LOG.error("thread %s died", args.thread.name if args.thread else "?",
                      exc_info=(args.exc_type, args.exc_value, args.exc_traceback))
    threading.excepthook = thread_hook


def wants_browser(a):
    """--open, unless the Quark Project app runs us (QUARK_SHELL=1): it shows the page itself."""
    return bool(a.open) and os.environ.get("QUARK_SHELL") != "1"


class Stop(Exception):
    pass


def main(argv=None):
    try:
        return _main(argv)
    except SystemExit:
        raise
    except BaseException:
        LOG.exception("stopped: unexpected error")
        return 1


def _main(argv=None):
    ap = argparse.ArgumentParser(description="Quark GitHub manager (read-only): http://127.0.0.1:%d/" % DEFAULT_PORT)
    ap.add_argument("--data", default=None, help="output folder (default %%USERPROFILE%%\\Desktop\\Quark\\github-manager)")
    ap.add_argument("--port", type=int, default=None, help="port (default: github-manager.json, else %d)" % DEFAULT_PORT)
    ap.add_argument("--open", action="store_true", help="open the page in the default browser")
    ap.add_argument("--once", action="store_true", help="poll once, write the files and exit (no server)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    data = os.path.abspath(os.path.expanduser(a.data or default_data_dir()))
    os.makedirs(data, exist_ok=True)
    setup_logging(data, console=not a.quiet)
    cfg = load_config(data)
    port = a.port or int(cfg.get("port") or DEFAULT_PORT)
    static = os.path.dirname(os.path.abspath(__file__))
    url = "http://127.0.0.1:%d/" % port
    if a.once:
        Manager(cfg, data, static, quiet=a.quiet).poll(manual=True)
        return 0
    # bind first: a second copy leaves at once, before loading any cache
    try:
        srv = make_server(None, port)
    except OSError as e:
        if port_busy(e):
            up = already_running(port)
            LOG.warning("not starting: port %d is already in use%s (exit %d)", port,
                        " by a running GitHub manager" if up else " by another program", EXIT_PORT_BUSY)
            if up and wants_browser(a):
                webbrowser.open(url)
            return EXIT_PORT_BUSY
        LOG.error("not starting: cannot listen on 127.0.0.1:%d: %s", port, e)
        return 1
    LOG.info("starting (pid %d, Python %s, %s) for %s at %s, data %s", os.getpid(), sys.version.split()[0],
             os.path.basename(sys.executable or "python"), cfg.get("repo"), url, data)
    try:
        m = Manager(cfg, data, static, quiet=a.quiet)
    except BaseException:
        srv.server_close()
        raise
    srv.RequestHandlerClass.manager = m
    reason = ["the server loop ended"]

    def on_signal(signum, frame):
        try:
            reason[0] = "signal %s" % signal.Signals(signum).name
        except ValueError:
            reason[0] = "signal %s" % signum
        raise Stop()
    for sig in ("SIGTERM", "SIGINT", "SIGBREAK", "SIGHUP"):
        if hasattr(signal, sig):
            try:
                signal.signal(getattr(signal, sig), on_signal)
            except (OSError, ValueError):
                pass
    t = threading.Thread(target=m.loop, name="poll", daemon=True)
    t.start()
    if wants_browser(a):
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    elif a.open:
        LOG.info("not opening the browser (QUARK_SHELL=1)")
    try:
        srv.serve_forever(poll_interval=1.0)
    except (Stop, KeyboardInterrupt):
        if reason[0] == "the server loop ended":
            reason[0] = "Ctrl+C"
    except BaseException as e:
        reason[0] = "error: %r" % e
        LOG.exception("server loop crashed")
        raise
    finally:
        m.stop.set()
        m.wake.set()
        srv.server_close()
        LOG.info("stopped (%s)", reason[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())

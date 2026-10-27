"""WIP sync: Quark\\quarkology-repo\\wip\\ is the same on q's PCs (SPEC.md 2, "WIP sync"; wip/README.md).

On app open and every INTERVAL s (and POST /api/wip/sync), in one background thread, hidden, with the PC's own git
sign-in (or the gh one the clone was made with):
  1. Commit what changed under wip\\ (new, changed and deleted files; nothing outside wip\\, ever; files over
     MAX_FILE_MB are left out and named) on main, with a message naming this PC. A temporary index is used, so
     whatever q staged by hand stays staged and the commit holds wip\\ only.
  2. `git fetch origin main`. Behind only: fast-forward. Both sides moved: when the PC's own commits touch wip\\
     only, they become one commit on top of origin/main: the other PC's files win their own paths and every file both
     changed keeps the other side at its name and this PC's version next to it as
     <name>.conflict-<machine>-<YYYYMMDD-HHMMSS><.ext> (no side is ever lost). The work tree moves with
     `git reset --keep` (it refuses rather than overwrite a file changed meanwhile).
  3. `git push origin HEAD:main` (once more after a fresh fetch when someone pushed in between).
It never resets, rebases or force-pushes anything else: on another branch, during a merge or rebase, with tracked
changes outside wip\\ in the way, or with commits outside wip\\ not pushed, it stops and says so on the Board. Open
conflicts (files under wip\\ named *.conflict-*) are a red item on the Board until q deletes or renames them.
"""
import os
import re
import socket
import threading
import time
import traceback

from . import config, util
from .updater import APP_REPO_LOCK, FETCH_TIMEOUT, _first_line, git

INTERVAL = 180.0          # seconds between syncs (and at app open)
MIN_GAP = 10.0            # "Sync now" at most this often
MAX_FILE_MB = 90          # GitHub refuses files over 100 MB
WIP = "wip"
CONFLICT_RE = re.compile(r"\.conflict-[A-Za-z0-9_-]+-\d{8}-\d{6}(\.|$)")
PUSH_TIMEOUT = 300


def machine_name():
    """This PC's name for commit messages and conflict copies: COMPUTERNAME / the host name, letters, digits, - and _."""
    n = os.environ.get("COMPUTERNAME") or socket.gethostname() or "pc"
    n = re.sub(r"[^A-Za-z0-9_-]+", "-", n.split(".")[0]).strip("-")[:32]
    return n or "pc"


def conflict_name(path, machine, when=None):
    """wip/maps/a.qmap -> wip/maps/a.conflict-<machine>-<YYYYMMDD-HHMMSS>.qmap (local time)."""
    d, base = os.path.split(path) if "/" not in path else path.rsplit("/", 1)
    stem, ext = os.path.splitext(base)
    if not stem:  # (a dot file: .gitkeep)
        stem, ext = base, ""
    tag = time.strftime("%Y%m%d-%H%M%S", time.localtime(when or time.time()))
    name = "%s.conflict-%s-%s%s" % (stem, machine, tag, ext)
    return d + "/" + name if d else name


def _z(out):
    return [p for p in out.split("\0") if p]


class SyncError(Exception):
    pass


class WipSync:
    def __init__(self, paths, enabled=True, interval=INTERVAL, machine=None):
        self.paths = paths
        self.enabled = enabled
        self.interval = interval
        self.machine = machine or machine_name()
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.stop = threading.Event()
        self.running = False
        self.last_request = -1e18
        self.thread = None
        self.state = {"state": "waiting", "note": None, "error": None, "lastRunAt": None, "lastOkAt": None,
                      "lastPushAt": None, "committed": 0, "pulled": 0, "pushed": 0, "conflicts": [], "skipped": [],
                      "newConflicts": [], "head": None}

    # -- API ---------------------------------------------------------------------------------------------------------
    def view(self):
        with self.lock:
            v = dict(self.state)
            v.update(enabled=self.enabled, running=self.running, machine=self.machine, interval=self.interval,
                     folder=os.path.join(self.paths.app_repo, WIP))
            return v

    def request(self, now=None):
        """Sync soon (the Board's Sync now). -> (accepted, seconds until allowed)."""
        now = now or time.time()
        with self.lock:
            if not self.enabled:
                return False, None
            wait = self.last_request + MIN_GAP - now
            if wait > 0:
                return False, round(wait, 1)
            self.last_request = now
        self.wake.set()
        return True, 0.0

    def _put(self, **kw):
        with self.lock:
            self.state.update(kw)

    # -- one sync ----------------------------------------------------------------------------------------------------
    def sync_once(self):
        with self.lock:
            self.running = True
        try:
            with APP_REPO_LOCK:
                res = self._sync()
        except SyncError as e:
            res = {"state": "error", "error": str(e)}
        except Exception:  # (never let the thread die)
            util.log("wipsync: %s" % traceback.format_exc(limit=4))
            res = {"state": "error", "error": "internal error (see the server log)"}
        finally:
            with self.lock:
                self.running = False
        now = util.now_iso()
        res.setdefault("error", None)
        res.setdefault("note", None)
        res["lastRunAt"] = now
        if res.get("state") == "ok":
            res["lastOkAt"] = now
            if res.get("pushed"):
                res["lastPushAt"] = now
        res["conflicts"] = self.conflicts()
        self._put(**res)
        return self.view()

    def conflicts(self):
        """The conflict copies still under wip\\ (tracked or not)."""
        root = os.path.join(self.paths.app_repo, WIP)
        out = []
        for d, dirs, files in os.walk(root):
            dirs[:] = [x for x in dirs if x != ".git"]
            for f in files:
                if CONFLICT_RE.search(f):
                    out.append(os.path.relpath(os.path.join(d, f), self.paths.app_repo).replace(os.sep, "/"))
        return sorted(out)[:200]

    def _g(self, *args, timeout=60.0, env=None, input=None, ok=(0,)):
        code, out, err = git(self.paths.app_repo, *args, timeout=timeout, env=env, input=input)
        if code not in ok:
            raise SyncError("git %s failed: %s" % (args[0], _first_line(err) or "exit %d" % code))
        return out

    def _identity(self):
        """An author for the WIP commits when this PC has no git identity (never changes q's git config)."""
        name = git(self.paths.app_repo, "config", "user.name")[1].strip()
        mail = git(self.paths.app_repo, "config", "user.email")[1].strip()
        env = {}
        if not name:
            env.update(GIT_AUTHOR_NAME="Quarkology WIP sync (%s)" % self.machine,
                       GIT_COMMITTER_NAME="Quarkology WIP sync (%s)" % self.machine)
        if not mail:
            env.update(GIT_AUTHOR_EMAIL="wip-sync@%s.invalid" % self.machine.lower(),
                       GIT_COMMITTER_EMAIL="wip-sync@%s.invalid" % self.machine.lower())
        return env

    def _sync(self):
        repo = self.paths.app_repo
        if not os.path.isdir(os.path.join(repo, ".git")):
            return {"state": "waiting", "note": "Quark\\%s\\ isn't cloned yet (Tools & updates clones it)" % config.APP_SOURCE_CLONE}
        gitdir = os.path.join(repo, ".git")
        for busy in ("rebase-merge", "rebase-apply", "MERGE_HEAD", "CHERRY_PICK_HEAD"):
            if os.path.exists(os.path.join(gitdir, busy)):
                raise SyncError("a git %s is in progress in quarkology-repo\\: finish or abort it; WIP sync waits"
                                % busy.split("-")[0].split("_")[0].lower())
        branch = self._g("rev-parse", "--abbrev-ref", "HEAD").strip()
        if branch != config.APP_BRANCH:
            raise SyncError("quarkology-repo\\ is on %s, not %s: WIP sync waits" % (branch, config.APP_BRANCH))
        os.makedirs(os.path.join(repo, WIP), exist_ok=True)
        ident = self._identity()
        res = {"state": "ok", "committed": 0, "pulled": 0, "pushed": 0, "skipped": [], "newConflicts": []}
        res["committed"], res["skipped"] = self._commit_local(ident)
        for attempt in (1, 2):
            pulled, new_conflicts = self._integrate(ident)
            res["pulled"] += pulled
            res["newConflicts"] += new_conflicts
            ahead = int(self._g("rev-list", "--count", "origin/%s..HEAD" % config.APP_BRANCH).strip() or 0)
            if not ahead:
                break
            code, _, err = git(repo, "push", "--quiet", "origin", "HEAD:" + config.APP_BRANCH, timeout=PUSH_TIMEOUT)
            if code == 0:
                res["pushed"] = ahead
                break
            if attempt == 2 or not re.search(r"rejected|non-fast-forward|fetch first", err or "", re.I):
                raise SyncError("push failed: %s" % (_first_line(err) or "exit %d" % code))
        res["head"] = self._g("rev-parse", "HEAD").strip()
        if res["skipped"]:
            res["note"] = "not synced (over %d MB): %s" % (MAX_FILE_MB, ", ".join(res["skipped"][:5]))
        return res

    def _commit_local(self, ident):
        """Step 1. -> (files committed, files left out for their size)."""
        repo = self.paths.app_repo
        out = self._g("status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames", "--", WIP)
        paths, skipped = [], []
        for rec in _z(out):
            p = rec[3:]
            if not p.startswith(WIP + "/"):
                continue  # (never anything outside wip/)
            full = os.path.join(repo, *p.split("/"))
            try:
                if os.path.isfile(full) and os.path.getsize(full) > MAX_FILE_MB << 20:
                    skipped.append(p)
                    continue
            except OSError:
                continue
            paths.append(p)
        if not paths:
            return 0, skipped
        index = os.path.join(repo, ".git", "quarkology-wip.index")
        env = dict(ident, GIT_INDEX_FILE=index)
        try:
            head = self._g("rev-parse", "HEAD").strip()
            self._g("read-tree", "HEAD", env=env)
            self._g("add", "-A", "--pathspec-from-file=-", "--pathspec-file-nul", env=env,
                    input=("\0".join(":(literal)" + p for p in paths) + "\0").encode("utf-8"))
            tree = self._g("write-tree", env=env).strip()
            if tree == self._g("rev-parse", "HEAD^{tree}").strip():
                return 0, skipped
            names = [p[len(WIP) + 1:] for p in paths]
            msg = "WIP sync from %s: %d file%s (%s%s)" % (self.machine, len(paths), "" if len(paths) == 1 else "s",
                                                          ", ".join(names[:5]), ", ..." if len(names) > 5 else "")
            commit = self._g("commit-tree", tree, "-p", head, "-m", msg, env=env).strip()
            self._g("update-ref", "-m", "wip sync: commit", "HEAD", commit, head)
        finally:
            try:
                os.remove(index)
            except OSError:
                pass
        # (the real index follows for those paths, so `git status` is clean there; nothing else is touched)
        self._g("reset", "-q", "HEAD", "--pathspec-from-file=-", "--pathspec-file-nul",
                input=("\0".join(":(literal)" + p for p in paths) + "\0").encode("utf-8"))
        return len(paths), skipped

    def _integrate(self, ident):
        """Step 2. -> (commits pulled, conflict copies made)."""
        repo = self.paths.app_repo
        br = config.APP_BRANCH
        code, _, err = git(repo, "fetch", "--quiet", "--no-tags", "origin", br, timeout=FETCH_TIMEOUT)
        if code != 0:
            raise SyncError("fetch failed: %s" % (_first_line(err) or "exit %d" % code))
        theirs = "origin/" + br
        ahead, behind = (int(x) for x in self._g("rev-list", "--left-right", "--count", "HEAD..." + theirs).split())
        if not behind:
            return 0, []
        dirty = self._g("status", "--porcelain", "--untracked-files=no", "--", ".", ":(exclude)" + WIP).strip()
        if dirty:
            raise SyncError("quarkology-repo\\ has changes outside wip\\ (%s): not pulled; commit or undo them"
                            % ", ".join(l[3:] for l in dirty.splitlines()[:3]))
        if not ahead:
            code, _, err = git(repo, "merge", "--ff-only", "--quiet", theirs, timeout=120)
            if code != 0:
                raise SyncError("fast-forward refused: %s" % (_first_line(err) or "exit %d" % code))
            return behind, []
        base = self._g("merge-base", "HEAD", theirs).strip()
        mine = _z(self._g("diff", "--name-only", "-z", "--no-renames", base, "HEAD"))
        other = set(_z(self._g("diff", "--name-only", "-z", "--no-renames", base, theirs)))
        outside = [p for p in mine if not p.startswith(WIP + "/")]
        if outside:
            raise SyncError("this PC's main has commits outside wip\\ (%s) that aren't pushed: push them (branch + "
                            "PR) or drop them; WIP sync waits" % ", ".join(outside[:3]))
        now = time.time()
        made = []
        index = os.path.join(repo, ".git", "quarkology-wip.index")
        env = dict(ident, GIT_INDEX_FILE=index)
        try:
            self._g("read-tree", theirs, env=env)
            for p in mine:
                local = self._blob("HEAD", p)
                if p in other:
                    remote = self._blob(theirs, p)
                    if local is None or local == remote:
                        continue           # (deleted here, or the same: theirs stays)
                    if remote is not None:  # both changed it: theirs at its name, mine next to it
                        p_conf = conflict_name(p, self.machine, now)
                        self._g("update-index", "--add", "--cacheinfo", "%s,%s,%s" % (local[0], local[1], p_conf), env=env)
                        made.append(p_conf)
                        continue
                if local is None:
                    self._g("update-index", "--force-remove", "--", p, env=env)
                else:
                    self._g("update-index", "--add", "--cacheinfo", "%s,%s,%s" % (local[0], local[1], p), env=env)
            tree = self._g("write-tree", env=env).strip()
            log = self._g("log", "--format=%s", "%s..HEAD" % theirs).strip().splitlines()
            msg = "WIP sync from %s: %s%s" % (self.machine, "; ".join(l.split(": ", 1)[-1] for l in log[:3]) or "merged",
                                              (" (%d conflict copies: %s)" % (len(made), ", ".join(made[:3]))) if made else "")
            commit = self._g("commit-tree", tree, "-p", theirs, "-m", msg, env=env).strip()
        finally:
            try:
                os.remove(index)
            except OSError:
                pass
        code, _, err = git(repo, "reset", "--keep", "--quiet", commit, timeout=120)
        if code != 0:
            raise SyncError("could not bring wip\\ up to date (a file changed meanwhile?): %s; the next sync tries again"
                            % (_first_line(err) or "exit %d" % code))
        return behind, made

    def _blob(self, rev, path):
        """(mode, id) of path at rev, or None."""
        out = self._g("ls-tree", "-z", rev, "--", path)
        for rec in _z(out):
            meta, _, name = rec.partition("\t")
            parts = meta.split()
            if name == path and len(parts) == 3 and parts[1] == "blob":
                return parts[0], parts[2]
        return None

    # -- thread ------------------------------------------------------------------------------------------------------
    def loop(self):
        while not self.stop.is_set():
            v = self.sync_once()
            # (no clone yet: look again soon, so the first sync follows the updater's clone within seconds)
            self.wake.wait(min(self.interval, 15.0) if v.get("state") == "waiting" else self.interval)
            self.wake.clear()

    def start(self):
        if not self.enabled:
            return
        self.thread = threading.Thread(target=self.loop, name="pm-wipsync", daemon=True)
        self.thread.start()

    def shutdown(self):
        self.stop.set()
        self.wake.set()

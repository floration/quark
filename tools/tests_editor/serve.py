#!/usr/bin/env python3
"""Quark tests editor: a local server that shows every test (quark_tests groups and commands, CI jobs, scripts,
hooks, ctest), when and how each one runs, and lets you edit their files and run them, at http://127.0.0.1:8775/
(the page: web/ beside this file). The data contract is API.md.

    py -3 serve.py [--port 8775] [--repo <quark working copy>] [--no-browser] [--config tests_editor.json]
    py -3 serve.py --snapshot model.json        (write the model once and exit)

It reads and writes one working copy of the quark repo (tests_editor.json `repo`, default ..\\working-repo next to
this folder, else the repo this file sits in). Saving writes the file only: no git commands that change anything.
Runs are one at a time, at low priority, with their output in cache/runs/<id>.log. Logs: cache/serve.log.
Python 3 standard library only (Windows `py -3` / `pyw -3`, Linux python3).
"""
import argparse, datetime, glob, gzip, hashlib, http.server, json, os, re, shutil, signal, subprocess, sys
import threading, time, urllib.request, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import model as modelmod  # noqa: E402

VERSION = "1.0"
DEFAULT_PORT = 8775
CORS_PORTS = range(8765, 8776)
MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
        ".mjs": "text/javascript; charset=utf-8", ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml",
        ".png": "image/png", ".ico": "image/x-icon", ".woff2": "font/woff2", ".woff": "font/woff", ".ttf": "font/ttf",
        ".map": "application/json", ".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8"}
TEXT_EXT = {".cpp", ".h", ".hpp", ".c", ".cc", ".txt", ".md", ".json", ".py", ".sh", ".cmake", ".yml", ".yaml"}
MAX_BODY = 8 << 20
ARG_OK = re.compile(r"^[\w.:,+-]{1,64}$")
MAX_CHUNK = 512 << 10
BACKUPS_KEPT = 50
RUNS_KEPT = 500
CLOUD_TIMING = "/mnt/project-files/release-tests/timing"
WIN = os.name == "nt"
NO_WINDOW = 0x08000000
BELOW_NORMAL = 0x00004000
NEW_GROUP = 0x00000200


# ---- logging: cache/serve.log (never a console window; also stderr when there is a terminal) ------------------

class Log:
    def __init__(self):
        self.f, self.tty, self.lock = None, False, threading.Lock()

    def open(self, path, quiet):
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if os.path.isfile(path) and os.path.getsize(path) > (2 << 20): os.replace(path, path + ".1")
            self.f = open(path, "a", encoding="utf-8", buffering=1)
        except OSError:
            self.f = None
        self.tty = not quiet and sys.stderr is not None and getattr(sys.stderr, "isatty", lambda: False)()

    def __call__(self, msg):
        line = "[tests-editor %s] %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), msg)
        with self.lock:
            for out in (self.f, sys.stderr if self.tty else None):
                if out is None: continue
                try:
                    out.write(line); out.flush()
                except (UnicodeEncodeError, OSError, ValueError):
                    try: out.write(line.encode("ascii", "replace").decode())
                    except Exception: pass


log = Log()


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def atomic_write(path, data, mode=None):
    """Write bytes to path through a temp file + rename (keeps the old file's permission bits)."""
    d = os.path.dirname(path) or "."
    tmp = os.path.join(d, ".%s.%d.tests-editor.tmp" % (os.path.basename(path), os.getpid()))
    try:
        if mode is None and os.path.exists(path): mode = os.stat(path).st_mode & 0o7777
        with open(tmp, "wb") as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        if mode is not None and not WIN: os.chmod(tmp, mode)
        for attempt in range(5):                                        # (Windows: an editor or scanner may hold it)
            try:
                os.replace(tmp, path); return
            except PermissionError:
                if attempt == 4: raise
                time.sleep(0.1 * (attempt + 1))
    finally:
        if os.path.exists(tmp):
            try: os.remove(tmp)
            except OSError: pass


# ---- config --------------------------------------------------------------------------------------------------

def load_config(path):
    try:
        with open(path, encoding="utf-8") as f: c = json.load(f)
        return c if isinstance(c, dict) else {}
    except FileNotFoundError:
        return {}
    except (OSError, ValueError) as e:
        log("config %s unreadable (%s): using defaults" % (path, e))
        return {}


def resolve_repo(arg, cfg, cfg_dir):
    """--repo, else config `repo` (relative to the config's folder), else ..\\working-repo, else the repo around us."""
    cands = []
    if arg: cands.append(os.path.abspath(arg))
    if cfg.get("repo"): cands.append(os.path.abspath(os.path.join(cfg_dir, os.path.expanduser(cfg["repo"]))))
    cands.append(os.path.abspath(os.path.join(HERE, "..", "working-repo")))
    cands.append(os.path.abspath(os.path.join(HERE, "..", "..")))
    for c in cands:
        if os.path.isfile(os.path.join(c, "tests", "tests.cpp")): return c
    return cands[0]


# ---- files: the allowlist -------------------------------------------------------------------------------------

def allowed_rel(rel):
    """The normalized repo-relative path if it may be read / written, else None."""
    if not isinstance(rel, str) or not rel or len(rel) > 400 or "\0" in rel or "\\" in rel: return None
    if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel) or rel.startswith("~"): return None
    parts = rel.split("/")
    if any(p in ("", ".", "..") or p != p.strip() for p in parts): return None
    ext = os.path.splitext(parts[-1])[1].lower()
    if parts[0] == "tests" and len(parts) >= 2 and ext in TEXT_EXT: return rel
    if parts[:2] == [".github", "workflows"] and len(parts) == 3 and ext in (".yml", ".yaml"): return rel
    if parts[0] == "tools" and len(parts) == 2 and ext == ".sh": return rel
    if parts[:2] == ["tools", "githooks"] and len(parts) == 3: return rel
    if rel == "CMakeLists.txt": return rel
    return None


def repo_path(repo, rel):
    """Absolute path of an allowed rel inside repo, or None (outside the allowlist, or through a symlink)."""
    rel = allowed_rel(rel)
    if not rel: return None
    root = os.path.realpath(repo)
    cur = root
    for p in rel.split("/"):
        cur = os.path.join(cur, p)
        if os.path.islink(cur): return None
    full = os.path.realpath(cur)
    if not full.startswith(root + os.sep): return None
    return full


# ---- runs -----------------------------------------------------------------------------------------------------

def find_binary(repo, cfg_bin=None):
    exe = "quark_tests.exe" if WIN else "quark_tests"
    p = os.path.join(repo, "build", exe)
    if os.path.isfile(p): return p
    alt = [q for q in glob.glob(os.path.join(repo, "build-*", exe)) if os.path.isfile(q)]
    if alt: return max(alt, key=os.path.getmtime)
    if cfg_bin:
        c = cfg_bin if os.path.isabs(cfg_bin) else os.path.join(repo, cfg_bin)
        if os.path.isfile(c): return os.path.abspath(c)
    return None


def find_bash():
    b = shutil.which("bash")
    if b: return b
    for c in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files (x86)\Git\bin\bash.exe"):
        if os.path.isfile(c): return c
    return None


# a group's wall time: "group X: N s" (one group), "  (group X: N s)" (the whole suite), "X rc=0 group X: N s" (release_check)
_GROUP_TIME = re.compile(r"^(?:[\w-]+ rc=\d+ )?\s*\(?group ([\w-]+): (\d+(?:\.\d+)?) s\)?", re.M)
_GROUP_FAILS = re.compile(r"^\s*\(group ([\w-]+): (\d+) failure")      # (the whole suite, after a failed group's time)
_GROUP_CRASH = re.compile(r"^\s*FAIL ([\w-]+): ")                      # (its process crashed / didn't start)
_RC_LINE = re.compile(r"^([\w-]+) rc=(\d+)\b")                         # (release_check.sh, one per group)
_FAIL = re.compile(r"^\s*(?:FAIL\b|TIMEOUT:)")
_FAIL_AT = re.compile(r"FAIL\s+(\S+?):(\d+):\s*(.*)$")
FAILS_KEPT = 20


def fail_line(line, repo=None):
    """{text, file?, line?} for a FAIL line (file repo-relative, e.g. tests/tests.cpp, when it can be found)."""
    t = line.strip()
    out = {"text": t[:300]}
    m = _FAIL_AT.search(t)
    if m:
        p = m.group(1).replace("\\", "/")
        i = p.rfind("tests/")
        rel = p[i:] if i >= 0 else (p if not os.path.isabs(p) else None)
        if rel: out.update(file=rel, line=int(m.group(2)))
    return out


def parse_log(text, group=None):
    """(times, results) from a run's output. times {group: sec}; results {group: {status, fails: n, failLines}}.

    One group (`group` given): every FAIL line is its own. The whole suite: each group's output comes in one piece,
    ended by its "(group X: N s)" line, then "FAIL X: <why>" if its process crashed and "(group X: K failure(s))" if it
    failed. release_check.sh: "X rc=N group X: N s" per group (its FAIL lines are in the group's own log)."""
    times, res = {}, {}
    def r(g): return res.setdefault(g, {"status": "passed", "fails": 0, "failLines": []})
    pending, last = [], None                                         # FAIL lines of the group printing now
    for ln in text.split("\n"):
        m = _GROUP_TIME.match(ln)
        if m:
            g = last = m.group(1)
            times[g] = float(m.group(2))
            rc = _RC_LINE.match(ln)
            if group is None:
                x = r(g)
                x["failLines"] += pending; pending = []
                if rc and rc.group(2) != "0": x["status"] = "failed"
            continue
        if group is None:
            m = _GROUP_FAILS.match(ln)
            if m:
                x = r(m.group(1)); x["status"] = "failed"; x["fails"] = int(m.group(2)); continue
            m = _RC_LINE.match(ln)
            if m:
                r(m.group(1))["status"] = "passed" if m.group(2) == "0" else "failed"; continue
            m = _GROUP_CRASH.match(ln)
            if m and m.group(1) == last:
                x = r(last); x["status"] = "failed"; x["failLines"].append(fail_line(ln)); continue
        if _FAIL.match(ln): pending.append(fail_line(ln))
    if group is not None: r(group)["failLines"] = pending
    for x in res.values():
        if x["failLines"]: x["status"] = "failed"
        if x["status"] == "failed": x["fails"] = x["fails"] or len(x["failLines"]) or 1   # (the group's own count first:
                                                                                         #  timingTest's set-aside FAILs print too)
        x["failLines"] = x["failLines"][:FAILS_KEPT]
    return times, res


class Runs:
    """One local run at a time; history in cache/runs.json (oldest first, last 500), output in cache/runs/<id>.log.

    Every finished run also updates cache/groups.json: per group, its newest time (from "(group X: N s)" lines: the
    whole suite, one group, release_check.sh, or an imported log) and its newest result (passed / failed, the FAIL
    lines). That file is the timing source on any machine; it outlives the run history."""

    def __init__(self, state_dir):
        self.dir = os.path.join(state_dir, "cache")
        self.path = os.path.join(self.dir, "runs.json")
        self.gpath = os.path.join(self.dir, "groups.json")
        self.lock = threading.Lock()
        self.cur = None                                                 # {rec, proc, stopping}
        self.hist = []
        try:
            with open(self.path, encoding="utf-8") as f: self.hist = json.load(f)
            if not isinstance(self.hist, list): self.hist = []
        except (OSError, ValueError):
            self.hist = []
        changed = False
        for r in self.hist:                                             # (the server stopped during a run)
            if r.get("status") == "running":
                r["status"], r["endedAt"], changed = "stopped", r.get("endedAt") or now_iso(), True
        if changed: self.save()
        self.groups, self.gen = {}, 0
        try:
            with open(self.gpath, encoding="utf-8") as f: g = json.load(f)
            if isinstance(g, dict) and isinstance(g.get("groups"), dict): self.groups = g["groups"]
        except (OSError, ValueError):
            for r in self.hist:                                         # (first start: what the history already has)
                self._note(r, r.get("groupTimes") or {}, r.get("groupResults") or {})

    def save(self):
        os.makedirs(self.dir, exist_ok=True)
        atomic_write(self.path, json.dumps(self.hist[-RUNS_KEPT:], indent=0).encode("utf-8"))

    def _note(self, rec, times, results, source=None):
        """Fold one run's group times / results into self.groups (newest wins)."""
        at = rec.get("endedAt") or rec.get("startedAt") or now_iso()
        src = source or "local run %s" % at
        for g, sec in times.items():
            e = self.groups.setdefault(g, {})
            if (e.get("timeAt") or "") <= at: e.update(sec=sec, timeAt=at, timeRun=rec.get("id"), timeSource=src)
        for g, x in results.items():
            e = self.groups.setdefault(g, {})
            if (e.get("at") or "") <= at: e.update(status=x["status"], at=at, run=rec.get("id"), fails=x.get("fails", 0),
                                                   failLines=x.get("failLines") or [], source=src)
        self.gen += 1

    def save_groups(self):
        os.makedirs(self.dir, exist_ok=True)
        atomic_write(self.gpath, json.dumps({"version": 1, "groups": self.groups}, indent=0).encode("utf-8"))

    def group_state(self):
        """(generation, {group: {sec, timeAt, timeRun, timeSource, status, at, run, fails, failLines}})."""
        with self.lock: return self.gen, json.loads(json.dumps(self.groups))

    def import_log(self, text, name=None):
        """Feed a run's output from elsewhere (a terminal run of build/quark_tests): its group times and results."""
        times, results = parse_log(text)
        if not times and not results: return 0
        with self.lock:
            self._note({"id": None, "endedAt": now_iso()}, times, results, "imported log %s%s" % (now_iso(), " (%s)" % name if name else ""))
            self.save_groups()
        return len(set(times) | set(results))

    def history(self):
        with self.lock: return list(self.hist)

    def get(self, rid):
        with self.lock:
            for r in reversed(self.hist):
                if r["id"] == rid: return dict(r)
        return None

    def log_path(self, rid):
        return os.path.join(self.dir, "runs", rid + ".log")

    def start(self, cmd, argv, cwd, group=None, extra=None, env_add=None):
        with self.lock:
            if self.cur: return None, self.cur["rec"]["id"]
            rid = time.strftime("%Y%m%d-%H%M%S", time.gmtime()) + "-" + os.urandom(2).hex()
            os.makedirs(os.path.join(self.dir, "runs"), exist_ok=True)
            tmp = os.path.join(self.dir, "tmp", rid)
            os.makedirs(tmp, exist_ok=True)
            env = dict(os.environ, TMPDIR=tmp, TMP=tmp, TEMP=tmp, PYTHONUNBUFFERED="1", **(env_add or {}))
            out = open(self.log_path(rid), "wb")
            kw = {}
            if WIN:
                kw["creationflags"] = BELOW_NORMAL | NO_WINDOW | NEW_GROUP
            else:
                kw["start_new_session"] = True                          # (its own process group: stop kills the tree)
                if shutil.which("nice"): argv = ["nice", "-n", "10"] + argv
            try:
                proc = subprocess.Popen(argv, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL, **kw)
            except OSError as e:
                out.close()
                return None, "cannot start: %s" % e
            rec = {"id": rid, "cmd": cmd, "argv": argv, "status": "running", "exit": None, "startedAt": now_iso(),
                   "endedAt": None, "sec": None, "pid": proc.pid, "group": group}
            rec.update(extra or {})
            self.hist.append(rec)
            self.hist = self.hist[-RUNS_KEPT:]
            self.cur = {"rec": rec, "proc": proc, "stopping": False, "t0": time.time(), "out": out, "tmp": tmp}
            self.save()
        threading.Thread(target=self._wait, args=(self.cur,), daemon=True).start()
        log("run %s: %s (pid %d)" % (rid, " ".join(argv), proc.pid))
        return rec, None

    def _wait(self, cur):
        code = cur["proc"].wait()
        cur["out"].close()
        sec = round(time.time() - cur["t0"], 1)
        rec = cur["rec"]
        text = ""
        try:
            with open(self.log_path(rec["id"]), "rb") as f:
                f.seek(max(0, os.path.getsize(self.log_path(rec["id"])) - (8 << 20)))
                text = f.read().decode("utf-8", "replace")
        except OSError:
            pass
        group = rec.get("group")
        times, results = parse_log(text, group)
        if rec["cmd"] == "script:release_check.sh":                     # (its groups' own logs: their FAIL lines)
            for lp in glob.glob(os.path.join(cur["tmp"], "quark_release_check.*", "*.log")):
                g = os.path.basename(lp)[:-4]
                if g in results and results[g]["status"] == "failed":
                    try:
                        with open(lp, encoding="utf-8", errors="replace") as f: results[g]["failLines"] = parse_log(f.read(), g)[1][g]["failLines"]
                    except OSError:
                        pass
        status = "stopped" if cur["stopping"] else ("passed" if code == 0 else "failed")
        if group:
            if status == "stopped": times, results = {}, {}             # (stopped: not a time, not a result)
            else:
                times[group] = sec                                       # (one group: its wall clock)
                results[group] = dict(results.get(group) or {"fails": 0, "failLines": []}, status=status)
        with self.lock:
            rec.update(exit=code, endedAt=now_iso(), sec=sec, status=status)
            if times: rec["groupTimes"] = times                         # (a stopped suite run: the groups that finished)
            if results: rec["groupResults"] = {g: {"status": x["status"], "fails": x["fails"]} for g, x in results.items()}
            if times or results:
                self._note(rec, times, results)
                self.save_groups()
            self.cur = None
            self.save()
            self._prune_tmp()
        log("run %s: %s (exit %s, %.1f s)" % (rec["id"], rec["status"], code, sec))

    def _prune_tmp(self):
        """Keep the private temp folders of the last 5 runs (a script's logs live there), drop the rest."""
        root = os.path.join(self.dir, "tmp")
        try:
            ds = sorted(os.listdir(root))
        except OSError:
            return
        for d in ds[:-5]: shutil.rmtree(os.path.join(root, d), ignore_errors=True)
        logs = sorted(glob.glob(os.path.join(self.dir, "runs", "*.log")))
        keep = {r["id"] for r in self.hist}
        for p in logs:
            if os.path.basename(p)[:-4] not in keep:
                try: os.remove(p)
                except OSError: pass

    def stop(self, rid):
        with self.lock:
            cur = self.cur
            if not cur or cur["rec"]["id"] != rid: return False
            cur["stopping"] = True
            pid = cur["proc"].pid
        kill_tree(pid, cur["proc"])
        return True


def kill_tree(pid, proc):
    """Stop a run and everything it started, by its PID (never by name)."""
    if WIN:
        try:
            subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True, timeout=15,
                           creationflags=NO_WINDOW)
        except (OSError, subprocess.TimeoutExpired):
            pass
        if proc.poll() is None:
            try: proc.kill()
            except OSError: pass
        return
    try:
        os.killpg(pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        return

    def later():
        t = time.time() + 3
        while time.time() < t and proc.poll() is None: time.sleep(0.05)
        try: os.killpg(pid, signal.SIGKILL)                              # (forked test groups too)
        except (ProcessLookupError, PermissionError): pass
    threading.Thread(target=later, daemon=True).start()


# ---- the build's own group list (quark_tests groups), in the background ----------------------------------------

class BuildGroups:
    def __init__(self, repo, cfg_bin, state_dir):
        self.repo, self.cfg_bin, self.state_dir = repo, cfg_bin, state_dir
        self.key, self.val, self.busy = None, None, False
        self.lock = threading.Lock()

    def __call__(self):
        b = find_binary(self.repo, self.cfg_bin)
        if not b: return None
        try: key = (b, os.path.getmtime(b))
        except OSError: return None
        with self.lock:
            if key == self.key: return self.val
            if not self.busy:
                self.busy = True
                threading.Thread(target=self._run, args=(key,), daemon=True).start()
            return self.val if self.val and self.val.get("binary") == self.rel(b) else None

    def rel(self, b):
        return os.path.relpath(b, self.repo).replace("\\", "/")

    def _run(self, key):
        b = key[0]
        val = {"binary": self.rel(b), "builtAt": modelmod.iso_of(key[1]), "groups": None, "error": None}
        tmp = os.path.join(self.state_dir, "cache", "tmp", "groups")
        os.makedirs(tmp, exist_ok=True)
        try:
            p = subprocess.run([b, "groups"], cwd=self.repo, capture_output=True, timeout=20, stdin=subprocess.DEVNULL,
                               env=dict(os.environ, TMPDIR=tmp, TMP=tmp, TEMP=tmp),
                               **({"creationflags": NO_WINDOW | BELOW_NORMAL} if WIN else {}))
            names = [l.strip() for l in p.stdout.decode("utf-8", "replace").split("\n") if re.match(r"^[\w-]+$", l.strip())]
            if p.returncode == 0: val["groups"] = names
            else: val["error"] = "exit %d" % p.returncode
        except (OSError, subprocess.TimeoutExpired) as e:
            val["error"] = str(e)
        shutil.rmtree(tmp, ignore_errors=True)
        with self.lock:
            self.key, self.val, self.busy = key, val, False


# ---- the app ----------------------------------------------------------------------------------------------------

class App:
    def __init__(self, args):
        self.args = args
        self.cfg_path = os.path.abspath(args.config or os.path.join(HERE, "tests_editor.json"))
        self.cfg = load_config(self.cfg_path)
        cfg_dir = os.path.dirname(self.cfg_path)
        self.repo = resolve_repo(args.repo, self.cfg, cfg_dir)
        self.state_dir = os.path.abspath(args.state_dir or HERE)
        td = self.cfg.get("timingDir")
        if td is None and os.path.isdir(CLOUD_TIMING): td = CLOUD_TIMING
        self.timing_dir = os.path.abspath(os.path.join(cfg_dir, td)) if td else None
        ov = self.cfg.get("overview")
        if ov is None and self.timing_dir: ov = os.path.join(os.path.dirname(self.timing_dir), "overview.md")
        self.overview = os.path.abspath(os.path.join(cfg_dir, ov)) if ov else None
        self.gm_url = (self.cfg.get("githubManager") or "http://127.0.0.1:8767").rstrip("/")
        self.runs = Runs(self.state_dir)
        self.bgroups = BuildGroups(self.repo, self.cfg.get("quark_tests"), self.state_dir)
        self.builder = modelmod.Builder(self.repo, self.timing_dir, self.overview, local=self.runs.group_state,
                                        build_groups=self.bgroups)
        self.gz = (None, None)
        self.ci_cache = (0, None)
        self.static_cache = {}
        self.write_lock = threading.Lock()

    def model(self):
        return self.builder.get()

    def model_gz(self):
        m, body, etag = self.model()
        if self.gz[0] != etag: self.gz = (etag, gzip.compress(body, 5))
        return etag, body, self.gz[1]

    # files
    def read_file(self, rel):
        p = repo_path(self.repo, rel)
        if not p: return 403, {"error": "not allowed", "path": rel}
        if not os.path.isfile(p): return 404, {"error": "no such file", "path": rel}
        with open(p, "rb") as f: b = f.read()
        try:
            text = b.decode("utf-8")
        except UnicodeDecodeError:
            return 415, {"error": "not UTF-8 text", "path": rel}
        return 200, {"path": rel, "text": text.replace("\r\n", "\n"), "sha256": sha256(b), "mtime": os.path.getmtime(p),
                     "eol": "crlf" if b"\r\n" in b else "lf", "size": len(b)}

    def write_file(self, rel, text, base):
        """(code, obj): writes text over rel when it still has sha256 base (None / "" for a new file)."""
        p = repo_path(self.repo, rel)
        if not p: return 403, {"error": "not allowed", "path": rel}
        if not isinstance(text, str): return 400, {"error": "text must be a string"}
        if not os.path.isdir(os.path.dirname(p)): return 404, {"error": "no such folder", "path": rel}
        with self.write_lock:
            old = None
            if os.path.isfile(p):
                with open(p, "rb") as f: old = f.read()
            cur = sha256(old) if old is not None else None
            if (base or None) != cur:
                return 409, {"error": "changed", "sha256": cur, "path": rel}
            text = text.replace("\r\n", "\n")
            data = text.replace("\n", "\r\n").encode("utf-8") if old is not None and b"\r\n" in old else text.encode("utf-8")
            if old is not None: self.backup(rel, old)
            atomic_write(p, data)
        log("saved %s (%d bytes)" % (rel, len(data)))
        return 200, {"ok": True, "sha256": sha256(data), "path": rel}

    def backup(self, rel, data):
        d = os.path.join(self.state_dir, "cache", "backups", *rel.split("/"))
        os.makedirs(d, exist_ok=True)
        stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
        name, k = stamp + ".txt", 1
        while os.path.exists(os.path.join(d, name)): name = "%s-%d.txt" % (stamp, k); k += 1
        with open(os.path.join(d, name), "wb") as f: f.write(data)
        for old in sorted(os.listdir(d))[:-BACKUPS_KEPT]:
            try: os.remove(os.path.join(d, old))
            except OSError: pass

    def diff(self, rel):
        if rel:
            if not repo_path(self.repo, rel): return 403, {"error": "not allowed", "path": rel}
            specs = [rel]
        else:
            specs = ["tests", ".github/workflows", ":(glob)tools/*.sh", "tools/githooks", "CMakeLists.txt"]
        code, out, err = modelmod.git(self.repo, "-c", "core.quotepath=off", "diff", "--no-color", "--no-ext-diff",
                                      "HEAD", "--", *specs, timeout=30)
        if code != 0: return 500, {"error": "git diff failed", "detail": err.decode("utf-8", "replace")[-400:]}
        d = out.decode("utf-8", "replace")
        return 200, {"path": rel or None, "diff": d[:4 << 20], "truncated": len(d) > (4 << 20)}

    def revert(self, rel):
        p = repo_path(self.repo, rel)
        if not p: return 403, {"error": "not allowed", "path": rel}
        code, out, err = modelmod.git(self.repo, "show", "HEAD:" + rel, timeout=30)
        if code != 0: return 404, {"error": "not in HEAD", "path": rel}
        cur = None
        if os.path.isfile(p):
            with open(p, "rb") as f: cur = sha256(f.read())
        return self.write_file(rel, out.decode("utf-8", "replace"), cur)

    # runs
    def start_run(self, cmd, args=None, groups=None):
        """cmd: "suite" (the whole suite; `groups` = just those, as QUARK_TEST_ONLY), "group:<name>", "cmd:<name>"
        (`args`: what follows the name, checked against its argsSpec), "script:<name>", or "groups" (list them)."""
        m, _, _ = self.model()
        qt = next((s for s in m["suites"] if s["id"] == "quark_tests"), {"items": []})
        items = {it["id"]: it for it in qt["items"]}
        if not isinstance(cmd, str): return 400, {"error": "cmd must be a string"}
        if args is not None and (not isinstance(args, list) or not all(isinstance(a, str) and ARG_OK.match(a) for a in args)):
            return 400, {"error": "args must be a list of short words (letters, digits, . _ - : , +)"}
        if groups is not None and (not isinstance(groups, list) or not all(isinstance(g, str) for g in groups)):
            return 400, {"error": "groups must be a list of group names"}
        if (args and not cmd.startswith("cmd:")) or (groups and cmd != "suite"):
            return 400, {"error": "args go with cmd:<name>, groups with suite"}
        extra, env, group = {}, None, None
        if cmd.startswith("script:"):
            s = next((s for s in m["suites"] if s["id"] == cmd and s["kind"] == "script"), None)
            if not s: return 404, {"error": "no such script", "cmd": cmd}
            bash = find_bash()
            if not bash: return 404, {"error": "no bash (install Git for Windows)"}
            argv = [bash, s["file"]]
        else:
            if cmd.startswith(("group:", "cmd:")) and cmd not in items: return 404, {"error": "no such test", "cmd": cmd}
            if not cmd.startswith(("group:", "cmd:")) and cmd not in ("groups", "suite"): return 400, {"error": "unknown cmd", "cmd": cmd}
            b = find_binary(self.repo, self.cfg.get("quark_tests"))
            if not b: return 404, {"error": "no build"}
            if cmd == "suite":
                argv = [b]
                if groups:
                    known = {it["name"] for it in qt["items"] if it["kind"] == "group"}
                    bad = [g for g in groups if g not in known]
                    if bad: return 404, {"error": "no such group: " + ", ".join(bad[:5])}
                    groups = [g for g in dict.fromkeys(groups)]
                    env, extra["only"] = {"QUARK_TEST_ONLY": ",".join(groups)}, groups
            elif cmd.startswith("group:"):
                group = cmd[6:]
                argv = [b, "group", group]
            elif cmd.startswith("cmd:"):
                it = items[cmd]
                if (it.get("run") or {}).get("byHand"): return 403, {"error": "run it by hand: " + it["run"]["byHand"]}
                spec, args = it.get("argsSpec") or {"min": 0, "max": 0}, args or []
                if len(args) < spec["min"] or (spec["max"] is not None and len(args) > spec["max"]):
                    want = "%d" % spec["min"] if spec["min"] == spec["max"] else "%d to %s" % (spec["min"], "any" if spec["max"] is None else spec["max"])
                    return 400, {"error": "quark_tests %s takes %s argument(s)" % (it["name"], want)}
                argv = [b, it["name"]] + args
                if args: extra["args"] = args
            else:
                argv = [b, "groups"]
        rec, err = self.runs.start(cmd, argv, self.repo, group, extra, env)
        if not rec:
            if err and err.startswith("cannot"): return 500, {"error": err}
            return 409, {"error": "busy", "running": err}
        return 200, {"id": rec["id"]}

    def run_status(self, rid, frm):
        r = self.runs.get(rid)
        if not r: return 404, {"error": "no such run"}
        p = self.runs.log_path(rid)
        size = os.path.getsize(p) if os.path.isfile(p) else 0
        frm = max(0, min(frm, size))
        data = b""
        if size > frm:
            with open(p, "rb") as f:
                f.seek(frm); data = f.read(min(size - frm, MAX_CHUNK))
        sec = r["sec"]
        if r["status"] == "running":
            try:
                t0 = datetime.datetime.strptime(r["startedAt"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
                sec = round((datetime.datetime.now(datetime.timezone.utc) - t0).total_seconds(), 1)
            except ValueError:
                pass
        return 200, {"id": rid, "cmd": r["cmd"], "args": r.get("args"), "only": r.get("only"), "groupResults": r.get("groupResults"),
                     "status": r["status"], "exit": r["exit"], "startedAt": r["startedAt"],
                     "endedAt": r["endedAt"], "sec": sec, "output": data.decode("utf-8", "replace"), "size": size,
                     "from": frm, "next": frm + len(data), "more": frm + len(data) < size, "groupTimes": r.get("groupTimes")}

    # CI (the GitHub Manager on 8767)
    def ci(self):
        t, val = self.ci_cache
        if val is not None and time.time() - t < 5: return val
        m, _, _ = self.model()
        names = {}
        for s in m["suites"]:
            if s["kind"] == "ci": names[s.get("workflowName") or s["title"]] = s["file"].rsplit("/", 1)[-1]
        try:
            with urllib.request.urlopen(self.gm_url + "/api/state?runs=100", timeout=1) as r:
                state = json.loads(r.read().decode("utf-8"))
        except Exception:
            val = {"available": False, "jobs": {}}
            self.ci_cache = (time.time(), val)
            return val
        jobs = {}
        for run in state.get("runs") or []:                             # (newest first: the first one seen wins)
            wf = names.get(run.get("workflow"), run.get("workflow") or "?")
            for j in run.get("jobs") or []:
                key = "%s/%s" % (wf, j.get("name"))
                if key in jobs: continue
                jobs[key] = {"conclusion": j.get("conclusion") or j.get("status"), "url": j.get("url"),
                             "runId": run.get("id"), "branch": run.get("branch"),
                             "at": j.get("completedAt") or j.get("startedAt") or run.get("updatedAt"),
                             "durationSec": _dur(j.get("startedAt"), j.get("completedAt")),
                             "failedSteps": j.get("failedSteps") or [], "sha": run.get("sha")}
        val = {"available": True, "jobs": jobs, "polledAt": (state.get("poll") or {}).get("lastOk")}
        self.ci_cache = (time.time(), val)
        return val

    def summary(self):
        m, _, _ = self.model()
        qt = next((s for s in m["suites"] if s["id"] == "quark_tests"), {"items": []})
        h = self.runs.history()
        last = h[-1] if h else None
        return {"suites": len(m["suites"]), "groups": sum(1 for i in qt["items"] if i["kind"] == "group"),
                "jobs": sum(len(s["items"]) for s in m["suites"] if s["kind"] == "ci"),
                "checks": sum(len(f["checks"]) for f in m["functions"].values()), "dirty": len(m["repo"]["dirty"]),
                "lint": len(m["lint"]), "lintErrors": sum(1 for l in m["lint"] if l["level"] == "error"),
                "lastRun": {k: last.get(k) for k in ("id", "cmd", "status", "exit", "startedAt", "endedAt", "sec")} if last else None,
                "generatedAt": m["generatedAt"], "version": VERSION}

    def static(self, rel):
        web = os.path.join(HERE, "web")
        if ".." in rel.split("/") or "\\" in rel or "\0" in rel: return None
        full = os.path.realpath(os.path.join(web, *[p for p in rel.split("/") if p]))
        if not full.startswith(os.path.realpath(web) + os.sep) or not os.path.isfile(full): return None
        st = os.stat(full)
        hit = self.static_cache.get(full)
        if hit and hit[0] == (st.st_mtime_ns, st.st_size): return hit[1]
        with open(full, "rb") as f: body = f.read()
        val = (body, '"%s"' % hashlib.sha1(body).hexdigest()[:16], MIME.get(os.path.splitext(full)[1].lower(), "application/octet-stream"))
        self.static_cache[full] = ((st.st_mtime_ns, st.st_size), val)
        return val


def _dur(a, b):
    try:
        f = "%Y-%m-%dT%H:%M:%SZ"
        return int((datetime.datetime.strptime(b, f) - datetime.datetime.strptime(a, f)).total_seconds())
    except (TypeError, ValueError):
        return None


# ---- HTTP ---------------------------------------------------------------------------------------------------------

def make_handler(app, port):
    hosts = {"127.0.0.1:%d" % port, "localhost:%d" % port}
    origins = {"http://%s:%d" % (h, p) for h in ("127.0.0.1", "localhost") for p in CORS_PORTS}

    class H(http.server.BaseHTTPRequestHandler):
        server_version = "QuarkTestsEditor/" + VERSION
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):
            if app.args.verbose: log("%s %s" % (self.address_string(), fmt % a))

        def _cors(self):
            o = self.headers.get("Origin")
            if o in origins:
                self.send_header("Access-Control-Allow-Origin", o)
                self.send_header("Access-Control-Expose-Headers", "ETag")
            self.send_header("Vary", "Origin, Accept-Encoding")

        def _send(self, code, body=b"", ctype="application/json; charset=utf-8", headers=None):
            self.send_response(code)
            self._cors()
            for k, v in (headers or {}).items(): self.send_header(k, v)
            if code != 304:
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            if self.command != "HEAD" and code != 304: self.wfile.write(body)

        def _json(self, code, obj):
            self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"), headers={"Cache-Control": "no-store"})

        def _host_ok(self):
            if self.headers.get("Host", "") in hosts: return True
            self._send(403, b'{"error":"bad host"}')
            return False

        def _query(self):
            q = {}
            if "?" in self.path:
                from urllib.parse import parse_qs
                for k, v in parse_qs(self.path.split("?", 1)[1], keep_blank_values=True).items(): q[k] = v[0]
            return q

        def _body(self):
            n = int(self.headers.get("Content-Length") or 0)
            if n > MAX_BODY: raise ValueError("body too big")
            raw = self.rfile.read(n) if n else b""
            if not raw: return {}
            obj = json.loads(raw.decode("utf-8"))
            if not isinstance(obj, dict): raise ValueError("expected a JSON object")
            return obj

        def do_OPTIONS(self):
            if not self._host_ok(): return
            self.send_response(204)
            self._cors()
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "If-None-Match, Content-Type")
            self.send_header("Access-Control-Max-Age", "600")
            self.send_header("Content-Length", "0")
            self.end_headers()

        def do_HEAD(self): self.do_GET()

        def do_GET(self):
            if not self._host_ok(): return
            path, q = self.path.split("?", 1)[0], self._query()
            try:
                if path == "/api/model":
                    etag, body, gz = app.model_gz()
                    h = {"ETag": etag, "Cache-Control": "no-cache"}
                    if etag in [t.strip() for t in self.headers.get("If-None-Match", "").split(",")]:
                        return self._send(304, headers=h)
                    if "gzip" in self.headers.get("Accept-Encoding", ""):
                        h["Content-Encoding"] = "gzip"
                        return self._send(200, gz, headers=h)
                    return self._send(200, body, headers=h)
                if path == "/api/file": return self._json(*app.read_file(q.get("path", "")))
                if path == "/api/diff": return self._json(*app.diff(q.get("path") or None))
                if path == "/api/run":
                    try: frm = int(q.get("from") or 0)
                    except ValueError: frm = 0
                    return self._json(*app.run_status(q.get("id", ""), frm))
                if path == "/api/runs":
                    return self._json(200, [{k: r.get(k) for k in ("id", "cmd", "status", "exit", "startedAt", "endedAt", "sec", "args", "only", "groupResults")}
                                            for r in reversed(app.runs.history())])
                if path == "/api/ci": return self._json(200, app.ci())
                if path == "/api/summary": return self._json(200, app.summary())
                if path == "/api/health": return self._json(200, {"ok": True, "version": VERSION, "repo": app.repo, "tool": "tests-editor"})
                if path.startswith("/api/"): return self._json(404, {"error": "no such endpoint"})
            except Exception as e:
                log("GET %s failed: %s: %s" % (path, type(e).__name__, e))
                return self._json(500, {"error": "%s: %s" % (type(e).__name__, e)})
            rel = "index.html" if path in ("/", "") else path.lstrip("/")
            hit = app.static(rel)
            if not hit:
                if rel == "index.html":
                    return self._send(200, b"<!doctype html><title>Tests editor</title><p>The page (web/) isn't installed next to serve.py.",
                                      "text/html; charset=utf-8")
                return self._send(404, b"not found", "text/plain; charset=utf-8")
            body, etag, ctype = hit
            h = {"ETag": etag, "Cache-Control": "no-cache"}
            if etag in [t.strip() for t in self.headers.get("If-None-Match", "").split(",")]: return self._send(304, headers=h)
            self._send(200, body, ctype, headers=h)

        def do_POST(self):
            if not self._host_ok(): return
            path = self.path.split("?", 1)[0]
            o = self.headers.get("Origin")
            if o and o not in origins and o != "http://%s" % self.headers.get("Host"):
                return self._json(403, {"error": "bad origin"})                 # (a page from elsewhere can't write)
            try:
                body = self._body()
            except (ValueError, UnicodeDecodeError) as e:
                return self._json(400, {"error": "bad request: %s" % e})
            try:
                if path in ("/api/file", "/api/revert"):
                    if path == "/api/file": code, obj = app.write_file(body.get("path", ""), body.get("text"), body.get("baseSha256"))
                    else: code, obj = app.revert(body.get("path", ""))
                    if code != 200: return self._json(code, obj)
                    _, mbody, _ = app.model()
                    out = json.dumps(obj, ensure_ascii=False).encode("utf-8")[:-1] + b',"model":' + mbody + b"}"
                    return self._send(200, out, headers={"Cache-Control": "no-store"})
                if path == "/api/run": return self._json(*app.start_run(body.get("cmd"), body.get("args"), body.get("groups")))
                if path == "/api/import-log":
                    text = body.get("text")
                    if not isinstance(text, str): return self._json(400, {"error": "text must be a string"})
                    return self._json(200, {"ok": True, "groups": app.runs.import_log(text, str(body.get("name") or "")[:80] or None)})
                if path == "/api/run/stop":
                    ok = app.runs.stop(body.get("id", ""))
                    return self._json(200 if ok else 404, {"ok": ok} if ok else {"error": "not running"})
                return self._json(404, {"error": "no such endpoint"})
            except Exception as e:
                log("POST %s failed: %s: %s" % (path, type(e).__name__, e))
                return self._json(500, {"error": "%s: %s" % (type(e).__name__, e)})

    return H


class Server(http.server.ThreadingHTTPServer):
    allow_reuse_address = not WIN                       # (on Windows it would let a second server share the port)
    daemon_threads = True


def ping(port):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/health" % port, timeout=2) as r:
            return json.loads(r.read().decode()).get("tool") == "tests-editor"
    except Exception:
        return False


def main(argv=None):
    ap = argparse.ArgumentParser(description="Quark tests editor (local server, 127.0.0.1 only).")
    ap.add_argument("--port", type=int, default=None, help="default %d (or `port` in tests_editor.json)" % DEFAULT_PORT)
    ap.add_argument("--repo", default=None, help="the quark working copy (default: tests_editor.json `repo`, ..\\working-repo)")
    ap.add_argument("--config", default=None, help="tests_editor.json (default: beside serve.py)")
    ap.add_argument("--state-dir", default=None, help="where cache/ lives (default: beside serve.py)")
    ap.add_argument("--no-browser", action="store_true", help="don't open the page")
    ap.add_argument("--snapshot", default=None, help="write the model to this file and exit")
    ap.add_argument("--quiet", action="store_true", help="log to cache/serve.log only")
    ap.add_argument("--verbose", action="store_true", help="log every request")
    args = ap.parse_args(argv)
    app = App(args)
    log.open(os.path.join(app.state_dir, "cache", "serve.log"), args.quiet or args.snapshot is not None)
    port = args.port or int(app.cfg.get("port") or DEFAULT_PORT)

    if args.snapshot:
        m, body, _ = app.model()
        with open(args.snapshot, "wb") as f: f.write(body)
        sys.stdout.write("wrote %s: %d suites, %d functions, %d lint, %.0f ms\n" % (
            args.snapshot, len(m["suites"]), len(m["functions"]), len(m["lint"]), m["buildMs"]))
        return 0

    url = "http://127.0.0.1:%d/" % port
    browser = not args.no_browser and os.environ.get("QUARK_SHELL") != "1"
    if ping(port):
        log("already running on port %d" % port)
        if browser: webbrowser.open(url)
        return 0
    try:
        srv = Server(("127.0.0.1", port), make_handler(app, port))
    except OSError as e:
        log("cannot listen on 127.0.0.1:%d (%s)" % (port, e))
        if sys.stderr is not None and not log.tty:
            try: sys.stderr.write("tests-editor: cannot listen on 127.0.0.1:%d (%s)\n" % (port, e))
            except Exception: pass
        return 2
    log("serving %s (repo %s, timing %s)" % (url, app.repo, app.timing_dir or "-"))
    try:
        t0 = time.perf_counter(); app.model()
        log("model built in %.0f ms" % ((time.perf_counter() - t0) * 1000))
    except Exception as e:
        log("first model build failed: %s: %s" % (type(e).__name__, e))
    if browser: threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

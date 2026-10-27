"""Keeps every source fresh in the background (one thread each, its own interval) and holds the results in memory,
so a request never waits for a source. One failing source never touches the others. A source that fails keeps its
last good data (`kept: true`, as of `lastOkAt`): a sibling's auto-update restart never blanks the board."""
import threading
import time
import traceback

from . import builds, checklist, derive, docs, editors, github, oracle, repo, servers, util
from .config import load_config

MODULES = (github, docs, editors, builds, repo, servers, oracle, checklist)
REFRESH_ALL_GAP = 10.0   # ?refresh=1 at most once per 10 s
REFRESH_ONE_GAP = 10.0   # POST /api/refresh?source=<key> (the Oracle box: oracle.MIN_GAP)
RETRY_LIMIT = 6          # a source still "waiting" after this many tries is reported as down


class Context:
    """What a source may use: paths, config.json (read fresh each time) and the versions the others found."""

    def __init__(self, paths, store):
        self.paths = paths
        self._store = store

    def config(self):
        return load_config(self.paths)

    def versions(self):
        return derive.versions(self._store.results())


class Runner:
    def __init__(self, module, store):
        self.m = module
        self.key = module.KEY
        self.interval = module.INTERVAL
        self.min_gap = getattr(module, "MIN_GAP", REFRESH_ONE_GAP)
        self.store = store
        self.wake = threading.Event()
        self.next_at = 0.0
        self.last_attempt = 0.0
        self.last_ok = None
        self.retries = 0
        self.thread = None

    def request(self, now=None):
        """Ask for a refresh soon; honours the source's minimum gap. -> (accepted, seconds until allowed)."""
        now = now or time.time()
        wait = self.last_attempt + self.min_gap - now
        if wait > 0:
            return False, round(wait, 1)
        self.next_at = now
        self.wake.set()
        return True, 0.0

    def run_once(self):
        start = time.time()
        self.last_attempt = start
        env = {"ok": False, "fetchedAt": util.iso(start), "error": None, "data": None}
        try:
            res = self.m.fetch(self.store.ctx)
            data, meta = res if isinstance(res, tuple) else (res, {})
            env.update(meta or {})
            env.update(ok=True, data=data)
            self.last_ok = start
            self.retries = 0
            nxt = start + self.interval
        except util.Retry as e:
            self.retries += 1
            env["error"] = str(e)
            if self.retries < RETRY_LIMIT:
                env["pending"] = True
                nxt = start + e.seconds
            else:
                nxt = start + min(self.interval, 60)
        except util.SourceError as e:
            env["error"] = str(e)
            nxt = start + self.interval
        except Exception as e:  # (a bug in one source must not stop it or the others)
            env["error"] = "internal error (%s)" % type(e).__name__
            util.log("source %s: %s" % (self.key, traceback.format_exc(limit=4)))
            nxt = start + self.interval
        if not env["ok"]:  # (the last good data stays, marked: derive and the page show it as "last known")
            prev = self.store.result(self.key)
            if prev.get("data") is not None:
                env.update(data=prev["data"], kept=True)
        env["durationMs"] = int((time.time() - start) * 1000)
        env["lastOkAt"] = util.iso(self.last_ok)
        env["nextAt"] = util.iso(nxt)
        self.next_at = nxt
        self.store.put(self.key, env, start)
        return env

    def loop(self, stop):
        while not stop.is_set():
            delay = self.next_at - time.time()
            if delay > 0:
                self.wake.wait(delay)
                self.wake.clear()
                if stop.is_set():
                    break
                if self.next_at - time.time() > 0.05:
                    continue
            self.run_once()


class Store:
    def __init__(self, paths, history=None, modules=MODULES):
        self.paths = paths
        self.history = history
        self.lock = threading.Lock()
        self.compute_lock = threading.Lock()
        self._results = {}
        self.ctx = Context(paths, self)
        self.runners = {m.KEY: Runner(m, self) for m in modules}
        for k in self.runners:
            self._results[k] = {"ok": False, "fetchedAt": None, "error": "not fetched yet", "data": None, "pending": True}
        self.memo = {}  # (derive's memory between refreshes: how long sources have disagreed)
        self.derived = derive.derive(self.results(), memo=self.memo)
        self.generated_at = util.now_iso()
        self.last_refresh_all = 0.0
        self.stop = threading.Event()
        self._load_oracle()

    # -- the Oracle box's last check survives restarts, so reopening the app never adds SSH connections ----------
    def _oracle_path(self):
        return self.paths.pm("oracle-last.json")

    def _load_oracle(self):
        r = self.runners.get(oracle.KEY)
        if not r:
            return
        try:
            saved = util.read_json_file(self._oracle_path())
            at = float(saved["attemptAt"])
            env = saved["result"]
        except (OSError, ValueError, KeyError, TypeError):
            return
        if not isinstance(env, dict) or at > time.time() + 60:
            return
        r.last_attempt = at
        r.next_at = at + r.interval
        if env.get("ok"):
            r.last_ok = at
        with self.lock:
            self._results[oracle.KEY] = env

    def _save_oracle(self, env, at):
        try:
            self.paths.ensure_pm_dir()
            util.write_json_atomic(self._oracle_path(), {"attemptAt": at, "result": env})
        except OSError as e:
            util.log("oracle-last.json not saved (%s)" % type(e).__name__)

    # -----------------------------------------------------------------------------------------------------------
    def results(self):
        with self.lock:
            return dict(self._results)

    def result(self, key):
        with self.lock:
            return self._results.get(key) or {}

    def put(self, key, env, at):
        with self.lock:
            self._results[key] = env
        if key == oracle.KEY:
            self._save_oracle(env, at)
        self.recompute()

    def recompute(self):
        with self.compute_lock:
            results = self.results()
            try:
                d = derive.derive(results, memo=self.memo)
            except Exception:
                util.log("derive failed: %s" % traceback.format_exc(limit=4))
                return
            if self.history is not None:
                try:
                    self.history.update(results, d)
                except Exception:
                    util.log("history failed: %s" % traceback.format_exc(limit=4))
            self.derived = d
            self.generated_at = util.now_iso()

    def refresh_all(self, now=None):
        """?refresh=1: every source except the Oracle box (that one only on its own button). Once per 10 s."""
        now = now or time.time()
        wait = self.last_refresh_all + REFRESH_ALL_GAP - now
        if wait > 0:
            return False, round(wait, 1)
        self.last_refresh_all = now
        for k, r in self.runners.items():
            if k != oracle.KEY:
                r.next_at = now
                r.wake.set()
        return True, 0.0

    def refresh(self, key):
        r = self.runners.get(key)
        if not r:
            return None
        return r.request()

    def start(self):
        for r in self.runners.values():
            r.thread = threading.Thread(target=r.loop, args=(self.stop,), name="pm-" + r.key, daemon=True)
            r.thread.start()

    def shutdown(self):
        self.stop.set()
        for r in self.runners.values():
            r.wake.set()


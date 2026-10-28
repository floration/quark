"""The PM's own event log: Quark\\quarkology\\history.jsonl, one {time, kind, text, link?} per line, kept
forever. Events come from comparing a small snapshot of facts between refreshes; the snapshot is saved
(history-state.json) so a restart neither repeats nor loses changes. A fact group is only compared while its
source is up, so a source going down never reads as "everything closed"."""
import json
import threading

from . import util
from . import derive

GH = derive.GH
DOCS = derive.DOCS
DASH = derive.DASH


def facts(results, derived):
    """Snapshot of what is watched. A key is missing when its source is not up."""
    f = {}
    gh = derive.ok_data(results, "github")
    repo = derive.ok_data(results, "repo")
    v = derive.versions(results, derive.ok_data)  # (never the last known data a down source kept)
    if gh or repo:
        log0 = ((repo or {}).get("log") or [{}])[0]
        tsha = util.short_sha((derive.gh_testing(gh).get("sha") if gh else None) or log0.get("sha"))
        if v.get("testing"):  # (`from`: GitHub's testing tip and the local mirror can differ; never compare across)
            f["testing"] = {"version": v["testing"], "sha": tsha, "from": "github" if gh else "repo"}
    if gh:
        f["production"] = {"tag": derive.gh_production(gh).get("tag") or v.get("production")}
        f["prerelease"] = {"tag": v.get("testingPrerelease")}
        f["prs"] = {str(p["number"]): {"title": p["title"], "draft": p["draft"]} for p in derived["prs"]
                    if p.get("number") is not None}
        runs = derive.gh_runs(gh)
        f["running"] = {str(r["id"]): {"name": r.get("name"), "branch": r.get("branch")} for r in runs["running"]
                        if r.get("id") is not None}
        known = {}
        for r in runs["recent"] + [runs["lastFailed"], runs["lastTesting"]]:
            if r and r.get("id") is not None and r.get("conclusion"):
                known[str(r["id"])] = {"conclusion": r["conclusion"], "branch": r.get("branch"), "name": r.get("name")}
        f["finished"] = known
    orc = derive.ok_data(results, "oracle")
    if orc and orc.get("units"):
        f["units"] = {n: (u or {}).get("active") for n, u in orc["units"].items()}
    if derive.ok_data(results, "docs") is not None:
        f["inProgress"] = {str(e["id"] if e.get("id") is not None else e["title"]):
                           {"title": e["title"], "waitingOn": e.get("waitingOn"), "link": e.get("entryLink") or e["link"]}
                           for e in derived["inProgress"]}
    ed = derive.ok_data(results, "editors")
    if ed and derive.human_pct(ed) is not None:
        f["humanPct"] = derive.human_pct(ed)
    return f


def diff(old, new):
    """Events (without time) for what changed from `old` to `new`; only groups present in both are compared."""
    ev = []

    def both(k):
        return k in old and k in new

    if both("testing") and old["testing"].get("from") == new["testing"].get("from"):
        o, n = old["testing"], new["testing"]
        if o.get("version") != n.get("version"):
            ev.append({"kind": "release", "text": "testing is now %s%s" %
                       (n.get("version"), " (%s)" % n["sha"] if n.get("sha") else ""), "link": GH + "#branch=testing"})
        elif n.get("sha") and o.get("sha") and n["sha"] != o["sha"]:
            ev.append({"kind": "push", "text": "testing moved to %s" % n["sha"], "link": GH + "#branch=testing"})
    if both("production") and new["production"].get("tag") and old["production"].get("tag") != new["production"]["tag"]:
        ev.append({"kind": "release", "text": "Production is now %s" % new["production"]["tag"], "link": None})
    if both("prerelease") and new["prerelease"].get("tag") and old["prerelease"].get("tag") != new["prerelease"]["tag"]:
        ev.append({"kind": "release", "text": "New prerelease %s" % new["prerelease"]["tag"], "link": None})
    if both("prs"):
        for n, p in new["prs"].items():
            if n not in old["prs"]:
                ev.append({"kind": "pr", "text": "PR #%s opened%s: %s" % (n, " (draft)" if p.get("draft") else "", p.get("title")),
                           "link": GH + "#pr=" + n})
        for n, p in old["prs"].items():
            if n not in new["prs"]:
                ev.append({"kind": "pr", "text": "PR #%s closed: %s" % (n, p.get("title")), "link": GH + "#pr=" + n})
    if both("running"):
        fin = new.get("finished") or {}
        for rid, r in new["running"].items():
            if rid not in old["running"]:
                ev.append({"kind": "ci", "text": "CI started on %s (%s)" % (r.get("branch") or "?", r.get("name") or "run"),
                           "link": GH + "#run=" + rid})
        for rid, r in old["running"].items():
            if rid not in new["running"]:
                c = fin.get(rid, {}).get("conclusion")
                word = {"success": "passed", "failure": "failed"}.get(c, c or "finished")
                ev.append({"kind": "ci", "text": "CI %s on %s (%s)" % (word, r.get("branch") or "?", r.get("name") or "run"),
                           "link": GH + "#run=" + rid})
    if both("finished"):
        # a run that finished between two refreshes without ever being seen running
        for rid, r in new["finished"].items():
            if rid not in old["finished"] and rid not in (old.get("running") or {}) and rid not in (old.get("ended") or []):
                word = {"success": "passed", "failure": "failed"}.get(r.get("conclusion"), r.get("conclusion"))
                ev.append({"kind": "ci", "text": "CI %s on %s (%s)" % (word, r.get("branch") or "?", r.get("name") or "run"),
                           "link": GH + "#run=" + rid})
    if both("units"):
        for n, a in new["units"].items():
            if n in old["units"] and old["units"][n] != a:
                ev.append({"kind": "unit", "text": "%s: %s -> %s" % (n, old["units"][n], a), "link": None})
    if both("inProgress"):
        for k, e in new["inProgress"].items():
            if k not in old["inProgress"]:
                ev.append({"kind": "entry", "text": "In progress: %s%s" %
                           (e["title"], " (waiting on %s)" % e["waitingOn"] if e.get("waitingOn") else ""), "link": e.get("link")})
        for k, e in old["inProgress"].items():
            if k not in new["inProgress"]:
                ev.append({"kind": "entry", "text": "Finished: %s" % e["title"], "link": e.get("link")})
    if both("humanPct") and abs((new["humanPct"] or 0) - (old["humanPct"] or 0)) >= 0.05:
        ev.append({"kind": "human", "text": "Human-made assets %.1f%% -> %.1f%%" % (old["humanPct"], new["humanPct"]),
                   "link": DASH + "#view=assets"})
    return ev


class History:
    def __init__(self, paths):
        self.paths = paths
        self.path = paths.pm("history.jsonl")
        self.state_path = paths.pm("history-state.json")
        self.lock = threading.Lock()
        self.events = []
        self.snapshot = {}
        self._load()

    def _load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line:
                        try:
                            e = json.loads(line)
                        except ValueError:
                            continue
                        if isinstance(e, dict):
                            self.events.append(e)
        except OSError:
            pass
        try:
            s = util.read_json_file(self.state_path)
            self.snapshot = s if isinstance(s, dict) else {}
        except (OSError, ValueError):
            self.snapshot = {}

    def update(self, results, derived):
        new = facts(results, derived)
        with self.lock:
            if "running" in new:  # (runs already announced as finished: never announced twice)
                left = [r for r in (self.snapshot.get("running") or {}) if r not in new["running"]]
                new["ended"] = ((self.snapshot.get("ended") or []) + left)[-50:]
            events = diff(self.snapshot, new)
            merged = dict(self.snapshot)
            merged.update(new)  # (groups whose source is down keep their last known value)
            changed = merged != self.snapshot
            self.snapshot = merged
            stamp = util.now_iso()
            out = []
            for e in events:
                e = {"time": stamp, "kind": e["kind"], "text": e["text"], **({"link": e["link"]} if e.get("link") else {})}
                out.append(e)
            self.events.extend(out)
            try:
                if out:
                    self.paths.ensure_pm_dir()
                    with open(self.path, "a", encoding="utf-8") as f:
                        for e in out:
                            f.write(json.dumps(e, ensure_ascii=False) + "\n")
                if changed:
                    self.paths.ensure_pm_dir()
                    util.write_json_atomic(self.state_path, merged)
            except OSError as e:
                util.log("history not saved (%s)" % type(e).__name__)
        return out

    def latest(self, limit=100):
        with self.lock:
            limit = max(0, min(int(limit), len(self.events)))
            return list(reversed(self.events[len(self.events) - limit:])) if limit else []


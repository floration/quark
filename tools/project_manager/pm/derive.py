"""The `derived` part of /api/pm: plain rules over the sources (SPEC 2, "Derived rules"). Every item says which
source it came from (`source`). Sibling field names may differ a little, so everything is read with tolerant
fallbacks (util.get with several names) and the raw data stays in `sources`."""
import time
import urllib.parse

from . import util
from .util import get, as_list

DASH = "http://127.0.0.1:8765/"
DOCS = "http://127.0.0.1:8766/"
GH = "http://127.0.0.1:8767/"
CHECKLIST_URL = "https://github.com/floration/quark/blob/testing/docs/RELEASE_CHECKLIST.md"
FAILED = {"failure", "failed", "timed_out", "startup_failure", "error", "cancelled_failure"}
SOURCE_LABELS = {"github": "GitHub Manager", "docs": "Claude Manager", "editors": "Editor Manager",
                 "builds": "Local builds", "repo": "Quark\\repository", "servers": "Server list (ranking Worker)",
                 "oracle": "Oracle box", "checklist": "Release checklist"}
SOURCE_FIX = {"github": "start the GitHub Manager", "docs": "start the Claude Manager",
              "editors": "start the Editor Manager", "servers": "check the internet connection",
              "oracle": "check config.json's oracle key path and that ssh.exe works"}
DAY = 86400.0


def ok_data(results, key):
    r = results.get(key) or {}
    return r.get("data") if r.get("ok") and isinstance(r.get("data"), dict) else None


# ---- GitHub Manager (8767) ----------------------------------------------------------------------------------------

def _tag_of(p):
    if isinstance(p, str):
        return {"tag": p}
    if isinstance(p, dict):
        return {"tag": get(p, "tag", "tag_name", "tagName", "name"), "url": get(p, "url", "html_url", "htmlUrl"),
                "publishedAt": get(p, "publishedAt", "published_at", "createdAt", "created_at")}
    return None


def _str(x):
    return x if isinstance(x, str) else None


def gh_testing(d):
    t = get(d, "testing", "branches.testing") or {}
    t = t if isinstance(t, dict) else {}
    pre = _tag_of(get(t, "prerelease", "latestPrerelease") or get(d, "prerelease", "testingPrerelease", "latestPrerelease"))
    return {"version": util.parse_version(get(t, "version") or get(d, "testingVersion", "versions.testing")),
            "sha": _str(get(t, "sha", "headSha", "commit")) or _str(get(d, "testingSha")),
            "committedAt": get(t, "committedAt", "commitDate", "date", "time"), "subject": get(t, "subject"),
            "aheadOfProduction": util.to_int(get(t, "aheadOfProduction")),
            "url": get(t, "url"), "prerelease": pre}


def gh_production(d):
    p = get(d, "production", "latestRelease", "release") or {}
    p = p if isinstance(p, dict) else ({"tag": p} if isinstance(p, str) else {})
    tag = get(p, "tag", "tag_name", "tagName") or get(d, "productionTag")
    return {"tag": tag, "version": util.parse_version(get(p, "version") or tag),
            "sha": get(p, "sha", "commit", "target_commitish"),
            "publishedAt": get(p, "publishedAt", "published_at", "createdAt"), "url": get(p, "url", "html_url")}


def norm_pr(p):
    checks = get(p, "checks", "checkState", "checksState", "ci", "statusCheckRollup", "checkStatus")
    if isinstance(checks, dict):
        checks = get(checks, "state", "conclusion", "status")
    n = util.to_int(get(p, "number", "id"))
    head = get(p, "head.ref", "headRefName", "headRef", "head")
    base = get(p, "base.ref", "baseRefName", "baseRef", "base")
    return {"number": n, "title": get(p, "title", default=""), "url": get(p, "url", "html_url", "htmlUrl"),
            "head": head if isinstance(head, str) else None, "base": base if isinstance(base, str) else None,
            "draft": bool(get(p, "draft", "isDraft", default=False)),
            "checks": checks if isinstance(checks, str) else None, "checksText": get(p, "checksText", "checksSummary"),
            "updatedAt": get(p, "updatedAt", "updated_at"),
            "link": GH + "#pr=%s" % n if n is not None else GH}


def gh_prs(d):
    """Open PRs: the summary's openPulls, else the state's pulls (open + last 30 closed: only the open ones)."""
    out = []
    for p in as_list(get(d, "openPulls", "prs", "pullRequests", "openPrs", "pulls", "openPRs")):
        if isinstance(p, dict) and str(get(p, "state", default="open")).lower() == "open":
            out.append(norm_pr(p))
    return out


def norm_run(r):
    if not isinstance(r, dict):
        return None
    rid = get(r, "id", "runId", "databaseId")
    return {"id": rid, "name": get(r, "name", "workflowName", "workflow"), "title": get(r, "title"),
            "branch": get(r, "branch", "head_branch", "headBranch"), "sha": get(r, "sha", "head_sha", "headSha"),
            "status": get(r, "status", "state"), "conclusion": get(r, "conclusion", "result"),
            "job": get(r, "job", "currentJob"), "event": get(r, "event"), "durationSec": get(r, "durationSec"),
            "startedAt": get(r, "startedAt", "run_started_at", "created_at", "createdAt"),
            "finishedAt": get(r, "finishedAt", "updated_at", "updatedAt", "completedAt"),
            "url": get(r, "url", "html_url", "htmlUrl"),
            "link": GH + "#run=%s" % rid if rid is not None else GH}


def gh_runs(d):
    r = get(d, "runs", "ci", "actions")
    running, recent, last_failed, last_testing = [], [], None, None
    if isinstance(r, dict):
        running = as_list(get(r, "running", "inProgress", "in_progress", "active"))
        recent = as_list(get(r, "recent", "completed", "all", "latest"))
        last_failed = get(r, "lastFailed", "lastFailure", "failed", "last_failed")
        last_testing = get(r, "lastTesting", "testing", "last_testing")
    elif isinstance(r, list):
        recent = r
        running = [x for x in r if isinstance(x, dict) and get(x, "status") in ("in_progress", "queued", "running", "waiting")]
    running = running or as_list(get(d, "running", "runningRuns", "activeRuns"))
    last_failed = last_failed or get(d, "lastFailedRun", "lastFailure", "lastFailed")
    last_testing = last_testing or get(d, "lastTesting", "lastTestingRun", "testing.lastRun", "testing.ci", "testing.run")
    if not last_testing:  # (the state / cache file: branches[name=testing].lastRun)
        last_testing = next((get(b, "lastRun") for b in as_list(get(d, "branches"))
                             if isinstance(b, dict) and b.get("name") == "testing"), None)
    running = [x for x in (norm_run(x) for x in running) if x]
    recent = [x for x in (norm_run(x) for x in recent) if x]
    last_failed, last_testing = norm_run(last_failed), norm_run(last_testing)
    if last_testing and not last_testing.get("branch"):
        last_testing["branch"] = "testing"
    return {"running": running, "recent": recent, "lastFailed": last_failed, "lastTesting": last_testing}


def testing_ci(runs, testing_sha):
    """The newest finished CI run on testing (None when unknown)."""
    lt = runs.get("lastTesting")
    if lt and lt.get("conclusion"):
        return lt
    done = [r for r in runs.get("recent", []) if r.get("branch") == "testing" and r.get("conclusion")]
    if done:
        return max(done, key=lambda r: util.parse_time(r.get("finishedAt") or r.get("startedAt")) or 0)
    lf = runs.get("lastFailed")
    if lf and lf.get("branch") == "testing" and testing_sha and lf.get("sha") and \
            (testing_sha.startswith(lf["sha"]) or lf["sha"].startswith(testing_sha)):
        return lf
    return None


def norm_local(x, source):
    df = get(x, "dirtyFiles", "uncommitted", "changes", "changedFiles", "modified")
    dirty = get(x, "dirty", "isDirty")
    dirty_files = len(df) if isinstance(df, list) else util.to_int(df)
    if dirty_files is None and isinstance(dirty, int) and not isinstance(dirty, bool):
        dirty_files = dirty  # (the GitHub Manager's dirty is a count)
    dirty = bool(dirty) if dirty is not None else (dirty_files or 0) > 0
    return {"name": get(x, "name", "id", default="?"), "path": get(x, "path"), "branch": get(x, "branch"),
            "sha": get(x, "sha", "head", "commit"),
            "ahead": util.to_int(get(x, "aheadOfTesting", "ahead", "aheadTesting", "ahead_testing", "commitsAhead")),
            "behind": util.to_int(get(x, "behindTesting", "behind", "behindOfTesting", "behind_testing", "commitsBehind")),
            "aheadOfProduction": util.to_int(get(x, "aheadOfProduction")), "version": get(x, "version"),
            "error": get(x, "error"),
            "dirty": dirty, "dirtyFiles": dirty_files,
            "dirtySince": get(x, "dirtySince", "uncommittedSince", "oldestChange", "oldestUncommitted", "dirtyAt"),
            "source": source, "link": GH + "#local=%s" % get(x, "name", "id", default="")}


def local_copies(results):
    gh = ok_data(results, "github") or {}
    repo = ok_data(results, "repo") or {}
    out, seen = [], {}
    for x in as_list(get(gh, "locals", "localCopies", "local", "copies", "clones")):
        if isinstance(x, dict):
            n = norm_local(x, "github")
            seen[n["name"]] = n
            out.append(n)
    for x in repo.get("locals") or []:
        if not x.get("present"):
            continue
        n = norm_local(x, "repo")
        if n["name"] in seen:  # (the GitHub Manager's view wins; fill what it left out)
            for k, v in n.items():
                if seen[n["name"]].get(k) is None:
                    seen[n["name"]][k] = v
        else:
            out.append(n)
    return out


# ---- Claude Manager (8766) -----------------------------------------------------------------------------------

BUCKET_ORDER = ("push", "owner", "pr", "ci", "tests", "claude")  # (owner + push = "q needs to push": push)


def bucket(waiting):
    """One bucket for a waitingOn string or list (the Claude Manager sends a list, e.g. ["owner", "push"])."""
    if isinstance(waiting, (list, tuple)):
        found = {bucket(w) for w in waiting if isinstance(w, str)}
        return next((b for b in BUCKET_ORDER if b in found), "other")
    w = (waiting or "").strip().lower() if isinstance(waiting, str) else ""
    if not w:
        return "other"
    if w in ("q", "you", "user", "owner") or "owner" in w or w.startswith("q "):
        return "owner"
    for b, keys in (("push", ("push",)), ("pr", ("pr", "pull")), ("ci", ("ci", "action", "run")),
                    ("tests", ("test",)), ("claude", ("claude", "agent", "thread"))):
        if any(w == k or w.startswith(k) for k in keys):
            return b
    return "other"


def entry_link(eid):
    return DOCS + "#entry=" + urllib.parse.quote(str(eid), safe=":/-_.~") if eid is not None else DOCS + "#tab=board"


def _waiting_text(w):
    if isinstance(w, (list, tuple)):
        return ", ".join(str(x) for x in w if x not in (None, "none")) or None
    return w if isinstance(w, str) and w else None


def in_progress(d):
    """The Claude Manager's inProgress (waitingOn may be a list), with `bucket` and a deep link to the entry."""
    by_id = {}  # (the Claude Manager's waiting{bucket: [ids]}, used when an entry has no waitingOn of its own)
    waiting_map = get(d, "waiting")
    for b, ids in (waiting_map.items() if isinstance(waiting_map, dict) else []):
        for i in (ids if isinstance(ids, list) else []):
            by_id.setdefault(str(i), []).append(b)
    out = []
    for e in as_list(get(d, "inProgress", "in_progress", "inprogress", "entries", "active")):
        if not isinstance(e, dict):
            continue
        status = get(e, "status", "state", default="")
        if isinstance(status, str) and status.lower() in ("done", "finished", "closed", "shipped", "fixed", "answered"):
            continue
        eid = get(e, "id", "entryId", "key")
        url = get(e, "threadUrl", "thread_url", "threadLink", "url", "link")
        waiting = get(e, "waitingOn", "waiting_on", "waiting", "bucket", "waitingFor")
        if not waiting and eid is not None:
            waiting = by_id.get(str(eid))
        wl = [w for w in (waiting if isinstance(waiting, list) else [waiting]) if isinstance(w, str) and w and w != "none"]
        prio = get(e, "priority", "prio")
        halting = any(_has_halt(x) for x in (status, prio, waiting)) or \
            "(HALT)" in str(get(e, "title", "name", default=""))  # (the docs write the priority into the title)
        out.append({"id": eid, "title": get(e, "title", "name", "text", default="(untitled)"),
                    "thread": get(e, "thread", "threadName", "thread_name"), "threadUrl": url,
                    "status": status, "where": get(e, "where", "location", "branch"),
                    "waitingOn": _waiting_text(waiting), "waitingOnList": wl,
                    "waitingWhy": get(e, "waitingWhy", "waiting_why", "why", "reason"),
                    "date": get(e, "date"), "lastChanged": get(e, "lastChanged"),
                    "bucket": bucket(wl), "link": entry_link(eid) if eid is not None else (url or entry_link(None)),
                    "entryLink": entry_link(eid), "priority": prio if isinstance(prio, str) else None,
                    "halting": halting, "since": get(e, "since", "haltingSince"), "blocks": get(e, "blocks"),
                    "source": "docs"})
    return out


def _has_halt(x):
    """True when a status / priority / waitingOn (string or list) says "halting" (or the docs' "(HALT)")."""
    if isinstance(x, (list, tuple)):
        return any(_has_halt(y) for y in x)
    return isinstance(x, str) and "halt" in x.lower()


# ---- halting: what q must do that blocks a Claude agent (shown in red at the top) -----------------------------

def _halt_time(h):
    return util.parse_time(h.get("since")) or 0


def docs_halting(d, ip):
    """The Claude Manager's halting list, plus in-progress entries marked halting (status / priority / waitingOn),
    one item per entry id, newest first."""
    out, seen = [], set()
    for h in as_list(get(d, "halting", "halts", "haltingEntries")):
        if isinstance(h, str):
            h = {"what": h}
        if not isinstance(h, dict):
            continue
        hid = get(h, "id", "entryId")
        title = get(h, "title", "name")
        what = get(h, "what", "action", "todo", "text") or title or "(no details)"
        thread_url = get(h, "threadUrl", "thread_url", "url")
        lk = get(h, "link")
        out.append({"id": hid, "title": title or what, "what": what, "blocks": get(h, "blocks", "blocking"),
                    "thread": get(h, "thread", "threadName"), "threadUrl": thread_url,
                    "since": get(h, "since", "date", "time", "createdAt"),
                    "link": lk if isinstance(lk, str) and lk else (entry_link(hid) if hid is not None else thread_url),
                    "source": "docs"})
        if hid is not None:
            seen.add(str(hid))
    for e in ip:
        if not e.get("halting") or (e.get("id") is not None and str(e["id"]) in seen):
            continue
        out.append({"id": e.get("id"), "title": e["title"], "what": e.get("waitingWhy") or e["title"],
                    "blocks": e.get("blocks") or e["title"], "thread": e.get("thread"), "threadUrl": e.get("threadUrl"),
                    "since": e.get("since") or e.get("date"), "link": e.get("link"), "source": "docs"})
    out.sort(key=_halt_time, reverse=True)  # (stable: undated ones keep their order, last)
    return out


ORACLE_HALT = (("ssh key not found", "Put the Oracle SSH key where Quark\\project-manager\\config.json says (oracle.key), or fix that path"),
               ("ssh.exe not found", "Install the Windows OpenSSH client (Settings > Apps > Optional features)"),
               ("not valid", "Fix oracle.host / oracle.user in Quark\\project-manager\\config.json"),
               ("timed out", "Check that the Oracle box is up and reachable over SSH (it timed out)"),
               ("timeout", "Check that the Oracle box is up and reachable over SSH (it timed out)"),
               ("permission denied", "Fix the Oracle SSH login (the key was refused: permission denied)"),
               ("host key", "Check the Oracle box's SSH host key (it changed or is unknown)"))


def board_halting(results):
    """The Supervisor Board's own sources that only q can fix (today: the Oracle box's SSH check)."""
    out = []
    r = results.get("oracle") or {}
    if not r.get("ok") and not r.get("pending"):
        err = str(r.get("error") or "")
        fix = next((f for k, f in ORACLE_HALT if k in err.lower()), None)
        if fix:
            out.append({"id": None, "title": "Oracle box check failing", "what": fix,
                        "blocks": "the Oracle box status (servers and bot units) for Claude and this board",
                        "detail": err[:200], "thread": None, "threadUrl": None,
                        "since": r.get("lastOkAt") or r.get("fetchedAt"), "link": None, "source": "oracle"})
    return out


def updates_halting(u):
    """From pm/updater.py's view (/api/pm `updates`): repository\\ not fast-forwardable, installs that failed."""
    out = []
    if not isinstance(u, dict) or u.get("enabled") is False:
        return out
    repo = u.get("repo") if isinstance(u.get("repo"), dict) else {}
    if repo.get("ffOk") is False and repo.get("error"):
        out.append({"id": None, "title": "repository\\ not updated",
                    "what": "Fix Quark\\repository\\ so it can fast-forward to testing: %s" % str(repo["error"])[:200],
                    "blocks": "installing Claude's merged tool changes on this PC",
                    "thread": None, "threadUrl": None, "since": u.get("lastRunAt"), "link": None, "source": "updates"})
    for e in as_list(u.get("entries")):
        if isinstance(e, dict) and e.get("state") == "failed":
            name = e.get("name") or e.get("id") or "a tool"
            code = e.get("exitCode")
            out.append({"id": None, "title": "%s install failed" % name,
                        "what": "Fix the %s install (%slog under Tools & updates), then Update now" %
                                (name, "exit %s; " % code if code is not None else ""),
                        "blocks": "the new %s from testing" % name, "thread": None, "threadUrl": None,
                        "since": e.get("lastRunAt"), "link": None, "source": "updates"})
    return out


def with_updates(derived, updates):
    """`derived` plus the updater's halting items (a copy: the store's dict is shared between requests)."""
    extra = updates_halting(updates)
    if not extra:
        return derived
    d = dict(derived)
    d["halting"] = list(derived.get("halting") or []) + extra
    d["haltingCount"] = len(d["halting"])
    return d


def questions(d):
    out = []
    for q in as_list(get(d, "questions", "openQuestions", "questionsForQ", "open_questions", "askQ")):
        if isinstance(q, str):
            q = {"text": q}
        if not isinstance(q, dict):
            continue
        qid = get(q, "id", "entryId")
        out.append({"id": qid, "text": get(q, "text", "title", "question", default=""), "body": get(q, "body"),
                    "thread": get(q, "thread"), "threadUrl": get(q, "threadUrl"),
                    "recommendation": get(q, "recommendation"),
                    "askedAt": get(q, "askedAt", "date", "time", "createdAt"),
                    "link": entry_link(qid) if qid is not None else (get(q, "url", "link", "threadUrl") or DOCS)})
    return out


# ---- Editor Manager (8765) --------------------------------------------------------------------------------------

def human_pct(d):
    v = util.to_float(get(d, "humanPct", "human_pct", "percentHuman", "provenance.pct_human", "provenance.humanPct",
                          "provenance.percentHuman", "provenance.human_pct", "totals.humanPct", "provenance.totals.humanPct"))
    if v is None:
        t = get(d, "provenance.totals", "provenance", "totals") or {}
        h = util.to_float(get(t, "human"), 0.0)
        rest = sum(util.to_float(get(t, k), 0.0) for k in ("ai", "thirdParty", "third_party", "thirdparty"))
        if h + rest > 0:
            v = 100.0 * h / (h + rest)
    return round(v, 1) if v is not None else None


def assets(d):
    """The Editor Manager's provenance in one shape: by_category may be a dict or a list, snake or camel case."""
    p = get(d, "provenance") or {}
    cats = get(p, "by_category", "byCategory", "categories")
    rows = []
    for c in as_list(cats):
        if isinstance(c, dict):
            h = util.to_int(get(c, "human"), 0)
            t = util.to_int(get(c, "third_party", "thirdParty", "third-party"), 0)
            a = util.to_int(get(c, "ai"), 0)
            rows.append({"category": get(c, "category", "name"), "total": util.to_int(get(c, "total"), h + t + a),
                         "human": h, "thirdParty": t, "ai": a,
                         "link": DASH + "#view=assets&category=%s" % get(c, "category", "name")})
    return {"total": util.to_int(get(p, "total")), "human": util.to_int(get(p, "human")),
            "thirdParty": util.to_int(get(p, "third_party", "thirdParty")), "ai": util.to_int(get(p, "ai")),
            "humanPct": human_pct(d), "byCategory": rows,
            "pendingMarks": util.to_int(get(d, "pending_marks", "pendingMarks")),
            "remaining": util.to_int(get(d, "remaining")), "generatedAt": get(d, "generated_at", "generatedAt"),
            "link": DASH + "#view=assets"}


def programs(d):
    out = []
    for p in as_list(get(d, "programs")):
        if isinstance(p, dict):
            pid = get(p, "id", "name")
            out.append({"id": pid, "name": get(p, "name", "id"),
                        "aheadTesting": util.to_int(get(p, "ahead_testing", "aheadTesting")),
                        "aheadProduction": util.to_int(get(p, "ahead_production", "aheadProduction")),
                        "uncommitted": util.to_int(get(p, "uncommitted")),
                        "filesChanged": util.to_int(get(p, "files_changed", "filesChanged")),
                        "link": DASH + "#view=changes&program=%s" % pid})
    return out


# ---- the derived block ----------------------------------------------------------------------------------------

def _build(results, name):
    for f in (ok_data(results, "builds") or {}).get("folders", []):
        if f.get("name") == name:
            return f
    return None


def versions(results):
    gh = ok_data(results, "github")
    repo = ok_data(results, "repo") or {}
    ed = ok_data(results, "editors") or {}
    t = gh_testing(gh) if gh else {}
    p = gh_production(gh) if gh else {}
    b_prod, b_test, b_work = _build(results, "production"), _build(results, "testing"), _build(results, "working")
    production = p.get("version") or \
        util.parse_version(get(ed, "production.version", "production.tag", "versions.production", "productionVersion")) or \
        util.base_version((b_prod or {}).get("version"))
    testing = t.get("version") or util.parse_version(repo.get("version")) or \
        util.parse_version(get(ed, "testing.version", "versions.testing", "testingVersion"))
    pre = (t.get("prerelease") or {}).get("tag")
    if not pre and b_test and str(b_test.get("tag") or "").startswith("test-"):
        pre = b_test["tag"]
    # the version a testing server registers with (QUARK_VERSION of the CI build: <VERSION>-test.<run>)
    testing_server = None
    for cand in (util.parse_version(pre), (b_test or {}).get("version")):
        if cand and "-test" in cand and (not testing or util.base_version(cand) == util.base_version(testing)):
            testing_server = cand
            break
    next_free = None
    bv = util.base_version(testing)
    if bv:
        a, b, c = bv.split(".")
        next_free = "%s.%s.%d" % (a, b, int(c) + 1)
    return {"production": production, "testing": testing, "testingPrerelease": pre,
            "working": util.base_version((b_work or {}).get("version")) if b_work else None,
            "nextFree": next_free, "testingServer": testing_server}


def _age_ok(ts, now, limit):
    t = util.parse_time(ts)
    return t is not None and now - t > limit


def derive(results, now=None):
    now = now or time.time()
    gh, docs, ed = ok_data(results, "github"), ok_data(results, "docs"), ok_data(results, "editors")
    repo, srv, orc = ok_data(results, "repo") or {}, ok_data(results, "servers"), ok_data(results, "oracle")
    chk = ok_data(results, "checklist")
    v = versions(results)
    t = gh_testing(gh) if gh else {}
    p = gh_production(gh) if gh else {}
    prs = gh_prs(gh) if gh else []
    runs = gh_runs(gh) if gh else {"running": [], "recent": [], "lastFailed": None, "lastTesting": None}
    locs = local_copies(results)
    loc = {x["name"]: x for x in locs}
    ip = in_progress(docs) if docs else []
    qs = questions(docs) if docs else []
    log0 = (repo.get("log") or [{}])[0] if repo.get("log") else {}
    testing_sha = t.get("sha") or log0.get("sha")
    ci = testing_ci(runs, testing_sha)
    tkey, pkey = util.version_key(util.base_version(v["testing"])), util.version_key(v["production"])
    testing_ahead = None if not v["testing"] or not v["production"] else tkey > pkey
    prod_pr = next((x for x in prs if x.get("head") == "testing" and x.get("base") != "testing"), None)

    # pipeline ---------------------------------------------------------------------------------------------------
    bw = _build(results, "working") or {}
    wr = loc.get("working-repo") or {}
    w_notes, w_state = [], "unknown"
    if wr:
        w_state = "ok"
        if (wr.get("ahead") or 0) > 0:
            w_state = "ahead"
            w_notes.append("%d commit%s not on testing" % (wr["ahead"], "" if wr["ahead"] == 1 else "s"))
        if (wr.get("behind") or 0) > 0:
            w_state = "behind" if w_state == "ok" else w_state
            w_notes.append("%d behind testing" % wr["behind"])
        if wr.get("dirty"):
            n = wr.get("dirtyFiles")
            w_notes.append("%s uncommitted" % ("%d file%s" % (n, "" if n == 1 else "s") if n else "changes"))
        if bw.get("commit") and wr.get("sha") and not str(wr["sha"]).startswith(bw["commit"]):
            w_notes.append("working\\ build is not the latest commit")
    elif bw.get("version"):
        w_state = "ok"
    t_notes = []
    if v["testingPrerelease"]:
        t_notes.append("prerelease " + v["testingPrerelease"])
    if any(r.get("branch") == "testing" for r in runs["running"]):
        t_notes.append("CI running")
    if ci and str(ci.get("conclusion") or "").lower() in FAILED:
        t_notes.append("last CI run failed")
    p_notes = []
    if testing_ahead:
        p_notes.append("PR #%s open" % prod_pr["number"] if prod_pr else "waiting for the hand-test")
    if srv and (srv.get("production") or {}).get("count") is not None:
        n = srv["production"]["count"]
        p_notes.append("%d server%s listed" % (n, "" if n == 1 else "s"))
    pre = t.get("prerelease") or {}
    pipeline = [
        {"stage": "working", "version": v["working"], "sha": bw.get("commit") or util.short_sha(wr.get("sha")),
         "state": w_state, "note": ", ".join(w_notes), "time": bw.get("mtime"), "source": "builds+repo"},
        {"stage": "testing", "version": v["testing"], "sha": util.short_sha(testing_sha),
         "state": "unknown" if testing_ahead is None else ("ahead" if testing_ahead else "ok"),
         "note": ", ".join(t_notes), "time": pre.get("publishedAt") or t.get("committedAt") or log0.get("time"),
         "source": "github" if gh else "repo"},
        {"stage": "production", "version": v["production"], "sha": util.short_sha(p.get("sha")),
         "state": "unknown" if testing_ahead is None else ("behind" if testing_ahead else "ok"),
         "note": ", ".join(p_notes), "time": p.get("publishedAt"), "source": "github" if gh else "builds"},
    ]
    arrows = [
        {"from": "working", "to": "testing",
         "label": ("%d commit%s" % (wr["ahead"], "" if wr["ahead"] == 1 else "s")) if (wr.get("ahead") or 0) > 0
         else ("uncommitted changes" if wr.get("dirty") else ("up to date" if wr else ""))},
        {"from": "testing", "to": "production",
         "label": ("PR #%s open" % prod_pr["number"] if prod_pr else "hand-test checklist") if testing_ahead
         else ("same version" if testing_ahead is False else ""),
         "link": prod_pr["link"] if prod_pr and testing_ahead else (CHECKLIST_URL if testing_ahead else None)},
    ]

    # next steps -------------------------------------------------------------------------------------------------
    steps = []
    for q in qs:
        steps.append({"text": "Answer: " + q["text"], "owner": "q", "link": q["link"], "source": "docs",
                      "why": "open question in the Claude Documents" + (" (%s)" % q["thread"] if q.get("thread") else "")})
    for e in ip:
        if e["bucket"] == "owner":
            steps.append({"text": e.get("waitingWhy") or e["title"], "owner": "q", "link": e["link"], "source": "docs",
                          "why": "\"%s\" is waiting on you" % e["title"]})
        elif e["bucket"] == "push":
            steps.append({"text": "Say push for \"%s\"" % (e.get("thread") or e["title"]), "owner": "q",
                          "link": e["link"], "source": "docs",
                          "why": "in-progress entry waiting on push" + (": " + e["waitingWhy"] if e.get("waitingWhy") else "")})
    if testing_ahead:
        steps.append({"text": "Hand-test %s with RELEASE_CHECKLIST" % v["testing"], "owner": "q", "link": CHECKLIST_URL,
                      "source": "derived", "why": "testing (%s) is ahead of production (%s)" % (v["testing"], v["production"]),
                      "steps": (chk or {}).get("headings") or []})
    for x in locs:
        if (x.get("ahead") or 0) > 0:
            steps.append({"text": "%s has %d commit%s not on testing" % (x["name"], x["ahead"], "" if x["ahead"] == 1 else "s"),
                          "owner": "q", "link": x["link"], "source": x["source"], "why": "local copy ahead of testing"})
    for e in ip:
        if e["bucket"] in ("pr", "ci", "tests", "claude"):
            steps.append({"text": e["title"] + (": " + e["waitingWhy"] if e.get("waitingWhy") else ""), "owner": "claude",
                          "link": e["link"], "source": "docs", "why": "in-progress entry waiting on %s" % (e.get("waitingOn") or e["bucket"])})

    # attention --------------------------------------------------------------------------------------------------
    att = []
    if ci and str(ci.get("conclusion") or "").lower() in FAILED:
        att.append({"level": "red", "text": "Last CI run on testing failed (%s)" % (ci.get("name") or ci.get("conclusion")),
                    "link": ci.get("link"), "source": "github"})
    if orc:
        for name, u in (orc.get("units") or {}).items():
            u = u if isinstance(u, dict) else {}
            state, sub = u.get("active"), u.get("sub")
            if state == "active" or (state is None and not orc.get("complete", True)):
                continue
            state = state or "unknown"
            att.append({"level": "amber" if name.endswith(".timer") else "red",
                        "text": "%s is %s%s on the Oracle box" % (name, state, "/" + sub if sub and sub != state else ""),
                        "link": None, "source": "oracle"})
    for key, label in SOURCE_LABELS.items():
        r = results.get(key) or {}
        if r.get("pending"):
            continue
        if not r.get("ok"):
            fix = SOURCE_FIX.get(key)
            err = r.get("error") or "not available"
            att.append({"level": "amber", "text": err if err.lower().startswith(label.lower()[:12]) else "%s: %s" % (label, err),
                        "fix": fix, "link": None, "source": key})
        elif r.get("stale"):
            att.append({"level": "amber", "text": "%s: %s" % (label, r.get("note") or "showing cached data"),
                        "fix": SOURCE_FIX.get(key), "link": None, "source": key})
    poll_err = get(gh, "poll.lastError") if gh else None
    if poll_err:
        att.append({"level": "amber", "text": "GitHub Manager: last poll failed (%s)" % str(poll_err)[:160],
                    "link": GH, "source": "github"})
    if srv and (srv.get("production") or {}).get("count") == 0:
        att.append({"level": "red", "text": "No production servers in the public list (%s)" % srv["production"].get("version"),
                    "link": None, "source": "servers"})
    for x in locs:
        if x.get("dirty") and _age_ok(x.get("dirtySince"), now, DAY):
            n = x.get("dirtyFiles")
            att.append({"level": "amber", "text": "%s has uncommitted changes older than a day%s" %
                        (x["name"], " (%d file%s)" % (n, "" if n == 1 else "s") if n else ""),
                        "link": x["link"], "source": x["source"]})
    att.sort(key=lambda a: 0 if a["level"] == "red" else 1)

    # halting (shown above everything; not repeated under Needs you) ----------------------------------------------
    halting = (docs_halting(docs, ip) if docs else []) + board_halting(results)
    halt_links = {h["link"] for h in halting if h.get("link")}
    halt_ids = {str(h["id"]) for h in halting if h.get("id") is not None}
    halt_sources = {h["source"] for h in halting if h["source"] != "docs"}
    steps = [s for s in steps if not (s.get("link") in halt_links and s.get("source") == "docs")]
    steps = [s for s in steps if not any(s.get("link") == entry_link(i) for i in halt_ids)]
    att = [a for a in att if not (a.get("source") in halt_sources and a.get("link") is None and
                                  a["text"].startswith(SOURCE_LABELS.get(a["source"], "?")))]

    return {"versions": v, "pipeline": pipeline, "arrows": arrows, "inProgress": ip, "nextSteps": steps,
            "attention": att, "halting": halting, "haltingCount": len(halting), "humanPct": human_pct(ed) if ed else None,
            # normalized copies of what the page needs most (the raw data stays in `sources`)
            "questions": qs, "prs": prs, "runs": {"running": runs["running"], "lastFailed": runs["lastFailed"],
                                                  "lastTesting": ci or runs["lastTesting"]},
            "github": {"repo": get(gh, "repo"), "poll": get(gh, "poll"), "via": (results.get("github") or {}).get("via")}
            if gh else None,
            "localCopies": locs, "programs": programs(ed) if ed else [], "assets": assets(ed) if ed else None}

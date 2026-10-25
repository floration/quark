#!/usr/bin/env python3
"""GitHub lookups for the Claude Documents manager (PRs, CI runs, releases, branches, commits of floration/quark)
through the `gh` CLI, with an on-disk cache, plus the link finder that turns entry text into link chips.

Sources, in order: a fixture file (--gh-fixture, for tests), the GitHub manager's shared file when it is younger
than 5 minutes (--gh-shared), else `gh`. Anything missing or failing leaves ok = False and empty lists; the rest of
the manager works without them. Python 3 standard library only.
"""
import datetime, json, os, re, subprocess, sys, time

REPO = "floration/quark"
GH = "https://github.com/" + REPO
SHARED_MAX_AGE = 300            # s: the GitHub manager's file counts as fresh this long
COMMIT_RECHECK = 1800           # s: re-ask about a commit GitHub did not have
COMMIT_LOOKUPS = 40             # most commit lookups per refresh (API budget)


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_iso(s):
    try:
        return datetime.datetime.fromisoformat(str(s).replace("Z", "+00:00"))
    except Exception:
        return None


def run(cmd, timeout=60, cwd=None):
    """(returncode, stdout bytes, stderr text) of an argument list, no shell, no console window on Windows."""
    kw = {}
    if os.name == "nt": kw["creationflags"] = 0x08000000                 # CREATE_NO_WINDOW
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never", GH_PROMPT_DISABLED="1", NO_COLOR="1")
    try:
        p = subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, env=env, **kw)
        return p.returncode, p.stdout, p.stderr.decode("utf-8", "replace")
    except FileNotFoundError:
        return 127, b"", "%s not found" % cmd[0]
    except subprocess.TimeoutExpired:
        return 124, b"", "%s timed out after %d s" % (cmd[0], timeout)
    except Exception as e:
        return 1, b"", str(e)


# ---- normalizing gh / REST / shared-file records -----------------------------------------------------------

def _get(d, *keys):
    for k in keys:
        cur = d
        for part in k.split("."):
            cur = cur.get(part) if isinstance(cur, dict) else None
        if cur not in (None, ""): return cur
    return None


def checks_state(rollup):
    """statusCheckRollup list -> success | failure | in_progress | None."""
    if isinstance(rollup, str): return _run_state(rollup, rollup)
    if not rollup: return None
    states = []
    for c in rollup:
        st = (c.get("status") or "").upper()
        concl = (c.get("conclusion") or c.get("state") or "").upper()
        if st and st != "COMPLETED": states.append("in_progress")
        elif concl in ("FAILURE", "ERROR", "TIMED_OUT", "CANCELLED", "ACTION_REQUIRED", "STARTUP_FAILURE"): states.append("failure")
        elif concl in ("PENDING", "EXPECTED", "QUEUED", "IN_PROGRESS"): states.append("in_progress")
        else: states.append("success")
    return "failure" if "failure" in states else "in_progress" if "in_progress" in states else "success"


def _run_state(status, conclusion):
    st, c = (status or "").lower(), (conclusion or "").lower()
    if st and st != "completed": return "in_progress"
    if c == "success": return "success"
    if c in ("failure", "timed_out", "startup_failure", "action_required", "error"): return "failure"
    if c in ("pending", "queued", "in_progress"): return "in_progress"
    return None


def norm_pr(p):
    merged = _get(p, "mergedAt", "merged_at")
    state = (_get(p, "state") or "").lower()
    if merged: state = "merged"
    elif state not in ("open", "closed", "merged"): state = "closed" if state else None
    n = _get(p, "number")
    return {"number": int(n) if n is not None else None, "title": _get(p, "title") or "", "state": state,
            "mergedAt": merged, "headRefName": _get(p, "headRefName", "head.ref", "headBranch"),
            "url": _get(p, "html_url", "url") or ("%s/pull/%s" % (GH, n)),
            "checks": checks_state(_get(p, "statusCheckRollup", "checks")), "updatedAt": _get(p, "updatedAt", "updated_at")}


def norm_run(r):
    rid = _get(r, "databaseId", "id")
    status, concl = _get(r, "status"), _get(r, "conclusion")
    return {"databaseId": rid, "number": _get(r, "number", "run_number"), "headBranch": _get(r, "headBranch", "head_branch"),
            "status": status, "conclusion": concl, "state": _run_state(status, concl),
            "url": _get(r, "html_url", "url") or ("%s/actions/runs/%s" % (GH, rid)),
            "createdAt": _get(r, "createdAt", "created_at"), "headSha": _get(r, "headSha", "head_sha"),
            "displayTitle": _get(r, "displayTitle", "display_title") or "",
            "workflowName": _get(r, "workflowName", "name") or ""}


def norm_release(r):
    tag = _get(r, "tagName", "tag_name")
    pre = _get(r, "isPrerelease", "prerelease")
    return {"tagName": tag, "name": _get(r, "name") or tag, "isPrerelease": bool(pre),
            "publishedAt": _get(r, "publishedAt", "published_at"),
            "url": _get(r, "html_url", "url") or "%s/releases/tag/%s" % (GH, tag)}


def normalize(raw):
    """Any of: gh --json output, REST output, the GitHub manager's file -> {prs, runs, releases, branches}."""
    raw = raw if isinstance(raw, dict) else {}
    def lst(*keys):
        for k in keys:
            v = raw.get(k)
            if isinstance(v, dict): v = v.get("workflow_runs") or v.get("items")
            if isinstance(v, list): return [x for x in v if isinstance(x, dict)]
        return []
    out = {"prs": [norm_pr(p) for p in lst("prs", "pulls", "pullRequests")],
           "runs": [norm_run(r) for r in lst("runs", "workflowRuns", "workflow_runs")],
           "releases": [norm_release(r) for r in lst("releases") if _get(r, "tagName", "tag_name")]}
    for p in out["prs"]: p["merged"] = p["state"] == "merged"                  # short aliases the page also reads
    for r in out["runs"]: r["id"], r["branch"] = r["databaseId"], r["headBranch"]
    for r in out["releases"]: r["tag"], r["prerelease"] = r["tagName"], r["isPrerelease"]
    br = raw.get("branches")
    if isinstance(br, list): out["branches"] = sorted({b if isinstance(b, str) else b.get("name") for b in br} - {None})
    out["runs"].sort(key=lambda r: r["createdAt"] or "", reverse=True)
    return out


# ---- fetching --------------------------------------------------------------------------------------------

def _gh_json(args, gh="gh"):
    code, out, err = run([gh] + args, timeout=90)
    if code != 0: raise RuntimeError(err.strip().splitlines()[-1] if err.strip() else "gh exit %d" % code)
    return json.loads(out.decode("utf-8") or "null")


def fetch_gh(gh="gh", repo=REPO):
    """PRs, runs, releases and branch names from gh; raises on failure of the core lists."""
    try:
        prs = _gh_json(["pr", "list", "--repo", repo, "--state", "all", "--limit", "100", "--json",
                        "number,title,state,mergedAt,headRefName,url,statusCheckRollup,updatedAt"], gh)
    except RuntimeError:                                                   # (GraphQL refused: REST has no checks)
        prs = _gh_json(["api", "repos/%s/pulls?state=all&per_page=100" % repo], gh)
    runs = _gh_json(["run", "list", "--repo", repo, "--limit", "100", "--json",
                     "databaseId,number,headBranch,status,conclusion,url,createdAt,headSha,displayTitle,workflowName"], gh)
    try:
        rels = _gh_json(["release", "list", "--repo", repo, "--limit", "100", "--json",
                         "tagName,name,isPrerelease,publishedAt"], gh)
    except RuntimeError:
        rels = _gh_json(["api", "repos/%s/releases?per_page=100" % repo], gh)
    raw = {"prs": prs, "runs": runs, "releases": rels}
    try:
        raw["branches"] = [b.get("name") for b in _gh_json(["api", "--paginate", "repos/%s/branches?per_page=100" % repo], gh)]
    except Exception:
        pass
    return normalize(raw)


def read_shared(path, max_age=SHARED_MAX_AGE):
    """The GitHub manager's shared file if it is fresh (its generatedAt, else mtime) and readable, else None."""
    try:
        with open(path, "rb") as f: raw = json.loads(f.read().decode("utf-8"))
        if not isinstance(raw, dict): return None
        t = parse_iso(raw.get("generatedAt"))
        age = (datetime.datetime.now(datetime.timezone.utc) - t).total_seconds() if t and t.tzinfo else time.time() - os.path.getmtime(path)
        if age > max_age or age < -300: return None
        data = normalize(raw)
        return data if (data["prs"] or data["runs"] or data["releases"]) else None
    except Exception:
        return None


class GitHub:
    """Cached GitHub state. refresh() is called from the background thread; data() is safe to read any time."""

    def __init__(self, cache_path, fixture=None, shared=None, disabled=False, gh="gh", repo=REPO, log=None):
        self.cache_path, self.fixture, self.shared, self.disabled, self.gh, self.repo = cache_path, fixture, shared, disabled, gh, repo
        self.log = log or (lambda msg: None)
        self.state = {"ok": False, "fetchedAt": None, "source": None, "error": None, "prs": [], "runs": [],
                      "releases": [], "branches": None, "commits": {}}
        self._load()

    def _load(self):
        if self.fixture or not self.cache_path: return
        try:
            with open(self.cache_path, encoding="utf-8") as f: c = json.load(f)
            for k in ("prs", "runs", "releases", "branches", "commits", "fetchedAt"):
                if k in c: self.state[k] = c[k]
            self.state["ok"] = False                                     # cached, not fresh: refresh decides
            self.state["source"] = "cache"
        except Exception:
            pass

    def _save(self):
        if self.fixture or not self.cache_path: return
        try:
            os.makedirs(os.path.dirname(self.cache_path), exist_ok=True)
            tmp = self.cache_path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f: json.dump(self.state, f, indent=1)
            os.replace(tmp, self.cache_path)
        except Exception as e:
            self.log("cache write failed: %s" % e)

    def refresh(self, wanted_commits=()):
        """Fetch everything; returns True when the data changed."""
        before = json.dumps(self.public(), sort_keys=True)
        if self.fixture:
            with open(self.fixture, encoding="utf-8") as f: raw = json.load(f)
            d = normalize(raw)
            self.state.update(d, ok=True, fetchedAt=raw.get("generatedAt") or now_iso(), source="fixture", error=None)
            self.state["commits"] = {k: {"exists": bool(v), "checkedAt": now_iso()} for k, v in (raw.get("commits") or {}).items()}
            return before != json.dumps(self.public(), sort_keys=True)
        if self.disabled:
            self.state.update(ok=False, source="disabled", error="--no-gh")
            return before != json.dumps(self.public(), sort_keys=True)
        d, source = (read_shared(self.shared), "shared") if self.shared else (None, None)
        try:
            if d is None:
                d, source = fetch_gh(self.gh, self.repo), "gh"
            elif "branches" not in d:
                try: d["branches"] = [b.get("name") for b in _gh_json(["api", "--paginate", "repos/%s/branches?per_page=100" % self.repo], self.gh)]
                except Exception: d["branches"] = self.state.get("branches")
            self.state.update(d, ok=True, fetchedAt=now_iso(), source=source, error=None)
            self._check_commits(wanted_commits)
        except Exception as e:
            if self.state.get("error") != str(e): self.log("GitHub lookup failed: %s" % e)
            self.state.update(ok=False, error=str(e))
        self._save()
        return before != json.dumps(self.public(), sort_keys=True)

    def _check_commits(self, shas):
        """Does GitHub have these commits (pushed) or not (local only)? Known ones are kept for good."""
        commits, n, t = self.state.setdefault("commits", {}), 0, time.time()
        known = {r.get("headSha") for r in self.state["runs"] if r.get("headSha")}
        for sha in shas:
            if any(k.startswith(sha) for k in known):
                commits[sha] = {"exists": True, "checkedAt": now_iso()}
                continue
            c = commits.get(sha)
            if c and (c.get("exists") or t - (parse_iso(c.get("checkedAt")) or datetime.datetime.min.replace(tzinfo=datetime.timezone.utc)).timestamp() < COMMIT_RECHECK):
                continue
            if n >= COMMIT_LOOKUPS: break
            n += 1
            code, out, err = run([self.gh, "api", "repos/%s/commits/%s" % (self.repo, sha), "--jq", ".sha"], timeout=30)
            if code == 0: commits[sha] = {"exists": True, "checkedAt": now_iso(), "sha": out.decode().strip()}
            elif "404" in err or "No commit found" in err or "422" in err: commits[sha] = {"exists": False, "checkedAt": now_iso()}

    def public(self):
        s = self.state
        return {"prs": s["prs"], "runs": s["runs"], "releases": s["releases"], "branches": s.get("branches")}


# ---- links ------------------------------------------------------------------------------------------------

_URL_RE = re.compile(r"https?://[^\s<>()\[\]`\"']+")
_MDLINK_RE = re.compile(r"\[([^\]]+)\]\((https?://[^\s)]+)\)")
_TAG_RE = re.compile(r"(?<![\w.-])(test-\d+\.\d+\.\d+(?:\.testing|-test)\.\d+|v\d+\.\d+\.\d+)(?![\w-]|\.\d)")
_VER_RE = re.compile(r"(?<![\w.])(\d\.\d{1,3}\.\d{1,3})(?![\w]|\.\d)")
_PR_RE = re.compile(r"\bPRs?\s*#?(\d{1,5})(?![\w.]|\.\d)((?:\s*(?:,|/|&|and|or)\s*#\d{1,5}\b)*)")
_HEX_RE = re.compile(r"(?<![\w/#])([0-9a-f]{7,40})(?![\w])")
_RUN_RE = re.compile(r"\b(?:CI\s+)?run\s+#?(\d{1,6})\b", re.I)
_RUNURL_RE = re.compile(r"/actions/runs/(\d+)")
_BRANCH_CLAUDE_RE = re.compile(r"(?<![\w/])(claude/[A-Za-z0-9][\w.-]*[A-Za-z0-9])")
_TICK_RE = re.compile(r"`([^`\n]{1,120})`")
_PATH_RE = re.compile(r"(?<![\w/.\\-])((?:src|tools|docs|tests|changelogs|assets|site|cmake|\.github)/[\w./-]*[\w/])(?::(\d+))?")


def _mk(kind, label, url, state=None, **extra):
    d = {"kind": kind, "label": label, "url": url, "state": state}
    d.update(extra)
    return d


def _thread_url(threads, name):
    """threads.json lookup: title or alias, case-insensitive; then an alias/title contained in the name."""
    if not name: return None, None
    low = name.strip().lower()
    for t in threads:
        if low == t["title"].lower() or low in [a.lower() for a in t.get("aliases", [])]: return t["title"], t.get("url")
    for t in threads:
        for a in [t["title"]] + t.get("aliases", []):
            if a and a.lower() in low: return t["title"], t.get("url")
    return None, None


class Linker:
    def __init__(self, gh_data, threads, commits=None):
        self.prs = {p["number"]: p for p in gh_data.get("prs", []) if p.get("number") is not None}
        self.runs = gh_data.get("runs", [])
        self.releases = gh_data.get("releases", [])
        self.branches = set(gh_data.get("branches") or [])
        self.have_branches = gh_data.get("branches") is not None
        self.commits = commits or {}
        self.threads = threads
        self.by_branch_pr = {}
        for p in sorted(self.prs.values(), key=lambda p: p.get("updatedAt") or ""):
            if p.get("headRefName"): self.by_branch_pr[p["headRefName"]] = p
        self.latest_run = {}
        for r in self.runs:                                                # newest first
            b = r.get("headBranch")
            if b and b not in self.latest_run: self.latest_run[b] = r
        self.run_by_number = {}
        for r in self.runs:
            if r.get("number") is not None: self.run_by_number.setdefault(int(r["number"]), r)
        self.run_by_id = {str(r.get("databaseId")): r for r in self.runs}
        self.thread_names = sorted([(t["title"], t) for t in threads], key=lambda x: -len(x[0]))
        self.thread_alias = sorted([(a, t) for t in threads for a in t.get("aliases", []) if len(a.split()) >= 2],
                                   key=lambda x: -len(x[0]))

    def thread(self, name):
        return _thread_url(self.threads, name)

    def pr_link(self, n):
        p = self.prs.get(n)
        if p:
            checks = p.get("checks")
            if checks is None and p.get("headRefName") in self.latest_run:     # (REST lists carry no checks)
                checks = self.latest_run[p["headRefName"]].get("state")
            return _mk("pr", "PR #%d" % n, p["url"], p["state"], checks=checks, title=p.get("title"), branch=p.get("headRefName"))
        return _mk("pr", "PR #%d" % n, "%s/pull/%d" % (GH, n), None)

    def run_link(self, r):
        label = "CI run %s" % (r.get("number") or r.get("databaseId"))
        return _mk("run", label, r["url"], r.get("state"), branch=r.get("headBranch"), workflow=r.get("workflowName"),
                   conclusion=r.get("conclusion"), createdAt=r.get("createdAt"), title=r.get("displayTitle"))

    def release_for(self, ver):
        """The release to show for a version: production v<ver> first, else the newest test prerelease."""
        prod = [r for r in self.releases if r["tagName"] == "v" + ver]
        if prod: return prod[0]
        tests = [r for r in self.releases if r["tagName"].startswith(("test-%s-test." % ver, "test-%s.testing." % ver))]
        tests.sort(key=lambda r: int(r["tagName"].rsplit(".", 1)[1]) if r["tagName"].rsplit(".", 1)[1].isdigit() else 0)
        return tests[-1] if tests else None

    def links(self, text, thread=None):
        """Every link chip found in text (entry body/title or commit subject), deduped, in reading order."""
        text = text or ""
        found = []                                                         # (pos, link)
        work = text

        def blank(m):
            nonlocal work
            work = work[:m.start()] + " " * (m.end() - m.start()) + work[m.end():]

        for m in list(_MDLINK_RE.finditer(text)):
            found.append((m.start(), self._url_link(m.group(2), m.group(1))))
            blank(m)
        for m in list(_URL_RE.finditer(work)):
            u = m.group(0).rstrip(".,;:!?*_")
            found.append((m.start(), self._url_link(u)))
            blank(m)
        for m in list(_TAG_RE.finditer(work)):
            tag = m.group(1)
            rel = next((r for r in self.releases if r["tagName"] == tag), None)
            found.append((m.start(), _mk("release", tag, rel["url"] if rel else "%s/releases/tag/%s" % (GH, tag), None,
                                         prerelease=rel["isPrerelease"] if rel else tag.startswith("test-"), known=bool(rel))))
            blank(m)
        for m in _PR_RE.finditer(work):
            nums = [m.group(1)] + re.findall(r"#(\d+)", m.group(2) or "")
            for n in nums: found.append((m.start(), self.pr_link(int(n))))
        for m in _RUNURL_RE.finditer(text):
            r = self.run_by_id.get(m.group(1))
            if r: found.append((m.start(), self.run_link(r)))
        for m in _RUN_RE.finditer(work):
            r = self.run_by_number.get(int(m.group(1)))
            if r: found.append((m.start(), self.run_link(r)))
        for m in _VER_RE.finditer(work):
            v = m.group(1)
            rel = self.release_for(v)
            if rel:
                found.append((m.start(), _mk("version", v, rel["url"], None, release=rel["tagName"],
                                             channel="testing" if rel["isPrerelease"] else "production")))
            else:
                found.append((m.start(), _mk("version", v, "%s/blob/testing/changelogs/%s.md" % (GH, v), None, release=None, channel=None)))
        for m in _HEX_RE.finditer(work):
            h = m.group(1)
            if not (re.search(r"\d", h) and re.search(r"[a-f]", h)): continue
            c = self.commits.get(h) or {}
            found.append((m.start(), _mk("commit", h[:7], "%s/commit/%s" % (GH, h), None, sha=h, exists=c.get("exists"))))
        branches = []
        for m in _BRANCH_CLAUDE_RE.finditer(work): branches.append((m.start(), m.group(1)))
        for m in _TICK_RE.finditer(work):
            name = m.group(1).strip()
            if not re.fullmatch(r"[A-Za-z0-9][\w./-]*[A-Za-z0-9]", name) or re.search(r"\.[A-Za-z]{1,4}$", name): continue
            near = work[max(0, m.start() - 16):m.end() + 9].lower()
            if name.startswith("claude/"): continue                          # (found above)
            if name in self.branches or ("branch" in near and name not in ("main", "master")):
                branches.append((m.start(), name))
        for m in re.finditer(r"\bbranch(?:es)?\s+([a-z][\w.-]*-[\w.-]*[a-z0-9])\b", work):
            branches.append((m.start(), m.group(1)))
        for pos, b in branches:
            exists = (b in self.branches) if self.have_branches else None
            r = self.latest_run.get(b)
            found.append((pos, _mk("branch", b, "%s/tree/%s" % (GH, b), r.get("state") if r else None, exists=exists)))
            if r: found.append((pos, self.run_link(r)))
            p = self.by_branch_pr.get(b)
            if p: found.append((pos, self.pr_link(p["number"])))
        for m in _PATH_RE.finditer(work):
            path, line = m.group(1).rstrip("/."), m.group(2)
            if "/" not in path: continue
            found.append((m.start(), _mk("file", path + (":" + line if line else ""),
                                         "%s/blob/testing/%s%s" % (GH, path, "#L" + line if line else ""), None)))
        low = work.lower()
        for name, t in self.thread_names:                                   # full titles anywhere
            i = low.find(name.lower())
            if i >= 0 and t.get("url"): found.append((i, _mk("thread", t["title"], t["url"], None)))
        for name, t in self.thread_alias:                                   # aliases only as '(Alias' or 'Alias thread'
            m = re.search(r"\(" + re.escape(name.lower()) + r"\b|\b" + re.escape(name.lower()) + r" thread\b", low)
            if m and t.get("url"): found.append((m.start(), _mk("thread", t["title"], t["url"], None)))
        if thread:
            title, url = self.thread(thread)
            if url: found.insert(0, (-1, _mk("thread", title, url, None)))
        found.sort(key=lambda x: x[0])
        out, seen = [], set()
        for _, l in found:
            key = (l["kind"], l["url"] or l["label"])
            if key in seen: continue
            seen.add(key)
            out.append(l)
        return out

    def _url_link(self, url, label=None):
        m = re.match(r"https://github\.com/floration/quark/pull/(\d+)", url)
        if m: return self.pr_link(int(m.group(1)))
        m = _RUNURL_RE.search(url)
        if m and "github.com/floration/quark/" in url:
            r = self.run_by_id.get(m.group(1))
            if r: return self.run_link(r)
        if re.match(r"https://claude\.ai/(?:code/)?artifact/", url): return _mk("artifact", label or "Artifact " + url.rsplit("/", 1)[-1][:8], url)
        if "claude.ai/code/" in url and "thread=" in url:
            title = next((t["title"] for t in self.threads if t.get("url") == url), None)
            return _mk("thread", label or title or "Thread", url)
        return _mk("url", label or re.sub(r"^https?://", "", url)[:60], url)


def commit_candidates(texts):
    """Short shas that look like commits of the game repo in these texts (for GitHub.refresh)."""
    out = []
    for t in texts:
        for m in _HEX_RE.finditer(_URL_RE.sub(" ", t or "")):
            h = m.group(1)
            if re.search(r"\d", h) and re.search(r"[a-f]", h) and h not in out: out.append(h)
    return out

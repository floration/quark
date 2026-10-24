#!/usr/bin/env python3
"""Claude Documents manager: a read-only local server that shows every Claude Documents entry, its history and
what it waits on, at http://127.0.0.1:8766/ (the page: index.html, manager.css, js/ beside this file).

    python3 serve.py [--docs "<PC Claude Documents folder>"] [--repo URL|path] [--port 8766] [--open]
    python3 serve.py --snapshot out.json [--gh-fixture tests/fixtures/github.json | --no-gh]
    python3 serve.py --ping                (exit 0 when a manager already answers on the port)

It never writes to the Claude Documents folder or its repo. History comes from its own bare clone in
<state dir>/mirror (fetched every --interval s); the PC folder is only read, to flag files edited there and not
synced yet. GitHub (gh CLI) is refreshed every 5 min into <state dir>/cache/github.json; the per-commit
entry diffs are cached by sha in <state dir>/cache/changes.json. Python 3 standard library only.

API: GET /api/data (everything, ETag / If-None-Match), GET /api/summary (small: in progress, waiting, questions,
recent changes), GET /api/health, POST /api/refresh (fetch now). CORS for the sibling tools on 8765-8770.
"""
import argparse, datetime, hashlib, http.server, json, os, re, subprocess, sys, threading, time, urllib.request, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import docsparse, ghlinks  # noqa: E402
from ghlinks import run, now_iso, parse_iso  # noqa: E402

VERSION = "1.0"
STATIC = {"index.html", "manager.css", "quark-tools.css"}              # plus anything under js/ (and fonts/, img/)
STATIC_DIRS = ("js/", "fonts/", "img/")
MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8", ".js": "text/javascript; charset=utf-8",
        ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png", ".ico": "image/x-icon",
        ".woff2": "font/woff2", ".woff": "font/woff", ".ttf": "font/ttf", ".otf": "font/otf", ".map": "application/json"}
CORS_PORTS = range(8765, 8771)
VOLATILE = ("generatedAt", "mirrorFetchedAt", "ghFetchedAt")


def log(msg):
    if sys.stderr is None: return                                           # (pythonw)
    line = "[docs-manager %s] %s\n" % (time.strftime("%H:%M:%S"), msg)
    try:
        sys.stderr.write(line)
    except UnicodeEncodeError:                                              # (a cp1252 console)
        sys.stderr.write(line.encode("ascii", "replace").decode())
    sys.stderr.flush()


def utc_of(iso):
    t = parse_iso(iso)
    return t.astimezone(datetime.timezone.utc).isoformat().replace("+00:00", "Z") if t else None


def norm_text(b):
    """Bytes -> text with LF line ends (a Windows checkout has CRLF; the repo has LF)."""
    return b.decode("utf-8", "replace").replace("\r\n", "\n")


def blob_id(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def web_url(repo):
    """https://github.com/owner/name for a GitHub remote, else None."""
    if not repo: return None
    m = re.match(r"(?:https?://(?:[^@/]+@)?github\.com/|git@github\.com:|ssh://git@github\.com/)([\w.-]+/[\w.-]+?)(?:\.git)?/?$", repo.strip())
    return "https://github.com/" + m.group(1) if m else None


# ---- the mirror: a bare clone read through one `git cat-file --batch` ---------------------------------------

class Mirror:
    def __init__(self, path, repo, branch="main"):
        self.path, self.repo, self.branch = path, repo, branch
        self.ref = "refs/remotes/origin/" + branch
        self.fetched_at, self.fetch_ok, self.error = None, False, None
        self._cat = None

    def git(self, *args, timeout=120):
        return run(["git", "--no-optional-locks", "-C", self.path] + list(args), timeout=timeout)

    def exists(self):
        return os.path.isdir(self.path) and self.git("rev-parse", "--git-dir")[0] == 0

    def fetch(self):
        """Clone on first use, then fetch; never touches anything outside self.path."""
        if not self.repo:
            self.fetch_ok, self.error = False, "no repo (use --repo, or --docs with an origin remote)"
            return False
        if not self.exists():
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            code, _, err = run(["git", "clone", "--bare", "--no-tags", "-q", self.repo, self.path], timeout=600)
            if code != 0:
                self.fetch_ok, self.error = False, "clone failed: " + (err.strip().splitlines() or ["?"])[-1]
                return False
        if self.git("remote", "get-url", "origin")[1].decode().strip() != self.repo:
            self.git("remote", "set-url", "origin", self.repo)
        self.git("config", "remote.origin.fetch", "+refs/heads/*:refs/remotes/origin/*")
        code, _, err = self.git("fetch", "-q", "--prune", "--no-tags", "origin", timeout=300)
        self.fetch_ok = code == 0
        self.error = None if code == 0 else "fetch failed: " + (err.strip().splitlines() or ["?"])[-1]
        if code == 0: self.fetched_at = now_iso()
        return self.fetch_ok

    def head(self):
        code, out, _ = self.git("rev-parse", "--verify", "-q", self.ref)
        return out.decode().strip() if code == 0 else None

    def cat(self, name):
        """Object bytes for a name like '<sha>:file' or '<sha>^{tree}', or None."""
        for attempt in (0, 1):
            try:
                if self._cat is None or self._cat.poll() is not None:
                    kw = {"creationflags": 0x08000000} if os.name == "nt" else {}
                    self._cat = subprocess.Popen(["git", "-C", self.path, "cat-file", "--batch"], stdin=subprocess.PIPE,
                                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, **kw)
                self._cat.stdin.write(name.encode() + b"\n"); self._cat.stdin.flush()
                hdr = self._cat.stdout.readline().decode().split()
                if len(hdr) < 3 or hdr[1] == "missing": return None
                data = self._cat.stdout.read(int(hdr[2])); self._cat.stdout.read(1)
                return data
            except Exception:
                self.close()
                if attempt: raise
        return None

    def tree(self, sha):
        """{name: blob id} of the top-level files of a commit."""
        data = self.cat(sha + "^{tree}") or b""
        out, i = {}, 0
        while i < len(data):
            sp = data.index(b" ", i); nul = data.index(b"\0", sp)
            mode, name = data[i:sp], data[sp + 1:nul].decode("utf-8", "replace")
            oid = data[nul + 1:nul + 21].hex()
            if not mode.startswith(b"4"): out[name] = oid
            i = nul + 21
        return out

    def log(self):
        """First-parent history of main, newest first: [{sha, parent, time, author, subject}]."""
        code, out, _ = self.git("log", "--first-parent", "--format=%H%x1f%P%x1f%aI%x1f%an%x1f%s%x1e", self.ref)
        if code != 0: return []
        res = []
        for rec in out.decode("utf-8", "replace").split("\x1e"):
            f = rec.strip("\n").split("\x1f")
            if len(f) < 5: continue
            res.append({"sha": f[0], "parent": (f[1].split() or [None])[0], "time": f[2], "author": f[3], "subject": f[4]})
        return res

    def close(self):
        p, self._cat = self._cat, None
        if not p: return
        try:
            p.stdin.close(); p.wait(5)
        except Exception:
            p.kill(); p.wait()
        p.stdout.close()


# ---- history: entry-level changes per commit, cached by sha --------------------------------------------------

class History:
    def __init__(self, mirror, cache_path, thread_names):
        self.mirror, self.cache_path, self.thread_names = mirror, cache_path, thread_names
        self.parsed = {}                                                   # (name, blob) -> parse_file result
        self.dirty = False
        self.cache = {"parser": docsparse.PARSER_VERSION, "commits": {}}
        try:
            with open(cache_path, encoding="utf-8") as f: c = json.load(f)
            if c.get("parser") == docsparse.PARSER_VERSION: self.cache = c
        except Exception:
            pass

    def text(self, blob):
        b = self.mirror.cat(blob) if blob else None
        return norm_text(b) if b is not None else ""

    def parse(self, name, blob):
        key = (name, blob)
        if key not in self.parsed: self.parsed[key] = docsparse.parse_file(name, self.text(blob), self.thread_names)
        return self.parsed[key]

    def commit(self, c):
        """Cached {blobs, files, items} for one commit (diffed against its first parent)."""
        hit = self.cache["commits"].get(c["sha"])
        if hit: return hit
        blobs = {k: v for k, v in self.mirror.tree(c["sha"]).items() if k.endswith(".md")}
        pblobs = {}
        if c["parent"]:
            ph = self.cache["commits"].get(c["parent"])
            pblobs = ph["blobs"] if ph else {k: v for k, v in self.mirror.tree(c["parent"]).items() if k.endswith(".md")}
        changed = sorted(n for n in set(blobs) | set(pblobs) if blobs.get(n) != pblobs.get(n))
        old_e, new_e, items = [], [], []
        for n in changed:
            if docsparse.file_kind(n) == "entries":
                old_e += self.parse(n, pblobs.get(n))["entries"] if pblobs.get(n) else []
                new_e += self.parse(n, blobs.get(n))["entries"] if blobs.get(n) else []
            else:
                title = (self.parse(n, blobs.get(n) or pblobs.get(n))["title"]) or n
                items += docsparse.diff_document(n, self.text(pblobs.get(n)), self.text(blobs.get(n)), title)
        items = docsparse.diff_entries(old_e, new_e) + items
        rec = {"blobs": blobs, "files": changed, "items": items}
        self.cache["commits"][c["sha"]] = rec
        self.dirty = True
        return rec

    def save(self):
        if not self.dirty: return
        try:
            os.makedirs(os.path.dirname(self.cache_path), exist_ok=True)
            tmp = self.cache_path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f: json.dump(self.cache, f, separators=(",", ":"))
            os.replace(tmp, self.cache_path)
            self.dirty = False
        except Exception as e:
            log("changes cache write failed: %s" % e)


# ---- waiting-on heuristics -----------------------------------------------------------------------------------

_OWNER_RE = re.compile(r"waiting on (?:the )?owner|waiting on q\b|waits? (?:on|for) (?:the )?owner|until (?:the owner|q)\b|"
                       r"\bowner (?:still )?to \w+|owner'?s (?:push|pick|picks|decision|decisions|choice|answer|ok|go)|"
                       r"question (?:to|for) the owner|ask(?:s|ing)? the owner|waits? for (?:a )?push|q says push|"
                       r"needs? (?:q|the owner)\b|for q to\b", re.I)
_PUSH_RE = re.compile(r"owner'?s push|waits? for (?:a )?push|q says push|until the owner says push|then \W?push\b|"
                      r"waiting on (?:the owner'?s |q'?s )?push|owner to push|q to push", re.I)
_TESTS_RE = re.compile(r"tests? (?:are |is )?(?:still )?fail|failing tests?|test failures?|tickrate failure|"
                       r"needs? measuring|measure (?:it )?first|\brepro\b|reproduc|\bflak(?:e|y)|fails? on testing", re.I)
_RECO_RE = re.compile(r"\brecommend(?:ation|ed)?\b\s*[:,]?\s*([^.;)]+)", re.I)


def _sentence(text, m):
    """The sentence around match m, trimmed, with 'the owner' said as q."""
    start = max(text.rfind(". ", 0, m.start()), text.rfind("\n", 0, m.start())) + 1
    ends = [i for i in (text.find(". ", m.end()), text.find("\n", m.end())) if i >= 0]
    s = text[start:min(ends) if ends else len(text)].strip(" .\n")
    s = re.sub(r"\bthe owner'?s\b", "q's", s, flags=re.I)
    s = re.sub(r"\bthe owner\b|\bowner\b", "q", s, flags=re.I)
    s = s.replace("**", "").replace("`", "")
    return (s[:117] + "...") if len(s) > 120 else s


def waiting_on(e, links):
    """(waitingOn list, waitingWhy sentence, notStarted) for one entry; see SPEC.md."""
    st, f = e["status"], e["file"][:2]
    if st in docsparse.DONE_STATUSES:
        return ["none"], "Nothing: %s." % {"done": "done", "fixed": "fixed", "wontfix": "won't fix", "answered": "answered",
                                             "told": "told"}[st], False
    if f == "08" and st != "in-progress": return ["none"], "Nothing: a reference note.", False
    text = "\n".join(x for x in (e["title"], e["body"], e["statusNote"], e.get("statusText")) if x)
    w, why = [], []

    def add(k, s=None):
        if k not in w: w.append(k)
        if s and s not in why: why.append(s)

    if f == "07" and st in ("open", "unknown"): add("owner", "Waiting on q's decision")
    if f == "06" and st in ("open", "unknown"): add("owner", "Not told yet: for q to read")
    m = _OWNER_RE.search(text)
    if m and (not w or _PUSH_RE.search(text)):
        pm = _PUSH_RE.search(text)
        if pm:
            needs = " (needs VERSION + changelogs)" if re.search(r"VERSION|changelog", text) else ""
            add("owner", "Waiting on q to push" + needs); add("push")
        else:
            add("owner", "Waiting on q: " + _sentence(text, m))
    for l in links:
        if l["kind"] == "pr" and l.get("state") == "open":
            add("pr", "%s is open%s" % (l["label"], {"failure": ", checks failing", "in_progress": ", checks running"}.get(l.get("checks"), "")))
            if l.get("checks") in ("failure", "in_progress"): add("ci")
    for l in links:
        if l["kind"] == "branch" and l.get("state") in ("failure", "in_progress"):
            add("ci", "CI %s on %s" % ("failing" if l["state"] == "failure" else "running", l["label"]))
    m = _TESTS_RE.search(text)
    if m: add("tests", "Tests: " + _sentence(text, m))
    started = st == "in-progress"
    if not w:
        add("claude", "A Claude thread is working on it" if started else "Nobody has started it yet")
    elif "ci" in w and not any(s.startswith("CI ") or "checks" in s for s in why):
        why.append("CI not green")
    return w, "; ".join(why) + ".", (not started and w == ["claude"])


def recommendation(e):
    m = _RECO_RE.search(e["body"] or "")
    return m.group(1).strip() if m else None


def where_of(e, links):
    """Where the work lives: linked branches plus folder / path text in backticks."""
    out = [l["label"] for l in links if l["kind"] == "branch"]
    for t in re.findall(r"`([^`\n]{2,100})`", e["body"] or ""):
        if ("\\" in t or "/" in t) and " " not in t.strip() and t not in out and not t.startswith("claude/"): out.append(t)
    return ", ".join(out)


# ---- building the data --------------------------------------------------------------------------------------

class Manager:
    def __init__(self, args):
        self.args = args
        self.state_dir = os.path.abspath(args.state_dir or HERE)
        self.docs = os.path.abspath(args.docs) if args.docs else None
        self.quark_dir = os.path.abspath(args.quark_dir or os.path.join(HERE, ".."))
        tp = args.threads or os.path.join(HERE, "threads.json")
        try:
            with open(tp, encoding="utf-8") as f: self.threads = json.load(f).get("threads", [])
        except Exception as e:
            log("threads.json unreadable (%s); threads won't link" % e); self.threads = []
        self.thread_names = [n for t in self.threads for n in [t["title"]] + t.get("aliases", [])]
        repo = args.repo
        docs_origin = None
        if self.docs and os.path.isdir(self.docs):
            code, out, _ = run(["git", "--no-optional-locks", "-C", self.docs, "remote", "get-url", "origin"], timeout=20)
            if code == 0: docs_origin = out.decode().strip()
        if not repo: repo = docs_origin
        if repo and os.path.isdir(repo): repo = os.path.abspath(repo)
        self.repo_web = web_url(repo) or web_url(docs_origin)
        self.mirror = Mirror(os.path.join(self.state_dir, "mirror"), repo, args.branch)
        self.history = History(self.mirror, os.path.join(self.state_dir, "cache", "changes.json"), self.thread_names)
        shared = None if args.gh_shared == "" else (args.gh_shared or os.path.join(self.quark_dir, "github-manager", "cache", "github.json"))
        self.gh = ghlinks.GitHub(os.path.join(self.state_dir, "cache", "github.json"), fixture=args.gh_fixture, shared=shared,
                                 disabled=args.no_gh, repo=args.gh_repo, log=log)
        self.lock = threading.Lock()
        self.body, self.etag, self.summary, self.summary_etag, self.data = None, None, None, None, None
        self.started = time.time()
        self.last_error = None

    # PC folder: read only, never git
    def pc_files(self):
        out = {}
        if not self.docs or not os.path.isdir(self.docs): return out
        for n in sorted(os.listdir(self.docs)):
            p = os.path.join(self.docs, n)
            if n.endswith(".md") and os.path.isfile(p):
                try:
                    with open(p, "rb") as f: out[n] = f.read()
                except OSError:
                    pass
        return out

    def pc_signature(self):
        if not self.docs or not os.path.isdir(self.docs): return None
        sig = []
        try:
            for n in sorted(os.listdir(self.docs)):
                if n.endswith(".md"):
                    st = os.stat(os.path.join(self.docs, n)); sig.append((n, st.st_size, st.st_mtime_ns))
        except OSError:
            pass
        return tuple(sig)

    def build(self):
        head = self.mirror.head()
        commits = self.mirror.log() if head else []
        recs = {}
        for c in reversed(commits): recs[c["sha"]] = self.history.commit(c)
        self.history.save()
        pc = self.pc_files()
        files_at_head = recs[head]["blobs"] if head else {}
        texts = {n: self.history.text(b) for n, b in files_at_head.items()}
        if not head:                                                        # no mirror yet: show the PC copy
            texts = {n: norm_text(b) for n, b in pc.items()}
        known_blobs = {}
        for r in recs.values():
            for n, b in r["blobs"].items(): known_blobs.setdefault(n, set()).add(b)

        gh = self.gh.public()
        linker = ghlinks.Linker(gh, self.threads, self.gh.state.get("commits"))

        # lineage: first seen / last changed per current entry id, walking oldest -> newest
        lineage, last_file = {}, {}
        for c in reversed(commits):
            when = {"sha": c["sha"], "time": c["time"], "timeUtc": utc_of(c["time"])}
            for n in recs[c["sha"]]["files"]: last_file[n] = dict(when, subject=c["subject"])
            for it in recs[c["sha"]]["items"]:
                if not it.get("entryId"): continue
                what = it.get("what") or it["kind"]
                if it["kind"] == "status": what = "status %s -> %s" % (it["from"], it["to"])
                if it["kind"] == "removed": lineage.pop(it["entryId"], None); continue
                rec = lineage.pop(it.get("fromId") or it["entryId"], None) if it["kind"] != "added" else None
                if rec is None: rec = {"firstSeen": when}
                rec["lastChanged"] = dict(when, what=what)
                lineage[it["entryId"]] = rec

        files, entries = [], []
        for n in sorted(set(texts) | set(pc)):
            text = texts.get(n)
            pcb = pc.get(n)
            pc_text = norm_text(pcb) if pcb is not None else None
            if not self.docs or not os.path.isdir(self.docs): pc_state = "no-pc-folder"
            elif text is None: pc_state = "pc-only"
            elif pcb is None: pc_state = "missing"
            elif pc_text == text: pc_state = "same"
            elif blob_id(pc_text.encode("utf-8")) in known_blobs.get(n, ()): pc_state = "behind"
            else: pc_state = "edited"
            src = text if text is not None else pc_text
            p = docsparse.parse_file(n, src, self.thread_names)
            f = {"name": n, "title": p["title"] or n, "kind": p["kind"], "markdown": src,
                 "lastChange": last_file.get(n), "pcUnsynced": pc_state in ("edited", "pc-only"), "pcState": pc_state,
                 "sections": p["sections"], "notes": p["notes"], "entryCount": len(p["entries"])}
            if pc_state in ("edited", "pc-only", "behind"): f["pcMarkdown"] = pc_text
            files.append(f)
            for e in p["entries"]:
                e = dict(e)
                links = linker.links("\n".join([e["title"], e["body"], e["statusNote"]]), e["thread"])
                canon, url = linker.thread(e["thread"])
                e["threadRaw"], e["thread"], e["threadUrl"] = e["thread"], canon or e["thread"], url
                e["links"] = links
                e["waitingOn"], e["waitingWhy"], e["notStarted"] = waiting_on(e, links)
                e["recommendation"] = recommendation(e) if e["file"].startswith("07") else None
                lin = lineage.get(e["id"], {})
                e["firstSeen"], e["lastChanged"] = lin.get("firstSeen"), lin.get("lastChanged")
                e["pcUnsynced"] = f["pcUnsynced"]
                entries.append(e)

        changes = []
        for c in commits:
            r = recs[c["sha"]]
            th = self.commit_thread(c["subject"], linker)
            changes.append({"sha": c["sha"], "time": c["time"], "timeUtc": utc_of(c["time"]), "author": c["author"],
                            "subject": c["subject"], "items": r["items"], "files": r["files"], "thread": th[0],
                            "threadUrl": th[1], "url": "%s/commit/%s" % (self.repo_web, c["sha"]) if self.repo_web else None,
                            "links": linker.links(c["subject"])})

        threads = {}
        for t in self.threads:
            threads[t["title"]] = {"title": t["title"], "url": t.get("url"), "aliases": t.get("aliases", []),
                                   "entries": 0, "entryIds": [], "open": 0, "lastActivity": None}
        for e in entries:
            if not e["thread"]: continue
            t = threads.setdefault(e["thread"], {"title": e["thread"], "url": None, "aliases": [], "entries": 0,
                                                 "entryIds": [], "open": 0, "lastActivity": None})
            t["entries"] += 1; t["entryIds"].append(e["id"])
            if e["waitingOn"] != ["none"] and not e["archived"]: t["open"] += 1
            lc = (e["lastChanged"] or {}).get("timeUtc")
            if lc and (not t["lastActivity"] or lc > t["lastActivity"]): t["lastActivity"] = lc
        for c in changes:
            if c["thread"] in threads and (not threads[c["thread"]]["lastActivity"] or c["timeUtc"] > threads[c["thread"]]["lastActivity"]):
                threads[c["thread"]]["lastActivity"] = c["timeUtc"]
        thread_list = sorted(threads.values(), key=lambda t: t["lastActivity"] or "", reverse=True)

        data = {"generatedAt": now_iso(), "version": VERSION, "mirrorHead": head, "mirrorFetchedAt": self.mirror.fetched_at,
                "fetchOk": self.mirror.fetch_ok, "fetchError": self.mirror.error, "ghOk": bool(self.gh.state.get("ok")),
                "ghFetchedAt": self.gh.state.get("fetchedAt"), "ghSource": self.gh.state.get("source"),
                "ghError": self.gh.state.get("error"), "quarkDir": self.quark_dir.replace("\\", "/"),
                "docsDir": self.docs.replace("\\", "/") if self.docs else None, "repoUrl": self.repo_web,
                "files": files, "entries": entries, "changes": changes, "threads": thread_list,
                "github": dict(gh, commits=self.gh.state.get("commits", {}))}
        self.publish(data)
        return data

    def commit_thread(self, subject, linker):
        """Thread of a commit: '(Thread title)' at the end of the subject, else a 'Thread: ...' prefix."""
        m = re.search(r"\(([^()]+)\)\s*$", subject)
        if m:
            t, u = linker.thread(m.group(1))
            if t: return t, u
        rest = re.sub(r"^(?:claim:|\d\d:)\s*", "", subject.strip(), flags=re.I)
        low = re.sub(r"\s*\([^()]*\)\s*$", "", rest).lower()
        head = low.split(":", 1)[0].strip()
        for t in self.threads:                                               # 'Thread: ...' or 'Claim: <alias>'
            names = [t["title"].lower()] + [a.lower() for a in t.get("aliases", [])]
            if head in names: return t["title"], t.get("url")
        for t in self.threads:                                               # 'Sound editor merged ...'
            for n in [t["title"]] + t.get("aliases", []):
                if len(n.split()) >= 2 and re.match(re.escape(n.lower()) + r"\b", low): return t["title"], t.get("url")
        for t in self.threads:                                               # 'Claim: bot editor and simulator'
            if len(head.split()) >= 2 and head in t["title"].lower(): return t["title"], t.get("url")
        return None, None

    def publish(self, data):
        stable = {k: v for k, v in data.items() if k not in VOLATILE}
        etag = '"%s"' % hashlib.sha1(json.dumps(stable, sort_keys=True).encode()).hexdigest()[:20]
        summary = make_summary(data)
        s_stable = {k: v for k, v in summary.items() if k != "generatedAt"}
        s_etag = '"%s"' % hashlib.sha1(json.dumps(s_stable, sort_keys=True).encode()).hexdigest()[:20]
        with self.lock:
            if etag != self.etag or self.body is None:
                self.body, self.etag = json.dumps(data, separators=(",", ":")).encode("utf-8"), etag
            if s_etag != self.summary_etag or self.summary is None:
                self.summary, self.summary_etag = json.dumps(summary, separators=(",", ":")).encode("utf-8"), s_etag
            self.data = data

    def health(self):
        d = self.data or {}
        return {"ok": self.body is not None, "version": VERSION, "uptime": int(time.time() - self.started),
                "generatedAt": d.get("generatedAt"), "mirrorHead": d.get("mirrorHead"), "mirrorFetchedAt": self.mirror.fetched_at,
                "fetchOk": self.mirror.fetch_ok, "fetchError": self.mirror.error, "ghOk": bool(self.gh.state.get("ok")),
                "ghFetchedAt": self.gh.state.get("fetchedAt"), "ghError": self.gh.state.get("error"), "etag": self.etag,
                "lastError": self.last_error}

    def refresh_gh(self, entries_text=()):
        return self.gh.refresh(ghlinks.commit_candidates(entries_text))

    def entry_texts(self):
        d = self.data or {}
        return [e["title"] + " " + e["body"] + " " + e["statusNote"] for e in d.get("entries", [])
                if not e.get("archived")] + [c["subject"] for c in d.get("changes", [])[:100]]


def make_summary(data):
    """The small, stable /api/summary for the sibling project manager."""
    es = [e for e in data["entries"] if not e.get("archived")]
    inprog = [e for e in es if e["file"].startswith("02") and e["section"].lower().startswith("in progress")
              and e["status"] in ("in-progress", "open", "unknown")]
    waiting = {k: [] for k in ("owner", "push", "pr", "ci", "tests", "claude")}
    for e in es:
        for k in e["waitingOn"]:
            if k in waiting: waiting[k].append(e["id"])
    qs = [e for e in es if e["file"].startswith("07") and e["status"] in ("open", "unknown")]
    links_of = lambda e: [l for l in e["links"] if l["kind"] == "branch"]
    return {"generatedAt": data["generatedAt"], "mirrorHead": data["mirrorHead"], "fetchOk": data["fetchOk"], "ghOk": data["ghOk"],
            "inProgress": [{"id": e["id"], "title": e["title"], "thread": e["thread"], "threadUrl": e["threadUrl"],
                            "status": e["status"], "where": where_of(e, links_of(e)), "waitingOn": e["waitingOn"],
                            "waitingWhy": e["waitingWhy"], "date": e["date"],
                            "lastChanged": e["lastChanged"]} for e in inprog],
            "waiting": waiting,
            "questions": [{"id": e["id"], "title": e["title"], "body": e["body"], "thread": e["thread"],
                           "threadUrl": e["threadUrl"], "date": e["date"], "recommendation": e.get("recommendation")} for e in qs],
            "recentChanges": data["changes"][:30]}


# ---- background refresh -------------------------------------------------------------------------------------

class Refresher(threading.Thread):
    def __init__(self, mgr, interval, gh_interval):
        super().__init__(daemon=True)
        self.mgr, self.interval, self.gh_interval = mgr, interval, gh_interval
        self.wake, self.next_fetch, self.next_gh = threading.Event(), 0.0, 0.0
        self.ready = threading.Event()

    def poke(self):
        self.next_fetch = self.next_gh = 0.0
        self.wake.set()

    def run(self):
        m, sig, last = self.mgr, None, None
        while True:
            try:
                changed = m.body is None
                if time.time() >= self.next_fetch:
                    ok_before, head_before = m.mirror.fetch_ok, m.mirror.head()
                    if not m.mirror.fetch():
                        if m.mirror.error != last: log(m.mirror.error); last = m.mirror.error
                    else: last = None
                    changed |= m.mirror.head() != head_before or m.mirror.fetch_ok != ok_before
                    self.next_fetch = time.time() + self.interval
                s = m.pc_signature()
                changed |= s != sig; sig = s
                if changed: m.build()                                    # (docs first: GitHub can take a while)
                self.ready.set()
                if time.time() >= self.next_gh:
                    self.next_gh = time.time() + self.gh_interval
                    if m.refresh_gh(m.entry_texts()): m.build()
                m.last_error = None
            except Exception as e:
                import traceback
                m.last_error = "%s: %s" % (type(e).__name__, e)
                log("refresh failed: %s" % traceback.format_exc().strip().splitlines()[-1])
            self.ready.set()
            self.wake.wait(5.0); self.wake.clear()


# ---- HTTP ---------------------------------------------------------------------------------------------------

def make_handler(mgr, refresher, port, quiet):
    hosts = {"127.0.0.1:%d" % port, "localhost:%d" % port}
    origins = {"http://%s:%d" % (h, p) for h in ("127.0.0.1", "localhost") for p in CORS_PORTS}

    class H(http.server.BaseHTTPRequestHandler):
        server_version = "QuarkDocsManager/" + VERSION
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):
            if not quiet: log("%s %s" % (self.address_string(), fmt % a))

        def _cors(self):
            o = self.headers.get("Origin")
            if o in origins and self.path.startswith("/api/"):
                self.send_header("Access-Control-Allow-Origin", o)
                self.send_header("Access-Control-Expose-Headers", "ETag, X-Mirror-Fetched-At, X-Gh-Fetched-At, X-Fetch-Ok")
            self.send_header("Vary", "Origin")

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
            self._send(code, json.dumps(obj).encode("utf-8"), headers={"Cache-Control": "no-store"})

        def _host_ok(self):
            if self.headers.get("Host", "") in hosts: return True
            self._send(403, b'{"error":"bad host"}')
            return False

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
            path = self.path.split("?", 1)[0]
            if path in ("/api/data", "/api/summary"):
                with mgr.lock:
                    body, etag = (mgr.body, mgr.etag) if path == "/api/data" else (mgr.summary, mgr.summary_etag)
                if body is None: return self._json(503, {"error": "starting", "detail": mgr.mirror.error or "loading"})
                h = {"ETag": etag, "Cache-Control": "no-cache", "X-Mirror-Fetched-At": mgr.mirror.fetched_at or "",
                     "X-Gh-Fetched-At": str(mgr.gh.state.get("fetchedAt") or ""), "X-Fetch-Ok": "1" if mgr.mirror.fetch_ok else "0"}
                inm = self.headers.get("If-None-Match", "")
                if etag and etag in [t.strip() for t in inm.split(",")]: return self._send(304, headers=h)
                return self._send(200, body, headers=h)
            if path == "/api/health": return self._json(200, mgr.health())
            if path.startswith("/api/"): return self._json(404, {"error": "no such endpoint"})
            rel = "index.html" if path == "/" else path.lstrip("/")
            if ".." in rel.split("/") or "\\" in rel or not (rel in STATIC or rel.startswith(STATIC_DIRS)):
                return self._send(404, b"not found", "text/plain; charset=utf-8")
            full = os.path.realpath(os.path.join(HERE, *rel.split("/")))
            if not full.startswith(os.path.realpath(HERE) + os.sep) or not os.path.isfile(full):
                return self._send(404, b"not found", "text/plain; charset=utf-8")
            with open(full, "rb") as f: body = f.read()
            self._send(200, body, MIME.get(os.path.splitext(full)[1].lower(), "application/octet-stream"),
                       headers={"Cache-Control": "no-cache"})

        def do_POST(self):
            if not self._host_ok(): return
            n = int(self.headers.get("Content-Length") or 0)
            if n: self.rfile.read(min(n, 65536))
            if self.path.split("?", 1)[0] == "/api/refresh":
                refresher.poke()
                return self._json(202, {"ok": True, "detail": "refreshing"})
            return self._json(404, {"error": "no such endpoint"})

    return H


class Server(http.server.ThreadingHTTPServer):
    allow_reuse_address = os.name != "nt"          # (on Windows it would let a second manager share the port)
    daemon_threads = True


def ping(port):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/health" % port, timeout=2) as r:
            return json.loads(r.read().decode()).get("version") is not None
    except Exception:
        return False


def default_docs():
    return os.path.join(os.path.expanduser("~"), "Desktop", "Quark", "Claude Documents")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Claude Manager: Claude Documents, read only (local server).")
    ap.add_argument("--docs", default=None, help="the PC's Claude Documents folder (read only); default %s" % default_docs())
    ap.add_argument("--repo", default=None, help="repo to mirror (URL or path); default: the docs folder's origin")
    ap.add_argument("--branch", default="main")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--interval", type=float, default=60, help="mirror fetch interval, s")
    ap.add_argument("--gh-interval", type=float, default=300, help="GitHub refresh interval, s")
    ap.add_argument("--state-dir", default=None, help="where mirror/ and cache/ live (default: this folder)")
    ap.add_argument("--threads", default=None, help="threads.json (default: beside this file)")
    ap.add_argument("--quark-dir", default=None, help="the Quark folder (default: the parent of this folder)")
    ap.add_argument("--gh-repo", default=ghlinks.REPO)
    ap.add_argument("--gh-fixture", default=None, help="read GitHub data from this JSON instead of gh (tests)")
    ap.add_argument("--gh-shared", default=None, help="the GitHub manager's shared file; '' to ignore it")
    ap.add_argument("--no-gh", action="store_true", help="no GitHub lookups")
    ap.add_argument("--snapshot", default=None, help="write the data to this file once and exit")
    ap.add_argument("--no-fetch", action="store_true", help="use the mirror as it is (snapshot / offline)")
    ap.add_argument("--open", action="store_true", help="open the page in the browser once data is ready")
    ap.add_argument("--ping", action="store_true", help="exit 0 if a manager answers on --port, else 1")
    ap.add_argument("--quiet", action="store_true", help="no request log")
    args = ap.parse_args(argv)

    if args.ping: return 0 if ping(args.port) else 1
    if args.docs is None and not args.repo: args.docs = default_docs()
    mgr = Manager(args)

    if args.snapshot:
        if not args.no_fetch and not mgr.mirror.fetch(): log(mgr.mirror.error)
        mgr.gh.refresh(())
        data = mgr.build()
        mgr.refresh_gh(mgr.entry_texts())                                   # (commit lookups need the entries)
        data = mgr.build()
        mgr.mirror.close()
        with open(args.snapshot, "w", encoding="utf-8") as f: json.dump(data, f, indent=1, ensure_ascii=False)
        log("wrote %s: %d files, %d entries, %d commits, gh %s" % (args.snapshot, len(data["files"]), len(data["entries"]),
                                                                  len(data["changes"]), "ok" if data["ghOk"] else "off"))
        return 0 if data["files"] else 1

    if os.environ.get("QUARK_SHELL") == "1": args.open = False             # (the Quark app shows the page itself)
    if sys.stderr is None:                                                  # pythonw / hidden start: log to a file
        try:
            os.makedirs(os.path.join(mgr.state_dir, "cache"), exist_ok=True)
            sys.stderr = open(os.path.join(mgr.state_dir, "cache", "serve.log"), "a", encoding="utf-8", buffering=1)
        except OSError:
            pass
    if ping(args.port):
        log("already running on port %d" % args.port)
        if args.open: webbrowser.open("http://127.0.0.1:%d/" % args.port)
        return 0
    if mgr.mirror.exists():
        try: mgr.build()                                                    # serve the last known state at once
        except Exception as e: log("first build failed: %s" % e)
    ref = Refresher(mgr, args.interval if not args.no_fetch else 1e12, args.gh_interval)
    ref.start()
    try:
        srv = Server(("127.0.0.1", args.port), make_handler(mgr, ref, args.port, args.quiet))
    except OSError as e:
        log("cannot listen on 127.0.0.1:%d (%s)" % (args.port, e)); return 2
    url = "http://127.0.0.1:%d/" % args.port
    log("serving %s (docs: %s, repo: %s)" % (url, mgr.docs or "-", mgr.mirror.repo or "-"))
    if args.open:
        def opener():
            ref.ready.wait(90)
            webbrowser.open(url)
        threading.Thread(target=opener, daemon=True).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close(); mgr.mirror.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A stand-in for serve.py while the page is built: serves web/ and a fixture /api/* parsed (roughly) from the real
repo. It copies the editable files into a temp folder first, so saves never touch the repo; "HEAD" for /api/diff and
/api/revert is the copy as it started. Test hooks: POST /__test/touch {"path"} edits a file "on disk" (a 409 next
save), POST /__test/reset puts every file back.

    python3 tools/tests_editor/web/test/fixture_server.py [--port 8775] [--repo .]
"""
import argparse, difflib, hashlib, json, os, re, shutil, sys, tempfile, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

WEB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ["tests/tests.cpp", "tests/rules_tests.h", "tests/server_tests.h", "tests/governor_tests.h", "tests/assets_tests.cpp",
         "tests/launcher_tests.cpp", "tests/mapreport.cpp", ".github/workflows/release.yml", ".github/workflows/main-guard.yml",
         ".github/workflows/macos-retry.yml", "tools/release_check.sh", "tools/githooks/pre-commit", "CMakeLists.txt"]
ALLOWED = re.compile(r"^(tests/[^\0]+|\.github/workflows/[^/]+|tools/[^/]+\.sh|tools/githooks/[^/]+|CMakeLists\.txt)$")
NOW = "2026-10-24T15:00:00Z"
TIMING = {"bots": 160, "botaim": 120, "botfalls": 95, "tourney": 88, "duel": 60, "weapons": 31, "maps": 140, "movement": 45, "sessions": 70,
          "sessions2": 66, "modes": 80, "hidden": 25, "race": 50, "votes": 55, "edit": 48, "ranked": 75, "ranked2": 52, "rules": 64,
          "assets": 12, "server": 58, "netcode": 40, "tickrate": 62, "governor": 35, "resetmaps": 4, "userdata": 6}

class Repo:
    def __init__(self, src):
        self.dir = tempfile.mkdtemp(prefix="tests_editor_fixture_")
        self.head = {}
        for f in FILES:
            p = os.path.join(src, f)
            if not os.path.exists(p): continue
            os.makedirs(os.path.dirname(os.path.join(self.dir, f)), exist_ok=True)
            shutil.copy(p, os.path.join(self.dir, f))
            self.head[f] = open(p, encoding="utf-8", newline="").read()
        self.lock = threading.Lock()
        self.runs = []
        self.ci_available = True
    def read(self, f): return open(os.path.join(self.dir, f), encoding="utf-8", newline="").read()
    def write(self, f, t):   # (atomic, like serve.py: a model built meanwhile never reads half a file)
        p = os.path.join(self.dir, f)
        with open(p + ".tmp", "w", encoding="utf-8", newline="") as fh: fh.write(t)
        os.replace(p + ".tmp", p)
    def reset(self):
        for f, t in self.head.items(): self.write(f, t)

sha = lambda t: hashlib.sha256(t.encode("utf-8")).hexdigest()

# ------------------------------------------------------------------ rough parsers
def match_brace(lines, i, open_ch="{", close_ch="}"):
    depth = 0
    for j in range(i, len(lines)):
        l = re.sub(r'"(\\.|[^"\\])*"', '""', lines[j].split("//")[0])
        depth += l.count(open_ch) - l.count(close_ch)
        if depth <= 0 and (open_ch in l or j > i): return j
    return len(lines) - 1

CALL = re.compile(r"(?:g_fail \+= )?\b(test\w+|\w+Tests|serverTests|launcherTests|assetTests|mapReport|ffaVsExperts|playtestMaps|serverBench|snapBench|exportAssets)\s*\(")
def calls_in(text, line0):
    out = []
    for k, l in enumerate(text.split("\n")):
        for m in CALL.finditer(l):
            i = m.end(); depth = 1; j = i
            while j < len(l) and depth:
                depth += {"(": 1, ")": -1}.get(l[j], 0); j += 1
            out.append({"fn": m.group(1), "args": l[i:j - 1], "line": line0 + k})
    return out

def parse_functions(repo):
    fns = {}
    for f in [x for x in FILES if x.startswith("tests/")]:
        lines = repo.read(f).split("\n")
        for i, l in enumerate(lines):
            m = re.match(r"^(?:static\s+)?(?:void|int|bool|uint16_t|FfaRun|double)\s+(test\w+|\w+Tests)\s*\(([^)]*)\)\s*\{?", l)
            if not m: continue
            end = match_brace(lines, i)
            body = "\n".join(lines[i + 1:end + 1])
            doc = []
            k = i - 1
            while k >= 0 and lines[k].startswith("//"): doc.insert(0, lines[k][2:].strip()); k -= 1
            checks = [{"line": i + 2 + n, "expr": re.search(r"CHECK\((.*)\);", bl).group(1)[:160]} for n, bl in enumerate(lines[i + 1:end + 1]) if re.search(r"\bCHECK\((.*)\);", bl)]
            called = sorted({c["fn"] for c in calls_in(body, 0) if c["fn"] != m.group(1)})
            fns[m.group(1)] = {"name": m.group(1), "file": f, "line": i + 1, "endLine": end + 1, "args": m.group(2), "doc": " ".join(doc),
                               "checks": checks, "calls": called, "usedBy": []}
    return fns

def parse_quark_tests(repo, fns):
    f = "tests/tests.cpp"
    lines = repo.read(f).split("\n")
    items = []
    main = next(i for i, l in enumerate(lines) if l.startswith("int main("))
    for i, l in enumerate(lines):
        m = re.match(r'^\s*if \(argc >= 2 && std::string\(argv\[1\]\) == "(\w+)"\)', l)
        if not m or i < main: continue
        end = match_brace(lines, i) if "{" in l.split(")", 2)[-1] else i
        cs = [c for c in calls_in("\n".join(lines[i:end + 1]), i + 1) if c["fn"] in fns]
        kind = "tool" if m.group(1) in ("playtest", "serverbench", "mapreport", "exportassets", "racefloors", "snapbench", "ffaexperts") else "shortcut"
        items.append({"id": "cmd:" + m.group(1), "kind": kind, "name": m.group(1), "file": f, "line": i + 1, "endLine": end + 1, "calls": cs,
                      "run": {"cmd": "build/quark_tests " + m.group(1), "parallel": False}})
    groups = []
    for lst in ("par", "alone"):
        st = next(i for i, l in enumerate(lines) if re.search(r"std::vector<T> %s = \{" % lst, l))
        j = st + 1; order = 0
        while lines[j].strip() != "};":
            m = re.match(r'^\s*\{"(\w+)",', lines[j])
            if m:
                end = match_brace(lines, j)
                cs = [c for c in calls_in("\n".join(lines[j:end + 1]), j + 1) if c["fn"] in fns]
                name = m.group(1)
                groups.append({"id": "group:" + name, "kind": "group", "name": name, "list": lst, "order": order, "file": f, "line": j + 1, "endLine": end + 1,
                               "calls": cs, "run": {"cmd": "build/quark_tests group " + name, "parallel": lst == "par"},
                               "timing": {"sec": TIMING.get(name, 30), "source": "release-tests/timing/step1.txt"} if name in TIMING else None,
                               "flaky": "timing-sensitive: re-run alone on an idle machine before calling it a failure" if name in ("tickrate", "netcode") else None})
                order += 1; j = end + 1
            else: j += 1
    for it in groups + items:
        for c in it["calls"]:
            if c["fn"] in fns and it["id"] not in fns[c["fn"]]["usedBy"]: fns[c["fn"]]["usedBy"].append(it["id"])
    return {"id": "quark_tests", "kind": "local", "title": "quark_tests", "file": f, "line": main + 1,
            "when": [{"trigger": "before-commit", "text": "before every commit: build, then build/quark_tests (par groups at once, then alone ones)", "file": "CLAUDE.md", "line": 0,
                      "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None}],
            "items": groups + items}

def yval(l):
    v = l.split(":", 1)[1] if ":" in l else ""
    v = re.sub(r"\s+#.*$", "", v).strip()
    return v

def parse_workflow(repo, f):
    lines = repo.read(f).split("\n")
    name = os.path.basename(f)
    when = []
    on = next((i for i, l in enumerate(lines) if l.startswith("on:")), None)
    if on is not None:
        k = on + 1
        while k < len(lines) and (lines[k].startswith(" ") or not lines[k].strip()):
            m = re.match(r"^  (\w+):(.*)$", lines[k])
            if m:
                trig = m.group(1); w = {"trigger": trig, "text": trig, "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None, "file": f, "line": k + 1}
                body = []
                k2 = k + 1
                while k2 < len(lines) and lines[k2].startswith("    "): body.append(lines[k2]); k2 += 1
                bt = "\n".join(body)
                if trig == "push":
                    w["branches"] = re.findall(r"'([^']+)'", (re.search(r"branches:\s*\[(.*)\]", bt) or [None, ""])[1])
                    w["tags"] = re.findall(r"'([^']+)'", (re.search(r"tags:\s*\[(.*)\]", bt) or [None, ""])[1])
                    w["text"] = "a push to any branch" + (", or a tag " + ", ".join(w["tags"]) if w["tags"] else "")
                    when.append(w)
                    if w["tags"]: when.append(dict(w, trigger="tag", text="a pushed tag " + ", ".join(w["tags"])))
                    k = k2; continue
                if trig == "workflow_dispatch":
                    w["inputs"] = [{"name": n, "type": "boolean", "default": False, "description": ""} for n in re.findall(r"^      (\w+):", bt, re.M)]
                    w["text"] = "a manual run (Actions → Run workflow)" + (": inputs " + ", ".join(i["name"] for i in w["inputs"]) if w["inputs"] else "")
                if trig == "schedule":
                    w["cron"] = (re.search(r"cron:\s*'([^']+)'", bt) or [None, None])[1]; w["text"] = "on a schedule (cron %s, UTC)" % w["cron"]
                if trig == "pull_request": w["text"] = "a pull request (opened, edited, new commits)"
                when.append(w); k = k2; continue
            k += 1
    jobs = []
    js = next(i for i, l in enumerate(lines) if l.startswith("jobs:"))
    starts = [i for i in range(js + 1, len(lines)) if re.match(r"^  [\w-]+:", lines[i])]
    for n, i in enumerate(starts):
        end = (starts[n + 1] - 1) if n + 1 < len(starts) else len(lines) - 1
        while end > i and not lines[end].strip(): end -= 1
        jid = re.match(r"^  ([\w-]+):", lines[i]).group(1)
        job = {"id": "job:" + jid, "kind": "job", "jobId": jid, "name": jid, "file": f, "line": i + 1, "endLine": end + 1, "runsOn": None, "needs": [],
               "if": None, "ifText": None, "timeoutMin": None, "matrix": None, "env": {}, "lines": {"if": None, "needs": None, "runsOn": None, "steps": None},
               "steps": [], "outputs": {}}
        k = i + 1
        while k <= end:
            l = lines[k]
            m = re.match(r"^    ([\w-]+):(.*)$", l)
            if m:
                key, v = m.group(1), yval(l)
                if key == "needs":
                    job["needs"] = [x.strip() for x in v.strip("[]").split(",") if x.strip()]; job["lines"]["needs"] = k + 1
                elif key == "if":
                    job["if"] = v.strip('"'); job["lines"]["if"] = k + 1
                elif key == "runs-on": job["runsOn"] = v; job["lines"]["runsOn"] = k + 1
                elif key == "name": job["name"] = v.strip("'\"")
                elif key == "timeout-minutes": job["timeoutMin"] = int(v)
                elif key == "steps": job["lines"]["steps"] = k + 1
                elif key == "strategy":
                    mm = re.search(r"group:\s*\[(.*)\]", "\n".join(lines[k:k + 6]))
                    if mm: job["matrix"] = {"group": [x.strip() for x in mm.group(1).split(",")]}
            k += 1
        sl = job["lines"]["steps"]
        if sl:
            st = [k for k in range(sl, end + 1) if re.match(r"^      - ", lines[k])]
            for n2, s in enumerate(st):
                se = (st[n2 + 1] - 1) if n2 + 1 < len(st) else end
                while se > s and (not lines[se].strip() or lines[se].strip().startswith("#")) and n2 + 1 < len(st): se -= 1
                blk = lines[s:se + 1]
                txt = "\n".join(blk)
                def key(kname):
                    mm = re.search(r"^(?:      - |        )%s:(.*)$" % kname, txt, re.M)
                    return re.sub(r"\s+#.*$", "", mm.group(1)).strip() if mm else None
                run = key("run")
                if run == "|":
                    ri = next(x for x, bl in enumerate(blk) if re.match(r"^(?:      - |        )run:", bl))
                    body = []
                    for bl in blk[ri + 1:]:
                        if bl.strip() and not bl.startswith("          "): break
                        body.append(bl[10:])
                    run = "\n".join(body).rstrip()
                nm = key("name")
                uses = key("uses")
                job["steps"].append({"name": (nm or (("uses " + uses.split("@")[0]) if uses else (run or "").split("\n")[0][:60])).strip('"'), "line": s + 1, "endLine": se + 1,
                                     "id": key("id"), "if": key("if"), "uses": uses, "run": run, "with": {}})
        jobs.append(job)
    for j in jobs:
        raw = j["if"] or ""
        t = []
        if "sanitizers == 'true'" in raw: t.append("only when meta says sanitizers=true (a human-marked [final])")
        if "'production'" in raw: t.append("only for a production tag (v<VERSION>)")
        if "'testing'" in raw: t.append("only for a push to testing")
        if "'site'" in raw: t.append("or a site-only manual run")
        if "inputs.final" in raw and "!(" in raw: t.append("every push; not on a final-test run")
        if "vars.DEPLOY_WORKER" in raw: t.append("when vars.DEPLOY_WORKER is true")
        j["ifText"] = "; ".join(t) if t else (raw or None)
    return {"id": "ci:" + name, "kind": "ci", "title": name, "file": f, "line": 1, "when": when, "items": jobs}

def parse_script(repo, f, sid, kind, trigger, text):
    lines = repo.read(f).split("\n")
    steps = []
    marks = [(i, l) for i, l in enumerate(lines) if re.match(r"^(for |if |echo \$par|exec |\[ \$fail|alone=|par=)", l)]
    for n, (i, l) in enumerate(marks):
        end = (marks[n + 1][0] - 1) if n + 1 < len(marks) else len(lines) - 1
        while end > i and not lines[end].strip(): end -= 1
        nm = {"alone=": "the alone list", "par=": "the par list (every group not alone)", "echo $par": "par groups, 4 at a time (xargs -P4)",
              "for g in $alone": "alone groups, one by one", "if command -v Xvfb": "every map hosted in FFA and screenshotted (Xvfb)",
              "[ $fail": "the result", "exec ": "check_secrets.py --staged"}
        name = next((v for k, v in nm.items() if l.startswith(k)), l[:50])
        steps.append({"id": "step:%d" % (n + 1), "kind": "step", "name": name, "file": f, "line": i + 1, "endLine": end + 1, "text": "\n".join(lines[i:end + 1])})
    return {"id": sid, "kind": kind, "title": os.path.basename(f), "file": f, "line": 1,
            "when": [{"trigger": trigger, "text": text, "file": f, "line": 1, "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None}], "items": steps}

def build_model(repo):
    fns = parse_functions(repo)
    qt = parse_quark_tests(repo, fns)
    suites = [qt]
    for f in FILES:
        if f.startswith(".github/workflows/"): suites.append(parse_workflow(repo, f))
    suites.append(parse_script(repo, "tools/release_check.sh", "script:release_check.sh", "script", "release-check", "before a push, by hand: tools/release_check.sh"))
    suites.append(parse_script(repo, "tools/githooks/pre-commit", "hook:pre-commit", "hook", "pre-commit", "every git commit (once core.hooksPath is tools/githooks)"))
    cm = repo.read("CMakeLists.txt").split("\n")
    ct = [{"id": "ctest:" + m.group(1), "kind": "step", "name": m.group(1), "file": "CMakeLists.txt", "line": i + 1, "endLine": i + 1, "text": l.strip()}
          for i, l in enumerate(cm) for m in [re.search(r"add_test\(NAME (\w+)", l)] if m]
    suites.append({"id": "ctest", "kind": "ctest", "title": "ctest", "file": "CMakeLists.txt", "line": ct[0]["line"] if ct else 1,
                   "when": [{"trigger": "ctest", "text": "ctest in the build folder (runs quark_tests and the UI test)", "file": "CMakeLists.txt", "line": 1,
                             "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None}], "items": ct})
    lint = []
    for s in suites:
        if s["kind"] != "ci": continue
        ids = {j["jobId"] for j in s["items"]}
        for j in s["items"]:
            for n in j["needs"]:
                if n not in ids: lint.append({"level": "error", "file": j["file"], "line": j["lines"]["needs"], "text": "job %s needs '%s', which doesn't exist" % (j["jobId"], n)})
            for st in j["steps"]:
                if st["uses"] and not re.search(r"@[0-9a-f]{40}$", st["uses"]): lint.append({"level": "warn", "file": j["file"], "line": st["line"], "text": "action %s isn't pinned by a commit SHA" % st["uses"]})
                if st["run"] and re.search(r"(^|\s)(\./)?build\S*/quark_tests(\.exe)?(\s+(group\s+\w+|serial|\w+))?\s*($|[;&|>])", st["run"], re.M) and not re.search(r"quark_tests(\.exe)? (exportassets|mapreport|groups|racefloors)", st["run"]) and "sanit" not in j["jobId"]:
                    lint.append({"level": "error", "file": j["file"], "line": st["line"], "text": "CI scope: job %s runs quark_tests gameplay groups" % j["jobId"]})
        for i, l in enumerate(repo.read(s["file"]).split("\n")):
            if "[final]" in l and "case" not in l and not l.lstrip().startswith("#") and "HEAD_MSG" not in l and "'[final]'" not in l:
                lint.append({"level": "error", "file": s["file"], "line": i + 1, "text": "[final] added outside the meta trigger"})
    for n, fn in fns.items():
        if not fn["usedBy"] and not any(n in g["calls"] for g in fns.values()):
            lint.append({"level": "warn", "file": fn["file"], "line": fn["line"], "text": "%s is defined but no group or command calls it" % n})
    ports = {}
    for g in qt["items"]:
        if g["kind"] == "group" and g["list"] == "par":
            for c in g["calls"]:
                for p in re.findall(r"TP\((\d+)\)", c["args"]): ports.setdefault(p, []).append(g)
    for p, gs in ports.items():
        if len({g["name"] for g in gs}) > 1:
            for g in gs: lint.append({"level": "warn", "file": g["file"], "line": g["line"], "text": "par groups %s share port TP(%s)" % (", ".join(sorted({x["name"] for x in gs})), p)})
    return {"generatedAt": NOW, "repo": {"path": repo.dir, "branch": "testing", "sha": "62c51aa", "dirty": [("M " + f) for f in FILES if f in repo.head and repo.read(f) != repo.head[f]], "version": "1.18.11"},
            "rules": [{"id": "ci-scope", "text": "CI runs only meta, builds and sanitizers; no gameplay tests in CI.", "source": "project memory / CLAUDE.md"},
                      {"id": "final", "text": "Sanitizers run only for a human-marked [final]; agents never add [final].", "source": "release.yml meta step"},
                      {"id": "local-before-commit", "text": "Build and run build/quark_tests before every commit.", "source": "CLAUDE.md"}],
            "suites": suites, "functions": fns, "lint": lint}

# ------------------------------------------------------------------ server
REPO = None
RUN = {"cur": None}
def fake_run(r):
    lines = ["building nothing (fixture)", "running %s" % r["cmd"]] + ["  check %d ok" % i for i in range(1, 9)] + ["group done: 2 s", "all tests passed"]
    for l in lines:
        if r["status"] != "running": return
        time.sleep(0.25); r["output"] += l + "\n"
    r["status"], r["exit"], r["endedAt"], r["sec"] = "passed", 0, NOW, 2.5

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, obj, ctype="application/json"):
        b = obj if isinstance(obj, bytes) else json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(b))); self.send_header("Cache-Control", "no-store"); self.end_headers(); self.wfile.write(b)
    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}")
    def do_GET(self):
        u = urlparse(self.path); q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/api/model": return self.send(200, build_model(REPO))
        if u.path == "/api/file":
            p = q.get("path", "")
            if not ALLOWED.match(p) or p not in REPO.head: return self.send(403, {"error": "not allowed"})
            t = REPO.read(p); return self.send(200, {"path": p, "text": t, "sha256": sha(t), "mtime": os.path.getmtime(os.path.join(REPO.dir, p))})
        if u.path == "/api/diff":
            ps = [q["path"]] if q.get("path") else list(REPO.head)
            d = "".join("".join(difflib.unified_diff(REPO.head[p].splitlines(True), REPO.read(p).splitlines(True), "a/" + p, "b/" + p)) for p in ps)
            return self.send(200, {"path": q.get("path"), "diff": d})
        if u.path == "/api/run":
            r = next((x for x in REPO.runs if x["id"] == q.get("id")), None)
            if not r: return self.send(404, {"error": "no run"})
            f = int(q.get("from", 0)); out = r["output"].encode()
            return self.send(200, dict({k: v for k, v in r.items() if k != "output"}, output=out[f:].decode(), size=len(out)))
        if u.path == "/api/runs": return self.send(200, [{k: r[k] for k in ("id", "cmd", "status", "exit", "startedAt", "sec")} for r in reversed(REPO.runs)])
        if u.path == "/api/ci":
            if not REPO.ci_available: return self.send(200, {"available": False, "jobs": {}})
            jobs = {}
            for j, c in [("meta", "success"), ("lightcache", "success"), ("windows", "success"), ("linux", "failure"), ("macos", "cancelled"), ("macos-watchdog", "success"),
                         ("sanitizers", "skipped"), ("release-testing", "skipped"), ("release", "skipped")]:
                jobs["release.yml/" + j] = {"conclusion": c, "url": "https://github.com/floration/quark/actions/runs/123", "runId": 123, "branch": "testing", "at": "2026-10-24T13:20:00Z",
                                            "durationSec": 400, "failedSteps": ["build + package"] if c == "failure" else []}
            jobs["main-guard.yml/from-testing"] = {"conclusion": "success", "url": "", "runId": 99, "branch": "testing", "at": "2026-10-23T22:05:00Z", "durationSec": 9, "failedSteps": []}
            return self.send(200, {"available": True, "jobs": jobs})
        if u.path == "/api/summary":
            m = build_model(REPO)
            return self.send(200, {"suites": len(m["suites"]), "groups": sum(1 for s in m["suites"] for i in s["items"] if i["kind"] == "group"), "jobs": 0, "checks": 0, "dirty": len(m["repo"]["dirty"]), "lint": len(m["lint"]), "lastRun": None})
        path = u.path.lstrip("/") or "index.html"
        full = os.path.normpath(os.path.join(WEB, path))
        if not full.startswith(WEB) or not os.path.isfile(full): return self.send(404, b"not found", "text/plain")
        ct = {"html": "text/html", "css": "text/css", "js": "text/javascript", "mjs": "text/javascript", "png": "image/png", "svg": "image/svg+xml"}.get(full.rsplit(".", 1)[-1], "application/octet-stream")
        self.send(200, open(full, "rb").read(), ct + "; charset=utf-8" if ct.startswith("text") else ct)
    def do_POST(self):
        u = urlparse(self.path); b = self.body()
        if u.path == "/api/file":
            p = b.get("path", "")
            if not ALLOWED.match(p) or p not in REPO.head: return self.send(403, {"error": "not allowed"})
            with REPO.lock:
                cur = REPO.read(p)
                if sha(cur) != b.get("baseSha256"): return self.send(409, {"error": "changed", "sha256": sha(cur)})
                REPO.write(p, b["text"])
                if os.environ.get("FIXTURE_DEBUG"):
                    print("SAVE", p, "".join(list(difflib.unified_diff(cur.splitlines(True), b["text"].splitlines(True)))[:80]), file=sys.stderr, flush=True)
            return self.send(200, {"ok": True, "sha256": sha(b["text"]), "model": build_model(REPO)})
        if u.path == "/api/revert":
            p = b.get("path", "")
            if p not in REPO.head: return self.send(403, {"error": "not allowed"})
            REPO.write(p, REPO.head[p]); return self.send(200, {"ok": True, "sha256": sha(REPO.head[p])})
        if u.path == "/api/run":
            if RUN["cur"] and RUN["cur"]["status"] == "running": return self.send(409, {"error": "a run is going"})
            r = {"id": "r%d" % (len(REPO.runs) + 1), "cmd": b.get("cmd"), "status": "running", "exit": None, "startedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "endedAt": None, "sec": None, "output": ""}
            REPO.runs.append(r); RUN["cur"] = r
            threading.Thread(target=fake_run, args=(r,), daemon=True).start()
            return self.send(200, {"id": r["id"]})
        if u.path == "/api/run/stop":
            r = next((x for x in REPO.runs if x["id"] == b.get("id")), None)
            if r and r["status"] == "running": r["status"], r["exit"], r["output"] = "stopped", -1, r["output"] + "stopped\n"
            return self.send(200, {"ok": True})
        if u.path == "/__test/touch":
            p = b["path"]; REPO.write(p, REPO.read(p) + "\n// touched by someone else\n"); return self.send(200, {"ok": True})
        if u.path == "/__test/reset":
            REPO.reset(); REPO.runs.clear(); REPO.ci_available = b.get("ci", True); return self.send(200, {"ok": True})
        self.send(404, {"error": "no such endpoint"})

def main():
    global REPO
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8775)
    ap.add_argument("--repo", default=os.path.abspath(os.path.join(WEB, "..", "..", "..")))
    ap.add_argument("--dump", action="store_true", help="print the model JSON and exit")
    a = ap.parse_args()
    REPO = Repo(a.repo)
    if a.dump: print(json.dumps(build_model(REPO), indent=1)); return
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    print("fixture on http://127.0.0.1:%d/ (files in %s)" % (a.port, REPO.dir), flush=True)
    try: srv.serve_forever()
    finally: shutil.rmtree(REPO.dir, ignore_errors=True)

if __name__ == "__main__": main()

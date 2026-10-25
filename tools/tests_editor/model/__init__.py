"""The tests editor's model (API.md GET /api/model), built from one working copy of the quark repo.

Every input is read by mtime: a file whose (mtime, size) didn't change is never parsed again, and the whole model is
rebuilt only when some input changed. Python 3 standard library only.
"""
import datetime, glob, hashlib, json, os, re, subprocess, threading, time

from . import cpp, lint, scripts, timing, workflows

SCRIPT_FILES = ["tools/release_check.sh", "tools/test_editor.sh", "tools/uitest.sh"]   # (then any other tools/*.sh that runs quark_tests)
HOOK_DIR = "tools/githooks"
SCRIPT_WHEN = {
    "tools/release_check.sh": ("release-check", "by hand before a push: every group, then every map hosted headless (HANDOVER section 0)"),
    "tools/test_editor.sh": ("manual", "by hand, headless map editor checks (needs a display: Xvfb)"),
    "tools/uitest.sh": ("ctest", "by ctest (its uitest entry); skipped without a display"),
}
# commands the page doesn't run: they write files into the working copy, or are a process the tests start themselves
BY_HAND = {
    "exportassets": "writes assets_export/ into the working copy (the Asset Studio runs it as quark_tests exportassets build/assets_export)",
    "exportbrain": "writes brain files into the working copy: run it from a terminal",
    "exportweapons": "writes weapon files into the working copy: run it from a terminal",
    "exportmode": "writes mode files into the working copy: run it from a terminal",
    "bot": "a bot client the tests start themselves (quark_tests bot MAP SECS)",
}
# the aliases of a BY_HAND command that only read the file they're given (checkbrain FILE, printweapons FILE, modeast
# FILE...): each gets an entry of its own that the page runs
READ_ONLY = re.compile(r"^(?:check|print)\w+$|^\w+ast$")
RULES = [
    ("ci-scope", "CI runs only meta, builds and sanitizers; no gameplay tests in CI.", "release.yml header / owner 2026-10-24",
     ".github/workflows/release.yml", r"CI runs no gameplay"),
    ("final", "Sanitizers run only for a human-marked [final]; agents never add [final].", "release.yml meta step / owner 2026-10-24",
     ".github/workflows/release.yml", r"Agents never add the marker"),
    ("local-before-commit", "Build and run build/quark_tests before every commit.", "CLAUDE.md",
     "CLAUDE.md", r"Before committing"),
]


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def iso_of(ts):
    return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _read(path):
    with open(path, "rb") as f: b = f.read()
    return b.decode("utf-8", "replace").replace("\r\n", "\n")


def _no_window():
    return {"creationflags": 0x08000000} if os.name == "nt" else {}


def git(repo, *args, timeout=10):
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0", LC_ALL="C")
    try:
        p = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, timeout=timeout, env=env,
                           stdin=subprocess.DEVNULL, **_no_window())
        return p.returncode, p.stdout, p.stderr
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, b"", str(e).encode()


class Builder:
    """Builds and caches the model for one repo. Thread-safe: one build at a time."""

    def __init__(self, repo, timing_dir=None, overview=None, local=None, build_groups=None):
        self.repo = os.path.abspath(repo)
        self.timing_dir, self.overview = timing_dir, overview
        self.local = local or (lambda: (0, {}))                          # () -> (generation, {group: newest local time / result})
        self.build_groups = build_groups or (lambda: None)               # () -> {binary, groups} or None
        self.lock = threading.Lock()
        self.files = {}                                                  # rel path -> (sig, parsed)
        self.sig, self.model, self.body, self.etag = None, None, None, None
        self.git_cache = (0, None, None)
        self.last_build_ms = None

    # inputs ---------------------------------------------------------------------------------------------------
    def rel(self, p):
        return os.path.relpath(p, self.repo).replace("\\", "/")

    def inputs(self):
        r = self.repo
        cpps = sorted(glob.glob(os.path.join(r, "tests", "*.cpp")) + glob.glob(os.path.join(r, "tests", "*.h")))
        wfs = sorted(glob.glob(os.path.join(r, ".github", "workflows", "*.yml")) + glob.glob(os.path.join(r, ".github", "workflows", "*.yaml")))
        sh = sorted(glob.glob(os.path.join(r, "tools", "*.sh")))
        hooks = sorted(p for p in glob.glob(os.path.join(r, HOOK_DIR, "*")) if os.path.isfile(p))
        other = [os.path.join(r, "CMakeLists.txt"), os.path.join(r, "VERSION"), os.path.join(r, "CLAUDE.md")]
        notes = []
        if self.timing_dir and os.path.isdir(self.timing_dir):
            notes = sorted(glob.glob(os.path.join(self.timing_dir, "*.txt")))
        if self.overview: notes.append(self.overview)
        return {"cpp": cpps, "wf": wfs, "sh": sh, "hooks": hooks, "other": other, "notes": notes}

    @staticmethod
    def stat_sig(paths):
        out = []
        for p in paths:
            try:
                st = os.stat(p); out.append((p, st.st_mtime_ns, st.st_size))
            except OSError:
                out.append((p, None, None))
        return tuple(out)

    def _parsed(self, path, kind, fn):
        """Parse a file once per (mtime, size)."""
        try:
            st = os.stat(path); sig = (st.st_mtime_ns, st.st_size)
        except OSError:
            return None
        hit = self.files.get((kind, path))
        if hit and hit[0] == sig: return hit[1]
        try:
            text = _read(path)
        except OSError:
            return None
        v = fn(self.rel(path), text)
        self.files[(kind, path)] = (sig, v)
        return v

    # git ------------------------------------------------------------------------------------------------------
    def repo_info(self, sig):
        t, key, val = self.git_cache
        if val is not None and key == sig and time.time() - t < 3: return val
        code, out, _ = git(self.repo, "status", "--porcelain=v1", "-b", "--untracked-files=normal")
        branch, dirty = None, []
        if code == 0:
            ls = out.decode("utf-8", "replace").split("\n")
            if ls and ls[0].startswith("## "):
                b = ls[0][3:]
                branch = b.split("...", 1)[0].replace("No commits yet on ", "")
                ls = ls[1:]
            dirty = [l[:2].strip() + " " + l[3:] for l in ls if l.strip()][:500]
        code, out, _ = git(self.repo, "rev-parse", "--short=7", "HEAD")
        sha = out.decode().strip() if code == 0 else None
        try:
            version = _read(os.path.join(self.repo, "VERSION")).strip().split("\n")[0].strip()
        except OSError:
            version = None
        val = {"path": self.repo, "branch": branch if branch != "HEAD (no branch)" else None, "sha": sha, "dirty": dirty,
               "version": version, "git": code == 0}
        self.git_cache = (time.time(), sig, val)
        return val

    # build ----------------------------------------------------------------------------------------------------
    def get(self, force=False):
        """(model, body bytes, etag); rebuilt only when an input changed."""
        with self.lock:
            ins = self.inputs()
            gen, local = self.local()
            bg = self.build_groups()
            sig = (self.stat_sig([p for k in ins.values() for p in k]), gen, json.dumps(bg, sort_keys=True))
            info = self.repo_info(sig)
            key = (sig, json.dumps(info, sort_keys=True))
            if not force and key == self.sig and self.model is not None: return self.model, self.body, self.etag
            t0 = time.perf_counter()
            m = self.build(ins, local, info, bg)
            self.last_build_ms = round((time.perf_counter() - t0) * 1000, 1)
            m["buildMs"] = self.last_build_ms
            etag = '"%s"' % hashlib.sha1(repr(key).encode()).hexdigest()[:20]     # (same inputs, same model)
            self.sig, self.model, self.etag = key, m, etag
            self.body = json.dumps(m, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
            return self.model, self.body, self.etag

    def build(self, ins, local, info, bg):
        texts = {}
        # C++: tests/*.cpp, tests/*.h
        sources = []
        for p in ins["cpp"]:
            s = self._parsed(p, "cpp", lambda rel, text: cpp.Source(rel, text))
            if s: sources.append(s); texts[s.path] = s.text
        ckey = tuple(id(s) for s in sources)                             # (a Source object per file version)
        if getattr(self, "_cpp_key", None) == ckey:
            index, macros, main, lists, shortcuts = self._cpp
        else:
            index, macros = cpp.index_files(sources)
            main = index.get("main") if index.get("main", {}).get("file") == "tests/tests.cpp" else None
            if main is None:
                main = next((f for f in index.values() if f["name"] == "main"), None)
            lists, shortcuts = cpp.main_layout(main["_src"], main, set(index)) if main else ([], [])
            self._cpp_key, self._cpp = ckey, (index, macros, main, lists, shortcuts)
        main_body = main["_src"].blank[main["_open"]:main["_close"]] if main else ""

        tm = timing.timings(self.timing_dir, [g["name"] for l in lists for g in l["groups"]])
        fl_groups, fl_fns = timing.flakes(self.overview)

        groups = []
        for l in lists:
            # run through a scheduler call (`forked(par, 3 * cores)`: parallel; `forked(alone, 1)`: one at a time), or
            # indexed in a fork loop of its own (`par[i]`, before the shared scheduler)
            sched = re.search(r"\b\w+\s*\(\s*%s\s*,\s*([^)]*)\)" % re.escape(l["name"]), main_body)
            if sched: parallel = sched.group(1).strip() != "1"
            else: parallel = bool(re.search(r"\b%s\s*\[" % re.escape(l["name"]), main_body)) and "alone" not in l["name"]
            for k, g in enumerate(l["groups"]):
                # timing: this machine's newest (cache/groups.json: local runs, imported logs) over the release-tests
                # notes, which are another machine's and older than the newest groups
                t, last = None, None
                note, lr = tm.get(g["name"]), local.get(g["name"]) or {}
                if lr.get("sec") is not None:
                    t = {"sec": lr["sec"], "source": lr.get("timeSource") or "local run %s" % lr.get("timeAt"), "at": lr.get("timeAt"), "run": lr.get("timeRun")}
                    if lr.get("hist"): t.update(history=lr["hist"], slower=_slower(lr["hist"]))
                elif note: t = {"sec": note["sec"], "source": note["source"], "at": iso_of(note["mtime"])}
                if lr.get("status"):
                    last = {"status": lr["status"], "at": lr.get("at"), "run": lr.get("run"), "fails": lr.get("fails", 0),
                            "failLines": lr.get("failLines") or [], "source": lr.get("source")}
                calls = [_call(c, g["locals"], g["ports"]) for c in g["calls"]]
                groups.append({"id": "group:" + g["name"], "kind": "group", "name": g["name"], "list": l["name"],
                               "order": k, "file": main["file"], "line": g["line"], "endLine": g["endLine"], "calls": calls,
                               "run": {"cmd": "build/quark_tests group %s" % g["name"], "parallel": parallel},
                               "timing": t, "lastRun": last, "flaky": fl_groups.get(g["name"]),
                               "ports": [list(p) for p in g["ports"]]})
        cmds = []
        for s in shortcuts:
            calls = [_call(c, s["locals"], s["ports"]) for c in s["calls"]]
            kind = "shortcut" if any(cpp.is_test_name(c["fn"]) for c in calls) else "tool"
            spec = s.get("argsSpec")
            if spec and spec["max"] != 0:                            # (NAME choices: the "name" entries of the function
                for c in s["calls"]:                                 #  it hands argv[2] to, e.g. testGameRules' list)
                    f = index.get(c["fn"])
                    if f and re.search(r"\bargv\s*\[\s*2\s*\]", c["args"]):
                        body = f["_src"].text[f["_open"]:f["_close"]]
                        ch = re.findall(r"\{\s*\"([\w-]+)\"\s*,", body)
                        if len(ch) >= 2: spec = dict(spec, choices=ch)
                        break
            if spec and "choices" not in spec: spec = cpp.name_params(main["_src"], s["_body"], spec, index)   # (choices: NAME)
            run = {"cmd": "build/quark_tests %s" % s["name"], "parallel": False}
            if s["name"] in BY_HAND: run["byHand"] = BY_HAND[s["name"]]
            cmds.append({"id": "cmd:" + s["name"], "kind": kind, "name": s["name"], "aliases": s.get("aliases") or [],
                         "file": main["file"], "line": s["line"], "endLine": s["endLine"], "calls": calls, "run": run,
                         "argsSpec": spec, "shadowsGroup": any(g["name"] == s["name"] for g in groups),
                         "ports": [list(p) for p in s["ports"]]})
            if s["name"] in BY_HAND:                                 # (the read-only aliases of a command that writes
                for a in s.get("aliases") or []:                     #  files: an entry of their own, run from the page)
                    if a in BY_HAND or not READ_ONLY.match(a): continue
                    sp = dict(spec or {"min": 0, "max": 1, "params": [], "hint": None})
                    sp.update(min=max(1, sp["min"]), max=sp["max"] if sp["max"] is None else max(1, sp["max"]),
                              params=[{"name": "file", "default": None}] + (sp.get("params") or [])[1:],
                              hint="reads the file, writes nothing")
                    cmds.append(dict(cmds[-1], id="cmd:" + a, name=a, aliases=[], aliasOf=s["name"], argsSpec=sp, shadowsGroup=False,
                                     run={"cmd": "build/quark_tests %s" % a, "parallel": False}))

        # functions: tests, anything a group / command calls, anything with checks
        direct = {}
        for it in groups + cmds:
            for c in it["calls"]: direct.setdefault(c["fn"], []).append(it["id"])
        exported = {n for n, f in index.items() if n != "main" and (cpp.is_test_name(n) or n in direct or f["checks"])}
        called_by = {}
        for n in exported:
            for c in index[n]["calls"]:
                if c in exported: called_by.setdefault(c, []).append(n)
        # indirect: the shortest call chain from each group / command to every function it reaches through others
        via = {}                                                         # fn -> [{item, path: [fns between]}]
        for it in groups + cmds:
            prev, todo = {}, []
            for c in it["calls"]:
                if c["fn"] in index and c["fn"] not in prev: prev[c["fn"]] = None; todo.append(c["fn"])
            for n in todo:                                               # (breadth first: todo grows as it goes)
                for c in index[n]["calls"]:
                    if c not in prev and c in index: prev[c] = n; todo.append(c)
            for n, p in prev.items():
                if p is None or n not in exported: continue
                path = []
                while p is not None: path.append(p); p = prev[p]
                via.setdefault(n, []).append({"item": it["id"], "path": path[::-1]})
        functions = {}
        for n in sorted(exported, key=lambda n: (index[n]["file"], index[n]["line"])):
            f = index[n]
            rec = {k: v for k, v in f.items() if not k.startswith("_")}
            rec["calls"] = [c for c in f["calls"] if c in exported]
            rec["helpers"] = [c for c in f["calls"] if c not in exported and c != "main"]
            rec["usedBy"] = sorted(set(direct.get(n, [])), key=direct.get(n, []).index) if n in direct else []
            rec["calledBy"] = sorted(called_by.get(n, []))
            rec["runsVia"] = [v for v in via.get(n, []) if v["item"] not in rec["usedBy"]]
            short = n[4:] if n.startswith("test") else n
            rec["flaky"] = fl_fns.get(short) or fl_fns.get(n)
            functions[n] = rec

        # CI workflows
        suites_ci = []
        for p in ins["wf"]:
            s = self._parsed(p, "wf", workflows.suite_of)
            if s: suites_ci.append(s); texts[s["file"]] = _read(p)
        suites_ci.sort(key=lambda s: (s["file"].rsplit("/", 1)[-1] != "release.yml", s["file"]))

        # scripts + hooks
        script_suites = []
        sh_paths = [os.path.join(self.repo, *f.split("/")) for f in SCRIPT_FILES]
        for p in ins["sh"]:
            if p in sh_paths: continue
            try:
                text = _read(p)
                if re.search(r"\bctest\b", text) or any((m.group(1).split() or [""])[0] not in lint.TOOL_CMDS
                                                         for m in lint._QT.finditer("\n".join(lint._code_part(l) for l in text.split("\n")))):
                    sh_paths.append(p)
            except OSError:
                pass
        for p in sh_paths:
            if not os.path.isfile(p): continue
            st = self._parsed(p, "sh", _script)
            rel = self.rel(p)
            texts[rel] = _read(p)
            trig, words = SCRIPT_WHEN.get(rel, ("manual", "by hand"))
            script_suites.append(dict(st, id="script:" + rel.rsplit("/", 1)[-1], kind="script", title=rel.rsplit("/", 1)[-1],
                                      file=rel, line=1, when=[_when(trig, words, rel, 1)]))
        hook_suites = []
        for p in ins["hooks"]:
            st = self._parsed(p, "sh", _script)
            rel = self.rel(p)
            texts[rel] = _read(p)
            name = rel.rsplit("/", 1)[-1]
            hook_suites.append(dict(st, id="hook:" + name, kind="hook", title=name, file=rel, line=1,
                                    when=[_when("pre-commit" if name == "pre-commit" else "manual",
                                                "on every git commit, once the clone has `git config core.hooksPath tools/githooks`"
                                                if name == "pre-commit" else "a git %s hook" % name, rel, 1)]))
        ctest_suite = None
        cm = os.path.join(self.repo, "CMakeLists.txt")
        if os.path.isfile(cm):
            tests = self._parsed(cm, "cmake", lambda rel, text: scripts.ctests(text, rel)) or []
            for t in tests:
                if t["name"] == "uitest": t["script"] = "script:uitest.sh"
            ctest_suite = {"id": "ctest", "kind": "ctest", "title": "ctest", "file": "CMakeLists.txt",
                           "line": tests[0]["line"] if tests else 1,
                           "when": [_when("ctest", "`ctest` in a build folder (cmake's test runner)", "CMakeLists.txt",
                                          tests[0]["line"] if tests else 1)], "items": tests}
        try:
            texts["CLAUDE.md"] = _read(os.path.join(self.repo, "CLAUDE.md"))
        except OSError:
            pass

        # quark_tests: when it runs = everything that invokes it
        qwhen = []
        cl = texts.get("CLAUDE.md", "")
        m = re.search(r"^.*Before committing.*$", cl, re.M)
        qwhen.append(_when("before-commit", "before every commit: build, then build/quark_tests (CLAUDE.md)", "CLAUDE.md",
                           cl.count("\n", 0, m.start()) + 1 if m else 1))
        for s in script_suites:                                          # (the steps that run tests: code, not comments,
            runs = [st for st in s["items"] if _runs_tests(st["text"])]  #  and not `quark_tests groups` / a tool command)
            if runs:
                qwhen.append(dict(s["when"][0], text="%s: %s" % (s["title"], " → ".join(st["name"] for st in runs)),
                                  file=s["file"], line=runs[0]["line"], via=s["id"]))
        for s in suites_ci:
            for j in s["items"]:
                for st in j["steps"]:
                    if _runs_tests(st.get("run") or ""):
                        qwhen.append(_when("push", "CI %s job %s: %s" % (s["file"].rsplit("/", 1)[-1], j["jobId"],
                                                                          j["ifText"] or "every run"), s["file"],
                                           st.get("runLine") or st["line"], via="%s/%s" % (s["id"], j["id"])))
                        break
        if ctest_suite:
            for t in ctest_suite["items"]:
                if (t.get("command") or "").split()[:1] == ["quark_tests"]:
                    qwhen.append(_when("ctest", "ctest (add_test quark_tests%s)" % (", timeout %d s" % t["timeoutSec"] if t["timeoutSec"] else ""),
                                       "CMakeLists.txt", t["line"]))
        qt = {"id": "quark_tests", "kind": "local", "title": "quark_tests", "file": main["file"] if main else "tests/tests.cpp",
              "line": main["line"] if main else 1, "when": qwhen, "items": groups + cmds,
              "lists": [{"name": l["name"], "line": l["line"], "endLine": l["endLine"], "groups": [g["name"] for g in l["groups"]],
                         "parallel": any(g["run"]["parallel"] for g in groups if g["list"] == l["name"]),
                         "jobs": _jobs_text(main["_src"].text[main["_open"]:main["_close"]]) if any(g["run"]["parallel"] for g in groups if g["list"] == l["name"]) else None}
                        for l in lists],
              "checkMacros": macros}

        suites = [qt] + suites_ci + script_suites + hook_suites + ([ctest_suite] if ctest_suite else [])
        rules = []
        for rid, text, source, f, rx in RULES:
            r = {"id": rid, "text": text, "source": source}
            t = texts.get(f)
            if t:
                mm = re.search(rx, t)
                if mm: r["file"], r["line"] = f, t.count("\n", 0, mm.start()) + 1
            rules.append(r)
        model = {"generatedAt": now_iso(), "repo": info, "rules": rules, "suites": suites, "functions": functions,
                 "buildGroups": bg, "timingSource": self.timing_dir if self.timing_dir and os.path.isdir(self.timing_dir) else None,
                 "lint": []}
        alone_lists = {l["name"]: [g["name"] for g in l["groups"]] for l in lists}
        model["lint"] = lint.run(model, texts, index, groups, cmds, suites_ci, script_suites, alone_lists)
        return model


def _slower(hist):
    """{sec, median, pct} when the newest time is over 50% (and 2 s) above the median of the ones before it (3 or
    more), else None: a test, or the code it drives, got slower (q judges by spikes, not averages)."""
    prev = sorted(h["sec"] for h in hist[:-1] if h.get("sec") is not None)
    if len(prev) < 3: return None
    med = prev[len(prev) // 2] if len(prev) % 2 else (prev[len(prev) // 2 - 1] + prev[len(prev) // 2]) / 2
    new = hist[-1]["sec"]
    if new > med * 1.5 and new - med >= 2: return {"sec": new, "median": med, "pct": round(100 * (new - med) / med) if med else None}
    return None


def _jobs_text(body):
    """How many par groups main() runs at once, in words: its `jobs = N * hardware_concurrency()` (and the Windows
    branch's, when it differs): "3 per core; Windows: 1 per core"; None when it isn't written that way."""
    per = lambda t: [int(m.group(1) or 1) for m in re.finditer(r"\bjobs\s*=\s*(?:(\d+)\s*\*\s*)?(?:std::)?max\s*\(\s*1u?\s*,\s*(?:std::)?thread::hardware_concurrency", t)]
    flat = r"((?:(?!#\s*(?:if|else|endif)).)*?)"                        # (a branch with no #if / #else inside)
    for m in re.finditer(r"#\s*ifdef\s+_WIN32\b%s#\s*else\b%s#\s*endif" % (flat, flat), body, re.S):
        if per(m.group(1)) and per(m.group(2)):
            w, o = per(m.group(1))[0], per(m.group(2))[0]
            return "%d per core" % o + ("" if w == o else "; Windows: %d per core" % w)
    n = per(body)
    return "%d per core" % n[0] if n else None


def _runs_tests(text):
    """True when shell / yaml run text runs quark_tests tests (in its code, not a comment; not a tool command)."""
    code = "\n".join(lint._code_part(l) for l in text.split("\n"))
    return any((x.group(1).split() or [""])[0] not in lint.TOOL_CMDS for x in lint._QT.finditer(code))


def _script(rel, text):
    about, items = scripts.steps(text)
    for it in items: it["file"] = rel
    return {"about": about, "items": items}


def _when(trigger, text, file, line, **kw):
    r = {"trigger": trigger, "text": text, "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None,
         "file": file, "line": line}
    r.update(kw)
    return r


def _call(c, locals_, ports):
    out = {"fn": c["fn"], "args": c["args"], "line": c["line"]}
    if c.get("via"): out["via"] = c["via"]                         # (inside a lambda passed to via(...): timingTest)
    a = c["args"].strip()
    m = re.match(r"^(\w+)\s*(?:\+\s*(\d+))?(\s*,.*)?$", a)
    if m and m.group(1) in locals_:
        base = locals_[m.group(1)]
        n = int(re.search(r"\d+", base).group()) + int(m.group(2) or 0)
        out["argsValue"] = "TP(%d)%s" % (n, m.group(3) or "")
        out["port"] = n
    else:
        pm = re.search(r"\bTP\s*\(\s*(\d+)\s*\)", a)
        if pm: out["port"] = int(pm.group(1))
    return out

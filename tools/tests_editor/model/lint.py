"""Rule checks on a built model (API.md "Lint"). Each finding: {level, rule, file, line, text}."""
import re

PIN = re.compile(r"@[0-9a-f]{40}$")
TOOL_CMDS = {"exportassets", "groups", "racefloors", "serverbench", "snapbench", "navbench", "playtest", "bot",
             "ffaexperts", "bounds", "exportbrain", "checkbrain", "brainast", "printbrain", "brainbench",
             "exportmode", "checkmode", "modeast", "printmode", "modebench"}
_QT = re.compile(r"(?:^|[\s/;&|(\"'])(?:\./)?(?:[\w.-]+/)*quark_tests(?:\.exe)?(?![\w.-])([^\n;|&)]*)")


def collapse(s):
    return re.sub(r"\s+", " ", s).strip()


def _f(level, rule, file, line, text):
    return {"level": level, "rule": rule, "file": file, "line": line, "text": text}


def _code_part(line):
    """A yaml / shell line without its # comment."""
    s = line.lstrip()
    if s.startswith("#"): return ""
    return re.split(r"(?<=\s)#", line, maxsplit=1)[0]


def run(model, texts, index, groups, shortcuts, suites_ci, scripts, alone_lists):
    out = []
    known = set(index)

    # CI scope: no gameplay quark_tests in CI (the sanitizers job only drives the code)
    for s in suites_ci:
        trig = s.get("sanitizerTrigger")
        for j in s["items"]:
            for st in j["steps"]:
                if not st.get("run"): continue
                for k, ln in enumerate(st["run"].split("\n")):
                    code = _code_part(ln)
                    for m in _QT.finditer(code):
                        arg = (m.group(1).split() or [""])[0]
                        if arg.startswith(">") or arg.startswith("2>"): arg = ""
                        if arg in TOOL_CMDS: continue
                        if trig and (j["jobId"] == "sanitizers" or "sanitizers" in (j.get("if") or "")): continue
                        line = (st.get("runLine") or st["line"]) + k
                        what = collapse(m.group(1))[:60] or "the whole suite"
                        out.append(_f("error", "ci-scope", s["file"], line,
                                      "CI job %s runs quark_tests (%s): CI runs only meta, builds and sanitizers, no gameplay tests" % (j["jobId"], what)))
        # needs: names a job that doesn't exist
        ids = {j["jobId"] for j in s["items"]}
        for j in s["items"]:
            for n in j["needs"]:
                if n not in ids:
                    out.append(_f("error", "needs", s["file"], j["lines"].get("needs") or j["line"],
                                  "job %s needs '%s', which isn't a job in %s" % (j["jobId"], n, s["file"].rsplit("/", 1)[-1])))
            for st in j["steps"]:
                u = st.get("uses")
                if u and not u.startswith(("./", "docker://")) and not PIN.search(u):
                    out.append(_f("warn", "pin", s["file"], st["lines"].get("uses") or st["line"],
                                  "action %s isn't pinned by a 40-character commit SHA" % u))
        if s.get("error"):
            out.append(_f("error", "yaml", s["file"], s.get("errorLine") or 1, "doesn't parse: " + s["error"]))

    # [final] only in the meta trigger's case
    allowed = []
    for s in suites_ci:
        t = s.get("sanitizerTrigger")
        if t: allowed.append((t["file"], t["line"], t["endLine"]))
    for path, text in texts.items():
        if not (path.startswith(".github/workflows/") or path.startswith("tools/")): continue
        if "[final]" not in text: continue
        for k, ln in enumerate(text.split("\n")):
            if "[final]" not in ln or "[final]" not in _code_part(ln): continue
            if any(path == f and a <= k + 1 <= b for f, a, b in allowed): continue
            out.append(_f("error", "final", path, k + 1,
                          "[final] used outside meta's sanitizer trigger (a human marks a final test; agents never add it)"))

    # groups / commands calling functions that don't exist
    for g in groups + shortcuts:
        for c in g["calls"]:
            if c["fn"] not in known:
                out.append(_f("error", "missing-fn", g["file"], c["line"],
                              "%s %s calls %s(), which isn't defined under tests/" % ("group" if g["kind"] == "group" else "command", g["name"], c["fn"])))

    # test functions nothing runs (reachable through the call graph from any group or command)
    reach, todo = set(), [c["fn"] for g in groups + shortcuts for c in g["calls"]]
    while todo:
        n = todo.pop()
        if n in reach or n not in index: continue
        reach.add(n)
        todo.extend(index[n]["calls"])
    for name, fn in index.items():
        if name == "main" or name in reach: continue
        if re.match(r"^test[A-Z0-9_]|Tests$", name) and (fn["checks"] or re.search(r"\bvoid$", fn.get("ret") or "")):
            out.append(_f("warn", "unused", fn["file"], fn["line"], "%s() is defined but no group or command runs it" % name))

    # two parallel groups sharing a TP() port (a test uses its port and every `port + K` in its body)
    owner = {}
    seen = set()
    for g in groups:
        if not g["run"]["parallel"]: continue
        for port, fn in g.get("ports", []):
            offs = (index.get(fn) or {}).get("portOffsets") or []
            for p in [port] + [port + k for k in offs]:
                o = owner.get(p)
                if o and o[0] != g["name"] and (o[0], g["name"], p) not in seen:
                    seen.add((o[0], g["name"], p))
                    out.append(_f("warn", "port", g["file"], g["line"],
                                  "parallel groups %s and %s both use port TP(%d): %s() in %s, %s() in %s" % (
                                      o[0], g["name"], p, o[1], o[0], fn, g["name"])))
                elif not o:
                    owner[p] = (g["name"], fn)

    # an old shortcut that shadows a group's name but runs less (quark_tests NAME vs group NAME)
    gmap = {g["name"]: g for g in groups}
    for s in shortcuts:
        g = gmap.get(s["name"])
        if not g: continue
        gc, sc = {c["fn"] for c in g["calls"]}, {c["fn"] for c in s["calls"]}
        if sc != gc:
            missing = sorted(gc - sc)
            out.append(_f("warn", "shadow", s["file"], s["line"],
                          "`quark_tests %s` is a shortcut that runs a different set than `group %s`%s; the group runs first only when no shortcut matches" % (
                              s["name"], s["name"], (" (it skips %s)" % ", ".join(missing[:6])) if missing else "")))

    # scripts that hard-code a group list (release_check.sh's alone="..."), against main()'s lists
    for sc in scripts:
        text = texts.get(sc["file"], "")
        for m in re.finditer(r'^\s*(\w+)="([a-z0-9_ -]+)"', text, re.M):
            name, words = m.group(1), m.group(2).split()
            if name not in alone_lists: continue
            want = alone_lists[name]
            if set(words) != set(want):
                line = text.count("\n", 0, m.start()) + 1
                out.append(_f("warn", "script-list", sc["file"], line,
                              "%s lists %s=\"%s\" but tests.cpp's %s list is: %s" % (sc["file"].rsplit("/", 1)[-1], name, " ".join(words), name, " ".join(want))))

    # the built quark_tests lists other groups than the source (an old build)
    bg = model.get("buildGroups")
    if bg and bg.get("groups") is not None:
        src = [g["name"] for g in groups]
        if bg["groups"] != src:
            extra, missing = sorted(set(bg["groups"]) - set(src)), sorted(set(src) - set(bg["groups"]))
            out.append(_f("warn", "build-groups", "tests/tests.cpp", groups[0]["line"] if groups else 1,
                          "the build (%s) lists other groups than tests.cpp%s%s%s: rebuild" % (
                              bg.get("binary"), ": not built yet " + ", ".join(missing) if missing else "",
                              "; gone from the source " + ", ".join(extra) if extra else "",
                              "; same groups, other order" if not extra and not missing else "")))
    out.sort(key=lambda f: (0 if f["level"] == "error" else 1, f["file"], f["line"] or 0))
    return out

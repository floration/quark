"""GitHub workflow files -> suites (when they run, their jobs and steps, with line numbers) and plain-words `if`s."""
import re

from . import yamlsub


def _s(v):
    return v if isinstance(v, str) else ("" if v is None else str(v).lower() if isinstance(v, bool) else str(v))


def _list(v):
    if v is None: return []
    if isinstance(v, list): return [_s(x) for x in v]
    return [_s(v)]


def strip_expr(v):
    """'${{ x }}' -> 'x' (a whole-value expression); other text as it is."""
    if not isinstance(v, str): return _s(v) if v is not None else None
    m = re.match(r"^\s*\$\{\{\s*(.*?)\s*\}\}\s*$", v, re.S)
    return m.group(1) if m and "${{" not in m.group(1) else v.strip()


# ---- plain words for `if:` expressions -----------------------------------------------------------------------

_EVENTS = {"push": "a push", "workflow_dispatch": "a manual run", "pull_request": "a pull request",
           "schedule": "a scheduled run", "workflow_run": "after another workflow", "release": "a release event",
           "pull_request_target": "a pull request (target)"}
_STATUS = {"always()": "always (even after a failure or a cancel)", "success()": "when everything before it passed",
           "failure()": "only when something before it failed", "cancelled()": "only when the run was cancelled",
           "!cancelled()": "unless the run was cancelled"}


def _split_top(expr, op):
    """Split at a top-level && or || (outside parens and quotes)."""
    out, depth, q, start, i = [], 0, None, 0, 0
    while i < len(expr):
        c = expr[i]
        if q:
            if c == q: q = None
        elif c == "'": q = c
        elif c == "(": depth += 1
        elif c == ")": depth -= 1
        elif depth == 0 and expr.startswith(op, i):
            out.append(expr[start:i].strip()); i += len(op); start = i; continue
        i += 1
    out.append(expr[start:].strip())
    return out


def _wrapped(e):
    """True when the whole of e is one (...) group."""
    if not (e.startswith("(") and e.endswith(")")): return False
    depth = 0
    for i, c in enumerate(e):
        depth += c == "("
        depth -= c == ")"
        if depth == 0 and i < len(e) - 1: return False
    return True


def if_text(expr, final_note=None):
    """Plain words for a workflow `if` (or any ${{ }} expression); the raw atom when it isn't a known pattern."""
    if expr is None: return None
    e = strip_expr(expr)
    if not isinstance(e, str): return _s(e)
    return _words(e, final_note)


def _words(e, final_note, top=True):
    e = e.strip()
    while _wrapped(e): e = e[1:-1].strip()
    ors = _split_top(e, "||")
    if len(ors) > 1: return " or ".join(_paren(_words(x, final_note, False)) for x in ors)
    ands = _split_top(e, "&&")
    if len(ands) > 1:
        parts = [_words(x, final_note, False) for x in ands]
        parts = ["(%s)" % w if " or " in w and not (w.startswith("(") and w.endswith(")")) else w for w in parts]
        merged = _merge_manual(ands, parts)
        return " and ".join(merged)
    return _atom(e, final_note)


def _paren(s):
    return "(%s)" % s if " and " in s else s


def _merge_manual(raw, words):
    """'a manual run and the final box is ticked' -> 'a manual run with final ticked'."""
    out, i = [], 0
    while i < len(raw):
        if words[i] == "a manual run" and i + 1 < len(raw) and re.match(r"^inputs\.(\w+)$", raw[i + 1].strip()):
            out.append("a manual run with '%s' ticked" % raw[i + 1].strip()[7:]); i += 2; continue
        out.append(words[i]); i += 1
    return out


def _atom(a, final_note):
    a = a.strip()
    if a in _STATUS: return _STATUS[a]
    if a.startswith("!") and not a.startswith("!="):
        inner = a[1:].strip()
        w = _words(inner, final_note, False)
        if inner.startswith("("): return "not when %s" % w
        return "not " + w
    m = re.match(r"^github\.ref\s*(==|!=)\s*'refs/heads/([^']+)'$", a)
    if m: return ("on branch %s" if m.group(1) == "==" else "not on branch %s") % m.group(2)
    m = re.match(r"^github\.ref\s*(==|!=)\s*'refs/tags/([^']+)'$", a)
    if m: return ("on tag %s" if m.group(1) == "==" else "not on tag %s") % m.group(2)
    m = re.match(r"^startsWith\(\s*github\.ref\s*,\s*'refs/(heads|tags)/([^']*)'\s*\)$", a)
    if m: return "on a %s starting with %s" % ("branch" if m.group(1) == "heads" else "tag", m.group(2) or "anything")
    m = re.match(r"^github\.ref_type\s*(==|!=)\s*'(\w+)'$", a)
    if m: return ("on a %s" if m.group(1) == "==" else "not on a %s") % m.group(2)
    m = re.match(r"^github\.ref_name\s*(==|!=)\s*'([^']+)'$", a)
    if m: return ("on %s" if m.group(1) == "==" else "not on %s") % m.group(2)
    m = re.match(r"^github\.event_name\s*(==|!=)\s*'(\w+)'$", a)
    if m:
        ev = _EVENTS.get(m.group(2), "a %s event" % m.group(2))
        return ev if m.group(1) == "==" else "not " + ev
    m = re.match(r"^needs\.([\w-]+)\.outputs\.([\w-]+)\s*(==|!=)\s*'([^']*)'$", a)
    if m:
        job, out, op, val = m.groups()
        s = "%s says %s%s%s" % (job, out, "=" if op == "==" else "!=", val)
        if out == "sanitizers" and val == "true" and op == "==":
            s = "only when %s says sanitizers=true ([final])" % job
            if final_note: s += ": " + final_note
        return s
    m = re.match(r"^needs\.([\w-]+)\.result\s*(==|!=)\s*'(\w+)'$", a)
    if m:
        job, op, val = m.groups()
        word = {"success": "passed", "failure": "failed", "skipped": "was skipped", "cancelled": "was cancelled"}.get(val, "is " + val)
        if op == "==": return "%s %s" % (job, word)
        return "%s didn't %s" % (job, "pass" if val == "success" else "end as " + val)
    m = re.match(r"^steps\.([\w-]+)\.(outcome|conclusion)\s*(==|!=)\s*'(\w+)'$", a)
    if m: return "step %s %s %s" % (m.group(1), "ended as" if m.group(3) == "==" else "didn't end as", m.group(4))
    m = re.match(r"^inputs\.(\w+)$", a)
    if m: return "the '%s' box is ticked" % m.group(1)
    m = re.match(r"^inputs\.(\w+)\s*(==|!=)\s*'?([^']*)'?$", a)
    if m: return "input %s %s %s" % (m.group(1), "is" if m.group(2) == "==" else "is not", m.group(3))
    m = re.match(r"^vars\.(\w+)\s*(==|!=)\s*'([^']*)'$", a)
    if m: return "repository variable %s %s '%s'" % (m.group(1), "is" if m.group(2) == "==" else "is not", m.group(3))
    m = re.match(r"^contains\(\s*github\.event\.head_commit\.message\s*,\s*'([^']*)'\s*\)$", a)
    if m: return "the head commit message has %s" % m.group(1)
    if a in ("true", "false"): return "always" if a == "true" else "never"
    return a


# ---- `on:` -> when entries ------------------------------------------------------------------------------------

def _cron_words(c):
    f = c.split()
    if len(f) != 5: return "on the schedule %s" % c
    mi, h, dom, mon, dow = f
    def lst(x): return " and ".join(x.split(",")) if "," in x else x
    if h == "*" and dom == mon == dow == "*":
        if mi.startswith("*/"): return "every %s minutes" % mi[2:]
        return "every hour at minute %s (UTC)" % lst(mi)
    if dom == mon == "*" and re.match(r"^[\d,]+$", mi) and re.match(r"^[\d,]+$", h):
        days = "every day" if dow == "*" else "on weekday(s) %s" % lst(dow)
        return "%s at %s (UTC)" % (days, ", ".join("%02d:%02d" % (int(hh), int(mm)) for hh in h.split(",") for mm in mi.split(",")))
    return "on the schedule %s (UTC)" % c


def whens(wf, path):
    """The `on:` triggers of one workflow as contract `when` records."""
    on = wf.get("on", wf.get("true"))
    ln = wf.key_lines.get("on", wf.key_lines.get("true", 1))
    if isinstance(on, str): on = {on: None}
    elif isinstance(on, list): on = {_s(x): None for x in on}
    elif not isinstance(on, dict): return []
    out = []

    def rec(trigger, text, line, **kw):
        r = {"trigger": trigger, "text": text, "branches": [], "tags": [], "paths": [], "inputs": [], "cron": None,
             "file": path, "line": line}
        r.update(kw)
        out.append(r)

    for ev, cfg in on.items():
        line = on.key_lines.get(ev, ln) if isinstance(on, yamlsub.YMap) else ln
        cfg = cfg if isinstance(cfg, dict) else {}
        if ev == "push":
            br, tg = _list(cfg.get("branches")), _list(cfg.get("tags"))
            ign, tign = _list(cfg.get("branches-ignore")), _list(cfg.get("tags-ignore"))
            paths = _list(cfg.get("paths"))
            pw = (" that changes " + ", ".join(paths)) if paths else ""
            if br or ign or not (tg or tign):
                if not br or br == ["**"]: t = "a push to any branch"
                else: t = "a push to " + ", ".join(br)
                if ign: t += " (except %s)" % ", ".join(ign)
                rec("push", t + pw, line, branches=br or ["**"], paths=paths)
            if tg or tign or not (br or ign):
                t = "a pushed tag " + (" or ".join(tg) if tg else "(any)")
                if tign: t += " (except %s)" % ", ".join(tign)
                rec("tag", t, line, tags=tg or ["**"], paths=paths)
        elif ev in ("pull_request", "pull_request_target"):
            br, types = _list(cfg.get("branches")), _list(cfg.get("types"))
            t = "a pull request" + (" into " + ", ".join(br) if br else "")
            if types: t += " (" + ", ".join(types) + ")"
            rec("pull_request", t, line, branches=br, paths=_list(cfg.get("paths")), types=types)
        elif ev == "workflow_dispatch":
            ins = cfg.get("inputs") if isinstance(cfg.get("inputs"), dict) else {}
            inputs = [{"name": k, "type": _s((v or {}).get("type") or "string"), "default": (v or {}).get("default"),
                       "description": _s((v or {}).get("description"))} for k, v in ins.items()]
            t = "a manual run (Actions -> Run workflow)"
            if inputs: t += ", with " + ", ".join("'%s'" % i["name"] for i in inputs)
            rec("workflow_dispatch", t, line, inputs=inputs)
        elif ev == "schedule":
            items = on[ev] if isinstance(on[ev], list) else []
            for k, it in enumerate(items):
                c = _s((it or {}).get("cron")) if isinstance(it, dict) else None
                il = items.item_lines[k] if isinstance(items, yamlsub.YList) and k < len(items.item_lines) else line
                rec("schedule", _cron_words(c) if c else "on a schedule", il, cron=c)
            if not items: rec("schedule", "on a schedule", line)
        elif ev == "workflow_run":
            wfs = _list(cfg.get("workflows"))
            rec("workflow_run", "after %s %s" % (", ".join(wfs) or "another workflow", "/".join(_list(cfg.get("types"))) or "runs"),
                line, workflows=wfs)
        else:
            rec(ev, "a %s event" % ev, line)
    return out


# ---- jobs ------------------------------------------------------------------------------------------------------

def sanitizer_trigger(jobs, path):
    """How meta's run step sets `sanitizers` (release.yml): the `case` arms that set SANITIZERS=true, in plain words.

    {file, line, endLine, step, arms: [{patterns, condition, line, text}], text} or None."""
    for jid, job in jobs.items():
        outs = job.get("outputs") if isinstance(job, dict) else None
        if not isinstance(outs, dict): continue
        for oname, oexpr in outs.items():
            if "sanitizer" not in oname: continue
            m = re.search(r"steps\.([\w-]+)\.outputs\.", _s(oexpr))
            steps = job.get("steps") or []
            for k, st in enumerate(steps):
                if not isinstance(st, dict) or (m and st.get("id") != m.group(1)) or not isinstance(st.get("run"), str): continue
                base = st.scalar_lines.get("run", steps.item_lines[k])
                return _case_arms(st["run"], base, path, jid, oname, st.get("id"))
    return None


_CASE_TOK = re.compile(r"""(?P<case>\bcase\s+"?\$\{?(?P<cvar>\w+)\}?"?\s+in\b)|(?P<esac>\besac\b)|(?P<dsemi>;;)"""
                       r"""|(?P<assign>\b(?P<avar>[A-Z_]+)=true\b)"""
                       r"""|(?:(?<=[\s;(])|^)(?P<pat>(?:'[^']*'|"[^"]*"|[^\s()|;'"])+(?:\|(?:'[^']*'|"[^"]*"|[^\s()|;'"])+)*)\)""")
_CHANNEL_WORDS = {"final": "a manual run with 'final' ticked", "none": "a build-only push or manual run",
                  "testing": "a push to testing", "production": "a production tag", "site": "a site-only run"}


def _case_arms(run, base, path, job, output, step_id):
    lines = run.split("\n")
    var = None
    for i, l in enumerate(lines):
        m = re.match(r"^\s*([A-Z_]+)=true\b", l)
        if m and re.search(r"\b%s=false\b" % m.group(1), run): var = m.group(1)
    if not var:
        for l in lines:
            m = re.search(r'echo "%s=\$(\w+)"' % re.escape(output), l)
            if m: var = m.group(1)
    if not var: return None
    arms, start, end = [], None, None
    levels = []                                                         # open cases: {var, state, arm}
    for i, l in enumerate(lines):
        code = "" if l.lstrip().startswith("#") else re.split(r"\s#", l, maxsplit=1)[0]
        for t in _CASE_TOK.finditer(code):
            if t.group("case"):
                levels.append({"var": t.group("cvar"), "state": "pat", "arm": None})
                if start is None: start = i
            elif t.group("esac"):
                if levels: levels.pop()
                if not levels and start is not None and end is None: end = i
            elif t.group("dsemi"):
                if levels: levels[-1]["state"] = "pat"
            elif t.group("pat") and levels and levels[-1]["state"] == "pat":
                arm = {"var": levels[-1]["var"], "patterns": [p.strip("'\"") for p in t.group("pat").split("|")],
                       "line": base + i, "depth": len(levels), "sets": False,
                       "parents": [lv["arm"] for lv in levels[:-1]]}
                levels[-1]["state"], levels[-1]["arm"] = "body", arm
                arms.append(arm)
            elif t.group("assign") and levels and levels[-1]["arm"] and t.group("avar") == var:
                levels[-1]["arm"]["sets"] = True
    if not arms: return None
    # an arm that sets var=true, with the arms of enclosing cases above it as its conditions
    out = []
    for a in arms:
        if not a["sets"]: continue
        conds = [p for p in a["parents"] if p] + [a]
        words = []
        for c in conds:
            if c["var"] == "CHANNEL":
                words.append(" or ".join(_CHANNEL_WORDS.get(p, "channel " + p) for p in c["patterns"]))
            elif c["var"] == "HEAD_MSG":
                words.append("the push's head commit message (a merged PR: its title) has " +
                             " or ".join(re.sub(r"^\*'?|'?\*$", "", p) for p in c["patterns"]))
            else:
                words.append("%s matches %s" % (c["var"], " or ".join(c["patterns"])))
        out.append({"line": a["line"], "condition": [{"var": c["var"], "patterns": c["patterns"]} for c in conds],
                    "text": ", and ".join(words)})
    if not out: return None
    text = "; or ".join(o["text"] for o in out)
    return {"file": path, "job": job, "step": step_id, "output": output, "var": var, "line": base + (start or 0),
            "endLine": base + (end if end is not None else len(lines) - 1), "arms": out, "text": text}


def jobs_of(wf, path):
    jobs = wf.get("jobs") if isinstance(wf.get("jobs"), dict) else {}
    trig = sanitizer_trigger(jobs, path)
    items = []
    for jid, job in jobs.items():
        if not isinstance(job, dict): continue
        needs = _list(job.get("needs"))
        cond = job.get("if")
        cond = strip_expr(cond) if cond is not None else None
        m = job.get("strategy", {}).get("matrix") if isinstance(job.get("strategy"), dict) else None
        steps, st_list = [], job.get("steps") if isinstance(job.get("steps"), list) else []
        for k, st in enumerate(st_list):
            st = st if isinstance(st, dict) else {}
            sl = st_list.item_lines[k] if isinstance(st_list, yamlsub.YList) else jobs.key_lines.get(jid)
            se = st_list.item_ends[k] if isinstance(st_list, yamlsub.YList) else sl
            run = st.get("run")
            name = st.get("name")
            if not name:
                name = ("Run " + _s(run).strip().split("\n")[0][:80]) if run is not None else _s(st.get("uses")) or "step %d" % (k + 1)
            sc = strip_expr(st.get("if")) if st.get("if") is not None else None
            steps.append({"name": _s(name), "line": sl, "endLine": se, "id": _s(st.get("id")) or None,
                          "if": sc, "ifText": if_text(sc) if sc else None, "uses": _s(st.get("uses")) or None,
                          "run": _s(run) if run is not None else None,
                          "runLine": st.scalar_lines.get("run", st.key_lines.get("run")) if isinstance(st, yamlsub.YMap) else None,
                          "with": st.get("with") if isinstance(st.get("with"), dict) else None,
                          "env": st.get("env") if isinstance(st.get("env"), dict) else None,
                          "workingDirectory": _s(st.get("working-directory")) or None,
                          "timeoutMin": st.get("timeout-minutes"), "continueOnError": st.get("continue-on-error"),
                          "lines": {k2: v2 for k2, v2 in st.key_lines.items()} if isinstance(st, yamlsub.YMap) else {}})
        note = trig["text"] if trig else None
        tm = job.get("timeout-minutes")
        items.append({
            "id": "job:" + jid, "kind": "job", "jobId": jid, "name": _s(job.get("name")) or jid, "file": path,
            "line": jobs.key_lines.get(jid), "endLine": jobs.key_ends.get(jid),
            "runsOn": _s(job.get("runs-on")) if not isinstance(job.get("runs-on"), list) else ", ".join(_list(job.get("runs-on"))),
            "needs": needs, "if": cond, "ifText": if_text(cond, note) if cond else None,
            "timeoutMin": tm if isinstance(tm, int) else None,
            "matrix": {k: _list(v) for k, v in m.items()} if isinstance(m, dict) else None,
            "env": job.get("env") if isinstance(job.get("env"), dict) else {},
            "lines": {"if": job.key_lines.get("if"), "needs": job.key_lines.get("needs"),
                      "runsOn": job.key_lines.get("runs-on"), "steps": job.key_lines.get("steps"),
                      "timeoutMin": job.key_lines.get("timeout-minutes"), "outputs": job.key_lines.get("outputs"),
                      "env": job.key_lines.get("env"), "permissions": job.key_lines.get("permissions")},
            "steps": steps,
            "outputs": {k: _s(v) for k, v in job["outputs"].items()} if isinstance(job.get("outputs"), dict) else {},
            "permissions": job.get("permissions"), "environment": _s(job.get("environment")) or None,
            "continueOnError": strip_expr(job.get("continue-on-error")) if job.get("continue-on-error") is not None else None,
            "comment": None,
        })
    return items, trig


def suite_of(path, text):
    """One workflow file -> contract suite (kind ci), or a suite with an `error` when it doesn't parse."""
    name = path.rsplit("/", 1)[-1]
    try:
        wf = yamlsub.parse(text)
    except yamlsub.YamlError as e:
        return {"id": "ci:" + name, "kind": "ci", "title": name, "file": path, "line": 1, "when": [], "items": [],
                "error": str(e), "errorLine": e.line}
    if not isinstance(wf, dict): wf = yamlsub.YMap()
    items, trig = jobs_of(wf, path)
    lines = text.split("\n")
    for it in items:                                                    # the comment on the job's key line and below it
        it["comment"] = _job_comment(lines, it["line"])
    return {"id": "ci:" + name, "kind": "ci", "title": _s(wf.get("name")) or name, "file": path,
            "line": wf.key_lines.get("on", 1), "when": whens(wf, path), "items": items,
            "workflowName": _s(wf.get("name")) or name, "env": wf.get("env") if isinstance(wf.get("env"), dict) else {},
            "concurrency": wf.get("concurrency"), "about": _header_comment(lines), "sanitizerTrigger": trig}


def _header_comment(lines):
    out = []
    for l in lines:
        s = l.strip()
        if not s.startswith("#"): break
        out.append(re.sub(r"^#+ ?", "", s))
    return "\n".join(out)


def _job_comment(lines, line):
    if not line or line > len(lines): return None
    first = lines[line - 1]
    out = []
    m = re.search(r"\s#\s?(.*)$", first)
    if m: out.append(m.group(1))
    ind = len(first) - len(first.lstrip())
    for l in lines[line:]:
        s = l.strip()
        if s.startswith("#") and len(l) - len(l.lstrip()) > ind: out.append(re.sub(r"^#+ ?", "", s)); continue
        break
    return "\n".join(out) or None

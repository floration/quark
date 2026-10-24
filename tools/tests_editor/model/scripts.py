"""Shell scripts / git hooks split into steps, and the ctest entries of CMakeLists.txt."""
import re

# a few well-known blocks get a name of their own; anything else is named by its comment or its first command
_NAMES = [
    (re.compile(r"xargs\s+-P\s*(\d+)"), lambda m: "parallel groups (%s at a time)" % m.group(1)),
    (re.compile(r"for\s+g\s+in\s+\$\(\S*quark_tests\s+groups\)"), lambda m: "list the groups (quark_tests groups)"),
    (re.compile(r"for\s+g\s+in\s+\$alone"), lambda m: "alone groups, one at a time"),
    (re.compile(r"exit 77"), lambda m: "skipped without a display (exit 77)"),
    (re.compile(r"command -v Xvfb|Xvfb :\d+ "), lambda m: "every map hosted headless + screenshotted (Xvfb)"),
    (re.compile(r"check_secrets\.py"), lambda m: "secrets check (tools/check_secrets.py)"),
    (re.compile(r"--uitest"), lambda m: "the UI test (quark --uitest)"),
]
_OPEN = re.compile(r"^\s*(?:if|for|while|until|case)\b|\{\s*$|\(\s*$")
_CLOSE = re.compile(r"(?:^|[;\s])(?:fi|done|esac)\s*(?:[;|&>].*)?$|^\s*[})]\s*(?:[;|&>].*)?$")
_FUNC = re.compile(r"^\s*(?:function\s+)?[A-Za-z_]\w*\s*\(\)\s*\{")


def _depth_delta(code):
    """Opens minus closes of compound commands on one line (if/for/while/case ... fi/done/esac, { }, function bodies)."""
    words = re.findall(r"(?:^|[\s;&|(])(if|for|while|until|case|fi|done|esac)(?=$|[\s;&|)])", code)
    d = sum(1 for w in words if w in ("if", "for", "while", "until", "case")) - sum(1 for w in words if w in ("fi", "done", "esac"))
    d += len(re.findall(r"(?:^|\s)\{(?:\s|$)", code)) - len(re.findall(r"(?:^|[\s;])\}(?:\s|;|$)", code))
    return d


def _code(line):
    """The line without a trailing # comment (roughly: quotes are respected)."""
    q, i = None, 0
    while i < len(line):
        c = line[i]
        if q:
            if c == "\\" and q == '"': i += 2; continue
            if c == q: q = None
        elif c in "'\"": q = c
        elif c == "#" and (i == 0 or line[i - 1] in " \t;"): return line[:i]
        i += 1
    return line


def steps(text):
    """Header comment + steps: [{id, kind, name, line, endLine, text, comment}] (1-based lines).

    A step is a top-level statement (a multi-line if / for / case counts as one) with the comments right above it;
    consecutive one-line statements stay together until a blank line, a comment or a compound statement."""
    lines = text.split("\n")
    if lines and lines[-1] == "": lines.pop()
    i, header = 0, []
    if lines and lines[0].startswith("#!"): i = 1
    while i < len(lines) and lines[i].lstrip().startswith("#"):
        header.append(re.sub(r"^\s*#+ ?", "", lines[i])); i += 1
    units = []                                                          # (start, end, comment lines, compound)
    pend_comment, cstart = [], None
    while i < len(lines):
        s = lines[i].strip()
        if not s:
            units.append(None); pend_comment, cstart = [], None; i += 1; continue
        if s.startswith("#"):
            if cstart is None: cstart = i
            pend_comment.append(re.sub(r"^#+ ?", "", s)); i += 1; continue
        start, depth, j = i, 0, i
        cont = False
        while j < len(lines):
            code = _code(lines[j])
            depth += _depth_delta(code)
            cont = code.rstrip().endswith(("\\", "|", "&&", "||"))
            j += 1
            if depth <= 0 and not cont: break
        compound = j - 1 > start or bool(re.search(r"(?:^|[\s;&|])(?:for|while|until|if|case)\s", _code(lines[start])))
        units.append((cstart if cstart is not None else start, j - 1, pend_comment, compound, start))
        pend_comment, cstart = [], None
        i = j
    out, cur = [], None

    def flush():
        if cur: out.append(cur)

    for u in units:
        if u is None:
            flush(); cur = None; continue
        s, e, com, compound, code_start = u
        if cur is None or com or compound or cur.get("_compound"):
            flush()
            cur = {"line": s + 1, "endLine": e + 1, "comment": "\n".join(com) or None, "_compound": compound,
                   "_first": lines[code_start].strip()}
        else:
            cur["endLine"] = e + 1
    flush()
    res = []
    for k, st in enumerate(out):
        body = "\n".join(lines[st["line"] - 1:st["endLine"]])
        name = None
        for rx, fn in _NAMES:
            m = rx.search(body)
            if m: name = fn(m); break
        if not name and k == 0 and re.search(r"^\s*(?:cd |set -e|export |[A-Z_]+=)", body, re.M): name = "setup"
        if not name and k == len(out) - 1 and re.search(r"\bexit\s+\$\w+|RESULT", body): name = "the result (exit code)"
        if not name and st["comment"]: name = st["comment"].split("\n")[0].strip(" .:")[:80]
        if not name: name = st["_first"][:80]
        res.append({"id": "step:%d" % (k + 1), "kind": "step", "name": name, "line": st["line"], "endLine": st["endLine"],
                    "text": body, "comment": st["comment"]})
    return "\n".join(header).strip(), res


def ctests(cmake_text, path="CMakeLists.txt"):
    """add_test(NAME x COMMAND ...) entries: [{id, kind, name, line, endLine, text, timeoutSec, skipCode, condition}]."""
    out, lines = [], cmake_text.split("\n")
    conds = []                                                          # the if() blocks we're inside
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        m = re.match(r"^if\s*\((.*)\)\s*$", s, re.I)
        if m: conds.append(m.group(1).strip())
        elif re.match(r"^endif\s*\(", s, re.I) and conds: conds.pop()
        elif re.match(r"^else(?:if)?\s*\(", s, re.I) and conds: conds[-1] = "not (%s)" % conds[-1]
        if re.match(r"^add_test\s*\(", s, re.I):
            j, buf = i, s
            while buf.count("(") > buf.count(")") and j + 1 < len(lines):
                j += 1; buf += " " + lines[j].strip()
            nm = re.search(r"\bNAME\s+(\S+)", buf)
            cmd = re.search(r"\bCOMMAND\s+(.*?)(?:\s+WORKING_DIRECTORY\b.*)?\)\s*$", buf)
            name = nm.group(1) if nm else "test"
            out.append({"id": "test:" + name, "kind": "step", "name": name, "line": i + 1, "endLine": j + 1,
                        "text": "\n".join(lines[i:j + 1]), "command": cmd.group(1).strip() if cmd else None,
                        "timeoutSec": None, "skipCode": None, "condition": " AND ".join("(%s)" % c for c in conds) or None,
                        "file": path})
            i = j
        i += 1
    for t in out:                                                       # set_tests_properties(<name> PROPERTIES ...)
        for k, l in enumerate(lines):
            m = re.match(r"^\s*set_tests_properties\s*\(\s*%s\s+PROPERTIES\s+(.*)\)\s*$" % re.escape(t["name"]), l)
            if not m: continue
            tm = re.search(r"\bTIMEOUT\s+(\d+)", m.group(1)); sk = re.search(r"\bSKIP_RETURN_CODE\s+(\d+)", m.group(1))
            if tm: t["timeoutSec"] = int(tm.group(1))
            if sk: t["skipCode"] = int(sk.group(1))
            t["propertiesLine"] = k + 1
    return out

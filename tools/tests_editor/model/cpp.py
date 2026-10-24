"""C++ reading for the tests editor: the function index of tests/ and the group lists / shortcut commands of main().

Not a compiler: a lexer blanks comments, string / char literals and preprocessor lines (same length, newlines kept),
so braces, parens and names can be matched with plain scans on the blanked text, and every position still maps to
the original text and its line. Fast enough to re-run on every edit (tests/ is ~14k lines: ~40 ms).
"""
import bisect, re

# one pass over the source: the first alternative that matches at the earliest position wins
_LEX = re.compile(r"""(?=[/"'\#uULR])(?:
    (?P<line>//[^\n]*(?:\\\n[^\n]*)*)                       # // comment (a trailing backslash continues it)
  | (?P<block>/\*.*?(?:\*/|\Z))                             # /* comment */
  | (?P<raw>(?<![\w])(?:u8|u|U|L)?R"(?P<delim>[^()\\\s"]{0,16})\(.*?\)(?P=delim)")
  | (?P<str>(?<![\w])(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")      # "string"
  | (?P<chr>(?<![\w])(?:u8|u|U|L)?'(?:[^'\\\n]|\\.){1,8}')  # 'c' (not a digit separator: 1'000 has a digit before)
  | (?P<pp>\#(?:[^\n\\]|\\.|\\\n)*)                          # a preprocessor line (# only starts one out here)
)""", re.S | re.M | re.X)

KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "alignas", "static_assert",
            "do", "else", "new", "delete", "throw", "case", "alignof", "noexcept", "typeid", "co_return", "co_await"}
_CALLISH = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_PARENS, _BRACKETS = re.compile(r"[()]"), re.compile(r"[\[\]]")
_IDENT_CALL = re.compile(r"([A-Za-z_]\w*)(\s*)\(|\(|\)")
_TRAIL_QUAL = re.compile(r"\)\s*(?:(?:const|noexcept|override|final|mutable|&&|&)\s*)*(?:->[^;{}()]*)?\s*$")
_NS_OPEN = re.compile(r"(?:\bnamespace\s*[\w:]*|\bextern\s*\"\s*\")\s*$")
_TP = re.compile(r"\bTP\s*\(\s*(\d+)\s*\)")
_CHECK_DEF = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w*CHECK\w*)[ \t]*\(", re.M)

TEST_NAME = re.compile(r"^test[A-Z0-9_]|^test$|Tests$")


def is_test_name(name):
    return bool(TEST_NAME.search(name))


class Source:
    """One file: original text, blanked text, string literal spans, line lookup."""

    def __init__(self, path, text):
        self.path, self.text = path, text
        out, last, self.strings = [], 0, {}                         # strings: start offset -> (end, value)
        for m in _LEX.finditer(text):
            s, e = m.span()
            out.append(text[last:s])
            kind = m.lastgroup if m.lastgroup != "delim" else "raw"
            chunk = text[s:e]
            if kind in ("str", "raw", "chr"):
                q = chunk.index('"' if kind != "chr" else "'")
                out.append(" " * q + chunk[q] + _blank(chunk[q + 1:-1]) + chunk[-1])
                if kind == "str": self.strings[s + q] = (e, None)
                elif kind == "raw":
                    d = m.group("delim") or ""
                    self.strings[s + q] = (e, chunk[q + 2 + len(d):-(2 + len(d))])
            else:
                out.append(_blank(chunk))
            last = e
        out.append(text[last:])
        self.blank = "".join(out)
        self.nl = _newlines(text)
        self._braces = None

    def line(self, pos):
        """1-based line of an offset."""
        return bisect.bisect_left(self.nl, pos) + 1

    def line_start(self, line):
        return 0 if line <= 1 else self.nl[line - 2] + 1

    def braces(self):
        """{open offset: close offset} for every brace pair (an unmatched one closes at the end of the file)."""
        if self._braces is None:
            m, stack = {}, []
            for b in re.finditer(r"[{}]", self.blank):
                if b.group() == "{": stack.append(b.start())
                elif stack: m[stack.pop()] = b.start()
            for o in stack: m[o] = len(self.blank)
            self._braces = m
        return self._braces

    def match_paren(self, i, open_="(", close="()"):
        """Offset of the paren closing the one at i (len(text) if none)."""
        depth = 0
        for m in (_PARENS if close == "()" else _BRACKETS).finditer(self.blank, i):
            depth += 1 if m.group() in "([" else -1
            if depth == 0: return m.start()
        return len(self.blank)

    def string_at(self, i):
        """The string literal starting at offset i (its quote), or None."""
        s = self.strings.get(i)
        if not s: return None
        return s[1] if s[1] is not None else _unescape(self.text[i + 1:s[0] - 1])


def _newlines(text):
    out, i = [], text.find("\n")
    while i >= 0:
        out.append(i); i = text.find("\n", i + 1)
    return out


def _blank(s):
    if "\n" not in s: return " " * len(s)
    return "\n".join(" " * len(x) for x in s.split("\n"))


_ESC = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}


def _unescape(s):
    return re.sub(r"\\(.)", lambda m: _ESC.get(m.group(1), m.group(1)), s)


def collapse(s):
    return re.sub(r"\s+", " ", s).strip()


# ---- the function index --------------------------------------------------------------------------------------

def functions(src):
    """Every function defined at namespace scope: [{name, args, ret, line, endLine, start, open, close, declLine}]."""
    blank, br, out = src.blank, src.braces(), []
    pos, n = 0, len(blank)
    brace_re = re.compile(r"[{}]")
    while True:
        m = brace_re.search(blank, pos)
        if not m: break
        o = m.start()
        if m.group() == "}": pos = o + 1; continue
        close = br.get(o, n)
        head = blank[max(0, o - 400):o]
        if _NS_OPEN.search(head): pos = o + 1; continue                  # namespace / extern "C": look inside
        fn = _function_head(src, o)
        if fn:
            fn.update(open=o, close=close, endLine=src.line(close))
            out.append(fn)
        pos = close + 1
    return out


def _function_head(src, o):
    blank = src.blank
    head = blank[max(0, o - 400):o]
    q = _TRAIL_QUAL.search(head)
    if not q: return None
    rp = o - len(head) + q.start()                                    # the ')' before the body
    depth, lp = 0, -1
    for i in range(rp, max(-1, rp - 4000), -1):                       # back to its '('
        c = blank[i]
        if c == ")": depth += 1
        elif c == "(":
            depth -= 1
            if depth == 0: lp = i; break
        elif c in "{};" and depth: return None
    if lp < 0: return None
    nm = re.search(r"((?:[A-Za-z_]\w*\s*::\s*)*~?[A-Za-z_]\w*)\s*$", blank[max(0, lp - 200):lp])
    if not nm: return None
    name = re.sub(r"\s+", "", nm.group(1))
    base = name.rsplit("::", 1)[-1]
    if base in KEYWORDS: return None
    ns = lp - (len(blank[max(0, lp - 200):lp]) - nm.start())          # offset of the name
    b = max(blank.rfind(";", 0, ns), blank.rfind("}", 0, ns), blank.rfind("{", 0, ns))
    decl = blank[b + 1:ns]
    if "=" in decl or "(" in decl or "," in decl: return None          # a lambda / initializer, not a definition
    ret = collapse(decl)
    if not ret or ret.endswith(("return", "else")): return None
    ds = b + 1 + (len(decl) - len(decl.lstrip()))
    return {"name": name, "base": base, "args": collapse(src.text[lp + 1:rp]), "ret": ret,
            "line": src.line(ns), "declLine": src.line(ds), "start": ds}


def doc_above(src, line):
    """The // comment block (or /* */ block) right above a line, without the markers."""
    text = src.text
    def get(i):                                                       # 0-based line i
        a = src.line_start(i + 1)
        e = text.find("\n", a)
        return text[a:e if e >= 0 else len(text)]

    class _L:
        def __getitem__(self, i): return get(i)
    lines = _L()
    out, i = [], line - 2
    while i >= 0:
        s = lines[i].strip()
        if s.startswith("//"):
            out.append(re.sub(r"^//+ ?", "", s)); i -= 1; continue
        if s.endswith("*/"):
            blk = []
            while i >= 0:
                blk.append(lines[i].strip())
                if "/*" in lines[i]: break
                i -= 1
            body = [re.sub(r"^/\*+\s?|\s?\*+/$|^\*+ ?", "", x).rstrip() for x in reversed(blk)]
            out.extend(reversed([x for x in body if x])); i -= 1
            continue
        break
    return "\n".join(reversed(out))


def check_macros(texts):
    """Names of the CHECK-like macros #defined in these files (CHECK, ACHECK, LCHECK, ...)."""
    names = {"CHECK"}
    for t in texts: names.update(_CHECK_DEF.findall(t))
    return sorted(names)


def checks_in(src, a, b, macros_re):
    out = []
    for m in macros_re.finditer(src.blank, a, b):
        lp = src.blank.index("(", m.start())
        rp = src.match_paren(lp)
        out.append({"line": src.line(m.start()), "expr": collapse(src.text[lp + 1:min(rp, b)]), "macro": m.group(1)})
    return out


def calls_in(src, a, b, known, keep_all=False):
    """Calls made at statement level between offsets a..b, in order: [{fn, args, line, start}].

    Statement level: not inside another call's argument list (an `if (...)`/`return`/grouping paren is transparent),
    not a member call (x.f()), not qualified (std::f()), and not a plain initializer (`T p = TP(1)`). Only names in
    `known` or that look like tests (test*, *Tests) count, unless keep_all."""
    blank, out, stack = src.blank, [], []
    for m in _IDENT_CALL.finditer(blank, a, b):
        g = m.group()
        if g == "(":
            stack.append(True); continue
        if g == ")":
            if stack: stack.pop()
            continue
        name = m.group(1)
        kw = name in KEYWORDS
        opaque = any(not t for t in stack)
        stack.append(kw)
        if kw or opaque: continue
        if not (keep_all or name in known or is_test_name(name)): continue
        pre = blank[max(a, m.start() - 3):m.start()].rstrip()
        if pre.endswith((".", "->", "::")): continue
        p2 = blank[max(a, m.start() - 40):m.start()].rstrip()
        if p2.endswith("=") and not re.search(r"[=!<>+\-*/%&|^]=$", p2): continue
        if re.search(r"(?:\b(?:void|int|bool|auto|static|double|float|char|unsigned|uint\d+_t|size_t|std::\w+)\s*\*?)$", p2):
            continue                                                  # a declaration: `int f(...)`
        lp = m.end() - 1
        rp = src.match_paren(lp)
        out.append({"fn": name, "args": collapse(src.text[lp + 1:min(rp, b)]), "line": src.line(m.start()),
                    "start": m.start()})
    return out


def index_files(sources):
    """{name: fn record} for every function defined in these Sources (first definition wins; others: overloads)."""
    macros = check_macros([s.text for s in sources])
    macros_re = re.compile(r"\b(" + "|".join(map(re.escape, macros)) + r")\s*\(")
    defs = []
    for s in sources:
        for f in functions(s):
            f["src"] = s
            defs.append(f)
    known = {f["base"] for f in defs}
    index = {}
    for f in defs:
        s = f["src"]
        rec = {"name": f["base"], "file": s.path, "line": f["line"], "endLine": f["endLine"], "args": f["args"],
               "ret": f["ret"], "doc": doc_above(s, f["declLine"]),
               "checks": checks_in(s, f["open"], f["close"], macros_re),
               "calls": [], "usedBy": [], "_src": s, "_open": f["open"], "_close": f["close"]}
        seen = []
        for m in _CALLISH.finditer(s.blank, f["open"], f["close"]):
            n = m.group(1)
            if n in known and n != rec["name"] and n not in seen and s.blank[m.start() - 1] not in ".>:": seen.append(n)
        rec["calls"] = seen
        rec["portOffsets"] = _port_span(s, f)
        if rec["name"] in index:
            index[rec["name"]].setdefault("overloads", []).append({"file": s.path, "line": f["line"], "args": f["args"]})
        else:
            index[rec["name"]] = rec
    return index, macros


def _port_span(src, f):
    """Offsets K of every `<port param> + K` in the body (the test also uses port + K); None: no port parameter."""
    m = re.search(r"\b(?:uint16_t|unsigned\s+short|u16)\s+(\w+)", f["args"]) or re.search(r"\bint\s+(\w*port\w*)", f["args"], re.I)
    if not m: return None
    return sorted({int(k) for k in re.findall(r"\b%s\s*\+\s*(\d+)\b" % re.escape(m.group(1)), src.blank[f["open"]:f["close"]])})


# ---- main(): group lists and shortcut commands ---------------------------------------------------------------

_VEC = re.compile(r"\b(?:std\s*::\s*)?vector\s*<\s*([\w:]+)\s*>\s*([A-Za-z_]\w*)\s*=?\s*\{")
_ARGV1 = re.compile(r"argv\s*\[\s*1\s*\]\s*\)?\s*==\s*\"")


def main_layout(src, main_fn, known):
    """Group lists and shortcut commands in main(): (lists, shortcuts).

    lists = [{name, line, endLine, groups: [{name, line, endLine, calls, locals}]}]
    shortcuts = [{name, line, endLine, calls}] (the `if (... argv[1] == "x") ...` commands, framework ones left out)
    """
    a, b = main_fn["_open"] + 1, main_fn["_close"]
    blank, br = src.blank, src.braces()
    lists, list_spans = [], []
    for m in _VEC.finditer(blank, a, b):
        o = m.end() - 1
        c = br.get(o, b)
        groups, pos = [], o + 1
        while True:
            e = blank.find("{", pos, c)
            if e < 0: break
            ec = br.get(e, c)
            g = _group_entry(src, e, ec, known)
            if g: groups.append(g)
            pos = ec + 1
        if groups:
            lists.append({"name": m.group(2), "type": m.group(1), "line": src.line(m.start()), "endLine": src.line(c),
                          "groups": groups})
            list_spans.append((m.start(), c))
    names = {l["name"] for l in lists}
    shortcuts = []
    for m in re.finditer(r"\bif\s*\(", blank[a:b]):
        lp = a + m.end() - 1
        rp = src.match_paren(lp)
        if rp >= b: continue
        if any(s <= lp <= e for s, e in list_spans): continue
        am = _ARGV1.search(blank, lp, rp)
        if not am: continue
        name = src.string_at(am.end() - 1)
        if name is None: continue
        bs = rp + 1
        while bs < b and blank[bs].isspace(): bs += 1
        if bs < b and blank[bs] == "{":
            be = br.get(bs, b); body = (bs + 1, be)
        else:
            be = _statement_end(blank, bs, b); body = (bs, be)
        inner = blank[body[0]:body[1]]
        if names and re.search(r"\b(?:%s)\b" % "|".join(map(re.escape, names)), inner): continue   # (groups / group NAME)
        calls = calls_in(src, body[0], body[1], known)
        shortcuts.append({"name": name, "line": src.line(m.start() + a), "endLine": src.line(be), "calls": calls,
                          "ports": _ports(src, body[0], body[1], calls), "locals": _locals(src, body[0], body[1])})
    return lists, shortcuts


def _statement_end(blank, i, b):
    depth = 0
    for m in re.finditer(r"[(){};]", blank[i:b]):
        c = m.group()
        if c in "({": depth += 1
        elif c in ")}": depth -= 1
        elif depth <= 0: return i + m.start()
    return b


def _group_entry(src, e, ec, known):
    """One `{"name", [] { ... }}` entry of a group list."""
    blank = src.blank
    q = blank.find('"', e, ec)
    if q < 0: return None
    name = src.string_at(q)
    lb = blank.find("[", q, ec)
    if name is None or lb < 0: return None
    rb = src.match_paren(lb, close="[]")
    body_open = blank.find("{", rb, ec)
    if body_open < 0: return None
    body_close = src.braces().get(body_open, ec)
    calls = calls_in(src, body_open + 1, body_close, known)
    return {"name": name, "line": src.line(e), "endLine": src.line(ec), "calls": calls,
            "locals": _locals(src, body_open + 1, body_close), "ports": _ports(src, body_open + 1, body_close, calls),
            "_body": (body_open + 1, body_close)}


def _locals(src, a, b):
    """{var: 'TP(28900)'} for `uint16_t p = TP(28900);`-style locals."""
    return {m.group(1): "TP(%s)" % m.group(2)
            for m in re.finditer(r"\b(\w+)\s*=\s*TP\s*\(\s*(\d+)\s*\)", src.blank[a:b])}


def _ports(src, a, b, calls):
    """[(port, call fn or None)] each TP() port a lambda passes: TP(N) literals and `p + K` with p = TP(N)."""
    loc = {m.group(1): int(m.group(2)) for m in re.finditer(r"\b(\w+)\s*=\s*TP\s*\(\s*(\d+)\s*\)", src.blank[a:b])}
    out = []
    for c in calls:
        m = _TP.search(c["args"])
        if m: out.append((int(m.group(1)), c["fn"])); continue
        m = re.match(r"^\s*(\w+)\s*(?:\+\s*(\d+))?\s*(?:,|$)", c["args"])
        if m and m.group(1) in loc: out.append((loc[m.group(1)] + int(m.group(2) or 0), c["fn"]))
    return out

"""A YAML subset good enough for GitHub workflow files, keeping line numbers.

Block maps and lists, `- key: v` list items, `|` / `>` block scalars (with -/+ chomping and an indent digit), flow
lists / maps (`[a, 'b']`, `{ name: x, path: "a\\nb" }`, over several lines too), single / double quoted strings,
plain scalars folded over continuation lines, and comments. Keys always stay strings: `on:` is the key "on" (YAML
1.1 would read it as true, the trap GitHub's own parser avoids). Values: true / false / null / ~ and plain integers
are typed, everything else is a string. No anchors, tags or multi-document files (workflows don't use them).

Maps are YMap (a dict) and lists YList (a list) with line info, 1-based:
  .line / .end           first and last line with content
  .key_lines / .key_ends  per key (YMap) or per index (YList: .item_lines / .item_ends)
  .scalar_lines           key -> first content line of a block scalar value
"""
import re


class YMap(dict):
    def __init__(self):
        super().__init__()
        self.line = self.end = 0
        self.key_lines, self.key_ends, self.scalar_lines = {}, {}, {}


class YList(list):
    def __init__(self):
        super().__init__()
        self.line = self.end = 0
        self.item_lines, self.item_ends, self.scalar_lines = [], [], {}


class YamlError(ValueError):
    def __init__(self, msg, line):
        super().__init__("line %d: %s" % (line, msg))
        self.line = line


_KEY = re.compile(r"""^(?:"((?:[^"\\]|\\.)*)"|'((?:[^']|'')*)'|([^\s#'"\[\]{},:?&*!|>%@`-][^#]*?|-[^\s#][^#]*?|\?[^#\s][^#]*?))\s*:(?:\s+|$)""")
_INT = re.compile(r"^[-+]?(?:0|[1-9][0-9]*)$")


class _Line:
    __slots__ = ("no", "indent", "text", "raw")

    def __init__(self, no, raw):
        self.no, self.raw = no, raw
        body = raw.rstrip("\r")
        self.indent = len(body) - len(body.lstrip(" "))
        self.text = _strip_comment(body.strip())


def _strip_comment(s):
    """Text without a trailing ` # comment` (a # inside quotes stays)."""
    if "#" not in s: return s
    q, i = None, 0
    while i < len(s):
        c = s[i]
        if q:
            if c == "\\" and q == '"': i += 2; continue
            if c == q:
                if q == "'" and s[i + 1:i + 2] == "'": i += 2; continue
                q = None
        elif c in "'\"" and (i == 0 or s[i - 1] in " \t[{,:-"):
            q = c
        elif c == "#" and (i == 0 or s[i - 1] in " \t"):
            return s[:i].rstrip()
        i += 1
    return s


def parse(text):
    lines = [_Line(i + 1, raw) for i, raw in enumerate(text.split("\n"))]
    p = _Parser(lines)
    i = p.skip(0)
    if i >= len(lines): return YMap()
    v, i = p.block(i, lines[i].indent)
    i = p.skip(i)
    if i < len(lines): raise YamlError("unexpected text (bad indentation?)", lines[i].no)
    return v


class _Parser:
    def __init__(self, lines):
        self.L = lines
        self.last = 0                                                   # the last line with content consumed

    def skip(self, i):
        while i < len(self.L) and not self.L[i].text: i += 1
        return i

    def block(self, i, indent):
        ln = self.L[i]
        if ln.text == "-" or ln.text.startswith("- "): return self.seq(i, ln.indent)
        if _KEY.match(ln.text): return self.map(i, ln.indent)
        return self.scalar_lines(i, indent)

    def seq(self, i, indent):
        out = YList(); out.line = self.L[i].no
        while True:
            i = self.skip(i)
            if i >= len(self.L): break
            ln = self.L[i]
            if ln.indent != indent or not (ln.text == "-" or ln.text.startswith("- ")):
                if ln.indent > indent: raise YamlError("bad indentation in a list", ln.no)
                break
            out.item_lines.append(ln.no)
            self.last = ln.no
            rest = ln.text[1:].lstrip()
            if not rest:
                j = self.skip(i + 1)
                if j < len(self.L) and self.L[j].indent > indent:
                    v, i = self.block(j, self.L[j].indent)
                else:
                    v, i = None, i + 1
            else:
                col = indent + 1 + (len(ln.text) - 1 - len(ln.text[1:].lstrip()))
                raw_rest = ln.raw.rstrip("\r")[col:]
                self.L[i] = _virtual(ln, col, rest, raw_rest)          # `- key: v`: a block at the item's column
                if rest.startswith(("|", ">")): v, i = self.block_scalar(i, indent, rest, out, len(out))
                else: v, i = self.block(i, col)
            out.append(v)
            out.item_ends.append(self.last)
        out.end = self.last
        return out, i

    def map(self, i, indent):
        out = YMap(); out.line = self.L[i].no
        while True:
            i = self.skip(i)
            if i >= len(self.L): break
            ln = self.L[i]
            if ln.indent != indent:
                if ln.indent > indent: raise YamlError("bad indentation", ln.no)
                break
            m = _KEY.match(ln.text)
            if not m:
                if ln.text.startswith("- "): break                     # (a list at the key's own indent ends it)
                raise YamlError("expected 'key: value'", ln.no)
            key = m.group(1) if m.group(1) is not None else (m.group(2).replace("''", "'") if m.group(2) is not None else m.group(3))
            if m.group(1) is not None: key = _dq(key)
            rest = ln.text[m.end():]
            out.key_lines[key] = ln.no
            self.last = ln.no
            if not rest:
                j = self.skip(i + 1)
                if j < len(self.L) and (self.L[j].indent > indent or
                                        (self.L[j].indent == indent and (self.L[j].text == "-" or self.L[j].text.startswith("- ")))):
                    v, i = self.block(j, self.L[j].indent)
                else:
                    v, i = None, i + 1
            elif rest.startswith(("|", ">")):
                v, i = self.block_scalar(i, indent, rest, out, key)
            elif rest[0] in "[{":
                v, i = self.flow_lines(i, m.end())
            elif rest[0] in "'\"":
                v, i = self.quoted_lines(i, m.end(), indent)
            else:
                v, i = self.plain(i, rest, indent)
            out[key] = v
            out.key_ends[key] = self.last
        out.end = self.last
        return out, i

    def scalar_lines(self, i, indent):
        t = self.L[i].text
        if t[0] in "[{": return self.flow_lines(i, 0)
        if t[0] in "'\"": return self.quoted_lines(i, 0, indent - 1)
        return self.plain(i, t, indent - 1)

    def plain(self, i, first, indent):
        parts, self.last = [first], self.L[i].no
        i += 1
        while i < len(self.L):                                          # folded continuation lines
            ln = self.L[i]
            if not ln.text:
                j = self.skip(i)
                if j < len(self.L) and self.L[j].indent > indent and not ln.raw.strip().startswith("#"): parts.append("\n"); i = j; continue
                break
            if ln.indent <= indent: break
            parts.append(ln.text); self.last = ln.no; i += 1
        s = " ".join(p for p in parts if p != "\n") if "\n" not in parts else _fold(parts)
        return _typed(s), i

    def quoted_lines(self, i, col, indent):
        """A quoted scalar starting at text column col (it may continue on the next lines)."""
        buf, start = self.L[i].text[col:], i
        q = buf[0]
        while True:
            end = _quote_end(buf, q)
            if end >= 0 or i + 1 >= len(self.L): break
            i += 1
            buf += " " + self.L[i].raw.strip()
        self.last = self.L[i].no
        if end < 0: raise YamlError("unterminated string", self.L[start].no)
        tail = _strip_comment(buf[end + 1:].strip())
        if tail: raise YamlError("text after a quoted string", self.L[i].no)
        body = buf[1:end]
        return (_dq(body) if q == '"' else body.replace("''", "'")), i + 1

    def flow_lines(self, i, col):
        buf, start = self.L[i].text[col:], i
        while _unbalanced(buf) and i + 1 < len(self.L):
            i += 1
            buf += " " + self.L[i].text
        self.last = self.L[i].no
        try:
            v, k = _Flow(buf).value(0)
        except ValueError as e:
            raise YamlError("flow value: %s" % e, self.L[start].no)
        if buf[k:].strip(): raise YamlError("text after a flow value", self.L[i].no)
        return v, i + 1

    def block_scalar(self, i, indent, header, owner, key):
        m = re.match(r"^([|>])([+-]?)([1-9]?)([+-]?)\s*$", header)
        if not m: raise YamlError("bad block scalar header %r" % header, self.L[i].no)
        style, chomp, explicit = m.group(1), m.group(2) or m.group(4), m.group(3)
        j, body, first = i + 1, [], None
        content_indent = indent + int(explicit) if explicit else None
        while j < len(self.L):
            raw = self.L[j].raw.rstrip("\r")
            if not raw.strip():
                body.append(""); j += 1; continue
            ind = len(raw) - len(raw.lstrip(" "))
            if content_indent is None:
                if ind <= indent: break
                content_indent = ind
            if ind < content_indent: break
            if first is None: first = self.L[j].no
            body.append(raw[content_indent:]); self.last = self.L[j].no; j += 1
        while body and body[-1] == "" and chomp != "+": body.pop()      # trailing blank lines: chomping decides
        text = "\n".join(body) if style == "|" else _fold_block(body)
        if chomp != "-" and (body or chomp == "+"): text += "\n"
        if first is not None: owner.scalar_lines[key] = first
        return text, j


def _virtual(ln, col, text, raw):
    v = _Line.__new__(_Line)
    v.no, v.indent, v.text, v.raw = ln.no, col, text, " " * col + raw
    return v


def _quote_end(s, q):
    i = 1
    while i < len(s):
        if q == '"' and s[i] == "\\": i += 2; continue
        if s[i] == q:
            if q == "'" and s[i + 1:i + 2] == "'": i += 2; continue
            return i
        i += 1
    return -1


def _unbalanced(s):
    depth, q, i = 0, None, 0
    while i < len(s):
        c = s[i]
        if q:
            if c == "\\" and q == '"': i += 2; continue
            if c == q: q = None
        elif c in "'\"": q = c
        elif c in "[{": depth += 1
        elif c in "]}": depth -= 1
        i += 1
    return depth > 0


_DQ = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "/": "/", " ": " ", "e": "\x1b", "a": "\a",
       "b": "\b", "f": "\f", "v": "\v"}


def _dq(s):
    def esc(m):
        c = m.group(1)
        if c in "xuU":
            return chr(int(m.group(2), 16)) if m.group(2) else m.group(0)
        return _DQ.get(c, c)
    return re.sub(r"\\(?:([xuU])([0-9a-fA-F]{2,8})|(.))", lambda m: esc(m) if m.group(1) else _DQ.get(m.group(3), m.group(3)), s)


def _typed(s):
    if s in ("true", "True", "TRUE"): return True
    if s in ("false", "False", "FALSE"): return False
    if s in ("null", "Null", "NULL", "~", ""): return None
    if _INT.match(s):
        try: return int(s)
        except ValueError: pass
    return s


def _fold(parts):
    out = ""
    for p in parts:
        if p == "\n": out += "\n"
        else: out += ("" if not out or out.endswith("\n") else " ") + p
    return out


def _fold_block(body):
    out, prev_more = [], False
    for k, ln in enumerate(body):
        more = ln.startswith((" ", "\t"))
        if not out: out.append(ln)
        elif ln == "" or more or prev_more or out[-1] == "": out.append("\n" + ln)
        else: out.append(" " + ln)
        prev_more = more
    return "".join(out)


class _Flow:
    def __init__(self, s): self.s = s

    def ws(self, i):
        while i < len(self.s) and self.s[i] in " \t\n": i += 1
        return i

    def value(self, i):
        i = self.ws(i)
        if i >= len(self.s): raise ValueError("unexpected end")
        c = self.s[i]
        if c == "[":
            out, i = [], self.ws(i + 1)
            while True:
                if i < len(self.s) and self.s[i] == "]": return out, i + 1
                v, i = self.value(i)
                i = self.ws(i)
                if i < len(self.s) and self.s[i] == ":":                   # [a: b] = [{a: b}]
                    v2, i = self.value(i + 1); v = {str(v): v2}; i = self.ws(i)
                out.append(v)
                if i < len(self.s) and self.s[i] == ",": i = self.ws(i + 1); continue
                if i < len(self.s) and self.s[i] == "]": return out, i + 1
                raise ValueError("expected , or ]")
        if c == "{":
            out, i = {}, self.ws(i + 1)
            while True:
                if i < len(self.s) and self.s[i] == "}": return out, i + 1
                k, i = self.scalar(i, key=True)
                i = self.ws(i)
                v = None
                if i < len(self.s) and self.s[i] == ":":
                    v, i = self.value(i + 1)
                    i = self.ws(i)
                out[str(k) if k is not None else ""] = v
                if i < len(self.s) and self.s[i] == ",": i = self.ws(i + 1); continue
                if i < len(self.s) and self.s[i] == "}": return out, i + 1
                raise ValueError("expected , or }")
        return self.scalar(i)

    def scalar(self, i, key=False):
        i = self.ws(i)
        s = self.s
        if i < len(s) and s[i] in "'\"":
            e = _quote_end(s[i:], s[i])
            if e < 0: raise ValueError("unterminated string")
            body = s[i + 1:i + e]
            return (_dq(body) if s[i] == '"' else body.replace("''", "'")), i + e + 1
        j = i
        while j < len(s):
            c = s[j]
            if c == "$" and s.startswith("${{", j):                           # an expression: braces inside are its own
                e = s.find("}}", j + 3)
                j = e + 2 if e >= 0 else len(s); continue
            if c in ",]}": break
            if c == ":" and (j + 1 >= len(s) or s[j + 1] in " ,]}"): break
            j += 1
        raw = s[i:j].strip()
        return (raw if key else _typed(raw)), j

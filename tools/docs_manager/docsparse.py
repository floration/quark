#!/usr/bin/env python3
"""Parses the Claude Documents markdown files into entries, and diffs two versions of a file into
entry-level changes (added / edited / status / moved / removed). Pure functions, no I/O, no git.

Entry shape in files 01-08 (README.md of Claude Documents):

    - **Title** (P1-P3 if known). Body ... _YYYY-MM-DD, <thread title>, <status>[: note]_

An entry may wrap over several lines; it ends at a blank line, a heading, the next `- ` bullet, or (once its
trailing italic is seen) the next unindented line, which is then section text. Entries without the italic
get status "unknown". 00-agent-rules.md, README.md and anything not named NN-*.md are documents.
Python 3 standard library only.
"""
import difflib, re

PARSER_VERSION = 4          # bump when parse/diff output changes: cached diffs are recomputed

STATUSES = ["open", "in-progress", "fixed", "wontfix", "answered", "told", "done"]
DONE_STATUSES = {"fixed", "wontfix", "answered", "told", "done"}
_ALIASES = {"in progress": "in-progress", "inprogress": "in-progress", "won't fix": "wontfix",
            "wont fix": "wontfix", "not a bug": "wontfix", "asked": "open", "waiting": "open",
            "planned": "open", "closed": "done", "shipped": "done", "merged": "done"}

_ENTRY_RE = re.compile(r"^(\s{0,3})[-*]\s+(.*)$")
_HEAD_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
_TRAILER_RE = re.compile(r"(?:^|\s)_(\d{4}-\d{2}-\d{2})((?:,[^\n]*?)?)_\s*$")
_PRIO_RE = re.compile(r"\(\s*(P[1-3])(?:\s*/\s*P[1-3])?\s*\)")
_BOLD_RE = re.compile(r"^\*\*(.+?)\*\*(.*)$", re.S)

FILE_KINDS = {  # what one entry of each file is, for change sentences
    "01": "bug", "02": "work item", "03": "possible bug", "04": "performance improvement",
    "05": "possible performance improvement", "06": "note for q", "07": "question for q", "08": "note"}


def file_kind(name):
    """'entries' for NN-*.md except 00 (rules), else 'document'."""
    m = re.match(r"^(\d\d)-.*\.md$", name)
    return "entries" if m and m.group(1) != "00" else "document"


def slug(text):
    s = re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")
    return s[:60].rstrip("-") or "entry"


def normalize_status(text, thread_titles=()):
    """(status, note) from the trailer's status text: first word if it is a known status, else a known
    status word or alias found in the text (note keeps the full text), else 'unknown'."""
    t = (text or "").strip()
    if not t: return "unknown", ""
    low = t.lower()
    for s in STATUSES + ["in progress"]:
        if low == s or re.match(re.escape(s) + r"\b(?![-\w])", low):
            rest = t[len(s):].lstrip(" :,;-").strip()
            return ("in-progress" if s == "in progress" else s), rest
    for a, s in _ALIASES.items():
        if re.match(re.escape(a) + r"\b", low): return s, t
    for s in STATUSES:
        if re.search(r"\b" + re.escape(s) + r"\b", low): return s, t
    for a, s in _ALIASES.items():
        if re.search(r"\b" + re.escape(a) + r"\b", low): return s, t
    return "unknown", t


def parse_trailer(inner, thread_names=()):
    """inner = text after the date inside _..._ (starts with ', ' or empty) -> (thread, status, note, raw)."""
    rest = inner.lstrip(",").strip()
    if not rest: return None, "unknown", "", ""
    thread, stext = None, ""
    low = rest.lower()
    for name in sorted(thread_names, key=len, reverse=True):      # a known thread title may hold commas
        n = name.lower()
        if low == n or low.startswith(n + ","):
            thread, stext = rest[:len(name)], rest[len(name) + 1:].strip()
            break
    if thread is None:
        parts = rest.split(",", 1)
        first = parts[0].strip()
        if len(parts) == 1:
            st, note = normalize_status(first)
            if st != "unknown" and len(first.split()) <= 3: return None, st, note, first
            return first, "unknown", "", ""
        thread, stext = first, parts[1].strip()
    st, note = normalize_status(stext)
    return thread, st, note, stext


def _split_title(text):
    """'**Title** (P2). Body' -> (title, priority, body). Without bold, the first sentence is the title."""
    m = _BOLD_RE.match(text)
    prio = None
    if m:
        title, body = m.group(1).strip(), m.group(2)
        pm = _PRIO_RE.search(title)
        if pm:
            prio = pm.group(1)
            title = (title[:pm.start()] + title[pm.end():]).strip()
            title = re.sub(r"\s+([.,:;])", r"\1", title)
        pm = re.match(r"\s*" + _PRIO_RE.pattern + r"\s*([.,:;])?", body)
        if pm:
            prio = prio or pm.group(1)
            body = body[pm.end():]
        body = re.sub(r"^\s*[;:,.]\s*", "", body).strip()      # '**Title**: body' / '**Title**; body' 
    else:
        sm = re.match(r"(.{8,}?[.:!?])(\s|$)", text)
        title = sm.group(1) if sm and len(sm.group(1)) <= 120 else text[:80].rstrip() + ("..." if len(text) > 80 else "")
        body = text[len(sm.group(1)):].strip() if sm and len(sm.group(1)) <= 120 else text
    if not prio:
        pm = _PRIO_RE.search(body)
        if pm: prio = pm.group(1)
    return title, prio, body


def parse_file(name, text, thread_names=()):
    """-> {"title", "kind", "sections": [...], "notes": [{"section", "text"}], "entries": [...]}.
    Entry: id, file, section, title, priority, body, date, thread, status, statusNote, statusText, line."""
    lines = (text or "").replace("\r\n", "\n").replace("\r", "\n").split("\n")
    kind = file_kind(name)
    out = {"title": "", "kind": kind, "sections": [], "notes": [], "entries": []}
    section, cur, closed, note = "", None, False, None

    def finish():
        nonlocal cur
        if cur is not None: out["entries"].append(_make_entry(name, section_of[0], cur, thread_names))
        cur = None

    def flush_note():
        nonlocal note
        if note and note["text"].strip(): out["notes"].append(note)
        note = None

    section_of = [""]
    for i, line in enumerate(lines, 1):
        h = _HEAD_RE.match(line)
        if h:
            finish(); flush_note()
            if len(h.group(1)) == 1 and not out["title"]:
                out["title"] = h.group(2)
            else:
                section = h.group(2)
                if section not in out["sections"]: out["sections"].append(section)
            section_of[0] = section
            continue
        if kind != "entries": continue
        e = _ENTRY_RE.match(line)
        if e and (len(e.group(1)) < 2 or cur is None):           # deeper bullets belong to the entry above
            finish(); flush_note()
            cur = {"lines": [e.group(2)], "line": i}
            closed = bool(_TRAILER_RE.search(e.group(2)))
            continue
        if not line.strip():
            finish(); closed = False
            if note: note["text"] += "\n"
            continue
        indented = line[:1] in (" ", "\t")
        if cur is not None and (indented or not closed):
            cur["lines"].append(("  " + line.strip()) if e else line.strip())
            closed = closed or bool(_TRAILER_RE.search(line))
            continue
        finish()
        if note is None: note = {"section": section, "text": line.strip()}
        else: note["text"] = (note["text"].rstrip("\n") + ("\n\n" if note["text"].endswith("\n") else "\n") + line.strip())
    finish(); flush_note()
    if out["entries"] and any(en["section"] == "" for en in out["entries"]) and "" not in out["sections"]:
        out["sections"].insert(0, "")
    seen = {}
    for en in out["entries"]:
        n = seen.get(en["id"], 0) + 1
        seen[en["id"]] = n
        if n > 1: en["id"] += "#%d" % n
    return out


def _make_entry(name, section, cur, thread_names):
    text = "\n".join(cur["lines"]).strip()
    date = thread = None
    status, note, stext = "unknown", "", ""
    m = _TRAILER_RE.search(text)
    if m:
        date = m.group(1)
        thread, status, note, stext = parse_trailer(m.group(2), thread_names)
        text = text[:m.start()].rstrip()
    title, prio, body = _split_title(text)
    return {"id": "%s:%s" % (name, slug(title)), "file": name, "section": section, "title": title,
            "priority": prio, "body": body, "date": date, "thread": thread, "status": status,
            "statusNote": note, "statusText": stext, "archived": section.strip().lower() == "archive",
            "line": cur["line"]}


# ---- diff -------------------------------------------------------------------------------------------------

def _ratio(a, b):
    if not a and not b: return 1.0
    sm = difflib.SequenceMatcher(None, a or "", b or "", autojunk=False)
    if sm.real_quick_ratio() < 0.3 or sm.quick_ratio() < 0.3: return 0.0
    return sm.ratio()


def _words(t):
    return set(re.findall(r"[a-z0-9]+", (t or "").lower()))


def _similar(a, b):
    """0..1 how likely b is an edited a (title mostly, body as backup)."""
    ta, tb = a["title"], b["title"]
    tr = _ratio(ta.lower(), tb.lower())
    wa, wb = _words(ta), _words(tb)
    contain = 0.0
    if wa and wb and (wa <= wb or wb <= wa): contain = 0.6 + 0.3 * min(len(wa), len(wb)) / max(len(wa), len(wb))
    br = _ratio(a["body"][:600], b["body"][:600])
    return max(tr, contain, 0.45 * tr + 0.55 * br, br if br > 0.85 else 0.0)


def _pair(old, new, threshold):
    """Greedy best-first pairing of two entry lists by similarity -> [(o, n)], leftovers."""
    cands = []
    for i, o in enumerate(old):
        for j, n in enumerate(new):
            s = _similar(o, n)
            if s >= threshold: cands.append((s, i, j))
    cands.sort(key=lambda c: (-c[0], c[1], c[2]))
    used_o, used_n, pairs = set(), set(), []
    for s, i, j in cands:
        if i in used_o or j in used_n: continue
        used_o.add(i); used_n.add(j); pairs.append((old[i], new[j]))
    return pairs, [o for i, o in enumerate(old) if i not in used_o], [n for j, n in enumerate(new) if j not in used_n]


def _short(title):
    return (title or "").rstrip(" .:,;")


def _added_summary(e):
    k = FILE_KINDS.get(e["file"][:2], "entry")
    t = _short(e["title"])
    if e["file"].startswith("02"):
        return ("Work claimed: %s" if e["status"] == "in-progress" else
                "Planned: %s" if e["status"] in ("open", "unknown") else "Work recorded as %s: %%s" % e["status"]) % t
    return "New %s: %s" % (k, t) + ("" if e["status"] in ("open", "unknown", "in-progress") else " (%s)" % e["status"])


def _item(kind, e, summary, **extra):
    """One change item; 'what' is the summary without the leading '<title>: ' (for 'last changed: ...')."""
    t = _short(e["title"]) + ": "
    it = {"entryId": e["id"], "file": e["file"], "title": e["title"], "kind": kind, "from": None, "to": None,
          "summary": summary, "what": summary[len(t):] if kind != "added" and summary.startswith(t) else kind,
          "section": e["section"]}
    it.update(extra)
    return it


def _compare(o, n):
    """One item for a matched pair, or None when nothing a reader cares about changed."""
    t = _short(n["title"])
    extra = {}
    if o["id"] != n["id"]: extra["fromId"] = o["id"]
    if o["file"] != n["file"]: extra["fromFile"] = o["file"]
    moved_sec = o["section"] != n["section"]
    if moved_sec: extra.update(fromSection=o["section"], toSection=n["section"])
    if o["status"] != n["status"]:
        s = "%s: %s -> %s" % (t, o["status"], n["status"])
        if n["statusNote"] and n["statusNote"] != o["statusNote"]: s += " (%s)" % n["statusNote"]
        if moved_sec: s += ", moved to %s" % (n["section"] or "the top")
        return _item("status", n, s, **dict(extra, **{"from": o["status"], "to": n["status"]}))
    if o["file"] != n["file"]:
        return _item("moved", n, "%s: moved from %s to %s" % (t, o["file"], n["file"]),
                     **dict(extra, **{"from": o["file"], "to": n["file"]}))
    what = []
    if o["title"] != n["title"]: what.append("title was \"%s\"" % _short(o["title"]))
    if o["priority"] != n["priority"]: what.append("priority %s -> %s" % (o["priority"] or "none", n["priority"] or "none"))
    if o["statusNote"] != n["statusNote"]: what.append("status note now \"%s\"" % n["statusNote"] if n["statusNote"] else "status note removed")
    if o["body"] != n["body"]: what.append("text edited")
    if o["thread"] != n["thread"]: what.append("thread %s -> %s" % (o["thread"], n["thread"]))
    if o["date"] != n["date"]: what.append("date %s -> %s" % (o["date"], n["date"]))
    if moved_sec and not what:
        return _item("moved", n, "%s: moved from %s to %s" % (t, o["section"] or "the top", n["section"] or "the top"),
                     **dict(extra, **{"from": o["section"], "to": n["section"]}))
    if not what: return None
    if moved_sec: what.append("moved to %s" % (n["section"] or "the top"))
    ex = dict(extra)
    if o["title"] != n["title"]: ex.update({"from": o["title"], "to": n["title"]})
    return _item("edited", n, "%s: %s" % (t, ", ".join(what)), **ex)


def diff_entries(old_entries, new_entries):
    """Entry-level changes between two lists of entries (any files). Matching: same id, then fuzzy title within
    the same file, then (moves) fuzzy title/body across files."""
    items = []
    old_by = {e["id"]: e for e in old_entries}
    new_by = {e["id"]: e for e in new_entries}
    pairs = [(old_by[i], new_by[i]) for i in new_by if i in old_by]
    lo = [e for e in old_entries if e["id"] not in new_by]
    ln = [e for e in new_entries if e["id"] not in old_by]
    for f in sorted({e["file"] for e in lo} & {e["file"] for e in ln}):
        p, ro, rn = _pair([e for e in lo if e["file"] == f], [e for e in ln if e["file"] == f], 0.6)
        pairs += p
        lo = [e for e in lo if e["file"] != f] + ro
        ln = [e for e in ln if e["file"] != f] + rn
    p, lo, ln = _pair(lo, ln, 0.85)
    pairs += [(o, n) for o, n in p if o["file"] != n["file"]] + [(o, n) for o, n in p if o["file"] == n["file"]]
    for o, n in pairs:
        it = _compare(o, n)
        if it: items.append(it)
    for n in ln: items.append(_item("added", n, _added_summary(n), to=n["status"]))
    for o in lo: items.append(_item("removed", o, "%s: removed from %s" % (_short(o["title"]), o["file"]), **{"from": o["status"]}))
    order = {e["id"]: i for i, e in enumerate(new_entries)}
    items.sort(key=lambda it: (it["file"], order.get(it["entryId"], 1 << 30)))
    return items


def diff_document(name, old_text, new_text, title=None):
    """One 'edited' item for a document file (rules, README): how many lines changed."""
    a = (old_text or "").replace("\r\n", "\n").splitlines()
    b = (new_text or "").replace("\r\n", "\n").splitlines()
    if a == b: return []
    plus = minus = 0
    for line in difflib.unified_diff(a, b, lineterm="", n=0):
        if line.startswith("+") and not line.startswith("+++"): plus += 1
        elif line.startswith("-") and not line.startswith("---"): minus += 1
    t = title or name
    kind = "added" if not a else "removed" if not b else "edited"
    word = {"added": "added", "removed": "removed", "edited": "edited"}[kind]
    return [{"entryId": None, "file": name, "title": t, "kind": kind, "from": None, "to": None,
             "summary": "%s %s (+%d / -%d lines)" % (t, word, plus, minus),
             "what": "%s (+%d / -%d lines)" % (word, plus, minus), "section": None}]

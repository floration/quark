#!/usr/bin/env python3
"""Writes for the Claude Manager: q's edits to the PC's Claude Documents folder (--docs), never the mirror.

Every write runs under one lock: fetch + `rebase --autostash` onto the folder's upstream (what sync.cmd's
`git pull --rebase --autostash` does, split so that being offline still lets q save locally), apply the change,
`git add` + commit only the files it touched, `git push`. A save carries the text the page loaded (`base`); if the
file changed since, the edit is 3-way merged (`git merge-file`); a clean merge is saved, overlapping edits are not
(ConflictError, with both texts). A failed push keeps the local commit (pushed: False) and is retried on the next
write and every 5 minutes. Line endings (CRLF / LF) and a UTF-8 BOM are kept as the file has them.

Text helpers (decode / merge3 / set_answer_trailer / insert_entry / add_answers) are pure; Writer does the git.
Python 3 standard library only, Windows + Linux.
"""
import datetime, hashlib, os, re, tempfile, threading, time

import docsparse
from ghlinks import run, now_iso

NAME_RE = re.compile(r"^([0-9]{2}-[a-z0-9-]+|README)\.md$")
QUESTIONS = "07-things-to-ask-the-user.md"
ANSWERS = "09-answers-from-q.md"
ANSWERS_SECTION = "Answers"
ANSWERS_HEADER = """# Answers from q

q's answers to the questions in `07-things-to-ask-the-user.md`, newest first. The Claude Manager writes them
when q answers a question there, and sets that 07 entry to `answered` at the same time. Agents: read this file
with 07, act on every `open` answer for your thread, then set its status to `told` (passed on to the thread that
asked) or `done` (acted on). Entry shape and rules: README.md.

## Answers
"""
MAX_TEXT = 2 * 1024 * 1024
MAX_ANSWER = 4000
MAX_ANSWERS = 30
TRAILER_NOTE_LEN = 120
RETRY_EVERY = 300
VIA = "(q, via Claude Manager)"
FALLBACK_NAME, FALLBACK_EMAIL = "q (Claude Manager)", "claude-manager@localhost"
OFFLINE_MSG = "Saved on this PC; it'll go up next time sync works."


class EditError(Exception):
    """A write that did not happen: HTTP code, a machine word, a plain sentence for q, extra fields."""
    def __init__(self, code, error, message, **extra):
        super().__init__(message)
        self.code, self.error, self.message, self.extra = code, error, message, extra

    def payload(self):
        return dict(self.extra, ok=False, error=self.error, message=self.message)


class ConflictError(EditError):
    def __init__(self, message, **extra):
        super().__init__(409, "conflict", message, **extra)


# ---- time (Dallas) --------------------------------------------------------------------------------------------

def _central_offset(utc):
    """US Central offset for a UTC time without tzdata: CDT from the 2nd Sunday of March 2:00 CST to the 1st
    Sunday of November 2:00 CDT (zoneinfo needs the tzdata package on Windows)."""
    y = utc.year
    mar = datetime.datetime(y, 3, 8) + datetime.timedelta(days=(6 - datetime.datetime(y, 3, 8).weekday()) % 7)
    nov = datetime.datetime(y, 11, 1) + datetime.timedelta(days=(6 - datetime.datetime(y, 11, 1).weekday()) % 7)
    start = mar.replace(hour=8, tzinfo=datetime.timezone.utc)                  # 2:00 CST = 08:00 UTC
    end = nov.replace(hour=7, tzinfo=datetime.timezone.utc)                    # 2:00 CDT = 07:00 UTC
    return datetime.timedelta(hours=-5 if start <= utc < end else -6)


def dallas_now(now=None):
    utc = now or datetime.datetime.now(datetime.timezone.utc)
    try:
        from zoneinfo import ZoneInfo
        return utc.astimezone(ZoneInfo("America/Chicago"))
    except Exception:
        return utc.astimezone(datetime.timezone(_central_offset(utc)))


# ---- pure text helpers ----------------------------------------------------------------------------------------

def valid_name(name):
    return isinstance(name, str) and bool(NAME_RE.fullmatch(name))


def decode(raw):
    """File bytes -> (text with LF line ends, eol "\\r\\n" or "\\n", bom bytes). Raises EditError if not UTF-8."""
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    try:
        text = raw[len(bom):].decode("utf-8")
    except UnicodeDecodeError:
        raise EditError(415, "encoding", "That file isn't UTF-8 text, so the manager won't edit it.")
    crlf = text.count("\r\n")
    lf = text.count("\n") - crlf
    eol = "\r\n" if crlf > lf else "\n"
    return text.replace("\r\n", "\n"), eol, bom


def encode(text, eol, bom=b""):
    t = text.replace("\r\n", "\n")
    if eol == "\r\n": t = t.replace("\n", "\r\n")
    return bom + t.encode("utf-8")


def sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def git_err(err, default="git failed"):
    """The telling line of git's stderr: the first 'fatal:' / 'error:' line (without the prefix), else the last."""
    lines = [l.strip() for l in (err or "").strip().splitlines() if l.strip()]
    for l in lines:
        m = re.match(r"(?:fatal|error):\s*(.*)", l)
        if m: return m.group(1)
    return lines[-1] if lines else default


def one_line(s):
    return re.sub(r"\s+", " ", str(s or "")).strip()


def merge3(base, ours, theirs, labels=("your edit", "when you opened it", "the newest version")):
    """3-way merge of LF texts with `git merge-file`: (clean, merged text, number of conflicts)."""
    tmp = tempfile.mkdtemp(prefix="claude-manager-merge-")
    try:
        paths = []
        for n, t in (("ours", ours), ("base", base), ("theirs", theirs)):
            p = os.path.join(tmp, n)
            with open(p, "wb") as f: f.write(t.encode("utf-8"))
            paths.append(p)
        code, out, err = run(["git", "merge-file", "-p", "-L", labels[0], "-L", labels[1], "-L", labels[2]] + paths, timeout=60)
        if code < 0 or code > 127:
            raise EditError(500, "merge", "git couldn't compare the two versions (%s)." % (err.strip() or code))
        return code == 0, out.decode("utf-8", "replace").replace("\r\n", "\n"), code
    finally:
        for n in os.listdir(tmp): os.remove(os.path.join(tmp, n))
        os.rmdir(tmp)


def _trailer_note(answer):
    a = one_line(answer)
    if len(a) <= TRAILER_NOTE_LEN: return a
    cut = a[:TRAILER_NOTE_LEN]
    sp = cut.rfind(" ")
    return (cut[:sp] if sp > TRAILER_NOTE_LEN // 2 else cut).rstrip(" ,.;:") + "..."


def set_answer_trailer(lines, entry, answer, today, thread_names=()):
    """Turns the status in the trailer of one entry (its last line) into `answered: <answer> (q, via Claude
    Manager)`, in place; nothing else in `lines` changes. Adds a trailer if the entry has none."""
    i = entry["endLine"] - 1
    line = lines[i]
    body, tail = line.rstrip(), line[len(line.rstrip()):]
    status = "answered: %s %s" % (_trailer_note(answer), VIA)
    m = docsparse.find_trailer(body)                                       # (text may follow it: kept as it is)
    if m:
        thread = docsparse.parse_trailer(m.group(2), thread_names)[0]
        inner = (", " + thread if thread else "") + ", " + status
        lines[i] = body[:m.start(2)] + inner + "_" + body[m.end():] + tail
    else:
        lines[i] = body + " _%s, %s_" % (today, status) + tail
    return lines


def _section_bounds(lines, section):
    """(first content line index, end index) of a section; '' = text before the first ## heading."""
    heads = [(i, docsparse._HEAD_RE.match(l)) for i, l in enumerate(lines)]
    heads = [(i, m) for i, m in heads if m]
    title = next((i for i, m in heads if len(m.group(1)) == 1), None)        # the first H1 is the title, not a section
    start = None
    if section == "":
        start = title + 1 if title is not None else 0
    else:
        for i, m in heads:
            if i != title and m.group(2) == section:
                start = i + 1; break
    if start is None: return None
    end = next((i for i, m in heads if i >= start), len(lines))
    return start, end


def insert_entry(text, section, entry_text):
    """Puts one entry line at the top of a section's entries (newest first): just above its first entry, else
    after the section's text. -> (new text, 1-based line). None if the section isn't there."""
    lines = text.split("\n")
    b = _section_bounds(lines, section)
    if b is None: return None
    start, end = b
    first = next((i for i in range(start, end) if re.match(r"^\s{0,1}[-*]\s", lines[i])), None)
    if first is not None:
        lines[first:first] = [entry_text]
        return "\n".join(lines), first + 1
    last = max([i for i in range(start, end) if lines[i].strip()] or [start - 1])
    new = ([""] if last >= start or start > 0 else []) + [entry_text]
    if last + 1 >= len(lines) or lines[last + 1].strip() or last + 1 == end: new.append("")
    at = last + 1
    lines[at:at] = new
    if at + len(new) == len(lines) and lines[-1] != "": lines.append("")
    return "\n".join(lines), at + new.index(entry_text) + 1


def format_entry(title, priority, body, status, date, thread="q"):
    t = one_line(title).rstrip()
    b = one_line(body)
    if priority:
        head = "**%s** (%s)." % (t.rstrip(" .:"), priority)
    else:
        head = "**%s**" % (t if re.search(r"[.:?!]$", t) else t + ".")
    if b and not re.search(r"[.!?)`\"']$", b): b += "."
    return "- %s%s _%s, %s, %s_" % (head, " " + b if b else "", date, thread, status)


def answer_entry(e, answer, stamp):
    a = one_line(answer)
    if not re.search(r"[.!?)`\"']$", a): a += "."
    src = ("from %s" % e["thread"]) if e.get("thread") else ""
    if e["file"] != QUESTIONS or not src: src = (src + " in " if src else "in ") + e["file"]
    asked = (", asked %s" % e["date"]) if e.get("date") else ""
    return "- **%s** %s Question %s%s. _%s, q, open_" % (one_line(e["title"]), a, src, asked, stamp)


def add_answers(text, entries_lines):
    """09's text (or None: create it) with the answer lines at the top of its Answers section, in order."""
    if text is None or not text.strip(): text = ANSWERS_HEADER
    if _section_bounds(text.split("\n"), ANSWERS_SECTION) is None:
        text = text.rstrip("\n") + "\n\n## " + ANSWERS_SECTION + "\n\n"
    for line in reversed(entries_lines):
        text = insert_entry(text, ANSWERS_SECTION, line)[0]
    return text


# ---- the writer ----------------------------------------------------------------------------------------------

class Writer:
    def __init__(self, docs, read_only=False, thread_names=(), log=None, on_change=None):
        self.docs = os.path.abspath(docs) if docs else None
        self.read_only = read_only
        self.thread_names = list(thread_names)
        self.log = log or (lambda m: None)
        self.on_change = on_change
        self.lock = threading.Lock()
        self.last_save = self.last_push = self.push_error = None
        self._editable = None                                              # (time, ok, reason)
        self._stop = threading.Event()

    # -- git
    def git(self, *args, timeout=120):
        return run(["git", "-C", self.docs] + list(args), timeout=timeout)

    def _out(self, *args):
        code, out, _ = self.git(*args, timeout=30)
        return out.decode("utf-8", "replace").strip() if code == 0 else None

    def editable(self, fresh=False):
        """(ok, reason): writes are possible at all (not read-only, the folder is a git clone with an upstream)."""
        if self.read_only: return False, "The Claude Manager was started read-only (--read-only)."
        if not self.docs or not os.path.isdir(self.docs): return False, "The Claude Documents folder wasn't found."
        c = self._editable
        if c and not fresh and time.time() - c[0] < 30: return c[1], c[2]
        top = self._out("rev-parse", "--show-toplevel")
        if not top or os.path.normcase(os.path.realpath(top)) != os.path.normcase(os.path.realpath(self.docs)):
            ok, why = False, "The Claude Documents folder isn't a git copy of the repo, so edits can't be sent anywhere."
        elif not self._out("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"):
            ok, why = False, "The Claude Documents folder's branch isn't linked to GitHub (no upstream). Run sync.cmd once or ask a Claude to fix it."
        else:
            ok, why = True, None
        self._editable = (time.time(), ok, why)
        return ok, why

    def _in_rebase(self):
        for p in ("rebase-merge", "rebase-apply"):
            gp = self._out("rev-parse", "--git-path", p)
            if gp and os.path.exists(gp if os.path.isabs(gp) else os.path.join(self.docs, gp)): return True
        return False

    def _busy_reason(self):
        for p in ("rebase-merge", "rebase-apply", "MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD"):
            gp = self._out("rev-parse", "--git-path", p)
            if gp and os.path.exists(gp if os.path.isabs(gp) else os.path.join(self.docs, gp)):
                return ("git is in the middle of a %s in the Claude Documents folder (maybe from sync.cmd), so the manager "
                        "won't write there. Run sync.cmd again or ask a Claude to finish it." % ("merge" if "MERGE" in p else "rebase"))
        if self._out("ls-files", "-u"):
            return ("Some files in the Claude Documents folder have unresolved git conflicts, so the manager won't write there. "
                    "Ask a Claude to fix the folder (or run sync.cmd and follow what it says).")
        return None

    def _require(self):
        ok, why = self.editable()
        if not ok: raise EditError(403 if self.read_only else 423, "read-only" if self.read_only else "not-editable", why)
        busy = self._busy_reason()
        if busy: raise EditError(423, "busy", busy)

    def _sync(self, fetch_timeout=90):
        """Fetch + rebase onto upstream with autostash. Returns True if GitHub was reached. Raises on a conflict."""
        code, _, err = self.git("fetch", "-q", timeout=fetch_timeout)
        if code != 0:
            self.log("fetch failed: %s" % git_err(err, "?"))
            return False
        code, _, err = self.git(*(self._identity() + ["rebase", "--autostash", "-q"]), timeout=120)   # (rebase commits too)
        if self._in_rebase():
            self.git(*(self._identity() + ["rebase", "--abort"]), timeout=60)
            raise EditError(423, "pull-failed",
                            "Couldn't bring the PC copy up to date: the edits waiting on this PC and the newest Claude Documents "
                            "on GitHub change the same lines, so git can't put them together. Nothing was saved. Run sync.cmd "
                            "in the Claude Documents folder to see it, or ask a Claude to fix the folder.",
                            detail=git_err(err, ""))
        busy = self._busy_reason()
        if busy: raise EditError(423, "busy", busy)
        if code != 0:
            raise EditError(423, "pull-failed", "Couldn't bring the PC copy up to date with GitHub, so nothing was saved: %s"
                            % git_err(err, "git rebase failed"))
        return True

    def unpushed(self):
        n = self._out("rev-list", "--count", "@{u}..HEAD")
        try: return int(n)
        except (TypeError, ValueError): return 0

    def _push(self):
        """(pushed, message). One retry after a rejected push (someone pushed in between): rebase, push again."""
        for attempt in (0, 1):
            code, _, err = self.git("push", "-q", timeout=120)
            if code == 0:
                self.last_push, self.push_error = now_iso(), None
                return True, None
            low = err.lower()
            rejected = "rejected" in low or "fetch first" in low or "non-fast-forward" in low
            if attempt == 0 and rejected:
                try:
                    if self._sync(): continue
                except EditError as e:
                    self.push_error = e.message
                    return False, e.message
            break
        last = git_err(err, "push failed")
        self.push_error = ("GitHub refused the push: " if rejected else "Couldn't reach GitHub: ") + last
        self.log("push failed: %s" % last)
        return False, self.push_error

    def _identity(self):
        if self._out("config", "user.name") and self._out("config", "user.email"): return []
        return ["-c", "user.name=" + FALLBACK_NAME, "-c", "user.email=" + FALLBACK_EMAIL]

    def _commit(self, files, message):
        code, _, err = self.git("add", "--", *files, timeout=60)
        if code != 0: raise EditError(500, "git-add", "Wrote the file, but git couldn't add it: %s" % err.strip())
        if self.git("diff", "--cached", "--quiet", "--", *files, timeout=30)[0] == 0: return False
        code, _, err = self.git(*(self._identity() + ["commit", "-q", "-m", message, "--", *files]), timeout=60)
        if code != 0:
            raise EditError(500, "git-commit", "Wrote the file on this PC, but git couldn't commit it (sync.cmd will send it "
                                               "later): %s" % git_err(err, "?"))
        return True

    # -- files
    def path(self, name):
        if not valid_name(name): raise EditError(400, "bad-name", "That isn't one of the Claude Documents files.")
        return os.path.join(self.docs, name)

    def _read(self, name):
        """(text LF, eol, bom) or None if the file doesn't exist."""
        p = self.path(name)
        if not os.path.isfile(p): return None
        with open(p, "rb") as f: return decode(f.read())

    def _eol_default(self):
        got = self._read(QUESTIONS)
        return got[1] if got else ("\r\n" if os.name == "nt" else "\n")

    def _write(self, name, text, eol, bom):
        p = self.path(name)
        tmp = os.path.join(self.docs, ".%s.claude-manager.tmp" % name)
        try:
            with open(tmp, "wb") as f: f.write(encode(text, eol, bom))
            os.replace(tmp, p)
        except OSError as e:
            try: os.remove(tmp)
            except OSError: pass
            raise EditError(500, "write", "Couldn't write %s (is it open in another program?): %s" % (name, e))

    def read(self, name, sync=False):
        """{file, exists, text, sha256, eol, sections, synced} for the page's editor."""
        self.path(name)
        synced = None
        if sync and self.editable()[0]:
            with self.lock:
                try:
                    if not self._busy_reason(): synced = self._sync(fetch_timeout=20)
                except EditError as e:
                    synced = False
                    self.log(e.message)
        got = self._read(name)
        text, eol = (got[0], got[1]) if got else ("", self._eol_default())
        p = docsparse.parse_file(name, text, self.thread_names)
        return {"ok": True, "file": name, "exists": got is not None, "text": text, "sha256": sha256(text),
                "eol": "crlf" if eol == "\r\n" else "lf", "bom": bool(got and got[2]), "sections": p["sections"],
                "kind": p["kind"], "synced": synced}

    # -- the three writes
    def _finish(self, files, message, result):
        committed = self._commit(files, message)
        result["committed"] = committed
        pushed, why = self._push() if (committed or self.unpushed()) else (True, None)
        self.last_save = now_iso()
        result.update(pushed=pushed, savedAt=self.last_save, unpushed=self.unpushed(),
                      message=("Saved and pushed." if pushed else OFFLINE_MSG) + ("" if pushed else " (%s)" % why))
        if not pushed: result["pushError"] = why
        if self.on_change:
            try: self.on_change()
            except Exception: pass
        return result

    def save(self, name, base, text):
        if not isinstance(text, str) or (base is not None and not isinstance(base, str)):
            raise EditError(400, "bad-request", "The page sent a save without the text.")
        if len(text) > MAX_TEXT: raise EditError(413, "too-big", "That file is too big to save from here.")
        text, base = text.replace("\r\n", "\n"), (base or "").replace("\r\n", "\n")
        self.path(name)
        with self.lock:
            self._require()
            online = self._sync()
            got = self._read(name)
            cur, eol, bom = got if got else ("", self._eol_default(), b"")
            merged = False
            if cur == text:
                new = cur
            elif cur == base:
                new = text
            else:
                clean, m, n = merge3(base, text, cur)
                if not clean:
                    raise ConflictError("Someone else changed the same lines of %s since you opened it, so it wasn't saved. "
                                        "Both versions are below: keep what you want and save again." % name,
                                        file=name, current=cur, sha256=sha256(cur), conflicted=m, conflicts=n, mine=text)
                new, merged = m, True
            res = {"ok": True, "file": name, "text": new, "sha256": sha256(new), "merged": merged, "online": online,
                   "changed": new != cur}
            if new != cur: self._write(name, new, eol, bom)
            elif not self.unpushed():
                res.update(pushed=True, committed=False, message="Nothing to save: the file already says that.",
                           savedAt=now_iso(), unpushed=0)
                return res
            return self._finish([name], "q: edited %s (Claude Manager)" % name, res)

    def _find(self, parsed, entry_id, title=None):
        es = parsed["entries"]
        e = next((x for x in es if x["id"] == entry_id), None)
        if e is None and title: e = next((x for x in es if x["title"] == title), None)
        if e is None:
            base = entry_id.split("#")[0]
            cands = [x for x in es if x["id"].split("#")[0] == base]
            if len(cands) == 1: e = cands[0]
        return e

    def answer(self, answers):
        if not isinstance(answers, list) or not answers:
            raise EditError(400, "bad-request", "No answer was sent.")
        if len(answers) > MAX_ANSWERS: raise EditError(400, "bad-request", "Too many answers at once (%d at most)." % MAX_ANSWERS)
        clean = []
        for a in answers:
            if not isinstance(a, dict) or not isinstance(a.get("entryId"), str) or not isinstance(a.get("text"), str):
                raise EditError(400, "bad-request", "An answer is missing its question or its text.")
            t = a["text"].strip()
            if not t: raise EditError(400, "empty", "One of the answers is empty.")
            if len(t) > MAX_ANSWER: raise EditError(400, "too-long", "An answer is too long (%d characters at most)." % MAX_ANSWER)
            name = a["entryId"].split(":", 1)[0]
            if not valid_name(name) or docsparse.file_kind(name) != "entries" or name == ANSWERS:
                raise EditError(400, "bad-name", "That answer doesn't point at a question in Claude Documents.")
            clean.append((name, a["entryId"], t, a.get("title") if isinstance(a.get("title"), str) else None))
        with self.lock:
            self._require()
            online = self._sync()
            now = dallas_now()
            today, stamp = now.strftime("%Y-%m-%d"), now.strftime("%Y-%m-%d %H:%M") + " CT"
            texts, parsed = {}, {}
            lines_09, done = [], []
            for name, eid, t, title in clean:
                if name not in texts:
                    got = self._read(name)
                    if got is None: raise EditError(404, "missing", "%s isn't in the Claude Documents folder any more." % name)
                    texts[name] = [got[0].split("\n"), got[1], got[2], got[0]]
                    parsed[name] = docsparse.parse_file(name, got[0], self.thread_names)
                e = self._find(parsed[name], eid, title)
                if e is None:
                    raise EditError(404, "gone", "Couldn't find the question “%s” in %s any more (someone may have "
                                                 "moved or renamed it). Nothing was saved; reload and try again." % (title or eid, name),
                                    entryId=eid)
                if name == QUESTIONS and e["status"] in docsparse.DONE_STATUSES:
                    raise EditError(409, "already-answered", "“%s” is already marked %s (%s). Nothing was saved."
                                    % (docsparse._short(e["title"]), e["status"], e["statusNote"] or e["statusText"]), entryId=eid)
                if name == QUESTIONS: set_answer_trailer(texts[name][0], e, t, today, self.thread_names)
                lines_09.append(answer_entry(e, t, stamp))
                done.append({"entryId": e["id"], "title": e["title"], "file": name})
            got = self._read(ANSWERS)
            t09, eol09, bom09 = got if got else (None, self._eol_default(), b"")
            new09 = add_answers(t09, lines_09)
            files = []
            for name, (lines, eol, bom, old) in texts.items():
                new = "\n".join(lines)
                if new != old: self._write(name, new, eol, bom); files.append(name)
            self._write(ANSWERS, new09, eol09, bom09)
            files.insert(0, ANSWERS)
            what = ('"%s"' % docsparse._short(one_line(done[0]["title"]))[:80]) if len(done) == 1 else "%d questions" % len(done)
            prefix = "09/07" if QUESTIONS in files else "09"
            res = {"ok": True, "answered": done, "files": files, "online": online}
            return self._finish(files, "%s: q answered %s (Claude Manager)" % (prefix, what), res)

    def add_entry(self, f):
        if not isinstance(f, dict): raise EditError(400, "bad-request", "The page sent an empty entry.")
        name, section = f.get("file"), f.get("section", "")
        title, body = one_line(f.get("title")), str(f.get("body") or "")
        prio, status = docsparse.norm_priority(f.get("priority") or ""), f.get("status") or "open"
        if not valid_name(name) or docsparse.file_kind(name) != "entries":
            raise EditError(400, "bad-name", "Entries go in files 01 to 09.")
        if not isinstance(section, str): raise EditError(400, "bad-request", "Pick a section.")
        if not title: raise EditError(400, "empty", "The entry needs a title.")
        if len(title) > 300 or len(body) > 20000: raise EditError(400, "too-long", "That entry is too long.")
        if prio not in [""] + docsparse.PRIORITIES: raise EditError(400, "bad-request", "Priority is HALT, P1, P2, P3 or none.")
        if status not in docsparse.STATUSES: raise EditError(400, "bad-request", "Status is one of: " + ", ".join(docsparse.STATUSES))
        with self.lock:
            self._require()
            online = self._sync()
            got = self._read(name)
            if got is None: raise EditError(404, "missing", "%s isn't in the Claude Documents folder." % name)
            text, eol, bom = got
            today = dallas_now().strftime("%Y-%m-%d")
            line = format_entry(title, prio, body, status, today)
            r = insert_entry(text, section, line)
            if r is None:
                raise EditError(409, "no-section", "The section “%s” isn't in %s any more. Pick another one." % (section, name))
            new, at = r
            self._write(name, new, eol, bom)
            res = {"ok": True, "file": name, "line": at, "entry": line, "text": new, "sha256": sha256(new), "online": online}
            return self._finish([name], '%s: q added "%s" (Claude Manager)' % (name[:2], docsparse._short(title)[:80]), res)

    # -- status + background push retry
    def status(self):
        ok, why = self.editable()
        st = {"canEdit": ok, "reason": why, "readOnly": self.read_only, "lastSave": self.last_save, "lastPush": self.last_push,
              "pushError": self.push_error, "unpushed": 0, "busy": None}
        if ok:
            st["unpushed"] = self.unpushed()
            st["busy"] = self._busy_reason()
        return st

    def retry_push(self):
        """Push commits left by an offline save. Returns True when nothing is left to push."""
        if not self.editable()[0]: return True
        with self.lock:
            if not self.unpushed(): return True
            if self._busy_reason(): return False
            try:
                if not self._sync():
                    self.push_error = "Couldn't reach GitHub (will try again)."
                    return False
            except EditError as e:
                self.push_error = e.message
                return False
            ok, _ = self._push()
        if ok and self.on_change:
            try: self.on_change()
            except Exception: pass
        return ok

    def start_retry(self, every=RETRY_EVERY):
        def loop():
            while not self._stop.wait(every):
                try:
                    if self.unpushed() and self.retry_push(): self.log("pushed the edits saved while offline")
                except Exception as e:
                    self.log("push retry failed: %s" % e)
        t = threading.Thread(target=loop, daemon=True, name="claude-manager-push-retry")
        t.start()
        return t

    def stop(self):
        self._stop.set()

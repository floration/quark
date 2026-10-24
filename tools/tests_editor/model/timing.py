"""Group timings and known flakes from the release-tests notes (cloud: /mnt/project-files/release-tests/).

The timing notes are written by hand, in a few shapes: a table (`  bots  256  64` under a `game-s ~real-s` header:
the last column is real seconds), runs (`  bots  9.27  9.35  9.20 s`: the mean), lists (`rules 42, maps 34, ...`) and
single lines (`  netcode   6.4 s`, `  tickrate ~116 s   (alone re-run: 96 s)`: the first number). The newest stepN.txt
wins per group, then baseline.txt. Missing folder or files: everything is None.
"""
import os, re

_NUM = r"~?(\d+(?:\.\d+)?)"


def _file_order(dirpath):
    """Timing notes, best first: stepN.txt by N (newest first), then baseline.txt."""
    try:
        names = os.listdir(dirpath)
    except OSError:
        return []
    steps = []
    for n in names:
        m = re.match(r"^step(\d+)\.txt$", n)
        if m:
            p = os.path.join(dirpath, n)
            steps.append((int(m.group(1)), os.path.getmtime(p), n))
    out = [n for _, _, n in sorted(steps, reverse=True)]
    if "baseline.txt" in names: out.append("baseline.txt")
    return out


def parse_note(text, groups):
    """{group: seconds} from one timing note."""
    out, real_col = {}, False
    gset = set(groups)
    for line in text.split("\n"):
        if re.search(r"game-s\s+~?real-s", line): real_col = True; continue
        if not line.strip() or line.lstrip().startswith(("Raw log", "NOTE")): continue
        # table row: name game real
        m = re.match(r"^\s+([a-z][\w-]*)\s+(\d+)\s+(\d+)\s*$", line)
        if m and m.group(1) in gset and real_col:
            out.setdefault(m.group(1), float(m.group(3))); continue
        # runs: name a b c s
        m = re.match(r"^\s+([a-z][\w-]*)\s+((?:\d+(?:\.\d+)?\s+){1,9})s\b", line)
        if m and m.group(1) in gset:
            vals = [float(x) for x in m.group(2).split()]
            out.setdefault(m.group(1), round(sum(vals) / len(vals), 2)); continue
        # list: name N, name N, ...
        if line.count(",") >= 2:
            for name, val in re.findall(r"(?:^|,)\s*([a-z][\w-]*)\s+" + _NUM + r"(?=\s*(?:,|s\b|$))", line):
                if name in gset: out.setdefault(name, float(val))
            continue
        m = re.match(r"^\s+([a-z][\w-]*)\s+" + _NUM + r"\s*s\b", line)
        if m and m.group(1) in gset: out.setdefault(m.group(1), float(m.group(2)))
    return out


def timings(dirpath, groups):
    """{group: {sec, source, at}} from the notes in dirpath (folder name kept in `source`)."""
    out = {}
    if not dirpath or not os.path.isdir(dirpath): return out
    base = os.path.basename(os.path.normpath(dirpath))
    parent = os.path.basename(os.path.dirname(os.path.normpath(dirpath)))
    for n in _file_order(dirpath):
        p = os.path.join(dirpath, n)
        try:
            with open(p, encoding="utf-8", errors="replace") as f: text = f.read()
            mt = os.path.getmtime(p)
        except OSError:
            continue
        for g, sec in parse_note(text, groups).items():
            if g not in out: out[g] = {"sec": sec, "source": "%s/%s/%s" % (parent, base, n), "mtime": mt}
    return out


def flakes(overview_path):
    """{group: text} and {test function short name: text} from section 5 ("Known flakes") of overview.md."""
    groups, fns = {}, {}
    try:
        with open(overview_path, encoding="utf-8", errors="replace") as f: text = f.read()
    except (OSError, TypeError):
        return groups, fns
    m = re.search(r"^##\s*5\b[^\n]*\n(.*?)(?=^##\s|\Z)", text, re.S | re.M)
    if not m: return groups, fns
    for row in m.group(1).split("\n"):
        cells = [c.strip() for c in row.strip().strip("|").split("|")] if row.strip().startswith("|") else []
        if len(cells) < 2 or set(cells[0]) <= set("-: ") or cells[0].lower() == "test": continue
        head = cells[0].replace("**", "")
        gm = re.match(r"^([a-z][\w-]*)\s*:\s*(.*)$", head)
        if not gm: continue
        g, rest = gm.group(1), gm.group(2)
        what = "; ".join(x.replace("**", "") for x in cells[1:] if x)
        txt = "%s: %s" % (rest, what) if rest else what
        groups[g] = (groups[g] + " | " + txt) if g in groups else txt
        for name in re.findall(r"\b([A-Z][A-Za-z0-9]+)\b", re.sub(r"\(.*?\)", "", rest)):
            fns[name] = txt
    return groups, fns

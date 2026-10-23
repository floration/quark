#!/usr/bin/env python3
"""The website's update pages, made from the changelogs (run by the release workflow before the site is published):

  site/updates/index.html            every version, newest first, with its short changelog (changelogs/<V>.md)
  site/updates/<V>/index.html        one version's full changelog: changelogs/full/<V>.md, what changed and why, in
                                     depth (every version from 1.17.0 on has one; older ones show their short notes,
                                     which were all there was)

Static pages, no scripts. The markdown is the small set the changelogs use (headings, nested lists, tables, paragraphs,
**bold**, *italic*, `code`, [links](https://...)); everything is escaped first, and only http(s) and relative links
become links. Standard library only. Usage: site_updates.py [repo root] [--allow-missing-full]"""
import html, os, re, shutil, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
root = args[0] if args else "."
ALLOW_MISSING_FULL = "--allow-missing-full" in sys.argv   # (CI on work in progress: a warning, not a failure)
CHANGELOGS = os.path.join(root, "changelogs")
OUT = os.path.join(root, "site", "updates")
FULL_SINCE = (1, 17, 0)   # versions from here on must have a full changelog


def inline(text):
    s = html.escape(text, quote=True)
    codes = []
    def keep(m):   # code spans first: nothing inside them is formatted
        codes.append("<code>" + m.group(1) + "</code>")
        return "\x00%d\x00" % (len(codes) - 1)
    s = re.sub(r"`([^`]+)`", keep, s)
    def link(m):
        label, url = m.group(1), html.unescape(m.group(2))
        if not re.match(r"^(https?://|/|\.\.?/|[A-Za-z0-9_.-]+(/|$|#))", url) or url.lower().startswith("javascript"):
            return m.group(0)
        return '<a href="%s">%s</a>' % (html.escape(url, quote=True), label)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<i>\1</i>", s)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], s)


def md(text, skip_title=True):
    out, para = [], []
    stack = []   # open lists: (indent, "ul" | "ol")
    lines = text.replace("\r\n", "\n").split("\n")
    def flush_para():
        if para: out.append("<p>" + inline(" ".join(para)) + "</p>"); para.clear()
    def close_lists(to=-1):
        while stack and stack[-1][0] > to:
            out.append("</li></%s>" % stack.pop()[1])
    i = 0
    while i < len(lines):
        raw = lines[i]
        line = raw.strip()
        if not line:
            flush_para(); i += 1; continue
        if line.startswith("|") and line.endswith("|"):
            flush_para(); close_lists()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|") and lines[i].strip().endswith("|"):
                rows.append(lines[i].strip()); i += 1
            cells = lambda r: [c.strip() for c in r[1:-1].split("|")]
            out.append('<div class="scroll"><table><tr>' + "".join("<th>%s</th>" % inline(c) for c in cells(rows[0])) + "</tr>")
            for r in rows[1:]:
                if re.fullmatch(r"\|[\s:|-]+\|", r): continue
                out.append("<tr>" + "".join("<td>%s</td>" % inline(c) for c in cells(r)) + "</tr>")
            out.append("</table></div>")
            continue
        m = re.match(r"^(\s*)([-*]|\d+[.)])\s+(.*)$", raw)
        if m:
            flush_para()
            indent, kind, body = len(m.group(1).replace("\t", "    ")), "ol" if m.group(2)[0].isdigit() else "ul", m.group(3)
            if stack and indent > stack[-1][0]:
                out.append("<%s>" % kind); stack.append((indent, kind))
            else:
                close_lists(indent)
                if stack and stack[-1][0] == indent and stack[-1][1] == kind: out.append("</li>")
                else:
                    if stack and stack[-1][0] == indent: out.append("</li></%s>" % stack.pop()[1])
                    out.append("<%s>" % kind); stack.append((indent, kind))
            out.append("<li>" + inline(body))
            i += 1
            continue
        if stack and raw[:1] in (" ", "\t"):   # a list item's continuation line
            out.append(" " + inline(line)); i += 1; continue
        close_lists()
        h = re.match(r"^(#{1,6})\s+(.*)$", line)
        if h:
            flush_para()
            level = len(h.group(1))
            if level == 1 and skip_title: i += 1; continue
            out.append("<h%d>%s</h%d>" % (min(level + 1, 6), inline(h.group(2)), min(level + 1, 6)))
        else:
            para.append(line)
        i += 1
    flush_para(); close_lists()
    return "\n".join(out)


STYLE = """
:root { --bg: #15161a; --box: #1d1f24; --line: #34373e; --text: #dcdcd6; --dim: #8b8e94; --cyan: #3fd8ff; --red: #ff4c3f; --yellow: #ffd23f; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.6 "IBM Plex Mono", ui-monospace, monospace; }
a { color: var(--cyan); }
a:hover { background: var(--cyan); color: var(--bg); text-decoration: none; }
.wrap { max-width: 860px; margin: 0 auto; padding: 0 16px 40px; }
header { padding: 34px 0 6px; }
h1 { font: 48px/1 Silkscreen, monospace; color: var(--cyan); margin: 0; letter-spacing: 2px; text-shadow: 4px 4px 0 #0c5d70; }
h1 a, h1 a:hover { color: inherit; background: none; text-decoration: none; }
nav { margin: 14px 0 22px; font-size: 14px; }
nav a { margin-right: 14px; }
h2 { font: 22px Silkscreen, monospace; color: var(--yellow); margin: 36px 0 8px; }
h2 a { color: var(--yellow); text-decoration: none; }
h3, h4, h5, h6 { color: var(--yellow); font-weight: 600; margin: 18px 0 4px; }
p { margin: 8px 0; }
.dim { color: var(--dim); }
ul, ol { margin: 6px 0; padding-left: 22px; }
li { margin: 3px 0; }
li::marker { color: var(--red); }
table { border-collapse: collapse; width: 100%; font-size: 14px; margin: 8px 0; }
th { text-align: left; color: var(--dim); font-weight: 400; border-bottom: 2px solid var(--line); padding: 4px 8px; }
td { border-bottom: 1px dotted var(--line); padding: 4px 8px; vertical-align: top; }
.scroll { overflow-x: auto; }
code { background: var(--box); padding: 0 4px; overflow-wrap: anywhere; }
.entry { border-bottom: 1px dotted var(--line); padding-bottom: 10px; }
.entry .body { color: #c3c3bd; font-size: 14px; }
.full { color: var(--dim); font-size: 13px; }
"""


def page(title, body, depth):
    up = "../" * depth
    return ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>%s</title>\n<link rel=\"icon\" type=\"image/png\" href=\"%sicon.png\">\n"
            "<link href=\"https://fonts.googleapis.com/css2?family=Silkscreen&family=IBM+Plex+Mono:wght@400;600&display=swap\" rel=\"stylesheet\">\n"
            "<style>%s</style>\n</head>\n<body><div class=\"wrap\">\n"
            "<header><h1><a href=\"%s\">quark</a></h1></header>\n"
            "<nav><a href=\"%s\">home</a><a href=\"%supdates/\">all updates</a></nav>\n%s\n</div></body>\n</html>\n"
            % (html.escape(title), up, STYLE, up, up, up, body))


def main():
    versions = []
    for name in os.listdir(CHANGELOGS):
        m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\.md", name)
        if m: versions.append(tuple(map(int, m.groups())))
    versions.sort(reverse=True)
    missing = []
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    os.makedirs(OUT)
    entries = []
    for v in versions:
        vs = "%d.%d.%d" % v
        short = open(os.path.join(CHANGELOGS, vs + ".md"), encoding="utf-8").read()
        full_path = os.path.join(CHANGELOGS, "full", vs + ".md")
        has_full = os.path.isfile(full_path)
        if v >= FULL_SINCE and not has_full: missing.append(vs)
        full = open(full_path, encoding="utf-8").read() if has_full else short
        d = os.path.join(OUT, vs)
        os.makedirs(d)
        note = "" if has_full else "<p class=\"dim\">The full notes for this version are its short notes: that was all that was written then.</p>\n"
        with open(os.path.join(d, "index.html"), "w", encoding="utf-8") as f:
            f.write(page("quark %s: everything that changed" % vs,
                         "<h2>v%s</h2>\n%s%s" % (html.escape(vs), note, md(full)), 2))
        entries.append("<div class=\"entry\"><h2><a href=\"%s/\">v%s</a></h2>\n<div class=\"body\">%s</div>\n"
                       "<p class=\"full\"><a href=\"%s/\">everything that changed in %s, and why &rarr;</a></p></div>"
                       % (vs, vs, md(short), vs, vs))
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(page("quark: every update",
                     "<p class=\"dim\">Every version, newest first. Each one's page has the full changelog.</p>\n" + "\n".join(entries), 1))
    print("site/updates: %d versions" % len(versions))
    if missing and ALLOW_MISSING_FULL:
        print("warning: changelogs/full/<version>.md is missing for: " + ", ".join(missing))
    elif missing:
        sys.exit("changelogs/full/<version>.md is missing for: " + ", ".join(missing) + " (every version from %d.%d.%d on needs one)" % FULL_SINCE)


if __name__ == "__main__":
    main()

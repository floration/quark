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
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)   # (notes for whoever writes the changelog: never on the page)
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


# The pages share the site's stylesheet (site/style.css: header, footer, the .prose long-form typography); this is
# what only the update pages need on top of it.
STYLE = """
.toc { display: flex; flex-wrap: wrap; gap: 6px; margin: 22px 0 0; padding: 0; list-style: none; }
.toc a { display: inline-block; padding: 4px 10px; border: 1px solid var(--line); border-radius: 999px; font-size: 14px; font-weight: 600; text-decoration: none; color: var(--muted); }
.toc a:hover { color: var(--text); border-color: var(--cyan); }
.entry { padding: 34px 0; border-bottom: 1px solid var(--line); display: grid; grid-template-columns: 180px minmax(0, 1fr); gap: 8px 32px; }
.entry:first-child { padding-top: 0; }
.entry > h2 { margin: 0; font-size: 26px; font-weight: 800; }
.entry > h2 a { color: var(--text); text-decoration: none; }
.entry > h2 a:hover { color: var(--cyan); }
.entry .latest { display: table; margin-top: 10px; }
.page .note { max-width: 760px; margin: 0 0 24px; }
.entry .prose h3 { font-size: 18px; margin: 22px 0 8px; }
.entry .prose h4 { font-size: 16px; }
.full { margin: 6px 0 0; font-weight: 700; }
.full a { text-decoration: none; }
.full a:hover { text-decoration: underline; }
.crumbs { margin: 0 0 14px; font-size: 15px; font-weight: 600; }
.crumbs a { text-decoration: none; }
.pager { display: flex; justify-content: space-between; gap: 16px; flex-wrap: wrap; margin-top: 56px; padding-top: 24px; border-top: 1px solid var(--line); font-weight: 700; }
.pager a { text-decoration: none; }
@media (max-width: 760px) { .entry { grid-template-columns: minmax(0, 1fr); } }
"""

FONTS = ("<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
         "<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>\n"
         "<link href=\"https://fonts.googleapis.com/css2?family=Work+Sans:wght@400..800&display=swap\" rel=\"stylesheet\">\n"
         "<link href=\"https://fonts.googleapis.com/css2?family=Rubik:wght@800&text=quark&display=swap\" rel=\"stylesheet\">\n")


def page(title, body, depth, head="", lede=""):
    """A page in the site's design: the same header, nav and footer as site/index.html (relative links, so they work
    at /updates/ and /updates/<V>/). `head` and `lede` are the title block (already HTML); `body` the content."""
    up = "../" * depth
    nav = "".join("<li><a href=\"%s\"%s>%s</a></li>" % (href, ' aria-current="page"' if label == "Updates" else "", label) for label, href in (
        ("Modes", up + "#modes"), ("Maps", up + "#maps"), ("Weapons", up + "#weapons"), ("Leaderboard", up + "leaderboard/"),
        ("Updates", up + "updates/"), ("Testing", up + "testing/"), ("Discord", "https://discord.gg/RERYWNWSXg"),
        ("GitHub", "https://github.com/floration/quark")))
    return ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>%s</title>\n<meta name=\"theme-color\" content=\"#0c0f14\">\n"
            "<link rel=\"icon\" type=\"image/png\" href=\"%sicon.png\">\n%s"
            "<link rel=\"stylesheet\" href=\"%sstyle.css\">\n<style>%s</style>\n</head>\n<body>\n"
            "<a class=\"skip\" href=\"#main\">Skip to content</a>\n"
            "<header class=\"top\"><div class=\"wrap\">\n"
            "<a class=\"logo\" href=\"%s\" aria-label=\"quark home\"><img src=\"%sicon.png\" alt=\"\" width=\"32\" height=\"32\"><span>quark</span></a>\n"
            "<nav aria-label=\"Main\"><ul class=\"nav\">%s</ul></nav>\n"
            "<a class=\"btn btn-primary btn-sm cta\" href=\"%s#download\">Download</a>\n</div></header>\n"
            "<main id=\"main\">\n<div class=\"page-head\"><div class=\"wrap\">\n%s%s</div></div>\n"
            "<div class=\"page\"><div class=\"wrap\">\n%s\n</div></div>\n</main>\n"
            "<footer class=\"foot\"><div class=\"wrap\">\n"
            "<div><p>Free and open source. No ads, no tracking, no cookies.</p>"
            "<p class=\"small\">UI font: Work Sans. Logo: Rubik. Both SIL Open Font License 1.1.</p></div>\n"
            "<ul aria-label=\"Links\"><li><a href=\"%s\">Home</a></li><li><a href=\"%sleaderboard/\">Leaderboard</a></li>"
            "<li><a href=\"%supdates/\">Updates</a></li><li><a href=\"%stesting/\">Testing</a></li>"
            "<li><a href=\"https://github.com/floration/quark/releases\">Releases</a></li>"
            "<li><a href=\"https://discord.gg/RERYWNWSXg\">Discord</a></li><li><a href=\"https://github.com/floration/quark\">GitHub</a></li></ul>\n"
            "</div></footer>\n</body>\n</html>\n"
            % (html.escape(title), up, FONTS, up, STYLE, up or "./", up, nav, up or "./", head, lede, body, up or "./", up, up, up))


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
    for i, v in enumerate(versions):
        vs = "%d.%d.%d" % v
        short = open(os.path.join(CHANGELOGS, vs + ".md"), encoding="utf-8").read()
        full_path = os.path.join(CHANGELOGS, "full", vs + ".md")
        has_full = os.path.isfile(full_path)
        if v >= FULL_SINCE and not has_full: missing.append(vs)
        full = open(full_path, encoding="utf-8").read() if has_full else short
        d = os.path.join(OUT, vs)
        os.makedirs(d)
        note = "" if has_full else "<p class=\"note\">The full notes for this version are its short notes: that was all that was written then.</p>\n"
        newer = "%d.%d.%d" % versions[i - 1] if i > 0 else None
        older = "%d.%d.%d" % versions[i + 1] if i + 1 < len(versions) else None
        pager = "<nav class=\"pager\" aria-label=\"Other versions\"><span>%s</span><span>%s</span></nav>" % (
            "<a href=\"../%s/\">&larr; %s</a>" % (older, older) if older else "",
            "<a href=\"../%s/\">%s &rarr;</a>" % (newer, newer) if newer else "<a href=\"../\">All updates &rarr;</a>")
        with open(os.path.join(d, "index.html"), "w", encoding="utf-8") as f:
            f.write(page("quark %s: everything that changed" % vs,
                         "%s<div class=\"prose\">\n%s\n</div>\n%s" % (note, md(full), pager), 2,
                         head="<p class=\"crumbs\"><a href=\"../\">&larr; All updates</a></p>\n<p class=\"eyebrow\">%s</p>\n<h1>quark %s</h1>\n"
                              % ("Latest update" if i == 0 else "Update", html.escape(vs)),
                         lede="<p class=\"lede\">Everything that changed in this version, and why.</p>\n"))
        entries.append("<article class=\"entry\" id=\"v%s\"><h2><a href=\"%s/\">%s</a>%s</h2>\n<div><div class=\"prose\">%s</div>\n"
                       "<p class=\"full\"><a href=\"%s/\">Everything that changed in %s, and why &rarr;</a></p></div></article>"
                       % (vs, vs, vs, "<span class=\"badge cyan latest\">Latest</span>" if i == 0 else "", md(short), vs, vs))
    majors = []   # quick links: the newest release of each minor version line, newest first (x.y)
    for v in versions:
        if not majors or majors[-1][0] != v[:2]: majors.append((v[:2], "%d.%d.%d" % v))
    toc = "<ul class=\"toc\" aria-label=\"Jump to a version\">%s</ul>\n" % "".join(
        "<li><a href=\"#v%s\">%d.%d</a></li>" % (vs, mv[0], mv[1]) for mv, vs in majors)
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(page("quark: every update", "\n".join(entries), 1,
                     head="<p class=\"eyebrow\">%d versions</p>\n<h1>Every update</h1>\n" % len(versions),
                     lede="<p class=\"lede\">Every version of quark, newest first. Each one has a page with its full changelog.</p>\n" + toc))
    print("site/updates: %d versions" % len(versions))
    if missing and ALLOW_MISSING_FULL:
        print("warning: changelogs/full/<version>.md is missing for: " + ", ".join(missing))
    elif missing:
        sys.exit("changelogs/full/<version>.md is missing for: " + ", ".join(missing) + " (every version from %d.%d.%d on needs one)" % FULL_SINCE)


if __name__ == "__main__":
    main()

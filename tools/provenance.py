#!/usr/bin/env python3
"""The asset provenance registry (docs/PROVENANCE.md): for every asset, whether it is AI-made, human-made (by the team)
or third-party (made by people, from an outside pack or font), so the owner can see what is left to replace and every
release's notes can say how far along it is. Standard library only.

    python3 tools/provenance.py sync [<export dir>]      add / drop items: the asset export's manifest.json (default
                                                          build/assets_export) + the UI pages and bot areas (from the
                                                          source) + the evidence in the repository
    python3 tools/provenance.py merge <sidecar.json>...   fold editor sidecars (Quark/<editor>/provenance.json) in
    python3 tools/provenance.py summary [--markdown]      human vs AI, overall and per category
    python3 tools/provenance.py check                     the files are valid (CI's meta job; needs no build)

The registry is assets/provenance/<category>.json. sync never needs a build for the ui / bots / fonts items; without
an export it keeps the file assets as they are and only re-applies the evidence. An entry with `by`, `date` or `tool`
was set by a person or an editor (curated): sync never changes it. Every other entry is the tool's own reading of the
repository and is worked out again on each sync (its extra fields, such as `wip` and `note`, are kept)."""
import ast, json, math, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REG = os.path.join(ROOT, "assets", "provenance")
FORMAT = 1
CATEGORIES = ["sounds", "voices", "models", "maps", "fonts", "images", "ui", "bots"]   # (the summary's order)
PREFIX = {"sounds": "sounds", "voices": "voices", "models": "models", "maps": "maps", "fonts": "fonts",
          "images": "images", "website": "images", "ui": "ui", "bots": "bots"}       # id's first part -> category
ORIGINS = ["ai", "human", "third-party"]
TOOLS = ["sound-editor", "model-editor", "map-editor", "bot-editor", "ui-customizer", "dashboard", "asset-studio"]
FIELDS = ["origin", "source", "by", "date", "tool", "version", "wip", "note", "evidence"]   # (written in this order)
CURATED = ("by", "date", "tool")


def category_of(item_id):
    return PREFIX.get(item_id.split("/", 1)[0])


def read(path):
    with open(path, encoding="utf-8") as f: return f.read()


# ---------------------------------------------------------------------------------------------------------------
# the registry files

def load_file(path, problems=None):
    """{id: entry} of one category file; duplicate ids are a problem (json would silently keep the last)"""
    dups = []
    def pairs(kv):
        seen = {}
        for k, v in kv:
            if k in seen: dups.append(k)
            seen[k] = v
        return seen
    data = json.loads(read(path), object_pairs_hook=pairs)
    if problems is not None:
        for d in dups: problems.append("%s: id %s is listed twice" % (os.path.relpath(path, ROOT), d))
    return data


def load_registry():
    reg = {}
    for c in CATEGORIES:
        p = os.path.join(REG, c + ".json")
        if os.path.exists(p): reg.update(load_file(p).get("items", {}))
    return reg


def entry_json(e):
    keys = [k for k in FIELDS if k in e] + sorted(k for k in e if k not in FIELDS)
    return "{" + ", ".join("%s: %s" % (json.dumps(k), json.dumps(e[k], ensure_ascii=False)) for k in keys) + "}"


def file_text(category, items):
    """the canonical text: one item per line, ids sorted (small diffs; json.load reads it)"""
    lines = ['{', ' "format": %d,' % FORMAT, ' "category": %s,' % json.dumps(category), ' "items": {']
    ids = sorted(items)
    for i, k in enumerate(ids):
        lines.append("  %s: %s%s" % (json.dumps(k, ensure_ascii=False), entry_json(items[k]), "," if i + 1 < len(ids) else ""))
    lines += [' }', '}']
    return "\n".join(lines) + "\n"


def save_registry(reg):
    os.makedirs(REG, exist_ok=True)
    by_cat = {c: {} for c in CATEGORIES}
    for k, e in reg.items():
        c = category_of(k)
        if c is None: sys.exit("unknown category for id %r (first part must be one of %s)" % (k, ", ".join(sorted(PREFIX))))
        by_cat[c][k] = e
    changed = []
    for c, items in by_cat.items():
        p = os.path.join(REG, c + ".json")
        text = file_text(c, items)
        if not os.path.exists(p) or read(p) != text:
            with open(p, "w", encoding="utf-8", newline="\n") as f: f.write(text)
            changed.append(c)
    return changed


# ---------------------------------------------------------------------------------------------------------------
# what the repository shows

def evidence_files(base, sub=""):
    """{lower-case path without extension: repo-relative file} for the files under base (README / .keep skipped)"""
    out = {}
    if not os.path.isdir(base): return out
    for d, dirs, names in os.walk(base):
        dirs.sort()
        for n in sorted(names):
            stem, ext = os.path.splitext(n)
            if n.startswith(".") or ext.lower() in (".md", ".txt"): continue
            rel = os.path.relpath(os.path.join(d, stem), base).replace(os.sep, "/")
            out[(sub + "/" + rel if sub else rel).lower()] = os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, "/")
    return out


def exec_assignments(path, names):
    """the values of some top-level assignments (and the helper functions they call) of a tool script, without running
    the script (import_sfx.py / import_voice.py run their main() on import)"""
    tree = ast.parse(read(path), path)
    keep = [n for n in tree.body if isinstance(n, ast.FunctionDef) or
            (isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id in names for t in n.targets))]
    ns = {}
    exec(compile(ast.Module(body=keep, type_ignores=[]), path, "exec"), ns)
    return {k: ns[k] for k in names if k in ns}


KENNEY = {"impact": "Impact Sounds", "scifi": "Sci-fi Sounds", "iface": "Interface Sounds", "rpg": "RPG Audio",
          "casino": "Casino Audio"}   # (assets/sfx/CREDITS.md)


class Repo:
    def __init__(self):
        self.overrides = evidence_files(os.path.join(ROOT, "assets", "overrides"))
        self.custom = evidence_files(os.path.join(ROOT, "assets", "sfx", "custom"), "sounds")
        sfx = exec_assignments(os.path.join(ROOT, "tools", "import_sfx.py"), ["KITS", "KIT_DIRS"])
        self.kits = {sfx["KIT_DIRS"][k]: v for k, v in sfx["KITS"].items()}
        voice = exec_assignments(os.path.join(ROOT, "tools", "import_voice.py"), ["ALL", "VOICES", "MORE"])
        self.recorded = {}   # voice folder -> {LINE: file}
        for folder, files in voice["VOICES"]:
            files = dict(files)
            if folder in voice["MORE"]: files["more.wav"] = voice["MORE"][folder]
            for f, lines in files.items():
                for ln in lines:
                    name = ln[0] if isinstance(ln, tuple) else ln
                    if not name.startswith("_"): self.recorded.setdefault(folder, {})[name] = f
        self.tts = ("male", "female")   # the voices with a generated line for every line (voice_data.h: kVoiceSets 2)

    def override(self, item_id):
        return self.overrides.get(os.path.splitext(item_id)[0].lower())

    def derive(self, item_id, export_dir=None):
        """(origin, source, evidence or None, extra fields) for an item, from what the repository shows"""
        ov = self.override(item_id)
        if ov: return "human", "the owner's file in assets/overrides/ (shipped as the new default)", ov, {}
        parts = item_id.split("/")
        cat = category_of(item_id)
        if cat == "sounds": return self.sound(parts[1], os.path.splitext(parts[2])[0])
        if cat == "voices": return self.voice(parts[1], os.path.splitext(parts[2])[0])
        if cat == "models": return "ai", "box model in src/client/models.cpp", None, {}
        if cat == "maps":
            name = os.path.splitext(parts[1])[0]
            if self.handmade(name, export_dir):
                return "human", "a hand-made map (its `handmade` line: written by the map editor)", "maps/%s.qmap" % name, {}
            if os.path.exists(os.path.join(ROOT, "tools", "maps", name + ".py")):
                return "ai", "generated by tools/make_maps.py from tools/maps/%s.py" % name, None, {}
            return "ai", "a shipped map not made by tools/make_maps.py, with no `handmade` line", None, {}
        if cat == "fonts":
            stem = os.path.splitext(parts[1])[0]
            lic = os.path.join("assets", "fonts", stem + "-OFL.txt")
            authors = {"WorkSans": "Work Sans Project Authors", "Rubik": "The Rubik Project Authors"}.get(stem, "its authors")
            use = {"WorkSans": "the game's UI font", "Rubik": "the icon's letter and the website's logo"}.get(stem, "")
            if os.path.exists(os.path.join(ROOT, lic)):
                return "third-party", "%s by %s, SIL OFL 1.1 (%s)%s" % (stem, authors, lic, "; " + use if use else ""), None, {}
            return "ai", "a font file with no licence file next to it", None, {}
        if item_id == "images/icon.png":
            return "ai", "drawn by tools/make_icon.py: a Rubik ExtraBold Q (the letterform is the font's, third-party) in the team colours", None, {}
        if parts[0] == "website":
            return "ai", "picture on the website (site/img/%s, the site branch): an in-game shot of the AI-made maps / models" % parts[1], None, {}
        if cat == "ui": return "ai", "drawn by the game's UI code (src/client/), AI-written", None, {}
        if cat == "bots": return "ai", "bot logic in src/game/bots.cpp, AI-written", None, {}
        return "ai", "", None, {}

    def handmade(self, name, export_dir):
        for d in ([export_dir] if export_dir else []) + [ROOT]:
            p = os.path.join(d, "maps", name + ".qmap")
            if os.path.exists(p):
                with open(p, encoding="utf-8", errors="replace") as f:
                    for line in f:
                        if line.strip() == "handmade": return True
                return False
        return False

    def sound(self, kit, name):
        key = "sounds/%s/%s" % (kit, name.lower())
        if key in self.custom:
            v = {"version": "1.14.1"} if kit == "mouth" else {}
            return "human", "the owner's recording (assets/sfx/custom/, tools/import_sfx.py)", self.custom[key], v
        if kit == "default": return "ai", "synthesized in src/client/audio.cpp", None, {}
        default_ov = self.override("sounds/default/%s.wav" % name)
        if kit == "retro":
            if default_ov: return "human", "the DEFAULT kit's override, chiptuned (src/client/audio.cpp)", default_ov, {}
            return "ai", "synthesized (chiptune) in src/client/audio.cpp", None, {}
        spec = self.kits.get(kit, {}).get(name)
        if spec:
            clips = [layer[0] for layer in spec[1]]
            own = [c for c in clips if c.startswith("custom/")]
            if own and len(own) == len(clips):
                return ("human", "mixed by tools/import_sfx.py (KITS) from the owner's recordings: " + ", ".join(sorted(set(own))),
                        None, {"version": "1.14.1"} if kit == "mouth" else {})
            packs = sorted(set(KENNEY.get(c.split("/")[0], c.split("/")[0]) for c in clips if not c.startswith("custom/")))
            src = "Kenney %s (CC0 1.0, assets/sfx/CREDITS.md): %s, mixed by tools/import_sfx.py" % (
                " + ".join(packs), ", ".join(c for c in clips))
            return "third-party", src + ("; also the owner's recordings" if own else ""), None, {}
        if default_ov: return "human", "falls back to the DEFAULT kit's sound: the owner's override", default_ov, {}
        return "ai", "falls back to the DEFAULT kit's sound, synthesized in src/client/audio.cpp", None, {}

    def voice(self, voice, line):
        folder = voice.replace("_", "")
        f = self.recorded.get(folder, {}).get(line)
        if f:
            return ("ai", "recording assets/voice/%s/%s, cut by tools/import_voice.py (added in 1.12.0; AI-voiced, "
                    "the owner said so 2026-10-24)" % (folder, f), None, {})
        if folder in self.tts:
            return "ai", "Piper neural TTS (tools/make_voice.py -> src/client/voice_data.h)", None, {}
        return "ai", "missing: no recording of this line, exported as silence (silent in the game)", None, {}


# ---------------------------------------------------------------------------------------------------------------
# the non-file items: UI pages and bot areas, from the source

HUD = [   # (id, a regex that finds it in src/client/*.cpp): one item per HUD element / overlay; dropped when it's gone
    ("hud/health", r"HUD_HEALTH"), ("hud/weapon-name", r"HUD_WEAPON\b"), ("hud/cooldown-bars", r"HUD_BARS"),
    ("hud/speed-meter", r"HUD_SPEED"), ("hud/weapon-list", r"HUD_SLOTS"), ("hud/key-overlay", r"Game::drawKeyOverlay\("),
    ("hud/crosshair", r"Game::drawCrosshair\("), ("hud/scope", r"scopeOverlay_"), ("hud/score-box", r"top: mode, timer, scores"),
    ("hud/objective", r"// objective line"), ("hud/spectating", r"spectating: a big banner"),
    ("hud/kill-feed", r"kill feed"), ("hud/chat", r"Game::drawChatBox\("), ("hud/notices", r"Game::drawNotices\("),
    ("hud/scoreboard", r"Game::drawScoreboard\("), ("hud/vote-panel", r"Game::drawVotePanel\("),
    ("hud/tagger-marker", r"Game::drawTaggerMarker\("), ("hud/rps", r"Game::drawRps\("), ("hud/bracket", r"Game::drawBracket\("),
    ("hud/crown", r"Game::drawCrown\("), ("hud/name-tags", r"name tags for teammates"), ("hud/damage-numbers", r"struct DmgNum"),
    ("hud/verbose-info", r"Game::drawVerboseHud\("), ("hud/widgets", r"kStyleNames"), ("hud/perf-overlay", r"Game::drawPerf\("),
    ("hud/loading", r"Game::drawBakeScreen\("),
]


def c_array(src, name):
    m = re.search(r"\b%s\[\]\s*=\s*\{([^}]*)\}" % re.escape(name), src)
    return re.findall(r'"([^"]*)"', m.group(1)) if m else []


def ui_pages():
    """ui/<page> ids: every page the menus navigate to (UiExp::nav targets and the UI test's page paths,
    src/client/uitest.cpp uiScreenPath), the same page reached from two places counted once, plus the HUD"""
    cdir = os.path.join(ROOT, "src", "client")
    srcs = {n: read(os.path.join(cdir, n)) for n in sorted(os.listdir(cdir)) if n.endswith((".cpp", ".h"))}
    allsrc = "\n".join(srcs.values())
    uit = srcs.get("uitest.cpp", "")
    raw = set(re.findall(r'UiExp::nav\("([^"]+)"\)', allsrc))
    a = uit.find("uiScreenPath() const")
    body = uit[a:uit.find("\n}\n", a)] if a >= 0 else ""
    sub = body[body.find("settingsSub"):body.find("};", body.find("settingsSub"))]
    raw |= {"settings" + s for s in re.findall(r'"(/[a-z/-]+)"', sub)}
    pk = body[body.find("pickerName"):body.find("};", body.find("pickerName"))]
    raw |= {"picker/" + s for s in re.findall(r'return "([a-z]+)";', pk) if s != "?"}
    raw |= {"info/" + s for s in c_array(body, "info")}
    raw |= {"custom/" + s for s in c_array(body, "custom")}
    raw |= set(re.findall(r'"((?:main|editor)/[a-z-]+)"', body + srcs.get("game.h", "")))
    pages = set()
    for p in raw:
        p = p.lstrip("~")
        if p.endswith("*"): continue                     # (info/*: the tabs, listed above)
        p = p.replace("editor-online", "editor")         # (the same editor, shared)
        p = re.sub(r"^ingame/pause/settings", "settings", p)   # (the settings pages, opened in game)
        p = re.sub(r"^.*picker:", "picker/", p)          # (one picker per list, from any screen)
        p = re.sub(r"^ingame/pause", "pause", p)
        if p.endswith("/chat"): p = "hud/chat"            # (the chat box and its line, anywhere)
        if p.endswith("/console"): p = "console"
        if p == "ingame": continue                       # (the HUD: its elements below)
        pages.add(p)
    if len(pages) < 20: sys.exit("ui: found only %d pages in src/client/ (has the UI code moved? update ui_pages())" % len(pages))
    pages |= {h for h, rx in HUD if re.search(rx, allsrc)}
    return sorted("ui/" + p for p in pages)


BOT_AREAS = ["bots/body/nav", "bots/body/safety", "bots/body/aim-hand", "bots/body/airstrafe", "bots/perception",
             "bots/aim", "bots/weapons", "bots/fight-movement", "bots/roam", "bots/targeting"]   # (the bot editor's brain units)


def bot_areas():
    src = read(os.path.join(ROOT, "src", "game", "gamedefs.h"))
    m = re.search(r"modeName\(int m\)\s*\{\s*static const char\* n\[\]\s*=\s*\{([^}]*)\}", src)
    if not m: sys.exit("bots: no modeName() list in src/game/gamedefs.h")
    return BOT_AREAS + ["bots/mode/" + n for n in re.findall(r'"([^"]+)"', m.group(1))]


def font_files():
    d = os.path.join(ROOT, "assets", "fonts")
    return ["fonts/" + n for n in sorted(os.listdir(d)) if n.lower().endswith((".ttf", ".otf"))] if os.path.isdir(d) else []


# ---------------------------------------------------------------------------------------------------------------
# commands

def curated(e):
    return any(k in e for k in CURATED)


def cmd_sync(args):
    export = args[0] if args else os.path.join(ROOT, "build", "assets_export")
    man = os.path.join(export, "manifest.json")
    reg = load_registry()
    repo = Repo()
    ids = set(ui_pages()) | set(bot_areas()) | set(font_files())
    ids |= {k for k in reg if k.startswith("bots/")}   # (user brains, bots/brain/<name>: never dropped)
    ids |= {k for k in reg if k.startswith("ui/") and curated(reg[k])}   # (pages an editor named: from a merge)
    file_cats = ("sounds", "voices", "models", "maps", "images")
    if os.path.exists(man):
        items = json.loads(read(man))["items"]
        files = {it["file"] for it in items}
        if not any(f.startswith("website/") for f in files):   # (no site/ checkout when it was exported: keep them)
            print("note: the export has no website pictures (no site/ checkout): keeping the registry's website/ items")
            files |= {k for k in reg if k.startswith("website/")}
        ids |= files
        export_dir = export
    else:
        print("note: no %s: keeping the file assets as listed (run `build/quark_tests exportassets %s` to add / drop them)" % (man, export))
        ids |= {k for k in reg if category_of(k) in file_cats or k.startswith("fonts/")}
        export_dir = None
    bad = sorted(k for k in ids if category_of(k) is None)
    if bad: sys.exit("ids of no category: " + ", ".join(bad[:10]))
    # an asset whose file changed extension (an override uploaded as .ogg / .otf) keeps its entry
    gone = {os.path.splitext(k)[0].lower(): k for k in reg if k not in ids}
    out, added, kept_curated, changed, moved = {}, [], 0, [], set()
    for k in sorted(ids):
        old = reg.get(k)
        if old is None and os.path.splitext(k)[0].lower() in gone:
            moved.add(gone[os.path.splitext(k)[0].lower()]); old = reg[gone.pop(os.path.splitext(k)[0].lower())]
        if old is not None and curated(old):
            out[k] = old; kept_curated += 1; continue
        origin, source, ev, extra = repo.derive(k, export_dir)
        e = {kk: vv for kk, vv in (old or {}).items() if kk not in ("origin", "source", "evidence")}
        if old is not None and old.get("origin") != origin: e.pop("version", None)   # (it was about the old origin)
        e.update({"origin": origin, "source": source})
        if ev: e["evidence"] = ev
        e.update(extra)
        if old is None: added.append(k)
        elif old.get("origin") != origin: changed.append("%s: %s -> %s" % (k, old.get("origin"), origin))
        out[k] = e
    dropped = sorted(k for k in reg if k not in out and k not in moved)
    files = save_registry(out)
    print("provenance: %d items (%d new, %d dropped, %d kept as curated)%s" % (
        len(out), len(added), len(dropped), kept_curated, "; wrote " + ", ".join(files) if files else "; no change"))
    for c in changed: print("  origin " + c)
    for k in dropped[:40]: print("  dropped " + k)
    return 0


def cmd_merge(args):
    if not args: sys.exit("merge: name one or more sidecar files (Quark/<editor>/provenance.json)")
    reg = load_registry()
    n_new = n_upd = 0
    for path in args:
        problems = []
        side = load_file(path, problems)
        if problems: sys.exit("\n".join(problems))
        if side.get("format") != FORMAT: sys.exit("%s: format %r (this tool reads %d)" % (path, side.get("format"), FORMAT))
        tool = side.get("tool")
        if not isinstance(tool, str) or not tool: sys.exit("%s: no \"tool\" (which editor wrote it: %s)" % (path, ", ".join(TOOLS)))
        for k, e in sorted(side.get("items", {}).items()):
            if category_of(k) is None: sys.exit("%s: %s is no category's id" % (path, k))
            if e.get("origin") not in ORIGINS: sys.exit("%s: %s has origin %r (one of %s)" % (path, k, e.get("origin"), ", ".join(ORIGINS)))
            new = dict(reg.get(k, {}))
            new.pop("evidence", None)            # (a person's word replaces the tool's reading)
            if e.get("origin") != new.get("origin"):
                for f in ("source", "version"): new.pop(f, None)   # (they were about the old origin)
            new.update(e)
            if "tool" not in new: new["tool"] = tool   # (so a sync keeps it as the editor's: curated)
            if new.get("origin") == "human" and "wip" in new and "wip" not in e: del new["wip"]   # (shipped: no longer in progress)
            if k not in reg:
                n_new += 1; print("  new %s (%s)%s" % (k, new["origin"], "" if k.startswith(("bots/", "ui/")) else ": a sync drops it unless the asset exists"))
            elif new != reg[k]:
                n_upd += 1; print("  %s: %s -> %s" % (k, reg[k].get("origin"), new["origin"]))
            reg[k] = new
    files = save_registry(reg)
    print("merge: %d updated, %d new%s" % (n_upd, n_new, "; wrote " + ", ".join(files) if files else ""))
    return 0


def counts(reg):
    per = {c: {o: 0 for o in ORIGINS} for c in CATEGORIES}
    for k, e in reg.items():
        c = category_of(k)
        if c in per and e.get("origin") in ORIGINS: per[c][e["origin"]] += 1
    return per


def pct(h, n):
    """one decimal, rounded toward 'not done yet': 100% only when every item is human-made, 0% only when none is"""
    if n == 0 or h == n: return "100%" if n else "-"
    v = math.floor(h * 1000.0 / n) / 10
    if h and v == 0: v = 0.1
    return "%.1f%%" % v


def ai_pct(h, n):
    if n == 0: return "-"
    if h == n: return "0%"
    v = round(100 - float(pct(h, n).rstrip("%")), 1)
    return "%.1f%%" % v


def cmd_summary(args):
    reg = load_registry()
    per = counts(reg)
    tot = {o: sum(per[c][o] for c in CATEGORIES) for o in ORIGINS}
    n = sum(tot.values()); h = tot["human"] + tot["third-party"]
    if "--markdown" in args:
        cats = " · ".join("%s %d/%d" % (c, per[c]["human"] + per[c]["third-party"], sum(per[c].values()))
                          for c in CATEGORIES if sum(per[c].values()))
        print("**Human-made assets: %s** (%d of %d: %d by the team, %d third-party) · AI-made %s" % (
            pct(h, n), h, n, tot["human"], tot["third-party"], ai_pct(h, n)))
        print()
        print("Human-made by category: %s. Every sound, voice line, model, map, font, picture, UI page and bot area "
              "counts as one item (assets/provenance/)." % cats)
        return 0
    print("%-8s %6s %6s %12s %6s %11s %4s" % ("category", "items", "human", "third-party", "ai", "human-made", "wip"))
    for c in CATEGORIES + ["total"]:
        if c == "total":
            row, wip = tot, sum(1 for e in reg.values() if e.get("wip"))
            print("-" * 59)
        else:
            row, wip = per[c], sum(1 for k, e in reg.items() if category_of(k) == c and e.get("wip"))
        cn = sum(row.values()); ch = row["human"] + row["third-party"]
        print("%-8s %6d %6d %12d %6d %11s %4d" % (c, cn, row["human"], row["third-party"], row["ai"], pct(ch, cn), wip))
    return 0


DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def cmd_check(args):
    problems, notes = [], []
    if not os.path.isdir(REG): problems.append("no assets/provenance/ folder")
    names = sorted(os.listdir(REG)) if os.path.isdir(REG) else []
    for n in names:
        p = os.path.join(REG, n)
        rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
        if not n.endswith(".json"): problems.append("%s: not a category file (assets/provenance/ holds <category>.json only)" % rel); continue
        cat = n[:-5]
        if cat not in CATEGORIES: problems.append("%s: no such category (%s)" % (rel, ", ".join(CATEGORIES))); continue
        try: data = load_file(p, problems)
        except ValueError as e: problems.append("%s: not JSON: %s" % (rel, e)); continue
        if data.get("format") != FORMAT: problems.append("%s: format %r, not %d" % (rel, data.get("format"), FORMAT))
        if data.get("category") != cat: problems.append("%s: category %r doesn't match the file name" % (rel, data.get("category")))
        items = data.get("items")
        if not isinstance(items, dict): problems.append("%s: no items object" % rel); continue
        if list(items) != sorted(items): problems.append("%s: ids aren't sorted (a sync / merge writes them sorted)" % rel)
        for k, e in items.items():
            if category_of(k) != cat: problems.append("%s: %s belongs in %s.json" % (rel, k, category_of(k)))
            if not isinstance(e, dict): problems.append("%s: %s isn't an object" % (rel, k)); continue
            if e.get("origin") not in ORIGINS: problems.append("%s: %s origin %r (one of %s)" % (rel, k, e.get("origin"), ", ".join(ORIGINS)))
            for f in ("source", "by", "tool", "version", "wip", "note", "evidence"):
                if f in e and not isinstance(e[f], str): problems.append("%s: %s %s isn't a string" % (rel, k, f))
            if "date" in e and not (isinstance(e["date"], str) and DATE.match(e["date"])): problems.append("%s: %s date %r isn't YYYY-MM-DD" % (rel, k, e["date"]))
            if isinstance(e.get("tool"), str) and e["tool"] not in TOOLS: notes.append("%s: %s tool %r isn't one docs/PROVENANCE.md names" % (rel, k, e["tool"]))
            if isinstance(e.get("version"), str) and not re.match(r"^\d+\.\d+\.\d+", e["version"]): problems.append("%s: %s version %r isn't x.y.z" % (rel, k, e["version"]))
        if read(p) != file_text(cat, items): notes.append("%s: not in the canonical layout (a sync or merge rewrites it)" % rel)
    for c in CATEGORIES:
        if c + ".json" not in names: problems.append("assets/provenance/%s.json is missing" % c)
    for m in notes: print("note: " + m)
    for m in problems: print("error: " + m)
    if problems: return 1
    print("provenance: ok (%d items)" % len(load_registry()))
    return 0


def main():
    cmds = {"sync": cmd_sync, "merge": cmd_merge, "summary": cmd_summary, "check": cmd_check}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        print(__doc__.split("\n\n")[1] if "\n\n" in __doc__ else __doc__)
        return 2
    return cmds[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Quark Physics Editor: the local server behind the physics editor page.

    python3 tools/physics_editor/serve.py [--port 8772] [--data-dir DIR] [--maptool PATH] [--maps-dir DIR ...] [--no-open]
    python3 tools/physics_editor/serve.py --install [FOLDER]     copy the tool to FOLDER (default <Quark folder>/physics-editor)
    tools/physics_editor/run.sh   (Linux / macOS)        tools\\physics_editor\\run.cmd   (Windows)
    --no-open (or QUARK_SHELL=1): no browser; the Quark Project app starts it and shows http://127.0.0.1:8772/?embed=1

Python 3 standard library only. Binds 127.0.0.1 only. It:
  - serves the page (web/),
  - lists, reads and writes physics sets as the game's custom modes: <data dir>/custom_modes/<name>.qmode
    (customModesDir(), src/game/customrules.cpp). A save rewrites ONLY the movement keys (gravity, speed, jump, dash,
    grapple, move_*): every other line stays exactly as it was, in order, and from a `logic` line to the end of the
    file (the gamemode editor's section) nothing is touched. The merge is done here, on the file as it is on disk at
    save time, so a change another editor made since the page loaded it is kept.
  - lists, reads and writes scenarios (<data dir>/physics_editor/scenarios/<name>.json) and the page's state
    (<data dir>/physics_editor/state/<key>.json),
  - lists the maps (the build's maps/ folder, the game's user maps) for testing on a real map,
  - runs the game's helper quark_maptool per request (one JSON request on stdin, one JSON answer on stdout:
    physparams / physsim / physpresets / physroom / physexport, INTERFACE.md),
  - runs `quark_maptool physdrive --stream` for the Drive tab: over a WebSocket (/api/drive/ws: the page's control
    lines in, the helper's answer lines out, one frame each, no polling: the lowest latency a browser gets to a local
    process), or, as a fallback, Server-Sent Events out + POST control lines in (/api/drive...).

Settings come from (later wins): built-in defaults, physics_editor.json next to this file, then the command line.
physics_editor.json keys: port, dataDir, maptool, mapsDirs (list), defaultMapsDir, referencesDir, helperArgs.

Every request must come from the page itself: Host must be 127.0.0.1 / localhost, every write carries the header
X-Quark-Editor (a page on another site can't send it without a CORS preflight, which is never allowed), and the
WebSocket checks Origin (browsers always send it there).
"""
import argparse, base64, hashlib, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, threading, time, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
WEB = os.path.join(HERE, "web")
CACHE = os.path.join(HERE, ".cache")
IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")        # scenario / map / state names
MODE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _-]{0,39}$")        # custom mode names: CustomRules safeName's characters, 40 max
MAX_BODY = 8 << 20
DEFAULT_PORT = 8772          # fixed (the Quark Project app opens http://127.0.0.1:8772/); physics_editor.json / --port change it
HELPER_CMDS = {"physparams", "physsim", "physpresets", "physroom", "physexport", "physdrive", "physexpr", "parse"}
REFS = os.path.join(HERE, "references")      # other games' movement values (references/index.json + <game>.json)
# The game's movement code, shown read-only by the Code tab (Movement code) and the Code graph: from the checkout this
# runs from, else the copy --install puts in source/ next to serve.py.
SOURCE_FILES = {"pmove.cpp": "src/game/pmove.cpp", "pmove.h": "src/game/pmove.h", "gamedefs.h": "src/game/gamedefs.h",
                "customrules.cpp": "src/game/customrules.cpp"}
MIME = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
        ".css": "text/css; charset=utf-8", ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png",
        ".qmode": "text/plain; charset=utf-8", ".qmap": "text/plain; charset=utf-8", ".txt": "text/plain; charset=utf-8",
        ".md": "text/plain; charset=utf-8", ".ico": "image/x-icon"}

# What the installed copy holds (--install). web/ is copied whole (its tests too: small).
PROGRAM_FILES = ["serve.py", "run.cmd", "run.sh", "README.md", "physics_editor.example.json", "INTERFACE.md"]


# Windows device names (con, nul, aux, com1...) can't be files there, with any extension: refused on every system,
# so a set or scenario saved on Linux still opens on q's Windows PC (the page checks the same: api.js isReservedName)
WIN_RESERVED = re.compile(r"^(con|prn|aux|nul|com[0-9]|lpt[0-9])\s*(\..*)?$", re.I)


def valid_name(n):
    return isinstance(n, str) and bool(NAME_RE.match(n)) and ".." not in n and not WIN_RESERVED.match(n)


def valid_mode(n):
    return isinstance(n, str) and bool(MODE_RE.match(n)) and not n.endswith(" ") and not WIN_RESERVED.match(n)


def default_data_dir():
    """The game's user data folder (map.cpp defaultUserDir), QUARK_DATA_DIR first."""
    e = os.environ.get("QUARK_DATA_DIR")
    if e: return e
    if IS_WIN:
        a = os.environ.get("APPDATA")
        if a: return os.path.join(a, "quark")
    elif sys.platform == "darwin":
        return os.path.join(os.path.expanduser("~"), "Library", "Application Support", "quark")
    x = os.environ.get("XDG_DATA_HOME")
    if x: return os.path.join(x, "quark")
    return os.path.join(os.path.expanduser("~"), ".local", "share", "quark")


# --- the Quark folder (holds repository\, working-repo*\, the installed editors) -----------------------------------

def looks_like_quark(d):
    try:
        return os.path.isdir(d) and any(n.lower() == "repository" or n.lower().startswith("working-repo") for n in os.listdir(d))
    except OSError:
        return False


def inside_git(d):
    while True:
        if os.path.exists(os.path.join(d, ".git")): return d
        up = os.path.dirname(d)
        if up == d: return None
        d = up


def default_quark():
    """where this is installed (Quark\\physics-editor\\), the folder holding the clone this runs from
    (Quark\\working-repo\\tools\\physics_editor\\), or ~/Desktop/Quark"""
    cands = [os.path.dirname(HERE)]
    clone = inside_git(HERE)
    if clone: cands.append(os.path.dirname(clone))
    cands.append(os.path.join(os.path.expanduser("~"), "Desktop", "Quark"))
    for c in cands:
        if looks_like_quark(c): return c
    return cands[-1]


def source_roots():
    """Repository checkouts to look in for the helper and the maps: the one this runs from (if it is one), then the
    Quark folder's clones (an installed copy has none of its own)."""
    out = []
    if os.path.isfile(os.path.join(ROOT, "CMakeLists.txt")): out.append(ROOT)
    q = default_quark()
    try:
        for n in sorted(os.listdir(q)):
            if n.lower() == "repository" or n.lower().startswith("working-repo"):
                p = os.path.join(q, n)
                if os.path.isdir(p) and p not in out: out.append(p)
    except OSError:
        pass
    return out


def find_binary(name, explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    cands = [os.path.join(HERE, name + EXE)]          # an installed copy: next to serve.py
    for r in source_roots():
        for b in ("build", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin"):
            cands.append(os.path.join(r, b, name + EXE))
    for c in cands:
        if os.path.isfile(c): return c
    return shutil.which(name)


class Config:
    def __init__(self, args):
        cfg = {}
        path = os.path.join(HERE, "physics_editor.json")
        if os.path.isfile(path):
            try:
                with open(path, encoding="utf-8") as f: cfg = json.load(f)
            except Exception as ex:
                print("physics_editor.json ignored: %s" % ex)
        g = lambda k, a=None: a if a not in (None, [], "") else cfg.get(k)
        self.port = int(g("port", args.port) or DEFAULT_PORT)
        self.data_dir = os.path.abspath(g("dataDir", args.data_dir) or default_data_dir())
        self.maptool = find_binary("quark_maptool", g("maptool", args.maptool))
        self.helper_args = list(cfg.get("helperArgs") or [])
        self.extra_maps = [os.path.abspath(d) for d in (g("mapsDirs", args.maps_dir) or [])]
        dm = g("defaultMapsDir")
        self.default_maps = os.path.abspath(dm) if dm else None
        rd = g("referencesDir", getattr(args, "references", None))
        self.refs = os.path.abspath(rd) if rd else REFS

    def default_maps_dir(self):
        if self.default_maps and os.path.isdir(self.default_maps): return self.default_maps
        cands = [os.path.join(HERE, "maps")]
        for r in source_roots(): cands += [os.path.join(r, "maps"), os.path.join(r, "build", "maps")]
        cands.append(os.path.join(CACHE, "maps"))
        for d in cands:
            try:
                if os.path.isdir(d) and any(f.endswith(".qmap") for f in os.listdir(d)): return d
            except OSError:
                continue
        return None

    def user_maps_dir(self): return os.path.join(self.data_dir, "maps")
    def modes_dir(self): return os.path.join(self.data_dir, "custom_modes")
    def scenarios_dir(self): return os.path.join(self.data_dir, "physics_editor", "scenarios")
    def state_dir(self): return os.path.join(self.data_dir, "physics_editor", "state")


CFG = None


# ---------------------------------------------------------------- .qmode files (the movement part only)

TOP_KEYS = ["gravity", "speed", "jump", "dash", "grapple"]          # CustomRules::toText order
BOOL_KEYS = {"dash", "grapple"}
MV_KEYS = ["crouch_speed", "ground_accel", "friction", "stop_speed", "air_accel", "air_cap", "overstrafe", "air_control",
           "step", "dash_speed", "dash_time", "dash_exit_min", "dash_keep", "dash_exit_max", "dash_vertical", "dash_charges",
           "dash_cooldown", "bounce_gain", "bounce_max", "hook_range", "hook_accel", "hook_max", "hook_gravity", "hook_release",
           "hook_cooldown", "ladder_speed", "ladder_side", "dash_jump_window", "super_dash_boost", "super_dash_jump",
           "super_dash_max", "dash_bounce_angle", "air_drift", "strafe_boost"]   # moveParamKey (gamedefs.h), in order
EXPR_KEYS = ["gravity", "speed", "jump"] + MV_KEYS     # expr.<key> lines (v2, INTERFACE.md), written in this order
IMPORT_PREFIX = "# imported from "                    # the set's record of values taken from other games (a comment)


def is_move_key(k):
    return k in TOP_KEYS or k.startswith("move_") or k.startswith("expr.")


def is_import_line(l):
    return l.rstrip("\r").startswith(IMPORT_PREFIX)


def _clean(v):
    return re.sub(r"[()]", "", re.sub(r"[=\r\n]", " ", str(v))).strip()


def import_lines(phys):
    out = []
    for i in phys.get("imports") or []:
        if not isinstance(i, dict) or not i.get("id") or not i.get("keys"): continue
        out.append("%s%s (%s): %s" % (IMPORT_PREFIX, _clean(i.get("name") or i["id"]), _clean(i["id"]), ", ".join(_clean(k) for k in i["keys"])))
    return out


def is_logic_line(line):
    """A `logic` line starts the gamemode editor's section (to the end of the file): never touched here. Exactly the
    game's isModeLogicLine (customrules.cpp): "logic" at column 0, then the end of the line, a space, a tab or a CR."""
    return line[:5] == "logic" and (len(line) == 5 or line[5] in " \t\r")


def fmt_num(v):
    """A float the way C++ ostream << float writes it (%g, 6 significant digits): 1, 0.5, 1.25, 2.33333."""
    v = float(v)
    if v != v or v in (float("inf"), float("-inf")): return "1"
    s = "%.6g" % v
    if "e" in s:   # (never inside the clamps the game allows; keep a plain number all the same)
        s = ("%.6f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def movement_lines(phys):
    """{key: text value} of every movement key a physics set writes, in toText order (move_* only when not 1)."""
    out = []
    for k in TOP_KEYS:
        if k in BOOL_KEYS: out.append((k, "1" if phys.get(k, True) not in (False, 0, "0") else "0"))
        else: out.append((k, fmt_num(phys.get(k, 1))))
    for k, v in (phys.get("move") or {}).items():
        if not re.match(r"^[a-z_]{1,32}$", k): continue
        try: f = float(v)
        except (TypeError, ValueError): continue
        if f != 1: out.append(("move_" + k, fmt_num(f)))
    ex = phys.get("expr") or {}
    rank = lambda k: EXPR_KEYS.index(k) if k in EXPR_KEYS else 1000
    keys = sorted((k for k, v in ex.items() if re.match(r"^[a-z_]{1,32}$", k) and isinstance(v, str) and v.strip()), key=lambda k: (rank(k), k))
    for k in keys: out.append(("expr." + k, re.sub(r"[\r\n]+", " ", ex[k]).strip()))
    return out


def merge_qmode(text, phys, name=None):
    """The movement keys of `phys` written into `text` (a .qmode): only those lines change, in place; a move_ key back
    at 1 is dropped (toText writes only the changed ones); missing changed keys are added where toText puts them. Every
    other line, and everything from a `logic` line on, stays byte for byte. text None/"" = a new file: the toText
    header (custommode=, mode=) and the movement lines only."""
    want = movement_lines(phys)
    imps = import_lines(phys)
    if not text:
        nm = name or "physics"
        return "custommode=%s\nmode=ffa\n" % nm + "".join(l + "\n" for l in imps) + "".join("%s=%s\n" % kv for kv in want)
    # (each line keeps its own ending: a file with CRLF, or mixed endings, comes back the same where it isn't changed)
    lines = text.split("\n")
    trailing = lines[-1] == ""
    if trailing: lines = lines[:-1]
    cr = lambda l: "\r" if l.endswith("\r") else ""
    cut = next((i for i, l in enumerate(lines) if is_logic_line(l)), len(lines))
    head, tail = lines[:cut], lines[cut:]
    keyof = lambda l: l.split("=", 1)[0] if "=" in l else None      # (CustomRules::fromText: the text before the first '=')
    wantd = dict(want)
    out = []
    last = {}
    for i, l in enumerate(head):
        k = keyof(l)
        if k is not None: last[k] = i
    seen = set()
    imps_placed = False
    for i, l in enumerate(head):
        k = keyof(l)
        if is_import_line(l):      # the import record: rewritten where its first line was
            if not imps_placed:
                out.extend(x + cr(l) for x in imps); imps_placed = True
            continue
        if k is None or not is_move_key(k) or last.get(k) != i:
            out.append(l)          # not a movement key, or an earlier duplicate (the game reads the last one): as it was
        elif k in wantd:
            out.append("%s=%s%s" % (k, wantd[k], cr(l))); seen.add(k)
        elif k.startswith("move_") and k[5:] in MV_KEYS:
            pass                   # a move_ key back at 1: dropped (toText writes only the changed ones)
        elif k.startswith("expr.") and k[5:] in EXPR_KEYS:
            pass                   # an expression taken off: dropped
        else:
            out.append(l)          # a move_ / expr. key this build doesn't know (the game ignores it): kept
    # (a missing gravity / speed / jump / dash / grapple at 1 or on is what the game reads anyway: not added)
    missing = [(k, v) for k, v in want if k not in seen and not (k in TOP_KEYS and v == "1")]
    if missing:
        def idx_of(keys):
            for kk in keys:
                for j in range(len(out) - 1, -1, -1):
                    if keyof(out[j]) == kk: return j
            return None
        for k, v in missing:
            if k.startswith("expr."):   # after the setting's own line, else after the last movement line
                sk = k[5:]
                j = idx_of([sk if sk in TOP_KEYS else "move_" + sk])
                if j is None:
                    for jj in range(len(out) - 1, -1, -1):
                        kk = keyof(out[jj]) or ""
                        if kk.startswith("move_") or kk.startswith("expr."): j = jj; break
                if j is None: j = idx_of(["rounds", "scorelimit", "timelimit", "regen", "grapple", "dash", "jump", "speed", "gravity"])
            elif k.startswith("move_"):
                j = None
                for jj in range(len(out) - 1, -1, -1):
                    if (keyof(out[jj]) or "").startswith("move_"): j = jj; break
                if j is None: j = idx_of(["rounds", "scorelimit", "timelimit", "regen", "grapple", "dash", "jump", "speed", "gravity"])
            elif k in ("gravity", "speed", "jump"):
                order = {"gravity": [], "speed": ["gravity"], "jump": ["speed", "gravity"]}[k]
                j = idx_of(order + ["weapons", "mode", "custommode"])
            else:
                j = idx_of((["dash"] if k == "grapple" else []) + ["health", "knockback", "jump", "speed", "gravity", "weapons", "mode", "custommode"])
            if j is None:
                # no anchor: after the last key=value line of the head (before blank lines / comments at its end)
                j = max([jj for jj in range(len(out)) if keyof(out[jj]) is not None] or [-1])
            out.insert(j + 1, "%s=%s%s" % (k, v, cr(out[j]) if j >= 0 else cr(text.split("\n", 1)[0])))
    if not imps_placed and imps:   # after the mode= line, else the custommode= line, else at the top
        j = -1
        for kk in ("mode", "custommode"):
            for jj in range(len(out) - 1, -1, -1):
                if keyof(out[jj]) == kk: j = jj; break
            if j >= 0: break
        c = cr(out[j]) if j >= 0 else cr(text.split("\n", 1)[0])
        out[j + 1:j + 1] = [x + c for x in imps]
    return "\n".join(out + tail) + ("\n" if trailing else "")


def read_mode_file(name):
    p = os.path.join(CFG.modes_dir(), name + ".qmode")
    if not os.path.isfile(p): return None
    with open(p, "rb") as f: return f.read().decode("utf-8", "replace")


def scan_float(v):
    """C's sscanf("%f"): the longest number at the start (after spaces), else 0 (what fromText reads)."""
    m = re.match(r"\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|[+-]?(?:inf(?:inity)?|nan))", v, re.I)
    if not m: return 0.0
    try: return float(m.group(1))
    except ValueError: return 0.0


def scan_int(v):
    """C's atoi."""
    m = re.match(r"\s*([+-]?\d+)", v)
    return int(m.group(1)) if m else 0


def mode_info(path):
    """The Open list's line for a custom mode: its name, mode, a logic section, and how many movement settings it really
    changes (as fromText reads it: the last line of a key counts, unknown keys and values back at the standard don't)."""
    info = {"custommode": "", "mode": "", "logic": False, "moved": 0}
    vals = {}
    try:
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            for line in f:
                line = line.rstrip("\n")
                if is_logic_line(line): info["logic"] = True; break
                k, eq, v = line.rstrip("\r").partition("=")
                if not eq: continue
                if k == "custommode": info["custommode"] = v
                elif k == "mode": info["mode"] = v
                elif k in TOP_KEYS or (k.startswith("move_") and k[5:] in MV_KEYS) or (k.startswith("expr.") and k[5:] in EXPR_KEYS): vals[k] = v
    except OSError:
        pass
    changed = set()
    for k, v in vals.items():
        if k in BOOL_KEYS:
            if scan_int(v) == 0: changed.add(k)
        elif k.startswith("expr."):
            if v.strip(" \t"): changed.add(k[5:])
        else:
            f = scan_float(v)
            if f == f and f not in (float("inf"), float("-inf")) and f != 1: changed.add(k[5:] if k.startswith("move_") else k)
    info["moved"] = len(changed)
    return info


def source_path(name):
    rel = SOURCE_FILES.get(name)
    if not rel: return None
    cands = []
    if os.path.isfile(os.path.join(ROOT, "CMakeLists.txt")): cands.append(os.path.join(ROOT, rel))
    cands.append(os.path.join(HERE, "source", name))
    for r in source_roots(): cands.append(os.path.join(r, rel))
    for c in cands:
        if os.path.isfile(c): return c
    return None


def source_label(path):
    """How the Movement code header names a source file: src/game/pmove.cpp in a checkout, source/pmove.cpp in an
    installed copy (next to serve.py), else the full path (another clone of the Quark folder)."""
    for base, need in ((ROOT, os.path.isfile(os.path.join(ROOT, "CMakeLists.txt"))), (HERE, True)):
        if need and os.path.normcase(path).startswith(os.path.normcase(base) + os.sep):
            return os.path.relpath(path, base).replace(os.sep, "/")
    return path.encode("latin-1", "replace").decode("latin-1")   # (an HTTP header: latin-1 only)


# ---------------------------------------------------------------- maps

GEN_LOCK = threading.Lock()


def generate_default_maps():
    with GEN_LOCK:
        if CFG.default_maps_dir(): return CFG.default_maps_dir(), ""
        gen = next((os.path.join(r, "tools", "make_maps.py") for r in source_roots() if os.path.isfile(os.path.join(r, "tools", "make_maps.py"))), None)
        if not gen: return None, "tools/make_maps.py not found"
        out = os.path.join(CACHE, "maps")
        os.makedirs(out, exist_ok=True)
        r = subprocess.run([sys.executable, gen, "--out", out], capture_output=True, text=True)
        if r.returncode != 0: return None, (r.stderr or r.stdout)[-2000:]
        return out, r.stdout.strip()


def map_header(path):
    info = {"title": "", "for": "", "modes": []}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f):
                if i > 60: break
                line = line.split("#", 1)[0].strip()
                kw, _, rest = line.partition(" ")
                if kw == "title": info["title"] = rest.strip()
                elif kw == "for": info["for"] = rest.strip()
                elif kw == "modes": info["modes"] = rest.split()
    except OSError:
        pass
    return info


def list_maps():
    out, dirs = [], []
    dm = CFG.default_maps_dir()
    if dm: dirs.append(("default", dm))
    dirs.append(("user", CFG.user_maps_dir()))
    for d in CFG.extra_maps: dirs.append(("extra", d))
    for src, d in dirs:
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".qmap") or not valid_name(f[:-5]): continue
            m = {"name": f[:-5], "source": src}
            m.update(map_header(os.path.join(d, f)))
            out.append(m)
    return out


def map_path(name, source):
    if not valid_name(name): return None
    d = {"default": CFG.default_maps_dir(), "user": CFG.user_maps_dir()}.get(source)
    dirs = [d] if d else list(CFG.extra_maps)
    for dd in dirs:
        if dd and os.path.isfile(os.path.join(dd, name + ".qmap")): return os.path.join(dd, name + ".qmap")
    return None


def list_files(d, ext, check=valid_name):
    if not os.path.isdir(d): return []
    res = []
    for f in sorted(os.listdir(d)):
        if f.endswith(ext) and check(f[:-len(ext)]):
            st = os.stat(os.path.join(d, f))
            res.append({"name": f[:-len(ext)], "size": st.st_size, "mtime": st.st_mtime})
    return res


def atomic_write(path, data, backup=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if backup and os.path.isfile(path):
        try: shutil.copyfile(path, path + ".bak")
        except OSError: pass
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-", suffix=".part")
    with os.fdopen(fd, "wb") as f: f.write(data)
    os.replace(tmp, path)


# ---------------------------------------------------------------- the helper

def helper_cmd(args):
    if not CFG.maptool: return None
    if not args or args[0] not in HELPER_CMDS: return None
    if not all(isinstance(a, str) and re.match(r"^[A-Za-z0-9_.=:,+-]{1,64}$", a) for a in args): return None
    return [CFG.maptool] + CFG.helper_args + list(args)


def parse_answer(out):
    try: return json.loads(out)
    except ValueError: pass
    for line in reversed(out.strip().splitlines()):
        try: return json.loads(line)
        except ValueError: continue
    return {"raw": out[-8000:]}


class Drive:
    """One `quark_maptool physdrive --stream`: control lines to its stdin, its answer lines out (to the WebSocket, or
    kept for the SSE fallback)."""
    MAX_LINES = 20000

    def __init__(self, cmd, first):
        self.id = uuid.uuid4().hex[:12]
        self.lines, self.base, self.cond, self.done, self.code, self.err = [], 0, threading.Condition(), False, None, ""
        self.started = time.time()
        self.sink = None            # a callable(line) while a WebSocket is attached
        flags = subprocess.CREATE_NO_WINDOW if IS_WIN else 0
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     bufsize=0, creationflags=flags)
        self.wlock = threading.Lock()
        self.send(json.dumps(first))
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def _read(self):
        f = self.proc.stdout
        while True:
            line = f.readline()
            if not line: break
            line = line.decode("utf-8", "replace").strip()
            if not line: continue
            with self.cond:      # (the sink is read under the lock: attach() hands over the lines kept until then, in order)
                s = self.sink
                if not s: self.lines.append(line)
            if s:
                try: s(line); continue
                except Exception:
                    self.sink = None
                    continue
            with self.cond:
                if len(self.lines) > self.MAX_LINES:     # (a page that stopped reading: keep the newest)
                    drop = len(self.lines) - self.MAX_LINES
                    del self.lines[:drop]; self.base += drop
                self.cond.notify_all()
        self.code = self.proc.wait()
        with self.cond:
            self.done = True
            self.cond.notify_all()
        s = self.sink
        if s:
            try: s(None)
            except Exception: pass

    def attach(self, sink):
        """Send the answer lines to sink from now on, after the ones that came before it was attached (physdrive's start
        line is often written before the WebSocket's sink is set: it went to the SSE buffer and the page waited for it)."""
        with self.cond:
            backlog, self.lines = self.lines, []
            self.base += len(backlog)
            for l in backlog: sink(l)
            self.sink = sink

    def _read_err(self):
        for line in self.proc.stderr:
            self.err = (self.err + line.decode("utf-8", "replace"))[-4000:]

    def send(self, line):
        if self.done or self.proc.poll() is not None: return False
        try:
            with self.wlock:
                self.proc.stdin.write((line.strip() + "\n").encode("utf-8"))
                self.proc.stdin.flush()
            return True
        except (OSError, ValueError):
            return False

    def stop(self):
        self.send("stop")
        def killer():
            try: self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired: self.proc.kill()
        threading.Thread(target=killer, daemon=True).start()


DRIVES = {}
DRIVES_LOCK = threading.Lock()


class Worker:
    """One `quark_maptool physserve --stream` kept running: the page's physsim / physexpr / physexport requests go to it
    as lines instead of a new process each (a process start is ~10 ms of the ~15 ms an edit took to come back on Linux,
    more on Windows; the maps sent as text are parsed once). A quark_maptool without physserve (older than this
    editor): the per-request process, as before. Restarted when it dies, times out or the binary is rebuilt."""
    CMDS = {"physsim", "physexpr", "physexport", "physparams", "physpresets", "physroom"}

    def __init__(self):
        self.lock = threading.Lock()
        self.proc = None; self.exe = None; self.mtime = None; self.err = ""; self.seq = 0

    def _stop(self):
        if self.proc:
            try: self.proc.kill()
            except Exception: pass
        self.proc = None

    def _start(self):
        """False: this quark_maptool has no physserve."""
        if not CFG.maptool: return False
        cmd = [CFG.maptool] + CFG.helper_args + ["physserve", "--stream"]   # (not through helper_cmd: the page can't start one)
        self.exe = CFG.maptool
        try: self.mtime = os.stat(self.exe).st_mtime
        except OSError: self.mtime = None
        self.err = ""
        # (buffered pipes: an answer is up to a few hundred KB, and an unbuffered readline reads it a byte per call)
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1 << 16,
                                     creationflags=subprocess.CREATE_NO_WINDOW if IS_WIN else 0)
        threading.Thread(target=self._drain, args=(self.proc,), daemon=True).start()
        self.proc.stdin.write(b"{}\n"); self.proc.stdin.flush()
        first = self._readline(10)
        try: ok = json.loads(first or b"null").get("type") == "ready"
        except (ValueError, AttributeError): ok = False
        if not ok: self._stop()
        return ok

    def _drain(self, proc):
        for line in proc.stderr:
            self.err = (self.err + line.decode("utf-8", "replace"))[-4000:]

    def _readline(self, timeout):
        """One answer line, or None when the process died or took longer than timeout (then it's killed)."""
        p = self.proc
        t = threading.Timer(timeout, lambda: p.kill())
        t.daemon = True; t.start()
        try: line = p.stdout.readline()
        finally: t.cancel()
        return line or None

    def run(self, args, request, timeout):
        """The helper's answer as /api/helper gives it (JSON bytes), or None: no physserve here, use a process."""
        with self.lock:
            if self.proc and (self.proc.poll() is not None or self.exe != CFG.maptool or self._changed()): self._stop()
            if not self.proc and not self._start(): return None
            self.seq += 1
            t0 = time.time()
            try:
                self.proc.stdin.write((json.dumps({"id": self.seq, "cmd": args[0], "request": request}) + "\n").encode("utf-8"))
                self.proc.stdin.flush()
            except OSError:
                self._stop(); raise RuntimeError("the helper stopped: " + self.err[-300:])
            line = self._readline(timeout)
            if line is None:
                err = self.err; self._stop()
                if time.time() - t0 >= timeout - 0.05: raise subprocess.TimeoutExpired(args[0], timeout)
                raise RuntimeError("the helper stopped: " + err[-300:])
            # {"id":N,"code":C,"answer":{...}} passed on as it is (no parse and dump of a few hundred KB of trace here)
            m = re.match(rb'\{"id":(\d+),"code":(-?\d+),"answer":', line)
            if not m or int(m.group(1)) != self.seq: self._stop(); raise RuntimeError("the helper answered out of turn")
            head = '{"code":%s,"stderr":"","worker":true,"wallMs":%.1f,"answer":' % (m.group(2).decode(), (time.time() - t0) * 1000)
            return head.encode() + line[m.end():].rstrip()

    def _changed(self):
        try: return os.stat(self.exe).st_mtime != self.mtime
        except OSError: return True


class Workers:
    """Two physserve workers: a run and an equation check / export answer side by side, as separate processes did."""
    def __init__(self): self.w = [Worker(), Worker()]; self.none = None

    def run(self, args, request, timeout):
        if not args or args[0] not in Worker.CMDS or len(args) != 1: return None
        if self.none is not None:   # (no physserve in this quark_maptool: not asked again until it's rebuilt or replaced)
            try: key = (CFG.maptool, os.stat(CFG.maptool).st_mtime)
            except OSError: key = None
            if key == self.none: return None
            self.none = None
        w = next((x for x in self.w if not x.lock.locked()), self.w[0])
        r = w.run(args, request, timeout)
        if r is None:
            try: self.none = (CFG.maptool, os.stat(CFG.maptool).st_mtime)
            except OSError: self.none = (CFG.maptool, None)
        return r

    def stop(self):
        for x in self.w:
            with x.lock: x._stop()


WORKERS = Workers()


def control_ok(line):
    if line in ("reset", "stop"): return True
    try: o = json.loads(line)
    except ValueError: return False
    return isinstance(o, dict) and bool(set(o) & {"cmd", "physics", "qmode", "reset", "start", "map"})


def add_drive(d):
    with DRIVES_LOCK:
        for k in [k for k, r in DRIVES.items() if r.done or time.time() - r.started > 6 * 3600]:
            try: DRIVES[k].proc.kill()
            except Exception: pass
            del DRIVES[k]
        DRIVES[d.id] = d


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "QuarkPhysicsEditor/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        if os.environ.get("QPE_VERBOSE"): sys.stderr.write("%s\n" % (fmt % a))

    def _host_ok(self):
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0].strip("[]").lower()
        return host in ("127.0.0.1", "localhost", "::1")

    def _origin_ok(self):
        o = self.headers.get("Origin")
        if not o: return False
        u = urlparse(o)
        return u.scheme == "http" and (u.hostname or "").lower() in ("127.0.0.1", "localhost", "::1") and (u.port or 80) == CFG.port

    def _send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body)
        if isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _err(self, code, msg):
        if self.command == "POST" and not getattr(self, "_body_read", False):
            # (a POST refused before its body was read: the unread body would be taken for the next request on this
            # keep-alive connection, so close it)
            self.close_connection = True
            return self._send(code, {"error": msg}, extra={"Connection": "close"})
        self._send(code, {"error": msg})

    def _body(self):
        if getattr(self, "_raw", None) is not None: return self._raw
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY: raise ValueError("body too large")
        self._body_read = True
        self._raw = self.rfile.read(n) if n else b""
        return self._raw

    def _json_body(self):
        b = self._body()
        return json.loads(b.decode("utf-8")) if b else {}

    def _write_ok(self):
        return self._host_ok() and self.headers.get("X-Quark-Editor") == "1"

    def do_GET(self):
        if not self._host_ok(): return self._err(403, "bad host")
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/" or p == "/index.html": return self._static("index.html")
            if p.startswith("/web/"): return self._static(p[5:])
            if p == "/api/config": return self._send(200, self._config())
            if p == "/api/source":
                path = source_path(q.get("file", ""))
                if not path: return self._err(404, "no such source file")
                with open(path, encoding="utf-8", errors="replace") as f:
                    return self._send(200, f.read(), MIME[".txt"], extra={"X-Source-Path": source_label(path)})
            if p.startswith("/references/"): return self._static(p[len("/references/"):], CFG.refs)
            if p == "/api/ping": return self._send(200, {"ok": True, "tool": "physics-editor"}, extra={"Access-Control-Allow-Origin": "*"})
            if p == "/api/maps": return self._send(200, {"maps": list_maps(), "defaultDir": CFG.default_maps_dir()})
            if p == "/api/map":
                path = map_path(q.get("name", ""), q.get("source", "default"))
                if not path: return self._err(404, "no such map")
                with open(path, encoding="utf-8", errors="replace") as f: return self._send(200, f.read(), MIME[".qmap"])
            if p == "/api/modes":
                res = list_files(CFG.modes_dir(), ".qmode", valid_mode)
                for m in res: m.update(mode_info(os.path.join(CFG.modes_dir(), m["name"] + ".qmode")))
                return self._send(200, {"modes": res, "dir": CFG.modes_dir()})
            if p == "/api/mode":
                n = q.get("name", "")
                if not valid_mode(n): return self._err(400, "bad name")
                t = read_mode_file(n)
                if t is None: return self._err(404, "no such custom mode")
                return self._send(200, t, MIME[".qmode"])
            if p == "/api/scenarios": return self._send(200, {"scenarios": list_files(CFG.scenarios_dir(), ".json"), "dir": CFG.scenarios_dir()})
            if p == "/api/scenario":
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                path = os.path.join(CFG.scenarios_dir(), n + ".json")
                if not os.path.isfile(path): return self._err(404, "no such scenario")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                path = os.path.join(CFG.state_dir(), n + ".json")
                if not os.path.isfile(path): return self._send(200, "null")
                with open(path, encoding="utf-8") as f: return self._send(200, f.read())
            if p == "/api/drive/ws": return self._websocket(q)
            m = re.match(r"^/api/drive/([0-9a-f]{12})/events$", p)
            if m: return self._events(m.group(1))
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_POST(self):
        self._body_read = False; self._raw = None
        if not self._write_ok(): return self._err(403, "writes need the X-Quark-Editor header from the page")
        try: self._body()          # (read now, whatever the endpoint: an unread body would break the keep-alive connection)
        except ValueError as ex: return self._err(413, str(ex))
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        try:
            if p == "/api/mode":
                # {physics, name?}: the movement keys written into the file as it is on disk now (merge_qmode);
                # {physics, from: "<other mode>"}: a new file made from that one's text (Save as), its custommode= renamed
                n = q.get("name", "")
                if not valid_mode(n): return self._err(400, "bad name (letters, digits, space, _ -; 40 at most)")
                body = self._json_body()
                phys = body.get("physics")
                if not isinstance(phys, dict): return self._err(400, "no physics")
                cur = read_mode_file(n)
                created = cur is None
                if created and body.get("from") and valid_mode(body["from"]):
                    src = read_mode_file(body["from"])
                    if src is not None:
                        cur = re.sub(r"^custommode=.*$", lambda _m: "custommode=" + n, src, count=1, flags=re.M)
                elif created and isinstance(body.get("baseText"), str) and body["baseText"].strip():
                    # an unsaved set opened from a .qmode file (Open file): its own lines kept, renamed
                    cur = re.sub(r"^custommode=.*$", lambda _m: "custommode=" + n, body["baseText"], count=1, flags=re.M)
                    if not re.search(r"^custommode=", cur, re.M): cur = "custommode=%s\n" % n + cur
                out = merge_qmode(cur, phys, n)
                path = os.path.join(CFG.modes_dir(), n + ".qmode")
                atomic_write(path, out.encode("utf-8"))
                return self._send(200, {"ok": True, "path": path, "text": out, "created": created})
            if p == "/api/merge":   # the same merge without writing (the Code tab's preview of a save)
                body = self._json_body()
                return self._send(200, {"text": merge_qmode(body.get("text") or "", body.get("physics") or {}, body.get("name"))})
            if p == "/api/state":
                n = q.get("key", "")
                if not valid_name(n): return self._err(400, "bad key")
                body = self._body()
                json.loads(body.decode("utf-8") or "null")
                atomic_write(os.path.join(CFG.state_dir(), n + ".json"), body, backup=False)
                return self._send(200, {"ok": True})
            if p == "/api/scenario":
                n = q.get("name", "")
                if not valid_name(n): return self._err(400, "bad name")
                body = self._json_body()
                atomic_write(os.path.join(CFG.scenarios_dir(), n + ".json"), json.dumps(body, indent=1).encode("utf-8"))
                return self._send(200, {"ok": True})
            if p == "/api/generate-maps":
                d, msg = generate_default_maps()
                return self._send(200 if d else 500, {"dir": d, "log": msg})
            if p == "/api/helper":
                body = self._json_body()
                cmd = helper_cmd(body.get("args") or [])
                if not cmd: return self._err(501, "quark_maptool not found (or bad args)")
                timeout = min(600.0, float(body.get("timeout") or 120))
                ans = WORKERS.run(body["args"], body.get("request") or {}, timeout) if not os.environ.get("QPE_NO_WORKER") else None
                if ans is not None: return self._send(200, ans, "application/json")
                t0 = time.time()
                r = subprocess.run(cmd, input=json.dumps(body.get("request") or {}), capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=timeout,
                                   creationflags=subprocess.CREATE_NO_WINDOW if IS_WIN else 0)
                return self._send(200, {"code": r.returncode, "answer": parse_answer(r.stdout), "stderr": r.stderr[-4000:],
                                        "wallMs": round((time.time() - t0) * 1000, 1)})
            if p == "/api/drive":   # the SSE fallback: start; the page reads /events and POSTs /control
                body = self._json_body()
                cmd = helper_cmd(["physdrive", "--stream"])
                if not cmd: return self._err(501, "quark_maptool not found")
                d = Drive(cmd, body.get("request") or {})
                add_drive(d)
                return self._send(200, {"id": d.id})
            m = re.match(r"^/api/drive/([0-9a-f]{12})/(control|stop)$", p)
            if m:
                d = DRIVES.get(m.group(1))
                if not d: return self._err(404, "no such drive")
                if m.group(2) == "stop":
                    d.stop()
                    return self._send(200, {"ok": True})
                line = (self._json_body().get("line") or "").strip()
                if not control_ok(line): return self._err(400, "bad control line")
                return self._send(200, {"ok": d.send(line)})
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            pass
        except subprocess.TimeoutExpired:
            self._err(504, "helper timed out")
        except ValueError as ex:
            self._err(400, str(ex))
        except Exception as ex:
            try: self._err(500, str(ex))
            except Exception: pass

    def do_PUT(self): return self.do_POST()

    def _config(self):
        return {"dataDir": CFG.data_dir, "modes": CFG.modes_dir(), "scenarios": CFG.scenarios_dir(), "userMaps": CFG.user_maps_dir(),
                "defaultMaps": CFG.default_maps_dir(), "maptool": CFG.maptool, "platform": sys.platform, "port": CFG.port,
                "installed": not os.path.isfile(os.path.join(ROOT, "CMakeLists.txt")), "references": os.path.isfile(os.path.join(CFG.refs, "index.json")),
                "source": bool(source_path("pmove.cpp"))}

    def _static(self, rel, root=WEB):
        rel = rel.split("?", 1)[0]
        full = os.path.normpath(os.path.join(root, rel))
        if not full.startswith(os.path.normpath(root) + os.sep) or not os.path.isfile(full):
            return self._err(404, "not found")
        ext = os.path.splitext(full)[1].lower()
        with open(full, "rb") as f: data = f.read()
        self._send(200, data, MIME.get(ext, "application/octet-stream"))

    # --- SSE fallback
    def _events(self, did):
        d = DRIVES.get(did)
        if not d: return self._err(404, "no such drive")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        i = d.base
        while True:
            with d.cond:
                while i - d.base >= len(d.lines) and not d.done:
                    if not d.cond.wait(timeout=10): break
                i = max(i, d.base)
                batch = d.lines[i - d.base:]
                done = d.done and not batch
            if batch:
                self.wfile.write(("".join("data: %s\n" % l for l in batch) + "\n").encode("utf-8"))
                i += len(batch)
            else:
                self.wfile.write(b": keepalive\n\n")
            self.wfile.flush()
            if done:
                self.wfile.write(("event: exit\ndata: %s\n\n" % json.dumps({"code": d.code, "stderr": d.err[-2000:]})).encode("utf-8"))
                self.wfile.flush()
                return

    # --- WebSocket (RFC 6455, text frames only): the page's lines in, the helper's lines out
    def _websocket(self, q):
        if (self.headers.get("Upgrade") or "").lower() != "websocket" or not self.headers.get("Sec-WebSocket-Key"):
            return self._err(400, "websocket only")
        if not self._origin_ok(): return self._err(403, "bad origin")
        if not CFG.maptool: return self._err(501, "quark_maptool not found")
        key = self.headers["Sec-WebSocket-Key"].strip()
        acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", acc)
        self.end_headers()
        self.wfile.flush()
        self.close_connection = True
        sock = self.connection
        try: sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # (no Nagle delay on the small answer frames)
        except OSError: pass
        slock = threading.Lock()

        def send_text(s):
            data = s.encode("utf-8")
            n = len(data)
            hdr = bytes([0x81, n]) if n < 126 else (bytes([0x81, 126]) + struct.pack(">H", n) if n < 65536 else bytes([0x81, 127]) + struct.pack(">Q", n))
            with slock: sock.sendall(hdr + data)

        closed = []

        def send_close():   # (once: a second close frame is a protocol error the browser logs)
            try:
                with slock:
                    if closed: return
                    closed.append(1)
                    sock.sendall(b"\x88\x00")
            except OSError: pass

        drive = None
        try:
            while True:
                op, payload = self._ws_read()
                if op is None or op == 8: break
                if op == 9:   # ping -> pong
                    with slock: sock.sendall(bytes([0x8A, len(payload)]) + payload)
                    continue
                if op != 1: continue
                line = payload.decode("utf-8", "replace").strip()
                if drive is None:
                    try: first = json.loads(line)
                    except ValueError: break
                    cmd = helper_cmd(["physdrive", "--stream"])
                    drive = Drive(cmd, first)
                    add_drive(drive)

                    def sink(l):
                        if l is None:
                            send_text(json.dumps({"type": "_exit", "code": drive.code, "stderr": drive.err[-2000:]}))
                            send_close()
                            return
                        send_text(l)
                    send_text(json.dumps({"type": "_ready", "id": drive.id}))
                    drive.attach(sink)
                    continue
                if not control_ok(line): continue
                if not drive.send(line):
                    send_text(json.dumps({"type": "_exit", "code": drive.code, "stderr": drive.err[-2000:]}))
                    break
        except (OSError, ValueError):
            pass
        finally:
            if drive: drive.sink = None; drive.stop()
            send_close()

    def _ws_read(self):
        rf = self.rfile
        h = rf.read(2)
        if len(h) < 2: return None, b""
        op, ln, masked = h[0] & 0x0F, h[1] & 0x7F, h[1] & 0x80
        if ln == 126: ln = struct.unpack(">H", rf.read(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", rf.read(8))[0]
        if ln > MAX_BODY: return None, b""
        mask = rf.read(4) if masked else b"\0\0\0\0"
        data = bytearray(rf.read(ln))
        for i in range(len(data)): data[i] ^= mask[i & 3]
        # (fragmented messages: the page never sends them; a continuation is appended)
        if not (h[0] & 0x80):
            op2, more = self._ws_read()
            return op, bytes(data) + more
        return op, bytes(data)


# ---------------------------------------------------------------- install

def checkout_maptool():
    """quark_maptool from the checkout this runs from (its build folders), or the current folder's (the Quark Project
    app runs --install with cwd = a checkout); never PATH or an installed copy."""
    roots = []
    for r in (ROOT, os.getcwd()):
        r = os.path.abspath(r)
        if os.path.isfile(os.path.join(r, "CMakeLists.txt")) and r not in roots: roots.append(r)
    for r in roots:
        for b in ("build-editors", "build", "build-release", "build/Release", "build/RelWithDebInfo", "build/Debug", "build-sim", "bin"):
            c = os.path.join(r, b, "quark_maptool" + EXE)
            if os.path.isfile(c): return c
    return None


def install(dest):
    """Copy the program to `dest`. Non-interactive and safe to re-run: only the program files are replaced
    (physics_editor.json, the data folder and anything else there are kept); quark_maptool is copied from the
    checkout's build folders when one is built, else the copy already installed (if any) is kept."""
    dest = os.path.abspath(dest)
    if os.path.normcase(dest) == os.path.normcase(HERE):
        print("physics editor: already installed here (%s)" % dest); return 0
    try:
        os.makedirs(dest, exist_ok=True)
        for n in PROGRAM_FILES:
            src = os.path.join(HERE, n)
            if os.path.isfile(src): shutil.copyfile(src, os.path.join(dest, n))
        if not IS_WIN and os.path.isfile(os.path.join(dest, "run.sh")): os.chmod(os.path.join(dest, "run.sh"), 0o755)
        # web/: copied over what is there (a stale file left from an older version is harmless: nothing links it)
        shutil.copytree(WEB, os.path.join(dest, "web"), dirs_exist_ok=True, ignore=shutil.ignore_patterns("node_modules", "*.png", "test"))
        if os.path.isdir(REFS):   # other games' values (Values › Compare with games)
            shutil.copytree(REFS, os.path.join(dest, "references"), dirs_exist_ok=True)
        os.makedirs(os.path.join(dest, "source"), exist_ok=True)   # the movement code the Code tab shows
        for n, rel in SOURCE_FILES.items():
            src = os.path.join(ROOT, rel)
            if os.path.isfile(src): shutil.copyfile(src, os.path.join(dest, "source", n))
        mt = checkout_maptool()
        have = os.path.join(dest, "quark_maptool" + EXE)
        if mt:
            tmp = have + ".new"
            shutil.copyfile(mt, tmp)
            if not IS_WIN: os.chmod(tmp, 0o755)
            try: os.replace(tmp, have)
            except OSError as ex:   # (Windows: the installed one is running; keep it, say so)
                try: os.remove(tmp)
                except OSError: pass
                print("physics editor: quark_maptool in use, kept the installed one (%s)" % ex)
            if IS_WIN:   # a MinGW build's runtime DLLs, if it has any next to it
                for f in os.listdir(os.path.dirname(mt)):
                    if f.lower().endswith(".dll") and f.lower().startswith(("libgcc", "libstdc", "libwinpthread")):
                        try: shutil.copyfile(os.path.join(os.path.dirname(mt), f), os.path.join(dest, f))
                        except OSError: pass
            maptool_note = "quark_maptool from %s" % mt
        elif os.path.isfile(have):
            maptool_note = "no quark_maptool built in this checkout: kept the installed one"
        else:
            maptool_note = "no quark_maptool built yet: the page uses its MOCK physics until one is installed"
        dm = CFG.default_maps_dir() if CFG else None
        md = os.path.join(dest, "maps")
        if dm and os.path.normcase(os.path.abspath(dm)) != os.path.normcase(md):
            os.makedirs(md, exist_ok=True)
            for f in os.listdir(dm):
                if f.endswith(".qmap"): shutil.copyfile(os.path.join(dm, f), os.path.join(md, f))
    except OSError as ex:
        print("physics editor: install to %s failed: %s" % (dest, ex))
        return 1
    print("physics editor installed in %s (%s)" % (dest, maptool_note))
    return 0


def main():
    global CFG
    ap = argparse.ArgumentParser(description="Quark physics editor (local server)")
    ap.add_argument("--port", type=int)
    ap.add_argument("--data-dir", help="the game's user data folder (default: QUARK_DATA_DIR, else %%APPDATA%%\\quark or ~/.local/share/quark)")
    ap.add_argument("--maptool", help="path to quark_maptool (default: next to serve.py, then this repository's build/, then PATH)")
    ap.add_argument("--maps-dir", action="append", help="another folder of .qmap files to list (repeatable)")
    ap.add_argument("--references", help="the folder of other games' movement values (default: references/ next to serve.py)")
    ap.add_argument("--no-browser", "--no-open", dest="no_browser", action="store_true",
                    help="don't open a browser (also when QUARK_SHELL=1: the Quark Project app shows the page)")
    ap.add_argument("--install", nargs="?", const="", metavar="FOLDER", help="copy the tool to FOLDER (default <Quark folder>/physics-editor) and stop")
    args = ap.parse_args()
    CFG = Config(args)
    if args.install is not None:
        sys.exit(install(args.install or os.path.join(default_quark(), "physics-editor")))
    try:
        srv = ThreadingHTTPServer(("127.0.0.1", CFG.port), Handler)
    except OSError as ex:
        print("can't listen on 127.0.0.1:%d (%s): is the physics editor already running? (another port: --port or physics_editor.json)" % (CFG.port, ex))
        sys.exit(1)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % CFG.port
    print("Quark Physics Editor on %s" % url)
    print("  data dir     %s" % CFG.data_dir)
    print("  custom modes %s" % CFG.modes_dir())
    print("  maps         %s" % CFG.default_maps_dir())
    print("  maptool      %s" % (CFG.maptool or "(not found: the page uses its MOCK physics)"))
    sys.stdout.flush()
    if not args.no_browser and os.environ.get("QUARK_SHELL") != "1":
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        for d in list(DRIVES.values()):
            try: d.proc.kill()
            except Exception: pass
        WORKERS.stop()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Install or update the Quark gamemode editor into a folder (for the Supervisor Board).

    python3 tools/mode_editor/install.py "<Quark>/gamemode-editor"        (run from a repository checkout)

Non-interactive, safe to run again, opens no browser. It copies the tool's program files (serve.py, run.cmd, run.sh,
README.md, mode_editor.example.json, web/**) into the target, plus the repository's built-in mode texts
(tools/modes/*.qlogic -> <target>/modes/) when they exist, and removes program files an older install left that this
version no longer has. It never touches what the user owns there: mode_editor.json, provenance.json (merged: an item
already in the target wins), .cache/ and the user's modes (those live in the game's data folder, not here). It writes
installed_from.json ({repo, commit, date}) so the installed serve.py finds this checkout's build/ (quark_tests,
quark_maptool). Exit code 0 on success; the last line printed is a one-line JSON summary.
"""
import json, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAM = ["serve.py", "run.cmd", "run.sh", "README.md", "mode_editor.example.json", "install.py", "snapshot_catalog.py", ".gitignore"]
USER_OWNED = {"mode_editor.json", "provenance.json", "installed_from.json", ".cache", "__pycache__"}
MANIFEST = ".installed_files.json"


def repo_root():
    """The checkout this script is in (or the current directory, which the Board sets to a checkout)."""
    for c in (os.path.normpath(os.path.join(HERE, "..", "..")), os.getcwd()):
        if os.path.isfile(os.path.join(c, "CMakeLists.txt")) and os.path.isdir(os.path.join(c, "tools", "mode_editor")): return c
    return os.path.normpath(os.path.join(HERE, "..", ".."))


def files_to_copy(root):
    """[(source, relative target)]: the program files, web/** (no tests' scratch), tools/modes/*.qlogic."""
    out = [(os.path.join(HERE, f), f) for f in PROGRAM if os.path.isfile(os.path.join(HERE, f))]
    web = os.path.join(HERE, "web")
    for d, dirs, files in os.walk(web):
        dirs[:] = sorted(x for x in dirs if not x.startswith(".") and x != "node_modules" and x != "out")
        for f in sorted(files):
            if f.startswith(".") or f.endswith((".bak", ".part")): continue
            src = os.path.join(d, f)
            out.append((src, os.path.relpath(src, HERE)))
    modes = os.path.join(root, "tools", "modes")
    if os.path.isdir(modes):
        for f in sorted(os.listdir(modes)):
            if f.endswith((".qlogic", ".md")): out.append((os.path.join(modes, f), os.path.join("modes", f)))
    return out


def same(a, b):
    if not os.path.isfile(b) or os.path.getsize(a) != os.path.getsize(b): return False
    with open(a, "rb") as fa, open(b, "rb") as fb: return fa.read() == fb.read()


def copy_atomic(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".part"
    shutil.copyfile(src, tmp)
    shutil.copymode(src, tmp)
    os.replace(tmp, dst)


def merge_provenance(src, dst):
    """Items the target lacks come from the repository's provenance.json; the target's own items are kept as they are."""
    if not os.path.isfile(src): return False
    try:
        with open(src, encoding="utf-8") as f: a = json.load(f)
    except (OSError, ValueError):
        return False
    b = {}
    if os.path.isfile(dst):
        try:
            with open(dst, encoding="utf-8") as f: b = json.load(f)
        except (OSError, ValueError):
            return False   # (an unreadable target file is the user's: leave it alone)
    items = dict(a.get("items") or {})
    items.update(b.get("items") or {})
    doc = dict(a); doc.update(b); doc["items"] = dict(sorted(items.items()))
    doc.setdefault("format", 1); doc.setdefault("tool", "mode-editor")
    if doc == b: return False
    tmp = dst + ".part"
    with open(tmp, "w", encoding="utf-8") as f: f.write(json.dumps(doc, indent=1) + "\n")
    os.replace(tmp, dst)
    return True


def main():
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 2
    target = os.path.abspath(sys.argv[1])
    root = repo_root()
    if os.path.normcase(target) == os.path.normcase(HERE):
        print("the target is this source folder: nothing to do")
        return 2
    os.makedirs(target, exist_ok=True)
    copied, kept, removed = [], 0, []
    wanted = files_to_copy(root)
    rels = set()
    for src, rel in wanted:
        rels.add(rel.replace(os.sep, "/"))
        dst = os.path.join(target, rel)
        if same(src, dst): kept += 1; continue
        copy_atomic(src, dst)
        copied.append(rel.replace(os.sep, "/"))
    # files the previous install put there that this version doesn't have
    man = os.path.join(target, MANIFEST)
    old = []
    if os.path.isfile(man):
        try:
            with open(man, encoding="utf-8") as f: old = json.load(f).get("files") or []
        except (OSError, ValueError):
            old = []
    for rel in old:
        if rel in rels or rel.split("/")[0] in USER_OWNED or ".." in rel.split("/"): continue
        p = os.path.join(target, *rel.split("/"))
        if os.path.isfile(p):
            os.remove(p); removed.append(rel)
            d = os.path.dirname(p)
            while d != target and os.path.isdir(d) and not os.listdir(d): os.rmdir(d); d = os.path.dirname(d)
    with open(man + ".part", "w", encoding="utf-8") as f: json.dump({"files": sorted(rels)}, f, indent=1)
    os.replace(man + ".part", man)
    prov = merge_provenance(os.path.join(HERE, "provenance.json"), os.path.join(target, "provenance.json"))
    commit = None
    try:
        r = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=10)
        if r.returncode == 0: commit = r.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    info = {"repo": root, "commit": commit, "date": time.strftime("%Y-%m-%d %H:%M:%S")}
    with open(os.path.join(target, "installed_from.json.part"), "w", encoding="utf-8") as f: json.dump(info, f, indent=1)
    os.replace(os.path.join(target, "installed_from.json.part"), os.path.join(target, "installed_from.json"))
    try: os.chmod(os.path.join(target, "run.sh"), 0o755)
    except OSError: pass
    for rel in copied: print("  updated %s" % rel)
    for rel in removed: print("  removed %s" % rel)
    print(json.dumps({"ok": True, "target": target, "repo": root, "commit": commit, "updated": len(copied), "unchanged": kept,
                      "removed": len(removed), "provenanceMerged": prov}))
    return 0


if __name__ == "__main__":
    sys.exit(main())

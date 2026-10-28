"""Quark Project became Quarkology (2026-10-27): q's files move from Quark\\project-manager\\ to Quark\\quarkology\\.

Two steps, both automatic (SPEC.md section 4, "Moving from Quark Project"):
  1. `copy_old(paths)`, run by `server.py --init` (setup.ps1, and every self-update) before it writes any default,
     while the old app may still be open: only when Quark\\project-manager\\ exists and Quark\\quarkology\\migrated.json
     does not, so it runs once and a second run changes nothing. It copies everything in the old folder but its code
     (CODE) and an Electron update's leftovers (SKIP): tabs.json, config.json, the history, the Play sessions and
     notes, installs-state.json (so no tool is reinstalled), logs\\, the checklist ticks, electron\\ and a staged
     electron.next\\ (no 110 MB download). It never overwrites a file the new folder already has, never deletes or
     changes anything in the old folder except writing MOVED.txt there, and records what it copied in migrated.json.
     An Electron folder is copied into <name>.migrating\\ and renamed only once every file came over (the old app runs
     from it during the copy; half an Electron would not start). After a failed copy no marker is written, and the
     next --init copies what is still missing.
  2. `sync_old(paths)`, run once by the server after it has bound its port (so the old app's server is gone): a file
     the old app changed after step 1 (a play note written, a tab dragged) is copied again, unless Quarkology has
     changed its copy since; a file the old app made after step 1 is copied too.
The shell's own data (%LOCALAPPDATA%\\QuarkProject) is copied by shell/main.js on its first start, when the old
Electron has closed (a live Chromium profile cannot be copied).
"""
import os
import shutil

from . import util

MARKER = "migrated.json"
MOVED = "MOVED.txt"
# the old folder's code (setup.ps1 / the self-update copy their own), never q's
CODE = {"server.py", "pm", "qo", "web", "shell", "project-manager.cmd", "project-manager-hidden.vbs", "quarkology.cmd",
        "quarkology-hidden.vbs", "installs.json", "quark.ico", MOVED, MARKER, "__pycache__"}
# an Electron update's leftovers (shell/electron-swap.ps1, qo/updater.py): electron.old\ is only a rollback copy
SKIP = {"electron.old", "electron.bad", "electron.download", "electron.next.tmp", "electron.new", "electron.migrating",
        "electron.next.migrating"}
WHOLE = ("electron", "electron.next")
MOVED_TEXT = """Quark Project is now called Quarkology (2026-10-27).

The app moved to %s (desktop shortcut "Quarkology"); your tabs, settings, history, Play notes and sign-ins went
with it. This folder is kept, untouched, for one more release in case anything is missing; nothing here is used now
(the old shortcut and project-manager.cmd start Quarkology).
"""


def _stat(path):
    st = os.stat(path)
    return [st.st_size, st.st_mtime_ns]


def _copy_missing(src, dst, rel, files, errors):
    """Copy the files of src that dst lacks (recursively); each copied file goes in files[rel path]."""
    for root, dirs, names in os.walk(src):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        sub = os.path.relpath(root, src)
        for n in names:
            if n.endswith((".pyc", ".quark-new")):
                continue
            r = os.path.normpath(os.path.join(rel, sub, n))
            s, d = os.path.join(root, n), os.path.normpath(os.path.join(dst, sub, n))
            if os.path.exists(d):
                continue
            try:
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
                files[r] = _stat(s) + _stat(d)
            except OSError as e:
                errors.append("%s: %s" % (r, e.strerror or e))


def _copy_whole(src, dst):
    """src -> dst in one piece: into dst.migrating\\ first, renamed when complete (OSError: nothing at dst)."""
    tmp = dst + ".migrating"
    shutil.rmtree(tmp, ignore_errors=True)
    try:
        shutil.copytree(src, tmp)
        os.replace(tmp, dst)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _copy_top(old, new, copied, errors, files, whole):
    """Everything of q's in the old folder that the new one lacks (whole=False: the Electron folders are left out)."""
    for name in sorted(os.listdir(old)):
        if name in CODE or name in SKIP or name.endswith((".quark-new", ".pyc")):
            continue
        s, d = os.path.join(old, name), os.path.join(new, name)
        if os.path.isdir(s):
            if name in WHOLE:
                if whole and not os.path.exists(d):
                    try:
                        _copy_whole(s, d)
                        copied.append(name + "\\")
                    except (OSError, shutil.Error) as e:
                        errors.append("%s\\: %s" % (name, getattr(e, "strerror", None) or e))
                continue
            n = len(files)
            _copy_missing(s, d, name, files, errors)
            if len(files) > n:
                copied.append("%s\\ (%d files)" % (name, len(files) - n))
        elif os.path.isfile(s) and not os.path.exists(d):
            try:
                shutil.copy2(s, d)
                files[name] = _stat(s) + _stat(d)
                copied.append(name)
            except OSError as e:
                errors.append("%s: %s" % (name, e.strerror or e))


def copy_old(paths):
    """Step 1. -> None when there is nothing to do, else {"copied": [names], "errors": [text], "done": bool}."""
    old, new = paths.old_pm_dir, paths.pm_dir
    if not os.path.isdir(old) or os.path.realpath(old) == os.path.realpath(new):
        return None
    marker = os.path.join(new, MARKER)
    if os.path.exists(marker):
        return None
    os.makedirs(new, exist_ok=True)
    copied, errors, files = [], [], {}
    _copy_top(old, new, copied, errors, files, whole=True)
    try:
        with open(os.path.join(old, MOVED), "w", encoding="utf-8") as f:
            f.write(MOVED_TEXT % new)
    except OSError:
        pass
    if not errors:
        # (a later --init that finds files still missing would add them: so the marker is written only when complete)
        util.write_json_atomic(marker, {"from": old, "copiedAt": util.now_iso(), "files": files, "syncedAt": None})
    return {"copied": copied, "errors": errors, "done": not errors}


def sync_old(paths):
    """Step 2, once. -> the files copied again ([] when none), or None when there was nothing to do."""
    marker = os.path.join(paths.pm_dir, MARKER)
    try:
        m = util.read_json_file(marker)
    except (OSError, ValueError):
        return None
    if not isinstance(m, dict) or m.get("syncedAt") or not isinstance(m.get("files"), dict) \
            or not os.path.isdir(paths.old_pm_dir):
        return None
    again = []
    for rel, rec in sorted(m["files"].items()):
        if not (isinstance(rec, list) and len(rec) == 4):
            continue
        s, d = os.path.join(paths.old_pm_dir, rel), os.path.join(paths.pm_dir, rel)
        try:
            src_now, dst_now = _stat(s), _stat(d)
        except OSError:
            continue
        # (changed in the old folder since step 1, and Quarkology's copy is still the one step 1 made)
        if src_now != rec[:2] and dst_now == rec[2:]:
            try:
                shutil.copy2(s, d)
                again.append(rel)
            except OSError:
                pass
    # (and what the old app wrote for the first time after step 1: a new log, the first play note)
    added = {}
    _copy_top(paths.old_pm_dir, paths.pm_dir, again, [], added, whole=False)
    m["files"].update(added)
    m["syncedAt"] = util.now_iso()
    m["syncedAgain"] = again
    try:
        util.write_json_atomic(marker, m)
    except OSError:
        pass
    return again

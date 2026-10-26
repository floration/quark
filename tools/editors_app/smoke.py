#!/usr/bin/env python3
"""Smoke test of a staged Quarkology Editors (export.py --out DIR): starts every editor server from the bundle the way
main.js does (cwd = bundle, QUARK_SHELL=1, QUARK_DATA_DIR = a temp folder) with this Python, and checks each answers
its page; checks every static page exists. Exit 0 = all up.   python3 tools/editors_app/smoke.py DIR"""
import json, os, socket, subprocess, sys, tempfile, time, urllib.request

stage = os.path.abspath(sys.argv[1])
bundle = os.path.join(stage, "bundle")
tabs = json.load(open(os.path.join(stage, "tabs.json")))["tabs"]
saves = tempfile.mkdtemp(prefix="quarkology-editors-")
env = dict(os.environ, QUARK_SHELL="1", QUARK_DATA_DIR=saves, PYTHONDONTWRITEBYTECODE="1")
procs, bad = [], []
for t in tabs:
    if "page" in t:
        if not os.path.isfile(os.path.join(bundle, t["page"])): bad.append(t["id"] + ": no page " + t["page"])
        continue
    s = socket.socket(); s.bind(("127.0.0.1", 0)); t["_port"] = s.getsockname()[1]; s.close()
    procs.append((t, subprocess.Popen([sys.executable, "-B", os.path.join(bundle, t["serve"]), "--port", str(t["_port"])] + t.get("args", []),
                                      cwd=bundle, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)))
try:
    for t, p in procs:
        url, ok, end = "http://127.0.0.1:%d/?embed=1" % t["_port"], False, time.time() + 30
        while time.time() < end and p.poll() is None:
            try:
                with urllib.request.urlopen(url, timeout=3) as r: ok = r.status == 200 and b"<" in r.read(4096)
                break
            except OSError: time.sleep(0.3)
        print("%-9s %s" % (t["id"], "up" if ok else "DOWN"))
        if not ok: bad.append(t["id"] + ": no page at " + url + " " + (p.stderr.read().decode()[-800:] if p.poll() is not None else ""))
finally:
    for _, p in procs: p.kill()
if bad: sys.exit("smoke: FAILED\n  " + "\n  ".join(bad))
print("smoke: every editor up")

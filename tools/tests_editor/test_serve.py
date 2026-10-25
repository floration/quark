#!/usr/bin/env python3
"""Tests for the tests editor's server and parsers: python3 tools/tests_editor/test_serve.py

Parses the real repo (read only) and the fixture repo in test/repo, then runs the server against temp copies (never
writing into the repo itself): files (save / 409 / backup / revert / diff), the allowlist and path traversal, runs
with a fake quark_tests, the GitHub Manager proxy and CORS.
"""
import gzip, json, os, shutil, socket, stat, subprocess, sys, tempfile, threading, time, unittest, urllib.error, urllib.request
import http.server

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FIXTURE = os.path.join(HERE, "test", "repo")
sys.path.insert(0, HERE)
import model  # noqa: E402
import serve  # noqa: E402
from model import yamlsub, cpp  # noqa: E402

WIN = os.name == "nt"


def rd(p, mode="r", encoding=None):
    with open(p, mode, encoding=encoding) as f: return f.read()


def wr(p, text):
    with open(p, "w") as f: f.write(text)


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close()
    return p


class RealRepo(unittest.TestCase):
    """The real repo: groups, calls that resolve, workflow line numbers that match the file, lint."""

    @classmethod
    def setUpClass(cls):
        b = model.Builder(REPO)
        t0 = time.perf_counter()
        cls.m, cls.body, cls.etag = b.get()
        cls.cold_ms = (time.perf_counter() - t0) * 1000
        t0 = time.perf_counter(); b.get(force=True)
        cls.warm_ms = (time.perf_counter() - t0) * 1000
        print("\n  real repo model: cold %.0f ms, warm rebuild %.0f ms, %d KB" % (cls.cold_ms, cls.warm_ms, len(cls.body) // 1024))

    def suite(self, sid):
        return next(s for s in self.m["suites"] if s["id"] == sid)

    def test_groups(self):
        qt = self.suite("quark_tests")
        groups = [i for i in qt["items"] if i["kind"] == "group"]
        lists = {i["list"] for i in groups}
        self.assertIn("par", lists); self.assertIn("alone", lists)
        self.assertGreaterEqual(len(groups), 10)
        for g in groups:
            self.assertTrue(g["calls"], g["name"])
            self.assertEqual(g["run"]["parallel"], g["list"] == "par")
            for c in g["calls"]:
                self.assertIn(c["fn"], self.m["functions"], "%s calls %s" % (g["name"], c["fn"]))
        # the order of each list is its source order
        for l in lists:
            lines = [g["line"] for g in groups if g["list"] == l]
            self.assertEqual(lines, sorted(lines))
        par = next(l for l in qt["lists"] if l["name"] == "par")
        self.assertRegex(par["jobs"], r"^\d+ per core")                    # (main()'s jobs = N * cores, for the Blocks header)
        src = rd(os.path.join(REPO, "tests", "tests.cpp"), encoding="utf-8").split("\n")
        for g in groups:
            self.assertIn('"%s"' % g["name"], src[g["line"] - 1])

    def test_functions(self):
        fns = self.m["functions"]
        for name, f in fns.items():
            text = rd(os.path.join(REPO, *f["file"].split("/")), encoding="utf-8").split("\n")
            self.assertIn(name, text[f["line"] - 1], name)
            for c in f["checks"]:
                self.assertRegex(text[c["line"] - 1], r"\b\w*CHECK\s*\(", name)
            for c in f["calls"]: self.assertIn(c, fns)
        self.assertGreater(sum(len(f["checks"]) for f in fns.values()), 1000)

    def test_workflow_lines(self):
        ci = self.suite("ci:release.yml")
        text = rd(os.path.join(REPO, ".github", "workflows", "release.yml"), encoding="utf-8").split("\n")
        self.assertTrue(ci["items"])
        for j in ci["items"]:
            self.assertTrue(text[j["line"] - 1].startswith("  %s:" % j["jobId"]), j["jobId"])
            if j["lines"]["if"]: self.assertTrue(text[j["lines"]["if"] - 1].strip().startswith("if:"))
            if j["lines"]["needs"]: self.assertTrue(text[j["lines"]["needs"] - 1].strip().startswith("needs:"))
            self.assertTrue(text[j["lines"]["runsOn"] - 1].strip().startswith("runs-on:"))
            for s in j["steps"]:
                self.assertTrue(text[s["line"] - 1].lstrip().startswith("- "), (j["jobId"], s["name"]))
                if s["run"] and s["runLine"]:
                    first = s["run"].split("\n")[0]
                    self.assertIn(first.strip()[:30], text[s["runLine"] - 1])
        meta = next(j for j in ci["items"] if j["jobId"] == "meta")
        self.assertIn("sanitizers", meta["outputs"])
        trig = ci["sanitizerTrigger"]
        self.assertIsNotNone(trig)
        self.assertIn("case", text[trig["line"] - 1])
        self.assertIn("[final]", trig["text"])
        san = next((j for j in ci["items"] if j["jobId"] == "sanitizers"), None)
        if san: self.assertIn("[final]", san["ifText"])
        self.assertTrue(any(w["trigger"] == "push" for w in ci["when"]))

    def test_lint_and_rules(self):
        self.assertIsInstance(self.m["lint"], list)
        for l in self.m["lint"]:
            self.assertIn(l["level"], ("warn", "error"))
            self.assertTrue(l["file"] and l["text"])
        self.assertEqual([r["id"] for r in self.m["rules"]], ["ci-scope", "final", "local-before-commit"])
        ids = [s["id"] for s in self.m["suites"]]
        self.assertEqual(ids[0], "quark_tests")
        self.assertEqual(ids[-1], "ctest")

    def test_script_steps(self):
        # the real release_check.sh: each step named once, by what it does (not by its raw shell or a progress echo)
        names = [s["name"] for s in self.suite("script:release_check.sh")["items"]]
        self.assertEqual(names.count("alone groups, one at a time"), 1, names)
        self.assertEqual(len([n for n in names if n.startswith("parallel groups (")]), 1, names)
        self.assertIn("every map hosted headless + screenshotted (Xvfb)", names)
        for n in names: self.assertNotRegex(n, r"^(?:jobs=|echo |for |if )|\$", names)
        # quark_tests' "when": the steps that run the groups, not one whose comment mentions quark_tests
        rc = [w for w in self.suite("quark_tests")["when"] if w["trigger"] == "release-check"]
        self.assertEqual(len(rc), 1)
        self.assertNotIn("setup", rc[0]["text"])
        self.assertIn("parallel groups", rc[0]["text"])
        self.assertIn("alone groups", rc[0]["text"])

    def test_command_args(self):
        # an argument without a name of its own takes the parameter name of the function argv[K] is handed to
        cmds = {i["name"]: i for i in self.suite("quark_tests")["items"] if i["kind"] != "group"}
        names = lambda n: [p["name"] for p in cmds[n]["argsSpec"]["params"]]
        self.assertEqual(names("bot"), ["addr", "seconds", "name"])
        self.assertEqual(names("navbench"), ["only", "surfaces"])
        self.assertEqual(names("playtest"), ["seconds"])
        self.assertIn("choices", cmds["rules"]["argsSpec"])                 # (a NAME from a list keeps no name: the page says NAME)
        # the read-only aliases of the export commands: their own entries, runnable, FILE required
        for a, of in (("checkbrain", "exportbrain"), ("weaponsast", "exportweapons"), ("printmode", "exportmode")):
            self.assertEqual(cmds[a]["aliasOf"], of)
            self.assertNotIn("byHand", cmds[a]["run"])
            self.assertEqual((cmds[a]["argsSpec"]["min"], names(a)[0]), (1, "file"))
            self.assertIn("byHand", cmds[of]["run"])

    def test_all_workflows_parse(self):
        for s in self.m["suites"]:
            self.assertNotIn("error", s, s["id"])

    def test_speed(self):
        self.assertLess(self.warm_ms, 300)


class Fixture(unittest.TestCase):
    """The fixture repo: the awkward shapes."""

    @classmethod
    def setUpClass(cls):
        t = os.path.join(FIXTURE, "timing")
        cls.m, _, _ = model.Builder(FIXTURE, t, os.path.join(t, "overview.md")).get()
        cls.items = {i["id"]: i for i in cls.m["suites"][0]["items"]}

    def test_groups_and_calls(self):
        a, b, g, d = (self.items["group:" + n] for n in ("alpha", "beta", "gamma", "delta"))
        self.assertEqual([c["fn"] for c in a["calls"]], ["testPorts", "testOther"])
        self.assertEqual(a["calls"][1]["argsValue"], "TP(201)")
        self.assertEqual([c["fn"] for c in b["calls"]], ["testInNamespace", "testPorts", "launcherTests"])
        self.assertEqual((b["line"], b["endLine"]), (48, 53))
        self.assertEqual([c["fn"] for c in g["calls"]], ["testPorts", "testMissing"])
        self.assertTrue(a["run"]["parallel"]); self.assertFalse(d["run"]["parallel"])
        self.assertEqual(d["list"], "alone")
        self.assertEqual(a["timing"]["sec"], 2.0)                      # the step note's mean, over the baseline
        self.assertIn("never", b["flaky"])

    def test_shortcuts(self):
        self.assertEqual(self.items["cmd:braces"]["kind"], "shortcut")
        self.assertEqual(self.items["cmd:ports"]["kind"], "shortcut")     # (two ifs on one line)
        self.assertEqual(self.items["cmd:toolish"]["kind"], "tool")
        self.assertEqual(self.items["cmd:bench"]["kind"], "tool")
        self.assertNotIn("cmd:groups", self.items)

    def test_functions(self):
        f = self.m["functions"]
        self.assertEqual(f["testBraces"]["doc"].split("\n")[0], "Strings and comments with braces must not confuse the brace matching.")
        self.assertEqual(len(f["testBraces"]["checks"]), 2)
        self.assertEqual(f["testBraces"]["checks"][1]["expr"], 'std::string("}").size() == 1')
        self.assertEqual((f["testBraces"]["line"], f["testBraces"]["endLine"]), (19, 27))
        self.assertEqual(f["testPorts"]["doc"], "Block doc\nfor testPorts.")
        self.assertIn("testInNamespace", f)
        self.assertEqual(f["testOther"]["checks"][0]["macro"], "OTHERCHECK")
        self.assertEqual(f["testOther"]["calls"], ["testBraces"])
        self.assertEqual(f["testPorts"]["usedBy"], ["group:alpha", "group:beta", "group:gamma", "cmd:ports"])

    def test_lambda_calls_and_runs_via(self):
        d = self.items["group:delta"]
        self.assertEqual([(c["fn"], c.get("via")) for c in d["calls"]], [("testBraces", "timed")])   # (the wrapper left out)
        f = self.m["functions"]
        self.assertEqual(f["testBraces"]["usedBy"], ["group:delta", "cmd:braces"])
        via = {v["item"]: v["path"] for v in f["testBraces"]["runsVia"]}
        self.assertEqual(via["group:alpha"], ["testOther"])                 # (alpha -> testOther -> testBraces)
        self.assertEqual(via["cmd:named"], ["testNamed"])
        self.assertNotIn("group:delta", via)                                 # (direct there: usedBy)
        self.assertEqual(f["testNeverRun"]["runsVia"], [])

    def test_args_spec(self):
        n, b, s = self.items["cmd:named"], self.items["cmd:bench"], self.items["cmd:braces"]
        self.assertEqual((n["argsSpec"]["min"], n["argsSpec"]["max"]), (1, 1))
        self.assertEqual(n["argsSpec"]["choices"], ["one", "two"])
        self.assertEqual(n["argsSpec"]["hint"], "NAME: one of testNamed's")
        self.assertIsNone(self.items["cmd:toolish"]["argsSpec"]["hint"])    # (the comment is the next command's)
        self.assertEqual(b["argsSpec"], {"min": 0, "max": 1, "params": [{"name": "secs", "default": "15"}], "hint": "[secs]"})
        self.assertEqual((s["argsSpec"]["min"], s["argsSpec"]["max"]), (0, 0))

    def test_lint(self):
        rules = {(l["rule"], l["line"]) for l in self.m["lint"]}
        for want in [("ci-scope", 38), ("final", 39), ("needs", 41), ("missing-fn", 54), ("pin", 33), ("unused", 37),
                     ("script-list", 4), ("port", 54)]:
            self.assertIn(want, rules)
        self.assertFalse(any(l["rule"] == "final" and l["line"] < 30 for l in self.m["lint"]))   # (meta's case is allowed)
        self.assertFalse(any(l["rule"] == "ci-scope" and l["line"] == 45 for l in self.m["lint"]))  # (a tool command)

    def test_workflow(self):
        ci = self.m["suites"][1]
        self.assertEqual([w["trigger"] for w in ci["when"]], ["push", "tag", "workflow_dispatch"])
        jobs = {j["jobId"]: j for j in ci["items"]}
        self.assertEqual(set(jobs), {"meta", "build", "quoted-job"})
        self.assertEqual(jobs["build"]["matrix"], {"group": ["alpha", "beta"]})
        self.assertEqual(jobs["build"]["timeoutMin"], 30)
        self.assertEqual(jobs["meta"]["steps"][1]["name"], "a quoted: name # not a comment")
        self.assertEqual(jobs["meta"]["steps"][0]["with"], {"persist-credentials": False, "path": "a\nb"})
        self.assertEqual(jobs["build"]["steps"][1]["run"], "echo one echo two")
        self.assertEqual(jobs["build"]["ifText"], "not when a manual run with 'final' ticked")
        self.assertEqual(jobs["meta"]["comment"].split("\n"), ["the first job", "a comment line under the key"])
        self.assertIn("[final]", ci["sanitizerTrigger"]["text"])


class ParseLog(unittest.TestCase):
    """Group times and results from a run's output (serve.parse_log)."""

    def test_whole_suite(self):
        log = ("weapons out\n  FAIL /home/q/quark/tests/tests.cpp:120: a == b\n  (group weapons: 12 s)\n  (group weapons: 1 failure(s))\n"
               "  (group bots: 30 s)\n  FAIL C:\\q\\tests\\rules_tests.h:5: c\n  (group duel: 3 s)\n  FAIL duel: crashed (signal 11)\n"
               "  (group duel: 2 failure(s))\n  FAIL vote refused: x\n  (group votes: 4 s)\n1 FAILURE(S)\n")
        times, res = serve.parse_log(log)
        self.assertEqual(times, {"weapons": 12.0, "bots": 30.0, "duel": 3.0, "votes": 4.0})
        self.assertEqual(res["weapons"]["failLines"], [{"text": "FAIL /home/q/quark/tests/tests.cpp:120: a == b", "file": "tests/tests.cpp", "line": 120}])
        self.assertEqual((res["bots"]["status"], res["duel"]["status"], res["duel"]["fails"]), ("passed", "failed", 2))
        self.assertEqual([x.get("file") for x in res["duel"]["failLines"]], ["tests/rules_tests.h", None])
        self.assertEqual(res["votes"]["status"], "failed")                   # (a FAIL line without a file is still one)
        _, res = serve.parse_log("  FAIL tests/r.h:1: x\n  FAIL tests/r.h:2: y\n  (group tickrate: 5 s)\n  (group tickrate: 1 failure(s))\n")
        self.assertEqual((res["tickrate"]["fails"], len(res["tickrate"]["failLines"])), (1, 2))   # (timingTest's set-aside FAILs print too)

    def test_one_group_and_release_check(self):
        times, res = serve.parse_log("group bots: 3 s\nall tests passed\n", "bots")
        self.assertEqual((times, res["bots"]["status"]), ({"bots": 3.0}, "passed"))
        times, res = serve.parse_log("bots rc=0 group bots: 9 s\nduel rc=1 group duel: 2 s\nmap x ok\n")
        self.assertEqual(times, {"bots": 9.0, "duel": 2.0})
        self.assertEqual((res["bots"]["status"], res["duel"]["status"]), ("passed", "failed"))


class Yaml(unittest.TestCase):
    def test_shapes(self):
        y = yamlsub.parse("on: [push]\nk: 'it''s'\nn: 3\nb: true\nlist:\n- a\n- b: 1\n  c: 2\nblk: |-\n  x\n\n  y\nkeep: |+\n  z\n\nafter: 1\n")
        self.assertEqual(y["on"], ["push"]); self.assertEqual(y["k"], "it's"); self.assertEqual(y["n"], 3)
        self.assertIs(y["b"], True); self.assertEqual(y["list"], ["a", {"b": 1, "c": 2}])
        self.assertEqual(y["blk"], "x\n\ny"); self.assertEqual(y["keep"], "z\n\n"); self.assertEqual(y["after"], 1)
        self.assertEqual(y.key_lines["after"], 16); self.assertEqual(y["list"].item_lines, [6, 7])
        self.assertEqual(y.scalar_lines["blk"], 10)

    def test_errors(self):
        with self.assertRaises(yamlsub.YamlError): yamlsub.parse("a: 'open\n")
        with self.assertRaises(yamlsub.YamlError): yamlsub.parse("a:\n  b: 1\n c: 2\n")

    def test_flow_and_expressions(self):
        y = yamlsub.parse('w: { a: "x, y", b: [1, 2], c: ${{ x }} }\n')
        self.assertEqual(y["w"], {"a": "x, y", "b": [1, 2], "c": "${{ x }}"})


def copy_repo(dst):
    """The files the editor reads, from the real repo, plus a git repo with one commit (for diff / revert)."""
    for d in ("tests", ".github/workflows", "tools/githooks"):
        src = os.path.join(REPO, *d.split("/"))
        if d == "tests":
            os.makedirs(os.path.join(dst, "tests"))
            for n in os.listdir(src):
                if n.endswith((".cpp", ".h")): shutil.copy2(os.path.join(src, n), os.path.join(dst, "tests", n))
        else:
            shutil.copytree(src, os.path.join(dst, *d.split("/")))
    for n in os.listdir(os.path.join(REPO, "tools")):
        if n.endswith(".sh"): shutil.copy2(os.path.join(REPO, "tools", n), os.path.join(dst, "tools", n))
    for n in ("CMakeLists.txt", "VERSION", "CLAUDE.md"): shutil.copy2(os.path.join(REPO, n), os.path.join(dst, n))
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
    for cmd in (["init", "-q"], ["config", "core.autocrlf", "false"], ["add", "-A"], ["commit", "-qm", "base"]):
        subprocess.run(["git", "-C", dst] + cmd, check=True, env=env, capture_output=True)


FAKE = r'''#!/usr/bin/env python3
import os, subprocess, sys, time
a = sys.argv[1:]
if a == ["groups"]:
    print("bots\nduel"); sys.exit(0)
if a == []:                               # the whole suite (QUARK_TEST_ONLY: just those)
    only = os.environ.get("QUARK_TEST_ONLY", "")
    print("only=" + only)
    gs = only.split(",") if only else ["bots", "duel"]
    if "bots" in gs: print("bots out\n  (group bots: 4 s)")
    if "duel" in gs: print("  FAIL /src/quark/tests/tests.cpp:12: x == y\n  (group duel: 2 s)\n  (group duel: 1 failure(s))")
    sys.exit(1 if "duel" in gs else 0)
if a[:1] == ["group"]:
    g = a[1]
    if g == "tourney":                    # long: a child process too (stop must kill the tree)
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        open(os.path.join(os.environ["TMPDIR"], "child.pid"), "w").write(str(child.pid))
        print("started", flush=True); time.sleep(60)
    print("running group %s" % g); print("group %s: 1 s" % g)
    print("tmp=" + os.environ.get("TMPDIR", ""))
    sys.exit(1 if g == "duel" else 0)
print("cmd", a); sys.exit(0)
'''


FAKE_GAME = r'''#!/usr/bin/env python3
import json, sys
a = sys.argv[1:]
if a[:1] == ["--uitest"]:                  # the UI test: a report, exit 0 = pass
    json.dump({"result": "pass", "widgets": 3, "passed": 3, "failed": 0, "untested": 0, "failures": []}, open(a[1], "w"))
    print("==== UI TEST: PASS ===="); sys.exit(0)
print("game", a); sys.exit(0)
'''

# release_check.sh as far as the maps go: a ok (with its screenshot and log), b failed
FAKE_RELEASE_CHECK = """#!/bin/bash
export TMPDIR="${TMPDIR:-/tmp}/quark_release_check.$$"; mkdir -p "$TMPDIR"
printf 'PNG' > "$TMPDIR/a.png"; echo "a hosted" > "$TMPDIR/a.map.log"; echo "b broke" > "$TMPDIR/b.map.log"
echo "map a ok"
echo "map b FAILED rc=1"
exit 1
"""


class Server(unittest.TestCase):
    """The HTTP server on a temp copy of the repo."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="tests_editor_")
        cls.repo = os.path.join(cls.tmp, "repo")
        os.makedirs(cls.repo)
        copy_repo(cls.repo)
        cls.state = os.path.join(cls.tmp, "state")
        cls.gm_port = free_port()
        cfg = os.path.join(cls.tmp, "tests_editor.json")
        with open(cfg, "w") as f: json.dump({"repo": "repo", "githubManager": "http://127.0.0.1:%d" % cls.gm_port, "timingDir": ""}, f)
        cls.port = free_port()
        args = serve.argparse.Namespace(repo=None, config=cfg, state_dir=cls.state, no_browser=True, snapshot=None,
                                        quiet=True, verbose=False, port=cls.port)
        cls.app = serve.App(args)
        assert cls.app.repo == cls.repo, cls.app.repo
        cls.srv = serve.Server(("127.0.0.1", cls.port), serve.make_handler(cls.app, cls.port))
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown(); cls.srv.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def req(self, path, body=None, headers=None, method=None):
        h = {"Host": "127.0.0.1:%d" % self.port}
        h.update(headers or {})
        data = json.dumps(body).encode() if body is not None else None
        if data: h["Content-Type"] = "application/json"
        r = urllib.request.Request("http://127.0.0.1:%d%s" % (self.port, path), data=data, headers=h, method=method)
        try:
            with urllib.request.urlopen(r, timeout=20) as resp:
                raw = resp.read()
                return resp.status, dict(resp.headers), raw
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def j(self, path, body=None, **kw):
        code, h, raw = self.req(path, body, **kw)
        return code, json.loads(raw.decode()) if raw else None

    def test_model_etag_gzip(self):
        code, h, raw = self.req("/api/model")
        self.assertEqual(code, 200)
        m = json.loads(raw)
        self.assertEqual(m["suites"][0]["id"], "quark_tests")
        code, _, _ = self.req("/api/model", headers={"If-None-Match": h["ETag"]})
        self.assertEqual(code, 304)
        code, h2, raw2 = self.req("/api/model", headers={"Accept-Encoding": "gzip"})
        self.assertEqual(h2.get("Content-Encoding"), "gzip")
        self.assertEqual(json.loads(gzip.decompress(raw2))["repo"]["path"], self.repo)
        code, s = self.j("/api/summary")
        self.assertGreater(s["groups"], 5)

    def test_file_roundtrip(self):
        rel = "tools/githooks/pre-commit"
        code, f = self.j("/api/file?path=" + rel)
        self.assertEqual(code, 200)
        mode = os.stat(os.path.join(self.repo, rel)).st_mode
        new = f["text"] + "# added by the test\n"
        code, r = self.j("/api/file", {"path": rel, "text": new, "baseSha256": f["sha256"]})
        self.assertEqual(code, 200); self.assertTrue(r["ok"]); self.assertIn("suites", r["model"])
        self.assertEqual(rd(os.path.join(self.repo, rel)), new)
        if not WIN: self.assertEqual(os.stat(os.path.join(self.repo, rel)).st_mode, mode)   # (still executable)
        code, r2 = self.j("/api/file", {"path": rel, "text": "x", "baseSha256": f["sha256"]})   # stale base
        self.assertEqual(code, 409); self.assertEqual(r2["error"], "changed"); self.assertEqual(r2["sha256"], r["sha256"])
        backups = os.listdir(os.path.join(self.state, "cache", "backups", "tools", "githooks", "pre-commit"))
        self.assertEqual(len(backups), 1)
        code, d = self.j("/api/diff?path=" + rel)
        self.assertIn("+# added by the test", d["diff"])
        code, d = self.j("/api/diff")
        self.assertIn("pre-commit", d["diff"])
        code, r3 = self.j("/api/revert", {"path": rel})
        self.assertEqual(code, 200)
        self.assertEqual(rd(os.path.join(self.repo, rel)), f["text"])
        self.assertEqual(len(os.listdir(os.path.join(self.state, "cache", "backups", "tools", "githooks", "pre-commit"))), 2)

    def test_crlf_kept_and_new_file(self):
        p = os.path.join(self.repo, "tests", "crlf.h")
        with open(p, "wb") as f: f.write(b"a\r\nb\r\n")
        code, f = self.j("/api/file?path=tests/crlf.h")
        self.assertEqual(f["text"], "a\nb\n"); self.assertEqual(f["eol"], "crlf")
        code, r = self.j("/api/file", {"path": "tests/crlf.h", "text": "a\nb\nc\n", "baseSha256": f["sha256"]})
        self.assertEqual(code, 200)
        self.assertEqual(rd(p, "rb"), b"a\r\nb\r\nc\r\n")
        code, r = self.j("/api/file", {"path": "tests/brand_new.cpp", "text": "// new\n", "baseSha256": None})
        self.assertEqual(code, 200)
        code, r = self.j("/api/file", {"path": "tests/brand_new.cpp", "text": "// again\n", "baseSha256": None})
        self.assertEqual(code, 409)
        os.remove(p); os.remove(os.path.join(self.repo, "tests", "brand_new.cpp"))

    def test_traversal_refused(self):
        bad = ["../x", "/etc/passwd", "tests/../CMakeLists.txt", "tests/./tests.cpp", "C:/x/tests.cpp", "tests\\tests.cpp",
               "src/main.cpp", ".github/workflows/../../VERSION", "tools/sub/x.sh", "tests//tests.cpp", "",
               "%2e%2e/x", "tests/tests.cpp\0", "VERSION"]
        for b in bad:
            code, r = self.j("/api/file?path=" + urllib.request.quote(b))
            self.assertEqual(code, 403, b)
            code, r = self.j("/api/file", {"path": b, "text": "x", "baseSha256": None})
            self.assertEqual(code, 403, b)
        if not WIN:
            outside = os.path.join(self.tmp, "outside.cpp")
            wr(outside, "secret")
            link = os.path.join(self.repo, "tests", "link.cpp")
            os.symlink(outside, link)
            try:
                self.assertEqual(self.j("/api/file?path=tests/link.cpp")[0], 403)
                self.assertEqual(self.j("/api/file", {"path": "tests/link.cpp", "text": "x", "baseSha256": None})[0], 403)
                self.assertEqual(rd(outside), "secret")
            finally:
                os.remove(link)

    def test_host_and_cors(self):
        code, h, _ = self.req("/api/summary", headers={"Origin": "http://127.0.0.1:8767"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8767")
        code, h, _ = self.req("/api/summary", headers={"Origin": "http://evil.example"})
        self.assertNotIn("Access-Control-Allow-Origin", h)
        code, h, _ = self.req("/api/summary", headers={"Origin": "http://127.0.0.1:8776"})
        self.assertNotIn("Access-Control-Allow-Origin", h)
        code, h, _ = self.req("/api/model", headers={"Origin": "http://localhost:8765"}, method="OPTIONS")
        self.assertEqual(code, 204); self.assertIn("POST", h.get("Access-Control-Allow-Methods", ""))
        code, _, _ = self.req("/api/summary", headers={"Host": "evil.example"})
        self.assertEqual(code, 403)
        code, r = self.j("/api/file", {"path": "tests/x.cpp", "text": "x"}, headers={"Origin": "http://evil.example"})
        self.assertEqual(code, 403)

    def test_static(self):
        code, h, raw = self.req("/")
        self.assertEqual(code, 200); self.assertIn("text/html", h["Content-Type"])
        self.assertEqual(self.req("/../serve.py")[0], 404)
        self.assertEqual(self.req("/api/nope")[0], 404)

    def test_ci_proxy(self):
        code, r = self.j("/api/ci")
        self.assertEqual(r, {"available": False, "jobs": {}})
        state = {"poll": {"lastOk": "2026-10-24T10:00:00Z"}, "runs": [
            {"id": 9, "workflow": "build-and-release", "branch": "testing", "updatedAt": "2026-10-24T10:00:00Z", "jobs": [
                {"name": "linux", "conclusion": "failure", "url": "u", "startedAt": "2026-10-24T09:50:00Z",
                 "completedAt": "2026-10-24T09:55:00Z", "failedSteps": ["build"]}]},
            {"id": 8, "workflow": "build-and-release", "jobs": [{"name": "linux", "conclusion": "success"}]}]}

        class GM(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                b = json.dumps(state).encode()
                self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
            def log_message(self, *a): pass
        gm = http.server.HTTPServer(("127.0.0.1", self.gm_port), GM)
        threading.Thread(target=gm.serve_forever, daemon=True).start()
        try:
            self.app.ci_cache = (0, None)
            code, r = self.j("/api/ci")
            self.assertTrue(r["available"])
            j = r["jobs"]["release.yml/linux"]
            self.assertEqual((j["conclusion"], j["runId"], j["durationSec"], j["failedSteps"]), ("failure", 9, 300, ["build"]))
        finally:
            gm.shutdown(); gm.server_close(); self.app.ci_cache = (0, None)

    @unittest.skipIf(WIN, "the fake quark_tests is a Python script (Linux / macOS)")
    def test_runs(self):
        b = os.path.join(self.repo, "build", "quark_tests")
        code, r = self.j("/api/run", {"cmd": "group:bots"})
        self.assertEqual((code, r), (404, {"error": "no build"}))
        os.makedirs(os.path.dirname(b), exist_ok=True)
        with open(b, "w") as f: f.write(FAKE.replace("#!/usr/bin/env python3", "#!" + sys.executable))
        os.chmod(b, 0o755)
        self.assertEqual(self.j("/api/run", {"cmd": "group:nope"})[0], 404)
        self.assertEqual(self.j("/api/run", {"cmd": "rm -rf /"})[0], 400)

        def wait(rid):
            for _ in range(200):
                code, st = self.j("/api/run?id=%s&from=0" % rid)
                if st["status"] != "running": return st
                time.sleep(0.05)
            self.fail("run never ended")

        code, r = self.j("/api/run", {"cmd": "group:bots"})
        self.assertEqual(code, 200)
        st = wait(r["id"])
        self.assertEqual((st["status"], st["exit"]), ("passed", 0))
        self.assertIn("running group bots", st["output"])
        tmpline = [l for l in st["output"].split("\n") if l.startswith("tmp=")][0]
        self.assertTrue(tmpline[4:].startswith(os.path.join(self.state, "cache", "tmp")))   # (a private TMPDIR)
        code, st2 = self.j("/api/run?id=%s&from=%d" % (r["id"], st["size"]))
        self.assertEqual(st2["output"], "")
        code, r = self.j("/api/run", {"cmd": "group:duel"})
        self.assertEqual(wait(r["id"])["status"], "failed")
        code, r = self.j("/api/run", {"cmd": "cmd:signing"})
        self.assertEqual(wait(r["id"])["status"], "passed")
        # timing from the local run feeds the model
        m = json.loads(self.req("/api/model")[2])
        bots = next(i for i in m["suites"][0]["items"] if i["id"] == "group:bots")
        self.assertTrue(bots["timing"]["source"].startswith("local run"))
        # stop: the run and its child are gone; one at a time
        code, r = self.j("/api/run", {"cmd": "group:tourney"})
        rid = r["id"]
        for _ in range(100):
            if "started" in self.j("/api/run?id=%s" % rid)[1]["output"]: break
            time.sleep(0.05)
        self.assertEqual(self.j("/api/run", {"cmd": "group:bots"})[0], 409)
        child = int(rd(os.path.join(self.state, "cache", "tmp", rid, "child.pid")))
        self.assertEqual(self.j("/api/run/stop", {"id": rid})[0], 200)
        st = wait(rid)
        self.assertEqual(st["status"], "stopped")
        for _ in range(100):
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                break
            except PermissionError:
                break
            try:
                if os.waitpid(child, os.WNOHANG)[0]: break
            except ChildProcessError:
                pass
            time.sleep(0.05)
        else:
            self.fail("the run's child process survived the stop")
        code, runs = self.j("/api/runs")
        self.assertEqual([x["cmd"] for x in runs[:4]], ["group:tourney", "cmd:signing", "group:duel", "group:bots"])
        self.assertEqual(json.loads(rd(os.path.join(self.state, "cache", "runs.json")))[-1]["status"], "stopped")
        # the whole suite / a chosen set (QUARK_TEST_ONLY): each group's result and time, in the model and groups.json
        code, r = self.j("/api/run", {"cmd": "suite", "groups": ["duel", "bots"]})
        st = wait(r["id"])
        self.assertEqual((st["status"], st["only"]), ("failed", ["duel", "bots"]))
        self.assertIn("only=duel,bots", st["output"])
        self.assertEqual(st["groupResults"]["duel"], {"status": "failed", "fails": 1})
        m = json.loads(self.req("/api/model")[2])
        g = {i["name"]: i for i in m["suites"][0]["items"] if i["kind"] == "group"}
        self.assertEqual((g["bots"]["lastRun"]["status"], g["bots"]["timing"]["sec"]), ("passed", 4.0))
        self.assertEqual(g["duel"]["lastRun"]["failLines"], [{"text": "FAIL /src/quark/tests/tests.cpp:12: x == y", "file": "tests/tests.cpp", "line": 12}])
        self.assertIsNone(g["tourney"]["lastRun"])                           # (stopped: no result)
        saved = json.loads(rd(os.path.join(self.state, "cache", "groups.json")))["groups"]
        self.assertEqual((saved["bots"]["sec"], saved["duel"]["status"]), (4.0, "failed"))
        self.assertEqual(self.j("/api/run", {"cmd": "suite", "groups": ["nope"]})[0], 404)
        self.assertEqual(self.j("/api/run", {"cmd": "group:bots", "groups": ["bots"]})[0], 400)
        # arguments: checked against the command's argsSpec; commands that write files are refused
        code, r = self.j("/api/run", {"cmd": "cmd:serverbench", "args": ["2", "1"]})
        self.assertEqual(code, 200)
        self.assertIn("'serverbench', '2', '1'", wait(r["id"])["output"])
        self.assertEqual(self.j("/api/run", {"cmd": "cmd:serverbench", "args": ["1", "2", "3"]})[0], 400)
        self.assertEqual(self.j("/api/run", {"cmd": "cmd:rules"})[0], 400)                   # (NAME is required)
        for bad in (["a b"], ["$(x)"], "overtime", [1]):
            self.assertEqual(self.j("/api/run", {"cmd": "cmd:rules", "args": bad})[0], 400, bad)
        code, r = self.j("/api/run", {"cmd": "cmd:rules", "args": ["nosuchrule"]})   # (not one of its names: refused)
        self.assertEqual(code, 400); self.assertIn("no such name nosuchrule", r["error"])
        code, r = self.j("/api/run", {"cmd": "cmd:rules", "args": ["overtime"]})
        self.assertIn("'rules', 'overtime'", wait(r["id"])["output"])
        self.assertEqual(self.j("/api/run", {"cmd": "cmd:exportassets"})[0], 403)
        self.assertEqual(self.j("/api/run", {"cmd": "cmd:exportbrain"})[0], 403)
        code, r = self.j("/api/run", {"cmd": "cmd:checkbrain", "args": ["tools/brains/x.brain"]})   # (its read-only alias runs)
        self.assertEqual(code, 200, r)
        self.assertIn("'checkbrain', 'tools/brains/x.brain'", wait(r["id"])["output"])
        self.assertEqual(self.j("/api/run", {"cmd": "cmd:checkbrain"})[0], 400)                 # (FILE is required)
        for bad in (["../x"], ["/etc/passwd"], ["C:/x"], ["a/../../b"]):
            self.assertEqual(self.j("/api/run", {"cmd": "cmd:checkbrain", "args": bad})[0], 400, bad)
        self.assertFalse(os.path.exists(os.path.join(self.repo, "assets_export")))
        # a log from a terminal run feeds the times too
        code, r = self.j("/api/import-log", {"text": "x\n  (group bots: 9 s)\n", "name": "pre-commit.log"})
        self.assertEqual((code, r["groups"]), (200, 1))
        m = json.loads(self.req("/api/model")[2])
        bots = next(i for i in m["suites"][0]["items"] if i["id"] == "group:bots")
        self.assertEqual(bots["timing"]["sec"], 9.0)
        self.assertTrue(bots["timing"]["source"].startswith("imported log"))
        os.remove(b)


    def test_script_runs(self):
        def wait(rid):
            for _ in range(400):
                code, st = self.j("/api/run?id=%s&from=0" % rid)
                if st["status"] != "running": return st
                time.sleep(0.05)
            self.fail("run never ended")
        game = os.path.join(self.repo, "build", "quark")
        if os.path.exists(game): os.remove(game)
        self.assertEqual(self.j("/api/run", {"cmd": "script:uitest.sh"}), (404, {"error": "no game build (build/quark): build quark first"}))
        os.makedirs(os.path.dirname(game), exist_ok=True)
        with open(game, "w") as f: f.write(FAKE_GAME.replace("#!/usr/bin/env python3", "#!" + sys.executable))
        os.chmod(game, 0o755)
        self.assertEqual(self.j("/api/env")[1]["quark"], "build/quark")
        # no display at all: uitest.sh skips itself (exit 77), and the run says skipped, not failed
        real, old_disp = serve.start_display, os.environ.pop("DISPLAY", None)
        serve.start_display = lambda: (None, None, None)
        try:
            code, r = self.j("/api/run", {"cmd": "script:uitest.sh"})
            st = wait(r["id"])
        finally:
            serve.start_display = real
            if old_disp is not None: os.environ["DISPLAY"] = old_disp
        self.assertEqual((st["status"], st["exit"]), ("skipped", 77), st["output"])
        self.assertIn("no display here", st["output"])
        # with a display (a private Xvfb here, when installed): quark --uitest <report> runs, the report is kept
        if not WIN and shutil.which("Xvfb"):
            code, r = self.j("/api/run", {"cmd": "script:uitest.sh"})
            st = wait(r["id"])
            self.assertEqual(st["status"], "passed", st["output"])
            self.assertRegex(st["display"], r"^a private Xvfb :\d+$")
            self.assertTrue(st["hasReport"])
            code, rep = self.j("/api/run/report?id=" + r["id"])
            self.assertEqual((code, rep["result"], rep["widgets"]), (200, "pass", 3))
            self.assertFalse(os.path.exists("/tmp/.X11-unix/X%s" % st["display"].rsplit(":", 1)[1]))   # (stopped with the run)
        self.assertEqual(self.j("/api/run/report?id=nope")[0], 404)
        # release_check.sh's maps: each one's result, screenshot and log kept (cache/maps), served by name
        rc = os.path.join(self.repo, "tools", "release_check.sh")
        orig = rd(rc)
        try:
            wr(rc, FAKE_RELEASE_CHECK)
            os.makedirs(os.path.join(self.repo, "maps"), exist_ok=True)
            for n in ("a", "b", "c"): wr(os.path.join(self.repo, "maps", n + ".qmap"), "version 1\n")
            code, r = self.j("/api/run", {"cmd": "script:release_check.sh"})
            self.assertEqual(wait(r["id"])["status"], "failed")
            code, m = self.j("/api/maps")
            self.assertEqual(m["known"], ["a", "b", "c"])
            self.assertEqual((m["maps"]["a"]["status"], m["maps"]["a"]["shot"], m["maps"]["a"]["log"]), ("passed", True, True))
            self.assertEqual((m["maps"]["b"]["status"], m["maps"]["b"]["rc"], m["maps"]["b"]["shot"]), ("failed", 1, False))
            self.assertNotIn("c", m["maps"])
            code, h, raw = self.req("/api/map-file?name=a&kind=shot")
            self.assertEqual((code, h["Content-Type"], raw), (200, "image/png", b"PNG"))
            self.assertEqual(self.req("/api/map-file?name=b&kind=log")[2], b"b broke\n")
            for bad in ("../x", "a/b", ""):
                self.assertEqual(self.req("/api/map-file?name=%s&kind=shot" % bad)[0], 404)
            self.assertEqual(self.j("/api/import-log", {"text": "map c ok\n"})[1]["maps"], 1)
            self.assertEqual(self.j("/api/maps")[1]["maps"]["c"]["status"], "passed")
        finally:
            wr(rc, orig)
            os.remove(game)


class Install(unittest.TestCase):
    def test_install_keeps_config_and_cache(self):
        import install
        d = tempfile.mkdtemp(prefix="tests_editor_install_")
        try:
            t = os.path.join(d, "tests-editor")
            os.makedirs(os.path.join(t, "cache"))
            self.assertEqual(install.main(["--target", t, "--quiet"]), 0)
            cfg = json.loads(rd(os.path.join(t, "tests_editor.json")))
            self.assertEqual(cfg["repo"], "../working-repo")
            with open(os.path.join(t, "tests_editor.json"), "w") as f: json.dump({"repo": "elsewhere"}, f)
            self.assertEqual(install.main(["--target", t, "--quiet"]), 0)
            self.assertEqual(json.loads(rd(os.path.join(t, "tests_editor.json")))["repo"], "elsewhere")
            self.assertTrue(os.path.isfile(os.path.join(t, "model", "cpp.py")))
            self.assertTrue(os.path.isdir(os.path.join(t, "cache")))
            self.assertFalse(os.path.exists(os.path.join(t, "model", "__pycache__")))
        finally:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=1)

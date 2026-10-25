#!/usr/bin/env python3
"""Tests for the Editor Manager (dashboard.py), standard library only:  python3 tools/editor_dashboard/test_dashboard.py
They build a throwaway Quark folder (a bare origin, repository\\ and working-repo\\ clones, editor folders) in a temp dir;
nothing outside it is read or written."""
import json, os, shutil, subprocess, sys, tempfile, types, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dashboard as D  # noqa: E402

ENV = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@x", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@x")


def git(cwd, *args):
    return subprocess.run(["git", "-C", cwd] + list(args), check=True, env=ENV, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE).stdout.decode().strip()


def write(path, text, mode="w"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode) as f: f.write(text)


class Fixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="qdash-test-")
        q = cls.quark = os.path.join(cls.tmp, "Quark")
        seed = os.path.join(cls.tmp, "seed")
        os.makedirs(seed)
        git(seed, "init", "-q", "-b", "testing")
        write(os.path.join(seed, "VERSION"), "1.2.3\n")
        write(os.path.join(seed, "tools", "sound_editor", "README.md"), "sound\n")
        write(os.path.join(seed, "tools", "provenance.py"), "print('merge: 0 updated, 0 new')\n")
        write(os.path.join(seed, "assets", "provenance", "sounds.json"), json.dumps(
            {"format": 1, "category": "sounds", "items": {"sounds/default/HIT.wav": {"origin": "ai"}, "sounds/default/JUMP.wav": {"origin": "human"}}}))
        git(seed, "add", "-A"); git(seed, "commit", "-qm", "one")
        cls.old = git(seed, "rev-parse", "HEAD")
        git(seed, "tag", "v1.2.2")
        write(os.path.join(seed, "tools", "sound_editor", "README.md"), "sound 2\n")
        git(seed, "commit", "-qam", "two")
        cls.new = git(seed, "rev-parse", "HEAD")
        git(seed, "tag", "test-1.2.3-test.7")
        bare = os.path.join(cls.tmp, "origin.git")
        subprocess.run(["git", "clone", "-q", "--bare", seed, bare], check=True, env=ENV)
        os.makedirs(q)
        for n in ("repository", "working-repo"):
            subprocess.run(["git", "clone", "-q", "-b", "testing", bare, os.path.join(q, n)], check=True, env=ENV)
        # working-repo: an unpushed commit with an override
        wr = os.path.join(q, "working-repo")
        write(os.path.join(wr, "assets", "overrides", "sounds", "default", "HIT.wav"), "RIFFlocal", "w")
        git(wr, "add", "-A"); git(wr, "commit", "-qm", "sound: HIT")
        # a clone whose origin/testing is old and which lacks testing's tip: stale
        st = os.path.join(q, "working-repo-old")
        subprocess.run(["git", "clone", "-q", "-b", "testing", seed, st], check=True, env=ENV)
        git(st, "reset", "-q", "--hard", cls.old)
        git(st, "update-ref", "refs/remotes/origin/testing", cls.old)
        for r in git(st, "for-each-ref", "--format=%(refname)").split():
            if r not in ("refs/heads/testing", "refs/remotes/origin/testing"): git(st, "update-ref", "-d", r)
        git(st, "reflog", "expire", "--expire=now", "--all"); git(st, "gc", "-q", "--prune=now")
        # editors
        write(os.path.join(q, "sound-editor", "sound-editor.html"), "<html></html>")
        write(os.path.join(q, "sound-editor", "exports", "overrides", "sounds", "default", "HIT.wav"), "RIFFlocal")
        write(os.path.join(q, "map-editor", "index.html"), "<html></html>")
        write(os.path.join(q, "map-editor", "serve.py"), "")
        write(os.path.join(q, "project-manager", "tabs.json"), json.dumps({"tabs": [
            {"id": "sound", "parent": "dashboard", "folder": "sound-editor", "file": "sound-editor.html"},
            {"id": "map", "parent": "dashboard", "url": "http://127.0.0.1:1/?embed=1&x=2", "port": 1, "folder": "map-editor"}]}))
        cls.data = os.path.join(cls.tmp, "data")
        write(os.path.join(cls.data, "overrides", "textures", "wall.png"), "png")
        write(os.path.join(cls.data, "overrides", "sounds", "default", "JUMP.wav"), "RIFF")
        cls.cfg = D.Config(types.SimpleNamespace(quark=q, repo=None, data_dir=cls.data, sidecar=os.path.join(cls.tmp, "marks.json")))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_refs_and_testing_build(self):
        r = D.repo_refs(os.path.join(self.quark, "repository"))
        self.assertEqual(r["testing_sha"], self.new)
        self.assertEqual(r["prod_tag"], "v1.2.2")
        self.assertEqual(r["testing_build"], "1.2.3-test.7")
        write(os.path.join(self.quark, "testing", "BUILD.txt"), "testing origin/testing %s-test.9 (built on this PC)\n" % self.new)
        self.assertEqual(D.local_testing_build(self.quark, self.new, "1.2.3"), "1.2.3-test.9")
        self.assertIsNone(D.local_testing_build(self.quark, self.old, "1.2.3"))

    def test_stale_clone_is_not_counted(self):
        st = os.path.join(self.quark, "working-repo-old")
        self.assertEqual(D.testing_base(st, "origin/testing", self.old, self.new), ("origin/testing", "stale"))
        wr = os.path.join(self.quark, "working-repo")
        self.assertEqual(D.testing_base(wr, "origin/testing", self.new, self.new)[1], "own")
        # an old origin/testing in a clone that has testing's tip: compared with the tip
        git(wr, "update-ref", "refs/remotes/origin/testing", self.old)
        try:
            self.assertEqual(D.testing_base(wr, "origin/testing", self.old, self.new), (self.new, "reference"))
            w = D.collect_worktree({"name": "wr", "path": wr, "role": "work"}, D.repo_refs(wr), {}, {"sha": self.new, "tp_files": {}})
            self.assertEqual([cm["s"] for cm in w["commits"]], ["sound: HIT"])   # ("two" is testing's, not local work)
        finally:
            git(wr, "update-ref", "refs/remotes/origin/testing", self.new)

    def test_export_compared_with_testing(self):
        t_idx = D.testing_overrides_index(os.path.join(self.quark, "repository"), self.new)
        self.assertEqual(t_idx, {})
        ov = (t_idx, D.repo_overrides_index(self.cfg.repo))
        f = os.path.join(self.quark, "sound-editor", "exports", "overrides", "sounds", "default", "HIT.wav")
        self.assertEqual(D.compare_to_repo(f, "sounds/default/HIT.wav", os.path.getsize(f), ov), "local")
        # in testing (same blob) -> same
        blob = D.git_blob_id(f)
        self.assertEqual(D.compare_to_repo(f, "sounds/default/HIT.wav", os.path.getsize(f), ({"sounds/default/hit": (blob, os.path.getsize(f))}, {})), "same")
        self.assertEqual(D.compare_to_repo(f, "sounds/default/HIT.wav", os.path.getsize(f), ({"sounds/default/hit": ("0" * 40, 3)}, {})), "differs")

    def test_unowned_overrides_listed(self):
        files, more, unowned = D.scan_exports(os.path.join(self.data, "overrides"), None, ({}, {}), True)
        self.assertEqual([e["path"] for e in files], ["sounds/default/JUMP.wav"])
        self.assertEqual(unowned, ["textures/wall.png"])

    def test_editor_links_from_tabs(self):
        eds = {e["folder"]: e for e in D.editor_links(self.cfg)[0]}
        self.assertEqual((eds["map-editor"]["kind"], eds["map-editor"]["url"], eds["map-editor"]["tab"]), ("server", "http://127.0.0.1:1/?x=2", "map"))
        self.assertEqual((eds["sound-editor"]["kind"], eds["sound-editor"]["url"]), ("page", "/editors/sound-editor/sound-editor.html"))
        self.assertFalse(eds["bot-editor"]["installed"])
        L = {e["folder"]: e for e in D.links(self.cfg)["editors"]}
        self.assertFalse(L["map-editor"]["up"])                         # (nothing listens on port 1)
        self.assertTrue(L["sound-editor"]["up"])

    def test_installs_view(self):
        repo = os.path.join(self.quark, "repository")
        write(os.path.join(repo, "tools", "project_manager", "installs.json"), json.dumps({"entries": [
            {"id": "sound", "sources": ["tools/sound_editor", "VERSION"], "run": "x", "check": "sound-editor\\sound-editor.html"}]}))
        cur = "+".join(git(repo, "rev-parse", "%s:%s" % (self.new, s)) for s in ("tools/sound_editor", "VERSION"))
        old = "+".join(git(repo, "rev-parse", "%s:%s" % (self.old, s)) for s in ("tools/sound_editor", "VERSION"))
        state = os.path.join(self.quark, "project-manager", "installs-state.json")
        try:
            for stamp, want in ((cur, "current"), (old, "older")):
                write(state, json.dumps({"entries": {"sound": {"stamp": stamp}}}))
                v = D.installs_view(self.cfg, repo, "origin/testing", {"origin/testing": "1.2.3"})
                self.assertEqual(v["sound-editor"]["state"], want)
            self.assertEqual(v["sound-editor"]["entry"], "sound")
        finally:
            os.remove(os.path.join(repo, "tools", "project_manager", "installs.json")); os.remove(state)

    def test_marks(self):
        reg = {"sounds/default/HIT.wav": "ai"}
        self.assertFalse(D.differs({"origin": "ai", "by": "q", "note": "x"}, {"origin": "ai"}))
        self.assertTrue(D.differs({"origin": "human"}, {"origin": "ai"}))
        self.assertTrue(D.differs({"origin": "ai"}, None))
        # a mark that repeats the registry's origin isn't written
        self.assertEqual(D.write_marks(self.cfg, ["sounds/default/HIT.wav"], "ai", "q", "", False, reg), {"marked": 0, "removed": 0, "same": 1})
        self.assertFalse(os.path.exists(self.cfg.sidecar))
        self.assertEqual(D.write_marks(self.cfg, ["sounds/default/HIT.wav"], "human", "q", "", False, reg)["marked"], 1)
        self.assertEqual(D.write_marks(self.cfg, ["sounds/default/HIT.wav", "x/y"], None, "", "", True, reg)["removed"], 1)
        self.assertEqual(D.write_marks(self.cfg, ["x/y"], None, "", "", True, reg)["removed"], 0)

    def test_decide(self):
        own = lambda o, **k: dict(k, origin=o, own=True, src="dashboard", fp="o", order=9)
        ed = lambda o, src, date, fp, order: {"origin": o, "own": False, "src": src, "date": date, "fp": fp, "order": order}
        # without the owner's mark the newest editor entry wins (and two editors that disagree are a conflict)
        a, b = ed("human", "sound-editor", "2026-10-20", "a", 0), ed("ai", "ui-customizer", "2026-10-22", "b", 1)
        win, pend, conf = D.decide([a, b], {"origin": "ai"})
        self.assertEqual((win is b, pend, conf), (True, False, True))
        # the owner's mark wins over a newer editor entry; it is settled when the mark saw that entry
        e = ed("human", "sound-editor", "2026-10-26", "e1", 0)
        m = own("ai", seen={"sound-editor": "e1"})
        self.assertEqual(D.decide([e, m], {"origin": "ai"})[1:], (False, False))
        self.assertEqual(e["st"], "over")
        e2 = dict(e, fp="e2")                                           # (a new export: a conflict again)
        self.assertEqual(D.decide([e2, own("ai", seen={"sound-editor": "e1"})], {"origin": "ai"})[1:], (False, True))

    def test_merge_settles_marks(self):
        """the round trip with the real tools/provenance.py: q overrules an editor, Merge writes q's choice once, and a
        second Merge has nothing to do (it used to flip the item back to the editor's origin)"""
        wr = self.cfg.repo
        real = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "provenance.py")
        shutil.copyfile(real, os.path.join(wr, "tools", "provenance.py"))
        side = os.path.join(self.quark, "sound-editor", "provenance.json")
        hit, jump = "sounds/default/HIT.wav", "sounds/default/JUMP.wav"
        write(side, json.dumps({"format": 1, "tool": "sound-editor", "items": {
            hit: {"origin": "human", "by": "q", "date": "2026-10-20"}, jump: {"origin": "ai", "date": "2026-10-20"},
            "textures/wall.png": {"origin": "human"}}}))
        reg = lambda: {r["id"]: r for r in D.assets_payload(self.cfg)["rows"]}
        others = lambda k: [(e["src"], e["origin"], e["fp"]) for e in D.sidecar_entries(self.cfg)[0][k] if not e["own"]]
        try:
            rows = reg()
            self.assertEqual((rows[hit]["p"][0]["origin"], rows[hit].get("pm")), ("human", 1))
            self.assertIn("textures/", rows["textures/wall.png"]["bad"])
            # q marks HIT third-party, and JUMP human (the registry's origin, but the editor says AI: written)
            self.assertEqual(D.write_marks(self.cfg, [hit], "third-party", "q", "", False, {hit: "ai"}, {hit: others(hit)})["marked"], 1)
            self.assertEqual(D.write_marks(self.cfg, [jump], "human", "q", "", False, {jump: "human"}, {jump: others(jump)})["marked"], 1)
            rows = reg()
            self.assertEqual((rows[hit]["p"][0]["origin"], rows[hit]["p"][1]["st"], rows[hit].get("c")), ("third-party", "over", None))
            self.assertNotIn("p", rows[jump])                           # (q keeps the registry's human: nothing pending)
            ok, out = D.run_merge(self.cfg)
            self.assertTrue(ok, out)
            self.assertTrue(any("skipped textures/wall.png" in l for l in out), out)
            rows = reg()
            self.assertEqual((rows[hit]["o"], rows[hit]["tool"]), ("third-party", "dashboard"))
            self.assertNotIn("p", rows[hit])
            self.assertEqual(D.run_merge(self.cfg)[1][-1], "nothing to merge")   # (no flip back to the editor's human)
            self.assertEqual(reg()[hit]["o"], "third-party")
            # the editor exports HIT again: a conflict (needs a decision), still not merged over q's mark
            doc = D.read_json(side)[0]; doc["items"][hit]["date"] = "2026-10-27"; write(side, json.dumps(doc))
            rows = reg()
            self.assertEqual((rows[hit].get("c"), rows[hit].get("pm")), (1, None))
            # the commit of the merged registry: a local commit of assets/provenance/ only
            self.assertTrue(D.registry_diff(self.cfg)["files"])
            head = git(wr, "rev-parse", "HEAD")
            ok, out = D.commit_registry(self.cfg, "provenance: test")
            self.assertTrue(ok, out)
            self.assertEqual(git(wr, "rev-parse", "HEAD~1"), head)
            self.assertEqual(git(wr, "status", "--porcelain", "--", "assets/provenance/"), "")
            self.assertIn("tools/provenance.py", git(wr, "status", "--porcelain"))   # (other changes left alone)
        finally:
            os.remove(side)
            if os.path.exists(self.cfg.sidecar): os.remove(self.cfg.sidecar)
            git(wr, "reset", "-q", "--hard", "HEAD~1") if git(wr, "log", "-1", "--format=%s") == "provenance: test" else None
            git(wr, "checkout", "-q", "--", "tools/provenance.py", "assets/provenance/")
            git(wr, "clean", "-qfd", "--", "assets/provenance/")
            if os.path.exists(D.plan_path(self.cfg)): os.remove(D.plan_path(self.cfg))

    def test_start_command(self):
        e = {"start": "py -3 serve.py --port 8769 --no-open", "port": 9373}
        self.assertEqual(D.start_command(e)[1:], ["serve.py", "--port", "9373", "--no-open"])
        self.assertEqual(D.start_command(dict(e, start="py -3 -B server.py"))[1:], ["-B", "server.py", "--port", "9373"])
        self.assertIsNone(D.start_command(dict(e, start="run.cmd")))
        self.assertIsNone(D.start_command(dict(e, start="py -3 serve.py & calc")))

    def test_program_files_are_not_exports(self):
        d = os.path.join(self.tmp, "physics-editor")
        for n in ("serve.py", "physics_editor.example.json", "physics_editor.json", "provenance.json", "scene.qmap", "README.md"):
            write(os.path.join(d, n), "x")
        files, _, _ = D.scan_exports(d, "Physics editor", ({}, {}), skip={"serve.py", "readme.md"})
        self.assertEqual([e["path"] for e in files], ["scene.qmap"])

    def test_merge_target_and_snapshot(self):
        self.assertTrue(D.merge_target(self.cfg)["ok"])                 # (working-repo has the registry)
        mirror = D.Config(types.SimpleNamespace(quark=self.quark, repo="repository", data_dir=self.data, sidecar=self.cfg.sidecar))
        self.assertFalse(D.merge_target(mirror)["ok"])
        st = D.State(self.cfg)
        st.refresh()
        s = st.summary()
        self.assertEqual(s["testing"]["build"], "1.2.3-test.7")
        self.assertEqual(s["stale_clones"], ["working-repo-old"])
        snd = next(p for p in s["programs"] if p["id"] == "sound-editor")
        self.assertEqual((snd["assets"], snd["pct_human"]), (2, 50.0))
        exp = next(p for p in st.changes["programs"] if p["id"] == "sound-editor")["exports"]
        self.assertEqual({e["path"]: e.get("repo") for e in exp}["exports/overrides/sounds/default/HIT.wav"], "local")
        # the cheap check: nothing changed -> nothing to do; a sidecar written -> the assets reload without git
        self.assertFalse(st.git_changed())
        # an uncommitted edit of an editor's file (no git command ran) is a change too; a file of no editor isn't
        wr = os.path.join(self.quark, "working-repo")
        write(os.path.join(wr, "NOTES.txt"), "not an editor's\n")
        self.assertFalse(st.git_changed())
        readme = os.path.join(wr, "tools", "sound_editor", "README.md")
        write(readme, "sound 3\n")
        try: self.assertTrue(st.git_changed())
        finally: git(wr, "checkout", "-q", "--", readme); os.remove(os.path.join(wr, "NOTES.txt")); st.refresh()
        D.write_marks(self.cfg, ["sounds/default/JUMP.wav"], "ai", "", "", False, {})
        st.watched_at = 0
        st.watch()
        self.assertTrue(any(r.get("p") for r in st.assets["rows"] if r["id"] == "sounds/default/JUMP.wav"))
        D.write_marks(self.cfg, ["sounds/default/JUMP.wav"], None, "", "", True)


if __name__ == "__main__":
    unittest.main()

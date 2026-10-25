#!/usr/bin/env python3
"""python3 test/freshness.test.py - build.freshness(): the red 'out of date' banner only for UI commits since the dump
(or, when git can't count, a VERSION difference); a VERSION-only bump is a note. git and VERSION are faked."""
import os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import build

tmp = tempfile.mkdtemp()
with open(os.path.join(tmp, 'VERSION'), 'w') as f:
    f.write('1.0.1\n')
build.REPO = tmp
dump = {'source': {'version': '1.0.0', 'commit': 'abc12345'}}


def run(ui_commits):
    def git(*args):
        if args[0] == 'rev-parse':
            return 'head0000'
        if args[0] == 'cat-file':
            return None if ui_commits is None else ''
        if args[0] == 'rev-list':
            return str(ui_commits)
    build.git = git
    return build.freshness(dump)


f = run(0)   # VERSION-only bump: not stale, a note
assert not f['stale'] and f['why'] == '' and 'only the version label differs' in f['note'], f
f = run(3)   # UI code changed: stale
assert f['stale'] and f['why'].startswith('3 commits changed the UI code') and f['note'] == '', f
f = run(None)   # git can't count: the VERSION difference decides
assert f['stale'] and f['why'] == 'the dump is 1.0.0, the game 1.0.1', f
dump['source']['version'] = '1.0.1'
f = run(0)   # same version, no commits: fresh, nothing to say
assert not f['stale'] and not f['note'], f
f = run(1)
assert f['stale'] and '(the dump is' not in f['why'], f

# install.py --extract-if-stale: extracts only for a stale dump, rebuilds after it, and a failed extract keeps the
# install successful (the app must not retry an 8-minute job on every start)
import io, contextlib, types
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'extract'))
import install
calls = []
stale = {'v': True}
build.main = lambda argv: calls.append('build') or {'freshness': {'stale': stale['v'], 'why': 'x', 'refresh': 'r'}}
fake = types.ModuleType('extract')
sys.modules['extract'] = fake
for ok, want in ((True, ['build', 'extract', 'build']), (False, ['build', 'extract'])):
    calls.clear()
    fake.main = (lambda argv: calls.append('extract')) if ok else (lambda argv: (calls.append('extract'), sys.exit('extract: cmake not found')))
    with contextlib.redirect_stdout(io.StringIO()) as out:
        rc = install.main(['--dest', os.path.join(tmp, 'dest'), '--extract-if-stale'])
    assert rc == 0 and calls == want, (ok, calls, out.getvalue())
    assert ok or 'could not regenerate the dump' in out.getvalue()
stale['v'] = False
calls.clear()
with contextlib.redirect_stdout(io.StringIO()):
    install.main(['--dest', os.path.join(tmp, 'dest'), '--extract-if-stale'])
assert calls == ['build'], calls
# a fresh dump whose screenshots are not on this PC: extracted once (they come with the extract)
build.main = lambda argv: calls.append('build') or {'freshness': {'stale': False}, 'shots': 0, 'shotsWanted': 121}
fake.main = lambda argv: calls.append('extract')
calls.clear()
with contextlib.redirect_stdout(io.StringIO()):
    install.main(['--dest', os.path.join(tmp, 'dest'), '--extract-if-stale'])
assert calls == ['build', 'extract', 'build'], calls
print('freshness tests passed (and install.py --extract-if-stale)')

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
print('freshness tests passed')

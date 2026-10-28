#!/usr/bin/env python3
# Used by autoupdate.sh (standard library only; run with python3 -I). Which GitHub release a server instance runs:
#   pick_release.py pick CHANNEL RELEASES_JSON [LATEST_JSON]
#     CHANNEL production: the newest production release (tag vX.Y.Z, not a prerelease).
#     CHANNEL testing:    the newest of those and the testing prereleases (tag test-X.Y.Z.N from 1.19.1 on,
#                         test-X.Y.Z.testing.N from 1.19.0, test-X.Y.Z-test.N before), by version: a testing server runs the newest build of either
#                         channel, never an older one. X.Y.Z.N (and X.Y.Z.testing.N) sorts above production X.Y.Z (CI keeps VERSION
#                         at the production release testing builds follow) and below the next one.
#     RELEASES_JSON is GitHub's release list (newest first); LATEST_JSON (optional) is /releases/latest, for when the
#     list holds no production release (more testing builds than one page). Prints, for autoupdate.sh:
#       tag <tag> / version <x.y.z[.N|.testing.N|-test.N]> / channel <production|testing> (what its signed manifest
#       must say) / production <tag>   the newest production release (testing only; autoupdate.sh: has it caught up?) /
#       asset <name> <state> <url>   one line per file
#     or nothing if there is no such release.
#   pick_release.py cmp A B   prints -1, 0 or 1: version A against B, as the launcher's versionCmp (releases.cpp):
#                             1.19.1.2 > 1.19.1 > 1.19.0.testing.5 > 1.19.0 > 1.19.0-test.3 > 1.19.0-test.2
#   pick_release.py version TAG   the version a tag names ("v1.2.3" -> 1.2.3, "test-1.2.3.4" -> 1.2.3.4, "test-1.2.3.testing.4" -> 1.2.3.testing.4,
#                             "test-1.2.3-test.4" -> 1.2.3-test.4)
import json
import re
import sys

PROD_TAG = re.compile(r"^v(\d+\.\d+\.\d+)$")
TEST_TAG = re.compile(r"^test-(\d+\.\d+\.\d+(?:\.\d+|\.testing\.\d+|-test\.\d+))$")


def version_of(tag):
    m = PROD_TAG.match(tag) or TEST_TAG.match(tag)
    return m.group(1) if m else ""


def key(v, width=0):
    core, _, pre = v.split("+")[0].partition("-")
    # as the launcher's splitVersion: a word in the core ("testing" in 1.19.0.testing.5) reads as 0, a missing part too
    nums = [int(x) if x.isdigit() else 0 for x in core.split(".")]
    nums = tuple(nums + [0] * (max(3, width) - len(nums)))
    if not pre:
        return (nums, 1, ())
    ids = tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split("."))
    return (nums, 0, ids)


def cmp(a, b):
    width = max(len(a.split("+")[0].partition("-")[0].split(".")), len(b.split("+")[0].partition("-")[0].split(".")))
    ka, kb = key(a, width), key(b, width)
    return (ka > kb) - (ka < kb)


def candidates(releases, channel):
    out = []
    for r in releases:
        if not isinstance(r, dict) or r.get("draft"):
            continue
        tag = str(r.get("tag_name", ""))
        if PROD_TAG.match(tag) and not r.get("prerelease"):
            out.append((version_of(tag), "production", r))
        elif channel == "testing" and TEST_TAG.match(tag) and r.get("prerelease"):
            out.append((version_of(tag), "testing", r))
    return out


def main(argv):
    if len(argv) == 4 and argv[1] == "cmp":
        print(cmp(argv[2], argv[3]))
        return 0
    if len(argv) == 3 and argv[1] == "version":
        print(version_of(argv[2]))
        return 0
    if len(argv) not in (4, 5) or argv[1] != "pick" or argv[2] not in ("production", "testing"):
        print("usage: pick_release.py pick production|testing RELEASES_JSON [LATEST_JSON] | cmp A B | version TAG", file=sys.stderr)
        return 2
    channel = argv[2]
    with open(argv[3], encoding="utf-8") as f:
        releases = json.load(f)
    if not isinstance(releases, list):
        releases = []
    cands = candidates(releases, channel)
    if not any(c[1] == "production" for c in cands) and len(argv) == 5:
        with open(argv[4], encoding="utf-8") as f:
            latest = json.load(f)
        cands += candidates([latest] if isinstance(latest, dict) else [], channel)
    if not cands:
        return 0
    best = cands[0]
    for c in cands[1:]:
        if cmp(c[0], best[0]) > 0:
            best = c
    version, chan, r = best
    print("tag", r.get("tag_name", ""))
    print("version", version)
    print("channel", chan)
    prods = [c for c in cands if c[1] == "production"]
    if channel == "testing" and prods:
        newest = prods[0]
        for c in prods[1:]:
            if cmp(c[0], newest[0]) > 0:
                newest = c
        print("production", newest[2].get("tag_name", ""))
    for a in r.get("assets", []) or []:
        name, state, url = str(a.get("name", "")), str(a.get("state", "")), str(a.get("browser_download_url", ""))
        if name and " " not in name and " " not in url:
            print("asset", name, state or "-", url or "-")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

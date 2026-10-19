#!/usr/bin/env python3
"""Bundle changelogs/*.md into site/changelog.json (newest first) so the site works without the GitHub API."""
import json, os, re, sys
root = sys.argv[1] if len(sys.argv) > 1 else "."
entries = []
for name in os.listdir(os.path.join(root, "changelogs")):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\.md", name)
    if m:
        entries.append((tuple(map(int, m.groups())), open(os.path.join(root, "changelogs", name), encoding="utf-8").read()))
entries.sort(reverse=True)
json.dump([{"version": ".".join(map(str, v)), "notes": t} for v, t in entries],
          open(os.path.join(root, "site", "changelog.json"), "w", encoding="utf-8"), indent=1)

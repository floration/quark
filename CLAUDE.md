# Working on quark

- Commit and push every change to the working branch automatically when it's done and tested; don't ask first.
- Before pushing: build (`cmake --build build -j`) and run `build/quark_tests` (headless multiplayer tests must pass).
- Client changes can be checked headless: `Xvfb :99 &` then `DISPLAY=:99 build/quark --host ffa --map arena --screenshot out.png`.
- Weapons and movement in `src/game/gamedefs.h` / `src/game/pmove.cpp` are placeholders until the owner specifies them.

## Priorities
- Performance and latency come first, above all else. Judge frame rate by 1% and 0.1% lows (frame-time spikes), not averages.
- Whenever a feature, the render pipeline, netcode or anything else can be made faster or lower-latency, do it.
- In-game F3 overlay shows avg / 1% low / 0.1% low FPS; use it (and `--screenshot` runs) to check changes.

## Releases, versions, changelogs
- Every push builds Windows (.exe), Linux and macOS packages + launchers via `.github/workflows/release.yml`, publishes GitHub release `v<VERSION>`, and deploys `site/` to https://floration.github.io/quark. Keep that workflow passing.
- Every update: bump `VERSION` (semver; started at 0.1.0) and add `changelogs/<VERSION>.md` describing what changed. The release notes, the website and the launcher all show that file.
- The launcher (`src/launcher/`) installs/updates from the release's `quark-<platform>.qpak` (made by `tools/pack.py`); keep those asset names stable.

## Maps
- One map per mode (duel=vs, crucible=duel (1v1/2v2), temple=1v1, colosseum=tourney, harbor=ffa (big), depot=bomb, sprint=race, summit=koth, railyard=payload, tug=npayload, foundry=ffa) plus arena for everything. `wallbang` brushes are dark grey and let hitscan through for half damage. The generated ones come from `tools/make_maps.py` (edit it and re-run, don't hand-edit those .qmap files). `for <mode>` in a map marks the mode it's built for; menus pick that map by default.
- `quark_tests` checks every map: spawns not in walls, preferred mode supported, bots can navigate from every spawn to every objective.

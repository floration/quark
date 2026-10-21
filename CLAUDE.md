# Working on quark

- Commit each change locally when it's done and tested, but do NOT push until the owner says to push (they batch fixes so fewer releases go out). When told to push, push all the local commits together.
- Before committing: build (`cmake --build build -j`) and run `build/quark_tests` (headless multiplayer tests must pass).
- Client changes can be checked headless: `Xvfb :99 &` then `DISPLAY=:99 build/quark --host ffa --map bastion --screenshot out.png`.
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
- Map pool (each map is built for ONE mode, its `for` line, and declares a `# setting`): ranked 1v1 maps temple (vanilla), furnace (vampire), gantry (instagib), anvil (all three); mode maps clockwork=duel, depot=bomb, summit=koth, downtown=koth, railyard=payload, canal=npayload, harbor=ffa, bastion=vs, orbital=vs, colosseum=tourney, foundry=tag, hamlet=hide, viaduct=race, lodestone=points, midtown=points (multi point: an attack down five points with staged spawns). Each map is offered for its own mode plus FFA (`modes` line, written by make_maps.py); Lodestone alone is built for every mode (not the ranked race). Any map can still be started in any mode: the menus warn it may not work. No map is named after a game mode. `wallbang` brushes are dark grey and let hitscan through for half damage. The generated ones come from `tools/make_maps.py` (edit it and re-run, don't hand-edit those .qmap files). `for <mode>` in a map marks the mode it's built for; menus pick that map by default. Why each map suits its mode and why each structure is there: `docs/MAP_STRUCTURES.md`.
- `quark_tests` checks every map: spawns not in walls, preferred mode supported, bots can navigate from every spawn to every objective, every ladder climbable, every staircase walkable and not cutting a path, and every map scoring >= 7 in decor / layout / movement / creativity / mode fit / balance (`build/quark_tests mapreport` writes `docs/MAP_REPORT.md`; metrics in `docs/MAP_METRICS.md`). Grade strictly; don't loosen a metric to pass a map.

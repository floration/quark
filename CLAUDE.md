# Working on quark

- Commit and push every change to the working branch automatically when it's done and tested; don't ask first.
- Before pushing: build (`cmake --build build -j`) and run `build/quark_tests` (headless multiplayer tests must pass).
- Client changes can be checked headless: `Xvfb :99 &` then `DISPLAY=:99 build/quark --host ffa --map arena --screenshot out.png`.
- Weapons and movement in `src/game/gamedefs.h` / `src/game/pmove.cpp` are placeholders until the owner specifies them.

## Priorities
- Performance and latency come first, above all else. Judge frame rate by 1% and 0.1% lows (frame-time spikes), not averages.
- Whenever a feature, the render pipeline, netcode or anything else can be made faster or lower-latency, do it.
- In-game F3 overlay shows avg / 1% low / 0.1% low FPS; use it (and `--screenshot` runs) to check changes.

## Releases
- Every push builds Windows (.exe), Linux and macOS packages via `.github/workflows/release.yml` and publishes them as a GitHub release. Keep that workflow passing.
